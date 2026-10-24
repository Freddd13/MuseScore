// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "performancelabels.h"
#include "../workflow/editorareas.h"
#ifdef SCRIPT_INTERFACE
#include "spectrumpane.h"
#include "rollwriting.h"
#include "rhythmpane.h"
#endif
#include "libmscore/select.h"
#include "libmscore/pedal.h"
#include "libmscore/staff.h"
#include <QPainter>
#include <QCursor>
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
      { if(property("kumoEditorClosing").toBool())return;
#ifdef SCRIPT_INTERFACE
      if(_spectrum)_spectrum->viewChanged();
      if(_rollWriting)_rollWriting->contextChanged();
      if(auto rhythm=findChild<RhythmPane*>())rhythm->contextChanged();
#endif
      updateDetachedRulers(this);
      if (_canvas) _canvas->update(); if (_noteCanvas) _noteCanvas->update(); if (_ruler) _ruler->update(); if(_workflowPanel&&_workflowPanel->isVisible())for(auto child:_workflowPanel->findChildren<QWidget*>())child->update(); }
void PerformanceEditor::invalidateVisual()
      { ++_visualRevision; syncValueScroll(); updateSurfaces(); }
void PerformanceEditor::surfaceResized(PerformanceSurface surface)
      {
      if (property("kumoEditorClosing").toBool() || !_scroll || !_noteCanvas) return;
      if (_valueScroll && _canvas) _valueScroll->setGeometry(_canvas->width() - PerformanceViewport::rightMargin, 0, PerformanceViewport::rightMargin, _canvas->height());
      if (surface == PerformanceSurface::Notes && _pitchScroll) {
            const double rows = _noteCanvas->height() / _viewport.rowHeight;
            const double maximum=_viewport.rowMaximum();
            _viewport.topPitch = _viewport.pitchAtIndex(qBound(qMin(maximum, rows - 1), _viewport.pitchIndex(_viewport.topPitch), maximum));
            QSignalBlocker block(_pitchScroll);
            _pitchScroll->setGeometry(_noteCanvas->width() - PerformanceViewport::rightMargin, 0, PerformanceViewport::rightMargin, _noteCanvas->height());
            _pitchScroll->setRange(0, qMax(0, qRound((_viewport.rowMaximum()+1 - rows) * 100)));
            _pitchScroll->setPageStep(qRound(rows * 100)); _pitchScroll->setSingleStep(100);
            _pitchScroll->setValue(qRound((_viewport.rowMaximum() - _viewport.pitchIndex(_viewport.topPitch)) * 100));
            }
      int width=_canvas->width();bool visible=false;
      for(QWidget* canvas:QList<QWidget*>{_canvas,_noteCanvas})if(canvas&&canvas->isVisible()){width=visible?qMin(width,canvas->width()):canvas->width();visible=true;}
#ifdef SCRIPT_INTERFACE
      if(_spectrum&&_spectrum->linkedTimelineWidth()>0)width=visible?qMin(width,_spectrum->linkedTimelineWidth()):_spectrum->linkedTimelineWidth();
#endif
      const double units=qMax(10,width-PerformanceViewport::gutter-PerformanceViewport::rightMargin)/_viewport.pixelsPerQuarter;
      const int span=qMax(1,comparisonTimeActive()?_score->tempomap()->time2tick(_score->tempomap()->tick2time(_scroll->value())+units*.5)-_scroll->value():qRound(units*DIVISION));
      _scroll->setPageStep(span); _scroll->setRange(0, qMax(0, (comparisonTimeActive()?qMax(_endTick,_comparisonEnd):_endTick) - span));
      invalidateVisual();
      }
void PerformanceEditor::buildGeometry()
      {
      if(!_noteCanvas || !_noteCanvas->isVisible())return;
      if (_geometryRevision == _visualRevision) return;
      _geometryRevision = _visualRevision; _noteGeometry.clear();
      if (!_noteCanvas || _notes.isEmpty()) return;
      const int high = qMin(127, int(std::ceil(_viewport.topPitch)));
      const int low = qMax(0, int(std::floor(_viewport.pitchAtIndex(_viewport.pitchIndex(_viewport.topPitch) - _noteCanvas->height() / _viewport.rowHeight))));
      const int from = tickForX(PerformanceViewport::gutter, false), until = tickForX(_noteCanvas->width() - PerformanceViewport::rightMargin, false);
      for (int index : _intervals.query(from, until, low, high)) {
            const auto& note = _notes[index];
            if (!note.inScope || (!note.enabled && !_ghostVoices) || !_viewport.showsPitch(note.pitch)) continue;
            const double x = xForTick(note.tick, false), end = xForTick(qMax(note.tick + 1, note.end), false);
            _noteGeometry.append({index, QRectF(x, _viewport.pitchY(note.pitch) + 1, qMax(2.0, end - x - 1), qMax(1.0, _viewport.rowHeight - 2))});
            }
      }
void PerformanceEditor::paintBackground(QPainter& painter, PerformanceSurface surface, QWidget* widget)
      {
      const auto& c = _appearance.colors;
      painter.fillRect(widget->rect(), c[PerformanceAppearance::Background]);
      if (surface == PerformanceSurface::Notes) {
            const int low = qMax(0, int(_viewport.pitchAtIndex(_viewport.pitchIndex(_viewport.topPitch) - widget->height() / _viewport.rowHeight)) - 2);
            const int high = qMin(127, int(std::ceil(_viewport.topPitch)) + 2);
            for (int pitch = low; pitch <= high; ++pitch) {
                  if(!_viewport.showsPitch(pitch))continue;
                  const double y = _viewport.pitchY(pitch);
                  painter.fillRect(QRectF(PerformanceViewport::gutter, y, widget->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin, _viewport.rowHeight), c[PerformanceKeyboard::isBlack(pitch) ? PerformanceAppearance::AccidentalRow : PerformanceAppearance::NaturalRow]);
                  painter.setPen(c[PerformanceAppearance::Grid]); painter.drawLine(QPointF(PerformanceViewport::gutter, y), QPointF(widget->width() - PerformanceViewport::rightMargin, y));
                  }
            painter.save(); painter.setClipRect(QRectF(0, 0, PerformanceViewport::gutter, widget->height()));
            QVector<QPair<int, QRectF>> labels;
            for (bool black : {false, true}) for (int pitch = low; pitch <= high; ++pitch) {
                  if (PerformanceKeyboard::isBlack(pitch) != black || !_viewport.showsPitch(pitch)) continue;
                  painter.setPen(QPen(QColor(black ? "#161616" : "#777777"), 1)); painter.setBrush(QColor(black ? "#161616" : "#f1f1f1"));
                  if(_keyboardMode==1&&!_usedPitches.contains(pitch))painter.setOpacity(.28);
                  if(_keyboardMode==2&&!_viewport.pitchRows.isEmpty())painter.drawRect(QRectF(0,_viewport.pitchY(pitch),PerformanceViewport::gutter*(black?.58:1.),_viewport.rowHeight));
                  else painter.drawPolygon(PerformanceKeyboard::shape(pitch, _viewport.topPitch, _viewport.rowHeight, PerformanceViewport::gutter));
                  if (_keyboardNames && (_viewport.rowHeight >= 10 || pitch % 12 == 0)) {
                        const double y = _viewport.pitchY(pitch);
                        const auto keyBounds = (_keyboardMode==2&&!_viewport.pitchRows.isEmpty()) ? QRectF(0,y,PerformanceViewport::gutter,_viewport.rowHeight) : PerformanceKeyboard::shape(pitch,_viewport.topPitch,_viewport.rowHeight,PerformanceViewport::gutter).boundingRect();
                        labels.append(qMakePair(pitch, keyBounds));
                        }
                  painter.setOpacity(1);
                  }
            // Keep text above adjacent key shapes at compact pitch zoom.
            QFont font = painter.font(); font.setPixelSize(qBound(7, int(_viewport.rowHeight * .82), 12)); painter.setFont(font);
            const double labelHeight = qMax(_viewport.rowHeight, double(painter.fontMetrics().height() + 2));
            for (const auto& label : labels) {
                  const double center = label.second.center().y();
                  if (center < 0 || center > widget->height()) continue;
                  const int pitch = label.first;
                  const bool black = PerformanceKeyboard::isBlack(pitch);
                  painter.setPen(black ? QColor("#ededed") : QColor("#292929"));
                  painter.setOpacity(_keyboardMode == 1 && !_usedPitches.contains(pitch) ? .28 : 1.);
                  const double y = qBound(0., center - labelHeight / 2, qMax(0., widget->height() - labelHeight));
                  painter.drawText(QRectF(black ? 2 : PerformanceViewport::gutter * .58 + 2, y,
                        PerformanceViewport::gutter * (black ? .58 : .42) - 4, labelHeight), Qt::AlignCenter, pitchName(pitch));
                  }
            painter.setOpacity(1);
            painter.restore();
            }
      if (surface == PerformanceSurface::Parameter && !(_velocityArrangement&&_parameter->currentIndex()==0)) {
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
      if(surface==PerformanceSurface::Parameter && _parameter->currentIndex()==0 && _velocityArrangement){
            const auto range=_viewport.ranges[rangeKind()];for(int staff:_velocityStaves){const auto lane=velocityLane(_velocityStaffNote.value(staff,-1),laneRect(),false);if(lane.isEmpty())continue;
                  for(double value:{range.minimum,(range.minimum+range.maximum)/2.,range.maximum}){const double y=yForValue(value,lane);painter.setPen(c[PerformanceAppearance::Grid]);painter.drawLine(QPointF(lane.left(),y),QPointF(lane.right(),y));painter.setPen(c[PerformanceAppearance::Text]);painter.drawText(QRectF(1,y-7,PerformanceViewport::gutter-8,14),Qt::AlignRight|Qt::AlignVCenter,QString::number(value,'f',0));}
                  }
            }
      if (!_score) return;
      painter.save(); painter.setClipRect(QRectF(PerformanceViewport::gutter, 0, widget->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin, widget->height()));
      const int from = tickForX(PerformanceViewport::gutter, false), until = tickForX(widget->width(), false);
      if(comparisonTimeActive()){
            const double origin=_score->tempomap()->tick2time(_scroll->value());
            const double left=_score->tempomap()->tick2time(from),right=_score->tempomap()->tick2time(until);
            const double desired=80/(2*_viewport.pixelsPerQuarter);
            const double magnitude=std::pow(10.,std::floor(std::log10(desired)));const double ratio=desired/magnitude;const double step=magnitude*(ratio<=1?1:ratio<=2?2:ratio<=5?5:10);
            for(double seconds=std::ceil(left/step)*step;seconds<=right;seconds+=step){
                  const double x=PerformanceViewport::gutter+(seconds-origin)*2*_viewport.pixelsPerQuarter;
                  painter.setPen(QPen(c[PerformanceAppearance::Grid],.7));painter.drawLine(QPointF(x,surface==PerformanceSurface::Parameter?laneRect().top():0),QPointF(x,surface==PerformanceSurface::Parameter?laneRect().bottom():widget->height()));
                  if(surface==PerformanceSurface::Ruler){const int minutes=int(seconds)/60;const QString label=QString("%1:%2").arg(minutes).arg(seconds-minutes*60,4,'f',1,QChar('0'));painter.setPen(c[PerformanceAppearance::Text]);painter.drawText(QPointF(x+3,19),label);}
                  }
            painter.restore();return;
            }
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
            if(!onScore)paintVelocityHeaders(painter,rect);
            if(_relativeActual)paintOriginalVelocity(painter,rect,onScore);
            if(!onScore&&comparisonActive()){paintExpressionComparison(painter,rect);painter.restore();return;}
            const auto active = painter.clipBoundingRect().intersected(rect.adjusted(-8, -8, 8, 8));
            int from = tickForX(active.left() - 8, onScore), until = tickForX(active.right() + 8, onScore);
            auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
            const bool dense = !onScore && !_velocityArrangement && _viewport.pixelsPerQuarter < 8;
            // Identical native-time/value marks have identical pixels. Draw each style
            // once; keep every source note in the hit/selection/transaction indexes.
            QSet<QPair<QPair<int, int>, quint64>> marks;
            QVector<int> emphasis;
            const int stemWidth = _appearance.velocityWidth, columnWidth = qMax(2, stemWidth);
            QVector<QPair<double, double>> density; QVector<QColor> densityColors;
            const double baseline = qBound(rect.top(), yForValue(rangeKind() == 1 ? 0 : 1, rect, onScore), rect.bottom());
            if (dense) { density.fill({rect.bottom(), rect.bottom()}, int(rect.width() / columnWidth) + 1); densityColors.fill(_appearance.colors[PerformanceAppearance::Text], density.size()); }
            const int begin = onScore ? 0 : int(first - _notes.cbegin());
            const int end = onScore ? _systemIndices.size() : int(std::upper_bound(first, _notes.cend(), until, [](int t, const NoteInfo& n) { return t < n.tick; }) - _notes.cbegin());
            for (int position = begin; position < end; ++position) {
                  const int index = onScore ? _systemIndices[position] : position;
                  const auto it = _notes.cbegin() + index;
                  if (!it->inScope || (!it->enabled && !_ghostVoices) || !hasNoteValue(*it)) continue;
                  const double x = xForNote(index, onScore);
                  if (x < active.left() - 8 || x > active.right() + 8) continue;
                  const auto noteLane=velocityLane(index,rect,onScore);if(noteLane.isEmpty())continue;
                  const double y = velocityY(index, rect, onScore);
                  const double noteBaseline=qBound(noteLane.top(),yForValue(rangeKind()==1?0:1,noteLane,onScore),noteLane.bottom());
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
                  painter.drawLine(QPointF(x, noteBaseline), QPointF(x, y));
                  painter.setBrush(it->audible ? QBrush(color) : Qt::NoBrush); painter.drawEllipse(QPointF(x, y), onScore ? 2.5 : 2.0, onScore ? 2.5 : 2.0);
                  if (!_axis->currentIndex() && !it->generatedVelocities.isEmpty() && (it->selected || index==_hover)) {
                        const auto range=generatedVelocityRange(*it,edit.type,edit.raw);
                        const double low=yForValue(range.first,noteLane,onScore),high=yForValue(range.second,noteLane,onScore);
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
                  const auto noteLane=velocityLane(index,rect,onScore);if(noteLane.isEmpty())continue;
                  const double noteBaseline=qBound(noteLane.top(),yForValue(rangeKind()==1?0:1,noteLane,onScore),noteLane.bottom());
                  const double x = xForNote(index, onScore), y = velocityY(index, rect, onScore);
                  const auto edit = _pending.value(note.note, {note.type, note.raw});
                  QColor color = onScore ? scoreNoteColor(index) : _appearance.noteColor(NoteVelocity::effective(note.base, edit.type, edit.raw), note.track);
                  if (!note.enabled) color.setAlpha(40);
                  const QColor border = index == _hover ? _appearance.colors[PerformanceAppearance::Preview] : (onScore ? QColor("#303a43") : _appearance.colors[PerformanceAppearance::Selection]);
                  painter.setPen(QPen(border, stemWidth + 2)); painter.drawLine(QPointF(x, noteBaseline), QPointF(x, y));
                  painter.setPen(QPen(color, stemWidth)); painter.drawLine(QPointF(x, noteBaseline), QPointF(x, y));
                  painter.setPen(QPen(border, 2)); painter.setBrush(color); const double radius = qMax(3.0, stemWidth / 2.0 + 1);
                  painter.drawEllipse(QPointF(x, y), radius, radius);
                  }
            }
      else if (parameter == 1) {
            if(!onScore&&comparisonActive()){paintExpressionComparison(painter,rect);painter.restore();return;}
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
            const auto& segments = onScore ? _systemSegments : _segments;
            auto first = std::lower_bound(segments.cbegin(), segments.cend(), from, [](const SegmentInfo& s, int t) { return s.tick < t; });
            if (first != segments.cbegin()) --first;
            if (!paintTempoCurves(painter, rect, onScore)) {
            const SegmentInfo* previous = nullptr;
            for (auto it = first; it != segments.cend(); ++it) {
                  const auto& segment = *it;
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
            const int nodeFrom = tickForX(rect.left() - 6, onScore), nodeUntil = tickForX(rect.right() + 6, onScore);
            auto firstTempo = std::lower_bound(_tempos.cbegin(), _tempos.cend(), nodeFrom, [](const TempoInfo& t, int tick) { return t.tick < tick; });
            for (auto it = firstTempo; it != _tempos.cend() && it->tick <= nodeUntil; ++it) {
                  const auto& tempo = *it;
                  const double x = xForTick(tempo.tick, onScore), y = yForValue(_tempoDraft.value(tempo.tick, tempo.bpm), rect, onScore);
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
            auto drawPedal = [&](int from, int until, int track, bool preview, bool selected, const PedalPlaybackInfo* actual = nullptr) {
                  const double x0 = xForTick(from, onScore), x1 = xForTick(until, onScore);
                  if (until <= from || until < tickForX(rect.left(), onScore) || from > tickForX(rect.right(), onScore)) return;
                  QColor color = _appearance.colors[preview ? PerformanceAppearance::Preview : selected ? PerformanceAppearance::Selection : PerformanceAppearance::Pedal];
                  if (selected && onScore && !preview) color = MScore::selectColor[track % VOICES];
                  const bool real = actual && actual->downKnown && actual->upKnown && actual->up > actual->down;
                  painter.setPen(QPen(color, real ? 1 : selected ? 3 : 2, preview ? Qt::DashLine : real ? Qt::DotLine : Qt::SolidLine));
                  auto fill = color; fill.setAlpha(selected ? 65 : onScore ? 30 : 40);
                  if (!real) painter.fillRect(QRectF(QPointF(x0, y), QPointF(x1, bottom)), fill);
                  painter.drawLine(QPointF(x0, bottom), QPointF(x0, y));
                  painter.drawLine(QPointF(x0, y), QPointF(x1, y));
                  painter.drawLine(QPointF(x1, y), QPointF(x1, bottom));
                  if (real) {
                        const double a = xForTick(actual->down, onScore), b = xForTick(actual->up, onScore);
                        const double level = qMin(bottom - 2, y + 6);
                        painter.fillRect(QRectF(QPointF(a, level), QPointF(b, bottom)), fill);
                        painter.setPen(QPen(color, selected ? 3 : 2));
                        painter.drawLine(QPointF(a, bottom), QPointF(a, level));
                        painter.drawLine(QPointF(a, level), QPointF(b, level));
                        painter.drawLine(QPointF(b, level), QPointF(b, bottom));
                  }
                  for (int side = 0; side < 2; ++side) {
                        const double x = side ? x1 : x0, direction = side ? -1 : 1;
                        const bool offNote = !pedalBoundary(side ? until : from, track, side);
                        painter.setPen(QPen(color, selected ? 2 : 1)); painter.setBrush(color);
                        QPolygonF triangle;
                        triangle << QPointF(x, y - 4) << QPointF(x + direction * 9, y) << QPointF(x, y + 4);
                        painter.drawPolygon(triangle);
                        if (offNote) {
                              painter.setPen(QPen(color, 1, Qt::DotLine));
                              painter.drawLine(QPointF(x, y + 5), QPointF(x, bottom));
                        }
                  }
            };
            const int tool = _pedalTool->currentIndex();
            for (const auto& pedal : _pedals) {
                  if (!pedalVisible(pedal)) continue;
                  const bool selected = pedal.pedal == _selectedPedal;
                  const bool preview = _dragging && tool == 0 && _pedalTarget == pedal.pedal;
                  const bool gap = _dragging && tool == 2 && _pedalUntil > _pedalFrom
                        && _score->staff(_pedalTrack / VOICES)->part() == pedal.pedal->part();
                  if (gap && pedal.from < _pedalUntil && pedal.until > _pedalFrom) {
                        drawPedal(pedal.from, qMin(pedal.until, _pedalFrom), pedal.track, true, false);
                        drawPedal(qMax(pedal.from, _pedalUntil), pedal.until, pedal.track, true, false);
                  }
                  else {
                        auto actual = _pedalPlaybackRanges.constFind(pedal.pedal);
                        drawPedal(preview ? _pedalFrom : pedal.from, preview ? _pedalUntil : pedal.until, pedal.track, preview, selected,
                            !preview && actual != _pedalPlaybackRanges.cend() ? &actual.value() : nullptr);
                  }
            }
            if (_dragging && tool != 2 && tool != 3 && !_pedalTarget)
                  drawPedal(_pedalFrom, _pedalUntil, _pedalTrack, true, false);
            if (_dragging && (tool == 2 || tool == 3)) {
                  const auto color = _appearance.colors[PerformanceAppearance::Preview];
                  const double x0 = xForTick(_pedalFrom, onScore), x1 = xForTick(_pedalUntil, onScore);
                  painter.setPen(QPen(color, 1, Qt::DashLine));
                  painter.drawLine(QPointF(x0, y), QPointF(x0, bottom));
                  if (tool == 2) {
                        painter.drawLine(QPointF(x1, y), QPointF(x1, bottom));
                        painter.setPen(QPen(color, 2)); painter.drawLine(QPointF(x0, bottom), QPointF(x1, bottom));
                        if (qAbs(x1 - x0) > painter.fontMetrics().horizontalAdvance(tr("抬起")) + 8)
                              painter.drawText(QRectF(x0, y + 10, x1 - x0, 20), Qt::AlignCenter, tr("抬起"));
                  }
            }
            }
      painter.restore();
      }

void PerformanceEditor::paintForeground(QPainter& painter, PerformanceSurface surface, QWidget* widget)
      {
      const auto& c = _appearance.colors;
      const QRectF area(PerformanceViewport::gutter, 0, widget->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin, widget->height());
      painter.save(); painter.setClipRect(area, Qt::IntersectClip);
      const QRectF clip = painter.clipBoundingRect();
      if (surface == PerformanceSurface::Notes) {
            buildGeometry(); QSet<quint64> cells;
            struct ValueLabel { int index; QPointF anchor; QRectF preferred; QString text; };
            QVector<ValueLabel> values;
#ifdef SCRIPT_INTERFACE
            const bool showNotes=!_spectrum||_spectrum->showRollNotes();
#endif
            for (const auto& geometry : _noteGeometry) {
                  const int i = geometry.index; const auto& note = _notes[i];
#ifdef SCRIPT_INTERFACE
                  if(!showNotes)continue;
#endif
                  const auto edit = _pending.value(note.note, {note.type, note.raw});
                  const int midi = NoteVelocity::effective(note.base, edit.type, edit.raw);
                  const bool showValue=note.enabled&&(_showValues || (_peekHeld && (_peekAll || note.selected || i==_hover || geometry.rect.adjusted(-160,-80,160,80).contains(_noteCanvas->mapFromGlobal(QCursor::pos())))));
                  const QString value=showValue?QString::number(note.base<0?edit.raw:midi)+(note.base<0 && edit.type==Note::ValueType::OFFSET_VAL?"%":""):QString();
                  const double valueWidth=showValue?painter.fontMetrics().horizontalAdvance(value)+6:0;
                  const bool valueInside=showValue&&geometry.rect.height()>=painter.fontMetrics().height()+2&&geometry.rect.width()>=valueWidth;
                  if(showValue&&!valueInside) {
                        const QSizeF size(valueWidth,painter.fontMetrics().height()+2);
                        values.append({i,QPointF(geometry.rect.right(),geometry.rect.center().y()),QRectF(geometry.rect.right()+3,geometry.rect.center().y()-size.height()/2,size.width(),size.height()),value});
                        }
                  if (!geometry.rect.adjusted(-3, -3, 3, 3).intersects(clip)) continue;
                  if (_viewport.pixelsPerQuarter < 8 && !note.selected && i != _hover && !_playingNotes.contains(i) && !_pending.contains(note.note)) {
                        const quint64 cell = (quint64(qMax(0, int(qMax(area.left(), geometry.rect.left())) / 2)) << 8) | note.pitch;
                        if (cells.contains(cell)) continue; cells.insert(cell);
                        }
                  QColor color = note.base < 0 && edit.type == Note::ValueType::OFFSET_VAL && _appearance.mode == 0 ? c[PerformanceAppearance::Grid].lighter(130) : _appearance.noteColor(midi, note.track); if (!note.enabled || !note.audible) color.setAlpha(45);
#ifdef SCRIPT_INTERFACE
                  if(_rollWriting&&_rollWriting->dimsOriginal(note.tick,note.track))color.setAlpha(38);
#endif
                  painter.setBrush(color); painter.setPen(note.selected ? QPen(c[PerformanceAppearance::Selection], 1.5) : QPen(color.darker(120), 1)); painter.drawRect(geometry.rect);
                  if (_playingNotes.contains(i)) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(c[PerformanceAppearance::Playhead], 2.5)); painter.drawRect(geometry.rect.adjusted(2, 2, -2, -2)); }
                  if (_pending.contains(note.note)) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(c[PerformanceAppearance::Preview], 1.5, Qt::DashLine)); painter.drawRect(geometry.rect.adjusted(-1, -1, 1, 1)); }
                  if (i == _hover) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(c[PerformanceAppearance::Text], 2)); painter.drawRect(geometry.rect.adjusted(-2, -2, 2, 2)); }
                  if ((_viewport.rowHeight >= 16 && geometry.rect.width() >= 28) || valueInside) {
                        const QString full=valueInside?note.name+QString::fromUtf8(" · ")+value:note.name;
                        const QString text=valueInside&&painter.fontMetrics().horizontalAdvance(full)>geometry.rect.width()-5?value:painter.fontMetrics().elidedText(full,Qt::ElideRight,qMax(0,int(geometry.rect.width()-5)));
                        painter.setPen(color.lightness() > 145 ? Qt::black : Qt::white);
                        painter.drawText(geometry.rect.adjusted(3,0,-2,0),Qt::AlignVCenter,text);
                        }
                  }
            std::stable_sort(values.begin(),values.end(),[this](const ValueLabel& a,const ValueLabel& b){
                  const auto priority=[this](int i){return i==_hover?4:_notes[i].selected?3:_pending.contains(_notes[i].note)?2:_playingNotes.contains(i)?1:0;};
                  return priority(a.index)>priority(b.index);
                  });
            QVector<QPair<int,QRectF>> requests;for(const auto& value:values)requests.append({value.index,value.preferred});
            const auto& placed=_rollValueLabels.arrange(_geometryRevision,area.adjusted(2,2,-2,-2),requests,{},[this](PerformanceLabelSpace& space){
                  for(const auto& geometry:_noteGeometry)space.reserve(geometry.rect.adjusted(-2,-2,2,2));
                  });
            for(int index=0;index<values.size();++index) {
                  const auto& value=values[index];const auto box=placed[index];if(box.isEmpty())continue;
                  const QPointF target(qBound(box.left(),value.anchor.x(),box.right()),qBound(box.top(),value.anchor.y(),box.bottom()));
                  if(QLineF(target,value.anchor).length()>12){auto line=c[PerformanceAppearance::Text];line.setAlpha(100);painter.setPen(QPen(line,1));painter.drawLine(value.anchor,target);}
                  painter.fillRect(box,c[PerformanceAppearance::Background]);painter.setPen(c[PerformanceAppearance::Text]);painter.drawText(box,Qt::AlignCenter,value.text);
                  }
#ifdef SCRIPT_INTERFACE
            if(_rollWriting)_rollWriting->paint(painter,area);
#endif
            if (_marquee) {
                  painter.setBrush(QColor(130, 190, 230, 35)); painter.setPen(QPen(c[PerformanceAppearance::Selection], 1, Qt::DashLine)); painter.drawRect(QRectF(_marqueeStart, _marqueeEnd).normalized());
                  }
            // Overlap count uses identical native tick/pitch coordinates, never time shifts.
            if (_hover >= 0) {
                  int count = 0; for (const auto& g : _noteGeometry) if (_notes[g.index].enabled && _notes[g.index].tick == _notes[_hover].tick && _notes[g.index].pitch == _notes[_hover].pitch) ++count;
                  if (count > 1) { painter.setPen(c[PerformanceAppearance::Text]); painter.drawText(QPointF(xForTick(_notes[_hover].tick, false) + 3, _viewport.pitchY(_notes[_hover].pitch) - 3), tr("×%1 · 右键选音").arg(count)); }
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
                              if(velocityLane(i,_lane,false).isEmpty())continue;const QPointF point(xForTick(it->tick, false), velocityY(i, _lane,false));
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
      if (tick >= 0) { painter.setPen(QPen(c[_seeking ? PerformanceAppearance::Preview : PerformanceAppearance::Playhead], 1.5)); const double x = xForTick(comparisonPlayTick(tick), false); painter.drawLine(QPointF(x, surface == PerformanceSurface::Parameter ? laneRect().top() : 0), QPointF(x, surface == PerformanceSurface::Parameter ? laneRect().bottom() : widget->height())); }
      painter.restore();
      if (_dragging && _parameter->currentIndex() != 2 && ((surface == PerformanceSurface::Parameter && !_noteGesture) || (surface == PerformanceSurface::Notes && _noteGesture))) {
            const QString value = _parameter->currentIndex() == 0 && _anchor >= 0 ? QString::number(noteValue(_anchor), 'f', 0) : QString::number(valueForY(_last.y(), _gestureLane), 'f', 0);
            QRectF badge(qBound(0.0, _last.x() + 10, double(widget->width() - 88)), qBound(0.0, _last.y() - 25, double(widget->height() - 24)), 84, 22);
            painter.fillRect(badge, c[PerformanceAppearance::Background]); painter.setPen(c[PerformanceAppearance::Preview]); painter.drawText(badge, Qt::AlignCenter, value + (rangeKind() == 1 ? " %" : (rangeKind() == 2 ? " BPM" : "")));
            }
      paintWheelBadge(painter, widget);
      }
}
