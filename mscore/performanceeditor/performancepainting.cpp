// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "libmscore/select.h"
#include "libmscore/pedal.h"
#include "libmscore/staff.h"
#include <QPainter>
#include <QComboBox>
#include <QScrollBar>
#include <cmath>
#include <algorithm>
namespace Ms {
namespace {
QString pitchName(int pitch)
      {
      static const char* names[] = {"C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"};
      return QString::fromUtf8(names[qBound(0, pitch, 127) % 12]) + QString::number(pitch / 12 - 1);
      }
}
int PerformanceEditor::rangeKind() const
      { return _parameter->currentIndex() == 0 ? _axis->currentIndex() : _parameter->currentIndex() + 1; }
void PerformanceEditor::updateSurfaces()
      { if (_canvas) _canvas->update(); if (_noteCanvas) _noteCanvas->update(); if (_ruler) _ruler->update(); }
void PerformanceEditor::invalidateVisual()
      { ++_visualRevision; syncValueScroll(); updateSurfaces(); }
void PerformanceEditor::surfaceResized(PerformanceSurface surface)
      {
      if (!_scroll || !_noteCanvas) return;
      if (_valueScroll && _canvas) _valueScroll->setGeometry(_canvas->width() - PerformanceViewport::rightMargin, 0, PerformanceViewport::rightMargin, _canvas->height());
      if (surface == PerformanceSurface::Notes && _pitchScroll) {
            const double rows = _noteCanvas->height() / _viewport.rowHeight;
            _viewport.topPitch = qBound(qMin(127.0, rows - 1), _viewport.topPitch, 127.0);
            QSignalBlocker block(_pitchScroll);
            _pitchScroll->setGeometry(_noteCanvas->width() - PerformanceViewport::rightMargin, 0, PerformanceViewport::rightMargin, _noteCanvas->height());
            _pitchScroll->setRange(0, qMax(0, qRound((128 - rows) * 100)));
            _pitchScroll->setPageStep(qRound(rows * 100)); _pitchScroll->setSingleStep(100);
            _pitchScroll->setValue(qRound((127 - _viewport.topPitch) * 100));
            }
      const int span = qMax(1, qRound(qMax(10, _canvas->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin) * DIVISION / _viewport.pixelsPerQuarter));
      _scroll->setPageStep(span); _scroll->setRange(0, qMax(0, _endTick - span));
      invalidateVisual();
      }
void PerformanceEditor::buildGeometry()
      {
      if (_geometryRevision == _visualRevision) return;
      _geometryRevision = _visualRevision; _noteGeometry.clear();
      if (!_noteCanvas || _notes.isEmpty()) return;
      const int high = qMin(127, int(std::ceil(_viewport.topPitch)));
      const int low = qMax(0, int(std::floor(_viewport.topPitch - _noteCanvas->height() / _viewport.rowHeight)));
      const int from = tickForX(PerformanceViewport::gutter, false), until = tickForX(_noteCanvas->width() - PerformanceViewport::rightMargin, false);
      for (int index : _intervals.query(from, until, low, high)) {
            const auto& note = _notes[index];
            if (!note.inScope || (!note.enabled && !_ghostVoices)) continue;
            const double x = xForTick(note.tick, false), end = xForTick(qMax(note.tick + 1, note.end), false);
            _noteGeometry.append({index, QRectF(x, (_viewport.topPitch - note.pitch) * _viewport.rowHeight + 1, qMax(2.0, end - x - 1), qMax(1.0, _viewport.rowHeight - 2))});
            }
      }
void PerformanceEditor::paintBackground(QPainter& painter, PerformanceSurface surface)
      {
      const auto& c = _appearance.colors;
      QWidget* widget = surface == PerformanceSurface::Notes ? static_cast<QWidget*>(_noteCanvas) : (surface == PerformanceSurface::Ruler ? static_cast<QWidget*>(_ruler) : _canvas);
      painter.fillRect(widget->rect(), c[PerformanceAppearance::Background]);
      if (surface == PerformanceSurface::Notes) {
            const int low = qMax(0, int(_viewport.topPitch - widget->height() / _viewport.rowHeight) - 2);
            const int high = qMin(127, int(std::ceil(_viewport.topPitch)) + 2);
            for (int pitch = low; pitch <= high; ++pitch) {
                  const double y = (_viewport.topPitch - pitch) * _viewport.rowHeight;
                  painter.fillRect(QRectF(PerformanceViewport::gutter, y, widget->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin, _viewport.rowHeight), c[PerformanceKeyboard::isBlack(pitch) ? PerformanceAppearance::AccidentalRow : PerformanceAppearance::NaturalRow]);
                  painter.setPen(c[PerformanceAppearance::Grid]); painter.drawLine(QPointF(PerformanceViewport::gutter, y), QPointF(widget->width() - PerformanceViewport::rightMargin, y));
                  }
            painter.save(); painter.setClipRect(QRectF(0, 0, PerformanceViewport::gutter, widget->height()));
            for (bool black : {false, true}) for (int pitch = low; pitch <= high; ++pitch) {
                  if (PerformanceKeyboard::isBlack(pitch) != black) continue;
                  painter.setPen(QPen(QColor(black ? "#161616" : "#777777"), 1)); painter.setBrush(QColor(black ? "#161616" : "#f1f1f1"));
                  painter.drawPolygon(PerformanceKeyboard::shape(pitch, _viewport.topPitch, _viewport.rowHeight, PerformanceViewport::gutter));
                  if (_keyboardNames && (_viewport.rowHeight >= 10 || pitch % 12 == 0)) {
                        painter.setPen(black ? QColor("#ededed") : QColor("#292929"));
                        QFont font = painter.font(); font.setPixelSize(qBound(7, int(_viewport.rowHeight * .82), 12)); painter.setFont(font);
                        const double y = (_viewport.topPitch - pitch) * _viewport.rowHeight;
                        painter.drawText(QRectF(black ? 2 : PerformanceViewport::gutter * .58 + 2, y,
                              PerformanceViewport::gutter * (black ? .58 : .42) - 4, _viewport.rowHeight), Qt::AlignVCenter | Qt::AlignRight, pitchName(pitch));
                        }
                  }
            painter.restore();
            }
      if (surface == PerformanceSurface::Parameter) {
            const auto range = _viewport.ranges[rangeKind()]; const QRectF lane = laneRect();
            const int divisions = rangeKind() == 3 ? 1 : 4;
            for (int i = 0; i <= divisions; ++i) {
                  const double value = range.minimum + (range.maximum - range.minimum) * i / divisions;
                  const double y = yForValue(value, lane);
                  painter.setPen(c[PerformanceAppearance::Grid]); painter.drawLine(QPointF(lane.left(), y), QPointF(lane.right(), y));
                  painter.setPen(c[PerformanceAppearance::Text]); painter.drawText(QRectF(1, qBound(0.0, y - 8, widget->height() - 16.0), PerformanceViewport::gutter - 8, 16), Qt::AlignRight | Qt::AlignVCenter, rangeKind() == 3 ? (i == 0 ? tr("关 0") : tr("开 127")) : QString::number(value, 'f', 0));
                  }
            if (range.minimum <= 0 && range.maximum >= 0) {
                  painter.setPen(QPen(c[PerformanceAppearance::Text], 1, Qt::DashLine)); painter.drawLine(QPointF(lane.left(), yForValue(0, lane)), QPointF(lane.right(), yForValue(0, lane)));
                  }
            }
      if (!_score) return;
      painter.save(); painter.setClipRect(QRectF(PerformanceViewport::gutter, 0, widget->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin, widget->height()));
      const int from = tickForX(PerformanceViewport::gutter, false), until = tickForX(widget->width(), false);
      double lastLabel = -1000;
      for (const auto& measure : _measures) {
            if (measure.until < from) continue;
            if (measure.from > until) break;
            const int subdivision = measure.beat * _viewport.pixelsPerQuarter / DIVISION >= 90 ? 4 : (measure.beat * _viewport.pixelsPerQuarter / DIVISION >= 45 ? 2 : 1);
            const int step = qMax(1, measure.beat / subdivision);
            for (int tick = measure.from; tick < measure.until; tick += step) {
                  if (tick < from) continue;
                  if (tick > until) break;
                  const double x = xForTick(tick, false); const bool bar = tick == measure.from;
                  painter.setPen(QPen(c[PerformanceAppearance::Grid], bar ? 1.5 : 0.7, (tick - measure.from) % measure.beat ? Qt::DotLine : Qt::SolidLine));
                  painter.drawLine(QPointF(x, surface == PerformanceSurface::Parameter ? laneRect().top() : 0), QPointF(x, surface == PerformanceSurface::Parameter ? laneRect().bottom() : widget->height()));
                  if (surface == PerformanceSurface::Ruler && x - lastLabel >= 54) {
                        const int beat = (tick - measure.from) / measure.beat;
                        QString label = QString("%1.%2").arg(measure.bar + 1).arg(beat + 1);
                        if ((tick - measure.from) % measure.beat) label += QString(".%1").arg(((tick - measure.from) % measure.beat) / step + 1);
                        painter.setPen(c[PerformanceAppearance::Text]); painter.drawText(QPointF(x + 3, 19), label); lastLabel = x;
                        }
                  }
            }
      painter.restore();
      }
void PerformanceEditor::paintLane(QPainter& painter, const QRectF& rect, bool onScore)
      {
      painter.save(); painter.setClipRect(rect.adjusted(-2, -3, 2, 0), Qt::IntersectClip);
      const int parameter = _parameter->currentIndex();
      if (parameter == 0) {
            const auto active = painter.clipBoundingRect().intersected(rect.adjusted(-8, -8, 8, 8));
            int from = tickForX(active.left() - 8, onScore), until = tickForX(active.right() + 8, onScore);
            auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
            const bool dense = !onScore && _viewport.pixelsPerQuarter < 8;
            // Identical native-time/value marks have identical pixels. Draw each style
            // once; keep every source note in the hit/selection/transaction indexes.
            QSet<QPair<QPair<int, int>, quint64>> marks;
            QVector<int> emphasis;
            const int stemWidth = _appearance.velocityWidth, columnWidth = qMax(2, stemWidth);
            QVector<QPair<double, double>> density; QVector<QColor> densityColors;
            const double baseline = qBound(rect.top(), yForValue(rangeKind() == 1 ? 0 : 1, rect, onScore), rect.bottom());
            if (dense) { density.fill({rect.bottom(), rect.bottom()}, int(rect.width() / columnWidth) + 1); densityColors.fill(_appearance.colors[PerformanceAppearance::Text], density.size()); }
            for (auto it = first; it != _notes.cend() && it->tick <= until; ++it) {
                  if (onScore && it->system != _system) continue;
                  const int index = int(it - _notes.cbegin());
                  if (!it->inScope || (!it->enabled && !_ghostVoices) || !hasNoteValue(*it)) continue;
                  const double x = xForTick(it->tick, onScore);
                  const double y = yForValue(noteValue(index), rect, onScore);
                  if (dense && !it->selected && index != _hover && !_playingNotes.contains(index) && !_pending.contains(it->note)) {
                        const int column = int((x - rect.left()) / columnWidth);
                        if (column >= 0 && column < density.size()) {
                              if (y < density[column].first) densityColors[column] = _appearance.noteColor(NoteVelocity::effective(it->base, it->type, it->raw), it->track);
                              density[column].first = qMin(density[column].first, y);
                              density[column].second = qMax(density[column].second == rect.bottom() ? y : density[column].second, y);
                              }
                        continue;
                        }
                  const auto edit = _pending.value(it->note, {it->type, it->raw});
                  QColor color = it->base < 0 && edit.type == Note::ValueType::OFFSET_VAL && _appearance.mode == 0 ? _appearance.colors[PerformanceAppearance::Grid].lighter(130) : _appearance.noteColor(NoteVelocity::effective(it->base, edit.type, edit.raw), it->track);
                  if (onScore) color = scoreNoteColor(index);
                  color.setAlpha(!it->enabled ? 40 : (it->audible ? 230 : 70));
                  const auto mark = qMakePair(qMakePair(qRound(x * 256), qRound(y * 256)), (quint64(color.rgba()) << 3) | (index == _hover ? 4 : 0) | (it->selected ? 2 : 0) | (it->audible ? 1 : 0));
                  if (marks.contains(mark)) continue;
                  if (it->selected || index == _hover) emphasis.append(index);
                  marks.insert(mark); painter.setPen(QPen(color, stemWidth));
                  painter.drawLine(QPointF(x, baseline), QPointF(x, y));
                  painter.setBrush(it->audible ? QBrush(color) : Qt::NoBrush); painter.drawEllipse(QPointF(x, y), onScore ? 2.5 : 2.0, onScore ? 2.5 : 2.0);
                  if (!_axis->currentIndex() && !it->generatedVelocities.isEmpty() && (it->selected || index==_hover)) {
                        const auto range=generatedVelocityRange(*it,edit.type,edit.raw);
                        const double low=yForValue(range.first,rect,onScore),high=yForValue(range.second,rect,onScore);
                        painter.setPen(QPen(color.lighter(140),1,Qt::DashLine)); painter.drawLine(QPointF(x+4,low),QPointF(x+4,high));
                        painter.drawLine(QPointF(x+2,low),QPointF(x+6,low)); painter.drawLine(QPointF(x+2,high),QPointF(x+6,high));
                        if(!onScore) painter.drawText(QPointF(x+8,high+12),QString("%1–%2").arg(range.first).arg(range.second));
                        }
                  }
            if (dense) {
                  painter.setPen(QPen(_appearance.colors[PerformanceAppearance::Text], 1));
                  for (int i = 0; i < density.size(); ++i) if (density[i].first < rect.bottom()) {
                        const double x = rect.left() + i * columnWidth;
                        painter.setPen(QPen(densityColors[i], stemWidth));
                        painter.drawLine(QPointF(x, baseline), QPointF(x, density[i].first));
                        painter.drawLine(QPointF(x - 1, density[i].second), QPointF(x + 1, density[i].second));
                        }
                  }
            // Draw selected/hovered targets last so overlapping voices and dense
            // overview bins cannot cover the native selection. No time offset.
            for (int index : emphasis) {
                  const auto& note = _notes[index];
                  const double x = xForTick(note.tick, onScore), y = yForValue(noteValue(index), rect, onScore);
                  const auto edit = _pending.value(note.note, {note.type, note.raw});
                  QColor color = onScore ? scoreNoteColor(index) : _appearance.noteColor(NoteVelocity::effective(note.base, edit.type, edit.raw), note.track);
                  if (!note.enabled) color.setAlpha(40);
                  const QColor border = index == _hover ? _appearance.colors[PerformanceAppearance::Preview] : (onScore ? QColor("#303a43") : _appearance.colors[PerformanceAppearance::Selection]);
                  painter.setPen(QPen(border, stemWidth + 2)); painter.drawLine(QPointF(x, baseline), QPointF(x, y));
                  painter.setPen(QPen(color, stemWidth)); painter.drawLine(QPointF(x, baseline), QPointF(x, y));
                  painter.setPen(QPen(border, 2)); painter.setBrush(color); const double radius = qMax(3.0, stemWidth / 2.0 + 1);
                  painter.drawEllipse(QPointF(x, y), radius, radius);
                  }
            }
      else if (parameter == 1) {
            auto bpmAt = [this](const SegmentInfo& segment) {
                  const auto original = std::upper_bound(_tempos.cbegin(), _tempos.cend(), segment.tick, [](int t, const TempoInfo& info) { return t < info.tick; });
                  const int originalTick = original == _tempos.cbegin() ? -1 : (original - 1)->tick;
                  auto draft = _tempoDraft.upperBound(segment.tick);
                  if (draft != _tempoDraft.cbegin()) {
                        --draft;
                        if (draft.key() >= originalTick) return draft.value();
                        }
                  return segment.bpm;
                  };
            const int from = tickForX(rect.left(), onScore), until = tickForX(rect.right(), onScore);
            auto first = std::lower_bound(_segments.cbegin(), _segments.cend(), from, [](const SegmentInfo& s, int t) { return s.tick < t; });
            if (first != _segments.cbegin()) --first;
            if (!paintTempoCurves(painter, rect, onScore)) {
            const SegmentInfo* previous = nullptr;
            for (auto it = first; it != _segments.cend(); ++it) {
                  const auto& segment = *it;
                  if (onScore && segment.system != _system) continue;
                  const double bpm = bpmAt(segment);
                  if (previous) {
                        const double x0 = xForTick(previous->tick, onScore), x1 = xForTick(segment.tick, onScore);
                        painter.setPen(QPen(_appearance.colors[_parameter->currentIndex() == 1 ? PerformanceAppearance::Tempo : PerformanceAppearance::Pedal], 2));
                        painter.drawLine(QPointF(x0, yForValue(bpmAt(*previous), rect, onScore)), QPointF(x1, yForValue(bpmAt(*previous), rect, onScore)));
                        painter.drawLine(QPointF(x1, yForValue(bpmAt(*previous), rect, onScore)), QPointF(x1, yForValue(bpm, rect, onScore)));
                        }
                  previous = &segment;
                  if (segment.tick > until) break;
                  }
            }
            for (const auto& tempo : _tempos) {
                  const double x = xForTick(tempo.tick, onScore), y = yForValue(_tempoDraft.value(tempo.tick, tempo.bpm), rect, onScore);
                  if (onScore && (tempo.tick < tickForX(rect.left(), true) || tempo.tick > tickForX(rect.right(), true))) continue;
                  painter.setPen(QPen(tempo.visible ? _appearance.colors[PerformanceAppearance::Preview] : _appearance.colors[PerformanceAppearance::Tempo], 2)); painter.setBrush(QBrush(_appearance.colors[PerformanceAppearance::Background]));
                  painter.drawRect(QRectF(x - 4, y - 4, 8, 8));
                  }
            for (auto i = _tempoDraft.cbegin(); i != _tempoDraft.cend(); ++i) {
                  const double x = xForTick(i.key(), onScore), y = yForValue(i.value(), rect, onScore);
                  painter.setPen(QPen(_appearance.colors[PerformanceAppearance::Preview], 2)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(QPointF(x, y), 4, 4);
                  }
            }
      else {
            const double y = yForValue(127, rect, onScore), bottom = yForValue(0, rect, onScore);
            auto drawPedal = [&](int from, int until, int track, bool preview) {
                  const double x0 = xForTick(from, onScore), x1 = xForTick(until, onScore);
                  if (until < tickForX(rect.left(), onScore) || from > tickForX(rect.right(), onScore)) return;
                  QColor color = _appearance.colors[preview ? PerformanceAppearance::Preview : PerformanceAppearance::Pedal];
                  painter.setPen(QPen(color, 2, preview ? Qt::DashLine : Qt::SolidLine));
                  auto fill = color; fill.setAlpha(onScore ? 30 : 40); painter.fillRect(QRectF(QPointF(x0, y), QPointF(x1, bottom)), fill);
                  painter.drawLine(QPointF(x0, bottom), QPointF(x0, y)); painter.drawLine(QPointF(x0, y), QPointF(x1, y)); painter.drawLine(QPointF(x1, y), QPointF(x1, bottom));
                  for (int side = 0; side < 2; ++side) {
                        painter.setPen(QPen(color, 2));
                        const double x = side ? x1 : x0;
                        const bool offNote = !pedalBoundary(side ? until : from, track, side);
                        painter.setBrush(color);
                        if (offNote) { QPolygonF diamond; diamond << QPointF(x, y - 4) << QPointF(x + 4, y) << QPointF(x, y + 4) << QPointF(x - 4, y); painter.drawPolygon(diamond);
                              painter.setPen(QPen(color, 1, Qt::DotLine)); painter.drawLine(QPointF(x, y + 5), QPointF(x, bottom)); }
                        else painter.drawRect(QRectF(x - 3, y - 3, 6, 6));
                        }
                  };
            for (const auto& pedal : _pedals) {
                  if (!pedalVisible(pedal)) continue;
                  const bool preview = _dragging && _pedalTarget == pedal.pedal;
                  drawPedal(preview ? _pedalFrom : pedal.from, preview ? _pedalUntil : pedal.until, pedal.track, preview);
                  }
            if (_dragging && !_pedalTarget) drawPedal(_pedalFrom, _pedalUntil, _pedalTrack, true);
            }
      painter.restore();
      }

void PerformanceEditor::paintForeground(QPainter& painter, PerformanceSurface surface)
      {
      const auto& c = _appearance.colors;
      QWidget* widget = surface == PerformanceSurface::Notes ? static_cast<QWidget*>(_noteCanvas) : (surface == PerformanceSurface::Ruler ? static_cast<QWidget*>(_ruler) : _canvas);
      const QRectF area(PerformanceViewport::gutter, 0, widget->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin, widget->height());
      painter.save(); painter.setClipRect(area, Qt::IntersectClip);
      const QRectF clip = painter.clipBoundingRect();
      if (surface == PerformanceSurface::Notes) {
            buildGeometry(); QSet<quint64> cells;
            for (const auto& geometry : _noteGeometry) {
                  const int i = geometry.index; const auto& note = _notes[i];
                  if (!geometry.rect.adjusted(-3, -3, 3, 3).intersects(clip)) continue;
                  if (_viewport.pixelsPerQuarter < 8 && !note.selected && i != _hover && !_playingNotes.contains(i) && !_pending.contains(note.note)) {
                        const quint64 cell = (quint64(qMax(0, int(qMax(area.left(), geometry.rect.left())) / 2)) << 8) | note.pitch;
                        if (cells.contains(cell)) continue; cells.insert(cell);
                        }
                  const auto edit = _pending.value(note.note, {note.type, note.raw});
                  const int midi = NoteVelocity::effective(note.base, edit.type, edit.raw);
                  QColor color = note.base < 0 && edit.type == Note::ValueType::OFFSET_VAL && _appearance.mode == 0 ? c[PerformanceAppearance::Grid].lighter(130) : _appearance.noteColor(midi, note.track); if (!note.enabled || !note.audible) color.setAlpha(45);
                  painter.setBrush(color); painter.setPen(note.selected ? QPen(c[PerformanceAppearance::Selection], 1.5) : QPen(color.darker(120), 1)); painter.drawRect(geometry.rect);
                  if (_playingNotes.contains(i)) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(c[PerformanceAppearance::Playhead], 2.5)); painter.drawRect(geometry.rect.adjusted(2, 2, -2, -2)); }
                  if (_pending.contains(note.note)) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(c[PerformanceAppearance::Preview], 1.5, Qt::DashLine)); painter.drawRect(geometry.rect.adjusted(-1, -1, 1, 1)); }
                  if (i == _hover) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(c[PerformanceAppearance::Text], 2)); painter.drawRect(geometry.rect.adjusted(-2, -2, 2, 2)); }
                  if (_viewport.rowHeight >= 16 && geometry.rect.width() >= 28) {
                        painter.setPen(color.lightness() > 145 ? Qt::black : Qt::white); painter.drawText(geometry.rect.adjusted(3, 0, -2, 0), Qt::AlignVCenter, note.name + (_showValues ? QString(" · %1%2").arg(note.base < 0 ? edit.raw : midi).arg(note.base < 0 && edit.type == Note::ValueType::OFFSET_VAL ? "%" : "") : QString()));
                        }
                  }
            if (_marquee) {
                  painter.setBrush(QColor(130, 190, 230, 35)); painter.setPen(QPen(c[PerformanceAppearance::Selection], 1, Qt::DashLine)); painter.drawRect(QRectF(_marqueeStart, _marqueeEnd).normalized());
                  }
            // Overlap count uses identical native tick/pitch coordinates, never time shifts.
            if (_hover >= 0) {
                  int count = 0; for (const auto& g : _noteGeometry) if (_notes[g.index].enabled && _notes[g.index].tick == _notes[_hover].tick && _notes[g.index].pitch == _notes[_hover].pitch) ++count;
                  if (count > 1) { painter.setPen(c[PerformanceAppearance::Text]); painter.drawText(QPointF(xForTick(_notes[_hover].tick, false) + 3, (_viewport.topPitch - _notes[_hover].pitch) * _viewport.rowHeight - 3), tr("×%1 · 右键选音").arg(count)); }
                  }
            }
      else if (surface == PerformanceSurface::Parameter) {
            _lane = laneRect(); paintLane(painter, _lane, false);
            if (_parameter->currentIndex() == 0) {
                  const int from = tickForX(clip.left() - 8, false), until = tickForX(clip.right() + 8, false);
                  auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
                  QSet<QPair<QPair<int, int>, int>> outlines;
                  for (auto it = first; it != _notes.cend() && it->tick <= until; ++it) {
                        int i = int(it - _notes.cbegin()); if (!it->enabled || !hasNoteValue(*it)) continue;
                        if (it->selected || i == _hover || _playingNotes.contains(i) || _pending.contains(it->note)) {
                              const QPointF point(xForTick(it->tick, false), yForValue(noteValue(i), _lane));
                              const int role = _pending.contains(it->note) ? PerformanceAppearance::Preview : (_playingNotes.contains(i) ? PerformanceAppearance::Playhead : PerformanceAppearance::Selection);
                              const auto outline = qMakePair(qMakePair(qRound(point.x() * 256), qRound(point.y() * 256)), role * 2 + (i == _hover));
                              if (outlines.contains(outline)) continue;
                              outlines.insert(outline); painter.setBrush(Qt::NoBrush);
                              painter.setPen(QPen(c[role], 1.5, role == PerformanceAppearance::Preview ? Qt::DashLine : Qt::SolidLine));
                              painter.drawEllipse(point, i == _hover ? 6 : 5, i == _hover ? 6 : 5);
                              }
                        }
                  }
            }
      const int tick = _seeking ? _seekTick : _playTick;
      if (tick >= 0) { painter.setPen(QPen(c[_seeking ? PerformanceAppearance::Preview : PerformanceAppearance::Playhead], 1.5)); const double x = xForTick(tick, false); painter.drawLine(QPointF(x, surface == PerformanceSurface::Parameter ? laneRect().top() : 0), QPointF(x, surface == PerformanceSurface::Parameter ? laneRect().bottom() : widget->height())); }
      painter.restore();
      if (_dragging && ((surface == PerformanceSurface::Parameter && !_noteGesture) || (surface == PerformanceSurface::Notes && _noteGesture))) {
            const QString value = _parameter->currentIndex() == 0 && _anchor >= 0 ? QString::number(noteValue(_anchor), 'f', 0) : QString::number(valueForY(_last.y(), _gestureLane), 'f', 0);
            QRectF badge(qBound(0.0, _last.x() + 10, double(widget->width() - 88)), qBound(0.0, _last.y() - 25, double(widget->height() - 24)), 84, 22);
            painter.fillRect(badge, c[PerformanceAppearance::Background]); painter.setPen(c[PerformanceAppearance::Preview]); painter.drawText(badge, Qt::AlignCenter, value + (rangeKind() == 1 ? " %" : (rangeKind() == 2 ? " BPM" : "")));
            }
      paintWheelBadge(painter, widget);
      }
}
