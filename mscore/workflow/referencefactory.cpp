// SPDX-License-Identifier: GPL-2.0-or-later
#include "referencefactory.h"
#include "../referenceaudio/mediafactory.h"
#include "../referenceaudio/spectrumfactory.h"
#include "../referenceaudio/projectfactory.h"
#include "referencebus.h"
#include "featuregate.h"
#include "workflowcontext.h"
#include "toolicons.h"
#include "../scoreview.h"
#include "../plugin/qmlpluginengine.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/system.h"
#include "libmscore/page.h"
#include <QToolButton>
#include <QTimer>
#include <QEvent>
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
 QHash<QObject*,bool> states;
 QList<QPointer<QObject>> sessions;
private slots:
 void audioChanged(){
  auto source=sender();if(!project||!source)return;const bool playing=source->property("playing").toBool(),starting=playing&&!states.value(source);states[source]=playing;
  if(starting&&!project->property("joint").toBool()&&project->property("audioStopsScore").toBool()&&seq&&seq->isPlaying()){seq->stopWait();seq->synchronizeStoppedGui();}

 }
public:
 ReferencePlaybackPolicy(QObject* media):QObject(qApp),project(media){connect(media,SIGNAL(changed()),this,SLOT(audioChanged()));}
 void watch(QObject* session){sessions.append(session);connect(session,SIGNAL(changed()),this,SLOT(audioChanged()));connect(session,&QObject::destroyed,this,[this,session]{states.remove(session);sessions.removeAll(nullptr);});bind();}
 void bind(){if(connectedSeq==seq)return;disconnect(musicConnection);connectedSeq=seq;if(seq)musicConnection=connect(seq,&Seq::started,this,[this]{
  if(!project||project->property("joint").toBool()||!project->property("scoreStopsAudio").toBool())return;
  if(project->property("playing").toBool())QMetaObject::invokeMethod(project,"stop");
  for(auto session:sessions)if(session&&session->property("playing").toBool())QMetaObject::invokeMethod(session,"pause");
 });}

};
// Small native buttons follow exact association starts. All playback stays in
// the shared reference project and its existing panel controller.
class ReferenceScoreControls : public QObject {
 Q_OBJECT
 QPointer<QObject> project;
 QPointer<ScoreView> view;
 QHash<QString,QPointer<QToolButton>> buttons;
 QTimer timer;
 bool eventFilter(QObject*,QEvent* event)override{if(event->type()==QEvent::Show||event->type()==QEvent::Resize)schedule();return false;}
 void bind(){
  auto next=mscore?mscore->currentScoreView():nullptr;if(view==next)return;
  if(view){view->removeEventFilter(this);disconnect(view,nullptr,this,nullptr);}
  for(auto button:buttons)if(button)button->deleteLater();buttons.clear();view=next;
  if(view){view->installEventFilter(this);connect(view,&ScoreView::viewRectChanged,this,[this]{schedule();});connect(view,&ScoreView::scaleChanged,this,[this](double){schedule();});}
 }
 void trigger(const QVariantMap& row){if(project)QMetaObject::invokeMethod(project,"requestAssociation",Q_ARG(QString,row.value("trackId").toString()),Q_ARG(QString,row.value("id").toString()));}
public slots:
 void schedule(){if(!timer.isActive())timer.start();}
 void refresh(){
  bind();QSet<QString> shown;
  if(view&&view->score()&&view->isVisible()&&project&&project->property("scoreControls").toBool()&&workflowContext()->referenceCurrent(view->score())){
   auto score=view->score();QMap<QString,QVariantList> groups;
   for(auto trackValue:project->property("tracks").toList()){auto track=trackValue.toMap();for(auto value:track.value("associations").toList()){
    auto row=value.toMap();if(row.value("stale").toBool())continue;row["trackId"]=track.value("id");row["trackName"]=track.value("name");
    groups[QString("%1:%2").arg(row.value("start").toInt()).arg(row.value("firstTrack").toInt())].append(row);
   }}
   QVector<QRect> occupied;const auto markers=view->notePreviewMarkerBoxes();
   for(auto it=groups.cbegin();it!=groups.cend();++it){auto row=it->front().toMap();const int tick=row.value("start").toInt(),staff=row.value("firstTrack").toInt()/VOICES;
    auto measure=score->tick2measure(Fraction::fromTicks(tick));if(!measure||!measure->system()||staff<0||staff>=score->nstaves())continue;
    auto segment=score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);
    auto system=measure->system();const QPointF anchor(segment?segment->canvasPos().x():measure->canvasPos().x(),system->staffYpage(staff)+system->page()->pos().y());
    auto where=view->toPhysical(anchor).toPoint();if(!view->rect().adjusted(-80,-60,80,60).contains(where))continue;
    auto button=buttons.value(it.key());if(!button){button=new QToolButton(view);button->setObjectName("referenceScoreClip/"+it.key());button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);button->setAutoRaise(false);buttons[it.key()]=button;
     connect(button,&QToolButton::clicked,this,[this,button]{auto rows=button->property("referenceClips").toList();if(rows.size()==1){trigger(rows.front().toMap());return;}QMenu menu(button);for(auto value:rows){auto clip=value.toMap();auto action=menu.addAction(clip.value("trackName").toString()+" · "+clip.value("name").toString());connect(action,&QAction::triggered,this,[this,clip]{trigger(clip);});}menu.exec(button->mapToGlobal(QPoint(0,button->height())));});
    }
    button->setProperty("referenceClips",*it);bool playing=false;for(auto value:*it)playing|=value.toMap().value("id").toString()==project->property("activeAssociation").toString();
    button->setText(playing?tr("停止"):tr("原曲"));setComponentToolIcon(button,playing?"stop":"play");button->setToolTip(row.value("trackName").toString()+" · "+row.value("name").toString());button->adjustSize();
    QRect box(where+QPoint(0,-button->height()-12),button->size());
    for(int lane=0;lane<6;++lane){bool overlap=false;for(const auto& other:occupied)overlap|=other.intersects(box);for(const auto& other:markers)overlap|=view->matrix().mapRect(other).intersects(box);if(!overlap)break;box.translate(0,-button->height()-3);}
    occupied.append(box);button->move(qBound(0,box.x(),qMax(0,view->width()-button->width())),qBound(0,box.y(),qMax(0,view->height()-button->height())));button->show();button->raise();shown.insert(it.key());
   }
  }
  for(auto it=buttons.cbegin();it!=buttons.cend();++it)if(it.value()&&!shown.contains(it.key()))it.value()->hide();
 }
public:
 explicit ReferenceScoreControls(QObject* value):QObject(qApp),project(value){timer.setSingleShot(true);timer.setInterval(40);connect(&timer,&QTimer::timeout,this,&ReferenceScoreControls::refresh);connect(value,SIGNAL(tracksChanged()),this,SLOT(schedule()));connect(value,SIGNAL(scoreControlsChanged()),this,SLOT(schedule()));connect(value,SIGNAL(activeAssociationChanged()),this,SLOT(schedule()));connect(workflowContext(),&WorkflowContext::activeScoreChanged,this,&ReferenceScoreControls::schedule);connect(workflowContext(),&WorkflowContext::changed,this,&ReferenceScoreControls::schedule);if(mscore)connect(mscore->getPluginEngine(),&QmlPluginEngine::endCmd,this,&ReferenceScoreControls::schedule);schedule();}
};
QPointer<ReferencePlaybackPolicy> playbackPolicy;

}
QObject* createReferenceMedia(QObject* parent) {
 if(!personalFeaturesEnabled())return new MissingMedia(parent,QObject::tr("个人扩展功能已关闭。"));
 auto loader=module();
 auto factory=qobject_cast<KumoMediaFactory*>(loader->instance());
 if(!factory)return new MissingMedia(parent,loader->errorString());
 auto media=factory->create(parent);referenceProject();if(playbackPolicy)playbackPolicy->watch(media);return media;
}
QObject* createReferenceSpectrum(QObject* parent){
 if(!personalFeaturesEnabled())return new MissingMedia(parent,QObject::tr("个人扩展功能已关闭。"));
 auto loader=module();auto factory=qobject_cast<KumoSpectrumFactory*>(loader->instance());return factory?factory->createSpectrum(parent):new MissingMedia(parent,QObject::tr("请将原曲音频组件与程序一起更新。"));
}
QObject* referenceProject(){
 static QPointer<QObject> project;if(project){if(playbackPolicy)playbackPolicy->bind();return project;}
 if(!personalFeaturesEnabled()){project=new MissingMedia(qApp,QObject::tr("个人扩展功能已关闭。"));return project;}
 auto factory=qobject_cast<KumoMediaProjectFactory*>(module()->instance());
 project=factory?factory->createProject(qApp,referenceAudioHost()):new MissingMedia(qApp,module()->errorString());if(project->property("available").toBool()){playbackPolicy=new ReferencePlaybackPolicy(project);playbackPolicy->bind();new ReferenceScoreControls(project);}return project;
}
void addReferenceActions(QMenu* menu){
 for(auto item:QList<QPair<QString,QString>>{{"reference-audio-play","toggleIndependent"},{"reference-joint-play","requestJoint"}}){auto action=getAction(item.first.toLatin1().constData());markPersonalAction(action);menu->addAction(action);QObject::connect(action,&QAction::triggered,qApp,[item]{auto project=referenceProject();if(project->property("available").toBool())QMetaObject::invokeMethod(project,item.second.toLatin1().constData());});}
}
}
#include "referencefactory.moc"
