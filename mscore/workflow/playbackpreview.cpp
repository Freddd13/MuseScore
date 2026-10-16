// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "playbackpreview.h"
#include "workflowcontext.h"
#include "referencebus.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/score.h"
#include "libmscore/playbacktiming.h"
#include "libmscore/repeatlist.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/tempo.h"
#include "libmscore/tie.h"
#include "libmscore/spannermap.h"
#include "libmscore/spanner.h"
#include "libmscore/harmony.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "mscore/shortcut.h"
#include <QAction>
#include <QCoreApplication>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QSignalBlocker>
#include <cmath>
#include <algorithm>
namespace Ms {
namespace {
struct Session {
      QPointer<Score> score;
      ScoreContentState state;
      QString key;
      PlaybackPreviewMaterial original, proposed;
      EventMap activeEvents;
      bool active=false, candidate=false, restoringLoop=false, loopTouched=false, oldLoop=false;
      int position=-1, begin=-1, end=-1;
      QPointer<QObject> connections;
};
Session& session(){static Session value;return value;}
bool current(Score* score){auto& s=session();return score&&s.score==score&&s.state==score->masterScore()->state();}
void publish(bool playing){auto& s=session();workflowContext()->setAudition({{"mode",s.candidate?"candidate":"score"},{"playing",playing},{"tick",s.position},{"fromUtick",s.begin},{"untilUtick",s.end}});}
QString noteKey(const Note* n){
      auto chord=n->chord();int grace=-1;if(chord->isGrace()&&chord->parent()&&chord->parent()->isChord())grace=toChord(chord->parent())->graceNotes().indexOf(chord);
      const auto& notes=chord->notes();auto found=std::find(notes.begin(),notes.end(),n);
      return QString("%1:%2:%3:%4").arg(chord->tick().ticks()).arg(n->track()).arg(grace).arg(found-notes.begin());
}
QHash<QString,const Note*> sourceNotes(Score* score){
      QHash<QString,const Note*> result;
      for(auto seg=score->firstSegment(SegmentType::ChordRest);seg;seg=seg->next1(SegmentType::ChordRest))for(int t=0;t<score->ntracks();++t){auto e=seg->element(t);if(!e||!e->isChord())continue;auto chords=toChord(e)->graceNotes();chords.append(toChord(e));for(auto chord:chords)for(auto note:chord->notes())result.insert(noteKey(note),note);}
      return result;
}
void release(){
      auto& s=session();s.active=false;setReferenceClock({});setReferenceEndTick(-1);
      if(seq)seq->invalidatePlaybackEvents();
      if(auto action=getAction("loop")){if(!s.loopTouched){s.restoringLoop=true;action->setChecked(s.oldLoop);s.restoringLoop=false;}}
      publish(false);
}
double mapClock(const QVector<QPair<double,double>>& points,double value,bool inverse=false){
      if(points.size()<2)return value;
      auto x=[inverse](const QPair<double,double>& p){return inverse?p.second:p.first;};auto y=[inverse](const QPair<double,double>& p){return inverse?p.first:p.second;};
      auto next=std::upper_bound(points.cbegin(),points.cend(),value,[&](double v,const QPair<double,double>& p){return v<x(p);});
      if(next==points.cbegin())return value+y(points.front())-x(points.front());if(next==points.cend())return value+y(points.back())-x(points.back());auto before=next-1;return y(*before)+(value-x(*before))*(y(*next)-y(*before))/(x(*next)-x(*before));
}
}
PlaybackPreviewMaterial renderPlaybackPreview(Score* original,Score* rendered){
      PlaybackPreviewMaterial result;auto source=original->masterScore(),copy=rendered->masterScore();
      copy->tempomap()->setRelTempo(source->tempomap()->relTempo());
      rendered->renderMidi(&result.events,true,MScore::playRepeats,mscore->synthesizerState());
      const auto notes=sourceNotes(original);
      QHash<int,int> channels;
      for(int part=0;part<qMin(original->parts().size(),rendered->parts().size());++part){
            auto from=rendered->parts()[part],to=original->parts()[part];
            for(const auto& change:*from->instruments()){
                  const auto old=change.second,now=to->instrument(Fraction::fromTicks(change.first));
                  for(int i=0;i<qMin(old->channel().size(),now->channel().size());++i){const int a=old->channel(i)->channel(),b=now->channel(i)->channel();if(a>=0&&b>=0&&b<256)channels.insert(a,b);}
                  }
            }
      QHash<QString,const Harmony*> harmonies;
      original->scanElements(&harmonies,[](void* context,Element* element){if(element->isHarmony()){auto h=toHarmony(element);static_cast<QHash<QString,const Harmony*>*>(context)->insert(QString("%1:%2:%3").arg(h->tick().ticks()).arg(h->track()).arg(h->harmonyName()),h);}},false);
      QHash<const Note*,const Note*> remapped;
      auto remap=[&](const Note* note)->const Note*{if(!note)return nullptr;auto it=remapped.constFind(note);if(it!=remapped.cend())return *it;auto mapped=notes.value(noteKey(note),nullptr);remapped.insert(note,mapped);return mapped;};
      for(auto& pair:result.events){auto& e=pair.second;if(e.type()>=0x80&&e.type()<0xf0&&channels.contains(e.channel()))e.setChannel(uchar(channels.value(e.channel())));e.setNote(remap(e.note()));e.setNoteEventOwner(remap(e.noteEventOwner()));if(auto h=e.harmony())e.setHarmony(harmonies.value(QString("%1:%2:%3").arg(h->tick().ticks()).arg(h->track()).arg(h->harmonyName()),nullptr));}
      QSet<int> ticks{0,source->repeatList().ticks()};
      for(auto repeat:source->repeatList()){
            ticks.insert(repeat->utick);ticks.insert(repeat->utick+repeat->len());
            for(auto map:{source->tempomap(),copy->tempomap()})for(const auto& tempo:*map)if(tempo.first>=repeat->tick&&tempo.first<=repeat->tick+repeat->len()){
                  const int t=repeat->utick+tempo.first-repeat->tick;ticks.insert(t);if(t>0)ticks.insert(t-1);
                  }
            }
      auto ordered=ticks.values();std::sort(ordered.begin(),ordered.end());
      for(int tick:ordered){const double from=PlaybackTiming::time(source,tick),to=PlaybackTiming::time(copy,tick);if(result.clock.isEmpty()||(from>result.clock.back().first&&to>result.clock.back().second))result.clock.append({from,to});}
      return result;
}
void alignPreviewReference(PlaybackPreviewMaterial& material){
      if(material.reference.isEmpty())return;
      QVector<QPair<double,double>> audio;for(auto v:material.reference.value("clock").toList()){auto p=v.toMap();audio.append({p.value("score").toDouble(),p.value("output").toDouble()});}
      QVector<double> positions;for(const auto& point:material.clock)positions.append(point.first);for(const auto& point:audio)positions.append(mapClock(material.clock,point.first,true));std::sort(positions.begin(),positions.end());positions.erase(std::unique(positions.begin(),positions.end()),positions.end());
      QVariantList clock;for(double position:positions)clock.append(QVariantMap{{"score",position},{"output",mapClock(audio,mapClock(material.clock,position))}});material.reference["clock"]=clock;
}
QVariantMap playbackPreviewReferencePlan(Score* score,bool proposed,int resumeTick){
      if(!current(score))return {};const auto& material=proposed?session().proposed:session().original;auto plan=material.reference;if(plan.isEmpty())return plan;
      QVector<QPair<double,double>> clock;for(auto v:plan.value("clock").toList()){auto point=v.toMap();clock.append({point.value("score").toDouble(),point.value("output").toDouble()});}
      const int startFrame=plan.value("startFrame").toInt();const double output=mapClock(clock,PlaybackTiming::time(score->masterScore(),resumeTick));
      const double offset=(output-double(startFrame)/MScore::sampleRate)*1000.;
      if(offset>0){QVariantList clips;for(auto value:plan.value("clips").toList()){auto clip=value.toMap();QVariantList pieces;
                  for(auto v:clip.value("pieces").toList()){auto piece=v.toMap();const double at=piece.value("at").toDouble(),duration=piece.value("duration").toDouble(),cut=qMax(0.,offset-at);if(cut>=duration-.000001)continue;const double from=piece.value("from").toDouble(),until=piece.value("until").toDouble();piece["from"]=from+(until-from)*cut/duration;piece["at"]=qMax(0.,at-offset);piece["duration"]=duration-cut;pieces.append(piece);}if(!pieces.isEmpty()){clip["pieces"]=pieces;clips.append(clip);}}
            plan["clips"]=clips;plan["startFrame"]=int(std::llround(output*MScore::sampleRate));plan["resumeOffset"]=offset;}
      plan["fromUtick"]=resumeTick;plan["usesPreview"]=true;return plan;
}
bool playbackPreviewActive(Score* score,bool proposed){auto& s=session();return current(score)&&s.active&&s.candidate==proposed&&((seq&&seq->isPlaying())||workflowContext()->audition().value("preparing").toBool());}
bool playbackPreviewPrepared(Score* score,const QString& key){return current(score)&&session().key==key;}
EventMap resumePlaybackPreview(const EventMap& events,int tick){
      struct Key {NPlayEvent attack;bool pressed=true;};
      QHash<int,Key> keys;QHash<int,bool> pedal;
      for(auto it=events.cbegin();it!=events.lower_bound(tick);++it){const auto& e=it->second;const int key=e.channel()*128+e.pitch();
            if(e.type()==ME_NOTEON&&e.velo())keys.insert(key,{e,true});
            else if(e.type()==ME_NOTEOFF||(e.type()==ME_NOTEON&&!e.velo())){auto found=keys.find(key);if(found!=keys.end()){if(pedal.value(e.channel()))found->pressed=false;else keys.erase(found);}}
            else if(e.type()==ME_CONTROLLER){
                  if(e.controller()==64){pedal[e.channel()]=e.value()>=64;if(!pedal.value(e.channel()))for(auto n=keys.begin();n!=keys.end();)if(n->attack.channel()==e.channel()&&!n->pressed)n=keys.erase(n);else ++n;}
                  else if(e.controller()==120||e.controller()==123||e.controller()==121)for(auto n=keys.begin();n!=keys.end();)if(n->attack.channel()==e.channel())n=keys.erase(n);else ++n;
                  }
            }
      QSet<int> boundary;auto at=events.equal_range(tick);for(auto it=at.first;it!=at.second;++it)if(it->second.type()==ME_NOTEON||it->second.type()==ME_NOTEOFF)boundary.insert(it->second.channel()*128+it->second.pitch());
      EventMap result;
      for(auto it=keys.cbegin();it!=keys.cend();++it){if(boundary.contains(it.key()))continue;auto attack=it->attack;if(attack.note()&&attack.note()->part()->instrument(attack.note()->tick())->useDrumset())continue;
            attack.setNominalTick(tick);result.insert({tick,attack});if(!it->pressed){attack.setVelo(0);result.insert({tick,attack});}}
      result.insert(events.cbegin(),events.cend());return result;
}
void preparePlaybackPreview(Score* score,const QString& key,PlaybackPreviewMaterial original,PlaybackPreviewMaterial proposed){
      auto& s=session();const int resume=current(score)?(s.active&&seq&&seq->isPlaying()?seq->getCurTick():s.position):-1;stopPlaybackPreview();if(s.connections)delete s.connections;
      s.score=score;s.state=score->masterScore()->state();s.key=key;s.original=std::move(original);s.proposed=std::move(proposed);s.position=resume;
      auto context=new QObject(qApp);s.connections=context;
      QObject::connect(score,&QObject::destroyed,context,[]{stopPlaybackPreview(true);auto& s=session();s.score.clear();s.original={};s.proposed={};});
      if(seq){
            QObject::connect(seq,&Seq::started,context,[]{if(session().active)publish(true);});
            QObject::connect(seq,&Seq::heartBeat,context,[](int,int tick,int){auto& s=session();if(!s.active)return;if(!current(s.score)||seq->score()!=s.score->masterScore()||!mscore->currentScore()||mscore->currentScore()!=s.score){stopPlaybackPreview(true);return;}s.position=tick;});
            QObject::connect(seq,&Seq::stopped,context,[]{auto& s=session();if(s.active&&!seq->isPlaying()){s.position=seq->getCurTick();release();}});
            }
      if(auto loop=getAction("loop"))QObject::connect(loop,&QAction::toggled,context,[](bool){auto& s=session();if(s.active&&!s.restoringLoop)s.loopTouched=true;});
}
QVariantMap startPlaybackPreview(Score* score,bool proposed,const QVariantMap& range){
      if(!current(score)||!mscore||mscore->currentScore()!=score)return {{"error",QObject::tr("请回到这份乐谱，更新演奏对应后试听。")}};
      if(!seq||!seq->driver())return {{"error",QObject::tr("请在软件设置中启用声音设备后试听。")}};
      auto& s=session();int resume=s.active&&seq->score()==score->masterScore()?seq->getCurTick():s.position;
      workflowContext()->finishPlaybackSession();if(seq->isPlaying())seq->stopWait();if(!seq->isPlaying())seq->synchronizeStoppedGui();if(seq->isPlaying()||!seq->backgroundRenderingIdle())return {{"error",QObject::tr("正在停止播放，稍后即可切换效果。")}};
      int tick=range.value("start",-1).toInt(),end=range.value("end",-1).toInt(),occurrence=qMax(1,range.value("occurrence",1).toInt()),seen=0,from=-1,until=-1;
      for(auto r:score->masterScore()->repeatList()){
            if(from<0&&tick>=r->tick&&tick<r->tick+r->len()&&++seen==occurrence)from=r->utick+tick-r->tick;
            if(from>=0&&end>=r->tick&&end<=r->tick+r->len()&&r->utick+end-r->tick>from){until=r->utick+end-r->tick;break;}
            }
      if(from<0||until<=from)return {{"error",QObject::tr("请选择有效谱段和播放第几遍。")}};
      int context=tick;auto measure=score->tick2measure(Fraction::fromTicks(tick));if(measure&&measure->prevMeasure())context=measure->prevMeasure()->tick().ticks();
      if(auto seg=score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest))for(int track=0;track<score->ntracks();++track){auto e=seg->element(track);if(!e||!e->isChord())continue;for(auto n:toChord(e)->notes()){QSet<Note*> visited;while(n->tieBack()&&n->tieBack()->startNote()&&!visited.contains(n)){visited.insert(n);n=n->tieBack()->startNote();context=qMin(context,n->chord()->tick().ticks());}}}
      for(auto item:score->spannerMap().map())if(item.second->isPedal()&&item.second->tick().ticks()<tick&&item.second->tick2().ticks()>tick)context=qMin(context,item.second->tick().ticks());
      const int begin=qMax(0,from-(tick-context));if(resume<begin||resume>=until)resume=begin;
      s.activeEvents=resumePlaybackPreview(proposed?s.proposed.events:s.original.events,resume);
      s.begin=begin;s.end=until;s.position=resume;s.candidate=proposed;s.active=true;s.loopTouched=false;
      if(auto action=getAction("loop")){s.oldLoop=action->isChecked();s.restoringLoop=true;action->setChecked(false);s.restoringLoop=false;}
      setReferenceClock(proposed?s.proposed.clock:QVector<QPair<double,double>>());setReferenceEndTick(until);
      seq->invalidatePlaybackEvents();seq->setScoreView(mscore->currentScoreView());
      if(range.value("deferStart").toBool()){publish(false);auto state=workflowContext()->audition();state["preparing"]=true;workflowContext()->setAudition(state);}
      else {if(auto play=getAction("play")){QSignalBlocker block(play);play->setChecked(true);}seq->startAtUTick(resume);publish(true);}
      return {{"ok",true},{"mode",proposed?"candidate":"score"},{"fromUtick",resume},{"untilUtick",until}};
}
void stopPlaybackPreview(bool finish){auto& s=session();if(s.active&&seq){if(seq->isPlaying())seq->stopWait();if(!seq->isPlaying())seq->synchronizeStoppedGui();}if(s.active)release();if(finish)s.position=-1;}
const EventMap* activePlaybackPreviewEvents(MasterScore* score){auto& s=session();return s.active&&current(s.score)&&s.score->masterScore()==score?&s.activeEvents:nullptr;}
}
