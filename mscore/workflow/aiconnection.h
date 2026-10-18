// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
class QMenu;
namespace Ms {
void initializeBuiltinAI();
void shutdownBuiltinAI();
void showAIConnection();
void addAIConnectionAction(QMenu *);
void showPersonalGuide(const QString &page = QString());
QString builtinPanelPath(const QString &name);
} // namespace Ms
