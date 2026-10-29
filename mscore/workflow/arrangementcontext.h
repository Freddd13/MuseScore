// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QVariantMap>
namespace Ms {
class Score;
class Note;
class Measure;
namespace PluginAPI { class ScoreObserver; }
QVariantMap arrangementSourceDetails(Score *, const QVariantMap &scope);
int arrangementMeasureOrdinal(const Measure *);
void arrangementPitchDetails(QVariantMap &, const Note *);
QVariantMap arrangementPlaybackRead(PluginAPI::ScoreObserver *, Score *, const QString &, const QVariantMap &);
}
