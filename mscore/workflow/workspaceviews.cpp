// SPDX-License-Identifier: GPL-2.0-or-later
#include "workspaceviews.h"
#include "systemlocks.h"
#include "editorareas.h"
#include "panelwindowcontrols.h"
#include "featuregate.h"
#include "mscore/musescore.h"
#include "mscore/performanceeditor/performanceeditor.h"
#include "mscore/scoreview.h"
#include "mscore/workspace.h"
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace Ms {
namespace {
class WorkspaceViews : public QObject {
  QPointer<MuseScore> window;
  QMenu *menu;
  QActionGroup *choices;
  QAction *scoreComparison;
  QString current = "arrange";
  bool changing = false;
  QList<QPointer<QToolButton>> selectors;
  QString key(const QString &name) const {
    return "personalWorkspaceViews/" + name;
  }
  void save() {
    if (!window || changing || !personalFeaturesEnabled())
      return;
    finishToolFullscreen(window);
    if (auto editor = window->findChild<PerformanceEditor *>())
      saveEditorAreaLayout(editor);
    QSettings settings;
    settings.setValue(key(current + "/layout"), window->saveState(1));
    if (auto dock = window->findChild<QDockWidget *>("performanceEditorDock"))
      if (!dock->isHidden() && !dock->isFloating()) {
        // Tabbed docks can retain the size of their inactive page. Save the
        // visible peer's extent so returning to this view restores the group.
        auto visible = dock;
        for (auto peer : window->tabifiedDockWidgets(dock))
          if (!peer->visibleRegion().isEmpty())
            visible = peer;
        settings.setValue(key(current + "/editorExtent"),
                          QVariantMap{{"width", visible->width()},
                                      {"height", visible->height()},
                                      {"windowWidth", window->width()},
                                      {"windowHeight", window->height()}});
      }
    settings.setValue(key("current"), current);
  }
  void restoreEditorExtent(const QString &mode) {
    if (!window || current != mode)
      return;
    auto dock = window->findChild<QDockWidget *>("performanceEditorDock");
    if (!dock || dock->isHidden() || dock->isFloating())
      return;
    const auto extent = QSettings().value(key(mode + "/editorExtent")).toMap();
    const auto area = window->dockWidgetArea(dock);
    const bool side = area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea;
    const int savedWindow = extent.value(side ? "windowWidth" : "windowHeight").toInt();
    const int savedSize = extent.value(side ? "width" : "height").toInt();
    if (savedWindow <= 0 || savedSize <= 0)
      return;
    const int available = side ? window->width() : window->height();
    const int wanted = qBound(1, qRound(double(savedSize) * available / savedWindow), available);
    const auto orientation = side ? Qt::Horizontal : Qt::Vertical;
    window->resizeDocks({dock}, {wanted}, orientation);
    if (window->QWidget::layout())
      window->QWidget::layout()->activate();
    if (!window->tabifiedDockWidgets(dock).isEmpty()) {
      // resizeDocks sizes the whole tab group. Measure its tab-bar overhead
      // rather than subtracting a fixed pixel size from the editor on return.
      const int actual = side ? dock->width() : dock->height();
      const int groupSize = qBound(1, wanted + (wanted - actual), available);
      if (actual > 0 && actual != wanted)
        window->resizeDocks({dock}, {groupSize}, orientation);
    }
  }
  void updateButtons() {
    auto action = choices->checkedAction();
    if (!action)
      return;
    for (auto button : selectors)
      if (button)
        button->setText(action->text());
  }
  void showScore(bool visible, bool arrangeFirstTime = false) {
    if (!window || !window->centralWidget())
      return;
    window->centralWidget()->setVisible(visible);
    window->setProperty("kumoScoreComparison", visible);
    if (current == "daw") {
      QSettings().setValue(key("daw/showScore"), visible);
      if (visible && arrangeFirstTime)
        QTimer::singleShot(0, this, [this] {
          if (!window || current != "daw" ||
              window->centralWidget()->isHidden())
            return;
          if (auto dock =
                  window->findChild<QDockWidget *>("performanceEditorDock")) {
            const int available =
                dock->height() + window->centralWidget()->height();
            window->resizeDocks({dock}, {available * 2 / 3}, Qt::Vertical);
          }
        });
    }
  }

public:
  explicit WorkspaceViews(MuseScore *main) : QObject(main), window(main) {
    window->setProperty("kumoWorkspaceView", current);
    menu = new QMenu(tr("工作视图"), main);
    menu->setObjectName("kumoWorkspaceMenu");
    markPersonalAction(menu->menuAction());
    Workspace::addMenuAndString(menu, "menu-kumo-workspace");
    choices = new QActionGroup(this);
    choices->setExclusive(true);
    for (const auto &item :
         QList<QPair<QString, QString>>{{"arrange", tr("编曲")},
                                        {"daw", tr("DAW")},
                                        {"engrave", tr("雕版")},
                                        {"post", tr("后处理")}}) {
      auto action = menu->addAction(item.second);
      action->setCheckable(true);
      action->setData(item.first);
      action->setObjectName("kumo-workspace-" + item.first);
      markPersonalAction(action);
      choices->addAction(action);
      Workspace::addActionAndString(action, action->objectName());
      connect(action, &QAction::triggered, this,
              [this, item] { activate(item.first); });
      if (item.first == current)
        action->setChecked(true);
    }
    menu->addSeparator();
    scoreComparison = menu->addAction(tr("显示乐谱对照"));
    scoreComparison->setObjectName("kumo-workspace-score");
    scoreComparison->setCheckable(true);
    scoreComparison->setChecked(true);
    scoreComparison->setEnabled(false);
    markPersonalAction(scoreComparison);
    Workspace::addActionAndString(scoreComparison, "kumo-workspace-score");
    connect(scoreComparison, &QAction::triggered, this,
            [this](bool shown) { showScore(shown, true); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { save(); });
    QTimer::singleShot(0, this, [this] {
      const auto saved =
          QSettings().value(key("current"), "arrange").toString();
      if (!personalFeaturesEnabled()) {
        if (saved != "arrange" && window)
          window->restoreState(
              QSettings().value(key("arrange/layout")).toByteArray(), 1);
        if (window)
          applyPersonalFeatureVisibility(window);
        return;
      }
      if (saved != "arrange")
        activate(saved, false);
      else if (window)
        window->setProperty("kumoWorkspaceView", current);
    });
  }
  void addMenu(QMenu *target) {
    if (!target)
      return;
    // readMenu creates a placeholder submenu from the saved identifier.
    // Replace that entry with the existing shared menu and its live actions.
    auto restored = Workspace::findMenuFromString("menu-kumo-workspace");
    if (restored && restored != menu) {
      for (auto action : restored->actions()) {
        if (menu->actions().contains(action))
          continue;
        if (auto child = action->menu()) {
          if (child->parent() == restored)
            child->setParent(menu);
        } else if (action->parent() == restored) {
          action->setParent(menu);
        }
        menu->addAction(action);
      }
      target->removeAction(restored->menuAction());
    }
    menu->setTitle(tr("工作视图"));
    Workspace::addMenuAndString(menu, "menu-kumo-workspace");
    if (!target->actions().contains(menu->menuAction()))
      target->addMenu(menu);
    if (window)
      if (auto toolbar = window->findChild<QToolBar *>("kumoPersonalTools"))
        if (!target->actions().contains(toolbar->toggleViewAction()))
          target->addAction(toolbar->toggleViewAction());
  }

  void addButton(QToolBar *toolbar) {
    if (!personalFeaturesEnabled())
      return;
    auto button = new QToolButton(toolbar);
    button->setObjectName("kumoWorkspaceSelector");
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu(menu);
    button->setToolTip(
        tr("工作视图：编曲、DAW、雕版和后处理；分别记住窗口布局。"));
    toolbar->addWidget(button);
    for (int i = selectors.size() - 1; i >= 0; --i)
      if (!selectors[i])
        selectors.removeAt(i);
    selectors.append(button);
    updateButtons();
  }
  void activate(const QString &mode, bool rememberCurrent = true) {
    if (!window || !personalFeaturesEnabled() || changing || mode == current)
      return;
    QAction *selected = nullptr;
    for (auto action : choices->actions())
      if (action->data().toString() == mode)
        selected = action;
    if (!selected)
      return;
    if (rememberCurrent)
      save();
    changing = true;
    current = mode;
    window->setProperty("kumoWorkspaceView", mode);
    const auto state = QSettings().value(key(mode + "/layout")).toByteArray();
    if (mode == "daw")
      window->showPerformanceEditor(true);
    if (!state.isEmpty())
      window->restoreState(state, 1);
    else if (mode == "daw") {
      for (auto dock : window->findChildren<QDockWidget *>()) {
        const bool summary =
            dock->property("kumoPanelGroup").toString() == "harmonySummary" ||
            (window->dockWidgetArea(dock) == Qt::TopDockWidgetArea &&
             dock->objectName().contains("HarmonyAssistant"));
        if (dock->objectName() != "performanceEditorDock" && !summary)
          dock->hide();
      }
    }
    if (state.isEmpty() && mode == "engrave")
      if (auto dock = window->findChild<QDockWidget *>("performanceEditorDock"))
        dock->hide();
    if (state.isEmpty() && (mode == "daw" || mode == "engrave"))
      if (auto input = window->findChild<QToolBar *>("entry-tools"))
        input->hide();
    scoreComparison->setEnabled(mode == "daw");
    const bool visible =
        mode != "daw" ||
        QSettings().value(key("daw/showScore"), false).toBool();
    {
      QSignalBlocker block(scoreComparison);
      scoreComparison->setChecked(visible);
    }
    showScore(visible, state.isEmpty());
    // Restore internal heights after the main window has restored dock sizes.
    QTimer::singleShot(0, this, [this, mode] {
      if (window && current == mode) {
        if (auto editor = window->findChild<PerformanceEditor*>())
          settleEditorAreaLayout(editor);
        restoreEditorExtent(mode);
      }
    });
    selected->setChecked(true);
    updateButtons();
    changing = false;
    QSettings().setValue(key("current"), current);
  }
  void returnToScore() {
    if (!window)
      return;
    if (current == "daw")
      activate("arrange");
    window->centralWidget()->show();
    window->raise();
    window->activateWindow();
    if (auto view = window->currentScoreView())
      view->setFocus();
  }
};
QPointer<WorkspaceViews> views;
WorkspaceViews *controller(MuseScore *main = nullptr) {
  if (!main)
    main = mscore;
  if (!views && main)
    views = new WorkspaceViews(main);
  return views;
}
} // namespace
void addWorkspaceViews(QMenu *menu) {
  if (auto instance = controller(
          qobject_cast<MuseScore *>(menu ? menu->parentWidget() : nullptr)))
    instance->addMenu(menu);
}
void addWorkspaceSelector(QToolBar *toolbar) {
  addSystemLockSelector(toolbar);
  if (auto instance = controller(qobject_cast<MuseScore *>(
          toolbar ? toolbar->parentWidget() : nullptr)))
    instance->addButton(toolbar);
}
void finishToolFullscreen(QMainWindow *window) {
  if (!window)
    return;
  for (auto dock : window->findChildren<QDockWidget*>())
    if (auto action = dock->findChild<QAction*>("kumoPanelFullscreen"))
      if (action->isChecked())
        action->setChecked(false);
  if (window->layout())
    window->layout()->activate();
}
void ensureEditorAreas(QSplitter *splitter) {
  if (!splitter || splitter->height() < 40)
    return;
  auto sizes = splitter->sizes();
  bool changed = false;
  splitter->setChildrenCollapsible(false);
  for (int index = 0; index < splitter->count(); ++index) {
    splitter->setCollapsible(index, false);
    if (!splitter->widget(index)->isHidden() && sizes[index] < 2) {
      sizes[index] = 80;
      changed = true;
    }
  }
  if (changed)
    splitter->setSizes(sizes);
}
QIcon personalToolIcon(const QString &key) {
  static const QStringList supported{
      "kumo-system-lock", "kumo-vst", "harmony-assistant", "reference-player",
      "performance-expression", "kumo-spectrum", "arrangement-suggestions", "kumo-rhythm",
      "kumo-roll-writing", "kumo-ai-connection"};
  if (!supported.contains(key))
    return QIcon();
  QPixmap image(96, 96);
  image.setDevicePixelRatio(4);
  image.fill(Qt::transparent);
  QPainter p(&image);
  p.setRenderHint(QPainter::Antialiasing);
  const auto ink = qApp->palette().color(QPalette::WindowText);
  const auto accent = qApp->palette().color(QPalette::Highlight);
  p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  if (key == "kumo-system-lock") {
    p.drawLine(3, 6, 21, 6);
    p.drawLine(3, 10, 21, 10);
    p.drawLine(3, 14, 9, 14);
    p.setPen(QPen(accent, 1.5));
    p.drawRoundedRect(QRectF(11, 13, 9, 8), 1, 1);
    p.drawArc(QRectF(13, 8, 5, 9), 0, 180 * 16);
  } else if (key == "kumo-vst") {
    p.drawRoundedRect(QRectF(3, 11, 18, 10), 1, 1);
    for (int x : {7, 11, 15, 19})
      p.drawLine(x, 16, x, 21);
    for (int x : {5, 9, 15})
      p.fillRect(QRectF(x, 11, 2, 5), ink);
    p.setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(14, 2, 14, 5);
    p.drawLine(18, 2, 18, 5);
    p.drawRoundedRect(QRectF(13, 5, 6, 3), 1, 1);
    p.drawLine(16, 8, 16, 10);
  } else if (key == "harmony-assistant") {
    for (int y : {8, 12, 16}) {
      p.setBrush(ink);
      p.drawEllipse(QRectF(5, y, 5, 3));
    }
    p.drawLine(10, 5, 10, 17);
    p.setPen(QPen(accent, 1.5));
    p.drawLine(15, 7, 20, 7);
    p.drawLine(15, 12, 20, 12);
  } else if (key == "reference-player") {
    for (int i = 0; i < 8; ++i) {
      const int h = (i * 7 % 11) + 2;
      p.drawLine(3 + i * 2, 12 - h / 2, 3 + i * 2, 12 + h / 2);
    }
  } else if (key == "performance-expression") {
    for (int i = 0; i < 4; ++i) {
      p.fillRect(QRectF(4 + i * 5, 18 - i * 3, 3, i * 3 + 3),
                 i == 2 ? accent : ink);
    }
  } else if (key == "kumo-spectrum") {
    p.drawLine(3, 3, 3, 21);
    for (int row = 0; row < 4; ++row) {
      auto color = row == 2 ? accent : ink;
      color.setAlpha(220 - row * 30);
      p.fillRect(QRectF(6, 4 + row * 4, 15 - row * 2, 2), color);
    }
  } else if (key == "kumo-rhythm") {
    p.setBrush(ink);
    p.drawEllipse(QRectF(3, 13, 5, 3));
    p.drawEllipse(QRectF(14, 13, 5, 3));
    p.drawLine(8, 14, 8, 4);
    p.drawLine(19, 14, 19, 4);
    p.drawLine(8, 4, 19, 4);
    p.setPen(QPen(accent, 1.5));
    for (int x : {5, 11, 17})
      p.drawLine(x, 19, x, 21);
  } else if (key == "kumo-roll-writing") {
    p.fillRect(QRectF(3, 5, 9, 3), ink);
    p.fillRect(QRectF(3, 11, 5, 3), ink);
    p.setPen(QPen(accent, 2.5, Qt::SolidLine, Qt::SquareCap));
    p.drawLine(10, 18, 19, 9);
    p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(8, 20, 10, 18);
  } else if (key == "kumo-ai-connection") {
    p.drawRoundedRect(QRectF(3, 4, 18, 13), 3, 3);
    p.drawLine(7, 17, 7, 21);
    p.drawLine(7, 21, 12, 17);
    p.setPen(QPen(accent, 2.5, Qt::SolidLine, Qt::RoundCap));
    for (int x : {7, 12, 17})
      p.drawPoint(x, 10);
  } else {
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(6, 3, 12, 12));
    p.drawLine(9, 18, 15, 18);
    p.drawLine(10, 21, 14, 21);
    p.setPen(QPen(accent, 1.5));
    p.drawLine(10, 11, 14, 7);
  }
  return QIcon(image);
}
void addEditorDockActions(QDockWidget *dock) {
  if (!dock || dock->property("kumoDockActions").toBool())
    return;
  dock->setProperty("kumoDockActions", true);
  auto windows = installPanelWindowControls(dock);
  const Qt::DockWidgetAreas areas =
      Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::TopDockWidgetArea |
      Qt::BottomDockWidgetArea;
  dock->setAllowedAreas(areas);
  auto floating = new QAction(QObject::tr("保持悬浮"), dock);
  floating->setCheckable(true);
  QObject::connect(
      floating, &QAction::toggled, dock, [dock, areas](bool locked) {
        if (locked)
          dock->setFloating(true);
        dock->setAllowedAreas(locked ? Qt::DockWidgetAreas(Qt::NoDockWidgetArea)
                                     : areas);
      });
  auto toScore = new QAction(QObject::tr("回到乐谱"), dock);
  QObject::connect(toScore, &QAction::triggered, dock, [dock, windows] {
    windows->fullscreenAction()->setChecked(false);
    dock->setProperty("kumoWindowTransition", false);
    if (dock->isFloating())
      dock->hide();
    if (auto instance = controller())
      instance->returnToScore();
  });
  dock->addActions({floating, toScore});
  dock->setContextMenuPolicy(Qt::ActionsContextMenu);
  auto title = new QWidget(dock);
  auto row = new QHBoxLayout(title);
  row->setContentsMargins(5, 1, 3, 1);
  row->setSpacing(3);
  auto name = new QLabel(dock->windowTitle(), title);
  name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  row->addWidget(name, 1);
  QObject::connect(dock, &QWidget::windowTitleChanged, name, &QLabel::setText);
  auto button = new QToolButton(title);
  button->setObjectName("performanceReturnToScore");
  toScore->setIcon(dock->style()->standardIcon(QStyle::SP_ArrowBack));
  button->setDefaultAction(toScore);
  button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  button->setAutoRaise(true);
  button->setVisible(dock->isFloating());
  row->addWidget(button);
  QObject::connect(dock, &QDockWidget::topLevelChanged, button,
                   &QWidget::setVisible);
  auto full = new QToolButton(title);
  full->setObjectName("kumoPanelFullscreenButton");
  full->setAutoRaise(true);
  full->setDefaultAction(windows->fullscreenAction());
  row->addWidget(full);
  auto detach = new QToolButton(title);
  detach->setAutoRaise(true);
  detach->setIcon(dock->style()->standardIcon(QStyle::SP_TitleBarNormalButton));
  detach->setToolTip(QObject::tr("悬浮／放回原位置"));
  row->addWidget(detach);
  QObject::connect(detach, &QToolButton::clicked, dock, [dock, floating, windows] {
    windows->fullscreenAction()->setChecked(false);
    if (dock->isFloating())
      floating->setChecked(false);
    dock->setFloating(!dock->isFloating());
  });
  auto close = new QToolButton(title);
  close->setAutoRaise(true);
  close->setIcon(dock->style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  close->setToolTip(QObject::tr("关闭面板"));
  row->addWidget(close);
  QObject::connect(close, &QToolButton::clicked, dock, [dock, windows] {
    windows->fullscreenAction()->setChecked(false);
    dock->close();
  });
  dock->setTitleBarWidget(title);
}
} // namespace Ms
