// Value-only routing of an editable slur through a smooth vertical corridor. GPL v2.
#pragma once
#include "freeslur.h"
namespace Ms { namespace FreeSlur {
struct RoutingCandidate {
      QVector<CurvePoint> points;
      bool limited = false;
      };
RoutingCandidate smoothRouting(const QVector<CurvePoint>&, const QVector<QRectF>&,
                              const QVector<QRectF>&, const QVector<Route>&,
                              double spatium, bool automatic);
} }
