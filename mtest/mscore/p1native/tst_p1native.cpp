#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QSaveFile>
#include <QQuickView>
#include <QJSValue>
#include <QDomDocument>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include "mscore/scoreview.h"
#include "mscore/shortcut.h"
#include "mscore/temposymbols.h"
#include "mscore/palette.h"
#include "mscore/palette/palettetree.h"
#include "mscore/taptempo/tapmark.h"
#include "mscore/taptempo/taptempo.h"
#include "libmscore/tempoexpression.h"
#include "libmscore/textline.h"
#include <QTreeWidget>
#include <QTableView>
#include "mscore/midicrop/midicroppanel.h"
#include <QDockWidget>
#include <QDataStream>
#include <QtMath>
#include "mscore/workflow/paneldock.h"
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
#include "libmscore/xml.h"
#include "libmscore/audio.h"
#include "libmscore/excerpt.h"
#include "libmscore/image.h"
#include "libmscore/part.h"
#include "libmscore/instrument.h"
#include "libmscore/pedal.h"
#include "audio/exports/exportmidi.h"
#include "audio/exports/midigatefile.h"
#include "libmscore/synthesizerstate.h"
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
#ifdef Q_MOC_RUN
 void playableTempoPaletteReadAndApply();
 void tapMarkPreviewCancelAndSave();
 void tapMarkRestoreAndRitProtection();
#else
#include "tempo_palette_tests.inc"
#include "tap_mark_tests.inc"
#endif
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
   QVERIFY(root);QVERIFY(root->property("observer").value<QObject*>());root->setPanelFloating(true);QTest::qWait(30);QVERIFY(root->panelFloating());root->setPanelFloating(false);QTest::qWait(30);
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
  auto click=[](QmlPlugin* root,const char* name){QTest::qWait(60);auto item=root->findChild<QQuickItem*>(name);if(!item||!item->isVisible()||!item->isEnabled())return false;auto where=item->mapToScene(QPointF(item->width()/2,item->height()/2));qInfo()<<name<<where<<root->window()->size()<<root->size();if(!QRectF(QPointF(),root->window()->size()).contains(where))return false;QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,where.toPoint());return true;};
  auto expression=open("PerformanceExpression","Performance Expression");QVERIFY(expression);expression->setPanelFloating(true);expression->window()->resize(520,800);expression->focusPanel();QTest::qWait(100);
  auto observer=qobject_cast<PluginAPI::ScoreObserver*>(expression->property("observer").value<QObject*>());QVERIFY(observer);
  auto info=observer->workflowInfo(),scope=observer->analysisScope(false);auto page=observer->performanceRange(scope["start"].toInt(),scope["end"].toInt(),0,score->ntracks(),0,32,observer->analysisRevision());auto notes=page["notes"].toList();QVERIFY(notes.size()>1);auto n=notes[1].toMap();
  QVariantList performance;for(int i=0;i<notes.size();++i){auto note=notes[i].toMap();performance.append(QVariantMap{{"id",QString("m%1").arg(i)},{"pitch",note["pitch"]},{"seconds",note["scoreSeconds"]},{"velocity",72}});}
  auto row=QVariantMap{{"original",n},{"tick",n["tick"]},{"track",n["track"]},{"index",n["index"]},{"pitch",n["pitch"]},{"seconds",n["scoreSeconds"]},{"scoreId",n["id"]},{"midiId","m1"},{"confirmed",false},{"confidence",.7},{"status","待确认"}};
  expression->setProperty("scope",scope);expression->setProperty("info",info);expression->setProperty("notes",notes);expression->setProperty("midi",QVariantMap{{"notes",performance},{"sha256","test"}});expression->setProperty("revision",observer->analysisRevision());expression->setProperty("result",QVariantMap{{"matches",QVariantList{row}},{"pending",1},{"missing",QVariantList{}},{"extra",QVariantList{}}});expression->setProperty("selected",0);QTest::qWait(30);
  QVERIFY(click(expression,"expressionLocate"));QCOMPARE(score->selection().noteList().size(),1);QCOMPARE(score->selection().noteList()[0]->pitch(),n["writtenPitch"].toInt());QVERIFY(expression->property("status").toString().contains("已选中"));
  QVERIFY(click(expression,"expressionConfirm"));QTRY_VERIFY(!expression->property("busy").toBool());QVERIFY(expression->property("status").toString().contains("已确认"));
  expression->setProperty("workspace",QUrl::fromLocalFile(settings.filePath("expression.json")).toString());QVERIFY(click(expression,"expressionOriginalAudition"));QVERIFY(mscore->currentScore()!=score);QVERIFY(expression->property("status").toString().contains("声音设备"));QVERIFY(click(expression,"expressionStop"));QTRY_VERIFY2(expression->property("status").toString().contains("试听已停止"),qPrintable(expression->property("status").toString()));expression->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("return-debug.png"));QVERIFY(click(expression,"expressionReturn"));QTRY_VERIFY2(expression->property("status").toString().contains("已返回原谱"),qPrintable(expression->property("status").toString()));QCOMPARE(mscore->currentScore(),score);QTest::qWait(100);
  expression->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("manual-expression.png"));
  auto reference=open("ReferencePlayer","Reference Player");QVERIFY(reference);reference->setPanelFloating(true);reference->window()->resize(900,600);reference->focusPanel();auto media=reference->property("media").value<QObject*>();QVERIFY(media);
  QFile wav(settings.filePath("reference.wav"));QVERIFY(wav.open(QIODevice::WriteOnly));QDataStream stream(&wav);stream.setByteOrder(QDataStream::LittleEndian);stream.writeRawData("RIFF",4);stream<<quint32(36+288000);stream.writeRawData("WAVEfmt ",8);stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(48000)<<quint32(96000)<<quint16(2)<<quint16(16);stream.writeRawData("data",4);stream<<quint32(288000);QByteArray pcm;QDataStream samples(&pcm,QIODevice::WriteOnly);samples.setByteOrder(QDataStream::LittleEndian);for(int i=0;i<144000;++i)samples<<qint16(qSin(i*2*M_PI*440/48000.)*(2000+1500*qSin(i*2*M_PI*2/48000.)));stream.writeRawData(pcm.constData(),pcm.size());wav.close();
  QVERIFY(QMetaObject::invokeMethod(media,"open",Q_ARG(QString,QUrl::fromLocalFile(wav.fileName()).toString())));QTRY_VERIFY(media->property("duration").toDouble()>2500);
  reference->setProperty("associations",QVariantList{QVariantMap{{"name","第1小节"},{"start",0},{"end",scope["end"]},{"firstTrack",0},{"from",500},{"until",1500},{"stale",false}}});QTest::qWait(50);auto list=reference->findChild<QQuickItem*>("referenceAssociations");QVERIFY(list);auto point=list->mapToScene(QPointF(25,20));QTest::mouseClick(reference->window(),Qt::LeftButton,Qt::NoModifier,point.toPoint());QCOMPARE(reference->property("a").toDouble(),500.);QCOMPARE(reference->property("b").toDouble(),1500.);
  QVERIFY(click(reference,"referenceLocate"));QVERIFY(reference->property("status").toString().contains("已跳到"));media->setProperty("volume",0.);QVERIFY(click(reference,"referencePlayAssociation"));QTRY_VERIFY(media->property("playing").toBool());QTRY_VERIFY(!media->property("playing").toBool());QVERIFY(reference->property("status").toString().contains("片段播放结束"));
  reference->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath("manual-reference.png"));
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
