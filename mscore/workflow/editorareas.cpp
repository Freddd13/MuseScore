// SPDX-License-Identifier: GPL-2.0-or-later
#include "editorareas.h"
#include "mscore/musescore.h"
#include "mscore/performanceeditor/performanceeditor.h"
#include "mscore/performanceeditor/performanceview.h"
#include "workspaceviews.h"
#include <QDockWidget>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QHash>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace Ms {
namespace {
class EditorAreas : public QObject {
  struct Area {
    QPointer<QWidget> widget;
    QPointer<QDockWidget> dock;
    QPointer<PerformanceCanvas> ruler;
    QAction *shown = nullptr, *floating = nullptr, *right = nullptr,
            *restore = nullptr;
    QString title;
    int order = 0, page = 0;
  };
  PerformanceEditor *editor;
  QSplitter *split;
  QTabBar *tabs;
  QList<Area> areas;
  bool updating = false;
  QString workspace() const {
    auto mode =
        mscore ? mscore->property("kumoWorkspaceView").toString() : QString();
    return mode.isEmpty() ? QString("arrange") : mode;
  }
  QString heightKey() const {
    return "performanceEditor/areaHeights/" + workspace() + "/" +
           QString::number(tabs ? tabs->currentIndex() : 0);
  }
  QString key(const Area &area) const {
    return "performanceEditor/detached/" + workspace() + "/" +
           area.widget->objectName();
  }

  void saveHeights() {
    if (updating)
      return;
    QVariantMap values;
    const auto sizes = split->sizes();
    for (int i = 0; i < split->count(); ++i)
      if (!split->widget(i)->isHidden())
        values[split->widget(i)->objectName()] = sizes[i];
    QSettings().setValue(heightKey(), values);
  }
  void updateChecks(Area &area) {
    if (!area.widget)
      return;
    const bool outside = area.dock && area.widget->parentWidget() == area.dock;
    const bool visible =
        outside ? area.dock->isVisible() : !area.widget->isHidden();
    if (area.widget->objectName() == "performanceWorkflowPanel")
      if (auto button =
              editor->findChild<QToolButton *>("performanceWorkflowToggle")) {
        QSignalBlocker guard(button);
        button->setChecked(visible);
      }
    QSignalBlocker block(area.shown);
    area.shown->setChecked(visible);
    area.restore->setEnabled(outside);
    area.floating->setEnabled(!outside || !area.dock->isFloating());
    area.right->setEnabled(!outside || area.dock->isFloating());
  }
  void applyPage() {
    if (updating)
      return;
    updating = true;
    const int page = tabs ? tabs->currentIndex() : 0;
    const auto heights =
        QSettings()
            .value(heightKey(),
                   QSettings().value("performanceEditor/areaHeights/" +
                                     QString::number(page)))
            .toMap();
    for (auto &area : areas) {
      if (!area.widget || area.widget->property("kumoDetachedArea").toBool()) {
        updateChecks(area);
        continue;
      }
      if (page != 3) {
        const bool common = area.page == 0;
        const bool workflow =
            area.widget->objectName() == "performanceWorkflowPanel";
        bool visible =
            common ? page != 3 && !(page == 2 && area.widget->objectName() !=
                                                     "performanceNotesArea")
                   : page == area.page;
        if (common && visible)
          visible =
              QSettings()
                  .value("performanceEditor/area/" + area.widget->objectName(),
                         !workflow)
                  .toBool();
        area.widget->setVisible(visible);
        if (auto root = area.widget->property("root").value<QObject *>())
          root->setProperty("visible", visible);
      }
      updateChecks(area);
    }
    auto sizes = split->sizes();
    for (int i = 0; i < split->count(); ++i)
      if (!split->widget(i)->isHidden() &&
          heights.contains(split->widget(i)->objectName()))
        sizes[i] =
            qMax(48, heights.value(split->widget(i)->objectName()).toInt());
    split->setSizes(sizes);
    ensureEditorAreas(split);
    updating = false;
  }
  void restore(int index, bool remember = true, bool geometry = true) {
    auto &area = areas[index];
    if (!area.widget || !area.dock ||
        !area.widget->property("kumoDetachedArea").toBool())
      return;
    if (geometry)
      QSettings().setValue(key(area) + "/geometry", area.dock->saveGeometry());
    if (remember)
      QSettings().setValue(key(area) + "/position", "editor");
    int insertion = 0;
    while (insertion < split->count() &&
           split->widget(insertion)->property("kumoAreaOrder").toInt() <
               area.order)
      ++insertion;
    area.widget->setProperty("kumoDetachedArea", false);
    area.widget->setProperty("kumoDetachedDock", QVariant());
    split->insertWidget(insertion, area.widget);
    area.dock->hide();
    if (area.ruler)
      area.ruler->hide();
    if (remember && mscore)
      mscore->showPerformanceEditor(true);
    if (remember && tabs)
      tabs->setCurrentIndex(area.page);
    applyPage();
  }
  void detach(int index, bool floating) {
    auto &area = areas[index];
    if (!area.widget || !mscore)
      return;
    area.widget->setProperty("kumoAreaMoving", true);
    if (tabs && area.page > 0)
      tabs->setCurrentIndex(
          area.page); // Build the existing tool before moving it.
    saveHeights();
    if (!area.dock) {
      area.dock = new QDockWidget(area.title, mscore);
      area.dock->setObjectName("kumoEditorArea/" + area.widget->objectName());
      area.dock->installEventFilter(this);
      addEditorDockActions(area.dock);
      mscore->addDockWidget(Qt::RightDockWidgetArea, area.dock);
      area.dock->addAction(area.restore);
      connect(area.dock, &QDockWidget::visibilityChanged, this, [this, index] {
        updateChecks(areas[index]);
        if (areas[index].widget &&
            areas[index].widget->property("kumoDetachedArea").toBool())
          if (auto root =
                  areas[index].widget->property("root").value<QObject *>())
            root->setProperty("visible", areas[index].dock->isVisible());
      });
      connect(area.dock, &QDockWidget::topLevelChanged, this,
              [this, index](bool outside) {
                auto &value = areas[index];
                if (!updating &&
                    value.widget->property("kumoDetachedArea").toBool())
                  QSettings().setValue(key(value) + "/position",
                                       outside ? "floating" : "right");
                updateChecks(value);
              });
    }
    if (area.page == 0 && area.widget->objectName() == "performanceNotesArea" &&
        !area.ruler) {
      area.ruler = new PerformanceCanvas(editor, PerformanceSurface::Ruler);
      area.ruler->setObjectName("performanceDetachedNotesRuler");
      area.ruler->setProperty("performanceExtraRuler", true);
      area.ruler->installEventFilter(editor);
      qobject_cast<QVBoxLayout *>(area.widget->layout())
          ->insertWidget(0, area.ruler);
    }
    area.widget->setProperty("kumoDetachedArea", true);
    area.widget->setProperty(
        "kumoDetachedDock",
        QVariant::fromValue(static_cast<QObject *>(area.dock)));
    area.dock->setWidget(area.widget);
    if (tabs && area.page > 0)
      tabs->setCurrentIndex(0);
    if (floating) {
      area.dock->setFloating(true);
      const auto geometry =
          QSettings().value(key(area) + "/geometry").toByteArray();
      if (geometry.isEmpty())
        area.dock->resize(760, 440);
      else
        area.dock->restoreGeometry(geometry);
    } else {
      area.dock->setFloating(false);
      const auto savedArea = Qt::DockWidgetArea(
          QSettings()
              .value(key(area) + "/dockArea", int(Qt::RightDockWidgetArea))
              .toInt());
      const auto target = updating && (savedArea == Qt::LeftDockWidgetArea ||
                                       savedArea == Qt::TopDockWidgetArea ||
                                       savedArea == Qt::BottomDockWidgetArea)
                              ? savedArea
                              : Qt::RightDockWidgetArea;
      mscore->addDockWidget(target, area.dock);
      if (!updating && target == Qt::RightDockWidgetArea)
        for (auto other : mscore->findChildren<QDockWidget *>())
          if (other != area.dock && other->isVisible() &&
              !other->isFloating() && mscore->dockWidgetArea(other) == target) {
            mscore->tabifyDockWidget(other, area.dock);
            break;
          }
    }
    area.widget->show();
    if (area.ruler)
      area.ruler->show();
    area.dock->show();
    area.dock->raise();
    if (!updating) {
      QSettings().setValue(key(area) + "/position",
                           floating ? "floating" : "right");
      QSettings().setValue(key(area) + "/open", true);
    }
    area.widget->setProperty("kumoAreaMoving", false);
    applyPage();
    updateChecks(area);
  }

public:
  EditorAreas(PerformanceEditor *owner, QSplitter *splitter, QMenu *menu)
      : QObject(owner), editor(owner), split(splitter) {
    tabs = editor->findChild<QTabBar *>("workflowExpressionTabs");
    menu->clear();
    const QHash<QString, QPair<QString, int>> names{
        {"performanceNotesArea", {tr("钢琴卷帘"), 0}},
        {"performanceParametersArea", {tr("力度／速度／踏板"), 0}},
        {"performanceWorkflowPanel", {tr("音轨／演奏对应"), 0}},
        {"embeddedExpressionPage", {tr("演奏表达"), 1}},
        {"transcriptionSpectrumPane", {tr("频谱扒谱"), 2}},
        {"rhythmWritingPane", {tr("写节奏"), 3}}};
    for (int order = 0; order < split->count(); ++order) {
      auto widget = split->widget(order);
      if (!names.contains(widget->objectName()))
        continue;
      Area area;
      area.widget = widget;
      area.title = names.value(widget->objectName()).first;
      area.page = names.value(widget->objectName()).second;
      area.order = order;
      widget->setProperty("kumoAreaOrder", order);
      auto sub = menu->addMenu(area.title);
      area.shown = sub->addAction(tr("显示这个区域"));
      area.shown->setObjectName("editorArea/show/" + widget->objectName());
      area.shown->setCheckable(true);
      area.floating = sub->addAction(tr("悬浮显示"));
      area.right = sub->addAction(tr("放到右侧"));
      area.restore = sub->addAction(tr("放回编辑器"));
      area.floating->setObjectName("editorArea/float/" + widget->objectName());
      area.right->setObjectName("editorArea/right/" + widget->objectName());
      area.restore->setObjectName("editorArea/restore/" + widget->objectName());
      const int index = areas.size();
      areas.append(area);
      connect(area.floating, &QAction::triggered, this,
              [this, index] { detach(index, true); });
      connect(area.right, &QAction::triggered, this,
              [this, index] { detach(index, false); });
      connect(area.restore, &QAction::triggered, this,
              [this, index] { restore(index); });
      connect(area.shown, &QAction::triggered, this,
              [this, index](bool visible) {
                auto &value = areas[index];
                if (value.widget->property("kumoDetachedArea").toBool()) {
                  value.dock->setVisible(visible);
                  QSettings().setValue(key(value) + "/open", visible);
                } else {
                  if (visible && value.page > 0 && tabs)
                    tabs->setCurrentIndex(value.page);
                  value.widget->setVisible(visible);
                  if (value.page == 0)
                    QSettings().setValue("performanceEditor/area/" +
                                             value.widget->objectName(),
                                         visible);
                }
                ensureEditorAreas(split);
                updateChecks(value);
              });
      updateChecks(areas[index]);
    }
    auto reset = menu->addAction(tr("恢复区域高度"));
    connect(reset, &QAction::triggered, this, [this] {
      QSettings().remove(heightKey());
      QList<int> sizes;
      for (int i = 0; i < split->count(); ++i)
        sizes.append(160);
      split->setSizes(sizes);
      applyPage();
    });
    connect(split, &QSplitter::splitterMoved, this, [this] { saveHeights(); });
    connect(menu, &QMenu::aboutToShow, this, [this] {
      for (auto &area : areas)
        updateChecks(area);
    });
    if (tabs) {
      connect(tabs, &QTabBar::currentChanged, this, [this] { applyPage(); });
      connect(tabs, &QTabBar::tabBarDoubleClicked, this, [this](int page) {
        if (page > 0)
          for (int index = 0; index < areas.size(); ++index)
            if (areas[index].page == page) {
              detach(index, true);
              break;
            }
      });
    }
    if (mscore)
      mscore->installEventFilter(this);
    const int initialPage = tabs ? tabs->currentIndex() : 0;
    QTimer::singleShot(0, this, [this, initialPage] {
      restoreWorkspace(tabs && tabs->currentIndex() != initialPage
                           ? tabs->currentIndex()
                           : -1);
    });
  }
  void restoreWorkspace(int requestedPage = -1) {
    updating = true;
    for (int index = 0; index < areas.size(); ++index)
      restore(index, false, false);
    const int page =
        requestedPage >= 0
            ? requestedPage
            : QSettings()
                  .value("performanceEditor/activePage/" + workspace(), 0)
                  .toInt();
    if (tabs)
      tabs->setCurrentIndex(qBound(0, page, tabs->count() - 1));
    for (int index = 0; index < areas.size(); ++index) {
      const auto position =
          QSettings().value(key(areas[index]) + "/position").toString();
      if (position == "floating" || position == "right") {
        const bool open =
            QSettings().value(key(areas[index]) + "/open", true).toBool();
        detach(index, position == "floating");
        if (areas[index].dock)
          areas[index].dock->setVisible(open);
      }
    }
    if (tabs)
      tabs->setCurrentIndex(qBound(0, page, tabs->count() - 1));
    updating = false;
    applyPage();
  }

  void saveLayout() {
    if (updating)
      return;
    saveHeights();
    QSettings().setValue("performanceEditor/activePage/" + workspace(),
                         tabs ? tabs->currentIndex() : 0);
    for (const auto &area : areas)
      if (area.widget && area.dock &&
          area.widget->property("kumoDetachedArea").toBool()) {
        QSettings().setValue(key(area) + "/geometry",
                             area.dock->saveGeometry());
        QSettings().setValue(key(area) + "/position",
                             area.dock->isFloating() ? "floating" : "right");
        if (mscore && !area.dock->isFloating())
          QSettings().setValue(key(area) + "/dockArea",
                               int(mscore->dockWidgetArea(area.dock)));
      }
  }
  bool openArea(const QString &name) {
    for (auto &area : areas)
      if (area.widget && area.widget->objectName() == name) {
        if (area.page == 1 && tabs &&
            !area.widget->property("loaded").toBool()) {
          if (tabs->currentIndex() == 1)
            tabs->setCurrentIndex(0);
          tabs->setCurrentIndex(1);
        }
        if (openSaved(area.widget))
          return true;
        if (tabs)
          tabs->setCurrentIndex(area.page);
        area.widget->show();
        if (auto root = area.widget->property("root").value<QObject *>())
          root->setProperty("visible", true);
        ensureEditorAreas(split);
        updateChecks(area);
        return true;
      }
    return false;
  }
  bool setVisibleArea(const QString &name, bool visible) {
    for (auto &area : areas)
      if (area.widget && area.widget->objectName() == name) {
        if (area.widget->property("kumoDetachedArea").toBool()) {
          area.dock->setVisible(visible);
          area.widget->setVisible(visible);
          QSettings().setValue(key(area) + "/open", visible);
        } else {
          area.widget->setVisible(visible);
          if (area.page == 0)
            QSettings().setValue("performanceEditor/area/" + name, visible);
        }
        ensureEditorAreas(split);
        updateChecks(area);
        return true;
      }
    return false;
  }
  bool visible() const {
    for (const auto &area : areas)
      if (area.dock && area.dock->isVisible())
        return true;
    return false;
  }
  bool openSaved(QWidget *widget) {
    if (updating || widget->property("kumoAreaMoving").toBool())
      return false;
    for (int index = 0; index < areas.size(); ++index)
      if (areas[index].widget == widget) {
        if (widget->property("kumoDetachedArea").toBool()) {
          areas[index].dock->show();
          areas[index].dock->raise();
          widget->show();
          QSettings().setValue(key(areas[index]) + "/open", true);
          return true;
        }
        const auto position =
            QSettings().value(key(areas[index]) + "/position").toString();
        if (position == "floating" || position == "right") {
          detach(index, position == "floating");
          return true;
        }
      }
    return false;
  }
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched == mscore && event->type() == QEvent::DynamicPropertyChange &&
        static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() ==
            "kumoWorkspaceView") {
      restoreWorkspace();
    }

    if (event->type() == QEvent::Close && !updating)
      for (const auto &area : areas)
        if (area.dock == watched && area.widget)
          QSettings().setValue(key(area) + "/open", false);
    return QObject::eventFilter(watched, event);
  }
  void updateRulers() const {
    for (const auto &area : areas)
      if (area.ruler && area.ruler->isVisible())
        area.ruler->update();
  }
  void release() {
    updating = true;
    for (int i = 0; i < areas.size(); ++i) {
      restore(i, false);
      if (areas[i].dock)
        delete areas[i].dock;
    }
  }
};
QHash<const PerformanceEditor *, QPointer<EditorAreas>> controllers;
} // namespace
void installEditorAreas(PerformanceEditor *editor, QSplitter *splitter,
                        QMenu *menu) {
  auto controller = new EditorAreas(editor, splitter, menu);
  controllers.insert(editor, controller);
  QObject::connect(editor, &QObject::destroyed,
                   [editor] { controllers.remove(editor); });
}
void releaseEditorAreas(PerformanceEditor *editor) {
  if (auto controller = controllers.value(editor))
    controller->release();
}
bool detachedEditorAreaVisible(const PerformanceEditor *editor) {
  auto controller = controllers.value(editor);
  return controller && controller->visible();
}
void updateDetachedRulers(const PerformanceEditor *editor) {
  if (auto controller = controllers.value(editor))
    controller->updateRulers();
}
bool showDetachedEditorArea(QWidget *widget) {
  if (!widget)
    return false;
  for (auto controller : controllers)
    if (controller && controller->openSaved(widget))
      return true;
  return false;
}
} // namespace Ms

namespace Ms {
bool setEditorAreaVisible(PerformanceEditor *editor, const QString &name,
                          bool visible) {
  auto controller = controllers.value(editor);
  return controller && controller->setVisibleArea(name, visible);
}
} // namespace Ms

namespace Ms {
bool openEditorArea(PerformanceEditor *editor, const QString &name) {
  auto controller = controllers.value(editor);
  return controller && controller->openArea(name);
}
void saveEditorAreaLayout(PerformanceEditor *editor) {
  if (auto controller = controllers.value(editor))
    controller->saveLayout();
}
} // namespace Ms
