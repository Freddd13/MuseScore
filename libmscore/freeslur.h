// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#ifndef MS_FREESLUR_H
#define MS_FREESLUR_H
#include <QPointF>
#include <QVector>
#include <QString>
namespace Ms {
struct FreeSlurNode {
      double position = .5; // fraction of the complete notated slur span
      QPointF offset, tangent; // spatium units; tangent is a derivative adjustment
      };
namespace FreeSlur {
constexpr int maximumNodes = 32; // 6 native grips + 3 * 32 fit the signed Grip enum
bool decode(const QString&, QVector<FreeSlurNode>*);
QString encode(const QVector<FreeSlurNode>&);
}
}
#endif
