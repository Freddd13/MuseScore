// SPDX-License-Identifier: GPL-2.0-or-later
#include "referencefactory.h"
#include "../referenceaudio/mediafactory.h"
#include "../referenceaudio/spectrumfactory.h"
#include "../referenceaudio/projectfactory.h"
#include "referencebus.h"
#include <QPointer>
#include <QCoreApplication>
#include <QPluginLoader>
#include <QDir>
#include <QMenu>
#include <QAction>
#include "../shortcut.h"
#include "../musescore.h"
namespace Ms {
namespace {
QPluginLoader* module(){
 static QPluginLoader* loader=nullptr;
 if(!loader){loader=new QPluginLoader(QDir(QCoreApplication::applicationDirPath()).filePath("kumo_referenceaudio"),qApp);loader->setLoadHints(QLibrary::PreventUnloadHint);}return loader;
}
class MissingMedia : public QObject {
 Q_OBJECT
 Q_PROPERTY(bool available READ available CONSTANT)
 Q_PROPERTY(QString status READ status CONSTANT)
 QString _error;
public:
 MissingMedia(QObject* p,QString error):QObject(p),_error(error){}
 bool available()const{return false;}
 QString status()const{return tr("原曲音频组件未就绪：%1").arg(_error);}
};
}
QObject* createReferenceMedia(QObject* parent) {
 auto loader=module();
 auto factory=qobject_cast<KumoMediaFactory*>(loader->instance());
 return factory?factory->create(parent):new MissingMedia(parent,loader->errorString());
}
QObject* createReferenceSpectrum(QObject* parent){
 auto loader=module();auto factory=qobject_cast<KumoSpectrumFactory*>(loader->instance());return factory?factory->createSpectrum(parent):new MissingMedia(parent,QObject::tr("请将原曲音频组件与程序一起更新。"));
}
QObject* referenceProject(){
 static QPointer<QObject> project;if(project)return project;
 auto factory=qobject_cast<KumoMediaProjectFactory*>(module()->instance());
 project=factory?factory->createProject(qApp,referenceAudioHost()):new MissingMedia(qApp,module()->errorString());return project;
}
void addReferenceActions(QMenu* menu){
 for(auto item:QList<QPair<QString,QString>>{{"reference-audio-play","toggleIndependent"},{"reference-joint-play","requestJoint"}}){auto action=getAction(item.first.toLatin1().constData());menu->addAction(action);QObject::connect(action,&QAction::triggered,qApp,[item]{auto project=referenceProject();if(project->property("available").toBool())QMetaObject::invokeMethod(project,item.second.toLatin1().constData());});}
}
}
#include "referencefactory.moc"
