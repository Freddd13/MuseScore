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
#include "mscore/workflow/paneldock.h"
#include <QDockWidget>
#include <QMainWindow>
#include <QTimer>
#include <QStyle>
#include <QQuickWindow>
#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>

#include "libmscore/musescoreCore.h"

namespace Ms {

//---------------------------------------------------------
//   QmlPlugin
//---------------------------------------------------------

QmlPlugin::QmlPlugin(QQuickItem* parent)
   : QQuickItem(parent)
      {}

void QmlPlugin::attachPanelDock(QDockWidget* dock)
      {
      _panelDock = dock;
      auto watchKeys = [this](QQuickWindow* window) { if (window) window->installEventFilter(this); };
      connect(this, &QQuickItem::windowChanged, this, watchKeys);
      watchKeys(window());
      auto location = [this](Qt::DockWidgetArea area) {
            _panelPlacement = area == Qt::TopDockWidgetArea ? "top" :
                  area == Qt::BottomDockWidgetArea ? "bottom" :
                  area == Qt::LeftDockWidgetArea ? "left" : "right";
            emit panelDockChanged();
            const int preferred=property("preferredRibbonHeight").toInt();
            const bool compact=preferred>0 && preferred<=44 && (area==Qt::TopDockWidgetArea || area==Qt::BottomDockWidgetArea);
            if(compact && !_panelDock->property("compactPluginTitle").toBool()) {
                  auto grip=new QWidget(_panelDock);grip->setFixedHeight(4);grip->setToolTip(tr("拖动此处调整面板位置"));
                  _panelDock->setTitleBarWidget(grip);_panelDock->setProperty("compactPluginTitle",true);
                  }
            else if(!compact && _panelDock->property("compactPluginTitle").toBool()) {
                  auto old=_panelDock->titleBarWidget();_panelDock->setTitleBarWidget(nullptr);if(old)old->deleteLater();
                  _panelDock->setProperty("compactPluginTitle",false);
                  }
            if (preferred > 0 && (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea))
                  QTimer::singleShot(0,this,[this,preferred]() {
                        if (!_panelDock || _panelDock->isFloating()) return;
                        if (auto main=qobject_cast<QMainWindow*>(_panelDock->parentWidget()))
                              main->resizeDocks({_panelDock.data()},{qBound(24,preferred,400)+(preferred>44 && property("ribbonContentHeight").toBool()?_panelDock->style()->pixelMetric(QStyle::PM_TitleBarHeight):0)},Qt::Vertical);
                        });
            };
      connect(dock, &QDockWidget::dockLocationChanged, this, location);
      connect(dock, &QDockWidget::topLevelChanged, this, [this](bool) { emit panelDockChanged(); });
      if (auto main = qobject_cast<QMainWindow*>(dock->parentWidget())) location(main->dockWidgetArea(dock));
      }
void QmlPlugin::focusPanel()
      {
      if (!_panelDock) return;
      _panelDock->show();_panelDock->raise();_panelDock->activateWindow();
      if (_panelDock->widget()) _panelDock->widget()->setFocus(Qt::OtherFocusReason);
      }
QQuickItem* QmlPlugin::detailPanelHost() const
      { return _detailWindow ? _detailWindow->contentItem() : nullptr; }
bool QmlPlugin::detailPanelVisible() const
      { return _detailDock && _detailDock->isVisible(); }
bool QmlPlugin::eventFilter(QObject* object, QEvent* event)
      {
      if (object==_detailDock && event->type()==QEvent::Close) emit panelDetailClosed();
      if (object==_summaryDock && event->type()==QEvent::Close) emit panelSummaryClosed();
      if (event->type()==QEvent::ShortcutOverride || event->type()==QEvent::KeyPress) {
            auto keys=static_cast<QKeyEvent*>(event);
            auto quick=qobject_cast<QQuickWindow*>(object);
            const QByteArray handler=property("panelSpaceHandler").toString().toUtf8();
            QQuickItem* focus=quick ? quick->activeFocusItem() : nullptr;
            bool editingText=false;
            for (auto item=focus; item; item=item->parentItem())
                  if (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit")) { editingText=true; break; }
            if (quick && focus && !editingText && !handler.isEmpty() && keys->key()==Qt::Key_Space && keys->modifiers()==Qt::NoModifier) {
                  if (event->type()==QEvent::KeyPress && !keys->isAutoRepeat())
                        QMetaObject::invokeMethod(this,handler.constData(),Qt::DirectConnection);
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
            _summaryDock=new QDockWidget(tr("和弦摘要"),main);
            _summaryDock->setObjectName(_panelDock->objectName()+"/summary");
            _summaryWindow=new QQuickWindow();
            _summaryWindow->setColor(QApplication::palette().color(QPalette::Window));
            _summaryDock->setWidget(QWidget::createWindowContainer(_summaryWindow));
            _summaryDock->widget()->setMinimumHeight(28);
            auto grip=new QWidget(_summaryDock);grip->setFixedHeight(4);
            grip->setToolTip(tr("拖动此处调整摘要位置"));
            _summaryDock->setTitleBarWidget(grip);
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
