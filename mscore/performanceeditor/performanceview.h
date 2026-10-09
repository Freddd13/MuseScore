// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#ifndef MS_PERFORMANCEVIEW_H
#define MS_PERFORMANCEVIEW_H
#include <QWidget>
#include <QPixmap>
#include <QVector>
#include <QPolygonF>
#include <array>
namespace Ms {
class PerformanceEditor;
enum class PerformanceSurface { Notes, Ruler, Parameter };
// Black keys face left; natural-key centres and black centres follow semitone rows.
struct PerformanceKeyboard {
      static bool isBlack(int pitch);
      static QPolygonF shape(int pitch, double topPitch, double rowHeight, double width);
      static int pitchAt(QPointF point, double topPitch, double rowHeight, double width);
      };
struct PerformanceRange { double minimum, maximum; };
struct PerformanceViewport {
      static constexpr int gutter = 76;
      static constexpr int rightMargin = 14;
      static constexpr int parameterPadding = 6;
      double pixelsPerQuarter = 72, rowHeight = 12, topPitch = 72;
      std::array<PerformanceRange, 4> ranges {{{1, 127}, {-100, 100}, {5, 240}, {0, 127}}};
      static PerformanceRange limits(int kind);
      void zoomRange(int kind, double factor, double anchor, bool keepBaseline = true);
      void panRange(int kind, double delta);
      };
// Prefix maxima make long notes starting before the viewport discoverable.
class PerformanceIntervalIndex {
      struct Entry { int index, from, until, maximumEnd; };
      std::array<QVector<Entry>, 128> _rows;
   public:
      void clear();
      void add(int index, int from, int until, int pitch);
      QVector<int> query(int from, int until, int lowPitch = 0, int highPitch = 127) const;
      };
class PerformanceCanvas : public QWidget {
      PerformanceEditor* _editor;
      PerformanceSurface _surface;
      QPixmap _background;
      quint64 _revision = 0;
      void paintEvent(QPaintEvent*) override;
      void resizeEvent(QResizeEvent*) override;
   public:
      PerformanceCanvas(PerformanceEditor*, PerformanceSurface);
      PerformanceSurface surface() const { return _surface; }
      };
}
#endif
