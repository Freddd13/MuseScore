#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QSaveFile>
#include <QQuickView>
#include <QDockWidget>
#include "mscore/plugin/qmlplugin.h"
#include "mscore/plugin/pluginManager.h"
#include <QCryptographicHash>
#include "mtest/testutils.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "libmscore/measure.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/undo.h"
static void initP1Resources(){Q_INIT_RESOURCE(musescorefonts_Petaluma);}
using namespace Ms;
class TestP1Native:public QObject{
 Q_OBJECT
 QTemporaryDir settings;
private slots:
 void initTestCase(){
  QCoreApplication::setOrganizationName("KumoP1Regression");QCoreApplication::setApplicationName("P1");
  QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());dataPath=settings.path();
  MScore::noGui=true;noSeq=true;converterMode=true;initMuseScoreResources();initP1Resources();QStringList args;MuseScore::init(args);QVERIFY(mscore);
 }
 void compatibleSnapshot(){
  std::unique_ptr<MasterScore> s(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(s);PluginAPI::Score wrapped(s.get());PluginAPI::ScoreObserver o;o.setScore(&wrapped);
  auto info=o.workflowInfo();QVERIFY(!info.contains("error"));auto scope=o.analysisScope(true);auto rev=o.analysisRevision();
  auto page=o.performanceRange(0,scope["end"].toInt(),0,8,0,16,rev);QVERIFY(!page.contains("error"));
  QVERIFY(!page["notes"].toList().isEmpty());for(auto v:page["notes"].toList()){auto n=v.toMap();QVERIFY(n.contains("logicalId"));QVERIFY(n.contains("writtenTpc"));QVERIFY(n["velocity"].toInt()>0);QVERIFY(n.contains("tempoProtected"));}
  QVERIFY(o.performanceRange(0,100,0,8,0,16,"stale").contains("error"));
 }
 void realWorkflowPanels(){
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));mscore->resize(1200,850);mscore->show();
  for(QString name:{"ReferencePlayer","PerformanceExpression"}){
   QString path=QString(TESTROOT)+"/share/plugins/"+name+"/"+name+"_MS3.qml";PluginDescription description;description.path=path;QVERIFY2(collectPluginMetaInformation(&description),qPrintable(path));mscore->registerPlugin(&description);mscore->pluginTriggered(path);QTest::qWait(120);
   QmlPlugin* root=nullptr;
   for(auto w:QGuiApplication::allWindows()){auto view=qobject_cast<QQuickView*>(w);auto panel=view?qobject_cast<QmlPlugin*>(view->rootObject()):nullptr;if(panel&&panel->property("menuPath").toString().contains(name=="ReferencePlayer"?"Reference Player":"Performance Expression"))root=panel;}
   QVERIFY(root);QVERIFY(root->property("observer").value<QObject*>());root->setPanelFloating(true);QTest::qWait(30);QVERIFY(root->panelFloating());root->setPanelFloating(false);QTest::qWait(30);
   if(name=="ReferencePlayer")QVERIFY(root->property("media").value<QObject*>()->property("available").toBool());
   else QVERIFY(root->property("workerSource").toString().size()>1000);
  }
 }
 void rawMidiAtomicCopy(){
  PluginAPI::ScoreObserver o;auto source=settings.filePath("source.mid"),copy=settings.filePath("copy.mid");QFile f(source);QVERIFY(f.open(QIODevice::WriteOnly));QByteArray data=QByteArray::fromHex("4d546864000000060000000101e04d54726b0000000400ff2f00");f.write(data);f.close();
  auto hash=QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());
  QVERIFY(o.savePerformanceSource(source,copy,hash)["ok"].toBool());QVERIFY(o.savePerformanceSource(source,copy,hash)["ok"].toBool());QVERIFY(o.savePerformanceSource(source,copy,"changed").contains("error"));
  QFile g(copy);QVERIFY(g.open(QIODevice::ReadOnly));QCOMPARE(g.readAll(),data);
  QSignalSpy done(&o,&PluginAPI::ScoreObserver::performanceMidiReady);o.readPerformanceMidi(source);QTRY_COMPARE(done.count(),1);QVERIFY(!done[0][1].toMap().contains("error"));
 }
};
QTEST_MAIN(TestP1Native)
#include "tst_p1native.moc"
