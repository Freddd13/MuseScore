// GPL-2.0-or-later.
#include "inputrhythm.h"
#include "score.h"
#include "chord.h"
#include "note.h"
#include "rest.h"
#include "measure.h"
#include "segment.h"
#include "staff.h"
#include "sig.h"
#include "tie.h"
#include "spanner.h"
#include "tremolo.h"

namespace Ms {
namespace {
// Standalone core hosts retain the old policy until the application supplies
// its persistent preference. No score property or serialized field is added.
bool inputRhythmEnabled = false;
std::vector<ChordRest*> chain(ChordRest* cr)
      {
      std::vector<ChordRest*> result;
      if (!cr) return result;
      if (cr->isChord()) {
            Chord* c = toChord(cr);
            while (Chord* previous = c->nextTiedChord(true)) c = previous;
            for (; c; c = c->nextTiedChord()) result.push_back(c);
            }
      else result.push_back(cr);
      return result;
      }
Segment* relocated(Score* score, const Fraction& tick, int track)
      {
      if (track<0) return nullptr;
      ChordRest* cr = score->findCR(tick, track);
      return cr ? cr->segment() : nullptr;
      }
}
bool InputRhythm::enabled() { return inputRhythmEnabled; }
void InputRhythm::setEnabled(bool value) { inputRhythmEnabled = value; }

bool InputRhythm::eligible(ChordRest* cr)
      {
      if (!cr || cr->tuplet() || cr->isGrace() || cr->isRepeatMeasure()
            || !cr->el().empty() || !cr->lyrics().empty()
            || (cr->beamMode() != Beam::Mode::AUTO && cr->beamMode() != Beam::Mode::NONE))
            return false;
      if (cr->isRest()) return !toRest(cr)->isGap();
      if (!cr->isChord()) return false;
      Chord* c = toChord(cr);
      if (c->playEventType() == PlayEventType::User || !c->graceNotes().empty()
            || c->tremolo() || c->arpeggio() || !c->articulations().empty())
            return false;
      for (Note* n : c->notes()) {
            if (!n->el().empty() || !n->spannerFor().empty() || !n->spannerBack().empty())
                  return false;
            }
      return true;
      }

bool InputRhythm::normalize(Score* score, ChordRest* target)
      {
      if (!enabled() || !target || score->inputState().slur()) return false;
      auto group = chain(target);
      for (auto cr : group) if (!eligible(cr)) return false;
      const Fraction start = group.front()->tick(), end = group.back()->endTick();
      const int track = target->track();
      // Keep spanners anchored within the rewrite range intact by declining
      // complex cases. External ties are handled by the existing regroup code.
      for (const auto& interval : score->spannerMap().findOverlapping(start.ticks(), end.ticks())) {
            Spanner* s = interval.value;
            if (!s->isTie() && ((s->track() == track && s->tick() >= start && s->tick() < end)
                  || (s->track2() == track && s->tick2() >= start && s->tick2() < end)))
                  return false;
            }
      // Compare before rebuilding: repeated normalization preserves identity,
      // user selection and undo history when the grouping is already correct.
      std::vector<std::pair<Fraction,TDuration>> expected;
      Fraction tick = start;
      while (tick < end) {
            Measure* m = score->tick2measure(tick);
            if (!m) return false;
            const Fraction stretch = target->staff()->timeStretch(tick);
            const Fraction stop = end < m->endTick() ? end : m->endTick();
            auto durations = toRhythmicDurationList((stop-tick)*stretch, target->isRest(),
                  tick-m->tick(), score->sigmap()->timesig(tick.ticks()).nominal(), m, 1);
            if (durations.empty()) return false;
            for (auto duration : durations) {
                  expected.emplace_back(tick,duration);
                  tick += duration.type() == TDuration::DurationType::V_MEASURE
                        ? stop-tick : duration.fraction()/stretch;
                  }
            }
      bool same = expected.size() == group.size();
      if (same) for (size_t i=0;i<group.size();++i)
            if (expected[i].first != group[i]->tick() || expected[i].second != group[i]->durationType()) same=false;
      if (same) return false;

      InputState& is = score->inputState();
      const InputState saved = is;
      const int inputTrack = is.track();
      const bool hasInput = is.segment(), hasLast = is.lastSegment();
      const Fraction inputTick = hasInput ? is.segment()->tick() : Fraction();
      const Fraction lastTick = hasLast ? is.lastSegment()->tick() : Fraction();
      struct Selected { Fraction tick; int track; int pitch; Element* stable; };
      std::vector<Selected> selection;
      const bool reselect = score->selection().isList();
      const bool range = score->selection().isRange() && score->selection().startSegment();
      const Fraction rangeStart = range ? score->selection().tickStart() : Fraction();
      const Fraction rangeEnd = range ? score->selection().tickEnd() : Fraction();
      const int staffStart = range ? score->selection().staffStart() : 0;
      const int staffEnd = range ? score->selection().staffEnd() : 0;
      if (reselect) for (Element* e : score->selection().elements()) {
            auto cr = InputState::chordRest(e);
            if (cr) selection.push_back({cr->tick(),cr->track(),e->isNote() ? toNote(e)->pitch() : -1,nullptr});
            else selection.push_back({{},0,-1,e});
            }
      score->regroupNotesAndRests(start,end,track);
      if (reselect) {
            score->deselectAll();
            for (const auto& item : selection) {
                  ChordRest* cr = item.stable ? nullptr : score->findCR(item.tick,item.track);
                  Element* e = item.stable ? item.stable : cr && cr->isChord() && item.pitch>=0
                        ? static_cast<Element*>(toChord(cr)->findNote(item.pitch)) : cr;
                  if (e) score->select(e,SelectType::ADD);
                  }
            }
      else if (range) {
            Segment* begin=score->tick2leftSegment(rangeStart);
            Segment* limit=score->tick2segment(rangeEnd,true,SegmentType::ChordRest);
            if (!limit && rangeEnd<score->lastMeasure()->endTick()) {
                  auto left=score->tick2leftSegment(rangeEnd);
                  limit=left ? left->next1(SegmentType::ChordRest) : nullptr;
                  }
            score->selection().setRange(begin,limit,staffStart,staffEnd);
            score->selection().updateSelectedElements();
            }
      is = saved;
      is.setSegment(hasInput ? relocated(score,inputTick,inputTrack) : nullptr);
      is.setLastSegment(hasLast ? relocated(score,lastTick,inputTrack) : nullptr);
      return true;
      }

bool InputRhythm::changeDuration(Score* score, ChordRest* cr, const TDuration& duration)
      {
      const Fraction ticks = duration.type()==TDuration::DurationType::V_MEASURE
            ? cr->measure()->stretchedLen(cr->staff()) : duration.fraction();
      return changeDuration(score,cr,ticks);
      }

bool InputRhythm::changeDuration(Score* score, ChordRest* cr, const Fraction& duration)
      {
      const Fraction tick = cr->tick(); const int track = cr->track();
      bool safe = enabled();
      for (auto part : chain(cr)) safe = safe && eligible(part);
      score->changeCRlen(cr,duration);
      return safe && normalize(score,score->findCR(tick,track));
      }

Note* InputRhythm::addToChain(Score* score, Chord* chord, const NoteVal& value,
      bool forceAccidental, InputState* external)
      {
      auto group = chain(chord);
      bool safe = enabled() && group.size()>1;
      for (auto part : group) safe = safe && eligible(part);
      if (!safe) return score->addNote(chord,value,forceAccidental,external);
      InputState& state = external ? *external : score->inputState();
      const InputState saved = state;
      Note* previous = nullptr; Note* result = nullptr;
      for (auto part : group) {
            InputState local = saved;
            local.setSegment(part->segment());
            Note* added = score->addNote(toChord(part),value,forceAccidental,&local);
            if (previous) {
                  Tie* tie = new Tie(score);
                  tie->setStartNote(previous); tie->setEndNote(added);
                  tie->setTrack(added->track()); tie->setTick(previous->tick()); tie->setTick2(added->tick());
                  score->undoAddElement(tie);
                  }
            previous = added;
            if (part == chord) result = added;
            }
      if (!external) score->select(result);
      state = saved;
      if (saved.segment() == chord->segment()
            && saved.noteEntryMethod()!=NoteEntryMethod::REALTIME_AUTO
            && saved.noteEntryMethod()!=NoteEntryMethod::REALTIME_MANUAL) {
            state.setSegment(group.back()->segment());
            state.moveToNextInputPos();
            }
      return result;
      }
}
