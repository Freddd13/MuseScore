// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
class QWidget;
class QMenu;
class QSplitter;
class QString;
namespace Ms {
class PerformanceEditor;
void installEditorAreas(PerformanceEditor *, QSplitter *, QMenu *);
void releaseEditorAreas(PerformanceEditor *);
bool detachedEditorAreaVisible(const PerformanceEditor *);
void updateDetachedRulers(const PerformanceEditor *);
bool showDetachedEditorArea(QWidget *);
bool setEditorAreaVisible(PerformanceEditor *, const QString &, bool);
bool openEditorArea(PerformanceEditor *, const QString &);
void saveEditorAreaLayout(PerformanceEditor *);
} // namespace Ms
