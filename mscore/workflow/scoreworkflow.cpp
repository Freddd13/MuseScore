// SPDX-License-Identifier: GPL-2.0-or-later
#include "mscore/plugin/api/scoreobserver.h"
#include "midiinput.h"
#include "workflowcontext.h"
#include "expressiontempo.h"
#include "libmscore/fermata.h"
#include "playbackpreview.h"
#include "positionhint.h"
#include "mscore/seq.h"
#include "mscore/scoreview.h"
#include "mscore/performanceeditor/performanceeditor.h"
#include "mscore/musescore.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/notevelocity.h"
#include "mscore/performanceeditor/markvelocity.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/tempotext.h"
#include "libmscore/tempo.h"
#include "libmscore/tempoexpression.h"
#include "libmscore/repeatlist.h"
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <QUrl>
#include <QCache>
#include <QPointer>
#include <QSet>
#include <QAction>
#include <QSignalBlocker>
#include "mscore/shortcut.h"
#include "libmscore/spannermap.h"
#include "libmscore/spanner.h"
#include "libmscore/tie.h"
#include <QSaveFile>
#include <QFile>
#include <QCryptographicHash>
#include "libmscore/textline.h"
namespace Ms { namespace PluginAPI {
void ScoreObserver::cancelPerformanceMidi(){if(_midiCancel)*_midiCancel=true;}
int ScoreObserver::readPerformanceMidi(const QString& path) {
 if(_midiCancel)*_midiCancel=true;
 auto cancel=std::make_shared<std::atomic_bool>(false);_midiCancel=cancel;
 auto watcher=new QFutureWatcher<QVariantMap>(this);int token=++_midiToken;
 connect(watcher,&QFutureWatcher<QVariantMap>::finished,this,[this,watcher,token,cancel]{
  if(!*cancel)emit performanceMidiReady(token,watcher->result());watcher->deleteLater();
 });
 watcher->setFuture(QtConcurrent::run([path,cancel]{return Ms::readPerformanceMidi(path,*cancel);}));
 return token;
}
QVariantMap ScoreObserver::savePerformanceSource(const QString& source,const QString& destination,const QString& expected) {
 auto local=[](const QString& p){return QUrl(p).isLocalFile()?QUrl(p).toLocalFile():p;};
 QFile input(local(source));if(!input.open(QIODevice::ReadOnly)||input.size()>32*1024*1024)return {{"error","MIDI source unavailable"}};
 auto bytes=input.readAll();auto hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
 if(hash!=expected)return {{"error","MIDI changed; import it again"}};
 const QString path=local(destination);
 if(QFile::exists(path)){QFile existing(path);if(existing.open(QIODevice::ReadOnly)&&existing.readAll()==bytes)return {{"ok",true}};return {{"error","destination already contains different data; choose another workspace"}};}
 QSaveFile output(path);if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())return {{"error","cannot save raw MIDI; previous file preserved"}};
 return {{"ok",true}};
}
QVariantMap ScoreObserver::stopWorkflowPlayback() {
 delete _workflowRepeat.data();_workflowRepeat.clear();
 stopPlaybackPreview();workflowContext()->finishPlaybackSession();
 if(seq&&seq->isPlaying())seq->stopWait();
 if(seq&&!seq->isPlaying())seq->synchronizeStoppedGui();
 if(seq&&(!seq->backgroundRenderingIdle()||seq->isPlaying()))return {{"error","waiting for stop/render"}};
 return {{"ok",true}};
}
QVariantMap ScoreObserver::workflowStatus() {
 if(!_score)return {{"error","no-score"}};auto master=_score->masterScore();
 return {{"revision",analysisRevision()},{"file",master->fileInfo()->absoluteFilePath()},{"name",master->name()},{"dirty",master->dirty()},{"readOnly",master->readOnly()},{"scoreSession",workflowScoreSession(master)}};
}
QVariantMap ScoreObserver::workflowInfo() {
 if(!_score)return {{"error","no-score"}};
 if(playing() || (seq&&!seq->backgroundRenderingIdle()))return {{"error","playing-or-rendering"}};
 auto master=_score->masterScore();master->updateVelo();if(_score!=master)_score->updateVelo();
 QVariantList repeats;for(auto r:master->repeatList())repeats.append(QVariantMap{{"tick",r->tick},{"end",r->tick+r->len()},{"utick",r->utick},{"seconds",r->utime},{"occurrence",r->playbackCount}});
 return {{"revision",analysisRevision()},{"file",master->fileInfo()->absoluteFilePath()},{"name",master->name()},{"dirty",master->dirty()},{"readOnly",master->readOnly()},{"repeats",repeats},{"velocityWriteModes",true},{"scoreSession",workflowScoreSession(master)}};
}
QVariantMap ScoreObserver::performanceRange(int start,int end,int firstTrack,int endTrack,int fromTick,int limit,const QString& expected) {
 QElapsedTimer work;work.start();
 // Bounded cache shared by independent workflow observers, invalidated by native content state.
 struct Cache {QPointer<Ms::Score> score;ScoreContentState state;int method=-1;TempoMap clock;QVector<QPair<int,int>> curves;MarkVelocityText marks;QMap<int,double> expressionTempos;QCache<QString,QVariantMap> pages{64};};static Cache cache;
 const QString revision=analysisRevision();
 if(!_score||(!expected.isEmpty()&&revision!=expected)||playing())return {{"error","stale-or-playing"}};
 if(seq&&!seq->backgroundRenderingIdle())return {{"error","rendering"}};
 const int method=mscore?mscore->synthesizerState().method():1;
 const auto state=_score->masterScore()->state();
 if(cache.score!=_score||cache.state!=state||cache.method!=method){cache.score=_score;cache.state=state;cache.method=method;cache.pages.clear();cache.clock=*_score->tempomap();cache.clock.setRelTempo(1.0);cache.marks=readMarkVelocityText(_score);cache.expressionTempos=ExpressionTempo::read(_score->masterScore());cache.curves.clear();for(auto curve:TempoExpression::curves(_score->masterScore()))cache.curves.append({curve->tick().ticks(),curve->tick2().ticks()});}
 QString key=QString("%1:%2:%3:%4:%5:%6").arg(start).arg(end).arg(firstTrack).arg(endTrack).arg(fromTick).arg(limit);
 if(auto saved=cache.pages.object(key)){auto value=*saved;value.insert("revision",revision);value.insert("milliseconds",double(work.nsecsElapsed())/1000000);return value;}
 auto page=analysisRange(start,end,firstTrack,endTrack,fromTick,limit,expected);
 if(page.contains("error"))return page;
 const auto& scoreClock=cache.clock;
 QVariantList notes;
 for(auto v:page.value("notes").toList()){
  auto n=v.toMap();auto note=resolve(n);if(!note)continue;
  const bool grace=n.value("grace").toBool();
  if(grace){const auto id=QString("g:%1:%2:%3:%4").arg(n.value("tick").toInt()).arg(n.value("track").toInt()).arg(n.value("graceIndex").toInt()).arg(n.value("index").toInt());n.insert("id",id);n.insert("logicalId",id);n.insert("tieBack",false);}
  const int base=n.value("velocityBase").toInt();
  n.insert("scoreSeconds",scoreClock.tick2time(n.value("tick").toInt()));
  n.insert("velocityMark",markVelocityAt(cache.marks,note->track(),n.value("tick").toInt()));
  n.insert("velocityType",int(note->veloType()));n.insert("velocityRaw",note->veloOffset());n.insert("velocityBase",base);
  n.insert("velocity",NoteVelocity::effective(base,note->veloType(),note->veloOffset()));
  bool manual=grace||note->veloType()!=Ms::Note::ValueType::OFFSET_VAL||note->veloOffset()!=0||note->chord()->playEventType()==PlayEventType::User;
  n.insert("protected",manual);n.insert("protection",grace?"grace / ornament":manual?"manual velocity / play events":"");
  bool tempoProtected=grace;
  auto segment=note->chord()->segment();for(auto e:segment->annotations())if(e->isTempoText()&&!ExpressionTempo::owns(toTempoText(e),cache.expressionTempos))tempoProtected=true;
  for(auto curve:cache.curves)if(n.value("tick").toInt()>=curve.first&&n.value("tick").toInt()<curve.second)tempoProtected=true;
  n.insert("tempoProtected",tempoProtected);n.insert("bpm",_score->tempomap()->tempo(n.value("tick").toInt())*60);
  notes.append(n);
 }
 // Return the written clock and protection data alongside this bounded page.
 auto master=_score->masterScore();const int until=page.value("done").toBool()?end:page.value("nextTick").toInt();
 QVariantList points,protectedTicks,ranges,restore,writable;
 auto clockPoint=[&](int tick){return QVariantMap{{"tick",tick},{"bpm",scoreClock.tempo(tick)*60},{"seconds",double(scoreClock.tick2time(tick))}};};
 auto restoreBefore=[&](int tick){
    auto segment=master->tick2leftSegment(Fraction::fromTicks(tick));
    while(segment&&(segment->tick().ticks()>=tick||!segment->isChordRestType()))segment=segment->prev1(SegmentType::ChordRest);
    if(segment&&segment->tick().ticks()>=start)restore.append(clockPoint(segment->tick().ticks()));
 };
 if(fromTick<=start){
    points.append(clockPoint(start));
    for(auto curve:cache.curves)if(curve.first<end&&curve.second>start){ranges.append(QVariantMap{{"start",curve.first},{"end",curve.second},{"reason",tr("沿用谱上已有的渐慢或渐快。")}});restoreBefore(curve.first);}
 }
 for(auto it=scoreClock.lower_bound(fromTick);it!=scoreClock.end()&&it->first<until;++it){
    auto value=clockPoint(it->first);value["pause"]=it->second.pause;value["fixed"]=bool(it->second.type&(TempoType::FIX|TempoType::RAMP));
    value["owned"]=ExpressionTempo::owns(master,it->first,cache.expressionTempos);points.append(value);
    if(it->second.pause>0)restoreBefore(it->first);
 }
 if(page.value("done").toBool()){auto value=clockPoint(end);auto at=scoreClock.find(end);if(at!=scoreClock.end()){value["pause"]=at->second.pause;value["fixed"]=bool(at->second.type&(TempoType::FIX|TempoType::RAMP));}points.append(value);}
 for(auto segment=master->tick2rightSegment(Fraction::fromTicks(fromTick));segment&&segment->tick().ticks()<until;segment=segment->next1(SegmentType::ChordRest)){
    if(!segment->isChordRestType())continue;const int tick=segment->tick().ticks();writable.append(tick);
    for(auto element:segment->annotations()){
       if(element->isTempoText()&&!ExpressionTempo::owns(toTempoText(element),cache.expressionTempos)){
          protectedTicks.append(QVariantMap{{"tick",tick},{"reason",tr("沿用谱上已有的速度记号。")}});restoreBefore(tick);
       }else if(element->isFermata()&&toFermata(element)->play()&&std::abs(toFermata(element)->timeStretch()-1)>.00001){
          ranges.append(QVariantMap{{"start",tick},{"end",tick+segment->ticks().ticks()},{"reason",tr("沿用延长记号的播放时长。")}});restoreBefore(tick);
       }
    }
 }
 page["tempoContext"]=QVariantMap{{"start",start},{"end",end},{"scoreEnd",master->lastMeasure()->endTick().ticks()},
    {"startSeconds",double(scoreClock.tick2time(start))},{"endSeconds",double(scoreClock.tick2time(end))},
    {"endWritable",bool(master->tick2segment(Fraction::fromTicks(end),false,SegmentType::ChordRest))},
    {"points",points},{"protectedTicks",protectedTicks},{"protectedRanges",ranges},{"restoreTicks",restore},{"writableTicks",writable}};
 page.insert("notes",notes);page.insert("milliseconds",double(work.nsecsElapsed())/1000000);cache.pages.insert(key,new QVariantMap(page));return page;
}
QVariantMap ScoreObserver::navigateWorkflow(const QVariantMap& range,bool audition) {
 if(!_score||!mscore||!mscore->currentScoreView()||mscore->currentScoreView()->score()!=_score)return {{"error","select the requested score tab first"}};
 if(seq&&seq->isPlaying()){seq->stopWait();if(seq->isPlaying())return {{"error","waiting for playback to stop"}};}
 auto view=mscore->currentScoreView();int tick=range.value("start",-1).toInt(),track=range.value("firstTrack",0).toInt();
 auto segment=_score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);
 if(!segment||track<0||track>=_score->ntracks())return {{"error","invalid range"}};
 auto element=segment->element(track);if(!element)for(int t=track;t<_score->ntracks()&&!element;++t)element=segment->element(t);
 if(range.contains("index")){auto note=resolve(range);if(!note)return {{"error","stale note; read the score again"}};element=note;}
 if(element){_score->select(element,SelectType::SINGLE,track/VOICES);view->adjustCanvasPosition(element,false);}
 view->moveCursor(Fraction::fromTicks(tick));
 if(auto editor=mscore->findChild<PerformanceEditor*>())editor->revealTick(tick);
 if(!audition)showWorkflowPositionHint(view,range);
 if(audition){
  if(!seq||!seq->driver())return {{"error","audio driver unavailable; the independent copy is open"}};
  if(!seq->backgroundRenderingIdle())return {{"error","waiting for render"}};
  auto master=_score->masterScore();int end=range.value("end",-1).toInt(),occurrence=range.value("occurrence",1).toInt();
  if(end<=tick)return {{"error","invalid audition range"}};
  int context=tick;auto measure=_score->tick2measure(Fraction::fromTicks(tick));
  if(measure&&measure->prevMeasure())context=measure->prevMeasure()->tick().ticks();
  // Incoming ties and pedal need real entry context rather than an artificial cut.
  if(segment)for(int t=0;t<_score->ntracks();++t){auto e=segment->element(t);if(!e||!e->isChord())continue;for(auto n:toChord(e)->notes()){QSet<Ms::Note*> seen;while(n->tieBack()&&n->tieBack()->startNote()&&!seen.contains(n)){seen.insert(n);n=n->tieBack()->startNote();context=qMin(context,n->chord()->tick().ticks());}}}
  for(auto pair:_score->spannerMap().map()){auto span=pair.second;if(span->isPedal()&&span->tick().ticks()<tick&&span->tick2().ticks()>tick)context=qMin(context,span->tick().ticks());}
  const auto& repeats=master->repeatList();int seen=0,utick=-1,endUtick=-1;
  for(auto r:repeats){
   if(utick<0&&tick>=r->tick&&tick<r->tick+r->len()&&++seen==qMax(1,occurrence))utick=r->utick+tick-r->tick;
   if(utick>=0&&end>=r->tick&&end<=r->tick+r->len()&&r->utick+end-r->tick>utick){endUtick=r->utick+end-r->tick;break;}
  }
  if(utick<0||endUtick<0)return {{"error","requested repeat occurrence/range unavailable"}};
  int fromUtick=qMax(0,utick-(tick-context));int actualContext=repeats.utick2tick(fromUtick);
  workflowContext()->finishPlaybackSession();seq->synchronizeStoppedGui();auto session=new QObject(this);workflowContext()->setPlaybackSession(session);
  auto loop=getAction("loop");bool oldLoop=loop&&loop->isChecked();
  if(loop)loop->setChecked(false);
  auto touched=std::make_shared<bool>(false);
  if(loop)connect(loop,&QAction::toggled,session,[touched](bool){*touched=true;});
  auto guarded=QPointer<MasterScore>(master);auto loopGuard=QPointer<QAction>(loop);
  connect(session,&QObject::destroyed,seq,[guarded,loopGuard,oldLoop,touched]{
   // destroyed() fires before QObject data is released.
   if(loopGuard&&!*touched)loopGuard->setChecked(oldLoop);
   if(seq&&guarded&&seq->score()==guarded&&seq->isPlaying())seq->stop();
  });
  connect(seq,&Seq::heartBeat,session,[session,guarded,endUtick](int,int current,int){
   if(!guarded||seq->score()!=guarded){session->deleteLater();return;}
   if(current>=endUtick){seq->stop();seq->stopNotes();session->deleteLater();}
  });
  connect(seq,&Seq::stopped,session,[session]{session->deleteLater();});
  seq->setScoreView(view);
  if(auto play=getAction("play")){QSignalBlocker block(play);play->setChecked(true);}
  seq->startAtUTick(fromUtick);
  return {{"ok",true},{"start",actualContext},{"end",end},{"occurrence",occurrence},{"fromUtick",fromUtick},{"untilUtick",endUtick},{"stopPrecision","GUI heartbeat"}};
 }
 return {{"ok",true},{"start",tick}};
}
}}
