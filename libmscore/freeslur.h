// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#ifndef MS_FREESLUR_H
#define MS_FREESLUR_H
#include <QPointF>
#include <QVector>
#include <QString>
#include <QRectF>
namespace Ms {
class Slur;
struct FreeSlurNode {
      double position = .5; // fraction of the complete notated slur span
      QPointF offset, tangent; // spatium units; tangent is a derivative adjustment
      bool locked = true; // legacy/manual nodes retain their position and tangent
      int before = 0, after = 0; // -1: above, 1: below; after belongs to the final node
      };
namespace FreeSlur {
constexpr int maximumNodes = 32; // 6 native grips + 3 * 32 fit the signed Grip enum
bool decode(const QString&, QVector<FreeSlurNode>*);
QString encode(const QVector<FreeSlurNode>&);
bool removeNode(QVector<FreeSlurNode>*,int index);
struct Route { double from=0,until=1;int direction=0; };
QVector<Route> routing(const QVector<FreeSlurNode>&,double from,double until);
bool smoothNode(Slur*,int index,FreeSlurNode*);
struct CurvePoint { double t; QPointF p,d; bool locked = false; };
struct AvoidanceResult {
      QVector<CurvePoint> points;
      int collisions = 0, candidates = 0;
      bool limited = false;
      };
// Pure bounded geometry; no score pointers, properties or undo commands.
AvoidanceResult avoid(const QVector<CurvePoint>&,const QVector<QRectF>&,bool up,double spatium,const QVector<Route>& routes={},bool automatic=true);
QPointF curvePosition(const QVector<CurvePoint>&,double t);
CurvePoint curvePoint(const QVector<CurvePoint>&,double t);

}
}
#endif
