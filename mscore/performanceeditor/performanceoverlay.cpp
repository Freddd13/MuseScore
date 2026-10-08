// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "mscore/scoreview.h"
#include "libmscore/note.h"
#include <QPainter>
#include <QComboBox>
#include <QCheckBox>
#include <QToolTip>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHelpEvent>
#include <cmath>
#include <algorithm>
namespace Ms {
void PerformanceEditor::syncOverlayTracking()
      {
      if (!_view) return;
      const bool needed = overlayAllowed() && (_handles->isChecked() || _band->isChecked());
      if (needed) {
            if (!_trackingOwned) { _trackingBefore = _view->hasMouseTracking(); _trackingOwned = true; }
            _view->setMouseTracking(true);
            }
      else if (_trackingOwned) { _view->setMouseTracking(_trackingBefore || _view->noteEntryMode() || _view->fotoMode()); _trackingOwned = false; }
      }
void PerformanceEditor::invalidateOverlay()
      { if (_view && !_overlayDamage.isEmpty()) _view->update(_overlayDamage); }
void PerformanceEditor::overlayViewChanged()
      {
      if (!_view) return;
      const auto matrix = _view->matrix();
      if (!_overlayDamage.isEmpty()) {
            // ScoreView scrolls its backing store. Invalidate both the fixed strip and
            // the pixels that QWidget::scroll copied, including accumulated wheel events.
            if (matrix.m11() != _overlayMatrix.m11() || matrix.m22() != _overlayMatrix.m22()) _view->update();
            else {
                  _overlayDamage |= _overlayDamage.translated(qRound(matrix.dx() - _overlayMatrix.dx()), qRound(matrix.dy() - _overlayMatrix.dy()));
                  _overlayDamage &= _view->rect(); _view->update(_overlayDamage);
                  }
            }
      _overlayMatrix = matrix;
      }
bool PerformanceEditor::overlayEvent(QEvent* event)
      {
      if (event->type() == QEvent::Wheel) { if (wheelVelocity(static_cast<QWheelEvent*>(event), false, true)) return true; finishWheel(); }
      if (_dirty || (!_handles->isChecked() && !_band->isChecked())) return false;
      if (event->type() == QEvent::Leave && !_dragging) { _overBand = false; updateHover(-1); invalidateOverlay(); }
      if (event->type() == QEvent::MouseMove && !_dragging) {
            auto mouse = static_cast<QMouseEvent*>(event);
            const bool before = _overBand; const QPointF old = _overlayPointer;
            _overBand = _band->isChecked() && _scoreLane.contains(mouse->pos()); _overlayPointer = mouse->pos();
            int hover = -1;
            if (_overBand && _parameter->currentIndex() == 0) { const auto candidates = hits(mouse->pos(), true, true); if (!candidates.isEmpty()) hover = candidates.front(); }
            else if (!_overBand) hover = _noteIndex.value(_view->elementNear(_view->toLogical(mouse->pos())), -1);
            if (hover >= 0 && !_notes[hover].enabled) hover = -1;
            updateHover(hover);
            if (_overBand || before) {
                  for (double x : {old.x(), _overlayPointer.x()}) _view->update(QRect(qRound(x) - 3, qRound(_scoreLane.top()) - 24, 7, qRound(_scoreLane.height()) + 30));
                  _view->setCursor(_overBand ? Qt::CrossCursor : Qt::ArrowCursor);
                  }
            if (_overBand) return true;
            }
      if (event->type() == QEvent::ToolTip) {
            auto help = static_cast<QHelpEvent*>(event);
            if (_hover >= 0) { QToolTip::showText(help->globalPos(), noteTooltip(_hover), _view); return true; }
            if (_scoreLane.contains(help->pos())) { QToolTip::showText(help->globalPos(), tr("单击端点选音并拖动；Alt+单击轮换重叠音；右键选择候选音。"), _view); return true; }
            }
      if (event->type() == QEvent::MouseButtonPress && _parameter->currentIndex() == 0 && _scoreLane.contains(static_cast<QMouseEvent*>(event)->pos())) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton) return false;
            const auto candidates = hits(mouse->pos(), true, true);
            if (candidates.isEmpty()) return false;
            if (mouse->modifiers() & Qt::AltModifier) { cycleHit(candidates); return true; }
            const int target = candidates.front();
            const bool cycle = candidates.size() > 1 && _selectedIndices.size() == 1 && _notes[target].selected
                  && std::abs(yForValue(noteValue(candidates[1]), _scoreLane) - yForValue(noteValue(target), _scoreLane)) < 3 && mouse->modifiers() == Qt::NoModifier;
            selectIndices({target}, mouse->modifiers(), !(mouse->modifiers() & Qt::ControlModifier)); updateHover(target); audition(target);
            if (mouse->modifiers() & Qt::ControlModifier) return true;
            beginGesture(mouse->pos(), _scoreLane, true, target); _cycleOnClick = cycle; _pressedCandidates = candidates; return _dragging;
            }
      return false;
      }
void PerformanceEditor::paintOverlay(QPainter& painter)
      {
      if (!overlayAllowed() || _dirty) { _scoreLane = QRectF(); _overlayDamage = QRegion(); return; }
      QRegion damage;
      painter.save(); painter.resetTransform(); painter.setRenderHint(QPainter::Antialiasing);
      QVector<int> handles = _selectedIndices;
      if (_hover >= 0 && !handles.contains(_hover)) handles.append(_hover);
      if (_handles->isChecked() || _band->isChecked()) for (int i : handles) {
            if (i < 0 || i >= _notes.size()) continue;
            const auto& note = _notes[i]; const QRectF bounds = _view->matrix().mapRect(note.bounds);
            if (!_view->rect().intersects(bounds.toRect())) continue;
            const QColor color = _pending.contains(note.note) ? _appearance.colors[PerformanceAppearance::Preview] : QColor("#52724b");
            if (i == _hover) {
                  painter.setPen(QPen(color, 2)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(bounds.adjusted(-3, -3, 3, 3), 2, 2);
                  damage |= bounds.adjusted(-5, -5, 5, 5).toAlignedRect();
                  }
            if (!_handles->isChecked() || _parameter->currentIndex() != 0) continue;
            const QPointF handle = bounds.topRight() + QPointF(12, -12);
            painter.setPen(QPen(color, 2)); painter.setBrush(note.audible ? QBrush(Qt::white) : Qt::NoBrush); painter.drawEllipse(handle, 5, 5);
            if (_view->matrix().m11() > 0.5) painter.drawText(handle + QPointF(8, -4), QString::number(noteValue(i)) + (_axis->currentIndex() ? "%" : ""));
            damage |= QRectF(handle - QPointF(8, 20), QSizeF(95, 30)).toAlignedRect();
            }
      _scoreLane = QRectF();
      if (_band->isChecked() && _system && !_systemSegments.isEmpty()) {
            const double left = 48, right = qMax(108, _view->width() - 12);
            _scoreLane = QRectF(left, _view->height() - 114, right - left, 88);
            const QRectF frame = _scoreLane.adjusted(-4, -24, 4, 4);
            painter.fillRect(frame, _appearance.colors[PerformanceAppearance::Background]);
            painter.setPen(_appearance.colors[PerformanceAppearance::Text]);
            const int focus = _hover >= 0 ? _hover : (_selectedIndices.isEmpty() ? -1 : _selectedIndices.front());
            QString label = tr("当前谱行 · %1").arg(_parameter->currentText());
            if (focus >= 0) {
                  const auto& note = _notes[focus]; label += tr(" · %1 · 谱表 %2 / 声部 %3 · %4").arg(note.name).arg(note.track / VOICES + 1).arg(note.track % VOICES + 1).arg(note.position);
                  if (_parameter->currentIndex() == 0) label += QString(" · %1%2").arg(noteValue(focus)).arg(_axis->currentIndex() ? "%" : " MIDI");
                  else if (_parameter->currentIndex() == 1) {
                        double bpm = _score->tempo(Fraction::fromTicks(note.tick)) * 60; auto draft = _tempoDraft.upperBound(note.tick);
                        if (draft != _tempoDraft.cbegin()) {
                              --draft; const auto original = std::upper_bound(_tempos.cbegin(), _tempos.cend(), note.tick, [](int tick, const TempoInfo& tempo) { return tick < tempo.tick; });
                              if (original == _tempos.cbegin() || draft.key() >= (original - 1)->tick) bpm = draft.value();
                              }
                        label += QString(" · %1 BPM").arg(bpm, 0, 'f', 1);
                        }
                  }
            painter.drawText(QRectF(frame.left() + 4, frame.top(), frame.width() - 8, 22), Qt::AlignLeft | Qt::AlignVCenter, label);
            paintLane(painter, _scoreLane, true);
            painter.save(); painter.setClipRect(_scoreLane.adjusted(-3, -2, 3, 2));
            if (_parameter->currentIndex() == 0) for (int i : handles) if (_notes[i].system == _system && _notes[i].enabled) {
                  painter.setPen(QPen(i == _hover ? _appearance.colors[PerformanceAppearance::Preview] : _appearance.colors[PerformanceAppearance::Selection], 2)); painter.setBrush(Qt::NoBrush);
                  painter.drawEllipse(QPointF(xForTick(_notes[i].tick, true), yForValue(noteValue(i), _scoreLane)), 5, 5);
                  }
            if (_playTick >= _systemSegments.front().tick && _playTick <= _systemSegments.back().tick) {
                  const double x = xForTick(_playTick, true); painter.setPen(QPen(_appearance.colors[PerformanceAppearance::Playhead], 2)); painter.drawLine(QPointF(x, _scoreLane.top()), QPointF(x, _scoreLane.bottom()));
                  }
            if (_overBand) { painter.setPen(QPen(_appearance.colors[PerformanceAppearance::Text], 1, Qt::DotLine)); painter.drawLine(QPointF(_overlayPointer.x(), _scoreLane.top()), QPointF(_overlayPointer.x(), _scoreLane.bottom())); }
            painter.restore(); damage |= frame.toAlignedRect();
            }
      paintWheelBadge(painter, _view);
      if (_wheelIndex >= 0 && _wheelSurface == _view) damage |= wheelBadge(_view).adjusted(-2, -2, 2, 2).toAlignedRect();
      _overlayDamage = damage; _overlayMatrix = _view->matrix(); painter.restore();
      }
}
