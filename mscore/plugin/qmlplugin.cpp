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
bool QmlPlugin::panelFloating() const { return _panelDock && _panelDock->isFloating(); }
void QmlPlugin::setPanelFloating(bool floating)
      { if (_panelDock) { _panelDock->setFloating(floating); _panelDock->show(); } }

}
