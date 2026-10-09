// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "mscore/scoreview.h"
#include <QApplication>
#include <QPainter>
#include <QWheelEvent>
#include <QToolButton>
#include <QComboBox>
#include <QCheckBox>
#include <QToolTip>
#include <cmath>
namespace Ms {
void PerformanceEditor::finishWheel(bool commit)
      {
      if (_wheelIndex >= 0) { repaintWheelTarget(_wheelIndex); if (_wheelSurface) _wheelSurface->update(wheelBadge(_wheelSurface).adjusted(-2, -2, 2, 2).toAlignedRect()); }
      _wheelTimer.stop(); _wheelSurface = nullptr; _wheelIndex = -1; _wheelRemainder = 0; _wheelBefore.clear();
      if (!_dragging && !_marquee && !_seeking && !_rangePanning) qApp->removeEventFilter(this);
      if (commit) applyPending();
      }
void PerformanceEditor::cancelWheel()
      {
      if (_wheelIndex < 0) return;
      _pending = _wheelBefore; finishWheel(false); QToolTip::hideText();
      status(tr("已取消本次滚轮调整。")); updateSurfaces(); invalidateOverlay();
      }
bool PerformanceEditor::wheelVelocity(QWheelEvent* wheel, bool notes, bool onScore)
      {
      const auto modifiers = wheel->modifiers();
      const bool temporary = !onScore && (modifiers & Qt::AltModifier);
      if ((modifiers & Qt::ControlModifier) || (onScore && (modifiers & Qt::AltModifier)) || (!_wheelButton->isChecked() && !temporary)
            || !_score || _score->readOnly() || _dragging || _parameter->currentIndex() != 0) return false;
      // A new wheel event can arrive before the queued post-commit refresh.
      // Resolve that snapshot first instead of interpreting the event as scrolling.
      if (_dirty) refresh();
      if (_dirty) return false;
      const QPointF point = wheel->position();
      const bool overStrip = onScore && _band->isChecked() && _scoreFrame.contains(point);
      if (overStrip && !_scoreLane.contains(point)) return false;
      QWidget* surface = onScore ? static_cast<QWidget*>(_view) : (notes ? static_cast<QWidget*>(_noteCanvas) : _canvas);
      int target = -1;
      // Keep a stationary wheel burst attached to its first endpoint even when
      // the edited endpoint moves away. Moving to another note releases it.
      if (_wheelIndex >= 0 && _wheelSurface == surface && QLineF(point, _wheelPoint).length() <= 4) target = _wheelIndex;
      else if (onScore) {
            // The explicit wheel mode edits native note heads without prior selection.
            // Alt belongs to ScoreView navigation; score editing requires the mode.
            if (_band->isChecked() && _scoreLane.contains(point)) {
                  const auto candidates = hits(point, true, true); if (!candidates.isEmpty()) target = candidates.front();
                  }
            else if (_handles->isChecked()) {
                  auto candidates = _selectedIndices; if (_hover >= 0 && !candidates.contains(_hover)) candidates.append(_hover);
                  for (int i : candidates) if (QLineF(point, _view->matrix().mapRect(_notes[i].bounds).topRight() + QPointF(12, -12)).length() <= 9) { target = i; break; }
                  }
            if (target < 0 && !overStrip && (_wheelButton->isChecked() || temporary)) target = _noteIndex.value(_view->elementNear(_view->toLogical(point.toPoint())), -1);
            }
      else {
            buildGeometry(); const auto candidates = hits(point, !notes); if (!candidates.isEmpty()) target = candidates.front();
            }
      if (target < 0 || !noteEditable(_notes[target])) return false;
      const int delta = wheel->angleDelta().y(); const int pixels = wheel->pixelDelta().y();
      if (!delta && !pixels) return false; // Do not swallow horizontal-only trackpads.
      if (_wheelIndex != target || _wheelSurface != surface) {
            // Different targets form separate undo bursts. A native commit refresh
            // preserves sorted note indices, but never carries a dying pointer.
            if (_wheelIndex >= 0) { finishWheel(); refresh(); }
            if (_dirty || target >= _notes.size() || !noteEditable(_notes[target])) return true;
            _wheelPoint = point; _wheelSurface = surface;
            _wheelIndex = target; _wheelValue = noteValue(target); _wheelBefore = _pending;
            pauseFollow(); qApp->installEventFilter(this); QToolTip::hideText();
            status(tr("滚轮仅调整指向音：细调 1／Shift 粗调 8 · Esc 取消 · 停转后提交。"));
            }
      if (_wheelPoint != point) { surface->update(wheelBadge(surface).adjusted(-2, -2, 2, 2).toAlignedRect()); _wheelPoint = point; }
      _wheelRemainder += delta ? delta / 120.0 : pixels / 15.0;
      const int steps = int(std::trunc(_wheelRemainder)); _wheelRemainder -= steps;
      if (steps) {
            const auto limit = PerformanceViewport::limits(_axis->currentIndex() ? 1 : 0);
            _wheelValue = qBound(limit.minimum, _wheelValue + steps * ((modifiers & Qt::ShiftModifier) ? 8 : 1), limit.maximum);
            setNoteValue(target, _wheelValue, false); updateHover(target);
            repaintWheelTarget(target); surface->update(wheelBadge(surface).adjusted(-2, -2, 2, 2).toAlignedRect());
            }
      _wheelTimer.start(); wheel->accept(); return true;
      }
QRectF PerformanceEditor::wheelBadge(QWidget* surface) const
      {
      return QRectF(qBound(0.0, _wheelPoint.x() + 10, double(qMax(0, surface->width() - 88))),
            qBound(0.0, _wheelPoint.y() - 25, double(qMax(0, surface->height() - 24))), 84, 22);
      }
void PerformanceEditor::paintWheelBadge(QPainter& painter, QWidget* surface) const
      {
      if (_wheelIndex < 0 || _wheelIndex >= _notes.size() || _wheelSurface != surface) return;
      const auto badge = wheelBadge(surface);
      painter.save(); painter.fillRect(badge, _appearance.colors[PerformanceAppearance::Background]);
      painter.setPen(_appearance.colors[PerformanceAppearance::Preview]); painter.drawRect(badge);
      painter.drawText(badge, Qt::AlignCenter, QString::number(noteValue(_wheelIndex), 'f', 0) + (_axis->currentIndex() ? " %" : " MIDI")); painter.restore();
      }
void PerformanceEditor::repaintWheelTarget(int index)
      {
      if (index < 0 || index >= _notes.size()) return;
      buildGeometry();
      for (const auto& geometry : _noteGeometry) if (geometry.index == index) _noteCanvas->update(geometry.rect.adjusted(-3, -24, 3, 3).toAlignedRect());
      const int x = qRound(xForTick(_notes[index].tick, false)); _canvas->update(QRect(x - 9, 0, 19, _canvas->height()));
      if (_view && (_handles->isChecked() || _band->isChecked())) invalidateOverlay();
      }

}
