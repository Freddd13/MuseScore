// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>
#include <QBuffer>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTableView>
#include <QPushButton>
#include <QDialog>
#include <QDomDocument>
#include "mscore/midicrop/midicroppanel.h"
#include <QQuickView>
#include "mtest/testutils.h"
#include "mscore/musescore.h"
#include "mscore/preferences.h"
#include "mscore/shortcut.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/qmlplugin.h"
#include "mscore/plugin/pluginManager.h"
#include "mscore/plugin/api/readonlyanalysisjob.h"
#include "audio/exports/exportmidi.h"
#include "audio/midi/fluid/fluid.h"
#include <QDataStream>
#include "audio/exports/midigatefile.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/measure.h"
#include "libmscore/undo.h"
#include "libmscore/pedal.h"
#include "libmscore/synthesizerstate.h"
static void initP0Resources() { Q_INIT_RESOURCE(musescorefonts_Petaluma); }
using namespace Ms;
class TestP0Native : public QObject {
      Q_OBJECT
      QTemporaryDir settings;
      QVector<float> render(const MidiFile& file,const MidiGateData& clockData,const QString& font) {
            FluidS::Fluid synth;synth.init(44100);if(!synth.loadSoundFonts({font}))return {};
            MidiGateProcessor clock;clock.process(clockData,{});
            std::multimap<int,PlayEvent> events;int last=0;
            for(const auto& track:file.tracks())for(const auto& pair:track.events())if(pair.second.type()==ME_NOTEON || pair.second.type()==ME_CONTROLLER || pair.second.type()==ME_PITCHBEND) {
                  int frame=qRound(clock.durationMs(0,pair.first)*44.1);events.emplace(frame,PlayEvent(pair.second));last=qMax(last,frame);
                  }
            QVector<float> output((last+44100*3)*2,0);auto event=events.begin();int at=0;
            while(at<output.size()/2) {
                  while(event!=events.end() && event->first<=at) {synth.play(event->second);++event;}
                  int count=qMin(64,output.size()/2-at);if(event!=events.end())count=qMin(count,event->first-at);
                  if(count<=0)continue;
                  float reverb[128]={},chorus[128]={};synth.process(unsigned(count),output.data()+at*2,reverb,chorus);at+=count;
                  }
            return output;
            }
      void wave(const QString& name,const QVector<float>& pcm,float gain) {
            QFile file(name);if(!file.open(QIODevice::WriteOnly))return;QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);
            const quint32 bytes=quint32(pcm.size()*2);stream.writeRawData("RIFF",4);stream<<quint32(36+bytes);stream.writeRawData("WAVEfmt ",8);stream<<quint32(16)<<quint16(1)<<quint16(2)<<quint32(44100)<<quint32(176400)<<quint16(4)<<quint16(16);stream.writeRawData("data",4);stream<<bytes;
            for(float sample:pcm)stream<<qint16(qBound(-32767,qRound(sample*gain*32767),32767));
            }
   private slots:
      void initTestCase() {
            QVERIFY(settings.isValid());QCoreApplication::setOrganizationName("MuseScoreRegression");QCoreApplication::setApplicationName("P0Native");
            QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());dataPath=settings.path();
            MScore::noGui=true;noSeq=true;converterMode=true;initMuseScoreResources();initP0Resources();QStringList arguments;MuseScore::init(arguments);QVERIFY(mscore);
            }
      void tapUsesSharedActionAndFixedGeometry() {
            auto action=getAction("tap-tempo");QVERIFY(action);QVERIFY(!action->autoRepeat());
            QToolButton* button=nullptr;for(auto candidate:mscore->findChildren<QToolButton*>())if(candidate->defaultAction()==action && candidate->menu())button=candidate;
            QVERIFY(button);const int width=button->width();action->trigger();QTest::qWait(65);action->trigger();QCOMPARE(button->width(),width);QVERIFY(button->text().startsWith("TAP"));
            QSignalSpy taps(action,&QAction::triggered);QTest::keyPress(button,Qt::Key_Space);QCOMPARE(taps.size(),1);
            QKeyEvent repeat(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier,QString(),true);QApplication::sendEvent(button,&repeat);QCOMPARE(taps.size(),1);
            QTest::keyRelease(button,Qt::Key_Space);QCOMPARE(taps.size(),1);
            QTest::mouseClick(button,Qt::LeftButton,Qt::NoModifier,QPoint(8,8));QTest::mouseDClick(button,Qt::LeftButton,Qt::NoModifier,QPoint(8,8));QCOMPARE(taps.size(),3);
            }
      void fullPagingAndRevisionInvalidation() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);
            PluginAPI::Score wrapped(score.get());PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            const int end=score->lastMeasure()->endTick().ticks();const QString token=observer.analysisRevision();QVariantList notes;int next=0,pages=0;double worst=0;
            do {
                  auto page=observer.analysisRange(0,end,0,8,next,1,token);QVERIFY(!page.contains("error"));notes.append(page.value("notes").toList());
                  worst=qMax(worst,page.value("milliseconds").toDouble());next=page.value("nextTick").toInt();++pages;
                  if(page.value("done").toBool())break;
                  QVERIFY(pages<1000);
                  }while(true);
            int expected=0;Note* first=nullptr;
            for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int track=0;track<8;++track)if(auto e=segment->element(track))if(e->isChord()) {auto chord=toChord(e);expected+=int(chord->notes().size());if(!first)first=chord->notes().front();}
            QCOMPARE(notes.size(),expected);QVERIFY(pages>1);QVERIFY(first);
            const auto row=notes.first().toMap();QVERIFY(row.contains("attackIndex"));QVERIFY(row.contains("logicalId"));QVERIFY(row.contains("logicalEnd"));QVERIFY(row.contains("writtenTpc"));QVERIFY(row.contains("fingerings"));
            score->startCmd();score->undoChangePitch(first,first->pitch()+1,first->tpc1(),first->tpc2());score->endCmd();
            QCOMPARE(observer.analysisRange(0,end,0,8,0,1,token).value("error").toString(),QString("stale"));
            qInfo("P0 range paging: %d notes, %d pages, peak %.3f ms",expected,pages,worst);
            }
      void largeRangePagingBudget() {
            QFile fixture(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(fixture.open(QIODevice::ReadOnly));
            QDomDocument document;QVERIFY(document.setContent(fixture.readAll()));
            auto source=document.documentElement().firstChildElement("Score");
            for(auto staff=source.firstChildElement("Staff");!staff.isNull();staff=staff.nextSiblingElement("Staff")) {
                  auto original=staff.firstChildElement("Measure");
                  for(int bar=2;bar<=1000;++bar) {auto next=original.cloneNode(true).toElement();next.setAttribute("number",bar);auto voice=next.firstChildElement("voice");voice.removeChild(voice.firstChildElement("Clef"));voice.removeChild(voice.firstChildElement("TimeSig"));staff.appendChild(next);}
                  }
            QTemporaryDir directory;QFile file(directory.filePath("long.mscx"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(document.toByteArray());file.close();
            std::unique_ptr<MasterScore> score(mscore->readScore(file.fileName()));QVERIFY(score);
            PluginAPI::Score wrapped(score.get());PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            const int end=score->lastMeasure()->endTick().ticks();const auto contentRevision=observer.analysisRevision();int at=0,count=0,pages=0;double worst=0;QElapsedTimer timer;timer.start();
            do {const auto page=observer.analysisRange(0,end,0,8,at,64,contentRevision);QVERIFY(!page.contains("error"));count+=page.value("notes").toList().size();++pages;worst=qMax(worst,page.value("milliseconds").toDouble());if(page.value("done").toBool())break;const int next=page.value("nextTick").toInt();QVERIFY(next>at);at=next;}while(pages<10000);
            QCOMPARE(count,6000);qInfo("P0 1000 bars / 6000 complete notes: %d pages, peak %.3f ms, total %.3f ms",pages,worst,double(timer.nsecsElapsed())/1000000.);
            }
      void workerValueIsolationAndCancellation() {
            PluginAPI::ReadOnlyAnalysisJob job;QSignalSpy done(&job,&PluginAPI::ReadOnlyAnalysisJob::finished);
            QVariantMap input{{"value",7}};job.start("({answer:input.value*3})",input);QTRY_COMPARE(done.count(),1);
            QCOMPARE(done.takeFirst().first().toMap().value("answer").toInt(),21);QCOMPARE(input.value("value").toInt(),7);
            PluginAPI::ReadOnlyAnalysisJob canceled;QSignalSpy canceledDone(&canceled,&PluginAPI::ReadOnlyAnalysisJob::finished);
            canceled.start("while(true) {}",{});QTest::qWait(20);canceled.cancel();QTest::qWait(50);QCOMPARE(canceledDone.count(),0);
            }
      void midiValuePairingAndOnlyOffMoves() {
            MidiFile file;file.setDivision(480);file.setFormat(1);MidiTrack track;track.setOutPort(0);track.setOutChannel(0);
            track.insert(0,MidiEvent(ME_CONTROLLER,0,64,127));track.insert(0,MidiEvent(ME_NOTEON,0,60,85));track.insert(960,MidiEvent(ME_NOTEON,0,60,0));track.insert(1200,MidiEvent(ME_CONTROLLER,0,64,0));file.tracks().append(track);
            MidiGateSource source;source.track=0;source.on=0;source.pitch=60;source.piano=true;source.staff=0;source.voice=0;
            const auto data=midiGateSnapshot(file,{source});QCOMPARE(data.notes.size(),1);QCOMPARE(data.pedals.size(),1);QVERIFY(data.notes[0].known);
            MidiGateProcessor p;MidiGateOptions options;options.jitter=0;auto rows=p.process(data,options);QVERIFY(rows[0].newOff<960);
            applyMidiGate(file,rows);QCOMPARE(file.tracks()[0].events().size(),size_t(4));
            bool moved=false;for(const auto& pair:file.tracks()[0].events()) {const auto e=pair.second;if(e.type()==ME_NOTEON && e.velo()==0) {moved=true;QCOMPARE(pair.first,rows[0].newOff);}else if(e.type()==ME_NOTEON) {QCOMPARE(pair.first,0);QCOMPARE(e.velo(),85);}else if(e.value()==0)QCOMPARE(pair.first,1200);else QCOMPARE(pair.first,0);}
            QVERIFY(moved);
            file.tracks()[0].insert(0,MidiEvent(ME_PITCHBEND,0,1,64));
            auto bent=midiGateSnapshot(file,{source});QVERIFY(!bent.pedals[0].supported);
            QCOMPARE(p.process(bent,options)[0].newOff,bent.notes[0].off);
            }
      void nativeRenderDefaultEqualityAndPedalCrop() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1800));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            const auto state=score->state();const bool dirty=score->dirty();const auto synth=mscore->synthesizerState();
            QBuffer raw,explicitRaw,cropped;raw.open(QIODevice::WriteOnly);explicitRaw.open(QIODevice::WriteOnly);cropped.open(QIODevice::WriteOnly);
            ExportMidi a(score.get()),b(score.get()),c(score.get());QVERIFY(a.write(&raw,true,true,synth));QVERIFY(b.write(&explicitRaw,true,true,synth,MidiExportOptions()));QCOMPARE(raw.data(),explicitRaw.data());
            MidiExportOptions option;option.crop=true;option.gate.jitter=0;QVERIFY(c.write(&cropped,true,true,synth,option));
            int changed=0;for(const auto& n:c.gateReport)if(n.newOff<n.off)++changed;
            QVERIFY2(changed>0,"Native render must expose at least one safely cropped piano note");
            QBuffer baseline;baseline.open(QIODevice::WriteOnly);QVERIFY(!c.gateBaseline.write(&baseline));QCOMPARE(baseline.data(),raw.data());
            QVERIFY(score->state()==state);QCOMPARE(score->dirty(),dirty);
            qInfo("P0 native MIDI: %d safe shortened notes; normal bytes %d",changed,raw.data().size());
            }
      void actualMidiComparisonWindow() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1800));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});panel.show();
            QPushButton* button=nullptr;for(auto candidate:panel.findChildren<QPushButton*>())if(candidate->text().startsWith("Compare"))button=candidate;QVERIFY(button);
            bool ready=false;int tries=0;QTimer close;close.setInterval(50);
            connect(&close,&QTimer::timeout,&panel,[&] {
                  ++tries;for(auto widget:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<QDialog*>(widget))if(dialog->windowTitle()=="MIDI release comparison") {
                        auto table=dialog->findChild<QTableView*>();if(table && table->model()->rowCount()>0) {ready=true;table->selectRow(0);const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!output.isEmpty())dialog->grab().save(output+"/midi-compare.png");dialog->accept();}
                        else if(tries>80)dialog->reject();
                        }
                  });
            close.start();button->click();close.stop();QVERIFY(ready);panel.hide();
            }
      void currentSoundfontCropPCMComparison() {
            const QString font=qEnvironmentVariable("P0_TEST_SOUNDFONT");QVERIFY2(QFileInfo::exists(font),"Set P0_TEST_SOUNDFONT to the current piano bank");
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1800));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            ExportMidi exporter(score.get());QBuffer buffer;buffer.open(QIODevice::WriteOnly);MidiExportOptions options;options.crop=true;options.gate.jitter=0;
            QVERIFY(exporter.write(&buffer,true,true,mscore->synthesizerState(),options));
            const auto before=render(exporter.gateBaseline,exporter.gateData,font),after=render(exporter.mf,exporter.gateData,font);QVERIFY(!before.isEmpty());QCOMPARE(before.size(),after.size());
            float peak=0,difference=0;for(int i=0;i<before.size();++i) {peak=qMax(peak,qAbs(before[i]));difference=qMax(difference,qAbs(before[i]-after[i]));}
            QVERIFY(peak>.000001f);qInfo("P0 current SF3 PCM: peak %.9f, maximum before/after difference %.9f",double(peak),double(difference));
            QVERIFY2(difference/qMax(.000001f,peak)<.0001f,"Unexpected audible-scale crop difference; tighten protection rules");
            const QString output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!output.isEmpty()) {wave(output+"/original.wav",before,.8f/peak);wave(output+"/cropped.wav",after,.8f/peak);}
            }
      void actualArrangementPluginWorkerAndDock() {
            auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));mscore->show();
            const QString path=QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml";
            PluginDescription description;description.path=path;QVERIFY(collectPluginMetaInformation(&description));mscore->registerPlugin(&description);mscore->pluginTriggered(path);QTest::qWait(100);
            QmlPlugin* panel=nullptr;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))if(root->version()=="1.0.0" && root->menuPath().contains("Arrangement"))panel=root;
            QVERIFY(panel);QVERIFY(!panel->property("workerSource").toString().isEmpty());
            QVERIFY(QMetaObject::invokeMethod(panel,"start",Q_ARG(QVariant,QVariant(false))));QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool(),10000);
            QVERIFY(!panel->property("running").toBool());QCOMPARE(panel->property("status").toString(),QString::fromUtf8("已更新"));
            qInfo("P0 native arrangement peak GUI batch %.3f ms",panel->property("worstBatch").toDouble());
            panel->setPanelFloating(true);QVERIFY(panel->panelFloating());panel->setPanelFloating(false);
            }
      void cleanupTestCase() {mscore->hide();}
      };
QTEST_MAIN(TestP0Native)
#include "tst_p0native.moc"
