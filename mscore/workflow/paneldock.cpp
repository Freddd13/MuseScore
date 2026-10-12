// SPDX-License-Identifier: GPL-2.0-or-later
#include "paneldock.h"
#include <QMainWindow>
#include <QDockWidget>
#include <QSettings>
#include <QPointer>
#include <QTimer>
#include <QEvent>
namespace Ms { namespace {
class PanelPosition : public QObject {
      QPointer<QMainWindow> main;
      QPointer<QDockWidget> dock;
      QString key;
      QTimer timer;
      bool ready=false;
      void save() {
            if(!ready||!main||!dock)return;
            QSettings settings;settings.beginGroup(key);
            settings.setValue("area",int(main->dockWidgetArea(dock)));
            settings.setValue("floating",dock->isFloating());
            settings.setValue("geometry",dock->saveGeometry());
            QString neighbour;for(auto other:main->tabifiedDockWidgets(dock))if(!other->objectName().isEmpty()){neighbour=other->objectName();break;}
            settings.setValue("neighbour",neighbour);
            }
      bool eventFilter(QObject*,QEvent* event) override {
            if(event->type()==QEvent::Close){timer.stop();save();}
            else if(ready&&(event->type()==QEvent::Move||event->type()==QEvent::Resize))timer.start();
            return false;
            }
   public:
      PanelPosition(QMainWindow* m,QDockWidget* d,QString k):QObject(d),main(m),dock(d),key(k) {
            timer.setSingleShot(true);timer.setInterval(250);connect(&timer,&QTimer::timeout,this,[this]{save();});
            d->installEventFilter(this);
            connect(d,&QDockWidget::dockLocationChanged,this,[this](Qt::DockWidgetArea){if(ready)timer.start();});
            connect(d,&QDockWidget::topLevelChanged,this,[this](bool){if(ready)timer.start();});
            QTimer::singleShot(0,this,[this]{ready=true;});
            }
      };
}
bool arrangeWorkflowDock(QMainWindow* main,QDockWidget* dock,const QString& id,const QString& group,Qt::DockWidgetArea requested) {
      if(!main||!dock||group.isEmpty())return false;
      const QString name=id.startsWith("kumoPanel/")?id.mid(10):id;
      dock->setObjectName("kumoPanel/"+name);
      const QString key="kumoPanels/"+name;QSettings settings;settings.beginGroup(key);
      const bool saved=settings.contains("area");int raw=settings.value("area",int(requested)).toInt();
      auto area=(raw==Qt::LeftDockWidgetArea||raw==Qt::RightDockWidgetArea||raw==Qt::TopDockWidgetArea||raw==Qt::BottomDockWidgetArea)?Qt::DockWidgetArea(raw):requested;
      main->addDockWidget(area,dock);
      QDockWidget* neighbour=nullptr;
      const auto docks=main->findChildren<QDockWidget*>();
      if(saved){
            const QString name=settings.value("neighbour").toString();
            for(auto other:docks)if(other!=dock&&other->objectName()==name&&!other->isFloating()&&main->dockWidgetArea(other)==area){neighbour=other;break;}
            }
      else {
            const QString preferred=group=="bottomTools"?"performanceEditorDock":"inspector";
            for(auto other:docks)if(other!=dock&&!other->isFloating()&&main->dockWidgetArea(other)==area&&other->objectName()==preferred){neighbour=other;break;}
            if(!neighbour)for(auto other:docks)if(other!=dock&&!other->isFloating()&&main->dockWidgetArea(other)==area&&other->property("kumoPanelGroup").toString()==group){neighbour=other;break;}
            }
      dock->setProperty("kumoPanelGroup",group);
      if(neighbour)main->tabifyDockWidget(neighbour,dock);
      if(saved&&settings.value("floating",false).toBool()){dock->setFloating(true);dock->restoreGeometry(settings.value("geometry").toByteArray());}
      new PanelPosition(main,dock,key);
      return saved||neighbour;
      }
}
