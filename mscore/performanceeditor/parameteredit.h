#ifndef MS_PARAMETEREDIT_H
#define MS_PARAMETEREDIT_H
#include <QMap>
#include <QString>
#include "libmscore/notevelocity.h"
namespace Ms {
class Score;
class Pedal;
struct VelocityEdit {
      Note::ValueType type;
      int raw;
      bool operator==(const VelocityEdit& other) const { return type == other.type && raw == other.raw; }
      };
namespace ParameterEdit {
bool velocities(Score*, const QMap<Note*, VelocityEdit>&);
// Map keys are existing ChordRest ticks; visible markers are protected unless explicitly selected.
bool tempos(Score*, const QMap<int, double>& bpm, int unlockedTick, QString* error = nullptr);
bool pedal(Score*, int track, int from, int until, Pedal* target, QString* error = nullptr);
}
}
#endif
