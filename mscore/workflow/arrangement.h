// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QVariantMap>
class QMenu;
namespace Ms {
class Score;
namespace PluginAPI {
class ScoreObserver;
}
QVariantMap arrangementOperation(PluginAPI::ScoreObserver *, Score *, const QString &, const QVariantMap &);
QVariantMap arrangementSuggestions(Score *);
void showSuggestionPanel(bool visible = true);
void addSuggestionPanelAction(QMenu *);
void notifyArrangementSuggestions(Score *);
} // namespace Ms
