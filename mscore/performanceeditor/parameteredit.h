#ifndef MS_PARAMETEREDIT_H
#define MS_PARAMETEREDIT_H
#include <QMap>
#include <QString>
#include "libmscore/notevelocity.h"
namespace Ms {
class Score;
class Pedal;
class Part;
namespace PedalPlayback { struct Settings; }
class TextLine;
class TempoText;
struct VelocityEdit {
      Note::ValueType type;
      int raw;
      bool operator==(const VelocityEdit& other) const { return type == other.type && raw == other.raw; }
      };
namespace ParameterEdit {
bool expression(Score*,const QMap<Note*,VelocityEdit>&,const QMap<int,double>&,QString* error = nullptr,bool saveOriginal = false,bool ownCommand = true);
bool writeTapTempo(Score*,int tick,double bpm,bool selectedExisting,QString* error = nullptr);
bool writeTapTempo(TempoText* target,double bpm,QString* error = nullptr);
bool velocities(Score*, const QMap<Note*, VelocityEdit>&);
// Map keys are existing ChordRest ticks; visible markers are protected unless explicitly selected.
bool tempos(Score*, const QMap<int, double>& bpm, int unlockedTick, QString* error = nullptr);
bool tempoCurve(Score*, int from, int until, TextLine* target, bool replace, QString* error = nullptr, const TextLine* prototype = nullptr);
bool restoreTempo(Score*, int tick, QString* error = nullptr, const TempoText* prototype = nullptr);
bool editTempoCurve(TextLine*, const QMap<Pid, QVariant>&, QString* error = nullptr);
bool detachTempoCurve(TextLine*, QString* error = nullptr);
bool pedal(Score*, int track, int from, int until, Pedal* target, QString* error = nullptr);
// Paint a held interval or an up gap without altering notes or unrelated instruments.
bool paintPedal(Score*, int track, int from, int until, bool down, QString* error = nullptr);
bool splitPedal(Score*, Pedal*, int tick, QString* error = nullptr);
bool removePedal(Score*, Pedal*, QString* error = nullptr);
bool pedalPlayback(Score*, Part*, Pedal*, const PedalPlayback::Settings&, bool inherit, QString* error = nullptr);
}
}
#endif
