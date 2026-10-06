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
#include <QDockWidget>
#include <QMainWindow>
#include <QTimer>
#include <QQuickWindow>
#include <QApplication>
#include <QCloseEvent>

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
      auto location = [this](Qt::DockWidgetArea area) {
            _panelPlacement = area == Qt::TopDockWidgetArea ? "top" :
                  area == Qt::BottomDockWidgetArea ? "bottom" :
                  area == Qt::LeftDockWidgetArea ? "left" : "right";
            emit panelDockChanged();
            const int preferred=property("preferredRibbonHeight").toInt();
            if (preferred > 0 && (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea))
                  QTimer::singleShot(0,this,[this,preferred]() {
                        if (!_panelDock || _panelDock->isFloating()) return;
                        if (auto main=qobject_cast<QMainWindow*>(_panelDock->parentWidget()))
                              main->resizeDocks({_panelDock.data()},{qBound(80,preferred,400)},Qt::Vertical);
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
            _detailDock=new QDockWidget(title,main);_detailDock->setObjectName("pluginDetailDock");
            _detailWindow=new QQuickWindow();_detailWindow->setObjectName("pluginDetailWindow");
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
            main->resizeDocks({_detailDock.data()},{360},Qt::Horizontal);
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
bool QmlPlugin::panelFloating() const { return _panelDock && _panelDock->isFloating(); }
void QmlPlugin::setPanelFloating(bool floating)
      { if (_panelDock) { _panelDock->setFloating(floating); _panelDock->show(); } }

}
