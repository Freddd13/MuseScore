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
#include "libmscore/tempotext.h"
#include "libmscore/tempo.h"
#include "libmscore/audio.h"
#include "libmscore/excerpt.h"
#include "libmscore/image.h"
#include "libmscore/part.h"
#include "mscore/performanceeditor/parameteredit.h"
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
 void guardedExpressionUndoAndStale(){
  auto s=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(s);mscore->setCurrentScoreView(mscore->appendScore(s));PluginAPI::Score wrap(s);PluginAPI::ScoreObserver o;o.setScore(&wrap);o.workflowInfo();
  auto scope=o.analysisScope(true);auto page=o.performanceRange(0,scope["end"].toInt(),0,8,0,64,o.analysisRevision());QVariantMap n;
  for(auto value:page["notes"].toList()){auto row=value.toMap();if(!row["protected"].toBool()&&!row["grace"].toBool()){n=row;break;}}QVERIFY(!n.isEmpty());
  auto note=toChord(s->tick2segment(Fraction::fromTicks(n["tick"].toInt()),false,SegmentType::ChordRest)->element(n["track"].toInt()))->notes()[n["index"].toInt()];
  auto v=n;v["oldType"]=n["velocityType"];v["oldRaw"]=n["velocityRaw"];v["oldBase"]=n["velocityBase"];v["target"]=60;
  int tempoTick=480;double oldTempo=s->tempomap()->tempo(tempoTick)*60;
  QVariantMap candidate{{"revision",o.analysisRevision()},{"scope",scope},{"velocity",QVariantList{v}},{"tempos",QVariantList{QVariantMap{{"tick",tempoTick},{"oldBpm",oldTempo},{"bpm",oldTempo+10}}}},{"keepDuration",false}};
  int undo=s->undoStack()->getCurIdx(),oldRaw=note->veloOffset();
  auto bad=candidate;bad["revision"]="old";QVERIFY(o.applyExpression(bad).contains("error"));QCOMPARE(s->undoStack()->getCurIdx(),undo);
  auto applied=o.applyExpression(candidate);QVERIFY2(applied["ok"].toBool(),qPrintable(applied["error"].toString()));QCOMPARE(s->undoStack()->getCurIdx(),undo+1);QVERIFY(note->veloOffset()!=oldRaw);QVERIFY(std::abs(s->tempomap()->tempo(tempoTick)*60-oldTempo-10)<.01);
  EditData ed;s->undoRedo(true,&ed);QCOMPARE(note->veloOffset(),oldRaw);QVERIFY(std::abs(s->tempomap()->tempo(tempoTick)*60-oldTempo)<.01);
  s->undoRedo(false,&ed);QVERIFY(note->veloOffset()!=oldRaw);
  QVERIFY(o.applyExpression(candidate).contains("error"));int after=s->undoStack()->getCurIdx();auto protectedCandidate=candidate;protectedCandidate["revision"]=o.analysisRevision();QVERIFY(o.applyExpression(protectedCandidate).contains("error"));QCOMPARE(s->undoStack()->getCurIdx(),after);
 }
 void copyPreservesOriginalAndTempoVisibility(){
  auto s=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(s);mscore->setCurrentScoreView(mscore->appendScore(s));PluginAPI::Score wrap(s);PluginAPI::ScoreObserver o;o.setScore(&wrap);
  const auto path=s->fileInfo()->filePath();bool dirty=s->dirty();int undo=s->undoStack()->getCurIdx();auto state=s->state();
  auto copy=settings.filePath("isolated.mscz");auto r=o.copyWorkflowScore(copy,o.analysisRevision(),false);QVERIFY2(r["ok"].toBool(),qPrintable(r["error"].toString()));
  QCOMPARE(s->fileInfo()->filePath(),path);QCOMPARE(s->dirty(),dirty);QCOMPARE(s->undoStack()->getCurIdx(),undo);QVERIFY(s->state()==state);
  QVERIFY(o.copyWorkflowScore(path,o.analysisRevision(),true).contains("error"));QVERIFY(o.copyWorkflowScore(copy,o.analysisRevision(),false).contains("error"));
  std::unique_ptr<MasterScore> clone(mscore->readScore(copy));QVERIFY(clone);QCOMPARE(clone->nstaves(),s->nstaves());QCOMPARE(clone->nmeasures(),s->nmeasures());
  QString error;QVERIFY(ParameterEdit::writeTapTempo(s,480,137.5,false,&error));auto segment=s->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);TempoText* mark=nullptr;for(auto e:segment->annotations())if(e->isTempoText())mark=toTempoText(e);QVERIFY(mark);QVERIFY(mark->visible());
  QVERIFY(!ParameterEdit::writeTapTempo(s,480,140,false,&error));QVERIFY(ParameterEdit::writeTapTempo(s,480,140,true,&error));QVERIFY(mark->visible());QCOMPARE(mark->tempo(),140./60);
 }
 void copyResourcesAndManualPlayback(){
  auto s=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(s);mscore->setCurrentScoreView(mscore->appendScore(s));
  auto segment=s->firstMeasure()->first(SegmentType::ChordRest);Note* note=nullptr;for(int t=0;t<s->ntracks()&&!note;++t){auto e=segment->element(t);if(e&&e->isChord())note=toChord(e)->notes().front();}QVERIFY(note);
  note->setVeloType(Note::ValueType::USER_VAL);note->setVeloOffset(77);note->chord()->setPlayEventType(PlayEventType::User);NoteEventList events;events.clear();events.append(NoteEvent(0,-15,820));note->setPlayEvents(events);
  s->startCmd();s->insertMeasure(ElementType::VBOX,s->first());s->endCmd();s->startCmd();auto part=new Score(s);auto ex=new Excerpt(s);ex->setPartScore(part);ex->setParts({s->parts().front()});ex->setTitle("Linked piano");Excerpt::createExcerpt(ex);s->excerpts().append(ex);s->endCmd();
  auto image=new Image(s);QImage pixel(8,8,QImage::Format_ARGB32);pixel.fill(Qt::darkGray);QByteArray png;QBuffer imageBytes(&png);imageBytes.open(QIODevice::WriteOnly);QVERIFY(pixel.save(&imageBytes,"PNG"));QVERIFY(image->loadFromData("inline.png",png));image->setParent(s->first());image->setTrack(0);s->first()->add(image);
  auto audio=new Audio();audio->setPath("input.wav");audio->setData(QByteArray("RIFF-resource-test"));s->setAudio(audio);
  PluginAPI::Score w(s);PluginAPI::ScoreObserver o;o.setScore(&w);auto path=settings.filePath("resources.mscz");auto copied=o.copyWorkflowScore(path,o.analysisRevision());QVERIFY2(copied["ok"].toBool(),qPrintable(copied["error"].toString()));
  std::unique_ptr<MasterScore> clone(mscore->readScore(path));QVERIFY(clone);QCOMPARE(clone->excerpts().size(),s->excerpts().size());QVERIFY(clone->audio());QCOMPARE(clone->audio()->data(),audio->data());
  auto cn=toChord(clone->tick2segment(segment->tick(),false,SegmentType::ChordRest)->element(note->track()))->notes().front();QCOMPARE(cn->veloOffset(),77);QCOMPARE(cn->veloType(),Note::ValueType::USER_VAL);QCOMPARE(cn->playEvents().front().ontime(),-15);QCOMPARE(cn->playEvents().front().len(),820);QCOMPARE(cn->chord()->playEventType(),PlayEventType::User);
  QVERIFY(cn->links());QVERIFY(cn->links()!=note->links());
  bool imageFound=false;for(auto e:clone->first()->el())if(e->isImage()){QVERIFY(toImage(e)->isValid());imageFound=true;}QVERIFY(imageFound);
 }
 void previewIsolationAndDurationGuard(){
  auto s=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(s);mscore->setCurrentScoreView(mscore->appendScore(s));PluginAPI::Score w(s);PluginAPI::ScoreObserver o;o.setScore(&w);auto scope=o.analysisScope(true);auto rev=o.analysisRevision();
  QVariantMap bad{{"revision","stale"}};int tabs=mscore->scores().size();QVERIFY(o.previewExpression(settings.filePath("workspace.json"),bad,scope).contains("error"));QCOMPARE(mscore->scores().size(),tabs);
  auto preview=o.previewExpression(settings.filePath("workspace.json"),{},scope);QVERIFY2(preview["ok"].toBool(),qPrintable(preview["error"].toString()));QVERIFY(mscore->currentScore()!=s);QCOMPARE(o.analysisRevision(),rev);QVERIFY(o.returnWorkflowSource()["ok"].toBool());QCOMPARE(mscore->currentScore(),static_cast<Score*>(s));
  double old=s->tempomap()->tempo(480)*60;QVariantMap c{{"revision",rev},{"scope",scope},{"tempos",QVariantList{QVariantMap{{"tick",480},{"oldBpm",old},{"bpm",old*2}}}},{"keepDuration",true}};int undo=s->undoStack()->getCurIdx();QVERIFY(o.applyExpression(c).contains("error"));QCOMPARE(s->undoStack()->getCurIdx(),undo);
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
