// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
class QDomDocument;
namespace Ms {
class Score;
namespace RegionalConnections {
// Values saved with each writing; note objects remain on the GUI thread.
QVariantList capture(Score *, int start, int end, const QSet<int> &tracks, QString *error);
bool validate(const QVariantList &, const QVariantMap &group, QString *error);
void strip(QDomDocument &);
void remove(Score *, int start, int end, const QSet<int> &tracks);
bool restore(Score *, const QVariantList &, QString *error);
} // namespace RegionalConnections
} // namespace Ms
