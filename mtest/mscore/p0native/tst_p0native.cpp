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
#include <QMenu>
#include "mscore/workspace.h"
#include "libmscore/xml.h"
#include "mscore/midicrop/midicroppanel.h"
#include <QQuickView>
#include "mtest/testutils.h"
#include "mscore/musescore.h"
#include "mscore/preferences.h"
#include "mscore/inputrhythmpreference.h"
#include "libmscore/inputnotation.h"
#include "libmscore/inputrhythm.h"
#include "mscore/scoreview.h"
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
#include "libmscore/pianohands.h"
#include "libmscore/handbracket.h"
#include "libmscore/symbol.h"
#include <QJSEngine>
#include <QRegularExpression>
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
      void notationMenusAndPreference()
            {
            auto join=mscore->findChild<QAction*>("auto-tie-beams");auto stems=mscore->findChild<QAction*>("coherent-tie-stems");
            QVERIFY(join);QVERIFY(stems);QVERIFY(!join->isChecked());QVERIFY(stems->isChecked());
            auto menu=Workspace::findMenuFromString("menu-tools");QVERIFY(menu);QVERIFY(menu->actions().contains(join));
            menu->popup(QPoint(20,20));QTest::qWait(30);menu->setActiveAction(join);QTest::qWait(20);
            QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(join).center());
            QTRY_VERIFY(join->isChecked());QVERIFY(InputNotation::tiedBeams());QVERIFY(preferences.getBool(PREF_SCORE_TIED_BEAMS));
            join->trigger();QVERIFY(!InputNotation::tiedBeams());QVERIFY(!preferences.getBool(PREF_SCORE_TIED_BEAMS));
            stems->trigger();QVERIFY(!InputNotation::coherentStems());QVERIFY(!preferences.getBool(PREF_SCORE_COHERENT_TIE_STEMS));
            stems->trigger();QVERIFY(InputNotation::coherentStems());QVERIFY(preferences.getBool(PREF_SCORE_COHERENT_TIE_STEMS));
            }
      void tapUsesSharedActionAndFixedGeometry() {
            auto action=getAction("tap-tempo");QVERIFY(action);QVERIFY(!action->autoRepeat());
            QToolButton* button=nullptr;for(auto candidate:mscore->findChildren<QToolButton*>())if(candidate->defaultAction()==action && candidate->menu())button=candidate;
            QVERIFY(button);const int width=button->width();action->trigger();QTest::qWait(65);action->trigger();QCOMPARE(button->width(),width);QVERIFY(!button->text().contains("TAP",Qt::CaseInsensitive));QVERIFY(!button->icon().isNull());
            QSignalSpy taps(action,&QAction::triggered);QTest::keyPress(button,Qt::Key_Space);QCOMPARE(taps.size(),1);
            QKeyEvent repeat(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier,QString(),true);QApplication::sendEvent(button,&repeat);QCOMPARE(taps.size(),1);
            QTest::keyRelease(button,Qt::Key_Space);QCOMPARE(taps.size(),1);
            QTest::mouseClick(button,Qt::LeftButton,Qt::NoModifier,QPoint(8,8));QTest::mouseDClick(button,Qt::LeftButton,Qt::NoModifier,QPoint(8,8));QCOMPARE(taps.size(),3);
            }
      void savedWorkspaceTapMigrationAndCustomization() {
            auto action=getAction("tap-tempo");auto tools=Workspace::findMenuFromString("menu-tools");QVERIFY(tools);tools->removeAction(action);
            auto restore=[](int version) {
                  const auto content=QString("<Workspace><uiVersion>%1</uiVersion><Toolbar name=\"playbackControl\"><action>play</action><action>independent-metronome</action><action>metronome</action></Toolbar></Workspace>").arg(version).toUtf8();
                  XmlReader reader(content);QVERIFY(reader.readNextStartElement());Workspace workspace;workspace.read(reader);
                  };
            restore(3);tools=Workspace::findMenuFromString("menu-tools");qInfo()<<"Tap migration mapping"<<action<<tools;for(auto entry:tools->actions())qInfo()<<entry<<entry->data()<<entry->text();QVERIFY(tools->actions().contains(action));
            const auto entries=mscore->playbackControlEntries();int position=0,tap=-1,metronome=-1,count=0;
            for(auto entry:*entries) {if(QString(entry)=="tap-tempo") {tap=position;++count;}if(QString(entry)=="independent-metronome")metronome=position;++position;}
            QCOMPARE(count,1);QCOMPARE(tap,metronome+1);
            restore(4);for(auto entry:*entries)QVERIFY(QString(entry)!="tap-tempo");
            mscore->setPlaybackControlEntries(mscore->allPlaybackControlEntries());mscore->populatePlaybackControls();
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
      void midiClockActualSecondsAndInverse() {
            MidiGateData data;data.tempos={{0,500000.},{480,1000000.}};MidiGateProcessor clock;clock.setClock(data);QCOMPARE(clock.durationMs(0,480),500.);QCOMPARE(clock.durationMs(0,960),1500.);QCOMPARE(clock.tickForMilliseconds(750.),600);QCOMPARE(clock.tickForMilliseconds(1500.),960);
            }
      void nativeHandRangeAssignment() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/performanceeditor/freeslur.mscx"));QVERIFY(score);auto measure=score->firstMeasure();
            auto at=[&](int tick){return toChord(measure->findSegment(SegmentType::ChordRest,Fraction::fromTicks(tick))->element(0))->downNote();};
            auto add=[&](int tick,SymId id){auto note=at(tick);auto symbol=new Symbol(score.get());symbol->setSym(id);symbol->setParent(note);symbol->setTrack(note->track());score->undoAddElement(symbol);return symbol;};
            score->startCmd();add(0,SymId::keyboardPlayWithLH);add(960,SymId::keyboardPlayWithLHEnd);score->endCmd();auto values=HandBracket::assignments(score.get());QCOMPARE(values.value(at(480),-1),0);QCOMPARE(values.value(at(1440),-1),-1);
            score->startCmd();add(1440,SymId::keyboardPlayWithRHEnd);score->endCmd();values=HandBracket::assignments(score.get());QCOMPARE(values.value(at(1440),-1),1);
            score->startCmd();add(480,SymId::keyboardPlayWithRH);score->endCmd();values=HandBracket::assignments(score.get());QCOMPARE(values.value(at(480),-1),-2);
            }
      void nativeLongRootReleaseAcrossVoices() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1920));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            auto state=score->state();const bool dirty=score->dirty();MidiExportOptions options;options.crop=true;options.gate.jitter=0;QBuffer output;output.open(QIODevice::WriteOnly);ExportMidi exporter(score.get());QVERIFY(exporter.write(&output,true,true,mscore->synthesizerState(),options));
            bool root=false;int next=-1;for(const auto& n:exporter.gateReport)if(n.pitch==60)next=n.on;QVERIFY(next>0);
            for(const auto& n:exporter.gateReport)if(n.pitch==36) {root=true;QCOMPARE(n.hand,0);QCOMPARE(n.reason,QString("hand-release"));QVERIFY(n.newOff<next);QVERIFY(n.beforeSpan>n.afterSpan);}
            QVERIFY(root);QVERIFY(score->state()==state);QCOMPARE(score->dirty(),dirty);
            MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});auto compare=panel.findChild<QPushButton*>("midiCropCompare");QVERIFY(compare);QTimer timer;timer.setInterval(30);int tries=0;bool clicked=false;
            connect(&timer,&QTimer::timeout,&panel,[&] {
                  auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(++tries>150){dialog->reject();return;}auto table=dialog->findChild<QTableView*>();auto roll=dialog->findChild<QAbstractScrollArea*>("midiCropRoll");if(!table || !roll || table->model()->rowCount()==0)return;
                  int rootRow=-1;for(int i=0;i<table->model()->rowCount();++i)if(table->model()->data(table->model()->index(i,0)).toString()=="C2")rootRow=i;if(rootRow<0)return;
                  table->selectRow(rootRow);const auto hit=roll->property("selectedNewOffHandle").toPoint()-QPoint(3,0);table->setCurrentIndex(QModelIndex());timer.stop();QVERIFY(roll->viewport()->rect().contains(hit));QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,hit);QCOMPARE(table->currentIndex().row(),rootRow);clicked=true;timer.stop();dialog->grab().save("midi-hand-release.png");dialog->accept();QVERIFY(table->model()->data(table->model()->index(rootRow,5)).toString().contains(QString::fromUtf8("提前松手")));
                  });
            timer.start();QTest::mouseClick(compare,Qt::LeftButton);timer.stop();QVERIFY(clicked);

            }
      void midiHandSettingsAndPersistence() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);
            MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});auto settings=panel.findChild<QPushButton*>("midiCropSettingsButton");QVERIFY(settings);bool edited=false;
            QTimer::singleShot(30,&panel,[&] {
                  auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);QCOMPARE(dialog->objectName(),QString("midiCropSettings"));auto lead=dialog->findChild<QSpinBox*>("cropReleaseLead");QVERIFY(lead);lead->setValue(75);
                  auto hands=dialog->findChild<QPushButton*>("cropHandLimitsButton");QVERIFY(hands);
                  QTimer::singleShot(30,&panel,[&] {
                        auto inner=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(inner);QCOMPARE(inner->objectName(),QString("cropHandLimits"));auto maximum=inner->findChild<QSpinBox*>("cropHand0Value1");QVERIFY(maximum);maximum->setValue(16);auto buttons=inner->findChild<QDialogButtonBox*>();QVERIFY(buttons);QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);edited=true;
                        });
                  QTest::mouseClick(hands,Qt::LeftButton);QVERIFY(edited);auto buttons=dialog->findChild<QDialogButtonBox*>();QVERIFY(buttons);QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);
                  });
            QTest::mouseClick(settings,Qt::LeftButton);QVERIFY(edited);QCOMPARE(panel.options(score.get()).gate.releaseLeadMs,75.);QCOMPARE(panel.options(score.get()).gate.hands[0].maximum,16);
            MidiCropPanel reopened(nullptr,[&]{return QList<Score*>{score.get()};});QCOMPARE(reopened.options(score.get()).gate.releaseLeadMs,75.);QCOMPARE(reopened.options(score.get()).gate.hands[0].maximum,16);
            QSettings settingsStore;settingsStore.remove("export/midi/releaseLeadMs");settingsStore.remove("export/midi/hand0");settingsStore.remove("export/midi/hand1");settingsStore.sync();
            }
      void pianoHandsValuePlanning() {
            PianoHands::Limits limits[2];PianoHands::Position positions[2];
            QVector<PianoHands::Key> keys={{0,36,0,0,false,false},{1,60,0,0,true,false},{2,64,0,0,true,false},{3,67,0,0,true,false}};
            auto strict=PianoHands::plan(keys,limits,positions,.5);QVERIFY(!strict.possible);QVERIFY(strict.released.isEmpty());
            keys[0].releasable=true;auto pedal=PianoHands::plan(keys,limits,positions,.5);QVERIFY(pedal.possible);QCOMPARE(pedal.released,QVector<int>{0});QCOMPARE(pedal.before[0].high-pedal.before[0].low,31);QCOMPARE(pedal.after[0].high-pedal.after[0].low,7);
            QVector<PianoHands::Key> white={{0,60,1,1,true,false},{1,76,1,1,true,false}};QVERIFY(PianoHands::plan(white,limits,positions,0).possible);white[0].pitch=61;white[1].pitch=77;QVERIFY(!PianoHands::plan(white,limits,positions,0).possible);
            QVector<PianoHands::Key> unison={{0,60,1,1,true,false},{1,60,1,1,true,false}};auto doubled=PianoHands::plan(unison,limits,positions,0);QCOMPARE(doubled.after[1].count,1);QVERIFY(doubled.possible);
            unison[1].hand=0;QVERIFY(PianoHands::plan(unison,limits,positions,0).uncertain);
            QVector<PianoHands::Key> fingers={{0,60,1,1,false,true,1},{1,64,1,1,true,false,1}};auto reuse=PianoHands::plan(fingers,limits,positions,.5);QVERIFY(reuse.possible);QCOMPARE(reuse.released,QVector<int>{0});fingers[0].releasable=false;QVERIFY(!PianoHands::plan(fingers,limits,positions,.5).possible);
            QVector<PianoHands::Key> impossible={{0,60,1,1,true,false},{1,84,1,1,true,false}};auto onset=PianoHands::plan(impossible,limits,positions,0);QVERIFY(!onset.possible);QVERIFY(onset.released.isEmpty());
            QVector<PianoHands::Key> layered={{0,60,0,0,false,true},{1,61,0,0,false,true},{2,62,0,0,true,false},{3,76,0,0,true,false},{4,78,0,0,false,true},{5,79,0,0,false,true}};auto plateau=PianoHands::plan(layered,limits,positions,.5);QVERIFY(plateau.possible);QCOMPARE(plateau.after[0].high-plateau.after[0].low,16);QCOMPARE(plateau.released.size(),2);QVERIFY(!plateau.released.contains(2) && !plateau.released.contains(3));
            QVector<PianoHands::Key> flexible={{0,36,0,0,false,true},{1,60,-1,0,true,false},{2,64,-1,0,true,false},{3,67,-1,0,true,false},{4,84,-1,1,true,false}};auto hands=PianoHands::plan(flexible,limits,positions,.5);QVERIFY(hands.possible);QVERIFY(hands.released.contains(0));QCOMPARE(hands.hands[4],1);
            }
      void midiConflictReleaseAndGroupedVariation() {
            MidiGateData data;data.pedals={{0,0,0,1900,true}};
            auto note=[](QString id,int pitch,int on,int off,int hand) {MidiGateNote n;n.id=id;n.pitch=pitch;n.on=on;n.off=off;n.hand=hand;n.handHint=hand;n.partId=0;n.staff=hand==0?1:0;n.voice=hand==0?4:0;n.known=n.piano=true;n.offOrdinal=pitch;n.sourceTick=on;return n;};
            data.notes={note("old-root",36,0,1800,0),note("new-c",60,480,960,0),note("new-e",64,480,960,0),note("new-g",67,480,960,0),note("melody",84,480,960,1)};
            MidiGateOptions options;options.jitter=0;MidiGateProcessor processor;auto rows=processor.process(data,options);QCOMPARE(rows[0].reason,QString("hand-release"));QVERIFY(rows[0].newOff<480);QVERIFY(rows[0].newOff>=116);QCOMPARE(rows[0].beforeSpan,31);QCOMPARE(rows[0].afterSpan,7);
            options.protectedTracks={4};auto protectedRows=processor.process(data,options);QCOMPARE(protectedRows[0].newOff,1800);QVERIFY(!protectedRows[1].handWarning.isEmpty());options.protectedTracks.clear();
            data.notes[0].off=2000;auto boundary=processor.process(data,options);QCOMPARE(boundary[0].newOff,2000);QVERIFY(boundary[0].reason!="hand-release");data.notes[0].off=1800;
            data.notes={note("fresh-low",60,0,960,1),note("fresh-high",84,0,960,1)};auto impossible=processor.process(data,options);QVERIFY(impossible[0].handWarning.contains(QString::fromUtf8("这组新音")));QVERIFY(impossible[1].handWarning.contains(QString::fromUtf8("调整跨度")));
            data.notes={note("chord-c",60,0,960,1),note("chord-e",64,0,960,1),note("chord-g",67,0,960,1)};options.jitter=.02;auto varied=processor.process(data,options),again=processor.process(data,options);int low=10000,high=0;for(int i=0;i<varied.size();++i){QCOMPARE(varied[i].newOff,again[i].newOff);low=qMin(low,varied[i].newOff);high=qMax(high,varied[i].newOff);QCOMPARE(varied[i].on,data.notes[i].on);QCOMPARE(varied[i].pitch,data.notes[i].pitch);}QVERIFY(high-low<=5);
            options.seed+=97;auto different=processor.process(data,options);bool changed=false;for(int i=0;i<varied.size();++i)changed|=different[i].newOff!=varied[i].newOff;QVERIFY(changed);
            }
      void continuousHandChoiceAndChordRelease() {
            PianoHands::Limits limits[2];PianoHands::Position positions[2];
            QVector<PianoHands::Key> current={{0,60,-1,0,true,false}};
            PianoHands::Future next;next.seconds=.25;next.keys={{0,60,-1,0,false,false},{1,36,0,0,true,false}};
            auto local=PianoHands::plan(current,limits,positions,0),context=PianoHands::planWithContext(current,limits,positions,0,{next});
            QCOMPARE(local.hands[0],0);QCOMPARE(context.hands[0],1);QVERIFY(context.possible);
            current[0].hand=0;QCOMPARE(PianoHands::planWithContext(current,limits,positions,0,{next}).hands[0],0);
            auto note=[](QString id,int pitch,int on,int off,int hand,int voice){MidiGateNote n;n.id=id;n.pitch=pitch;n.on=on;n.off=off;n.hand=hand;n.handHint=hand<0?0:hand;n.partId=0;n.staff=0;n.voice=voice;n.known=n.piano=true;n.offOrdinal=pitch;n.sourceTick=on;return n;};
            MidiGateData data;data.notes={note("held",60,0,960,-1,0),note("low",36,240,480,0,4)};
            MidiGateOptions options;options.jitter=0;MidiGateProcessor processor;auto assigned=processor.process(data,options);
            QCOMPARE(assigned[0].hand,1);QCOMPARE(assigned[1].hand,0);QVERIFY(assigned[1].handWarning.isEmpty());
            data.pedals={{0,0,0,2500,true}};
            data.notes={note("c",60,0,960,1,0),note("e",64,0,1440,1,1),note("g",67,0,1920,1,2)};
            auto grouped=processor.process(data,options);QCOMPARE(grouped[0].newOff,grouped[1].newOff);QCOMPARE(grouped[1].newOff,grouped[2].newOff);
            data.notes.append(note("next",65,480,960,1,3));auto followed=processor.process(data,options);
            for(int i=0;i<3;++i){QVERIFY(followed[i].newOff<480);QCOMPARE(followed[i].newOff,followed[0].newOff);}
            options.jitter=.02;auto varied=processor.process(data,options);int low=10000,high=0;for(int i=0;i<3;++i){low=qMin(low,varied[i].newOff);high=qMax(high,varied[i].newOff);}QVERIFY(high-low<=3);
            }
      void pianoHandsWorkerAndRules() {
            PluginAPI::ReadOnlyAnalysisJob job;QSignalSpy done(&job,&PluginAPI::ReadOnlyAnalysisJob::finished);
            QVariantList notes={QVariantMap{{"id","old"},{"pitch",36},{"track",4},{"attackTick",0},{"logicalEnd",1200},{"onSeconds",0.},{"analysisHand","left"},{"handKnown",true}},QVariantMap{{"id","new"},{"pitch",72},{"track",4},{"attackTick",480},{"logicalEnd",960},{"onSeconds",.5},{"analysisHand","left"},{"handKnown",true}}};
            QVariantList pedals={QVariantMap{{"tick",0},{"end",1500},{"firstTrack",0},{"endTrack",8}}};QVariantMap input{{"notes",notes},{"pedals",pedals}};
            job.start("(function(){PianoHands.analyse([input.notes[0]],input.pedals,{tick:0,seconds:0,part:0});PianoHands.analyse([{id:'other',pitch:84,track:8,attackTick:0,logicalEnd:960,onSeconds:0,analysisHand:'right',handKnown:true}],[],{tick:0,seconds:0,part:8});return PianoHands.analyse(input.notes,input.pedals,{tick:480,seconds:0.5,part:0});})()",input);QTRY_COMPARE(done.count(),1);auto result=done.takeFirst().first().toMap();QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));QVERIFY(result.value("possible").toBool());auto sides=result.value("sides").toList();QCOMPARE(sides[0].toMap().value("released").toList().size(),1);QCOMPARE(sides[0].toMap().value("travel").toInt(),20);QCOMPARE(notes.size(),2);
            }
      void workerValueIsolationAndCancellation() {
            PluginAPI::ReadOnlyAnalysisJob job;QSignalSpy done(&job,&PluginAPI::ReadOnlyAnalysisJob::finished);
            QVariantMap input{{"value",7}};job.start("({answer:input.value*3})",input);QTRY_COMPARE(done.count(),1);
            QCOMPARE(done.takeFirst().first().toMap().value("answer").toInt(),21);
            QJSEngine packer;QVariantMap sources;for(const auto& entry:QVector<QPair<QString,QString>>{{"harmony","HarmonyAssistant/Harmony.js"},{"timeline","HarmonyAssistant/Timeline.js"},{"rules","ArrangementAssistant/Rules.js"}}) {QFile file(QString(TESTROOT)+"/share/plugins/"+entry.second);QVERIFY(file.open(QIODevice::ReadOnly));sources[entry.first]=QString::fromUtf8(file.readAll());}
            auto rules=sources.value("rules").toString();rules.remove(QRegularExpression("^\\.(pragma|import).*?$",QRegularExpression::MultilineOption));QVERIFY(!packer.evaluate(rules).isError());packer.globalObject().setProperty("sources",packer.toScriptValue(sources));auto bundle=packer.evaluate("workerBundle(sources)");QVERIFY(!bundle.isError());
            QVariantMap config=packer.evaluate("defaults()").toVariant().toMap();config["roles"]=QVariantMap{{"4",QVariantMap{{"hand","left"}}}};
            QVariantList musicalNotes={QVariantMap{{"id","old"},{"pitch",36},{"tpc",14},{"track",4},{"index",0},{"tick",0},{"attackTick",0},{"end",1200},{"logicalEnd",1200},{"onSeconds",0.},{"endSeconds",1.25}},QVariantMap{{"id","new"},{"pitch",72},{"tpc",14},{"track",4},{"index",0},{"tick",480},{"attackTick",480},{"end",960},{"logicalEnd",960},{"onSeconds",.5},{"endSeconds",1.}}};
            QVariantMap musicalData{{"notes",musicalNotes},{"pedals",QVariantList{QVariantMap{{"tick",0},{"end",1500},{"firstTrack",0},{"endTrack",8}}}},{"scope",QVariantMap{{"start",0},{"end",1500},{"firstTrack",0},{"endTrack",8}}}};
            PluginAPI::ReadOnlyAnalysisJob rulesJob;QSignalSpy rulesDone(&rulesJob,&PluginAPI::ReadOnlyAnalysisJob::finished);rulesJob.start(bundle.toString(),{{"data",musicalData},{"config",config},{"imported",QVariantList{}}});QTRY_COMPARE(rulesDone.count(),1);const auto checked=rulesDone.takeFirst().first().toMap();QVERIFY2(!checked.contains("error"),qPrintable(checked.value("error").toString()));bool released=false;for(const auto& issue:checked.value("issues").toList())released|=issue.toMap().value("code").toString()=="release-left";QVERIFY(released);
QCOMPARE(input.value("value").toInt(),7);
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
            MidiFile binary;binary.setDivision(480);binary.setFormat(1);MidiTrack switched;switched.setOutPort(0);switched.setOutChannel(0);
            switched.insert(0,MidiEvent(ME_CONTROLLER,0,64,100));switched.insert(0,MidiEvent(ME_NOTEON,0,60,85));switched.insert(960,MidiEvent(ME_NOTEOFF,0,60,0));switched.insert(1200,MidiEvent(ME_CONTROLLER,0,64,0));binary.tracks().append(switched);
            auto onOff=midiGateSnapshot(binary,{source});QVERIFY(onOff.pedals[0].supported);QVERIFY(p.process(onOff,options)[0].newOff<960);
            binary.tracks()[0].insert(400,MidiEvent(ME_CONTROLLER,0,64,80));auto continuous=midiGateSnapshot(binary,{source});QVERIFY(!continuous.pedals[0].supported);QCOMPARE(p.process(continuous,options)[0].newOff,960);

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
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1920));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});panel.show();
            QPushButton* button=nullptr;for(auto candidate:panel.findChildren<QPushButton*>())if(candidate->objectName()=="midiCropCompare")button=candidate;QVERIFY(button);
            bool ready=false,releaseShown=false;int tries=0;QTimer close;close.setInterval(50);
            connect(&close,&QTimer::timeout,&panel,[&] {
                  ++tries;for(auto widget:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<QDialog*>(widget))if(dialog->objectName()=="midiCropComparison") {
                        auto table=dialog->findChild<QTableView*>();if(table && table->model()->rowCount()>0) {ready=true;int selected=-1;for(int row=0;row<table->model()->rowCount();++row)if(table->model()->index(row,5).data().toString().contains(QString::fromUtf8("为弹下一组音提前松手")))selected=row;releaseShown=selected>=0;table->selectRow(qMax(0,selected));const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!output.isEmpty())dialog->grab().save(output+"/midi-compare.png");dialog->accept();}
                        else if(tries>80)dialog->reject();
                        }
                  });
            close.start();button->click();close.stop();QVERIFY(ready);QVERIFY(releaseShown);panel.hide();
            }
      void currentSoundfontCropPCMComparison() {
            const QString font=qEnvironmentVariable("P0_TEST_SOUNDFONT");QVERIFY2(QFileInfo::exists(font),"Set P0_TEST_SOUNDFONT to the current piano bank");
            for(const auto& fixture:QStringList{"scoreobserver/piano.mscx","p0native/longroot.mscx"}) {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/"+fixture));QVERIFY(score);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1920));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            ExportMidi exporter(score.get());QBuffer buffer;buffer.open(QIODevice::WriteOnly);MidiExportOptions options;options.crop=true;options.gate.jitter=0;
            QVERIFY(exporter.write(&buffer,true,true,mscore->synthesizerState(),options));
            const auto before=render(exporter.gateBaseline,exporter.gateData,font),after=render(exporter.mf,exporter.gateData,font);QVERIFY(!before.isEmpty());QCOMPARE(before.size(),after.size());
            float peak=0,difference=0;for(int i=0;i<before.size();++i) {peak=qMax(peak,qAbs(before[i]));difference=qMax(difference,qAbs(before[i]-after[i]));}
            QVERIFY(peak>.000001f);qInfo("P0 current SF3 %s PCM: peak %.9f, maximum before/after difference %.9f",qPrintable(fixture),double(peak),double(difference));
            QVERIFY2(difference/qMax(.000001f,peak)<.0001f,"Unexpected audible-scale crop difference; tighten protection rules");
            const QString output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!output.isEmpty()) {const auto name=QFileInfo(fixture).completeBaseName();wave(output+"/"+name+"-original.wav",before,.8f/peak);wave(output+"/"+name+"-cropped.wav",after,.8f/peak);}
            }
            }
      void actualHarmonyWorkerCacheAndHideCancellation() {
            auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));auto note=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(4))->downNote();score->select(note);mscore->show();
            const QString path=QString(TESTROOT)+"/share/plugins/HarmonyAssistant/HarmonyAssistant_MS3.qml";PluginDescription description;description.path=path;QVERIFY(collectPluginMetaInformation(&description));mscore->registerPlugin(&description);mscore->pluginTriggered(path);QTest::qWait(100);
            QmlPlugin* panel=nullptr;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))if(root->menuPath()=="Plugins.Harmony Assistant")panel=root;QVERIFY(panel);QVERIFY(!panel->property("regionWorkerSource").toString().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);auto regions=panel->property("automaticRegions").value<QJSValue>().toVariant().toList();if(regions.isEmpty())regions=panel->property("automaticRegions").toList();QVERIFY(!regions.isEmpty());QCOMPARE(regions.first().toMap().value("root").toInt(),0);
            const auto source=panel->property("regionWorkerSource");panel->setProperty("regionWorkerSource",QString("while(true){}"));panel->setProperty("analysisDirty",true);QVERIFY(QMetaObject::invokeMethod(panel,"scheduleAnalysis"));QTRY_VERIFY_WITH_TIMEOUT(panel->property("regionJobToken").toInt()>0,10000);
            panel->setProperty("visible",false);QVERIFY(panel->summaryPanelVisible());QVERIFY(panel->property("regionJobToken").toInt()>0);panel->showSummaryPanel(false);panel->showDetailPanel(false);QTRY_COMPARE(panel->property("regionJobToken").toInt(),0);QTest::qWait(50);panel->setProperty("regionWorkerSource",source);panel->setProperty("visible",true);QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
            }
      void actualArrangementPluginWorkerAndDock() {
            mscore->resize(1280,900);
            auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));mscore->show();
            const QString path=QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml";
            PluginDescription description;description.path=path;QVERIFY(collectPluginMetaInformation(&description));mscore->registerPlugin(&description);mscore->pluginTriggered(path);QTest::qWait(100);
            QmlPlugin* panel=nullptr;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))if(root->menuPath()=="Plugins.Arrangement Check")panel=root;
            QVERIFY(panel);QVERIFY(!panel->property("workerSource").toString().isEmpty());
            QVERIFY(QMetaObject::invokeMethod(panel,"start",Q_ARG(QVariant,QVariant(false))));QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool(),10000);
            QVERIFY(!panel->property("running").toBool());QCOMPARE(panel->property("status").toString(),QString::fromUtf8("已更新"));
            qInfo("P0 native arrangement peak GUI batch %.3f ms",panel->property("worstBatch").toDouble());
            auto observer=qobject_cast<PluginAPI::ScoreObserver*>(panel->property("observer").value<QObject*>());QVERIFY(observer);
            auto notes=observer->snapshot(0,0,score->ntracks(),false)["notes"].toList();QVERIFY(!notes.isEmpty());auto anchor=notes.first().toMap();
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver harmony;harmony.setScore(&wrapped);auto chord=anchor;chord["chord"]="C";chord["chordTick"]=0;chord["onTop"]=true;harmony.setScorePreview({chord});
            auto view=mscore->currentScoreView();QCOMPARE(view->notePreviewMarkerBoxes().size(),1);const auto harmonyBox=view->notePreviewMarkerBoxes().first();
            auto config=panel->property("config").value<QJSValue>().toVariant().toMap();config["scoreHints"]=true;panel->setProperty("config",config);
            panel->setProperty("issues",QVariantList{QVariantMap{{"id","hint"},{"type","playability"},{"code","release-left"},{"tick",0},{"bar",1},{"beat",1},{"severity",QString::fromUtf8("提示")},{"title",QString::fromUtf8("左手先松开C2，再弹下一组音")},{"basis",QString::fromUtf8("踏板继续保持低音。")},{"notes",QVariantList{anchor}}}});
            auto list=panel->findChild<QQuickItem*>("arrangementIssues");QVERIFY(list);list->setProperty("currentIndex",0);QVERIFY(QMetaObject::invokeMethod(panel,"publishMarkers"));
            auto boxes=view->notePreviewMarkerBoxes();QCOMPARE(boxes.size(),2);QVERIFY(!boxes[0].intersects(boxes[1]));
            QRectF hint;for(const auto& box:boxes)if(box!=harmonyBox)hint=box;QVERIFY(!hint.isEmpty());
            view->adjustCanvasPosition(toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(anchor["track"].toInt()))->upNote(),false);QTest::qWait(40);
            QVERIFY(view->width()>300 && view->height()>200);const auto point=view->toPhysical(hint.center()).toPoint();QVERIFY(view->rect().contains(point));
            QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,point);auto nearby=panel->findChild<QQuickWindow*>("arrangementProblemHint");QTRY_VERIFY(nearby&&nearby->isVisible());QCOMPARE(panel->property("current").value<QJSValue>().toVariant().toMap().value("id").toString(),QString("hint"));
            const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!output.isEmpty()){const auto image=view->grab(view->toPhysical(score->firstMeasure()->canvasBoundingRect().united(hint)).adjusted(-45,-25,45,25).intersected(view->rect()));QVERIFY(!image.isNull());QVERIFY(image.save(output+"/arrangement-hint.png"));mscore->grab().save(output+"/arrangement-main.png");QTRY_VERIFY(panel->window()->width()>300);QVERIFY(panel->window()->grabWindow().save(output+"/arrangement-panel.png"));}
            config["scoreHints"]=false;panel->setProperty("config",config);QVERIFY(QMetaObject::invokeMethod(panel,"publishMarkers"));QCOMPARE(view->notePreviewMarkerBoxes().size(),1);
            panel->setPanelFloating(true);QVERIFY(panel->panelFloating());panel->setPanelFloating(false);
            }
      void cleanupTestCase() {mscore->hide();}
            void notationPictures()
            {
            auto previousView=mscore->currentScoreView();const bool previouslyVisible=mscore->isVisible();
            auto sample=mscore->readScore(QString(TESTROOT)+"/mtest/libmscore/inputrhythm/blank.mscx");QVERIFY(sample);
            mscore->setCurrentScoreView(mscore->appendScore(sample));InputRhythm::setEnabled(true);
            auto& input=sample->inputState();input.setTrack(0);input.setSegment(sample->firstMeasure()->first(SegmentType::ChordRest));input.setLastSegment(input.segment());input.setNoteEntryMode(true);
            for(auto pair:QVector<QPair<int,int>>{{360,67},{360,71},{240,74}}){input.setDuration(TDuration(Fraction::fromTicks(pair.first),true));NoteVal value(pair.second);sample->startCmd();QVERIFY(sample->addPitch(value,false));sample->endCmd();}
            input.setNoteEntryMode(false);auto view=mscore->currentScoreView();QVERIFY(view);mscore->resize(1100,700);mscore->show();view->resize(950,500);view->show();
            for(bool coherent:{false,true}){InputNotation::setCoherentStems(coherent);sample->setLayoutAll();sample->doLayout();sample->update();mscore->endCmd();QTest::qWait(30);
                  QCOMPARE(view->score(),static_cast<Score*>(sample));view->setOffset(0,0);
                  auto chord=toChord(sample->findCR(Fraction(),0));QVERIFY(chord);view->adjustCanvasPosition(chord->upNote(),false);QTest::qWait(30);
                  const auto image=view->grab();QVERIFY(!image.isNull());const auto pixels=image.toImage();int ink=0;
                  for(int y=0;y<pixels.height();++y)for(int x=0;x<pixels.width();++x){const auto color=pixels.pixelColor(x,y);if(color.red()<80&&color.green()<80&&color.blue()<80)++ink;}
                  QVERIFY2(ink>500,"Screenshot must contain the score, not just a blank page");
                  QVERIFY(image.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path())).filePath(coherent?"input-332-coherent.png":"input-332-native.png")));}
            mscore->setCurrentScoreView(previousView);if(!previouslyVisible)mscore->hide();
            }
};
QTEST_MAIN(TestP0Native)
#include "tst_p0native.moc"
