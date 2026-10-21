// GPL-2.0-or-later. Native ownership with one shared view per tool.
#include "builtinpanels.h"
#include "systemlocks.h"
#include "mscore/performanceeditor/rollwriting.h"
#include "arrangement.h"
#include "paneldock.h"
#include "featuregate.h"
#include "aiconnection.h"
#include "workspaceviews.h"
#include "editorareas.h"
#include "mscore/workspace.h"
#include "mscore/musescore.h"
#include "mscore/shortcut.h"
#include "mscore/performanceeditor/performanceeditor.h"
#include "mscore/plugin/qmlpluginengine.h"
#include "mscore/plugin/qmlplugin.h"
#include <QQuickView>
#include <QQmlContext>
#include <QDockWidget>
#include <QPointer>
#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QVBoxLayout>
#include <QTimer>
#include <QSettings>
#include <QDynamicPropertyChangeEvent>
#include "mscore/scoreview.h"
namespace Ms {namespace {
QPointer<QDockWidget> harmonyDock;
QPointer<QmlPlugin> harmonyRoot;
class ViewPaletteBinding : public QObject {
 QPointer<QQmlContext> context;QPointer<QWidget> host;QPointer<PerformanceEditor> editor;QVariant lastPalette;bool lastEmbedded=false;
 void update(){
  if(!context||!host)return;
  const bool embedded=!host->property("kumoDetachedArea").toBool();
  const QVariant colors=embedded&&editor?editor->property("workflowPanelTheme"):QVariant();
  if(embedded!=lastEmbedded){lastEmbedded=embedded;context->setContextProperty("workflowEmbedded",embedded);}
  if(colors!=lastPalette){lastPalette=colors;context->setContextProperty("workflowPanelPalette",colors);}
 }
 bool eventFilter(QObject*,QEvent* event) override {
  if(event->type()==QEvent::PaletteChange||event->type()==QEvent::ParentChange||event->type()==QEvent::DynamicPropertyChange)update();
  return false;
 }
 public:ViewPaletteBinding(QQuickView* view,QWidget* owner):QObject(view),context(view->rootContext()),host(owner),editor(mscore?mscore->performanceEditor():nullptr){
  if(host)host->installEventFilter(this);if(editor)editor->installEventFilter(this);update();
 }
};
QQuickView* loadView(const QString& name,bool embedded,QWidget* owner){
 auto engine=new QmlPluginEngine;auto view=new QQuickView(engine,nullptr);engine->setParent(view);
 view->rootContext()->setContextProperty("workflowEmbedded",embedded);
 view->rootContext()->setContextProperty("workflowNativeTools",name=="PerformanceExpression");
 if(embedded&&owner)new ViewPaletteBinding(view,owner);
 const QString file=builtinPanelPath(name);
 view->setResizeMode(QQuickView::SizeRootObjectToView);view->setSource(QUrl::fromLocalFile(file));
 auto root=qobject_cast<QmlPlugin*>(view->rootObject());
 if(!root){if(mscore)mscore->showMessage(QObject::tr("界面读取失败：%1").arg(file),8000);delete view;return nullptr;}
 view->setMinimumSize(QSize(0,0));view->resize(embedded?QSize(700,180):QSize(360,520));
 root->setFilePath(QFileInfo(file).absolutePath());
 for(auto child:root->findChildren<QWindow*>())child->setTransientParent(view);
 QObject::connect(mscore->getPluginEngine(),&QmlPluginEngine::endCmd,root,&QmlPlugin::endCmd);
 QObject::connect(qApp,&QCoreApplication::aboutToQuit,root,[root]{root->disposePanelForReload();});
 view->setColor(QApplication::palette().color(QPalette::Window));
 if(owner)QObject::connect(owner,&QObject::destroyed,view,&QObject::deleteLater);
 return view;
}
}
void showHarmonyPanel(bool visible){
 if(!mscore || !personalFeaturesEnabled())return;
 if(harmonyDock&&harmonyRoot){harmonyDock->setVisible(visible);if(visible)harmonyRoot->focusPanel();return;}
 if(!visible)return;
 const QSize previousWindow=mscore->size();
 auto view=loadView("HarmonyAssistant",false,nullptr);if(!view)return;
 auto root=qobject_cast<QmlPlugin*>(view->rootObject());
 auto dock=new QDockWidget(QObject::tr("和弦助手"),mscore);harmonyDock=dock;harmonyRoot=root;
 auto container=QWidget::createWindowContainer(view);container->setMinimumSize(0,0);container->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
 dock->setWidget(container);dock->setMinimumSize(270,28);
 const bool restored=arrangeWorkflowDock(mscore,dock,"HarmonyAssistant_MS3","rightTools",Qt::RightDockWidgetArea);
 root->attachPanelDock(dock);
 auto action=getAction("harmony-assistant");
 QObject::connect(dock,&QDockWidget::visibilityChanged,action,[action](bool on){QSignalBlocker block(action);action->setChecked(on);});
 QObject::connect(root,&QObject::destroyed,dock,&QObject::deleteLater);
 dock->show();dock->raise();root->runPlugin();
 if(!restored)mscore->resizeDocks({dock},{360},Qt::Horizontal);
 if(mscore->size()!=previousWindow&&!mscore->isMaximized())mscore->resize(previousWindow);
 QTimer::singleShot(0,dock,[previousWindow]{if(mscore&&mscore->size()!=previousWindow&&!mscore->isMaximized())mscore->resize(previousWindow);});
}
void installExpressionTools(QWidget* editor,QSplitter* splitter,QVBoxLayout* layout){
 auto tabs=new QTabBar(editor);tabs->setObjectName("workflowExpressionTabs");tabs->setExpanding(false);
 tabs->addTab(QObject::tr("演奏编辑"));tabs->addTab(QObject::tr("演奏表达"));layout->insertWidget(0,tabs);
 QObject::connect(tabs,&QTabBar::currentChanged,splitter,[tabs,splitter]{const int previous=tabs->property("previousLayoutPage").toInt();QSettings().setValue("performanceEditor/modeLayout/"+QString::number(previous),splitter->saveState());});
 auto host=new QWidget(splitter);host->setObjectName("embeddedExpressionPage");host->setMinimumHeight(180);
 auto box=new QVBoxLayout(host);box->setContentsMargins(0,0,0,0);splitter->addWidget(host);host->hide();
 QObject::connect(tabs,&QTabBar::currentChanged,host,[host,box,splitter](int page){
  const QSize previousWindow=mscore?mscore->size():QSize();
  auto root=host->findChild<QmlPlugin*>("embeddedExpressionRoot");
  if(page==1&&!host->property("loaded").toBool()){
   if(auto view=loadView("PerformanceExpression",true,host)){
    auto content=qobject_cast<QmlPlugin*>(view->rootObject());content->setObjectName("embeddedExpressionRoot");
    view->setMinimumSize(QSize(0,0));view->resize(700,180);
    auto container=QWidget::createWindowContainer(view,host);container->setMinimumSize(0,0);container->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
    box->addWidget(container);host->setProperty("root",QVariant::fromValue(static_cast<QObject*>(content)));
    QObject::connect(content,&QObject::destroyed,host,[host,container]{host->setProperty("root",QVariant());host->setProperty("loaded",false);container->deleteLater();});
    content->runPlugin();host->setProperty("loaded",true);root=content;
   }
  }
  if(!root)root=qobject_cast<QmlPlugin*>(host->property("root").value<QObject*>());
  if(page==1&&showDetachedEditorArea(host))return;
  if(!host->property("kumoDetachedArea").toBool()){host->setVisible(page==1);if(root)root->setVisible(page==1);}
  if(root&&page==1){
   auto owner=host->parentWidget();while(owner&&!qobject_cast<PerformanceEditor*>(owner))owner=owner->parentWidget();
   if(!owner||!owner->property("kumoActivatingDetachedTool").toBool())
    QTimer::singleShot(0,root,[root]{if(root->isVisible())root->focusPanel();});
  }
  if(page==1){auto sizes=splitter->sizes();int at=splitter->indexOf(host);if(at>=0&&sizes[at]<100){sizes[at]=190;splitter->setSizes(sizes);}}
  const auto workspace=mscore?mscore->property("kumoWorkspaceView").toString():QString();
  if(page==1&&mscore)QTimer::singleShot(0,host,[host,previousWindow,workspace]{
   if(!mscore||!host->isVisible()||mscore->property("kumoWorkspaceView").toString()!=workspace)return;
   if(previousWindow.isValid()&&mscore->size()!=previousWindow&&!mscore->isMaximized())mscore->resize(previousWindow);
   auto owner=host->parentWidget();
   while(owner&&!qobject_cast<PerformanceEditor*>(owner))owner=owner->parentWidget();
   if(auto editor=qobject_cast<PerformanceEditor*>(owner))ensureEditorDockContent(editor);
  });

 });
}
void showExpressionPanel(){
 if(!mscore || !personalFeaturesEnabled())return;const QSize before=mscore->size();mscore->showPerformanceEditor(true);
 if(auto tabs=mscore->performanceEditor()->findChild<QTabBar*>("workflowExpressionTabs")){
  if(openEditorArea(mscore->performanceEditor(),"embeddedExpressionPage")){
   ensureEditorDockContent(mscore->performanceEditor());
   return;
  }
  auto host=mscore->performanceEditor()->findChild<QWidget*>("embeddedExpressionPage");if(host&&!host->property("loaded").toBool()&&tabs->currentIndex()==1)tabs->setCurrentIndex(0);tabs->setCurrentIndex(1);
 }
 if(mscore->size()!=before)mscore->resize(before);
 ensureEditorDockContent(mscore->performanceEditor());
}
void addBuiltinPanelActions(QMenu* menu){
 addSystemLockActions(menu);
 auto writing=rollWritingAction();menu->addAction(writing);Workspace::addActionAndString(writing,"kumo-roll-writing");
 auto spectrum = new QAction(QObject::tr("频谱扒谱"),menu->parentWidget());spectrum->setObjectName("kumo-spectrum");markPersonalAction(spectrum);
 spectrum->setIcon(personalToolIcon("kumo-spectrum"));menu->addAction(spectrum);Workspace::addActionAndString(spectrum,"kumo-spectrum");
 QObject::connect(spectrum,&QAction::triggered,qApp,[]{
  if(!personalFeaturesEnabled())return;mscore->showPerformanceEditor(true);
  openEditorArea(mscore->performanceEditor(),"transcriptionSpectrumPane");
 });
 addAIConnectionAction(menu);
 addSuggestionPanelAction(menu);
 auto harmony=getAction("harmony-assistant");harmony->setCheckable(true);markPersonalAction(harmony);menu->addAction(harmony);
 auto expression=getAction("performance-expression");markPersonalAction(expression);menu->addAction(expression);
}
}
