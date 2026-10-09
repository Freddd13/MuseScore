// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "libmscore/score.h"
#include "libmscore/textline.h"
#include "libmscore/tempoexpression.h"
#include "libmscore/tempo.h"
#include <QPainter>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QMenu>
#include <QPushButton>
#include <QCursor>
#include <algorithm>
#include <cmath>
namespace Ms {
void PerformanceEditor::refreshTempoCurves()
      {
      _tempoCurves.clear(); _tempoPlot.clear(); _curveIndex = -1;
      if (!_score) { _selectedCurve = nullptr; return; }
      bool selectedPresent = false;
      for (auto line : TempoExpression::curves(_score)) {
            if (!TempoExpression::conflict(_score, line->tick().ticks(), line->tick2().ticks(), line).isEmpty()) continue;
            _tempoCurves.append({line, line->tick().ticks(), line->tick2().ticks(), line->ritDerivedStart() * 60,
                  TempoExpression::target(line, line->ritDerivedStart()) * 60, line->ritCurve()});
            _tempoMax = qMax(_tempoMax, qMax(_tempoCurves.back().start, _tempoCurves.back().target));
            selectedPresent |= _selectedCurve == line;
            }
      if (!selectedPresent) _selectedCurve = nullptr;
      if (_tempoCurves.isEmpty()) return;
      for (const auto& event : *_score->masterScore()->tempomap()) {
            double bpm = event.second.tempo * 60; bool ramp = false;
            for (const auto& curve : _tempoCurves) if (event.first >= curve.from && event.first < curve.until) {
                  bpm = curve.start + (curve.target - curve.start) * std::pow(double(event.first - curve.from) / (curve.until - curve.from), curve.shape); ramp = true; break;
                  }
            _tempoPlot.append({event.first, bpm, ramp});
            }
      // Curve end precedes a same-tick restore marker, showing both values.
      for (const auto& curve : _tempoCurves) _tempoPlot.append({curve.until, curve.target, true, true});
      std::stable_sort(_tempoPlot.begin(), _tempoPlot.end(), [](const TempoPlotPoint& a, const TempoPlotPoint& b) {
            return a.tick != b.tick ? a.tick < b.tick : a.end && !b.end;
            });
      if (_tempoPlot.back().tick < _endTick) _tempoPlot.append({_endTick, _score->tempo(Fraction::fromTicks(_endTick)) * 60, false});
      }

bool PerformanceEditor::paintTempoCurves(QPainter& painter, const QRectF& rect, bool onScore)
      {
      if (_tempoCurves.isEmpty() || _tempoPlot.isEmpty()) return false;
      const int from = tickForX(rect.left(), onScore), until = tickForX(rect.right(), onScore);
      auto first = std::lower_bound(_tempoPlot.cbegin(), _tempoPlot.cend(), from, [](const TempoPlotPoint& p, int tick) { return p.tick < tick; });
      if (first != _tempoPlot.cbegin()) --first;
      auto bpmAt = [this](const TempoPlotPoint& p) {
            if (p.ramp) return p.bpm;
            auto original = std::upper_bound(_tempos.cbegin(), _tempos.cend(), p.tick, [](int tick, const TempoInfo& info) { return tick < info.tick; });
            const int lastOriginal = original == _tempos.cbegin() ? -1 : (original - 1)->tick;
            auto draft = _tempoDraft.upperBound(p.tick);
            if (draft != _tempoDraft.cbegin()) { --draft; if (draft.key() >= lastOriginal) return draft.value(); }
            return p.bpm;
            };
      painter.setPen(QPen(_appearance.colors[PerformanceAppearance::Tempo], 2));
      for (auto current = first; current != _tempoPlot.cend() && current + 1 != _tempoPlot.cend();) {
            auto following = current + 1;
            // Draw at most one segment per screen pixel, but keep curve ends
            // and same-tick restoration jumps. Playback retains every map node.
            if (current->ramp && !current->end && following->ramp && !following->end) {
                  auto curve = std::upper_bound(_tempoCurves.cbegin(), _tempoCurves.cend(), current->tick,
                        [](int tick, const TempoCurveInfo& info) { return tick < info.from; });
                  if (curve != _tempoCurves.cbegin()) {
                        --curve;
                        const int wanted = qMin(curve->until, tickForX(xForTick(current->tick, onScore) + 1, onScore));
                        auto candidate = std::lower_bound(following, _tempoPlot.cend(), wanted,
                              [](const TempoPlotPoint& p, int tick) { return p.tick < tick; });
                        if (candidate != _tempoPlot.cend() && candidate->tick <= curve->until) following = candidate;
                        }
                  }
            const auto& next = *following;
            const double x0 = xForTick(current->tick, onScore), x1 = xForTick(next.tick, onScore);
            const double y0 = yForValue(bpmAt(*current), rect, onScore), y1 = yForValue(bpmAt(next), rect, onScore);
            if (current->ramp && next.tick > current->tick) painter.drawLine(QPointF(x0, y0), QPointF(x1, y1));
            else { painter.drawLine(QPointF(x0, y0), QPointF(x1, y0)); painter.drawLine(QPointF(x1, y0), QPointF(x1, y1)); }
            if (next.tick > until) break;
            current = following;
            }
      for (int i = 0; i < _tempoCurves.size(); ++i) {
            const auto& curve = _curveIndex == i && _dragging ? _curveDraft : _tempoCurves[i];
            if (curve.until < from || curve.from > until) continue;
            const QColor color = _appearance.colors[_curveIndex == i && _dragging ? PerformanceAppearance::Preview : PerformanceAppearance::Tempo];
            if (_curveIndex == i && _dragging) {
                  QPainterPath preview;
                  for (int sample = 0; sample <= 128; ++sample) {
                        const double fraction = sample / 128.0;
                        QPointF point(xForTick(qRound(curve.from + (curve.until - curve.from) * fraction), onScore),
                              yForValue(curve.start + (curve.target - curve.start) * std::pow(fraction, curve.shape), rect, onScore));
                        if (!sample) preview.moveTo(point); else preview.lineTo(point);
                        }
                  painter.setPen(QPen(color, 2, Qt::DashLine)); painter.drawPath(preview);
                  }
            painter.setPen(QPen(color, 2)); painter.setBrush(_appearance.colors[PerformanceAppearance::Background]);
            const int ticks[] = {curve.from, curve.until, (curve.from + curve.until) / 2};
            const double values[] = {curve.start, curve.target, curve.start + (curve.target - curve.start) * std::pow(.5, curve.shape)};
            for (int grip = 0; grip < 3; ++grip) {
                  QPointF point(xForTick(ticks[grip], onScore), yForValue(values[grip], rect, onScore));
                  if (grip == 2) { QPolygonF diamond; diamond << point + QPointF(0,-5) << point + QPointF(5,0) << point + QPointF(0,5) << point + QPointF(-5,0); painter.drawPolygon(diamond); }
                  else painter.drawEllipse(point, 4, 4);
                  }
            }
      return true;
      }

bool PerformanceEditor::beginTempoCurve(QPointF point, const QRectF& rect, bool onScore)
      {
      _curveIndex = -1;
      for (int i = 0; i < _tempoCurves.size(); ++i) {
            const auto& curve = _tempoCurves[i];
            const int ticks[] = {curve.from, curve.until, (curve.from + curve.until) / 2};
            const double values[] = {curve.start, curve.target, curve.start + (curve.target - curve.start) * std::pow(.5, curve.shape)};
            for (int grip = 0; grip < 3; ++grip)
                  if (QLineF(point, QPointF(xForTick(ticks[grip], onScore), yForValue(values[grip], rect, onScore))).length() < 10) {
                        _curveIndex = i; _curveGrip = grip; _curveDraft = curve; _selectedCurve = curve.line;
                        QSignalBlocker blocker(_number); _number->setRange(grip == 2 ? .1 : 5, grip == 2 ? 8 : 999);
                        findChild<QPushButton*>("performanceWriteValue")->setText(tr("写入曲线参数"));
                        _number->setPrefix(QString()); _number->setSuffix(grip == 2 ? tr(" 曲率") : " BPM"); _number->setValue(grip == 2 ? curve.shape : values[grip]); return true;
                        }
            }
      const int tick = tickForX(point.x(), onScore);
      for (const auto& curve : _tempoCurves) if (tick >= curve.from && tick < curve.until) {
            _dragging = false; status(tr("此范围关联 rit.：拖两端圆点或中间曲率菱形；右键可明确解除关联。")); return true;
            }
      _selectedCurve = nullptr; findChild<QPushButton*>("performanceWriteValue")->setText(tr("写入速度节点")); _number->setPrefix(QString()); _number->setRange(5, 999); _number->setSuffix(" BPM"); return false;
      }
void PerformanceEditor::moveTempoCurve(QPointF point)
      {
      const auto& original = _tempoCurves[_curveIndex];
      _curveDraft = original;
      const double delta = valueForY(point.y(), _gestureLane, _scoreGesture) - valueForY(_press.y(), _gestureLane, _scoreGesture);
      if (_curveGrip == 2) {
            if (std::abs(original.target - original.start) > .001) {
                  double value = original.start + (original.target - original.start) * std::pow(.5, original.shape) + delta;
                  const double fraction = qBound(.001, (value - original.start) / (original.target - original.start), .999);
                  _curveDraft.shape = qBound(.1, std::log(fraction) / std::log(.5), 8.0);
                  }
            }
      else {
            const int tick = nearestSegment(tickForX(point.x(), _scoreGesture));
            if (_curveGrip == 0) {
                  if (tick >= 0 && tick < original.until) _curveDraft.from = tick;
                  _curveDraft.start = qBound(5.0, original.start + delta, 999.0);
                  if (!original.line->ritTargetMode()) _curveDraft.target = qBound(5.0, _curveDraft.start * original.line->ritTarget() / 100, 999.0);
                  }
            else { if (tick > original.from && tick <= _endTick) _curveDraft.until = tick; _curveDraft.target = qBound(5.0, original.target + delta, 999.0); }
            }
      QSignalBlocker blocker(_number); _number->setValue(_curveGrip == 2 ? _curveDraft.shape : _curveGrip ? _curveDraft.target : _curveDraft.start);
      status(tr("rit. 预览：%1 → %2 BPM，曲率 %3；松开一次写入，Esc 取消。")
            .arg(_curveDraft.start,0,'f',2).arg(_curveDraft.target,0,'f',2).arg(_curveDraft.shape,0,'f',2));
      }
bool PerformanceEditor::finishTempoCurve(QString* error)
      {
      if (_curveIndex < 0 || _curveIndex >= _tempoCurves.size()) return false;
      const auto original = _tempoCurves[_curveIndex]; _curveIndex = -1;
      QMap<Pid,QVariant> properties;
      properties[Pid::SPANNER_TICK] = QVariant::fromValue(Fraction::fromTicks(_curveDraft.from));
      properties[Pid::SPANNER_TICKS] = QVariant::fromValue(Fraction::fromTicks(_curveDraft.until - _curveDraft.from));
      if (_curveGrip == 0 && std::abs(_curveDraft.start - original.start) > .0001) properties[Pid::RIT_START_BPM] = _curveDraft.start;
      if (_curveGrip == 1) properties[Pid::RIT_TARGET] = original.line->ritTargetMode() ? _curveDraft.target : _curveDraft.target / _curveDraft.start * 100;
      if (_curveGrip == 2) properties[Pid::RIT_CURVE] = _curveDraft.shape;
      return ParameterEdit::editTempoCurve(original.line, properties, error);
      }
bool PerformanceEditor::setTempoCurveNumber(double value)
      {
      if (!_selectedCurve) return false;
      QString error; QMap<Pid,QVariant> values;
      if (_curveGrip == 2) values[Pid::RIT_CURVE] = value;
      else if (_curveGrip == 0) values[Pid::RIT_START_BPM] = value;
      else values[Pid::RIT_TARGET] = _selectedCurve->ritTargetMode() ? value : value / (_selectedCurve->ritDerivedStart() * 60) * 100;
      _committing = true; ParameterEdit::editTempoCurve(_selectedCurve, values, &error); _committing = false;
      _dirty = true; scheduleRefresh(); status(error); return true;
      }
bool PerformanceEditor::tempoCurveMenu(QPointF point, bool onScore)
      {
      if (_parameter->currentIndex() != 1) return false;
      const int tick = tickForX(point.x(), onScore);
      TextLine* target = nullptr;
      for (const auto& curve : _tempoCurves) if (tick >= curve.from && tick <= curve.until) { target = curve.line; break; }
      if (!target) return false;
      QMenu menu(this); auto detach = menu.addAction(tr("解除 rit. 播放关联（保留文字线，恢复原速度标记）"));
      auto reset = menu.addAction(tr("曲率恢复线性")); const auto chosen = menu.exec(QCursor::pos()); QString error;
      if (chosen == detach) ParameterEdit::detachTempoCurve(target, &error);
      else if (chosen == reset) ParameterEdit::editTempoCurve(target, {{Pid::RIT_CURVE,1.0}}, &error);
      status(error); return true;
      }
}
