// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVector>
#include <QtMath>
#include <algorithm>

namespace Ms {
// Session-relative monotonic milliseconds, supplied by the controller or a test.
class TapEstimate {
      QVector<double> _intervals;
      qint64 _last = -1;
      static double median(QVector<double> values) {
            std::sort(values.begin(), values.end());
            const int n = values.size();
            return n ? (values[(n-1)/2] + values[n/2])*.5 : 0.;
            }
   public:
      void reset() { _last = -1; _intervals.clear(); }
      bool add(qint64 timestamp) {
            if (timestamp < 0) return false;
            if (_last < 0) { _last = timestamp; return true; }
            const double interval = double(timestamp - _last);
            if (interval < 60.) return false;
            if (interval > qMax(5000., median(_intervals)*4.)) _intervals.clear();
            else { _intervals.append(interval); if (_intervals.size() > 12) _intervals.removeFirst(); }
            _last = timestamp;
            return true;
            }
      int count() const { return _last < 0 ? 0 : _intervals.size()+1; }
      bool ready() const { return _intervals.size() >= 3; }
      double interval() const { return median(_intervals); }
      double bpm(double quarterBeats = 1.) const { return ready() && interval() > 0 ? 60000.*quarterBeats/interval() : 0.; }
      bool stable() const {
            if (!ready()) return false;
            QVector<double> deviations;
            const double center = interval();
            for (double value : _intervals) deviations.append(qAbs(value-center));
            return median(deviations)/center <= .08;
            }
      };
}
