// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
class QDockWidget;
class QMainWindow;
class QMenu;
class QToolBar;
class QSplitter;
class QIcon;
class QString;
namespace Ms {
void addWorkspaceViews(QMenu *);
void addWorkspaceSelector(QToolBar *);
void addEditorDockActions(QDockWidget *);
void finishToolFullscreen(QMainWindow *);
void ensureEditorAreas(QSplitter *);
QIcon personalToolIcon(const QString &);
} // namespace Ms
