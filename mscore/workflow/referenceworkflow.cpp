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
  for(auto av:track.value("associations").toList()){
   auto a=av.toMap();if(!a.value("active",true).toBool()||a.value("occurrence",1).toInt()!=occurrence||a.value("end").toInt()<=start||a.value("start").toInt()>=end)continue;
   if(a.value("stale").toBool())return {{"error",tr("音轨“%1”的关联需要重新核对。").arg(track.value("name").toString())}};
   const int first=qMax(start,a.value("start").toInt()),last=qMin(end,a.value("end").toInt());QVector<int> ticks{first,last};
   if(mode!=0){for(int t=first+120;t<last;t+=120)ticks.append(t);for(auto v:a.value("anchors").toList()){int t=v.toMap().value("tick").toInt();if(t>first&&t<last)ticks.append(t);}if(mode==2)for(auto v:leader.value("anchors").toList()){int t=v.toMap().value("tick").toInt();if(t>first&&t<last)ticks.append(t);}}
   std::sort(ticks.begin(),ticks.end());ticks.erase(std::unique(ticks.begin(),ticks.end()),ticks.end());
   for(int i=1;i<ticks.size();++i){int p=ticks[i-1],q=ticks[i];double audioFrom=audioAt(a,p),audioUntil=audioAt(a,q),at=(outputAt(p)-origin)*1000,duration=mode==0?(audioUntil-audioFrom)/trackSpeed:(outputAt(q)-outputAt(p))*1000;
    if(duration<=0)return {{"error",tr("对齐点顺序不正确。")}};
    pieces.append(QVariantMap{{"from",audioFrom},{"until",audioUntil},{"at",at},{"duration",duration},{"fade",request.value("fade",true)}});
   }
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
 int context=start;auto measure=_score->tick2measure(Fraction::fromTicks(start));if(measure&&measure->prevMeasure())context=measure->prevMeasure()->tick().ticks();auto segment=_score->tick2segment(Fraction::fromTicks(start),false,SegmentType::ChordRest);
 if(segment)for(int track=0;track<_score->ntracks();++track){auto e=segment->element(track);if(!e||!e->isChord())continue;for(auto n:toChord(e)->notes()){QSet<Ms::Note*> visited;while(n->tieBack()&&n->tieBack()->startNote()&&!visited.contains(n)){visited.insert(n);n=n->tieBack()->startNote();context=qMin(context,n->chord()->tick().ticks());}}}
 for(auto pair:_score->spannerMap().map()){auto line=pair.second;if(line->isPedal()&&line->tick().ticks()<start&&line->tick2().ticks()>start)context=qMin(context,line->tick().ticks());}
 int launch=qMax(0,from-(start-context));
 return {{"ok",true},{"revision",analysisRevision()},{"clips",clips},{"clock",clockValues},{"joint",true},{"volume",request.value("volume",.8)},{"fromUtick",launch},{"untilUtick",until},{"startFrame",int(origin*MScore::sampleRate)},{"actualStart",master->repeatList().utick2tick(launch)},{"start",start},{"end",end},{"mode",mode}};
}
QVariantMap ScoreObserver::startReferencePlayback(const QVariantMap& plan){
 if(!plan.value("usesPreview").toBool())stopPlaybackPreview();
 if(!_score||!mscore||mscore->currentScore()!=_score||plan.value("revision").toString()!=analysisRevision())return {{"error",tr("请回到关联的乐谱，重新准备播放。")}};
 if(!seq||!seq->driver()||seq->isPlaying()||!seq->backgroundRenderingIdle())return {{"error",tr("请停止播放并检查声音设备。")}};
 if(mscore->countIn()||mscore->loop())return {{"error",tr("请先关闭预备拍和乐谱循环；联合循环使用原曲面板的循环按钮。")}};
 const int from=plan.value("fromUtick",-1).toInt(),end=plan.value("untilUtick",-1).toInt();
 if(from<0||end<=from||end>_score->masterScore()->repeatList().ticks())return {{"error",tr("谱段范围不正确，请重新准备播放。")}};
 QVector<QPair<double,double>> clock;for(auto v:plan.value("clock").toList()){auto p=v.toMap();double a=p.value("score").toDouble(),b=p.value("output").toDouble();if(!std::isfinite(a)||!std::isfinite(b)||(!clock.isEmpty()&&(a<=clock.back().first||b<=clock.back().second)))return {{"error",tr("时间对应不是连续向前的，请重新设置对齐点。")}};clock.append({a,b});}
 workflowContext()->finishPlaybackSession();seq->synchronizeStoppedGui();
 auto project=referenceProject();if(!project->property("available").toBool())return {{"error",project->property("status")}};
 QVariantMap result;QMetaObject::invokeMethod(project,"start",Q_RETURN_ARG(QVariantMap,result),Q_ARG(QVariantMap,plan));if(result.contains("error"))return result;
 setReferenceClock(clock);setReferenceEndTick(end);
 auto session=new QObject(this);workflowContext()->setPlaybackSession(session);auto task=new QTimer(session);task->setInterval(20);auto guarded=QPointer<Ms::Score>(_score);auto state=_score->masterScore()->state();auto started=std::make_shared<bool>(false),natural=std::make_shared<bool>(false);const bool independent=plan.value("mode").toInt()==0;
 connect(session,&QObject::destroyed,project,[project,guarded]{QMetaObject::invokeMethod(project,"stop");if(seq&&guarded&&seq->score()==guarded->masterScore()&&seq->isPlaying())seq->stopWait();setReferenceClock({});setReferenceEndTick(-1);});
 connect(task,&QTimer::timeout,this,[this,session,guarded,state,project,from,started,natural]{
  if(!guarded||guarded->masterScore()->state()!=state||mscore->currentScore()!=guarded||!project->property("playing").toBool()){session->deleteLater();return;}
  if(!*started&&project->property("prepared").toBool()){*started=true;seq->setScoreView(mscore->currentScoreView());if(auto play=getAction("play")){QSignalBlocker block(play);play->setChecked(true);}seq->startAtUTick(from);}
  if(*started&&seq&&seq->isPlaying())project->setProperty("scoreTick",guarded->masterScore()->repeatList().utick2tick(seq->getCurTick()));
  Q_UNUSED(natural);
 });task->start();
 connect(seq,&Seq::heartBeat,session,[guarded,end,natural](int,int tick,int){if(guarded&&seq->score()==guarded->masterScore()&&tick>=end){*natural=true;seq->stop();seq->stopNotes();}});
 connect(seq,&Seq::stopped,session,[session,natural,independent,end]{if(independent&&(*natural||(seq&&seq->getCurTick()>=end)))return;session->deleteLater();});
 connect(this,&ScoreObserver::scoreChanged,session,[session,project]{QMetaObject::invokeMethod(project,"stop");if(seq&&seq->isPlaying())seq->stop();setReferenceClock({});session->deleteLater();});
 return {{"ok",true},{"preparing",true}};
}
QVariantMap ScoreObserver::stopReferencePlayback(){auto p=referenceProject();QMetaObject::invokeMethod(p,"stop");if(seq&&seq->isPlaying())seq->stopWait();setReferenceClock({});setReferenceEndTick(-1);return {{"ok",true}};}
}}
