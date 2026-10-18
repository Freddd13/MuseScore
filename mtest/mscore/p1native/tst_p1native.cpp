#include "audio/exports/exportmidi.h"
#include "audio/exports/midigatefile.h"
#include "audio/midi/event.h"
#include "libmscore/audio.h"
#include "libmscore/chord.h"
#include "libmscore/excerpt.h"
#include "libmscore/fermata.h"
#include "libmscore/image.h"
#include "libmscore/instrument.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/pedal.h"
#include "libmscore/regionalvariants.h"
#include "libmscore/select.h"
#include "libmscore/slur.h"
#include "libmscore/spannermap.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/system.h"
#include "libmscore/tempo.h"
#include "libmscore/tempoexpression.h"
#include "libmscore/tempotext.h"
#include "libmscore/textline.h"
#include "libmscore/tie.h"
#include "libmscore/glissando.h"
#include "libmscore/undo.h"
#include "libmscore/velocitybackup.h"
#include "libmscore/xml.h"
#include "mscore/midicrop/midicroppanel.h"
#include "mscore/midicrop/gateaudition.h"
#include "mscore/preferences.h"
#include <QScopedValueRollback>
#include "mscore/musescore.h"
#include "mscore/palette.h"
#include "mscore/palette/palettetree.h"
#include "mscore/performanceeditor/parameteredit.h"
#include "mscore/performanceeditor/performanceeditor.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/plugin/pluginManager.h"
#include "mscore/plugin/qmlplugin.h"
#include "mscore/scoretab.h"
#include "mscore/scoreview.h"
#include "mscore/shortcut.h"
#include "mscore/taptempo/tapmark.h"
#include "mscore/taptempo/taptempo.h"
#include "mscore/temposymbols.h"
#include "mscore/workflow/midicapture.h"
#include "mscore/workflow/midiinput.h"
#include "mscore/workflow/paneldock.h"
#include "mscore/workflow/referencebus.h"
#include "mscore/workflow/regionalcontrols.h"
#include "mscore/workflow/regionalcomparison.h"
#include <QWheelEvent>
#include "mscore/workflow/regionalpanel.h"
#include "mscore/workflow/workflowcontext.h"
#include "mtest/testutils.h"
#include "thirdparty/qzip/qzipreader_p.h"
#include "thirdparty/qzip/qzipwriter_p.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDialog>
#include <QDockWidget>
#include <QDomDocument>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QQuickView>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtMath>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include "crashreport.inc"
static void initP1Resources(){Q_INIT_RESOURCE(musescorefonts_Petaluma);}
using namespace Ms;
static void floatWorkflowPanel(QmlPlugin* root){
 if(root->property("embeddedWorkflow").toBool()){
  auto editor=mscore->performanceEditor();
  if(root->property("withinEditor").toBool())editor->findChild<QAction*>("editorArea/float/embeddedExpressionPage")->trigger();
  if(auto dock=mscore->findChild<QDockWidget*>("kumoEditorArea/embeddedExpressionPage")){dock->resize(760,620);dock->show();QTest::qWait(100);}
 }
 else{root->setPanelFloating(true);root->window()->resize(520,800);}
}
class TestP1Native:public QObject{
 Q_OBJECT
 QTemporaryDir settings;
private slots:
 void cleanupTestCase(){
  QList<QPointer<QmlPlugin>> roots;for(auto w:QGuiApplication::allWindows())if(auto v=qobject_cast<QQuickView*>(w))if(auto p=qobject_cast<QmlPlugin*>(v->rootObject()))roots.append(p);
  for(auto p:roots)if(p)p->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  delete Ms::mscore;Ms::mscore=nullptr;
 }
 void initTestCase(){
  installTestCrashReport();
  QCoreApplication::setOrganizationName("KumoP1Regression");QCoreApplication::setApplicationName("P1");
  QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());dataPath=settings.path();
  MScore::noGui=true;noSeq=true;converterMode=true;initMuseScoreResources();initP1Resources();QStringList args;MuseScore::init(args);QVERIFY(mscore);
 }
#ifdef Q_MOC_RUN
 void playableTempoPaletteReadAndApply();
 void tapMarkPreviewCancelAndSave();
 void tapMarkRestoreAndRitProtection();
#else
#include "tempo_palette_tests.inc"
#include "tap_mark_tests.inc"
#endif
 void liveMatcherRecovery(){
  QVector<RealtimeMatcher::Group> groups;const QList<int> pitches{60,62,64,67,65,69,71,72};
  for(int i=0;i<pitches.size();++i)groups.append({i*480,i*.5,{pitches[i]}});
  RealtimeMatcher matcher;matcher.reset(groups,false);
  matcher.observe({60},0);QCOMPARE(matcher.tick(),0);matcher.observe({62},.5);matcher.observe({64},1.);
  matcher.observe({91},1.1);QCOMPARE(matcher.tick(),960);
  matcher.observe({65},2.);QCOMPARE(matcher.tick(),1920);matcher.observe({69},2.5);matcher.observe({71},3.);matcher.observe({72},3.5);QVERIFY(matcher.atEnd());
  matcher.reset(groups,true);matcher.observe({20},0);matcher.observe({88},.5);matcher.observe({42},1.);matcher.observe({10},2.);
  QCOMPARE(matcher.tick(),1920);matcher.observe({33},2.5);matcher.observe({70},3.);matcher.observe({19},3.5);QVERIFY(matcher.atEnd());
  groups.clear();for(int i=0;i<20000;++i)groups.append({i*120,i*.125,{48+i%24}});
  matcher.reset(groups,false);QElapsedTimer timer;timer.start();for(int i=0;i<1000;++i)matcher.observe(groups[i].pitches,i*.1);
  QCOMPARE(matcher.tick(),groups[999].tick);qInfo("Live matching 1000 onset groups, 20000 target groups: %lld ms",timer.elapsed());
 }
 void midiCaptureRecordingAndTiming(){
  MidiCaptureSession capture;QSignalSpy finished(&capture,&MidiCaptureSession::finished),failed(&capture,&MidiCaptureSession::failed);
  QVERIFY(!captureMidiInput(NPlayEvent(ME_NOTEON,0,60,80),midiInputTime()));
  QVERIFY(!capture.begin({{"monitor",false}},false).contains("error"));
  MidiCaptureSession other;QVERIFY(other.begin({},false).contains("error"));
  qint64 origin=midiInputTime();QTest::qWait(150);
  // Delivery is intentionally late. The recording uses arrival at the input FIFO.
  QVERIFY(captureMidiInput(NPlayEvent(ME_CONTROLLER,1,64,127),origin+10000000));
  QVERIFY(captureMidiInput(NPlayEvent(ME_NOTEON,1,60,93),origin+20000000));
  QVERIFY(captureMidiInput(NPlayEvent(ME_NOTEOFF,1,60,0),origin+90000000));
  QVERIFY(captureMidiInput(NPlayEvent(ME_CONTROLLER,1,64,0),origin+120000000));
  captureMidiInput(NPlayEvent(ME_NOTEON,2,67,71),midiInputTime()); // stop closes this held key
  capture.stop();QTRY_COMPARE(finished.count(),1);QCOMPARE(failed.count(),0);
  std::atomic_bool cancel(false);auto result=readPerformanceMidi(finished[0][0].toString(),cancel);
  QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));
  auto notes=result["notes"].toList();QCOMPARE(notes.size(),2);auto note=notes[0].toMap();
  QCOMPARE(note["pitch"].toInt(),60);QCOMPARE(note["velocity"].toInt(),93);QCOMPARE(note["channel"].toInt(),1);
  QVERIFY(note["seconds"].toDouble()<.001);
  QVERIFY(qAbs(note["endSeconds"].toDouble()-note["seconds"].toDouble()-.07)<.001);
  QVERIFY(!notes[1].toMap()["unreleased"].toBool());
  int pedalEvents=0;for(auto value:result["events"].toList()){auto e=value.toMap();if((e["type"].toInt()&240)==ME_CONTROLLER&&e["a"].toInt()==64)++pedalEvents;}
  QCOMPARE(pedalEvents,2);QVERIFY(!capture.state()["recording"].toBool());
  QVERIFY(!captureMidiInput(NPlayEvent(ME_NOTEON,0,60,80),midiInputTime()));
 }
 void midiCaptureFastOnsets(){
  QVariantList target;for(int i=0;i<3;++i)target.append(QVariantMap{{"tick",i*480},{"pitch",60+i*2},{"scoreSeconds",i*.04}});
  MidiCaptureSession capture;QVERIFY(!capture.begin({{"notes",target},{"monitor",false}},false).contains("error"));const auto start=midiInputTime();
  for(int i=0;i<3;++i){captureMidiInput(NPlayEvent(ME_NOTEON,0,60+i*2,80),start+i*40000000);captureMidiInput(NPlayEvent(ME_NOTEOFF,0,60+i*2,0),start+i*40000000+10000000);}
  QTRY_COMPARE(capture.state()["matchedTick"].toInt(),960);capture.cancel();
 }
 void midiCapturePositionAndSelection(){
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));mscore->showPerformanceEditor(true);
  PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto scope=observer.analysisScope(true);
  auto notes=observer.performanceRange(0,1920,0,8,0,32,observer.analysisRevision())["notes"].toList();QVERIFY(!notes.isEmpty());
  auto selected=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->notes()[0];score->select(selected);const auto selection=score->selection().elements();
  MidiCaptureSession capture;QVERIFY(!capture.begin({{"notes",notes},{"scope",scope},{"monitor",false}},false).contains("error"));
  QTest::qWait(100);QVERIFY(capture.state()["waitingForFirstNote"].toBool());QCOMPARE(capture.state()["seconds"].toDouble(),0.);
  for(int pitch:{36,52,55}){captureMidiInput(NPlayEvent(ME_NOTEON,0,pitch,80),midiInputTime());captureMidiInput(NPlayEvent(ME_NOTEOFF,0,pitch,0),midiInputTime());}
  QTRY_COMPARE(capture.state()["matchedTick"].toInt(),0);QTRY_COMPARE(workflowContext()->capture()["matchedTick"].toInt(),0);
  for(int pitch:{59,62,69}){captureMidiInput(NPlayEvent(ME_NOTEON,0,pitch,90),midiInputTime());captureMidiInput(NPlayEvent(ME_NOTEOFF,0,pitch,0),midiInputTime());}
  QTRY_COMPARE(capture.state()["matchedTick"].toInt(),480);QTRY_COMPARE(workflowContext()->capture()["matchedTick"].toInt(),480);
  QCOMPARE(score->selection().elements(),selection);QCOMPARE(score->masterScore()->playPos().ticks(),0);
  capture.cancel();QVERIFY(workflowContext()->capture().isEmpty());QCOMPARE(score->selection().elements(),selection);
 }
 void midiCaptureEndModesAndCancellation(){
  MidiCaptureSession capture;QSignalSpy finished(&capture,&MidiCaptureSession::finished),failed(&capture,&MidiCaptureSession::failed);
  QVERIFY(!capture.begin({{"endMode","duration"},{"duration",1}},false).contains("error"));
  QTest::qWait(1100);QVERIFY(capture.state()["recording"].toBool());QVERIFY(capture.state()["waitingForFirstNote"].toBool());
  captureMidiInput(NPlayEvent(ME_NOTEON,0,64,80),midiInputTime());captureMidiInput(NPlayEvent(ME_NOTEOFF,0,64,0),midiInputTime());
  QTRY_COMPARE(finished.count(),1);QVERIFY(finished[0][1].toString().contains(QString::fromUtf8("设定时长")));
  QVariantList target{QVariantMap{{"tick",0},{"pitch",60}},QVariantMap{{"tick",0},{"pitch",48}},QVariantMap{{"tick",480},{"pitch",64}},QVariantMap{{"tick",480},{"pitch",52}}};
  QVERIFY(!capture.begin({{"endMode","passage"},{"notes",target},{"role",1}},false).contains("error"));
  captureMidiInput(NPlayEvent(ME_NOTEON,0,60,80),midiInputTime());captureMidiInput(NPlayEvent(ME_NOTEOFF,0,60,0),midiInputTime());
  QTest::qWait(250);QVERIFY(capture.state()["recording"].toBool()); // a pause inside the passage
  captureMidiInput(NPlayEvent(ME_NOTEON,0,64,80),midiInputTime());captureMidiInput(NPlayEvent(ME_NOTEOFF,0,64,0),midiInputTime());
  QTRY_COMPARE(finished.count(),2);QVERIFY(finished[1][1].toString().contains(QString::fromUtf8("选段末尾")));
  QVERIFY(!capture.begin({},false).contains("error"));qint64 queued=midiInputTime();capture.cancel();
  QVERIFY(captureMidiInput(NPlayEvent(ME_NOTEON,0,72,90),queued));
  QVERIFY(!captureMidiInput(NPlayEvent(ME_NOTEON,0,72,90),midiInputTime()));QCOMPARE(finished.count(),2);
  QVERIFY(!capture.begin({},false).contains("error"));capture.stop();QTRY_COMPARE(failed.count(),1);
  QVERIFY(!capture.begin({{"duration",0}},false).isEmpty());
 }
 void midiCaptureScoreChange(){
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));
  MidiCaptureSession capture;QSignalSpy failed(&capture,&MidiCaptureSession::failed);
  QVERIFY(!capture.begin({},false).contains("error"));captureMidiInput(NPlayEvent(ME_NOTEON,0,60,80),midiInputTime());
  auto other=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(other);mscore->setCurrentScoreView(mscore->appendScore(other));
  QTRY_COMPARE(failed.count(),1);QVERIFY(!capture.state()["recording"].toBool());
  QVERIFY(!captureMidiInput(NPlayEvent(ME_NOTEOFF,0,60,0),midiInputTime()));
 }
 void compatibleSnapshot(){
  std::unique_ptr<MasterScore> s(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(s);PluginAPI::Score wrapped(s.get());PluginAPI::ScoreObserver o;o.setScore(&wrapped);
  auto info=o.workflowInfo();QVERIFY(!info.contains("error"));auto scope=o.analysisScope(true);auto rev=o.analysisRevision();
  auto page=o.performanceRange(0,scope["end"].toInt(),0,8,0,16,rev);QVERIFY(!page.contains("error"));
  QVERIFY(!page["notes"].toList().isEmpty());for(auto v:page["notes"].toList()){auto n=v.toMap();QVERIFY(n.contains("logicalId"));QVERIFY(n.contains("writtenTpc"));QVERIFY(n["velocity"].toInt()>0);QVERIFY(n.contains("tempoProtected"));}
  QVERIFY(o.performanceRange(0,100,0,8,0,16,"stale").contains("error"));
 }
#ifdef Q_MOC_RUN
 void expressionVelocitySavedHistory();
 void expressionPartialApplicationMouse();
 void analysisIdentityAndPositionHint();
 void nativeHarmonySummaryGlyphs();
 void assistantSurfaceLifetime();
 void harmonyScoreRangeAndPositionDrag();
 void arrangementRetainsRangeAndSurfaces();
#else
#include "velocity_history_tests.inc"
#include "expression_application_tests.inc"
#include "assistant_surface_tests.inc"
#endif
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
 void fingerprintAndReadonlyBaseline(){
  auto s=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(s);mscore->setCurrentScoreView(mscore->appendScore(s));PluginAPI::Score w(s);PluginAPI::ScoreObserver o;o.setScore(&w);
  auto state=s->state();auto first=o.workflowFingerprint(0,1,o.analysisRevision());QVERIFY(!first.contains("error"));QCOMPARE(first["measures"].toList().size(),1);QVERIFY(s->state()==state);
  QString path=settings.filePath("baseline.mscz");auto r=o.copyWorkflowScore(path,o.analysisRevision());QVERIFY(r["ok"].toBool());QVERIFY(o.openWorkflowScore(path,true,r["sha256"].toString())["ok"].toBool());auto base=mscore->currentScore();QVERIFY(base!=s);QVERIFY(base->readOnly());
  PluginAPI::Score wb(base);PluginAPI::ScoreObserver ob;ob.setScore(&wb);auto duplicate=ob.workflowFingerprint(0,1,ob.analysisRevision());
  QCOMPARE(duplicate["context"],first["context"]);QCOMPARE(duplicate["measures"].toList().first().toMap()["hash"],first["measures"].toList().first().toMap()["hash"]);
  auto part=s->parts().front();auto channel=part->instrument()->channel(0);int mapping=channel->channel();QVERIFY(mapping>=0);
  const int oldPort=s->midiPort(mapping),oldChannel=s->midiChannel(mapping);
  s->updateMidiMapping(channel,part,2,3);auto changedRouting=o.workflowFingerprint(0,1,o.analysisRevision());QVERIFY(changedRouting["context"]!=first["context"]);
  s->updateMidiMapping(channel,part,oldPort,oldChannel);auto restored=o.workflowFingerprint(0,1,o.analysisRevision());QCOMPARE(restored["context"],first["context"]);
  QVERIFY(ob.copyWorkflowScore(path,ob.analysisRevision(),true).contains("error"));QVERIFY(!ParameterEdit::writeTapTempo(base,480,120,false,nullptr));QVERIFY(ob.applyExpression({}).contains("error"));
  QVERIFY(ob.openWorkflowScore(path,true,"tampered").contains("error"));
  mscore->setCurrentScoreView(mscore->scores().indexOf(s));auto note=toChord(s->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().front();s->startCmd();note->undoChangeProperty(Pid::VELO_OFFSET,12);s->endCmd();
  QVERIFY(o.workflowFingerprint(0,1,first["revision"].toString()+"invalid").contains("error"));auto changed=o.workflowFingerprint(0,1,o.analysisRevision());QVERIFY(changed["measures"].toList().first().toMap()["hash"]!=first["measures"].toList().first().toMap()["hash"]);
 }
 void exportSourceRangeVoiceAndMelody(){
  std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);
  auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1800));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
  score->firstMeasure()->setRepeatCount(2);score->firstMeasure()->setRepeatEnd(true);
  ExportMidi exporter(score.get());exporter.collectGateData=true;QBuffer buffer;buffer.open(QIODevice::WriteOnly);QVERIFY(exporter.write(&buffer,true,true,mscore->synthesizerState()));
  auto data=exporter.gateData;QVERIFY(data.notes.size()>=12);bool repeated=false;int mapped=0;for(auto n:data.notes)if(n.known){++mapped;QVERIFY(n.sourceTick==0||n.sourceTick==480);QCOMPARE(n.sourceMeasure,1);if(n.on>=1920)repeated=true;}QVERIFY(mapped>=12);QVERIFY(repeated);
  MidiGateProcessor processor;MidiGateOptions gate;gate.jitter=0;gate.scoreStart=0;gate.scoreEnd=1920;gate.protectMelody=true;auto rows=processor.process(data,gate);int cropped=0,protectedCount=0;
  for(auto n:rows){if(n.melody){QCOMPARE(n.newOff,n.off);QCOMPARE(n.reason,QString("protected-melody"));++protectedCount;}else if(n.newOff<n.off)++cropped;}QVERIFY(cropped>0);QVERIFY(protectedCount>0);
  gate.scoreStart=1920;gate.scoreEnd=3840;rows=processor.process(data,gate);for(auto n:rows)QCOMPARE(n.newOff,n.off);
  gate.scoreStart=0;gate.scoreEnd=1920;gate.protectMelody=false;gate.tracks={4};rows=processor.process(data,gate);cropped=0;for(auto n:rows){if(n.voice!=4){QCOMPARE(n.newOff,n.off);QCOMPARE(n.reason,QString("unselected-voice"));}else if(n.newOff<n.off)++cropped;}QVERIFY(cropped>0);
  gate.protectedTracks={4};rows=processor.process(data,gate);for(auto n:rows)QCOMPARE(n.newOff,n.off);
 }
 void cropRangePreferencesAndExplicitVoices(){
  std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});panel.show();
  auto settingsButton=panel.findChild<QPushButton*>("midiCropSettingsButton");QVERIFY(settingsButton);bool configured=false;
  QTimer::singleShot(50,&panel,[&]{for(auto w:QApplication::topLevelWidgets())if(auto d=qobject_cast<QDialog*>(w))if(d->objectName()=="midiCropSettings"){auto from=d->findChild<QSpinBox*>("cropFromMeasure"),until=d->findChild<QSpinBox*>("cropUntilMeasure");if(from&&until){from->setValue(1);until->setValue(1);d->findChild<QCheckBox*>("cropProtectMelody")->setChecked(true);configured=true;}d->accept();}});settingsButton->click();QVERIFY(configured);
  auto gate=panel.options(score.get()).gate;QCOMPARE(gate.scoreStart,0);QCOMPARE(gate.scoreEnd,1920);QVERIFY(gate.protectMelody);MidiCropPanel restored(nullptr,[&]{return QList<Score*>{score.get()};});QVERIFY(restored.options(score.get()).gate.protectMelody);QCOMPARE(restored.options(score.get()).gate.scoreEnd,1920);
  auto compare=panel.findChild<QPushButton*>("midiCropCompare");QVERIFY(compare);QTimer timer;timer.setInterval(50);int stage=0,tries=0;bool done=false;
  connect(&timer,&QTimer::timeout,&panel,[&]{++tries;for(auto w:QApplication::topLevelWidgets())if(auto d=qobject_cast<QDialog*>(w)){
   if(d->objectName()=="midiCropStaffVoices"&&stage==1){auto tree=d->findChild<QTreeWidget*>("cropStaffVoicesTree");if(tree&&tree->topLevelItemCount()>=8){for(int i=0;i<tree->topLevelItemCount();++i){auto row=tree->topLevelItem(i);row->setCheckState(1,i==4?Qt::Checked:Qt::Unchecked);row->setCheckState(2,i==4?Qt::Checked:Qt::Unchecked);}stage=2;d->accept();}}
   else if(d->objectName()=="midiCropComparison"){
    if(stage==0){stage=1;QTimer::singleShot(30,&panel,[&]{for(auto w:QApplication::topLevelWidgets())if(auto nested=qobject_cast<QDialog*>(w))if(nested->objectName()=="midiCropStaffVoices"){auto tree=nested->findChild<QTreeWidget*>("cropStaffVoicesTree");if(tree&&tree->topLevelItemCount()>=8){for(int i=0;i<tree->topLevelItemCount();++i){auto row=tree->topLevelItem(i);row->setCheckState(1,i==4?Qt::Checked:Qt::Unchecked);row->setCheckState(2,i==4?Qt::Checked:Qt::Unchecked);}stage=2;nested->accept();}else nested->reject();}});if(auto b=d->findChild<QPushButton*>("midiCropStaffVoicesButton"))b->click();}
    else if(stage==2){auto table=d->findChild<QTableView*>();if(table&&table->model()->rowCount()>0){auto out=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!out.isEmpty())d->grab().save(out+"/midi-range-protection.png");done=true;d->accept();}}
    if(tries>100)d->reject();
   }
  }});timer.start();compare->click();timer.stop();QVERIFY(done);gate=panel.options(score.get()).gate;QVERIFY(gate.tracks==QSet<int>{4});QVERIFY(gate.protectedTracks==QSet<int>{4});panel.hide();
 }
 void completePerformancePagingBudget(){
  QFile fixture(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(fixture.open(QIODevice::ReadOnly));QDomDocument xml;QVERIFY(xml.setContent(fixture.readAll()));auto source=xml.documentElement().firstChildElement("Score");
  for(auto staff=source.firstChildElement("Staff");!staff.isNull();staff=staff.nextSiblingElement("Staff")){auto original=staff.firstChildElement("Measure");for(int i=2;i<=1000;++i){auto m=original.cloneNode(true).toElement();m.setAttribute("number",i);auto voice=m.firstChildElement("voice");voice.removeChild(voice.firstChildElement("Clef"));voice.removeChild(voice.firstChildElement("TimeSig"));staff.appendChild(m);}}
  QFile input(settings.filePath("large-performance.mscx"));QVERIFY(input.open(QIODevice::WriteOnly));input.write(xml.toByteArray());input.close();std::unique_ptr<MasterScore> score(mscore->readScore(input.fileName()));QVERIFY(score);PluginAPI::Score w(score.get());PluginAPI::ScoreObserver o;o.setScore(&w);QVERIFY(!o.workflowInfo().contains("error"));
  int at=0,count=0,pages=0,batch=12;double worst=0;auto revision=o.analysisRevision();QElapsedTimer timer;timer.start();
  while(true){auto page=o.performanceRange(0,score->lastMeasure()->endTick().ticks(),0,8,at,batch,revision);QVERIFY(!page.contains("error"));auto notes=page["notes"].toList();count+=notes.size();for(auto n:notes)QVERIFY(n.toMap().contains("velocityBase"));worst=qMax(worst,page["milliseconds"].toDouble());if(page["milliseconds"].toDouble()>2)batch=qMax(1,batch/2);else if(page["milliseconds"].toDouble()<.8)batch=qMin(32,batch+1);++pages;if(page["done"].toBool())break;QVERIFY(page["nextTick"].toInt()>at);at=page["nextTick"].toInt();QVERIFY(pages<5000);}
  QCOMPARE(count,6000);qInfo("P1 complete expression reads: 1000 bars / %d notes / %d pages, peak %.3f ms, total %.3f ms",count,pages,worst,double(timer.nsecsElapsed())/1000000.);
 }
 void realWorkflowPanels(){
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));mscore->resize(1200,850);mscore->show();
  for(QString name:{"ReferencePlayer","PerformanceExpression","ArrangementVariants"}){
   QString path=QString(TESTROOT)+"/share/plugins/"+name+"/"+name+"_MS3.qml";PluginDescription description;description.path=path;QVERIFY2(collectPluginMetaInformation(&description),qPrintable(path));mscore->registerPlugin(&description);mscore->pluginTriggered(path);QTest::qWait(120);
   QmlPlugin* root=nullptr;
   for(auto w:QGuiApplication::allWindows()){auto view=qobject_cast<QQuickView*>(w);auto panel=view?qobject_cast<QmlPlugin*>(view->rootObject()):nullptr;if(panel&&panel->property("menuPath").toString().contains(name=="ReferencePlayer"?"Reference Player":name=="PerformanceExpression"?"Performance Expression":"Arrangement Variants"))root=panel;}
   QVERIFY(root);QVERIFY(root->property("observer").value<QObject*>());if(root->property("embeddedWorkflow").toBool()){auto dock=mscore->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);dock->setFloating(true);QTest::qWait(30);QVERIFY(dock->isFloating());dock->setFloating(false);}else{root->setPanelFloating(true);QTest::qWait(30);QVERIFY(root->panelFloating());root->setPanelFloating(false);}QTest::qWait(30);
   if(name=="ReferencePlayer")QVERIFY(root->property("media").value<QObject*>()->property("available").toBool());
   else if(name=="PerformanceExpression")QVERIFY(root->property("workerSource").toString().size()>1000);
   else {
    root->setProperty("workspace",QUrl::fromLocalFile(settings.filePath("variants.json")).toString());QVERIFY(QMetaObject::invokeMethod(root,"newGroup"));QTRY_VERIFY(root->property("groupIndex").toInt()==0&&!root->property("busy").toBool());
    auto groups=root->property("groups").value<QJSValue>().toVariant().toList();QCOMPARE(groups.size(),1);QCOMPARE(groups[0].toMap()["entries"].toList().size(),2);
    QVERIFY(QFile::exists(settings.filePath("variants.json")));QVERIFY(mscore->currentScore()!=score);
    QVERIFY(QMetaObject::invokeMethod(root,"saveActive"));QVERIFY(QMetaObject::invokeMethod(root,"duplicate"));QTRY_VERIFY(!root->property("busy").toBool());
    groups=root->property("groups").value<QJSValue>().toVariant().toList();QCOMPARE(groups[0].toMap()["entries"].toList().size(),3);
    QVERIFY(QMetaObject::invokeMethod(root,"load",Q_ARG(QVariant,QVariant(QUrl::fromLocalFile(settings.filePath("variants.json")).toString()))));QTRY_VERIFY(!root->property("busy").toBool());QVERIFY(mscore->currentScore()!=score);
   }
  }
 }

 void panelPositionAndTabs(){
  QMainWindow main;main.resize(800,600);auto inspector=new QDockWidget("Inspector",&main);inspector->setObjectName("inspector");main.addDockWidget(Qt::RightDockWidgetArea,inspector);
  auto dock=new QDockWidget("Tools",&main);QVERIFY(arrangeWorkflowDock(&main,dock,"repair-position-test","rightTools",Qt::RightDockWidgetArea));QVERIFY(main.tabifiedDockWidgets(inspector).contains(dock));
  main.show();QTest::qWait(30);main.addDockWidget(Qt::LeftDockWidgetArea,dock);dock->show();QTest::qWait(300);dock->close();delete dock;
  auto reopened=new QDockWidget("Tools",&main);QVERIFY(arrangeWorkflowDock(&main,reopened,"repair-position-test","rightTools",Qt::RightDockWidgetArea));QCOMPARE(main.dockWidgetArea(reopened),Qt::LeftDockWidgetArea);
 }
 void realPanelMouseActions(){
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));mscore->resize(1250,900);mscore->show();
  auto open=[&](QString folder,QString title)->QmlPlugin*{
   const QString path=QString(TESTROOT)+"/share/plugins/"+folder+"/"+folder+"_MS3.qml";PluginDescription d;d.path=path;if(!collectPluginMetaInformation(&d))return nullptr;mscore->registerPlugin(&d);mscore->pluginTriggered(path);QTest::qWait(100);
   for(auto w:QGuiApplication::allWindows()){auto view=qobject_cast<QQuickView*>(w);auto root=view?qobject_cast<QmlPlugin*>(view->rootObject()):nullptr;if(root&&root->property("menuPath").toString().endsWith(title))return root;}return nullptr;
  };
  auto click=[](QmlPlugin* root,const char* name){QTest::qWait(60);if(root->property("embeddedWorkflow").toBool()&&(QString(name)=="expressionLocate"||QString(name)=="expressionConfirm"||QString(name)=="expressionOpenEditor")){QMetaObject::invokeMethod(root,"showMoreActions");QTest::qWait(60);}auto item=root->findChild<QQuickItem*>(name);if(!item){std::function<QQuickItem*(QQuickItem*)> find=[&](QQuickItem* parent)->QQuickItem*{if(parent->objectName()==name)return parent;for(auto child:parent->childItems())if(auto result=find(child))return result;return nullptr;};item=find(root);}for(int wait=0;item && !item->isEnabled() && wait<80;++wait)QTest::qWait(25);if(!item||!item->isVisible()||!item->isEnabled())return false;auto where=item->mapToScene(QPointF(item->width()/2,item->height()/2));qInfo()<<name<<where<<root->window()->size()<<root->size();if(!QRectF(QPointF(),item->window()->size()).contains(where))return false;QTest::mouseClick(item->window(),Qt::LeftButton,Qt::NoModifier,where.toPoint());return true;};
  auto expression=open("PerformanceExpression","Performance Expression");QVERIFY(expression);floatWorkflowPanel(expression);expression->focusPanel();QTest::qWait(100);
  auto observer=qobject_cast<PluginAPI::ScoreObserver*>(expression->property("observer").value<QObject*>());QVERIFY(observer);
  auto info=observer->workflowInfo(),scope=observer->analysisScope(false);auto page=observer->performanceRange(scope["start"].toInt(),scope["end"].toInt(),0,score->ntracks(),0,32,observer->analysisRevision());auto notes=page["notes"].toList();QVERIFY(notes.size()>1);auto n=notes[1].toMap();
  QVariantList performance;for(int i=0;i<notes.size();++i){auto note=notes[i].toMap();performance.append(QVariantMap{{"id",QString("m%1").arg(i)},{"pitch",note["pitch"]},{"seconds",note["scoreSeconds"]},{"velocity",72}});}
  auto row=QVariantMap{{"original",n},{"tick",n["tick"]},{"track",n["track"]},{"index",n["index"]},{"pitch",n["pitch"]},{"seconds",n["scoreSeconds"]},{"scoreId",n["id"]},{"midiId","m1"},{"confirmed",false},{"confidence",.7},{"status","待确认"}};
  QVERIFY(QMetaObject::invokeMethod(expression,"chooseTarget",Q_ARG(QVariant,false)));expression->setProperty("stale",false);expression->setProperty("scope",scope);expression->setProperty("info",info);expression->setProperty("notes",notes);expression->setProperty("midi",QVariantMap{{"notes",performance},{"sha256","test"}});expression->setProperty("revision",observer->analysisRevision());expression->setProperty("result",QVariantMap{{"matches",QVariantList{row}},{"pending",1},{"missing",QVariantList{}},{"extra",QVariantList{}}});expression->setProperty("selected",0);expression->setProperty("advancedSettings",true);QTest::qWait(30);
  QVERIFY(click(expression,"expressionLocate"));QVERIFY2(score->selection().noteList().size()==1,qUtf8Printable(expression->property("status").toString()));QCOMPARE(score->selection().noteList()[0]->pitch(),n["writtenPitch"].toInt());QVERIFY(expression->property("status").toString().contains("已选中"));
  QVERIFY(click(expression,"expressionConfirm"));QTRY_VERIFY(!expression->property("busy").toBool());QVERIFY(expression->property("status").toString().contains("已确认"));
  expression->setProperty("advancedSettings",false);QTRY_VERIFY(expression->property("candidate").value<QJSValue>().isObject());
  const int auditionTabs=mscore->scores().size();
  QVERIFY(click(expression,"expressionOriginalAudition"));QTRY_VERIFY2(expression->property("status").toString().contains("声音设备"),qPrintable(expression->property("status").toString()));
  QCOMPARE(mscore->scores().size(),auditionTabs);QCOMPARE(mscore->currentScore(),static_cast<Score*>(score));
  QVERIFY(click(expression,"expressionCandidateAudition"));QTRY_VERIFY(expression->property("status").toString().contains("声音设备"));QCOMPARE(mscore->scores().size(),auditionTabs);QVERIFY(!expression->property("previewing").toBool());

  auto oldVelocity=score->undoStack()->getCurIdx();auto original=score->selection().noteList().front();int oldRaw=original->veloOffset();
  QVERIFY(click(expression, "expressionOpenEditor"));
  auto editorDock = mscore->findChild<QDockWidget *>("performanceEditorDock");
  QVERIFY(editorDock);
  QTRY_VERIFY(editorDock->isVisible());
  auto compareLane = editorDock->findChild<QWidget *>("performanceWorkflowLanes");QVERIFY(compareLane);
  auto parameterCompare=editorDock->findChild<QToolButton*>("performanceExpressionCompare");QVERIFY(parameterCompare&&parameterCompare->isChecked());
  auto parameterSelector=editorDock->findChild<QComboBox*>("performanceParameter");QVERIFY(parameterSelector&&parameterSelector->isVisible());
  QVERIFY(!QRect(parameterSelector->mapTo(editorDock,QPoint()),parameterSelector->size()).intersects(QRect(parameterCompare->mapTo(editorDock,QPoint()),parameterCompare->size())));

  const auto previewOutput=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!previewOutput.isEmpty())editorDock->grab().save(previewOutput+"/editor-expression-main-049.png");
  QVERIFY(observer->showEditorCorrespondence()["ok"].toBool());QTRY_VERIFY(compareLane->isVisible());
  auto use = editorDock->findChild<QToolButton *>("workflowapply");
  QVERIFY(use && use->isVisible());
  QTest::qWait(80);
  auto lane=editorDock->findChild<QWidget*>("performanceWorkflowLanes");QVERIFY(lane);QVERIFY(lane->height()>=100);QVERIFY(lane->visibleRegion().contains(QPoint(lane->width()/2,lane->height()-2)));
  QVERIFY(editorDock->rect().contains(use->mapTo(editorDock,use->rect().center())));
  const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!output.isEmpty())editorDock->grab().save(output+"/editor-expression-047.png");QTest::mouseClick(use, Qt::LeftButton);
  QTRY_VERIFY(expression->property("status").toString().contains("已使用演奏力度"));
  QTRY_VERIFY(!expression->property("busy").toBool());QVERIFY(!expression->property("stale").toBool());
  QCOMPARE(score->undoStack()->getCurIdx(), oldVelocity + 1);
  QVERIFY(original->veloOffset() != oldRaw);
  EditData undo;score->undoRedo(true,&undo);QCOMPARE(original->veloOffset(),oldRaw);
  expression->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("manual-expression.png"));
  auto reference=open("ReferencePlayer","Reference Player");QVERIFY(reference);reference->setPanelFloating(true);reference->window()->resize(900,600);reference->focusPanel();auto media=reference->property("media").value<QObject*>();QVERIFY(media);
  QFile wav(settings.filePath("reference.wav"));QVERIFY(wav.open(QIODevice::WriteOnly));QDataStream stream(&wav);stream.setByteOrder(QDataStream::LittleEndian);stream.writeRawData("RIFF",4);stream<<quint32(36+288000);stream.writeRawData("WAVEfmt ",8);stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(48000)<<quint32(96000)<<quint16(2)<<quint16(16);stream.writeRawData("data",4);stream<<quint32(288000);QByteArray pcm;QDataStream samples(&pcm,QIODevice::WriteOnly);samples.setByteOrder(QDataStream::LittleEndian);for(int i=0;i<144000;++i)samples<<qint16(qSin(i*2*M_PI*440/48000.)*(2000+1500*qSin(i*2*M_PI*2/48000.)));stream.writeRawData(pcm.constData(),pcm.size());wav.close();
  QVERIFY(QMetaObject::invokeMethod(media,"open",Q_ARG(QString,QUrl::fromLocalFile(wav.fileName()).toString())));QTRY_VERIFY(media->property("duration").toDouble()>2500);
  reference->setProperty("associations",QVariantList{QVariantMap{{"name","第1小节"},{"start",0},{"end",scope["end"]},{"firstTrack",0},{"from",500},{"until",1500},{"stale",false}}});QTest::qWait(50);auto list=reference->findChild<QQuickItem*>("referenceAssociations");QVERIFY(list);auto point=list->mapToScene(QPointF(25,20));QTest::mouseClick(reference->window(),Qt::LeftButton,Qt::NoModifier,point.toPoint());QCOMPARE(reference->property("a").toDouble(),500.);QCOMPARE(reference->property("b").toDouble(),1500.);
  QVERIFY(click(reference,"referenceLocate"));QVERIFY(reference->property("status").toString().contains("已跳到"));media->setProperty("volume",0.);reference->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("reference-before-play-050.png"));QVERIFY(click(reference,"referencePlayAssociation"));QTRY_VERIFY(media->property("playing").toBool());QTRY_VERIFY(!media->property("playing").toBool());QVERIFY(reference->property("status").toString().contains("片段播放结束"));
  auto waveform=reference->findChild<QQuickItem*>("referenceWaveformMouse");QVERIFY(waveform);auto down=waveform->mapToScene(QPointF(waveform->width()*.4,waveform->height()*.5)).toPoint(),up=waveform->mapToScene(QPointF(waveform->width()*.6,waveform->height()*.5)).toPoint();
  QTest::mousePress(reference->window(),Qt::LeftButton,Qt::NoModifier,down);QTest::mouseMove(reference->window(),up);QTest::qWait(30);QVERIFY(reference->property("b").toDouble()>reference->property("a").toDouble());QCOMPARE(reference->property("selectedAssociation").toInt(),-1);QTest::mouseRelease(reference->window(),Qt::LeftButton,Qt::NoModifier,up);
  auto speed=reference->findChild<QQuickItem*>("referenceSpeed");QVERIFY(speed);auto speedPoint=speed->mapToScene(QPointF(speed->width()/2,10)).toPoint();double oldSpeed=media->property("speed").toDouble();QTest::mousePress(reference->window(),Qt::LeftButton,Qt::NoModifier,speedPoint);QTest::mouseMove(reference->window(),speedPoint+QPoint(20,0));QTest::mouseRelease(reference->window(),Qt::LeftButton,Qt::NoModifier,speedPoint+QPoint(20,0));QVERIFY(media->property("speed").toDouble()>oldSpeed);
  reference->focusPanel();reference->forceActiveFocus();QTest::qWait(80);double oldStart=reference->property("viewStart").toDouble();QTest::keyPress(reference->window(),Qt::Key_Space);QVERIFY(reference->property("spaceDown").toBool());QTest::mousePress(reference->window(),Qt::LeftButton,Qt::NoModifier,down);QTest::mouseMove(reference->window(),down+QPoint(30,0));QTest::mouseRelease(reference->window(),Qt::LeftButton,Qt::NoModifier,down+QPoint(30,0));QTest::keyRelease(reference->window(),Qt::Key_Space);QVERIFY(!media->property("playing").toBool());QVERIFY(!reference->property("spaceDown").toBool());QVERIFY(reference->property("viewStart").toDouble()<=oldStart);
  reference->forceActiveFocus();QTest::keyClick(reference->window(),Qt::Key_Space);QTRY_VERIFY(media->property("playing").toBool());QTest::keyClick(reference->window(),Qt::Key_Space);QTRY_VERIFY(!media->property("playing").toBool());
  QMimeData audioFiles;audioFiles.setUrls({QUrl::fromLocalFile(wav.fileName()),QUrl::fromLocalFile(wav.fileName())});
  QDragEnterEvent dragEnter(QPoint(100,180),Qt::CopyAction,&audioFiles,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(reference->window(),&dragEnter);
  QDragMoveEvent dragMove(QPoint(100,180),Qt::CopyAction,&audioFiles,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(reference->window(),&dragMove);
  QDropEvent drop(QPointF(100,180),Qt::CopyAction,&audioFiles,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(reference->window(),&drop);
  QTRY_COMPARE(reference->property("tracks").value<QJSValue>().toVariant().toList().size(),2);
  QTRY_VERIFY(([&]{const auto tracks=reference->property("tracks").value<QJSValue>().toVariant().toList();return tracks.size()==2 && tracks[0].toMap().value("b").toDouble()>2500 && tracks[1].toMap().value("b").toDouble()>2500;})());
  reference->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("manual-reference.png"));
 }

 void recordedExpressionMouseActions(){
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));
  const QString path=QString(TESTROOT)+"/share/plugins/PerformanceExpression/PerformanceExpression_MS3.qml";
  PluginDescription description;description.path=path;QVERIFY(collectPluginMetaInformation(&description));mscore->registerPlugin(&description);mscore->pluginTriggered(path);QTest::qWait(100);
  QmlPlugin* root=nullptr;for(auto window:QGuiApplication::allWindows()){auto view=qobject_cast<QQuickView*>(window);auto item=view?qobject_cast<QmlPlugin*>(view->rootObject()):nullptr;if(item&&item->property("menuPath").toString().endsWith("Performance Expression")&&item->isVisible())root=item;}
  QVERIFY(root);floatWorkflowPanel(root);root->focusPanel();
  auto click=[root](const char* name){if(root->property("embeddedWorkflow").toBool()&&(QString(name)=="expressionLocate"||QString(name)=="expressionConfirm"||QString(name)=="expressionOpenEditor")){QMetaObject::invokeMethod(root,"showMoreActions");QTest::qWait(60);}auto item=root->findChild<QQuickItem*>(name);if(!item||!item->isEnabled()||!item->isVisible())return false;auto p=item->mapToScene(QPointF(item->width()/2,item->height()/2));QTest::mouseClick(item->window(),Qt::LeftButton,Qt::NoModifier,p.toPoint());return true;};
  QVERIFY(click("expressionRecord"));QTRY_VERIFY(!root->property("busy").toBool());
  QVERIFY2(root->property("status").toString().contains("输入"),qPrintable(root->property("status").toString()));
  auto notes=root->property("notes").value<QJSValue>().toVariant().toList();QVERIFY(notes.size()>1);
  auto session=qobject_cast<MidiCaptureSession*>(root->property("captureSession").value<QObject*>());QVERIFY(session);
  // Supply the same input interface using a timed test keyboard, with no physical device.
  QVERIFY(!session->begin({{"notes",notes},{"monitor",false}},false).contains("error"));
  root->setProperty("busy",true);root->setProperty("status",QString::fromUtf8("录制中，弹完后点击停止录制。"));
  struct Input{double time;int type,pitch;};QVector<Input> events;double first=1e9;QSet<QString> seen;
  for(auto value:notes){auto note=value.toMap();if(note["tieBack"].toBool()||note["grace"].toBool())continue;auto id=note.value("logicalId",note["id"]).toString();if(seen.contains(id))continue;seen.insert(id);first=std::min(first,note["scoreSeconds"].toDouble());}
  seen.clear();for(auto value:notes){auto note=value.toMap();if(note["tieBack"].toBool()||note["grace"].toBool())continue;auto id=note.value("logicalId",note["id"]).toString();if(seen.contains(id))continue;seen.insert(id);double time=(note["scoreSeconds"].toDouble()-first)*.3;events.append({time,ME_NOTEON,note["pitch"].toInt()});events.append({time+.04,ME_NOTEOFF,note["pitch"].toInt()});}
  std::stable_sort(events.begin(),events.end(),[](const Input&a,const Input&b){return a.time<b.time;});qint64 origin=midiInputTime();
  for(const auto& event:events){int wait=int((origin+qint64(event.time*1e9)-midiInputTime())/1000000);if(wait>0)QTest::qWait(wait);captureMidiInput(NPlayEvent(event.type,0,event.pitch,event.type==ME_NOTEON?105:0),midiInputTime());}
  root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("recording-expression.png"));
  QVERIFY(click("expressionRecord"));QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!session->state()["stopping"].toBool()&&!session->state()["saving"].toBool(),10000);
  QTRY_VERIFY_WITH_TIMEOUT(root->property("candidate").value<QJSValue>().isObject(),10000);
  auto result=root->property("result").value<QJSValue>().toVariant().toMap();QVERIFY2(!result["matches"].toList().isEmpty(),qPrintable(root->property("status").toString()));
  root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("recorded-expression-preview.png"));
  int undo=score->undoStack()->getCurIdx();QVERIFY(click("expressionApplyVelocity"));QTRY_VERIFY(root->property("status").toString().contains("已使用演奏力度"));QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
  EditData edit;score->undoRedo(true,&edit);QCOMPARE(score->undoStack()->getCurIdx(),undo);
  root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("recorded-expression-applied.png"));
  root->setVisible(false);root->deleteLater();QTest::qWait(50);
 }
 void harmonyOpenAndScoreSwitch(){
  auto first=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(first);
  mscore->setCurrentScoreView(mscore->appendScore(first));mscore->resize(1250,900);mscore->show();first->deselectAll();
  const QString path=QString(TESTROOT)+"/share/plugins/HarmonyAssistant/HarmonyAssistant_MS3.qml";PluginDescription description;description.path=path;QVERIFY(collectPluginMetaInformation(&description));mscore->registerPlugin(&description);mscore->pluginTriggered(path);
  QmlPlugin* root=nullptr;QTRY_VERIFY(([&]{for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto item=qobject_cast<QmlPlugin*>(view->rootObject()))if(item->menuPath().endsWith("Harmony Assistant")){root=item;return true;}return false;})());
  auto config=root->property("configuration").value<QJSValue>().toVariant().toMap();config["chordLabels"]=true;config["dualPanel"]=true;
  QVERIFY(QMetaObject::invokeMethod(root,"applyPreferences",Q_ARG(QVariant,QVariant(config))));
  qInfo("Harmony: waiting for first analysis");
  QTRY_VERIFY(!root->property("analysisDirty").toBool());qInfo("Harmony: first analysis ready");QVERIFY(root->property("analysisRegions").value<QJSValue>().toVariant().toList().size()>0);QVERIFY(root->summaryPanelVisible());
  auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);QVERIFY(observer->auxiliarySurfaceVisible());
  auto second=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(second);second->setName("Harmony switch test");mscore->setCurrentScoreView(mscore->appendScore(second));second->deselectAll();
  qInfo("Harmony: waiting for new score");
  QTRY_VERIFY(observer->wrappedScore());QTRY_COMPARE(observer->wrappedScore()->score(),static_cast<Score*>(second));QTRY_VERIFY(!root->property("analysisDirty").toBool());
  QVERIFY(root->property("analysisRegions").value<QJSValue>().toVariant().toList().size()>0);QTRY_VERIFY2(root->property("analysisStatus").toString().startsWith("已更新"),qPrintable(root->property("analysisStatus").toString()));
  auto view=mscore->currentScoreView();view->setOffset(0,0);view->adjustCanvasPosition(second->firstMeasure(),false);QTest::qWait(80);
  view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("manual-harmony-score.png"));
  if(root->summaryPanelHost())root->summaryPanelHost()->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("manual-harmony-summary.png"));
 }

 void rawMidiAtomicCopy(){
  PluginAPI::ScoreObserver o;auto source=settings.filePath("source.mid"),copy=settings.filePath("copy.mid");QFile f(source);QVERIFY(f.open(QIODevice::WriteOnly));QByteArray data=QByteArray::fromHex("4d546864000000060000000101e04d54726b0000000400ff2f00");f.write(data);f.close();
  auto hash=QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());
  QVERIFY(o.savePerformanceSource(source,copy,hash)["ok"].toBool());QVERIFY(o.savePerformanceSource(source,copy,hash)["ok"].toBool());QVERIFY(o.savePerformanceSource(source,copy,"changed").contains("error"));
  QFile g(copy);QVERIFY(g.open(QIODevice::ReadOnly));QCOMPARE(g.readAll(),data);
  QSignalSpy done(&o,&PluginAPI::ScoreObserver::performanceMidiReady);o.readPerformanceMidi(source);QTRY_COMPARE(done.count(),1);QVERIFY(!done[0][1].toMap().contains("error"));
 }
 void nativeReferenceDocumentAndMenu();
 void referenceClockAndPlan();
 void editorCorrespondenceSelection();
 void multiTrackSaveRestore();
 void excerptExpressionPreview();
 void regionalBoundaryConnectionsUndoAndFile();
 void regionalGlissandoRequiresBothEnds();
 void regionalScoreSwitchMouseAndRestore();
 void regionalSwitchUndoAndSharedSettings();
 void regionalCombinationAndDifferences();
 void gateManualReleaseAndAudition();
 void gateEditorMouseComparison();
 void regionalComparisonMouseAndWheel();
 void regionalSaveMainAndRestoreCurrent();
 void regionalIndependentVoicesExpandImportAndExport();
 void regionalRejectForeignVoiceAndKeepFile();
 void regionalPanelMouseOperations();
 void regionalNotationFixturesAndCrossBoundary();
 void regionalResourcesLinkedPartsAndManualEvents();
 void regionalAtomicFileSaveAndReopen();
};
#include "reference_tests.inc"
#include "reference_056_tests.inc"
#include "regional_tests.inc"
#include "regional_comparison_tests.inc"
#include "gate_comparison_tests.inc"
QTEST_MAIN(TestP1Native)
#include "tst_p1native.moc"
