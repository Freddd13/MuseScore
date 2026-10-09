#include "notevelocity.h"
#include "articulation.h"
#include "chord.h"
#include "instrument.h"
#include "part.h"
#include "score.h"
#include "staff.h"
#include "synthesizerstate.h"

namespace Ms {
namespace NoteVelocity {
int playbackBase(const Note* note, int tick, qreal multiplier, bool fixedMax)
      {
      int value = fixedMax ? 127 : note->staff()->velocities().val(Fraction::fromTicks(tick));
      value *= multiplier; // Match the renderer's integer truncation before customization.
      return qBound(1, value, 127); // collectNote clips its base before note customization.
      }

int referenceBase(const Note* note, int globalMethod, int eventIndex)
      {
      const Chord* chord = note->chord();
      if (chord->isGrace()) chord = toChord(chord->parent());
      int tick = chord->tick().ticks();
      if (!note->playEvents().empty())
            tick += chord->actualTicks().ticks() * note->playEvents().at(qBound(0, eventIndex, note->playEvents().size() - 1)).ontime() / 1000;
      return referenceBaseAt(note,tick,globalMethod);
      }

int referenceBaseAt(const Note* note, int tick, int globalMethod)
      {
      const Chord* chord=note->chord();
      if (chord->isGrace()) chord=toChord(chord->parent());
      Instrument* instrument = chord->part()->instrument(chord->tick());
      qreal multiplier = 1;
      for (const Articulation* articulation : chord->articulations())
            if (articulation->playArticulation()) multiplier *= articulation->velocityMultiplier(instrument);
      int method = note->score()->synthesizerState().method();
      if (method < 0) method = globalMethod < 0 ? 1 : globalMethod;
      return playbackBase(note, tick, multiplier, method == 2 && instrument->singleNoteDynamics());
      }
}
}
