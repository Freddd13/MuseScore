// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MS_WORKFLOW_PANELDOCK_H
#define MS_WORKFLOW_PANELDOCK_H
#include <Qt>
#include <QString>
class QMainWindow;
class QDockWidget;
namespace Ms {
bool arrangeWorkflowDock(QMainWindow*,QDockWidget*,const QString& id,const QString& group,Qt::DockWidgetArea);
}
#endif
