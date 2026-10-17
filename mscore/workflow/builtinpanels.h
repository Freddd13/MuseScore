// GPL-2.0-or-later
#pragma once
class QMenu;class QWidget;class QSplitter;class QVBoxLayout;
namespace Ms {
void showHarmonyPanel(bool visible=true);
void showExpressionPanel();
void addBuiltinPanelActions(QMenu*);
void installExpressionTools(QWidget*,QSplitter*,QVBoxLayout*);
}
