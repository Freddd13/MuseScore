// SPDX-License-Identifier: GPL-2.0-or-later
#include "workspaceviews.h"
#include "systemlocks.h"
#include "editorareas.h"
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
    if (auto editor = window->findChild<PerformanceEditor *>())
      saveEditorAreaLayout(editor);
    QSettings settings;
    settings.setValue(key(current + "/layout"), window->saveState(1));
    settings.setValue(key("current"), current);
  }
  void updateButtons() {
    auto action = choices->checkedAction();
    if (!action)
      return;
    for (auto button : selectors)
      if (button)
        button->setText(action->text());
  }
  void showScore(bool visible) {
    if (!window || !window->centralWidget())
      return;
    window->centralWidget()->setVisible(visible);
    window->setProperty("kumoScoreComparison", visible);
    if (current == "daw") {
      QSettings().setValue(key("daw/showScore"), visible);
      if (visible)
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
            [this](bool shown) { showScore(shown); });
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
    scoreComparison->setEnabled(mode == "daw");
    const bool visible =
        mode != "daw" ||
        QSettings().value(key("daw/showScore"), false).toBool();
    {
      QSignalBlocker block(scoreComparison);
      scoreComparison->setChecked(visible);
    }
    showScore(visible);
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
    for (int x = 0; x < 5; ++x)
      for (int y = 0; y < 5; ++y) {
        auto color = ((x + y) % 4 == 0) ? accent : ink;
        color.setAlpha(65 + ((x * 3 + y) % 4) * 60);
        p.fillRect(QRectF(3 + x * 4, 3 + y * 4, 3, 3), color);
      }
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
  QObject::connect(toScore, &QAction::triggered, dock, [dock] {
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
  auto detach = new QToolButton(title);
  detach->setAutoRaise(true);
  detach->setIcon(dock->style()->standardIcon(QStyle::SP_TitleBarNormalButton));
  detach->setToolTip(QObject::tr("悬浮／放回原位置"));
  row->addWidget(detach);
  QObject::connect(detach, &QToolButton::clicked, dock, [dock, floating] {
    if (dock->isFloating())
      floating->setChecked(false);
    dock->setFloating(!dock->isFloating());
  });
  auto close = new QToolButton(title);
  close->setAutoRaise(true);
  close->setIcon(dock->style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  close->setToolTip(QObject::tr("关闭面板"));
  row->addWidget(close);
  QObject::connect(close, &QToolButton::clicked, dock, &QDockWidget::close);
  dock->setTitleBarWidget(title);
}
} // namespace Ms
