// SPDX-License-Identifier: GPL-2.0-or-later
#include "referencefactory.h"
#include "../referenceaudio/mediafactory.h"
#include "../referenceaudio/spectrumfactory.h"
#include "../referenceaudio/projectfactory.h"
#include "referencebus.h"
#include "featuregate.h"
#include <QPointer>
#include <QCoreApplication>
#include <QPluginLoader>
#include <QDir>
#include <QMenu>
#include <QAction>
#include "../shortcut.h"
#include "../musescore.h"
#include "../seq.h"
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
class ReferencePlaybackPolicy : public QObject {
 Q_OBJECT
 QPointer<QObject> project;
 QPointer<Seq> connectedSeq;
 QMetaObject::Connection musicConnection;
 bool audioWasPlaying=false;
private slots:
 void audioChanged(){
  if(!project)return;const bool playing=project->property("playing").toBool(),starting=playing&&!audioWasPlaying;audioWasPlaying=playing;
  if(starting&&!project->property("joint").toBool()&&project->property("audioStopsScore").toBool()&&seq&&seq->isPlaying()){seq->stopWait();seq->synchronizeStoppedGui();}
 }
public:
 ReferencePlaybackPolicy(QObject* media):QObject(qApp),project(media){connect(media,SIGNAL(changed()),this,SLOT(audioChanged()));}
 void bind(){if(connectedSeq==seq)return;disconnect(musicConnection);connectedSeq=seq;if(seq)musicConnection=connect(seq,&Seq::started,this,[this]{if(project&&project->property("playing").toBool()&&!project->property("joint").toBool()&&project->property("scoreStopsAudio").toBool())QMetaObject::invokeMethod(project,"stop");});}
};

}
QObject* createReferenceMedia(QObject* parent) {
 if(!personalFeaturesEnabled())return new MissingMedia(parent,QObject::tr("个人扩展功能已关闭。"));
 auto loader=module();
 auto factory=qobject_cast<KumoMediaFactory*>(loader->instance());
 return factory?factory->create(parent):new MissingMedia(parent,loader->errorString());
}
QObject* createReferenceSpectrum(QObject* parent){
 if(!personalFeaturesEnabled())return new MissingMedia(parent,QObject::tr("个人扩展功能已关闭。"));
 auto loader=module();auto factory=qobject_cast<KumoSpectrumFactory*>(loader->instance());return factory?factory->createSpectrum(parent):new MissingMedia(parent,QObject::tr("请将原曲音频组件与程序一起更新。"));
}
QObject* referenceProject(){
 static QPointer<QObject> project;static QPointer<ReferencePlaybackPolicy> policy;if(project){if(policy)policy->bind();return project;}
 if(!personalFeaturesEnabled()){project=new MissingMedia(qApp,QObject::tr("个人扩展功能已关闭。"));return project;}
 auto factory=qobject_cast<KumoMediaProjectFactory*>(module()->instance());
 project=factory?factory->createProject(qApp,referenceAudioHost()):new MissingMedia(qApp,module()->errorString());if(project->property("available").toBool()){policy=new ReferencePlaybackPolicy(project);policy->bind();}return project;
}
void addReferenceActions(QMenu* menu){
 for(auto item:QList<QPair<QString,QString>>{{"reference-audio-play","toggleIndependent"},{"reference-joint-play","requestJoint"}}){auto action=getAction(item.first.toLatin1().constData());markPersonalAction(action);menu->addAction(action);QObject::connect(action,&QAction::triggered,qApp,[item]{auto project=referenceProject();if(project->property("available").toBool())QMetaObject::invokeMethod(project,item.second.toLatin1().constData());});}
}
}
#include "referencefactory.moc"
