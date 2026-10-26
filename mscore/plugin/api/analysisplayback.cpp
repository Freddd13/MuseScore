// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
// GUI-side preparation in bounded musical chunks; callers receive values only.
#include "scoreobserver.h"
#include "handmarkers.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/instrument.h"
#include "libmscore/staff.h"
#include "libmscore/repeatlist.h"
#include "libmscore/rendermidi.h"
#include "libmscore/playbacktiming.h"
#include "libmscore/tempo.h"
#include "libmscore/tie.h"
#include "libmscore/textbase.h"
#include "mscore/musescore.h"
#include "mscore/seq.h"
#include "audio/midi/event.h"
#include <QElapsedTimer>
#include <climits>
#include <algorithm>

namespace Ms { namespace PluginAPI {
struct AnalysisPlaybackRead {
      QPointer<Ms::Score> score;
      QString revision;
      std::unique_ptr<MidiRenderer> renderer;
      SynthesizerState synth;
      EventMap events;
      int next=0;
      int end=0;
      bool ready=false, rendered=false;
      MidiEventFixup fixup;
      MidiControllerFilter controllerFilter;
      EventMap::iterator finalizing;
      quint64 finalized=0;
      double preparationMs=0;
      struct Pass {int start,end,offset,occurrence;};
      QVector<Pass> passes;
      QHash<int,QVector<Pass>> notePasses;
      QHash<const Ms::Note*,QVariantList> tieContexts;
};
namespace {
bool unavailable(ScoreObserver* observer) {return observer->playing() || (seq&&!seq->backgroundRenderingIdle());}
}

QString ScoreObserver::analysisPlaybackRevision()
      {
      if(!_score)return {};
      const auto synth=mscore?mscore->synthesizerState():SynthesizerState{};
      QString channels;
      for(const auto staff:_score->staves()) {
            int mask=0;for(int voice=0;voice<VOICES;++voice)if(staff->playbackVoice(voice))mask|=1<<voice;
            channels+=QString::number(mask,16);
            }
      for(const auto part:_score->parts())for(const auto& entry:*part->instruments())for(const auto source:entry.second->channel()) {
            const auto channel=source->channel()>=0 && size_t(source->channel())<_score->masterScore()->midiMapping().size()?_score->masterScore()->playbackChannel(source):source;
            channels+=QChar('0'+(channel->mute()?1:0)+(channel->soloMute()?2:0));
            }
      return QString("%1:%2:%3:%4:%5:%6").arg(analysisRevision()).arg(_score->masterScore()->expandRepeats())
            .arg(synth.method()).arg(synth.ccToUse()).arg(_score->tempomap()->relTempo(),0,'g',16).arg(channels);
      }

QVariantMap ScoreObserver::analysisPlaybackPrepare(int start,int end,const QString& expected)
      {
      if(!_score)return {{"error","no-score"}};
      const auto revision=analysisPlaybackRevision();
      if(expected.isEmpty() || revision!=expected)return {{"error","stale"},{"revision",revision}};
      if(unavailable(this))return {{"error","playing-or-rendering"}};
      if(!_playbackRead)_playbackRead=std::make_shared<AnalysisPlaybackRead>();
      auto& read=*_playbackRead;QElapsedTimer timer;timer.start();
      if(read.score!=_score || read.revision!=revision) {
            read=AnalysisPlaybackRead{};read.score=_score;read.revision=revision;
            read.synth=mscore?mscore->synthesizerState():SynthesizerState{};
            read.renderer=std::make_unique<MidiRenderer>(_score);read.renderer->setMinChunkSize(1);
            // Establish swing/capo and the current repeat layout before the shared maps.
            read.renderer->getChunkAt(0);_score->updateChannel();_score->updateVelo();
            read.end=_score->repeatList().ticks();
            for(const auto repeat:_score->repeatList())read.passes.append({repeat->tick,repeat->tick+repeat->len(),repeat->utick-repeat->tick,repeat->playbackCount});
            read.preparationMs=double(timer.nsecsElapsed())/1000000.;
            }
      MidiRenderer::Context context(read.synth);context.metronome=false;context.renderHarmony=false;context.playbackMapsReady=true;context.finalizeEvents=false;
      int chunks=0;
      while(!read.rendered) {
            const auto chunk=read.renderer->getChunkAt(read.next);
            if(!chunk){read.rendered=true;read.finalizing=read.events.begin();break;}
            read.renderer->renderChunk(chunk,&read.events,context);
            if(chunk.utick2()<=read.next)return {{"error","playback-did-not-advance"}};
            read.next=chunk.utick2();++chunks;read.rendered=read.next>=read.end;
            if(read.rendered)read.finalizing=read.events.begin();
            if(timer.nsecsElapsed()>=2000000 || chunks>=4)break;
            }
      if(read.rendered && !read.ready) {
            int count=0;
            while(read.finalizing!=read.events.end() && count<512 && timer.nsecsElapsed()<2000000) {
                  auto current=read.finalizing++;read.fixup.process(current->second);
                  if(read.controllerFilter.redundant(current->second))read.events.erase(current);
                  ++count;++read.finalized;
                  }
            read.ready=read.finalizing==read.events.end();
            }
      QVariantList ranges;
      if(read.ready)for(const auto segment:_score->repeatList()) {
            const int first=qMax(start,segment->tick),last=qMin(end,segment->tick+segment->len());if(last<=first)continue;
            const int offset=segment->utick-segment->tick;
            ranges.append(QVariantMap{{"scoreStart",first},{"scoreEnd",last},{"start",first+offset},{"end",last+offset},
                  {"startSeconds",PlaybackTiming::time(_score,first+offset)},{"scoreStartSeconds",_score->tempomap()->tick2time(first)},{"endSeconds",PlaybackTiming::time(_score,last+offset)},
                  {"offset",offset},{"occurrence",segment->playbackCount}});
            }
      return {{"revision",revision},{"ready",read.ready},{"nextTick",read.next},{"endTick",read.end},{"ranges",ranges},
            {"progress",read.ready?100:read.rendered?95:read.end?90*qMin(read.next,read.end)/read.end:90},{"chunks",chunks},
            {"endSeconds",PlaybackTiming::time(_score,read.end)},{"stage",read.ready?"ready":read.rendered?"events":"render"},{"eventCount",int(read.events.size())},
            {"preparationMilliseconds",read.preparationMs},{"milliseconds",double(timer.nsecsElapsed())/1000000.}};
      }

QVariantMap ScoreObserver::analysisPlaybackPage(int fromTick,int firstTrack,int endTrack,int limit,const QString& expected)
      {
      if(!_score)return {{"error","no-score"}};
      if(expected.isEmpty() || expected!=analysisPlaybackRevision())return {{"error","stale"}};
      if(unavailable(this))return {{"error","playing-or-rendering"}};
      if(!_playbackRead)return {{"error","not-prepared"}};
      auto& read=*_playbackRead;
      if(read.score!=_score || read.revision!=expected || !read.ready)return {{"error","not-prepared"}};
      firstTrack=qBound(0,firstTrack,_score->ntracks());endTrack=qBound(firstTrack,endTrack,_score->ntracks());limit=qBound(1,limit,64);
      auto it=fromTick==INT_MIN?read.events.cbegin():read.events.lower_bound(fromTick);
      QVariantList rows;QElapsedTimer timer;timer.start();int groups=0;QSet<const Ms::Note*> describedContexts;
      auto tieContext=[&](const Ms::Note* origin) {
            if(read.tieContexts.contains(origin))return read.tieContexts.value(origin);
            QVariantList result;QSet<const Ms::Note*> visited;auto current=origin;
            while(current&&!visited.contains(current)) {
                  visited.insert(current);const auto chord=current->chord();const int at=chord->tick().ticks();
                  const int index=int(std::find(chord->notes().begin(),chord->notes().end(),current)-chord->notes().begin());
                  QVariantList fingers;for(const auto element:current->el())if(element->isFingering())fingers.append(toTextBase(element)->plainText());
                  auto hands=readHandMarkers(chord->el(),at,current->track(),-1);hands.append(readHandMarkers(current->el(),at,current->track(),index));
                  if(!fingers.isEmpty()||!hands.isEmpty()) {
                        auto note=describe(current,current->ppitch());note.insert("id",QString("%1:%2:%3").arg(at).arg(current->track()).arg(index));
                        const auto measure=chord->measure();note.insert("measure",measure->no()+1);note.insert("beat",1.+double(at-measure->tick().ticks())/(DIVISION*4./measure->timesig().denominator()));
                        result.append(QVariantMap{{"tick",at},{"positionSeconds",_score->tempomap()->tick2time(at)},{"continuation",current!=origin},{"fingerings",fingers},{"hands",hands},{"note",note}});
                        }
                  current=current->tieFor()?current->tieFor()->endNote():nullptr;
                  }
            read.tieContexts.insert(origin,result);return result;
            };
      while(it!=read.events.cend()) {
            const int at=it->first;
            do {
                  const auto& event=it->second;const int staff=event.getOriginatingStaff();
                  if(staff<0 || staff>=_score->nstaves())continue;
                  const auto part=_score->staff(staff)->part();
                  if(part->endTrack()<=firstTrack || part->startTrack()>=endTrack)continue;
                  const bool controller=event.type()==ME_CONTROLLER&&event.controller()==CTRL_SUSTAIN;
                  if(!controller && event.type()!=ME_NOTEON)continue;
                  QVariantMap row;
                  if(controller) {
                        row={{"kind","pedal"},{"value",event.value()},{"generated",event.generatedPedal()}};
                        }
                  else {
                        const auto note=event.noteEventOwner()?event.noteEventOwner():event.note();if(!note)continue;
                        if(note->track()<firstTrack || note->track()>=endTrack)continue;
                        row=describe(note,event.pitch(),true);row.insert("kind",event.velo()?"on":"off");
                        row.insert("velocity",event.velo());row.insert("discard",event.discard());row.insert("muted",event.isMuted());
                        row.insert("eventIndex",event.noteEventIndex());row.insert("grace",note->chord()->isGrace());
                        if(event.pitch()%12==note->ppitch()%12)row.insert("tpc",note->tpc1());
                        int grace=-1;
                        if(note->chord()->isGrace()) {
                              const auto main=toChord(note->chord()->parent());const auto& list=main->graceNotes();
                              grace=list.indexOf(note->chord());row.insert("graceIndex",grace);row.insert("tick",main->tick().ticks());
                              }
                        const auto id=grace<0?QString("%1:%2:%3").arg(row.value("tick").toInt()).arg(note->track()).arg(row.value("index").toInt()):
                              QString("%1:%2:g%3:%4").arg(row.value("tick").toInt()).arg(note->track()).arg(grace).arg(row.value("index").toInt());
                        row.insert("id",id);
                        auto origin=note;QSet<const Ms::Note*> visited;
                        while(origin->tieBack()&&origin->tieBack()->startNote()&&!visited.contains(origin)){visited.insert(origin);origin=origin->tieBack()->startNote();}
                        const auto& originNotes=origin->chord()->notes();const int index=int(std::find(originNotes.begin(),originNotes.end(),origin)-originNotes.begin());
                        row.insert("logicalId",grace<0?QString("%1:%2:%3").arg(origin->chord()->tick().ticks()).arg(origin->track()).arg(index):id);
                        if(event.velo()&&grace<0&&origin->tieFor()&&!describedContexts.contains(origin)) {
                              const auto context=tieContext(origin);if(!context.isEmpty())row.insert("tieContext",context);describedContexts.insert(origin);
                              }
                        const int written=row.value("tick").toInt();
                        if(!read.notePasses.contains(written)) {
                              QVector<AnalysisPlaybackRead::Pass> candidates;
                              for(const auto& pass:read.passes)if(written>=pass.start&&written<pass.end)candidates.append(pass);
                              read.notePasses.insert(written,candidates);
                              }
                        const auto& candidates=read.notePasses[written];
                        const int nominalOffset=event.nominalTick()-written;
                        auto pass=std::lower_bound(candidates.cbegin(),candidates.cend(),nominalOffset,[](const AnalysisPlaybackRead::Pass& pass,int offset){return pass.offset<offset;});
                        if(pass==candidates.cend() && !candidates.isEmpty())--pass;
                        else if(pass!=candidates.cbegin() && qAbs((pass-1)->offset-nominalOffset)<=qAbs(pass->offset-nominalOffset))--pass;
                        if(pass!=candidates.cend()){row.insert("offset",pass->offset);row.insert("occurrence",pass->occurrence);}
                        }
                  row.insert("utick",at);row.insert("seconds",PlaybackTiming::time(_score,at));
                  row.insert("channel",event.channel());row.insert("partId",part->startTrack());row.insert("staff",staff);
                  row.insert("nominalTick",event.nominalTick());
                  if(controller) {
                        int occurrence=1;
                        for(const auto& pass:read.passes)if(event.nominalTick()>=pass.start+pass.offset&&event.nominalTick()<pass.end+pass.offset){occurrence=pass.occurrence;break;}
                        row.insert("occurrence",occurrence);
                        }
                  rows.append(row);
            } while(++it!=read.events.cend() && it->first==at);
            if(++groups>=limit || timer.nsecsElapsed()>=2000000)break;
            }
      return {{"revision",expected},{"events",rows},{"done",it==read.events.cend()},
            {"nextTick",it==read.events.cend()?read.end:it->first},{"milliseconds",double(timer.nsecsElapsed())/1000000.}};
      }
}}
