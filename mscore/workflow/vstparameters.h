// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <functional>
class QWidget;
class QAction;
namespace Ms {
namespace VstHost {
class Plugin;
}
void showVstParameters(QWidget *, const QString &binding, const QString &title,
                       std::function<VstHost::Plugin *()> resolve,
                       std::function<void(bool commit)> changed, QAction *undo, QAction *redo);
void refreshVstParameterWindows();
bool vstParameterGestureActive();
void closeVstParameterWindows(QWidget *owner);
} // namespace Ms
