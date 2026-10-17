// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QVariantList>
#include <QVariantMap>
namespace Ms {
class Score;class ScoreView;
void showRegionalDifference(ScoreView*,Score* other,const QVariantList& changes);
bool regionalAuditionEnabled();
void setRegionalAuditionEnabled(bool);
QVariantMap switchRegionalWriting(ScoreView*,const QString& group,const QString& variant);
}
