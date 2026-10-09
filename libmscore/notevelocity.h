// Shared integer-percent velocity semantics. No new score properties or file format.
#ifndef MS_NOTEVELOCITY_H
#define MS_NOTEVELOCITY_H

#include "note.h"
#include <QtGlobal>
#include <cmath>
#include <limits>

namespace Ms {
namespace NoteVelocity {
constexpr int minOffset = -127;
constexpr int maxOffset = 12600; // 1 -> 127 at the smallest MIDI base velocity.

inline int effective(int base, Note::ValueType type, int raw)
      {
      const qint64 value = type == Note::ValueType::USER_VAL ? raw
            : qint64(base) + qint64(base) * raw / 100;
      return int(qBound<qint64>(1, value, 127));
      }

inline int offsetFor(int base, int velocity)
      {
      base = qMax(1, base);
      velocity = qBound(1, velocity, 127);
      const double ideal = 100.0 * (velocity - base) / base;
      const int center = int(std::lround(ideal));
      int best = qBound(minOffset, center, maxOffset);
      int error = std::abs(effective(base, Note::ValueType::OFFSET_VAL, best) - velocity);
      for (int candidate = qMax(minOffset, center - 3); candidate <= qMin(maxOffset, center + 3); ++candidate) {
            const int difference = std::abs(effective(base, Note::ValueType::OFFSET_VAL, candidate) - velocity);
            if (difference < error || (difference == error && std::abs(candidate - ideal) < std::abs(best - ideal))) {
                  best = candidate;
                  error = difference;
                  }
            }
      return best;
      }

// Must be called on the GUI thread while rendering is idle, after updateVelo().
int referenceBase(const Note*, int globalMethod = 1, int eventIndex = 0);
int referenceBaseAt(const Note*, int tick, int globalMethod = 1);
int playbackBase(const Note*, int tick, qreal articulationMultiplier, bool fixedMax);

inline int eventVelocity(int customized, double factor)
      { return qBound(1, int(std::lround(customized * factor)), 127); }

inline int converted(const Note* note, Note::ValueType target, int base)
      {
      const int value = effective(base, note->veloType(), note->veloOffset());
      return target == Note::ValueType::USER_VAL ? value : offsetFor(base, value);
      }
}
}
#endif
