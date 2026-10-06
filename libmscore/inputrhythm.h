// GPL-2.0-or-later. Optional input policy; never called by import or layout.
#ifndef MS_INPUTRHYTHM_H
#define MS_INPUTRHYTHM_H
namespace Ms {
class Score;
class ChordRest;
class Chord;
class Note;
class InputState;
class TDuration;
class Fraction;
struct NoteVal;
class InputRhythm {
   public:
      static bool enabled();
      static void setEnabled(bool);
      static bool eligible(ChordRest*);
      // Caller owns the existing undo transaction. Returns true only on regroup.
      static bool normalize(Score*, ChordRest*);
      static bool changeDuration(Score*, ChordRest*, const TDuration&);
      static bool changeDuration(Score*, ChordRest*, const Fraction&);
      static Note* addToChain(Score*, Chord*, const NoteVal&, bool forceAccidental, InputState* = nullptr);
      };
}
#endif
