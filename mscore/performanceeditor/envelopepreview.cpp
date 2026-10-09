// Generated playback factors are captured while stopped, never in paint/audio callbacks.
#include "performanceeditor.h"
#include "libmscore/playbackenvelope.h"
#include "libmscore/chord.h"
#include "libmscore/tremolo.h"
namespace Ms {
void PerformanceEditor::cacheGeneratedVelocity(NoteInfo& info, int method)
      {
      if(info.note->chord()->playEventType()!=PlayEventType::Auto) return;
      auto capture=[&](const Note* owner) {
            bool generated=false;
            for(const auto& event:owner->playEvents()) if(event.velocitySourceIndex()>=0 || event.velocityFactor()!=1) { generated=true; break; }
            if(!generated) return;
            for(const auto& event:owner->playEvents()) {
                  if(PlaybackEnvelope::eventSource(owner,event)!=info.note) continue;
                  auto chord=owner->chord(); if(chord->isGrace()) chord=toChord(chord->parent());
                  const int tick=chord->tick().ticks()+qint64(chord->actualTicks().ticks())*event.ontime()/1000;
                  const int base=NoteVelocity::referenceBaseAt(info.note,tick,method);
                  const double factor=PlaybackEnvelope::eventFactor(owner,event);
                  if(info.generatedVelocities.isEmpty() || info.generatedVelocities.back().base!=base || info.generatedVelocities.back().factor!=factor)
                        info.generatedVelocities.append({base,factor});
                  }
            };
      const auto tremolo=info.note->chord()->tremolo();
      if(tremolo && tremolo->twoNotes() && tremolo->chord2()==info.note->chord() && tremolo->playbackEnvelope().mode()
            && tremolo->chord1() && tremolo->chord1()->playEventType()==PlayEventType::Auto)
            for(const auto owner:tremolo->chord1()->notes()) capture(owner);
      else capture(info.note);
      if(!info.generatedVelocities.isEmpty()) {
            info.base=info.baseMin=info.baseMax=info.generatedVelocities.front().base;
            for(auto event:info.generatedVelocities) { info.baseMin=qMin(info.baseMin,event.base); info.baseMax=qMax(info.baseMax,event.base); }
            }
      else if(info.note->tieBack() && info.note->playEvents().isEmpty()) info.audible=false;
      }
QPair<int,int> PerformanceEditor::generatedVelocityRange(const NoteInfo& info, Note::ValueType type, int raw) const
      {
      if(info.base<0) return {-1,-1};
      if(info.generatedVelocities.isEmpty()) return {NoteVelocity::effective(info.baseMin,type,raw),NoteVelocity::effective(info.baseMax,type,raw)};
      int low=127,high=1;
      for(auto event:info.generatedVelocities) {
            const int value=NoteVelocity::eventVelocity(NoteVelocity::effective(event.base,type,raw),event.factor);
            low=qMin(low,value); high=qMax(high,value);
            }
      return {low,high};
      }
QPair<int,int> PerformanceEditor::actualVelocityRange(const Note* note) const
      {
      auto i=_noteIndex.constFind(note); if(i==_noteIndex.cend()) return {-1,-1};
      const auto& info=_notes[i.value()]; const auto edit=_pending.value(info.note,{info.type,info.raw});
      return generatedVelocityRange(info,edit.type,edit.raw);
      }
}
