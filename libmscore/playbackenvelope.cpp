// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "playbackenvelope.h"
#include "element.h"
#include "note.h"
#include "noteevent.h"
#include "chord.h"
#include "tremolo.h"
#include "xml.h"
#include <cmath>
namespace Ms {
std::initializer_list<Pid> PlaybackEnvelope::properties()
      { static const std::initializer_list<Pid> ids {Pid::ENVELOPE_MODE, Pid::ENVELOPE_START, Pid::ENVELOPE_END, Pid::ENVELOPE_CURVE, Pid::ENVELOPE_ALTERNATE}; return ids; }
bool PlaybackEnvelope::handles(Pid id)
      { return id >= Pid::ENVELOPE_MODE && id <= Pid::ENVELOPE_ALTERNATE; }
QVariant PlaybackEnvelope::property(Pid id) const
      {
      switch(id) {
            case Pid::ENVELOPE_MODE: return _mode;
            case Pid::ENVELOPE_START: return _start;
            case Pid::ENVELOPE_END: return _end;
            case Pid::ENVELOPE_CURVE: return _curve;
            case Pid::ENVELOPE_ALTERNATE: return _alternate;
            default: return {};
            }
      }
QVariant PlaybackEnvelope::defaultProperty(Pid id) const { return PlaybackEnvelope(_ornament).property(id); }
bool PlaybackEnvelope::setProperty(Pid id, const QVariant& v)
      {
      const double value=v.toDouble();
      if (!std::isfinite(value)) return false;
      switch(id) {
            case Pid::ENVELOPE_MODE: if (v.toInt()<0 || v.toInt()>4) return false; _mode=v.toInt(); break;
            case Pid::ENVELOPE_START: if(value<1 || value>400) return false; _start=value; break;
            case Pid::ENVELOPE_END: if(value<1 || value>400) return false; _end=value; break;
            case Pid::ENVELOPE_CURVE: if(value<.1 || value>8) return false; _curve=value; break;
            case Pid::ENVELOPE_ALTERNATE: if(value<-100 || value>100) return false; _alternate=value; break;
            default: return false;
            }
      return true;
      }
bool PlaybackEnvelope::operator==(const PlaybackEnvelope& other) const
      { return _mode==other._mode && _start==other._start && _end==other._end && _curve==other._curve && _alternate==other._alternate; }
double PlaybackEnvelope::factor(double progress, bool first, bool alternate) const
      {
      if (!_mode) return 1;
      const double percent = _mode==1 ? (first ? _start : _end)
            : _start+(_end-_start)*std::pow(qBound(0.0,progress,1.0),_curve);
      return qBound(1.0,percent-(alternate ? _alternate : 0),400.0)/100;
      }
bool PlaybackEnvelope::read(Element* owner, XmlReader& xml)
      {
      for(auto id:properties()) if(xml.name()==propertyName(id)) { owner->readProperty(xml,id); return true; }
      return false;
      }
void PlaybackEnvelope::write(const Element* owner, XmlWriter& xml)
      { for(auto id:properties()) owner->writeProperty(xml,id); }
QMap<Pid,QVariant> PlaybackEnvelope::preset(int mode, bool ornament)
      {
      QMap<Pid,QVariant> result {{Pid::ENVELOPE_MODE,mode}};
      if (!mode || mode==4) return result;
      result[Pid::ENVELOPE_START] = mode==2 ? 80.0 : 100.0;
      result[Pid::ENVELOPE_END] = mode==3 ? 80.0 : mode==2 || ornament ? 100.0 : 90.0;
      result[Pid::ENVELOPE_CURVE] = 1.0;
      result[Pid::ENVELOPE_ALTERNATE] = ornament ? 15.0 : 5.0;
      return result;
      }
const Note* PlaybackEnvelope::eventSource(const Note* owner, const NoteEvent& event)
      {
      if (owner->chord()->playEventType()!=PlayEventType::Auto || event.velocitySourceIndex()<0) return owner;
      const auto tremolo=owner->chord()->tremolo();
      if (tremolo && tremolo->twoNotes() && tremolo->chord1()==owner->chord() && tremolo->chord2()
            && event.velocitySourceIndex()<int(tremolo->chord2()->notes().size())) return tremolo->chord2()->notes()[event.velocitySourceIndex()];
      return owner;
      }
double PlaybackEnvelope::eventFactor(const Note* owner, const NoteEvent& event)
      { return owner->chord()->playEventType()==PlayEventType::Auto ? event.velocityFactor() : 1; }
}
