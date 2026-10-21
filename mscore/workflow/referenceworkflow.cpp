#include <QCryptographicHash>
#include <QDataStream>
// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "mscore/plugin/api/scoreobserver.h"
#include "referencefactory.h"
#include "referencebus.h"
#include "workflowcontext.h"
#include "playbackpreview.h"
#include "mscore/seq.h"
#include "mscore/shortcut.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "libmscore/repeatlist.h"
#include "libmscore/playbacktiming.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/tie.h"
#include "libmscore/pedal.h"
#include "libmscore/spannermap.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QAction>
#include <QSignalBlocker>
#include <cmath>
#include <algorithm>
namespace Ms { namespace PluginAPI {
QVariantMap ScoreObserver::setReferenceTracks(const QVariantList& tracks){
 auto project=referenceProject();QVariantMap result;QMetaObject::invokeMethod(project,"setTracks",Q_RETURN_ARG(QVariantMap,result),Q_ARG(QVariantList,tracks));if(result.value("ok").toBool())workflowContext()->setReferenceScore(_score);return result;
}
QVariantMap ScoreObserver::setEditorComparison(const QVariantMap& data){
 if(data.isEmpty()){workflowContext()->setExpression(nullptr,{});return {{"ok",true}};}
 if(!_score||data.value("revision").toString()!=analysisRevision())return {{"error",tr("乐谱已经改变，请重新读取对应。")}};
 if(data.value("result").toMap().value("matches").toList().size()>100000)return {{"error",tr("对应过多，请分段查看。")}};
 workflowContext()->setExpression(_score,data);return {{"ok",true}};
}
QVariantMap ScoreObserver::updateEditorComparisonState(const QVariantMap& data){
 auto context=workflowContext();const auto previous=context->expression();
 if(!_score||!context->current(_score)||data.value("owner")!=previous.value("owner")||data.value("revision")!=previous.value("revision"))return {{"error",tr("重新打开演奏对比后可以继续选择音符。")}};
 QVariantMap state;for(const char* name:{"selectedScoreId","selectedMidiId","busy","previewCanUndo","previewCanRedo","applyCounts"})if(data.contains(name))state[name]=data.value(name);
 context->updateExpression(state);return {{"ok",true}};
}
namespace {
double audioAt(const QVariantMap& a,int tick){
 QVariantList points=a.value("anchors").toList();points.prepend(QVariantMap{{"tick",a.value("start")},{"ms",a.value("from")}});points.append(QVariantMap{{"tick",a.value("end")},{"ms",a.value("until")}});
 for(int i=1;i<points.size();++i){auto p=points[i-1].toMap(),q=points[i].toMap();if(tick<=q.value("tick").toInt())return p.value("ms").toDouble()+(tick-p.value("tick").toInt())*(q.value("ms").toDouble()-p.value("ms").toDouble())/(q.value("tick").toInt()-p.value("tick").toInt());}return a.value("until").toDouble();
}
}
QString ScoreObserver::referenceStructureKey() const {
 if(!_score)return {};
 QByteArray bytes;QDataStream out(&bytes,QIODevice::WriteOnly);
 for(auto measure=_score->firstMeasure();measure;measure=measure->nextMeasure())
  out<<measure->tick().ticks()<<measure->ticks().ticks()<<measure->timesig().numerator()<<measure->timesig().denominator();
 return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
QVariantMap ScoreObserver::referencePlan(const QVariantMap& request){
 if(!_score||playing()||(seq&&!seq->backgroundRenderingIdle()))return {{"error",tr("请先停止乐谱播放，等待声音准备完成。")}};
 if(request.value("revision").toString()!=analysisRevision())return {{"error",tr("乐谱已经改变，请重新选择关联。")}};
 auto master=_score->masterScore();
 const int start=request.value("start",-1).toInt(),end=request.value("end",-1).toInt(),occurrence=request.value("occurrence",1).toInt(),mode=request.value("mode",0).toInt();
 if(start<0||end<=start||mode<0||mode>2)return {{"error",tr("请选择有效的谱段和播放方式。")}};
 int seen=0,from=-1,until=-1;for(auto r:master->repeatList()){
  if(from<0&&start>=r->tick&&start<r->tick+r->len()&&++seen==occurrence)from=r->utick+start-r->tick;
  if(from>=0&&end>=r->tick&&end<=r->tick+r->len()&&r->utick+end-r->tick>from){until=r->utick+end-r->tick;break;}
 }
 if(from<0||until<0||until-from!=end-start)return {{"error",tr("片段跨过了反复跳转。请拆成几段，并分别选择播放第几遍。")}};
 const double origin=PlaybackTiming::time(master,from);
 auto seconds=[&](int tick){return PlaybackTiming::time(master,from+tick-start);};
 QVariantList clockValues;QVector<QPair<double,double>> clock;
 auto leader=request.value("leader").toMap();const double speed=request.value("speed",1).toDouble();
 if(!std::isfinite(speed)||speed<.5||speed>1.5)return {{"error",tr("播放速度应在 50–150% 之间。")}};
 if(mode==2){if(leader.value("start").toInt()>start||leader.value("end").toInt()<end||leader.value("stale").toBool())return {{"error",tr("选定音频的关联应覆盖整段乐谱，并已经核对。")}};
  QVector<int> ticks{start,end};for(auto v:leader.value("anchors").toList()){int t=v.toMap().value("tick").toInt();if(t>start&&t<end)ticks.append(t);}std::sort(ticks.begin(),ticks.end());
  for(int tick:ticks){double normal=seconds(tick),output=origin+(audioAt(leader,tick)-audioAt(leader,start))/1000/speed;clock.append({normal,output});clockValues.append(QVariantMap{{"score",normal},{"output",output}});}
 }
 auto outputAt=[&](int tick){return mode==2?origin+(audioAt(leader,tick)-audioAt(leader,start))/1000/speed:seconds(tick);};
 QVariantList clips;bool solo=false;for(auto value:request.value("tracks").toList())if(value.toMap().value("solo").toBool())solo=true;
 for(auto value:request.value("tracks").toList()){
  auto track=value.toMap();if(track.value("mute").toBool()||!track.value("participate",true).toBool()||(solo&&!track.value("solo").toBool()))continue;
  QVariantList pieces;
  const double trackSpeed=mode==0?track.value("speed",speed).toDouble():speed;if(!std::isfinite(trackSpeed)||trackSpeed<.5||trackSpeed>1.5)return {{"error",tr("音轨速度应在 50–150% 之间。")}};
  auto associations=track.value("associations").toList();
  std::sort(associations.begin(),associations.end(),[](const QVariant& a,const QVariant& b){return a.toMap().value("start").toInt()<b.toMap().value("start").toInt();});
  int previousEnd=-1;double previousOutputEnd=0;
  for(auto av:associations){
   auto a=av.toMap();if(!a.value("active",true).toBool()||a.value("occurrence",1).toInt()!=occurrence||a.value("end").toInt()<=start||a.value("start").toInt()>=end)continue;
   if(a.value("stale").toBool())return {{"error",tr("音轨“%1”的关联需要重新核对。").arg(track.value("name").toString())}};
   const int first=qMax(start,a.value("start").toInt()),last=qMin(end,a.value("end").toInt());
   if(previousEnd>first)return {{"error",tr("音轨“%1”在同一谱位选了多个片段。请在关联列表中选择本次使用的一条；需要叠加时可放入另一音轨。").arg(track.value("name").toString())}};
   const double independentAt=previousEnd<0?(seconds(first)-origin)*1000:previousOutputEnd+(seconds(first)-seconds(previousEnd))*1000;
   QVector<int> ticks{first,last};
   if(mode!=0){for(int t=first+120;t<last;t+=120)ticks.append(t);for(auto v:a.value("anchors").toList()){int t=v.toMap().value("tick").toInt();if(t>first&&t<last)ticks.append(t);}if(mode==2)for(auto v:leader.value("anchors").toList()){int t=v.toMap().value("tick").toInt();if(t>first&&t<last)ticks.append(t);}}
   std::sort(ticks.begin(),ticks.end());ticks.erase(std::unique(ticks.begin(),ticks.end()),ticks.end());
   for(int i=1;i<ticks.size();++i){int p=ticks[i-1],q=ticks[i];double audioFrom=audioAt(a,p),audioUntil=audioAt(a,q),at=mode==0?independentAt:(outputAt(p)-origin)*1000,duration=mode==0?(audioUntil-audioFrom)/trackSpeed:(outputAt(q)-outputAt(p))*1000;
    if(duration<=0)return {{"error",tr("对齐点顺序不正确。")}};
    const double required=(audioUntil-audioFrom)/duration;
    if(!std::isfinite(required)||required<.499999||required>1.500001)return {{"error",tr("“%1”的这段录音需要 %2% 速度。当前范围是 50–150%；调整片段或谱段长度后可再试，也可选“开头对齐，各自播放”。").arg(track.value("name").toString(),QString::number(required*100.,'f',1))},{"requiredSpeed",required},{"track",track.value("id")}};
    pieces.append(QVariantMap{{"from",audioFrom},{"until",audioUntil},{"at",at},{"duration",duration},{"fade",request.value("fade",true)}});
    previousOutputEnd=at+duration;
   }
   previousEnd=last;
  }
  std::sort(pieces.begin(),pieces.end(),[](const QVariant& a,const QVariant& b){return a.toMap().value("at").toDouble()<b.toMap().value("at").toDouble();});
  QVariantList merged;
  for(auto value:pieces){auto piece=value.toMap();if(!merged.isEmpty()){auto previous=merged.back().toMap();double endAt=previous.value("at").toDouble()+previous.value("duration").toDouble();double previousSpeed=(previous.value("until").toDouble()-previous.value("from").toDouble())/previous.value("duration").toDouble(),nextSpeed=(piece.value("until").toDouble()-piece.value("from").toDouble())/piece.value("duration").toDouble();
    if(std::abs(endAt-piece.value("at").toDouble())<.01&&std::abs(previous.value("until").toDouble()-piece.value("from").toDouble())<.01&&std::abs(previousSpeed-nextSpeed)<.00001){previous["until"]=piece.value("until");previous["duration"]=previous.value("duration").toDouble()+piece.value("duration").toDouble();merged.back()=previous;continue;}}
   merged.append(piece);
  }
  if(!merged.isEmpty())clips.append(QVariantMap{{"id",track.value("id")},{"path",track.value("path")},{"identity",track.value("identity")},{"volume",track.value("volume",1.)},{"pieces",merged}});
 }
 if(clips.isEmpty())return {{"error",tr("这个谱段没有选中可播放的音频关联。")}};
 int context=start;auto measure=_score->tick2measure(Fraction::fromTicks(start));
 if(measure&&measure->prevMeasure())context=measure->prevMeasure()->tick().ticks();
 // A held voice can have no new chord at the selected segment in another staff.
 for(int track=0;track<_score->ntracks();++track){
  auto chord=_score->findCR(Fraction::fromTicks(start),track);
  if(!chord||!chord->isChord()||chord->endTick().ticks()<=start)continue;
  for(auto note:toChord(chord)->notes()){
   QSet<Ms::Note*> visited;
   while(note->tieBack()&&note->tieBack()->startNote()&&!visited.contains(note)){visited.insert(note);note=note->tieBack()->startNote();}
   context=qMin(context,note->chord()->tick().ticks());
  }
 }
 for(auto pair:_score->spannerMap().map()){auto line=pair.second;if(line->isPedal()&&line->tick().ticks()<start&&line->tick2().ticks()>start)context=qMin(context,line->tick().ticks());}
 int launch=qMax(0,from-(start-context));
 return {{"ok",true},{"revision",analysisRevision()},{"clips",clips},{"clock",clockValues},{"joint",true},{"volume",request.value("volume",.8)},{"fromUtick",from},{"contextUtick",launch},{"startUtick",from},{"untilUtick",until},{"startFrame",int(origin*MScore::sampleRate)},{"actualStart",start},{"start",start},{"end",end},{"mode",mode}};
}
QVariantMap ScoreObserver::startReferencePlayback(const QVariantMap& plan){
 delete _workflowRepeat.data();_workflowRepeat.clear();
 if(!plan.value("usesPreview").toBool())stopPlaybackPreview();
 if(!_score||!mscore||mscore->currentScore()!=_score||plan.value("revision").toString()!=analysisRevision())return {{"error",tr("请回到关联的乐谱，重新准备播放。")}};
 if(!seq||!seq->driver()||seq->isPlaying()||!seq->backgroundRenderingIdle())return {{"error",tr("请停止播放并检查声音设备。")}};
 const bool preview=plan.value("usesPreview").toBool();
 const int from=(preview?plan.value("fromUtick",-1):plan.value("startUtick",plan.value("fromUtick",-1))).toInt(),end=plan.value("untilUtick",-1).toInt();
 const int context=preview?-1:plan.value("contextUtick",plan.value("fromUtick",from)).toInt();
 if(from<0||end<=from||end>_score->masterScore()->repeatList().ticks()||context>from||context < -1)return {{"error",tr("谱段范围不正确，请重新准备播放。")}};
 QVector<QPair<double,double>> clock;for(auto v:plan.value("clock").toList()){auto p=v.toMap();double a=p.value("score").toDouble(),b=p.value("output").toDouble();if(!std::isfinite(a)||!std::isfinite(b)||(!clock.isEmpty()&&(a<=clock.back().first||b<=clock.back().second)))return {{"error",tr("时间对应不是连续向前的，请重新设置对齐点。")}};clock.append({a,b});}
 workflowContext()->finishPlaybackSession();seq->synchronizeStoppedGui();
 auto project=referenceProject();if(!project->property("available").toBool())return {{"error",project->property("status")}};
 auto audioPlan=plan;audioPlan["loop"]=false;QVariantMap result;QMetaObject::invokeMethod(project,"start",Q_RETURN_ARG(QVariantMap,result),Q_ARG(QVariantMap,audioPlan));if(result.contains("error"))return result;project->setProperty("repeatSelection",plan.value("loop",false));
 setReferenceClock(clock);setReferenceEndTick(end);
 auto session=new QObject(this);workflowContext()->setPlaybackSession(session);auto task=new QTimer(session);task->setInterval(20);auto guarded=QPointer<Ms::Score>(_score);auto state=_score->masterScore()->state();auto startWait=std::make_shared<QElapsedTimer>();auto heard=std::make_shared<bool>(false);auto started=std::make_shared<bool>(false),natural=std::make_shared<bool>(false),finished=std::make_shared<bool>(false);const bool usesPreview=plan.value("usesPreview").toBool();
 struct SavedOption {QPointer<QAction> action;bool before=false,touched=false;};
 auto options=std::make_shared<QVector<SavedOption>>();
 if(!usesPreview)for(const char* name:{"countin","loop"})if(auto action=getAction(name)){
  options->append({action,action->isChecked(),false});action->setChecked(false);const int index=options->size()-1;
  connect(action,&QAction::toggled,session,[options,index](bool){(*options)[index].touched=true;});
 }
 connect(session,&QObject::destroyed,project,[project,guarded,options,finished]{*finished=true;QMetaObject::invokeMethod(project,"stop");if(seq&&guarded&&seq->score()==guarded->masterScore()&&seq->isPlaying())seq->stopWait();setReferenceClock({});setReferenceEndTick(-1);for(const auto& option:*options)if(option.action&&!option.touched)option.action->setChecked(option.before);});
 auto complete=[this,session,plan,finished,usesPreview,project]{
  if(*finished)return;*finished=true;
  std::function<void()> restart;
  if(project->property("repeatSelection").toBool())restart=usesPreview?playbackPreviewRepeat():std::function<void()>([this,plan,project]{if(!project->property("repeatSelection").toBool())return;auto next=plan;next["loop"]=true;next["fromUtick"]=plan.value("startUtick",plan.value("fromUtick"));auto result=startReferencePlayback(next);if(result.contains("error"))emit workflowPlaybackError(result.value("error").toString());});
  session->deleteLater();
  if(restart){auto pending=new QObject(this);_workflowRepeat=pending;QTimer::singleShot(40,pending,[this,pending,restart]{_workflowRepeat.clear();pending->deleteLater();restart();});}
 };
 auto position=[usesPreview,end](){if(referenceReachedTick()==end)return end;return usesPreview?workflowContext()->audition().value("tick",-1).toInt():(seq?seq->getCurTick():-1);};
 connect(task,&QTimer::timeout,this,[this,session,guarded,state,project,from,context,end,started,natural,finished,complete,position,heard,startWait]{
  if(*finished)return;
  if(!guarded||guarded->masterScore()->state()!=state||mscore->currentScore()!=guarded){session->deleteLater();return;}
  const bool audio=project->property("playing").toBool();
  if(!*started){if(!audio){session->deleteLater();return;}if(project->property("prepared").toBool()){*started=true;startWait->start();seq->setScoreView(mscore->currentScoreView());if(auto play=getAction("play")){QSignalBlocker block(play);play->setChecked(true);}seq->startAtUTick(from,context);}return;}
  const bool music=seq&&seq->isPlaying();if(music)*heard=true;
  if(!music&&!*heard){if(startWait->elapsed()>5000){emit workflowPlaybackError(tr("乐谱播放尚未开始，请检查声音设备后再试。"));session->deleteLater();}return;}
  if(music)project->setProperty("scoreTick",guarded->masterScore()->repeatList().utick2tick(seq->getCurTick()));
  else if(position()>=end-1)*natural=true;
  const bool audioEnded=!audio&&project->property("completed").toBool();
  if((!music&&!*natural)||(!audio&&!audioEnded)){session->deleteLater();return;}
  if(!music&&audioEnded)complete();

 });task->start();
 connect(seq,&Seq::started,session,[heard]{*heard=true;});
 connect(seq,&Seq::heartBeat,session,[guarded,end,natural](int,int tick,int){if(guarded&&seq->score()==guarded->masterScore()&&tick>=end){*natural=true;seq->stop();seq->stopNotes();}});
 connect(seq,&Seq::stopped,session,[session,natural,finished,end,position]{if(*finished)return;if(position()>=end-1)*natural=true;if(!*natural)session->deleteLater();});
 connect(this,&ScoreObserver::scoreChanged,session,[session,project]{QMetaObject::invokeMethod(project,"stop");if(seq&&seq->isPlaying())seq->stop();setReferenceClock({});session->deleteLater();});
 return {{"ok",true},{"preparing",true}};
}
QVariantMap ScoreObserver::stopReferencePlayback(){delete _workflowRepeat.data();_workflowRepeat.clear();workflowContext()->finishPlaybackSession();auto p=referenceProject();QMetaObject::invokeMethod(p,"stop");if(seq&&seq->isPlaying())seq->stopWait();setReferenceClock({});setReferenceEndTick(-1);return {{"ok",true}};}
}}
