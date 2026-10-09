// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceview.h"
#include "performanceeditor.h"
#include "libmscore/mscore.h"
#include <QPainter>
#include <QPaintEvent>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
namespace Ms {
bool PerformanceKeyboard::isBlack(int pitch)
      { const int key = pitch % 12; return key == 1 || key == 3 || key == 6 || key == 8 || key == 10; }
QPolygonF PerformanceKeyboard::shape(int pitch, double topPitch, double rowHeight, double width)
      {
      const double y = (topPitch - pitch) * rowHeight;
      const double shoulder = width * .34, right = width - 1;
      if (isBlack(pitch)) return QPolygonF(QRectF(shoulder, y, right - shoulder, rowHeight));
      static const int naturalIndex[] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
      const int octave = pitch / 12, key = naturalIndex[pitch % 12];
      const double frontTop = (topPitch - octave * 12 - (key + 1) * 12.0 / 7 + 1) * rowHeight;
      const double frontBottom = frontTop + rowHeight * 12.0 / 7;
      QPolygonF outline;
      outline << QPointF(0, frontTop) << QPointF(shoulder, frontTop)
            << QPointF(shoulder, y) << QPointF(right, y) << QPointF(right, y + rowHeight)
            << QPointF(shoulder, y + rowHeight) << QPointF(shoulder, frontBottom) << QPointF(0, frontBottom);
      return outline;
      }
int PerformanceKeyboard::pitchAt(QPointF point, double topPitch, double rowHeight, double width)
      {
      if (point.x() < 0 || point.x() >= width) return -1;
      // At the right edge every key is precisely one pitch row high.
      if (point.x() >= width * .34) { const int pitch = int(std::ceil(topPitch - point.y() / rowHeight)); return pitch >= 0 && pitch <= 127 ? pitch : -1; }
      for (int pitch = 0; pitch <= 127; ++pitch)
            if (!isBlack(pitch) && shape(pitch, topPitch, rowHeight, width).containsPoint(point, Qt::OddEvenFill)) return pitch;
      return -1;
      }
PerformanceRange PerformanceViewport::limits(int kind)
      { return kind == 0 ? PerformanceRange{1, 127} : (kind == 1 ? PerformanceRange{-127, 12600} : (kind == 2 ? PerformanceRange{5, 999} : PerformanceRange{0, 127})); }
void PerformanceViewport::zoomRange(int kind, double factor, double anchor, bool keepBaseline)
      {
      if (kind == 3) return;
      auto& range = ranges[kind];
      const auto limit = limits(kind);
      const double fraction = (anchor - range.minimum) / (range.maximum - range.minimum);
      const double span = qBound(kind == 2 ? 5.0 : 8.0, (range.maximum - range.minimum) / factor, limit.maximum - limit.minimum);
      range.minimum = kind == 0 && keepBaseline ? limit.minimum : qBound(limit.minimum, anchor - fraction * span, limit.maximum - span);
      range.maximum = range.minimum + span;
      }
void PerformanceViewport::panRange(int kind, double delta)
      {
      if (kind == 3) return;
      auto& range = ranges[kind]; const auto limit = limits(kind);
      const double span = range.maximum - range.minimum;
      range.minimum = qBound(limit.minimum, range.minimum + delta, limit.maximum - span);
      range.maximum = range.minimum + span;
      }
void PerformanceIntervalIndex::clear() { for (auto& row : _rows) row.clear(); }
void PerformanceIntervalIndex::add(int index, int from, int until, int pitch)
      {
      auto& row = _rows[qBound(0, pitch, 127)];
      Q_ASSERT(row.isEmpty() || row.back().from <= from);
      row.append({index, from, until, qMax(until, row.isEmpty() ? 0 : row.back().maximumEnd)});
      }
QVector<int> PerformanceIntervalIndex::query(int from, int until, int lowPitch, int highPitch) const
      {
      QVector<int> result;
      for (int pitch = qMax(0, lowPitch); pitch <= qMin(127, highPitch); ++pitch) {
            const auto& row = _rows[pitch];
            auto it = std::upper_bound(row.cbegin(), row.cend(), until, [](int t, const Entry& e) { return t < e.from; });
            while (it != row.cbegin()) {
                  --it;
                  if (it->maximumEnd <= from) break;
                  if (it->until > from) result.append(it->index);
                  }
            }
      return result;
      }
PerformanceCanvas::PerformanceCanvas(PerformanceEditor* editor, PerformanceSurface surface)
      : QWidget(editor), _editor(editor), _surface(surface)
      {
      setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
      setObjectName(surface == PerformanceSurface::Notes ? "performanceNoteCanvas" : (surface == PerformanceSurface::Ruler ? "performanceRuler" : "performanceParameterCanvas"));
      if (surface == PerformanceSurface::Ruler) setFixedHeight(28);
      else setMinimumHeight(surface == PerformanceSurface::Notes ? 100 : 90);
      }
void PerformanceCanvas::resizeEvent(QResizeEvent*) { _editor->surfaceResized(_surface); }
void PerformanceCanvas::paintEvent(QPaintEvent* event)
      {
      QElapsedTimer paintTimer; paintTimer.start();
      const qreal ratio = devicePixelRatioF();
      if (_background.size() != size() * ratio || _background.devicePixelRatioF() != ratio || _revision != _editor->_visualRevision) {
            _background = QPixmap(size() * ratio); _background.setDevicePixelRatio(ratio);
            _background.fill(Qt::transparent);
            QPainter background(&_background); _editor->paintBackground(background, _surface);
            _revision = _editor->_visualRevision;
            }
      QPainter painter(this); painter.setClipRegion(event->region()); painter.drawPixmap(0, 0, _background);
      _editor->paintForeground(painter, _surface);
      if (qEnvironmentVariableIsSet("PERFORMANCE_PAINT_PROFILE")) qInfo("Performance paint %d: %.2f ms", int(_surface), paintTimer.nsecsElapsed() / 1e6);
      }
}
