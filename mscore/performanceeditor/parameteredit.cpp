#include "parameteredit.h"
#include "mscore/seq.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/measure.h"
#include "libmscore/chordrest.h"
#include "libmscore/tempotext.h"
#include "libmscore/pedal.h"
#include "libmscore/spanner.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/undo.h"
#include <QRegularExpression>

namespace Ms {
namespace {
bool editable(Score* score)
      { return score && !score->readOnly() && !score->isPlaying() && (!seq || (!seq->isPlaying() && seq->backgroundRenderingIdle())); }
// Paired boundaries wrap standard linked-element commands on apply, undo and redo.
class TempoBoundary : public UndoCommand {
      Score* _score;
      bool _begin;
      void flip(EditData*) override
            {
            if (_begin) _score->beginTempoEdit();
            else _score->endTempoEdit();
            _begin = !_begin;
            }
   public:
      TempoBoundary(Score* score, bool begin) : _score(score), _begin(begin) {}
      UNDO_NAME("PerformanceTempoBatchBoundary")
      };
}
namespace ParameterEdit {
bool velocities(Score* score, const QMap<Note*, VelocityEdit>& edits)
      {
      if (!editable(score)) return false;
      bool changed = false;
      for (auto i = edits.cbegin(); i != edits.cend(); ++i) {
            if (!i.key() || i.key()->score() != score || (i->type != Note::ValueType::USER_VAL && i->type != Note::ValueType::OFFSET_VAL)) return false;
            if (i.key()->veloType() != i->type || i.key()->veloOffset() != i->raw) changed = true;
            }
      if (!changed) return false;
      score->startCmd();
      for (auto i = edits.cbegin(); i != edits.cend(); ++i) {
            i.key()->undoChangeProperty(Pid::VELO_TYPE, int(i->type));
            i.key()->undoChangeProperty(Pid::VELO_OFFSET, i->raw);
            }
      score->endCmd();
      return true;
      }

bool tempos(Score* score, const QMap<int, double>& bpm, int unlockedTick, QString* error)
      {
      if (!editable(score)) return false;
      score = score->masterScore();
      struct Point { Segment* segment; TempoText* text; double bpm; };
      QVector<Point> points;
      for (auto i = bpm.cbegin(); i != bpm.cend(); ++i) {
            if (!std::isfinite(i.value()) || i.value() < 5 || i.value() > 999) {
                  if (error) *error = QObject::tr("速度必须在 5–999 BPM 内。");
                  return false;
                  }
            auto segment = score->tick2segment(Fraction::fromTicks(i.key()), false, SegmentType::ChordRest);
            if (!segment) continue;
            TempoText* existing = nullptr;
            int count = 0;
            for (Element* annotation : segment->annotations())
                  if (annotation->isTempoText()) { existing = toTempoText(annotation); ++count; }
            if (count > 1) {
                  if (error) *error = QObject::tr("同一时刻有多个速度标记，请先在谱面明确编辑目标。");
                  return false;
                  }
            if (existing && existing->visible() && i.key() != unlockedTick) continue;
            if (existing && existing->isRelative()) {
                  if (i.key() == unlockedTick) {
                        if (error) *error = QObject::tr("相对速度标记请在原检视器编辑，不能直接当作绝对 BPM 改写。");
                        return false;
                        }
                  continue;
                  }
            if (!existing || !qFuzzyCompare(existing->tempo(), i.value() / 60.0))
                  points.append({segment, existing, i.value()});
            }
      if (points.isEmpty()) return false;
      score->startCmd();
      score->undo(new TempoBoundary(score, true));
      for (const Point& point : points) {
            if (!point.text) {
                  auto text = new TempoText(score);
                  text->setParent(point.segment);
                  text->setTrack(0);
                  text->setVisible(false);
                  text->setFollowText(false);
                  text->setTempo(point.bpm / 60.0);
                  text->setXmlText(QString("<sym>metNoteQuarterUp</sym> = %1").arg(point.bpm, 0, 'f', 2));
                  score->undoAddElement(text);
                  }
            else {
                  point.text->undoSetFollowText(false);
                  QString text = point.text->xmlText();
                  const QRegularExpression number("(=\\s*)([0-9]+(?:[.,][0-9]+)?)");
                  const auto match = number.match(text);
                  if (match.hasMatch()) {
                        // Preserve the beat unit, dots and explanatory text.
                        const double factor = TempoText::findTempoValue(text);
                        const double beat = factor > 0 ? factor * 60.0 : 1.0;
                        text.replace(match.capturedStart(2), match.capturedLength(2), QString::number(point.bpm / beat, 'f', 2));
                        static_cast<Element*>(point.text)->undoChangeProperty(Pid::TEXT, text);
                        }
                  point.text->undoSetTempo(point.bpm / 60.0);
                  }
            }
      score->undo(new TempoBoundary(score, false));
      score->endCmd();
      return true;
      }

bool pedal(Score* score, int track, int from, int until, Pedal* target, QString* error)
      {
      if (!editable(score) || from < 0 || until <= from
            || track < 0 || track >= score->nstaves() * VOICES) return false;
      if (!score->lastMeasure() || until > score->lastMeasure()->endTick().ticks() || (target && (target->score() != score || target->track() != track))) return false;
      auto start = score->tick2segment(Fraction::fromTicks(from), false, SegmentType::ChordRest);
      bool startValid = false;
      if (start) for (int voice = 0; voice < VOICES; ++voice) {
            auto element = start->element((track / VOICES) * VOICES + voice);
            if (element && element->isChordRest()) startValid = true;
            }
      auto end = score->findCRinStaff(Fraction::fromTicks(until), track / VOICES);
      if (!startValid || !end || (end->tick() + end->actualTicks()).ticks() != until) {
            if (error) *error = QObject::tr("踏板须吸附对应谱表的音符/休止边界。");
            return false;
            }
      const Part* part = score->staff(track / VOICES)->part();
      for (const auto& pair : score->spannerMap().map()) {
            Spanner* span = pair.second;
            if (span->isPedal() && span != target && span->part() == part
                  && span->tick().ticks() < until && span->tick2().ticks() > from) {
                  if (error) *error = QObject::tr("与已有踏板重叠；请拖动已有踏板的端点。");
                  return false;
                  }
            }
      if (target && target->tick().ticks() == from && target->tick2().ticks() == until) return false;
      score->startCmd();
      if (target) {
            target->undoChangeProperty(Pid::SPANNER_TICK, QVariant::fromValue(Fraction::fromTicks(from)));
            target->undoChangeProperty(Pid::SPANNER_TICKS, QVariant::fromValue(Fraction::fromTicks(until - from)));
            }
      else {
            auto span = new Pedal(score);
            span->setTrack(track); span->setTrack2(track);
            span->setTick(Fraction::fromTicks(from));
            span->setTicks(Fraction::fromTicks(until - from));
            score->undoAddElement(span);
            }
      score->endCmd();
      return true;
      }
}
}
