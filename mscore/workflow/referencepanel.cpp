// GPL-2.0-or-later. Native dock lifecycle; the media module is loaded on demand.
#include "referencepanel.h"
#include "paneldock.h"
#include "featuregate.h"
#include "aiconnection.h"
#include "referencepaths.h"
#include "referencefactory.h"
#include "panelwindowcontrols.h"
#include "workspaceviews.h"
#include "libmscore/score.h"
#include "mscore/musescore.h"
#include "mscore/shortcut.h"
#include "mscore/plugin/qmlpluginengine.h"
#include "mscore/plugin/qmlplugin.h"
#include "mscore/plugin/api/score.h"
#include <QJSValue>
#include <QAction>
#include <QDockWidget>
#include <QQuickView>
#include <QPointer>
#include <QMenu>
#include <QApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalBlocker>
namespace Ms {namespace {
QPointer<QDockWidget> referenceDock;
QPointer<QmlPlugin> referenceRoot;
const QString referenceTag = QStringLiteral("kumo.reference");

bool panelOwnsScore(Score* score)
{
 if(!score||!referenceRoot)return false;
 const auto value=referenceRoot->property("owner");auto object=value.canConvert<QJSValue>()?value.value<QJSValue>().toQObject():value.value<QObject*>();auto owner=qobject_cast<PluginAPI::Score*>(object);
 return owner && owner->score() && owner->score()->masterScore()==score->masterScore();
}
}
void flushReferencePanelForScore(Score* score){
 if(!panelOwnsScore(score))return;
 QMetaObject::invokeMethod(referenceRoot,"storeTrack",Qt::DirectConnection);
 QMetaObject::invokeMethod(referenceRoot,"saveScoreProject",Qt::DirectConnection);
}

ReferenceScoreSave::ReferenceScoreSave(Score* score, const QString& targetPath)
{
    if (!score || targetPath.isEmpty())
        return;
    _master = score->masterScore();
    const auto& tags = _master->metaTags();
    _hadReference = tags.contains(referenceTag);
    _previous = tags.value(referenceTag);
    auto document = QJsonDocument::fromJson(_previous.toUtf8()).toVariant().toMap();
    if (panelOwnsScore(score)) {
        QVariant captured;
        if (QMetaObject::invokeMethod(referenceRoot, "referenceProjectSnapshot", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariant, captured))) {
            if (captured.userType() == qMetaTypeId<QJSValue>())
                captured = captured.value<QJSValue>().toVariant();
            if (!captured.toMap().isEmpty())
                document = captured.toMap();
        }
    }
    if (document.value("schema").toInt() != 2)
        return;
    const auto sourcePath = _master->created() ? QString() : _master->fileInfo()->absoluteFilePath();
    const auto serialized = QJsonDocument::fromVariant(
        referenceDocumentForSave(document, sourcePath, targetPath)).toJson(QJsonDocument::Compact);
    if (serialized.size() > 4 * 1024 * 1024)
        return;
    _master->metaTags().insert(referenceTag, QString::fromUtf8(serialized));
    _staged = true;
}

ReferenceScoreSave::~ReferenceScoreSave()
{
    if (!_staged)
        return;
    if (_hadReference)
        _master->metaTags().insert(referenceTag, _previous);
    else
        _master->metaTags().remove(referenceTag);
}

void ReferenceScoreSave::accept()
{
    if (!_staged)
        return;
    _staged = false;
    if (panelOwnsScore(_master))
        QMetaObject::invokeMethod(referenceRoot, "rememberSavedReference", Qt::DirectConnection);
}

void showReferencePanel(bool visible){
 if(!mscore || !personalFeaturesEnabled())return;
 if(referenceDock&&referenceRoot){referenceDock->setVisible(visible);if(visible)referenceRoot->focusPanel();return;}
 if(!visible)return;
 auto engine=new QmlPluginEngine;auto view=new QQuickView(engine,nullptr);engine->setParent(view);
 const auto file=builtinPanelPath("ReferencePlayer");
 view->setResizeMode(QQuickView::SizeRootObjectToView);view->setSource(QUrl::fromLocalFile(file));
 auto root=qobject_cast<QmlPlugin*>(view->rootObject());if(!root){mscore->showMessage(QObject::tr("原曲参考界面读取失败，请检查安装目录中的 ReferencePlayer。"),8000);delete view;return;}
 auto dock=new QDockWidget(QObject::tr("原曲参考"),mscore);referenceDock=dock;referenceRoot=root;
 auto container=QWidget::createWindowContainer(view);
 container->setMinimumSize(0,0);container->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
 dock->setWidget(container);dock->setMinimumSize(270,96);
 addEditorDockActions(dock);
 const QPointer<QmlPlugin> panel = root;
 installPanelWindowControls(dock)->setTransport([] {
     auto project = referenceProject();
     if (!project || !project->property("playing").toBool())
         return false;
     const auto owner = project->property("workflowPlaybackOwner").toString();
     return owner.isEmpty() || owner == "reference-panel" || project->property("joint").toBool();
 }, [panel] {
     if (panel)
         QMetaObject::invokeMethod(panel.data(), "stopJoint");
 });
 const bool restored=arrangeWorkflowDock(mscore,dock,"ReferencePlayer_MS3","bottomTools",Qt::BottomDockWidgetArea);
 QObject::connect(root,&QObject::destroyed,dock,&QObject::deleteLater);
 root->attachPanelDock(dock);root->setFilePath(QFileInfo(file).absolutePath());
 QObject::connect(mscore->getPluginEngine(),&QmlPluginEngine::endCmd,root,&QmlPlugin::endCmd);
 QObject::connect(qApp,&QCoreApplication::aboutToQuit,root,[root]{root->disposePanelForReload();});
 auto action=getAction("reference-player");
 QObject::connect(dock,&QDockWidget::visibilityChanged,action,[action](bool shown){QSignalBlocker block(action);action->setChecked(shown);});
 QObject::connect(dock,&QObject::destroyed,action,[action]{QSignalBlocker block(action);action->setChecked(false);});
 view->setColor(QApplication::palette().color(QPalette::Window));dock->show();dock->raise();
 if(!restored)mscore->resizeDocks({dock},{350},Qt::Vertical);
 root->runPlugin();
}
void addReferencePanelAction(QMenu* menu){
 auto action=getAction("reference-player");action->setCheckable(true);markPersonalAction(action);menu->addAction(action);
 if(!action->property("referencePanelBound").toBool()){action->setProperty("referencePanelBound",true);QObject::connect(action,&QAction::triggered,qApp,[](bool checked){showReferencePanel(checked);});}
}
}
