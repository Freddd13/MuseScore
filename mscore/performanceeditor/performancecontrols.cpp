// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "mscore/scoreview.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/pedal.h"
#include "libmscore/segment.h"
#include "libmscore/chordrest.h"
#include "libmscore/sig.h"
#include <QMenu>
#include <QComboBox>
#include <algorithm>
namespace Ms {
bool PerformanceEditor::staffEnabled(int track) const
      { return !_excludedStaves.contains(track / VOICES); }
void PerformanceEditor::rebuildStaffMenu()
      {
      _staffMenu->clear();
      auto all = _staffMenu->addAction(tr("显示所有谱表"));
      connect(all, &QAction::triggered, this, [this] { cancelGesture(); _excludedStaves.clear(); rebuildFilter(); });
      if (!_score) return;
      for (int staff = 0; staff < _score->nstaves(); ++staff) {
            auto action = _staffMenu->addAction(tr("谱表 %1 · %2").arg(staff + 1).arg(_score->staff(staff)->part()->partName()));
            action->setCheckable(true); action->setChecked(!_excludedStaves.contains(staff)); action->setObjectName(QString("performanceStaff%1").arg(staff));
            connect(action, &QAction::toggled, this, [this, staff](bool on) { finishWheel(); cancelGesture(); if (on) _excludedStaves.remove(staff); else _excludedStaves.insert(staff); rebuildFilter(); });
            }
      }
QColor PerformanceEditor::scoreNoteColor(int index) const
      {
      const auto& note = _notes[index];
      if (_scoreVoiceColors) {
            return MScore::selectColor[note.track % VOICES];
            }
      const auto edit = _pending.value(note.note, {note.type, note.raw});
      QColor color = _appearance.noteColor(NoteVelocity::effective(note.base, edit.type, edit.raw), note.track);
      return QColor::fromHsvF(color.hsvHueF(), color.hsvSaturationF() * .55, color.valueF() * .65);
      }
void PerformanceEditor::resetScoreRange(bool dataRange)
      {
      auto range = PerformanceViewport::limits(rangeKind());
      if (dataRange && rangeKind() != 3) {
            double low = range.maximum, high = range.minimum;
            const bool selected = std::any_of(_systemIndices.cbegin(), _systemIndices.cend(), [this](int i) { return _notes[i].selected && noteEditable(_notes[i]); });
            if (rangeKind() == 2) for (const auto& s : _systemSegments) { low = qMin(low, s.bpm); high = qMax(high, s.bpm); }
            else for (int i : _systemIndices) if (noteEditable(_notes[i]) && (!selected || _notes[i].selected)) { low = qMin(low, noteValue(i)); high = qMax(high, noteValue(i)); }
            if (low <= high) {
                  const auto limit = range;
                  const double minimumSpan = rangeKind() == 2 ? 5 : 8, pad = qMax(minimumSpan / 2, (high - low) * .1);
                  range = {qMax(limit.minimum, low - pad), qMin(limit.maximum, high + pad)};
                  if (range.maximum - range.minimum < minimumSpan) { range.minimum = qBound(limit.minimum, (low + high - minimumSpan) / 2, limit.maximum - minimumSpan); range.maximum = range.minimum + minimumSpan; }
                  }
            }
      _scoreViewport.ranges[rangeKind()] = range; invalidateOverlay();
      }
void PerformanceEditor::scoreRangeControl(int control)
      {
      finishWheel(); cancelGesture();
      auto range = _scoreViewport.ranges[rangeKind()];
      if (control < 2) _scoreViewport.zoomRange(rangeKind(), control ? 1.5 : 1 / 1.5, (range.minimum + range.maximum) / 2, false);
      else if (control < 4) _scoreViewport.panRange(rangeKind(), (control == 2 ? 1 : -1) * (range.maximum - range.minimum) / 8);
      else resetScoreRange(control == 4);
      invalidateOverlay();
      }
int PerformanceEditor::nearestSegment(int tick) const
      {
      auto next = std::lower_bound(_segments.cbegin(), _segments.cend(), tick, [](const SegmentInfo& s, int t) { return s.tick < t; });
      if (next == _segments.cend()) return _segments.isEmpty() ? -1 : _segments.back().tick;
      if (next == _segments.cbegin()) return next->tick;
      return tick - (next - 1)->tick < next->tick - tick ? (next - 1)->tick : next->tick;
      }
bool PerformanceEditor::pedalVisible(const PedalInfo& pedal) const
      {
      return _score && staffEnabled(pedal.track) && (_scope->currentIndex() == 2
            || (_scope->currentIndex() == 1 ? pedal.track / VOICES == _contextTrack / VOICES
                  : pedal.pedal->part() == _score->staff(_contextTrack / VOICES)->part()));
      }
bool PerformanceEditor::pedalBoundary(int tick, int track, bool end) const
      {
      // Score::tick2segment still scans measures in MS3. Cache boundaries at the
      // content snapshot, so hover, drawing and pedal drags never traverse a score.
      const auto& cache = _pedalBoundaries[end ? 1 : 0];
      auto staff = cache.constFind(track / VOICES);
      return staff != cache.cend() && staff->contains(tick);
      }
QString PerformanceEditor::parameterTooltip(QPointF point, bool onScore) const
      {
      const int tick = tickForX(point.x(), onScore);
      int bar, beat, sub; _score->sigmap()->tickValues(tick, &bar, &beat, &sub);
      const QString position = tr("%1.%2 +%3 ticks").arg(bar + 1).arg(beat + 1).arg(sub);
      if (_parameter->currentIndex() == 1)
            return tr("%1 · 四分音符 %2 BPM\n节点模式：空白点按添加、上下拖动改值。\n原速度文字受保护；请点中方形节点再修改。").arg(position).arg(_score->tempo(Fraction::fromTicks(tick)) * 60, 0, 'f', 2);
      for (const auto& pedal : _pedals) if (pedalVisible(pedal) && tick >= pedal.from - 10 && tick <= pedal.until + 10)
            return tr("%1 · 踏板开 127\n谱表 %2 · %3 → %4 ticks\n%5；%6\n顶端拖动端点；菱形／虚线表示离开音符边界。").arg(position).arg(pedal.track / VOICES + 1).arg(pedal.from).arg(pedal.until)
                  .arg(pedalBoundary(pedal.from, pedal.track, false) ? tr("起点在音符边界") : tr("起点为细分／自由定位"))
                  .arg(pedalBoundary(pedal.until, pedal.track, true) ? tr("终点在音符边界") : tr("终点为细分／自由定位"));
      return tr("%1 · 踏板关 0\n水平拖动建立踏板；%2。").arg(position, _pedalGrid->currentText());
      }
}
