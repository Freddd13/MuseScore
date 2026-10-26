//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2002-2012 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "qmlplugin.h"
#include "mscore/workflow/panelwindowcontrols.h"
#include "mscore/workflow/paneldock.h"
#include <QDockWidget>
#include <QMainWindow>
#include <QTimer>
#include <QStyle>
#include <QQuickWindow>
#include <QApplication>
#include <QCloseEvent>
#include <QToolTip>
#include <QScreen>
#include <QGuiApplication>
#include <QKeyEvent>

#include "libmscore/musescoreCore.h"

namespace Ms {
namespace {
class PanelGrip final : public QWidget {
public:
      explicit PanelGrip(QWidget* parent):QWidget(parent) {setFixedHeight(4);}
      QSize sizeHint() const override {return {24,4};}
      QSize minimumSizeHint() const override {return {24,4};}
};
}
//---------------------------------------------------------
//   QmlPlugin
//---------------------------------------------------------

QmlPlugin::QmlPlugin(QQuickItem* parent)
   : QQuickItem(parent)
      {}

QmlPlugin::~QmlPlugin()
      {
      auto release=[this](QPointer<QDockWidget>& pointer,QPointer<QQuickWindow>& window) {
            auto dock=pointer.data();pointer.clear();
            if(!dock)return;dock->removeEventFilter(this);disconnect(dock,nullptr,this,nullptr);
            if(window){const auto items=window->contentItem()->childItems();for(auto item:items)item->setParentItem(nullptr);}
            window.clear();dock->deleteLater();
            };
      release(_summaryDock,_summaryWindow);release(_detailDock,_detailWindow);
      }

void QmlPlugin::disposePanelForReload()
      {
      auto dock=_panelDock.data();
      if(dock){dock->close();_panelDock.clear();delete dock;}
      }

void QmlPlugin::attachPanelDock(QDockWidget* dock)
      {
      _panelDock = dock;
      dock->installEventFilter(this);
      if (!property("preferredPanelGroup").toString().isEmpty())
            installPanelWindowControls(dock);
      if(property("independentPanelSurfaces").toBool())dock->setAttribute(Qt::WA_DeleteOnClose,false);
      auto watchKeys = [this](QQuickWindow* window) { if (window) window->installEventFilter(this); };
      connect(this, &QQuickItem::windowChanged, this, watchKeys);
      watchKeys(window());
      auto location = [this](Qt::DockWidgetArea area) {
            _panelPlacement = area == Qt::TopDockWidgetArea ? "top" :
                  area == Qt::BottomDockWidgetArea ? "bottom" :
                  area == Qt::LeftDockWidgetArea ? "left" : "right";
            emit panelDockChanged();
            const int preferred=property("preferredRibbonHeight").toInt();
            const bool compact=!_panelDock->isFloating() && preferred>0 && (preferred<=44 || property("compactPanelTitle").toBool()) && (area==Qt::TopDockWidgetArea || area==Qt::BottomDockWidgetArea);
            if(compact && !_panelDock->property("compactPluginTitle").toBool()) {
                  auto grip=new PanelGrip(_panelDock);grip->setToolTip(tr("拖动此处调整面板位置"));
                  _panelDock->setTitleBarWidget(grip);_panelDock->setProperty("compactPluginTitle",true);
                  }
            else if(!compact && _panelDock->property("compactPluginTitle").toBool()) {
                  auto old=_panelDock->titleBarWidget();_panelDock->setTitleBarWidget(nullptr);if(old)old->deleteLater();
                  _panelDock->setProperty("compactPluginTitle",false);
                  }
            if (preferred > 0 && !_panelDock->property("kumoWindowTransition").toBool()
                && (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea))
                  QTimer::singleShot(0,this,[this,preferred]() {
                        if (!_panelDock || _panelDock->isFloating()) return;
                        if (auto main=qobject_cast<QMainWindow*>(_panelDock->parentWidget()))
                              main->resizeDocks({_panelDock.data()},{qBound(24,preferred,400)+(property("ribbonContentHeight").toBool()&&_panelDock->widget()?qMax(0,_panelDock->height()-_panelDock->widget()->height()):0)},Qt::Vertical);
                        });
            };
      connect(dock, &QDockWidget::dockLocationChanged, this, location);
      connect(dock, &QDockWidget::topLevelChanged, this, [this,location](bool) { if(auto main=qobject_cast<QMainWindow*>(_panelDock->parentWidget()))location(main->dockWidgetArea(_panelDock));else emit panelDockChanged(); });
      if (auto main = qobject_cast<QMainWindow*>(dock->parentWidget())) location(main->dockWidgetArea(dock));
      }
void QmlPlugin::focusPanel()
      {
      if (!_panelDock) return;
      _panelDock->show();_panelDock->raise();_panelDock->activateWindow();
      if (_panelDock->widget()) _panelDock->widget()->setFocus(Qt::OtherFocusReason);
      if(window()){window()->requestActivate();forceActiveFocus(Qt::OtherFocusReason);}
      }
void QmlPlugin::hidePanel() { if(_panelDock)_panelDock->hide(); }
void QmlPlugin::resizePanel(int width,int height) { if(_panelDock&&_panelDock->isFloating())_panelDock->resize(qBound(200,width,1600),qBound(100,height,1200)); }
void QmlPlugin::showPanelHint(const QString& text, QQuickItem* target)
      {
      if(text.isEmpty() || !target || !target->window()) {QToolTip::hideText();return;}
      const auto local=target->mapToScene(QPointF(0,target->height()+4)).toPoint();
      QToolTip::showText(target->window()->mapToGlobal(local),text);
      }
QVariantMap QmlPlugin::panelScreen(QQuickItem* target) const
      {
      auto window=target?target->window():this->window();if(!window)return {};
      auto point=window->mapToGlobal(target?target->mapToScene(QPointF(target->width()/2,target->height()/2)).toPoint():QPoint());
      auto screen=QGuiApplication::screenAt(point);if(!screen)screen=window->screen();if(!screen)return {};
      const auto area=screen->availableGeometry();return {{"x",area.x()},{"y",area.y()},{"width",area.width()},{"height",area.height()}};
      }
void QmlPlugin::fitPanelHeight(int height,bool auxiliary,bool reset)
      {
      auto target=auxiliary?_summaryDock.data():_panelDock.data();
      if(target&&(target->property("kumoPanelFullScreen").toBool()||target->property("kumoWindowTransition").toBool()))return;
      const int content=qBound(28,height,200);
      QTimer::singleShot(0,this,[this,content,auxiliary,reset]{
            auto dock=auxiliary?_summaryDock.data():_panelDock.data();if(!dock||!dock->widget()||!dock->isVisible())return;
            if(dock->property("kumoPanelFullScreen").toBool()||dock->property("kumoWindowTransition").toBool())return;
            const int chrome=qMax(0,dock->height()-dock->widget()->height());
            if(!reset&&dock->widget()->height()>=content)return;
            if(dock->isFloating()){dock->resize(dock->width(),content+chrome);return;}
            auto main=qobject_cast<QMainWindow*>(dock->parentWidget());if(!main)return;
            const auto area=main->dockWidgetArea(dock);if(area==Qt::TopDockWidgetArea||area==Qt::BottomDockWidgetArea)main->resizeDocks({dock},{content+chrome},Qt::Vertical);
            });
      }
QQuickItem* QmlPlugin::detailPanelHost() const
      { return _detailWindow ? _detailWindow->contentItem() : nullptr; }
bool QmlPlugin::detailPanelVisible() const
      { return _detailDock && _detailDock->isVisible(); }
bool QmlPlugin::eventFilter(QObject* object, QEvent* event)
      {
      if(object==_panelDock && event->type()==QEvent::Close) {
            const auto handler=property("panelCloseHandler").toString().toUtf8();
            if(!handler.isEmpty())QMetaObject::invokeMethod(this,handler.constData(),Qt::DirectConnection);
            }
      if (object==_detailDock && event->type()==QEvent::Close) emit panelDetailClosed();
      if (object==_summaryDock && event->type()==QEvent::Close) emit panelSummaryClosed();
      if(event->type()==QEvent::WindowDeactivate && qobject_cast<QQuickWindow*>(object)) {
            const auto cancel=property("panelSpaceCancelHandler").toString().toUtf8();
            if(!cancel.isEmpty())QMetaObject::invokeMethod(this,cancel.constData(),Qt::DirectConnection);
            }
      if (event->type()==QEvent::ShortcutOverride || event->type()==QEvent::KeyPress || event->type()==QEvent::KeyRelease) {
            auto keys=static_cast<QKeyEvent*>(event);
            auto quick=qobject_cast<QQuickWindow*>(object);
            const QByteArray handler=property("panelSpaceHandler").toString().toUtf8();
            QQuickItem* focus=quick ? quick->activeFocusItem() : nullptr;
            bool editingText=false;
            for (auto item=focus; item; item=item->parentItem())
                  if (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit")) { editingText=true; break; }
            if (quick && !editingText && !handler.isEmpty() && keys->key()==Qt::Key_Space && keys->modifiers()==Qt::NoModifier) {
                  const auto release=property("panelSpaceReleaseHandler").toString().toUtf8();
                  if (!keys->isAutoRepeat()) {
                        QByteArray action;
                        if(event->type()==QEvent::KeyRelease)action=release;
                        else if(event->type()==QEvent::KeyPress)action=release.isEmpty()?handler:property("panelSpacePressHandler").toString().toUtf8();
                        if(!action.isEmpty())QMetaObject::invokeMethod(this,action.constData(),Qt::DirectConnection);
                        }
                  keys->accept(); return true;
                  }
            }
      return QQuickItem::eventFilter(object,event);
      }
void QmlPlugin::showDetailPanel(bool visible)
      {
      if (!visible) {if (_detailDock) _detailDock->hide();return;}
      if (!_panelDock) return;
      auto main=qobject_cast<QMainWindow*>(_panelDock->parentWidget());if (!main) return;
      if (!_detailDock) {
            QString title=property("detailPanelTitle").toString();
            if (title.isEmpty()) title=_panelDock->windowTitle()+tr(" - Details");
            _detailDock=new QDockWidget(title,main);_detailDock->setObjectName(_panelDock->objectName()+"/details");
            _detailWindow=new QQuickWindow();_detailWindow->setObjectName(_panelDock->objectName()+"/detailWindow");
            const QColor background=property("detailPanelBackground").value<QColor>();
            _detailWindow->setColor(background.isValid() ? background : QApplication::palette().color(QPalette::Window));
            _detailDock->setWidget(QWidget::createWindowContainer(_detailWindow));
            _detailDock->setMinimumWidth(240);_detailDock->resize(360,740);
            installPanelWindowControls(_detailDock);
            _detailDock->installEventFilter(this);
            connect(_detailDock,&QDockWidget::visibilityChanged,this,[this](bool){emit panelDetailChanged();});
            // One shared QML tree, no second plugin / observer / analysis worker.
            // The container owns its QWindow; close the auxiliary surface with the plugin root.
            connect(this,&QObject::destroyed,_detailDock,&QDockWidget::deleteLater);
            main->addDockWidget(Qt::RightDockWidgetArea,_detailDock);
            const bool restored=arrangeWorkflowDock(main,_detailDock,_panelDock->objectName()+"/details",property("preferredPanelGroup").toString(),Qt::RightDockWidgetArea);
            if(!restored)main->resizeDocks({_detailDock.data()},{360},Qt::Horizontal);
            emit panelDetailChanged();
            }
      _detailDock->show();_detailDock->raise();
      }
void QmlPlugin::focusDetailPanel()
      {
      showDetailPanel(true);if (!_detailDock) return;
      _detailDock->activateWindow();
      if (_detailDock->widget()) _detailDock->widget()->setFocus(Qt::OtherFocusReason);
      }
QQuickItem* QmlPlugin::summaryPanelHost() const
      { return _summaryWindow ? _summaryWindow->contentItem() : nullptr; }
bool QmlPlugin::summaryPanelVisible() const
      { return _summaryDock && _summaryDock->isVisible(); }
bool QmlPlugin::summaryPanelFloating() const
      { return _summaryDock && _summaryDock->isFloating(); }
void QmlPlugin::showSummaryPanel(bool visible)
      {
      if (!visible) { if (_summaryDock) _summaryDock->hide(); return; }
      if (!_panelDock) return;
      auto main=qobject_cast<QMainWindow*>(_panelDock->parentWidget()); if (!main) return;
      if (!_summaryDock) {
            const auto title=property("summaryPanelTitle").toString();
            _summaryDock=new QDockWidget(title.isEmpty()?_panelDock->windowTitle()+tr(" · 摘要"):title,main);
            _summaryDock->setObjectName(_panelDock->objectName()+"/summary");
            _summaryWindow=new QQuickWindow();
            _summaryWindow->setColor(QApplication::palette().color(QPalette::Window));
            _summaryDock->setWidget(QWidget::createWindowContainer(_summaryWindow));
            _summaryDock->widget()->setMinimumHeight(28);
            auto grip=new PanelGrip(_summaryDock);
            grip->setToolTip(tr("拖动此处调整摘要位置"));
            _summaryDock->setTitleBarWidget(grip);
            installPanelWindowControls(_summaryDock);
            _summaryDock->installEventFilter(this);
            connect(_summaryDock,&QDockWidget::visibilityChanged,this,[this](bool){emit panelSummaryChanged();});
            connect(_summaryDock,&QDockWidget::topLevelChanged,this,[this](bool){emit panelSummaryChanged();});
            connect(this,&QObject::destroyed,_summaryDock,&QDockWidget::deleteLater);
            const bool restored=arrangeWorkflowDock(main,_summaryDock,_panelDock->objectName()+"/summary","harmonySummary",Qt::TopDockWidgetArea);
            if (!restored) main->resizeDocks({_summaryDock.data()},{32},Qt::Vertical);
            emit panelSummaryChanged();
            }
      _summaryDock->show();_summaryDock->raise();
      }
void QmlPlugin::setSummaryPanelFloating(bool floating)
      { if (_summaryDock) { _summaryDock->setFloating(floating); _summaryDock->show(); } }
bool QmlPlugin::panelFloating() const { return _panelDock && _panelDock->isFloating(); }
void QmlPlugin::setPanelFloating(bool floating)
      { if (_panelDock) { _panelDock->setFloating(floating); _panelDock->show(); } }

}
