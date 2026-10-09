// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "mscore/scoreview.h"
#include "libmscore/note.h"
#include "libmscore/system.h"
#include "libmscore/measure.h"
#include <QPainter>
#include <QFontMetrics>
#include <QComboBox>
#include <QCheckBox>
#include <QToolButton>
#include <QToolTip>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHelpEvent>
#include <QApplication>
#include <cmath>
#include <algorithm>
namespace Ms {
void PerformanceEditor::syncOverlayTracking()
      {
      if (!_view) return;
      const bool needed = overlayAllowed() && (_handles->isChecked() || _band->isChecked() || _wheelButton->isChecked());
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
      if (_scoreRangePanning) {
            if (event->type() == QEvent::MouseMove) {
                  auto mouse = static_cast<QMouseEvent*>(event); const auto r = _scoreViewport.ranges[rangeKind()];
                  _scoreViewport.panRange(rangeKind(), (mouse->y() - _last.y()) * (r.maximum - r.minimum) / _scoreLane.height());
                  _last = mouse->pos(); invalidateOverlay(); return true;
                  }
            if (event->type() == QEvent::MouseButtonRelease) { _scoreRangePanning = false; qApp->removeEventFilter(this); _view->releaseMouse(); _view->unsetCursor(); return true; }
            }
      if (event->type() == QEvent::MouseButtonPress && _band->isChecked() && !_dirty && !_scoreLane.isEmpty()) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                  for (int i = 0; i < 6; ++i) if (_scoreControls[i].contains(mouse->pos())) { scoreRangeControl(i); return true; }
                  if (_scoreAxis.contains(mouse->pos()) && rangeKind() != 3) { finishWheel(); cancelGesture(); _scoreRangePanning = true; _last = mouse->pos(); qApp->installEventFilter(this); _view->setCursor(Qt::ClosedHandCursor); _view->grabMouse(); return true; }
                  }
            }
      if (event->type() == QEvent::Wheel && _band->isChecked() && !_dirty) {
            auto wheel = static_cast<QWheelEvent*>(event);
            if (_scoreAxis.contains(wheel->position())) {
                  finishWheel(); cancelGesture(); _scoreViewport.zoomRange(rangeKind(), wheel->angleDelta().y() > 0 ? 1.2 : 1 / 1.2, valueForY(wheel->position().y(), _scoreLane, true), false); invalidateOverlay(); return true;
                  }
            }
      if (event->type() == QEvent::Wheel) { if (wheelVelocity(static_cast<QWheelEvent*>(event), false, true)) return true; finishWheel(); }
      if (_dirty || _geometryDirty) {
            // Keep ownership while the queued snapshot/layout refresh erases the
            // old strip. An immediate undo/click must not reach a hidden staff.
            QPointF point; bool pointer = false;
            switch (event->type()) {
                  case QEvent::MouseMove: case QEvent::MouseButtonPress:
                  case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick:
                        point = static_cast<QMouseEvent*>(event)->pos(); pointer = true; break;
                  case QEvent::Wheel: point = static_cast<QWheelEvent*>(event)->position(); pointer = true; break;
                  case QEvent::ToolTip: point = static_cast<QHelpEvent*>(event)->pos(); pointer = true; break;
                  default: break;
                  }
            if (pointer && _scoreFrame.contains(point)) { scheduleRefresh(); return true; }
            return false;
            }
      if (!_handles->isChecked() && !_band->isChecked() && !_wheelButton->isChecked()) return false;
      if (event->type() == QEvent::Leave && !_dragging) { _overBand = false; updateHover(-1); invalidateOverlay(); }
      if (event->type() == QEvent::MouseMove && !_dragging) {
            auto mouse = static_cast<QMouseEvent*>(event);
            const bool before = _overBand; const QPointF old = _overlayPointer;
            _overBand = _band->isChecked() && _scoreLane.contains(mouse->pos()); _overlayPointer = mouse->pos();
            int hover = -1;
            if (_overBand && _parameter->currentIndex() == 0) { const auto candidates = hits(mouse->pos(), true, true); if (!candidates.isEmpty()) hover = candidates.front(); }
            else if (!_overBand && !_scoreFrame.contains(mouse->pos())) {
                  if (_handles->isChecked()) {
                        auto handles = _selectedIndices; if (_hover >= 0 && !handles.contains(_hover)) handles.append(_hover);
                        for (int i : handles) if (QLineF(mouse->pos(), _view->matrix().mapRect(_notes[i].bounds).topRight() + QPointF(12, -12)).length() <= 11) { hover = i; break; }
                        }
                  if (hover < 0) hover = _noteIndex.value(_view->elementNear(_view->toLogical(mouse->pos())), -1);
                  if (hover < 0 && _handles->isChecked() && _hover >= 0
                        && _view->matrix().mapRect(_notes[_hover].bounds).adjusted(-4, -25, 35, 6).contains(mouse->pos())) hover = _hover;
                  }
            if (hover >= 0 && !_notes[hover].enabled) hover = -1;
            updateHover(hover);
            if (_overBand || before) {
                  for (double x : {old.x(), _overlayPointer.x()}) _view->update(QRect(qRound(x) - 3, qRound(_scoreLane.top()) - 24, 7, qRound(_scoreLane.height()) + 30));
                  _view->setCursor(_overBand ? Qt::CrossCursor : Qt::ArrowCursor);
                  }
            if (_scoreFrame.contains(mouse->pos())) { if (!_overBand) _view->setCursor(Qt::ArrowCursor); return true; }
            }
      if (event->type() == QEvent::ToolTip) {
            auto help = static_cast<QHelpEvent*>(event);
            const QStringList controls {tr("缩小纵向显示，显示更大数值范围"), tr("放大纵向显示，精细调节当前范围"), tr("数值视窗上移"), tr("数值视窗下移"), tr("匹配当前谱行选音；没有选音时匹配当前谱行数据"), tr("恢复此参数的完整合法范围")};
            for (int i = 0; i < 6; ++i) if (_scoreControls[i].contains(help->pos())) { QToolTip::showText(help->globalPos(), controls[i], _view); return true; }
            if (_parameter->currentIndex() && _scoreLane.contains(help->pos())) { QToolTip::showText(help->globalPos(), parameterTooltip(help->pos(), true), _view); return true; }
            if (_noteTips && _hover >= 0) { QToolTip::showText(help->globalPos(), noteTooltip(_hover), _view); return true; }
            if (!_noteTips && _hover >= 0) { QToolTip::hideText(); return true; }
            if (_scoreAxis.contains(help->pos())) { QToolTip::showText(help->globalPos(), tr("滚轮缩放此谱行带数值范围；上下拖动刻度轴平移。与编辑器纵轴独立。"), _view); return true; }
            if (_scoreLane.contains(help->pos())) { QToolTip::showText(help->globalPos(), tr("单击端点选音并拖动；Alt+单击轮换重叠音；右键选择候选音。"), _view); return true; }
            if (_scoreFrame.contains(help->pos())) { QToolTip::hideText(); return true; }
            }
      if (event->type() == QEvent::MouseButtonPress && _band->isChecked() && _parameter->currentIndex() == 0 && _scoreLane.contains(static_cast<QMouseEvent*>(event)->pos())) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton) return false;
            const auto candidates = hits(mouse->pos(), true, true);
            if (candidates.isEmpty()) { if (_tool->currentIndex() == 0) { seek(tickForX(mouse->x(), true)); return true; } return false; }
            if (mouse->modifiers() & Qt::AltModifier) { cycleHit(candidates); return true; }
            const int target = candidates.front();
            const bool cycle = candidates.size() > 1 && _selectedIndices.size() == 1 && _notes[target].selected
                  && std::abs(yForValue(noteValue(candidates[1]), _scoreLane, true) - yForValue(noteValue(target), _scoreLane, true)) < 3 && mouse->modifiers() == Qt::NoModifier;
            selectIndices({target}, mouse->modifiers(), !(mouse->modifiers() & Qt::ControlModifier)); updateHover(target); audition(target);
            if (mouse->modifiers() & Qt::ControlModifier) return true;
            beginGesture(mouse->pos(), _scoreLane, true, target); _cycleOnClick = cycle; _pressedCandidates = candidates; return _dragging;
            }
      // The fixed strip owns its complete frame. Never let its title, axis or
      // margins select/edit the score hidden beneath it. Drag release and lane
      // context menus still belong to the normal editor transaction handler.
      if (!_scoreFrame.isEmpty()) {
            if (event->type() == QEvent::MouseButtonDblClick) {
                  const auto point = static_cast<QMouseEvent*>(event)->pos();
                  if (_scoreFrame.contains(point) && !_scoreLane.contains(point)) return true;
                  }
            if (event->type() == QEvent::MouseButtonPress) {
                  auto mouse = static_cast<QMouseEvent*>(event);
                  if (_scoreFrame.contains(mouse->pos()) && !_scoreLane.contains(mouse->pos())) return true;
                  }
            if (event->type() == QEvent::MouseButtonRelease && !_dragging && _scoreFrame.contains(static_cast<QMouseEvent*>(event)->pos())) return true;
            if (event->type() == QEvent::Wheel) {
                  auto wheel = static_cast<QWheelEvent*>(event);
                  if (_scoreFrame.contains(wheel->position()) && wheel->modifiers() == Qt::NoModifier) return true;
                  }
            }
      return false;
      }
void PerformanceEditor::paintOverlay(QPainter& painter)
      {
      if (!overlayAllowed()) {
            // Retain old damage until the queued refresh can erase it; never lose
            // ownership of pixels during a layout notification in playback.
            _scoreLane = _scoreAxis = _scoreFrame = QRectF(); _scoreControls.fill(QRectF()); return;
            }
      if (_dirty || _geometryDirty) {
            _scoreLane = _scoreAxis = QRectF(); _scoreControls.fill(QRectF()); return;
            }
      QRegion damage;
      painter.save(); painter.resetTransform(); painter.setRenderHint(QPainter::Antialiasing);
      QVector<int> handles = _selectedIndices;
      if (_hover >= 0 && !handles.contains(_hover)) handles.append(_hover);
      if (_handles->isChecked() || _band->isChecked()) for (int i : handles) {
            if (i < 0 || i >= _notes.size()) continue;
            const auto& note = _notes[i]; const QRectF bounds = _view->matrix().mapRect(note.bounds);
            if (!_view->rect().intersects(bounds.toRect())) continue;
            const QColor color = _pending.contains(note.note) ? _appearance.colors[PerformanceAppearance::Preview] : scoreNoteColor(i);
            if (i == _hover || (_band->isChecked() && note.selected)) {
                  painter.setPen(QPen(color, 2, i == _hover ? Qt::DashLine : Qt::SolidLine)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(bounds.adjusted(-3, -3, 3, 3), 2, 2);
                  damage |= bounds.adjusted(-5, -5, 5, 5).toAlignedRect();
                  }
            if (!_handles->isChecked() || _parameter->currentIndex() != 0) continue;
            const QPointF handle = bounds.topRight() + QPointF(12, -12);
            painter.setPen(QPen(color, 2)); painter.setBrush(note.audible ? QBrush(Qt::white) : Qt::NoBrush); painter.drawEllipse(handle, 5, 5);
            if (_view->matrix().m11() > 0.5) painter.drawText(handle + QPointF(8, -4), QString::number(noteValue(i)) + (_axis->currentIndex() ? "%" : ""));
            damage |= QRectF(handle - QPointF(8, 20), QSizeF(95, 30)).toAlignedRect();
            }
      _scoreLane = _scoreAxis = _scoreFrame = QRectF(); _scoreControls.fill(QRectF());
      if (_band->isChecked() && _system && !_systemSegments.isEmpty()) {
            const auto first = _system->firstMeasure(), last = _system->lastMeasure();
            if (!first || !last) { painter.restore(); return; }
            const double left = qMax(56.0, _view->matrix().map(first->canvasPos()).x());
            const double right = qMin(double(_view->width() - 12), _view->matrix().map(last->canvasPos() + QPointF(last->width(), 0)).x());
            if (right <= left) { painter.restore(); return; }
            _scoreLane = QRectF(left, _view->height() - _bandHeight - 26, right - left, _bandHeight);
            _scoreAxis = QRectF(left - 48, _scoreLane.top(), 46, _scoreLane.height());
            const QRectF frame = _scoreLane.adjusted(-50, -26, 4, 5);
            _scoreFrame = frame;
            painter.fillRect(frame, QColor("#f1f2ef"));
            painter.setPen(QPen(QColor("#acb0aa"), 1)); painter.setBrush(Qt::NoBrush); painter.drawRect(frame);
            painter.setPen(QColor("#4b514b"));
            int focus = _hover >= 0 && _notes[_hover].system == _system ? _hover : -1;
            if (focus < 0) for (int i : _selectedIndices) if (_notes[i].system == _system) { focus = i; break; }
            QString label = tr("当前谱行 · %1").arg(_parameter->currentText());
            if (focus >= 0) {
                  const auto& note = _notes[focus]; label += QString(" · %1").arg(note.name);
                  if (_parameter->currentIndex() == 0) label += QString(" · %1%2").arg(noteValue(focus)).arg(_axis->currentIndex() ? "%" : " MIDI");
                  else if (_parameter->currentIndex() == 1) {
                        double bpm = _score->tempo(Fraction::fromTicks(note.tick)) * 60; auto draft = _tempoDraft.upperBound(note.tick);
                        if (draft != _tempoDraft.cbegin()) {
                              --draft; const auto original = std::upper_bound(_tempos.cbegin(), _tempos.cend(), note.tick, [](int tick, const TempoInfo& tempo) { return tick < tempo.tick; });
                              if (original == _tempos.cbegin() || draft.key() >= (original - 1)->tick) bpm = draft.value();
                              }
                        label += QString(" · %1 BPM").arg(bpm, 0, 'f', 1);
                        }
                  label += tr(" · %1 · 谱表 %2 / 声部 %3").arg(note.position).arg(note.track / VOICES + 1).arg(note.track % VOICES + 1);
                  }
            const QStringList controls {QString::fromUtf8("−"), "+", QString::fromUtf8("↑"), QString::fromUtf8("↓"), tr("匹配"), tr("全")};
            const int controlWidth = frame.width() < 400 ? 22 : 30;
            const double controlsLeft = frame.right() - 6 * controlWidth - 3;
            const int titleWidth = qMax(0, int(controlsLeft - left - 8));
            painter.drawText(QRectF(left + 4, frame.top(), titleWidth, 22), Qt::AlignLeft | Qt::AlignVCenter, painter.fontMetrics().elidedText(label, Qt::ElideRight, titleWidth));
            for (int i = 0; i < 6; ++i) {
                  _scoreControls[i] = QRectF(controlsLeft + i * controlWidth, frame.top() + 2, controlWidth - 2, 20);
                  painter.setPen(QColor("#91978e")); painter.setBrush(QColor("#e5e8e2")); painter.drawRoundedRect(_scoreControls[i], 2, 2);
                  painter.setPen(QColor("#454e43")); painter.drawText(_scoreControls[i], Qt::AlignCenter, i == 4 && controlWidth == 22 ? tr("适") : controls[i]);
                  }
            const auto range = _scoreViewport.ranges[rangeKind()];
            painter.drawText(QRectF(frame.left() + 2, frame.top() + 4, 45, 16), Qt::AlignRight, rangeKind() == 2 ? "BPM" : (rangeKind() == 1 ? "%" : (rangeKind() == 3 ? "CC64" : "MIDI")));
            const int divisions = rangeKind() == 3 ? 1 : qMax(2, int(_bandHeight / 26));
            for (int i = 0; i <= divisions; ++i) {
                  const double y = _scoreLane.top() + i * _scoreLane.height() / divisions;
                  const double value = range.maximum - i * (range.maximum - range.minimum) / divisions;
                  painter.setPen(QColor("#d6d9d3")); painter.drawLine(QPointF(left, y), QPointF(right, y));
                  painter.setPen(QColor("#62685f")); painter.drawText(QRectF(left - 47, qBound(_scoreLane.top(), y - 7, _scoreLane.bottom() - 14), 43, 14), Qt::AlignRight | Qt::AlignVCenter,
                        rangeKind() == 3 ? (i ? tr("关 0") : tr("开 127")) : QString::number(value, 'f', range.maximum - range.minimum < 10 ? 1 : 0));
                  }
            if (range.minimum < 0 && range.maximum > 0) { painter.setPen(QColor("#929b8e")); const double y = yForValue(0, _scoreLane, true); painter.drawLine(QPointF(left, y), QPointF(right, y)); }
            paintLane(painter, _scoreLane, true);
            painter.save(); painter.setClipRect(_scoreLane.adjusted(-3, -2, 3, 2));
            if (_parameter->currentIndex() == 0) for (int i : handles) if (_notes[i].system == _system && _notes[i].enabled) {
                  painter.setPen(QPen(i == _hover ? _appearance.colors[PerformanceAppearance::Preview] : scoreNoteColor(i), 2)); painter.setBrush(Qt::NoBrush);
                  painter.drawEllipse(QPointF(xForTick(_notes[i].tick, true), yForValue(noteValue(i), _scoreLane, true)), 5, 5);
                  }
            if (_playTick >= _systemSegments.front().tick && _playTick <= _systemSegments.back().tick) {
                  const double x = xForTick(_playTick, true); painter.setPen(QPen(QColor("#4b7658"), 2)); painter.drawLine(QPointF(x, _scoreLane.top()), QPointF(x, _scoreLane.bottom()));
                  }
            if (_overBand) { painter.setPen(QPen(QColor("#616961"), 1, Qt::DotLine)); painter.drawLine(QPointF(_overlayPointer.x(), _scoreLane.top()), QPointF(_overlayPointer.x(), _scoreLane.bottom())); }
            painter.restore(); damage |= frame.toAlignedRect();
            }
      paintWheelBadge(painter, _view);
      if (_wheelIndex >= 0 && _wheelSurface == _view) damage |= wheelBadge(_view).adjusted(-2, -2, 2, 2).toAlignedRect();
      _overlayDamage = damage; _overlayMatrix = _view->matrix(); painter.restore();
      }
}
