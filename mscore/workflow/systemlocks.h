// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVariantMap>
class QMenu;
class QToolBar;
namespace Ms {
class Score;
void addSystemLockActions(QMenu*);
void addSystemLockSelector(QToolBar*);
QVariantMap changeSystemLocks(Score*, bool lock, bool selectedLines, bool wholeScore = false);
}
