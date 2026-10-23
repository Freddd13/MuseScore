// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#ifndef MS_PERFORMANCE_LABELS_H
#define MS_PERFORMANCE_LABELS_H
#include <QHash>
#include <QRectF>
#include <QVector>
#include <cmath>

namespace Ms {
// Screen-space labels use nearby free space. All visible note heads are
// reserved before text is placed; the painter's damage clip does not alter
// placement between partial repaints.
class PerformanceLabelSpace {
      QRectF _bounds;
      QHash<quint64, QVector<QRectF>> _cells;
      static int cell(double value) { return int(std::floor(value / 32.)); }
      static quint64 key(int x, int y) { return (quint64(quint32(x)) << 32) | quint32(y); }
      bool overlaps(const QRectF& box) const {
            for (int y=cell(box.top()); y<=cell(box.bottom()); ++y)
                  for (int x=cell(box.left()); x<=cell(box.right()); ++x) {
                        const auto found=_cells.constFind(key(x,y));
                        if(found!=_cells.cend()) for(const auto& used:found.value())
                              if(box.intersects(used)) return true;
                        }
            return false;
            }
   public:
      explicit PerformanceLabelSpace(const QRectF& bounds) : _bounds(bounds) {}
      void reserve(QRectF box) {
            box=box.intersected(_bounds); if(box.isEmpty())return;
            for (int y=cell(box.top()); y<=cell(box.bottom()); ++y)
                  for (int x=cell(box.left()); x<=cell(box.right()); ++x)
                        _cells[key(x,y)].append(box);
            }
      QRectF place(const QRectF& preferred) {
            if(preferred.isEmpty() || preferred.width()>_bounds.width() || preferred.height()>_bounds.height())return {};
            const double step=preferred.height()+3;
            for(int side=0;side<2;++side) for(int shift=0;shift<7;++shift) {
                  const int row=shift==0?0:((shift+1)/2)*(shift%2?-1:1);
                  const double x=preferred.x()-(side?preferred.width()+20:0);
                  const double y=preferred.y()+row*step;
                  const QRectF box(qBound(_bounds.left(),x,_bounds.right()-preferred.width()),
                                   qBound(_bounds.top(),y,_bounds.bottom()-preferred.height()),preferred.width(),preferred.height());
                  if(overlaps(box.adjusted(-1,-1,1,1)))continue;
                  reserve(box.adjusted(-1,-1,1,1));return box;
                  }
            return {};
            }
      };

// Static score repaints (cursor blink, expose) reuse the same placement. Text
// widths, priority order, viewport geometry and handles all participate in the
// key; a changed value or selection therefore immediately gets a new layout.
class PerformanceValueLayout {
      quint64 _revision=0;
      QRectF _bounds;
      QVector<QPair<int,QRectF>> _requests;
      QVector<QRectF> _extra, _placed;
   public:
      template<class Reserve>
      const QVector<QRectF>& arrange(quint64 revision,const QRectF& bounds,
                    const QVector<QPair<int,QRectF>>& requests,const QVector<QRectF>& extra,Reserve reserve) {
            if(_revision==revision&&_bounds==bounds&&_requests==requests&&_extra==extra)return _placed;
            _revision=revision;_bounds=bounds;_requests=requests;_extra=extra;_placed.clear();
            if(requests.isEmpty())return _placed;
            PerformanceLabelSpace space(bounds);reserve(space);for(const auto& box:extra)space.reserve(box);
            for(const auto& request:requests)_placed.append(space.place(request.second));
            return _placed;
            }
      };
}
#endif
