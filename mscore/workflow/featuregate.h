// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
class QMainWindow;
class QAction;
namespace Ms {
bool personalFeaturesEnabled();
QString personalFeatureConfigurationPath();
QString personalFeatureStatus();
bool personalAction(const QString &key);
bool personalPlugin(const QString &path);
bool retiredPersonalPlugin(const QString &path);
void applyPersonalFeatureVisibility(QMainWindow *);
void markPersonalAction(QAction *);
} // namespace Ms
