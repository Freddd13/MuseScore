// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QVector>
#include <QMap>
#include <QString>
namespace Ms {
class Score;
using MarkVelocityText=QVector<QMap<int,QString>>;
MarkVelocityText readMarkVelocityText(Score*);
QString markVelocityAt(const MarkVelocityText&,int track,int tick);
}
