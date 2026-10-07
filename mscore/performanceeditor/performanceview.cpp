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
namespace {
PerformanceRange bounds(int kind)
      { return kind == 0 ? PerformanceRange{1, 127} : (kind == 1 ? PerformanceRange{-127, 12600} : PerformanceRange{5, 999}); }
}
void PerformanceViewport::zoomRange(int kind, double factor, double anchor)
      {
      if (kind == 3) return;
      auto& range = ranges[kind];
      const auto limit = bounds(kind);
      const double fraction = (anchor - range.minimum) / (range.maximum - range.minimum);
      const double span = qBound(kind == 2 ? 5.0 : 8.0, (range.maximum - range.minimum) / factor, limit.maximum - limit.minimum);
      range.minimum = qBound(limit.minimum, anchor - fraction * span, limit.maximum - span);
      range.maximum = range.minimum + span;
      }
void PerformanceViewport::panRange(int kind, double delta)
      {
      if (kind == 3) return;
      auto& range = ranges[kind]; const auto limit = bounds(kind);
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
