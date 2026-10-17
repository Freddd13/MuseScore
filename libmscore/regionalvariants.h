// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>
class QIODevice;
namespace Ms {
class Score;
class MasterScore;
namespace RegionalVariants {
// Inactive versions contain compressed notation values, never live note objects.
QVariantMap state(const Score *);
QVariantMap create(Score *, int start, int end, const QVariantList &tracks, const QString &name);
QVariantMap duplicate(Score *, const QString &group, const QString &name, bool activate = false);
QVariantMap select(Score *, const QString &group, const QString &variant);
QVariantMap selectMany(Score *, const QVariantMap &choices);
QVariantMap storeCombination(Score *, const QString &name);
QVariantMap selectCombination(Score *, const QString &combination);
MasterScore *comparison(Score *, const QString &group, const QString &variant, QString *error);
QVariantList differences(Score *, Score *, const QString &group);
QVariantMap chooseMain(Score *, const QString &group, const QString &variant);
QVariantMap rename(Score *, const QString &group, const QString &variant, const QString &name);
QVariantMap expand(Score *, const QString &group, int start, int end);
QVariantMap importVersion(Score *, Score *source, const QString &group, const QString &name);
bool exportOrdinary(Score *, QIODevice *, const QString &filename, bool main);
QVariantMap check(Score *, int start, int end, const QVariantList &tracks);
QByteArray capture(Score *, int start, int end, const QVariantList &tracks, QString *error);
bool active(const Score *);
bool writing();
bool save(Score *, QIODevice *, const QString &filename, bool thumbnail);
bool restore(MasterScore *, const QByteArray &, QString *error);
} // namespace RegionalVariants
} // namespace Ms
