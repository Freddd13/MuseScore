#include "playbacktiming.h"
#include "arpeggio.h"
#include "chord.h"
#include "note.h"
#include "score.h"
#include "segment.h"
#include "staff.h"
#include "part.h"
#include "tempo.h"
#include "repeatlist.h"
#include <limits>
#include "audio/midi/event.h"
#include <algorithm>
#include <cmath>

namespace Ms {
namespace PlaybackTiming {
Arpeggio* arpeggio(const Chord* chord)
      {
      if (!chord || chord->isGrace()) return nullptr;
      // An explicit local sign wins over one spanning down from another staff.
      if (chord->arpeggio()) {
            auto a = chord->arpeggio();
            return a->playArpeggio() && a->timingMode() ? a : nullptr;
            }
      for (int track = chord->track() - VOICES; track >= 0; track -= VOICES) {
            auto element = chord->segment()->element(track);
            if (!element || !element->isChord()) continue;
            auto owner = toChord(element);
            if (owner->part() != chord->part()) break;
            auto a = owner->arpeggio();
            if (a && a->playArpeggio() && a->timingMode() && track + a->span() * VOICES > chord->track()) return a;
            }
      return nullptr;
      }

QList<Note*> arpeggioNotes(const Arpeggio* a)
      {
      QList<Note*> result;
      if (!a || !a->parent() || !a->parent()->isChord()) return result;
      auto owner = a->chord();
      for (int staff = 0; staff < a->span(); ++staff) {
            int track = owner->track() + staff * VOICES;
            if (track >= owner->score()->ntracks()) break;
            auto element = owner->segment()->element(track);
            if (!element || !element->isChord()) continue;
            auto chord = toChord(element);
            if (chord->part() != owner->part()) break;
            if (chord != owner && chord->arpeggio()) continue;
            if (chord->playEventType() != PlayEventType::Auto || chord->tremolo()) continue;
            for (auto note : chord->notes())
                  if (note->play() && !note->hidden() && !note->tieBack()) result.append(note);
            }
      std::stable_sort(result.begin(), result.end(), [](const Note* a, const Note* b) { return a->ppitch() < b->ppitch(); });
      if (a->arpeggioType() == ArpeggioType::DOWN || a->arpeggioType() == ArpeggioType::DOWN_STRAIGHT)
            std::reverse(result.begin(), result.end());
      return result;
      }

double intervalMs(const Arpeggio* a)
      {
      auto notes = arpeggioNotes(a);
      if (notes.size() < 2) return a->intervalMs();
      double duration = std::numeric_limits<double>::max();
      for (auto note : notes) {
            auto chord = note->chord();
            int tick = chord->tick().ticks();
            double ms = 1000.0 * (chord->score()->tempomap()->tick2time(tick + chord->actualTicks().ticks()) - chord->score()->tempomap()->tick2time(tick));
            duration = std::min(duration, ms);
            }
      return std::min(a->intervalMs(), duration * 0.5 / (notes.size() - 1));
      }

QList<Chord*> graceNotes(const Chord* chord)
      {
      QList<Chord*> result;
      for (auto group : {chord->graceNotesBefore(), chord->graceNotesAfter()}) for (auto grace : group) {
            if (grace->playEventType() != PlayEventType::Auto) continue;
            for (auto note : grace->notes()) if (note->play() && !note->hidden()) { result.append(grace); break; }
            }
      return result;
      }
double graceSpanMs(const Chord* chord)
      {
      int count = graceNotes(chord).size();
      if (!count || chord->playEventType() != PlayEventType::Auto) return 0;
      auto map = chord->score()->tempomap(); int tick = chord->tick().ticks();
      double ms = 1000.0 * (map->tick2time(tick + chord->actualTicks().ticks()) - map->tick2time(tick));
      double requested = chord->getProperty(Pid::GRACE_DURATION).toDouble();
      double limit = ms * 0.5;
      // Keep a later anticipation after the preceding same-voice onset,
      // including a short note at the start of the score. It must remain audible.
      if (chord->getProperty(Pid::GRACE_PLAY_MODE).toInt() == 1 && chord->segment())
            for (auto s = chord->segment()->prev1(SegmentType::ChordRest); s; s = s->prev1(SegmentType::ChordRest)) {
                  if (!s->element(chord->track())) continue;
                  limit = std::min(limit, 500.0 * (map->tick2time(tick) - map->tick2time(s->tick().ticks())));
                  break;
                  }
      return std::min(limit, chord->getProperty(Pid::GRACE_DURATION_MODE).toInt() ? ms * requested / 100.0 : requested * count);
      }
int tickAtTime(const Score* score, double seconds)
      {
      auto map = score->tempomap();
      return seconds < 0 ? qRound(seconds * DIVISION * map->tempo(0) * map->relTempo()) : map->time2tick(seconds);
      }
int graceStartTick(const Chord* chord)
      {
      int tick = chord->tick().ticks();
      if (chord->getProperty(Pid::GRACE_PLAY_MODE).toInt() != 1) return tick;
      int duration = chord->actualTicks().ticks();
      int raw = tickAtTime(chord->score(), chord->score()->tempomap()->tick2time(tick) - graceSpanMs(chord) / 1000.0);
      int permille = qRound(1000.0 * (raw - tick) / duration);
      return tick + duration * permille / 1000; // Match the first rendered grace event.
      }

double time(const Score* score, int utick)
      {
      return utick < 0 ? double(utick) / (DIVISION * score->tempomap()->tempo(0) * score->tempomap()->relTempo()) : score->utick2utime(utick);
      }
bool belongsToStart(const NPlayEvent& event, int target)
      { return event.nominalTick() == target; }
int startTick(const EventMap& events, int target, const Score* score)
      {
      int result = target;
      for (auto i = events.begin(); i != events.lower_bound(target); ++i)
            if (i->second.type() == ME_NOTEON && i->second.velo() && belongsToStart(i->second, target)) result = std::min(result, i->first);
      return result;
      }
int exportOffset(const EventMap& events)
      { return events.empty() ? 0 : std::max(0, -events.begin()->first); }
}
}
