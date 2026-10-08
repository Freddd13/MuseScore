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
bool blackKey(int pitch) { const int key = pitch % 12; return key == 1 || key == 3 || key == 6 || key == 8 || key == 10; }
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
            for (int pitch = qMax(0, int(_viewport.topPitch - widget->height() / _viewport.rowHeight)); pitch <= qMin(127, int(std::ceil(_viewport.topPitch))); ++pitch) {
                  const double y = (_viewport.topPitch - pitch) * _viewport.rowHeight;
                  painter.fillRect(QRectF(0, y, widget->width() - PerformanceViewport::rightMargin, _viewport.rowHeight), c[blackKey(pitch) ? PerformanceAppearance::AccidentalRow : PerformanceAppearance::NaturalRow]);
                  painter.setPen(c[PerformanceAppearance::Grid]); painter.drawLine(QPointF(0, y), QPointF(widget->width() - PerformanceViewport::rightMargin, y));
                  painter.fillRect(QRectF(0, y + 1, blackKey(pitch) ? 22 : 32, _viewport.rowHeight - 1), blackKey(pitch) ? QColor("#17191c") : QColor("#bec2c7"));
                  if (_viewport.rowHeight >= 16 || pitch % 12 == 0) {
                        painter.setPen(c[PerformanceAppearance::Text]); painter.drawText(QRectF(34, y, 40, _viewport.rowHeight), Qt::AlignVCenter, pitchName(pitch));
                        }
                  }
            }
      if (surface == PerformanceSurface::Parameter) {
            const auto range = _viewport.ranges[rangeKind()]; const QRectF lane = laneRect();
            painter.setPen(c[PerformanceAppearance::Text]);
            painter.drawText(4, 13, rangeKind() == 2 ? "♩ BPM" : (rangeKind() == 3 ? "CC64" : (rangeKind() == 1 ? "%" : "MIDI")));
            for (int i = 0; i <= 4; ++i) {
                  const double value = range.minimum + (range.maximum - range.minimum) * i / 4;
                  const double y = yForValue(value, lane);
                  painter.setPen(c[PerformanceAppearance::Grid]); painter.drawLine(QPointF(lane.left(), y), QPointF(lane.right(), y));
                  painter.setPen(c[PerformanceAppearance::Text]); painter.drawText(QRectF(1, y - 8, PerformanceViewport::gutter - 8, 16), Qt::AlignRight | Qt::AlignVCenter, rangeKind() == 3 ? (i == 0 ? tr("关") : (i == 4 ? tr("开") : QString())) : QString::number(value, 'f', 0));
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
                  painter.drawLine(QPointF(x, 0), QPointF(x, widget->height()));
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
      painter.save(); painter.setClipRect(rect.adjusted(-2, -20, 2, 2), Qt::IntersectClip);
      if (onScore) painter.fillRect(rect, _appearance.colors[PerformanceAppearance::Background]);
      const int parameter = _parameter->currentIndex();
      if (parameter == 0) {
            const auto active = painter.clipBoundingRect().intersected(rect.adjusted(-8, -8, 8, 8));
            int from = tickForX(active.left() - 8, onScore), until = tickForX(active.right() + 8, onScore);
            auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
            const bool dense = !onScore && _viewport.pixelsPerQuarter < 8;
            // Identical native-time/value marks have identical pixels. Draw each style
            // once; keep every source note in the hit/selection/transaction indexes.
            QSet<QPair<QPair<int, int>, quint64>> marks;
            QVector<QPair<double, double>> density;
            if (dense) density.fill({rect.bottom(), rect.bottom()}, int(rect.width() / 2) + 1);
            for (auto it = first; it != _notes.cend() && it->tick <= until; ++it) {
                  if (onScore && it->system != _system) continue;
                  const int index = int(it - _notes.cbegin());
                  if (!it->inScope || (!it->enabled && !_ghostVoices) || !hasNoteValue(*it)) continue;
                  const double x = xForTick(it->tick, onScore);
                  const double y = yForValue(noteValue(index), rect);
                  if (dense && !it->selected && index != _hover && !_playingNotes.contains(index) && !_pending.contains(it->note)) {
                        const int column = int((x - rect.left()) / 2);
                        if (column >= 0 && column < density.size()) {
                              density[column].first = qMin(density[column].first, y);
                              density[column].second = qMax(density[column].second == rect.bottom() ? y : density[column].second, y);
                              }
                        continue;
                        }
                  const auto edit = _pending.value(it->note, {it->type, it->raw});
                  QColor color = it->base < 0 && edit.type == Note::ValueType::OFFSET_VAL && _appearance.mode == 0 ? _appearance.colors[PerformanceAppearance::Grid].lighter(130) : _appearance.noteColor(NoteVelocity::effective(it->base, edit.type, edit.raw), it->track);
                  color.setAlpha(!it->enabled ? 40 : (it->audible ? 230 : 70));
                  const auto mark = qMakePair(qMakePair(qRound(x * 256), qRound(y * 256)), (quint64(color.rgba()) << 2) | (it->selected ? 2 : 0) | (it->audible ? 1 : 0));
                  if (marks.contains(mark)) continue;
                  marks.insert(mark); painter.setPen(QPen(color, it->selected ? 2 : 1));
                  painter.drawLine(QPointF(x, rect.bottom()), QPointF(x, y));
                  painter.setBrush(it->audible ? QBrush(color) : Qt::NoBrush); painter.drawEllipse(QPointF(x, y), 3.5, 3.5);
                  }
            if (dense) {
                  painter.setPen(QPen(_appearance.colors[PerformanceAppearance::Text], 1));
                  for (int i = 0; i < density.size(); ++i) if (density[i].first < rect.bottom()) {
                        const double x = rect.left() + i * 2;
                        painter.drawLine(QPointF(x, rect.bottom()), QPointF(x, density[i].first));
                        painter.drawLine(QPointF(x - 1, density[i].second), QPointF(x + 1, density[i].second));
                        }
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
            const SegmentInfo* previous = nullptr;
            for (auto it = first; it != _segments.cend(); ++it) {
                  const auto& segment = *it;
                  if (onScore && segment.system != _system) continue;
                  const double bpm = bpmAt(segment);
                  if (previous) {
                        const double x0 = xForTick(previous->tick, onScore), x1 = xForTick(segment.tick, onScore);
                        painter.setPen(QPen(_appearance.colors[_parameter->currentIndex() == 1 ? PerformanceAppearance::Tempo : PerformanceAppearance::Pedal], 2));
                        painter.drawLine(QPointF(x0, yForValue(bpmAt(*previous), rect)), QPointF(x1, yForValue(bpmAt(*previous), rect)));
                        painter.drawLine(QPointF(x1, yForValue(bpmAt(*previous), rect)), QPointF(x1, yForValue(bpm, rect)));
                        }
                  previous = &segment;
                  if (segment.tick > until) break;
                  }
            for (const auto& tempo : _tempos) {
                  const double x = xForTick(tempo.tick, onScore), y = yForValue(_tempoDraft.value(tempo.tick, tempo.bpm), rect);
                  if (onScore && (tempo.tick < tickForX(rect.left(), true) || tempo.tick > tickForX(rect.right(), true))) continue;
                  painter.setPen(QPen(tempo.visible ? _appearance.colors[PerformanceAppearance::Preview] : _appearance.colors[PerformanceAppearance::Tempo], 2)); painter.setBrush(QBrush(_appearance.colors[PerformanceAppearance::Background]));
                  painter.drawRect(QRectF(x - 4, y - 4, 8, 8));
                  }
            }
      else {
            painter.setPen(QPen(_appearance.colors[_parameter->currentIndex() == 1 ? PerformanceAppearance::Tempo : PerformanceAppearance::Pedal], 2));
            const double y = rect.center().y();
            for (const auto& pedal : _pedals) {
                  if (_scope->currentIndex() != 2 && _score && pedal.pedal->part() != _score->staff(_contextTrack / VOICES)->part()) continue;
                  int from = pedal.from, until = pedal.until;
                  if (_dragging && _pedalTarget == pedal.pedal) { from = _pedalFrom; until = _pedalUntil; }
                  const double x0 = xForTick(from, onScore), x1 = xForTick(until, onScore);
                  painter.drawLine(QPointF(x0, y), QPointF(x1, y));
                  painter.drawRect(QRectF(x0 - 4, y - 4, 8, 8)); painter.drawRect(QRectF(x1 - 4, y - 4, 8, 8));
                  }
            if (_dragging && !_pedalTarget) {
                  painter.setPen(QPen(_appearance.colors[PerformanceAppearance::Preview], 3));
                  painter.drawLine(QPointF(xForTick(_pedalFrom, onScore), y), QPointF(xForTick(_pedalUntil, onScore), y));
                  }
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
      if (tick >= 0) { painter.setPen(QPen(c[_seeking ? PerformanceAppearance::Preview : PerformanceAppearance::Playhead], 1.5)); const double x = xForTick(tick, false); painter.drawLine(QPointF(x, 0), QPointF(x, widget->height())); }
      painter.restore();
      if (_dragging && ((surface == PerformanceSurface::Parameter && !_noteGesture) || (surface == PerformanceSurface::Notes && _noteGesture))) {
            const QString value = _parameter->currentIndex() == 0 && _anchor >= 0 ? QString::number(noteValue(_anchor), 'f', 0) : QString::number(valueForY(_last.y(), _gestureLane), 'f', 0);
            QRectF badge(qBound(0.0, _last.x() + 10, double(widget->width() - 88)), qBound(0.0, _last.y() - 25, double(widget->height() - 24)), 84, 22);
            painter.fillRect(badge, c[PerformanceAppearance::Background]); painter.setPen(c[PerformanceAppearance::Preview]); painter.drawText(badge, Qt::AlignCenter, value + (rangeKind() == 1 ? " %" : (rangeKind() == 2 ? " BPM" : "")));
            }
      }
}
