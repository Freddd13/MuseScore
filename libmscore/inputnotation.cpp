// SPDX-License-Identifier: GPL-2.0-or-later
#include "inputnotation.h"
#include "beam.h"
#include "chord.h"
#include "measure.h"
#include "note.h"
#include "segment.h"
#include "staff.h"
#include "tie.h"
#include <QSet>
#include <deque>
#include <climits>

namespace Ms { namespace InputNotation {
namespace {
bool joinBeams = false, coordinateStems = false;
bool ordinary(const Chord* chord, const Measure* measure)
      {
      if (!chord || chord->measure() != measure || chord->isGrace() || chord->staffMove()
          || chord->tuplet() || chord->tremolo() || chord->stemDirection() != Direction::AUTO
          || chord->beamMode() != Beam::Mode::AUTO || !chord->staff()->isPitchedStaff(chord->tick())
          || measure->hasVoices(chord->staffIdx(), measure->tick(), measure->ticks())) return false;
      auto beam = chord->beam();
      return !beam || (beam->beamDirection() == Direction::AUTO && !beam->userModified()
            && !beam->cross() && beam->growLeft() == 1 && beam->growRight() == 1);
      }
}
void setTiedBeams(bool value) { joinBeams = value; }
void setCoherentStems(bool value) { coordinateStems = value; }
bool tiedBeams() { return joinBeams; }
bool coherentStems() { return coordinateStems; }

bool joinTiedChords(const ChordRest* previous, const ChordRest* current)
      {
      if (!joinBeams || !previous || !current || !previous->isChord() || !current->isChord()
          || previous->measure() != current->measure() || previous->track() != current->track()
          || previous->endTick() != current->tick() || previous->tuplet() || current->tuplet()
          || previous->beamMode() != Beam::Mode::AUTO || current->beamMode() != Beam::Mode::AUTO
          || previous->durationType().hooks() < 1 || current->durationType().hooks() < 1) return false;
      auto first = toChord(previous); auto second = toChord(current);
      if (first->staffMove() || second->staffMove() || first->isGrace() || second->isGrace()
          || first->notes().size() != second->notes().size()) return false;
      for (auto note : first->notes()) if (!note->tieFor() || !note->tieFor()->endNote()
            || note->tieFor()->endNote()->chord() != second) return false;
      return true;
      }

void coordinateTieStems(Measure* measure)
      {
      if (!coordinateStems || !measure) return;
      QSet<Chord*> visited;
      for (auto segment = measure->first(SegmentType::ChordRest); segment; segment = segment->next(SegmentType::ChordRest))
            for (auto element : segment->elist()) {
                  if (!element || !element->isChord()) continue;
                  auto first = toChord(element);
                  if (visited.contains(first)) continue;
                  bool linked = false;
                  for (auto note : first->notes()) linked |= note->tieFor() || note->tieBack();
                  if (!linked) continue;
                  std::deque<Chord*> pending {first}; QVector<Chord*> chords; QSet<Beam*> beams;
                  bool safe = true, hasTie = false; int low = INT_MAX, high = INT_MIN, balance = 0;
                  QSet<const Note*> counted; QSet<Chord*> local;
                  while (!pending.empty()) {
                        auto chord = pending.front(); pending.pop_front();
                        if (local.contains(chord)) continue;
                        local.insert(chord); visited.insert(chord); chords.append(chord);
                        safe &= ordinary(chord, measure) && chord->track() == first->track();
                        if (!safe || chords.size() > 64 || beams.size() > 8) { safe = false; break; }
                        if (auto beam = chord->beam()) if (!beams.contains(beam)) {
                              beams.insert(beam);
                              for (auto member : beam->elements()) {
                                    if (!member->isChord()) { safe = false; break; }
                                    pending.push_back(toChord(member));
                                    }
                              }
                        for (auto note : chord->notes()) {
                              int line = note->line(), middle = chord->staff()->middleLine(chord->tick());
                              low = qMin(low, line); high = qMax(high, line);
                              if (qAbs(line - middle) > 4) safe = false;
                              const Note* logical = note;
                              for (int depth=0;depth<64&&logical->tieBack()&&logical->tieBack()->startNote()
                                   &&logical->tieBack()->startNote()->chord()
                                   &&logical->tieBack()->startNote()->chord()->measure()==measure;++depth) logical=logical->tieBack()->startNote();
                              if (!counted.contains(logical)) { counted.insert(logical); balance += line > middle ? 1 : line < middle ? -1 : 0; }
                              for (auto tie : {note->tieFor(), note->tieBack()}) {
                                    if (!tie || !tie->startNote() || !tie->endNote()) continue;
                                    auto other = tie->startNote() == note ? tie->endNote() : tie->startNote();
                                    if (!other->chord()) { safe = false; continue; }
                                    if (other->chord()->measure() != measure) continue;
                                    if (other->line() != line) { safe = false; continue; }
                                    hasTie = true; pending.push_back(other->chord());
                                    }
                              }
                        }
                  // Only compact ordinary groups have freedom to share a direction.
                  // Manual stems/beams, multiple voices and wide registers retain native rules.
                  if (!safe || !hasTie || chords.size() < 2 || high - low > 4) continue;
                  const bool up = balance > 0;
                  for (auto beam : beams) beam->setUp(up);
                  for (auto chord : chords) if (chord->up() != up) { chord->setUp(up); chord->layoutStem1(); }
                  }
      }
} }
