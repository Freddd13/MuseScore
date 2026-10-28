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
#include "mscore/exportdialog.h"
#include "mscore/midicrop/gateaudition.h"
#include <QScopeGuard>
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
#include "libmscore/fingering.h"
#include "libmscore/stafftext.h"
#include <QJSEngine>
#include <QRegularExpression>
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/measure.h"
#include "libmscore/undo.h"
#include "libmscore/pedal.h"
#include "libmscore/pedalplayback.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include "libmscore/synthesizerstate.h"
#include "libmscore/tie.h"
#include "libmscore/tempo.h"
#include "libmscore/lyrics.h"
#include "libmscore/rehearsalmark.h"
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
      void midiRepeatHeaderUsesRequestedPolicy() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);
            score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);score->tempomap()->setTempo(0,2.);score->tempomap()->setRelTempo(1.);score->tempomap()->setTempo(960,1.5);score->tempomap()->setPause(480,.25);
            const auto state=score->state();const auto undo=score->undoStack()->getCurIdx();const auto dirty=score->dirty();
            for(bool repeat:{false,true})for(bool written:{false,true}){
                  MidiExportOptions options;options.writtenPedal=written;QByteArray expected;
                  score->setExpandRepeats(repeat);{ExportMidi exporter(score.get());QBuffer buffer(&expected);buffer.open(QIODevice::WriteOnly);QVERIFY(exporter.write(&buffer,repeat,true,mscore->synthesizerState(),options));}
                  score->setExpandRepeats(!repeat);ExportMidi exporter(score.get());exporter.collectGateData=true;QBuffer buffer;buffer.open(QIODevice::WriteOnly);QVERIFY(exporter.write(&buffer,repeat,true,mscore->synthesizerState(),options));
                  qInfo()<<"Requested repeat / written pedal"<<repeat<<written<<"expected / actual bytes"<<expected.size()<<buffer.size();
                  QFile saved(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+QString("/repeat-policy-%1-%2.mid").arg(repeat).arg(written));QVERIFY(saved.open(QIODevice::WriteOnly));saved.write(buffer.data());saved.close();
                  QCOMPARE(exporter.gateData.expandRepeats,repeat);QVERIFY(exporter.gateData.exportRPNs);
                  QCOMPARE(buffer.data(),expected);QCOMPARE(score->expandRepeats(),!repeat);QCOMPARE(score->state(),state);QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(score->dirty(),dirty);
                  }
            }
      void midiExportDialogUsesCurrentChoices() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);
            const bool oldRepeat=preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),oldRpn=preferences.getBool(PREF_IO_MIDI_EXPORTRPNS);
            auto restore=qScopeGuard([&]{preferences.setPreference(PREF_IO_MIDI_EXPANDREPEATS,oldRepeat);preferences.setPreference(PREF_IO_MIDI_EXPORTRPNS,oldRpn);});
            preferences.setPreference(PREF_IO_MIDI_EXPANDREPEATS,false);preferences.setPreference(PREF_IO_MIDI_EXPORTRPNS,false);
            ExportDialog dialog(score.get(),mscore);dialog.show();dialog.fileTypeComboBox->setCurrentIndex(7);QTest::qWait(80);
            QVERIFY(dialog.midiExpandRepeats->isVisible());QTest::mouseClick(dialog.midiExpandRepeats,Qt::LeftButton,Qt::NoModifier,QPoint(8,dialog.midiExpandRepeats->height()/2));QTest::mouseClick(dialog.midiExportRPNs,Qt::LeftButton,Qt::NoModifier,QPoint(8,dialog.midiExportRPNs->height()/2));
            QVERIFY(dialog.midiExpandRepeats->isChecked());QVERIFY(dialog.midiExportRPNs->isChecked());QVERIFY(!preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS));QVERIFY(!preferences.getBool(PREF_IO_MIDI_EXPORTRPNS));
            auto panel=dialog.findChild<MidiCropPanel*>();QVERIFY(panel);const auto path=settings.filePath("current-midi-options.mid");QVERIFY(panel->write(score.get(),path,{}));
            QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));MidiFile midi;QVERIFY(midi.read(&file));int attacks=0,rpn=0;for(const auto& track:midi.tracks())for(const auto& pair:track.events()){const auto& e=pair.second;attacks+=e.type()==ME_NOTEON&&e.velo()>0;rpn+=e.type()==ME_CONTROLLER&&(e.dataA()==CTRL_HRPN||e.dataA()==CTRL_LRPN);}
            qInfo()<<"Live export choices attacks / RPN"<<attacks<<rpn;QVERIFY(rpn>0);QCOMPARE(attacks,12);dialog.close();
            }
      void midiPreviewUsesCurrentRepeatChoice() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);
            const bool previous=preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS);auto restore=qScopeGuard([&]{preferences.setPreference(PREF_IO_MIDI_EXPANDREPEATS,previous);});preferences.setPreference(PREF_IO_MIDI_EXPANDREPEATS,false);
            ExportDialog dialog(score.get(),mscore);dialog.show();dialog.fileTypeComboBox->setCurrentIndex(7);QTest::qWait(60);dialog.midiExpandRepeats->setChecked(true);auto panel=dialog.findChild<MidiCropPanel*>();QVERIFY(panel);
            int observed=-1,attempts=0;QTimer timer;timer.setInterval(30);connect(&timer,&QTimer::timeout,&dialog,[&]{auto active=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!active||active->objectName()!="midiCropComparison")return;
                  auto table=active->findChild<QTableView*>("midiCropNotes");auto roll=active->findChild<QAbstractScrollArea*>("midiCropRoll");if(++attempts>140){active->reject();return;}if(!table||!roll||!roll->isEnabled()||!table->model()->rowCount())return;observed=table->model()->rowCount();timer.stop();active->accept();});
            timer.start();QTest::mouseClick(panel->findChild<QPushButton*>("midiCropCompare"),Qt::LeftButton);timer.stop();qInfo()<<"Live preview note rows"<<observed;QCOMPARE(observed,12);dialog.close();
            }
      void midiAuditionUsesExportRepeatChoice() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(score->lastMeasure()->endTick());pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            const bool original=MScore::playRepeats;auto restore=qScopeGuard([&]{MScore::playRepeats=original;});MScore::playRepeats=false;score->setExpandRepeats(false);
            ExportMidi exporter(score.get());exporter.collectGateData=true;MidiExportOptions options;options.crop=true;options.gate.jitter=0;options.writtenPedal=true;QBuffer output;output.open(QIODevice::WriteOnly);QVERIFY(exporter.write(&output,true,true,mscore->synthesizerState(),options));
            const auto audition=prepareGateAudition(score.get(),exporter.gateData,exporter.gateReport);int attacks=0;for(const auto& event:audition.original.events)if(event.second.type()==ME_NOTEON&&event.second.velo()>0&&(event.second.note()||event.second.noteEventOwner()))++attacks;
            qInfo()<<"Export / audition attacks; unmatched"<<exporter.gateData.notes.size()<<attacks<<audition.unmatched;QCOMPARE(attacks,exporter.gateData.notes.size());QCOMPARE(audition.unmatched,0);QVERIFY(!MScore::playRepeats);QVERIFY(!score->expandRepeats());
            }
      void midiExportChoicesKeepNoteIdentity() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);
            auto snapshot=[&](bool repeat,bool rpn){ExportMidi exporter(score.get());exporter.collectGateData=true;QBuffer output;output.open(QIODevice::WriteOnly);if(!exporter.write(&output,repeat,rpn,mscore->synthesizerState()))return MidiGateData{};return exporter.gateData;};
            auto reference=snapshot(true,true);QVERIFY(!reference.notes.isEmpty());
            auto key=[](const MidiGateNote& n){return QString("%1:%2:%3:%4").arg(n.sourceTick).arg(n.sourceUtick).arg(n.voice).arg(n.pitch);};QMap<QString,QString> ids;for(const auto& n:reference.notes)if(n.known)ids[key(n)]=n.id;
            for(bool repeat:{true,false})for(bool rpn:{false,true}){const auto changed=snapshot(repeat,rpn);for(const auto& n:changed.notes)if(n.known)QCOMPARE(n.id,ids.value(key(n)));}
            }
      void midiPauseAlignsLyricsAndMarks() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);
            auto chord=toChord(score->findCR(Fraction::fromTicks(480),0));QVERIFY(chord);auto lyric=new Lyrics(score.get());lyric->setParent(chord);lyric->setTrack(0);lyric->setXmlText("word");auto mark=new RehearsalMark(score.get());mark->setParent(chord->segment());mark->setTrack(0);mark->setXmlText("A");
            score->startCmd();score->undoAddElement(lyric);score->undoAddElement(mark);score->endCmd();score->tempomap()->setPause(480,.25);
            ExportMidi exporter(score.get());exporter.collectGateData=true;QBuffer output;output.open(QIODevice::WriteOnly);QVERIFY(exporter.write(&output,true,true,mscore->synthesizerState()));
            QVector<int> expected,lyrics,marks;for(const auto& n:exporter.gateData.notes)if(n.pitch==59)expected.append(n.on);
            for(const auto& track:exporter.mf.tracks())for(const auto& pair:track.events())if(pair.second.type()==ME_META){if(pair.second.metaType()==META_LYRIC)lyrics.append(pair.first);else if(pair.second.metaType()==META_MARKER)marks.append(pair.first);}
            qInfo()<<"Pause note / lyric / marker ticks"<<expected<<lyrics<<marks;QCOMPARE(lyrics,expected);QCOMPARE(marks,expected);
            QFile saved(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/pause-metadata-134.mid");QVERIFY(saved.open(QIODevice::WriteOnly));saved.write(output.data());
            }
      void midiAnticipationKeepsFirstBeatMetadata() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);
            auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));QVERIFY(chord);chord->downNote()->setOnTimeOffset(-100);
            auto lyric=new Lyrics(score.get());lyric->setParent(chord);lyric->setTrack(4);lyric->setXmlText("word");
            auto mark=new RehearsalMark(score.get());mark->setParent(chord->segment());mark->setTrack(0);mark->setXmlText("A");
            score->startCmd();score->undoAddElement(lyric);score->undoAddElement(mark);score->endCmd();
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(score->lastMeasure()->endTick());pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            MidiExportOptions options;options.writtenPedal=true;ExportMidi exporter(score.get());exporter.collectGateData=true;QBuffer output;output.open(QIODevice::WriteOnly);QVERIFY(exporter.write(&output,false,true,mscore->synthesizerState(),options));
            int firstBeat=-1,lyricAt=-1,markAt=-1,firstOn=-1,pedalAt=-1;bool setupAtZero=false;
            for(const auto& track:exporter.mf.tracks())for(const auto& pair:track.events()){
                  const auto& event=pair.second;
                  if(event.type()==ME_NOTEON&&event.velo()>0&&event.pitch()==36)firstOn=pair.first;
                  if(event.type()==ME_CONTROLLER&&event.dataA()==64&&event.dataB()>=64)pedalAt=pair.first;
                  if(event.type()==ME_META){const auto text=QByteArray(reinterpret_cast<const char*>(event.edata()),event.len());
                        if(event.metaType()==META_LYRIC)lyricAt=pair.first;
                        if(event.metaType()==META_MARKER){if(text.startsWith("Kumo: score first beat"))firstBeat=pair.first;else if(text.startsWith("A"))markAt=pair.first;}
                        if(event.metaType()==META_TEMPO&&pair.first==0)setupAtZero=true;
                        }
                  }
            auto material=renderPlaybackPreview(score.get(),score.get(),false,0);int expectedPedal=-1;
            for(const auto& pair:material.events)if(pair.second.type()==ME_CONTROLLER&&pair.second.controller()==64&&pair.second.value()>=64)expectedPedal=pair.first+firstBeat;
            qInfo()<<"Anticipation onset / first beat / lyric / mark / pedal"<<firstOn<<firstBeat<<lyricAt<<markAt<<pedalAt;
            QVERIFY(firstBeat>0);QCOMPARE(firstOn,0);QVERIFY(setupAtZero);QCOMPARE(lyricAt,firstBeat);QCOMPARE(markAt,firstBeat);QVERIFY(expectedPedal>=firstBeat);QCOMPARE(pedalAt,expectedPedal);
            QVERIFY(output.data().contains("Kumo: score first beat"));
            QFile saved(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/anticipation-metadata-134.mid");QVERIFY(saved.open(QIODevice::WriteOnly));saved.write(output.data());
            }
      void midiHandOverridesSurviveExportChoices() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(score->lastMeasure()->endTick());pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            ExportMidi first(score.get());first.collectGateData=true;QBuffer baseline;baseline.open(QIODevice::WriteOnly);QVERIFY(first.write(&baseline,true,true,mscore->synthesizerState()));MidiExportOptions options;options.splitHands=true;
            options.crop=true;options.gate.protectMelody=false;options.gate.jitter=0;
            int expected=0;for(const auto& note:first.gateData.notes)if(note.pitch==84){options.handOverrides[note.id]=0;options.gate.releaseOverrides[note.id]=note.on+240;++expected;}QCOMPARE(expected,2);
            for(bool repeat:{false,true})for(bool rpn:{false,true}){ExportMidi changed(score.get());QBuffer output;output.open(QIODevice::WriteOnly);QVERIFY(changed.write(&output,repeat,rpn,mscore->synthesizerState(),options));int kept=0;for(const auto& note:changed.gateReport)if(note.pitch==84){QCOMPARE(note.exportHand,0);QCOMPARE(note.reason,QString("manual-release"));QCOMPARE(note.newOff,note.on+240);++kept;}QCOMPARE(kept,repeat?2:1);}
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
      void nativeNestedHandsForMidi() {
            QFile source(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(source.open(QIODevice::ReadOnly));auto xml=source.readAll();
            xml.replace("<Symbol><name>keyboardPlayWithLH</name></Symbol>","<Symbol><name>coda</name><Symbol><name>keyboardPlayWithLH</name></Symbol></Symbol>");
            const auto path=settings.filePath("gate-nested-hands.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();std::unique_ptr<MasterScore> score(mscore->readScore(path));QVERIFY(score);
            auto old=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->downNote();QCOMPARE(HandBracket::assignments(score.get()).value(old,-1),0);
            ExportMidi exporter(score.get());exporter.collectGateData=true;QBuffer output;output.open(QIODevice::WriteOnly);QVERIFY(exporter.write(&output,true,true,mscore->synthesizerState()));
            bool found=false;for(const auto& n:exporter.gateData.notes)if(n.pitch==36){found=true;QCOMPARE(n.hand,0);}QVERIFY(found);
            }
      void nativeAlternativeFingerExport_data() {
            QTest::addColumn<QString>("text");QTest::addColumn<QString>("third");QTest::addColumn<int>("mask");QTest::addColumn<bool>("release");
            QTest::newRow("alternative-both-in-use")<<QString("2(3)")<<QString("3")<<6<<true;
            QTest::newRow("alternative-one-free")<<QString("2(3)")<<QString()<<6<<false;
            QTest::newRow("editorial")<<QString("[2]")<<QString()<<2<<true;
            QTest::newRow("ordinary")<<QString("2")<<QString()<<2<<true;
            QTest::newRow("conflicting-annotations")<<QString("2")<<QString()<<0<<false;
            }
      void nativeAlternativeFingerExport() {
            QFETCH(QString,text);QFETCH(QString,third);QFETCH(int,mask);QFETCH(bool,release);
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);auto old=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->downNote();old->setPitch(59,19,19);
            auto next=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(5));QVERIFY(next);
            auto add=[&](Note* note,const QString& value){if(value.isEmpty())return;auto f=new Fingering(score.get());f->setParent(note);f->setTrack(note->track());f->setPlainText(value);score->addElement(f);};add(old,text);add(next->notes()[0],"2");add(next->notes()[1],third);
            if(mask==0)add(old,"3");
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1920));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);score->doLayout();
            const auto state=score->state();const bool dirty=score->dirty();const auto undo=score->undoStack()->getCurIdx();
            ExportMidi exporter(score.get());QBuffer output;output.open(QIODevice::WriteOnly);MidiExportOptions options;options.crop=true;options.gate.jitter=0;QVERIFY(exporter.write(&output,true,true,mscore->synthesizerState(),options));
            bool found=false;for(const auto& n:exporter.gateReport)if(n.pitch==59){found=true;QCOMPARE(n.fingerMask,mask);QCOMPARE(n.reason=="hand-release",release);if(release){QVERIFY(n.newOff<480);QVERIFY(n.handWarning.contains(QString::fromUtf8("指法")));}if(mask==0){QCOMPARE(n.reason,QString("fingering-uncertain"));QCOMPARE(n.newOff,n.off);}}QVERIFY(found);
            QVERIFY(score->state()==state);QCOMPARE(score->dirty(),dirty);QCOMPARE(score->undoStack()->getCurIdx(),undo);
            if(QString::fromLatin1(QTest::currentDataTag())=="alternative-both-in-use") {
                  MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});panel.show();QTimer timer;timer.setInterval(40);bool shown=false;int tries=0;
                  connect(&timer,&QTimer::timeout,&panel,[&]{auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(++tries>100){dialog->reject();return;}auto table=dialog->findChild<QTableView*>();if(!table||table->model()->rowCount()==0)return;
                        for(int i=0;i<table->model()->rowCount();++i)if(table->model()->index(i,0).data().toString()=="B3"){QVERIFY(table->model()->index(i,5).data().toString().contains(QString::fromUtf8("为弹下一组音提前松手")));QVERIFY(table->model()->index(i,5).data().toString().contains(QString::fromUtf8("指法")));table->selectRow(i);const auto out=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");QVERIFY(!out.isEmpty());QVERIFY(dialog->grab().save(out+"/midi-finger-alternatives.png"));shown=true;timer.stop();dialog->accept();return;}});
                  timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiCropCompare"),Qt::LeftButton);timer.stop();QVERIFY(shown);
                  }
            const auto saved=settings.filePath("gate-finger-"+QString::fromLatin1(QTest::currentDataTag())+".mscz");QFileInfo info(saved);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> reopened(mscore->readScore(saved));QVERIFY(reopened);auto savedNote=toChord(reopened->firstSegment(SegmentType::ChordRest)->element(4))->downNote();QStringList fingers;for(auto e:savedNote->el())if(e->isFingering())fingers<<toFingering(e)->plainText();QVERIFY(fingers.contains(text));
            }
      void midiSubstitutionTiming() {
            MidiGateData data;data.tempos={{0,500000.}};
            MidiGateNote old;old.id="held";old.known=old.piano=true;old.pitch=60;old.hand=0;old.partId=0;old.offOrdinal=0;old.off=old.newOff=1920;old.fingerings=QStringList{"1-3"};old.finger=1;old.fingerMask=1;
            auto blocker=old;blocker.id="blocker";blocker.pitch=64;blocker.off=blocker.newOff=720;blocker.fingerings=QStringList{"3"};blocker.finger=3;blocker.fingerMask=4;blocker.offOrdinal=1;
            auto next=old;next.id="next";next.pitch=62;next.on=960;next.off=next.newOff=1440;next.fingerings=QStringList{"1"};next.finger=1;next.fingerMask=1;next.offOrdinal=2;
            data.notes={old,blocker,next};MidiGateOptions options;options.jitter=0;options.lengthRatio=options.nextAttackRatio=1.;MidiGateProcessor processor;
            const auto result=processor.process(data,options);QCOMPARE(result[0].fingerChanges.size(),1);const auto change=result[0].fingerChanges[0];QCOMPARE(change.from,1);QCOMPARE(change.to,3);QVERIFY(change.tick>720&&change.tick<960);QVERIFY(change.afterPitches.contains(64));QVERIFY(!result[0].fingeringUnresolved);QVERIFY(result[0].requiredUntil>=change.tick);
            }
      void midiSubstitutionReleasesAndManualRestore() {
            MidiGateData data;data.tempos={{0,500000.}};data.pedals={{0,0,-1,2400,true}};
            MidiGateNote old;old.id="held";old.known=old.piano=true;old.pitch=60;old.hand=0;old.partId=0;old.offOrdinal=0;old.off=old.newOff=1920;old.fingerings=QStringList{"1-3"};old.finger=1;old.fingerMask=1;
            auto blocker=old;blocker.id="blocker";blocker.voice=1;blocker.pitch=64;blocker.finger=3;blocker.fingerMask=4;blocker.fingerings=QStringList{"3"};blocker.offOrdinal=1;
            auto next=old;next.id="next";next.pitch=62;next.on=960;next.off=next.newOff=1440;next.fingerings=QStringList{"1"};next.finger=1;next.fingerMask=1;next.offOrdinal=2;
            data.notes={old,blocker,next};MidiGateOptions options;options.jitter=0;options.lengthRatio=options.nextAttackRatio=1.;MidiGateProcessor processor;
            const auto result=processor.process(data,options);QCOMPARE(result[0].fingerChanges.size(),1);QCOMPARE(result[1].reason,QString("finger-release"));QVERIFY(result[1].newOff<=result[0].fingerChanges[0].tick);QVERIFY(result[0].fingerChanges[0].releasedPitches.contains(64));
            options.releaseOverrides["blocker"]=1920;const auto restored=processor.process(data,options);QCOMPARE(restored[1].newOff,1920);QVERIFY(restored[0].fingerChanges.isEmpty());QVERIFY(restored[0].fingeringUnresolved);QCOMPARE(restored[0].newOff,1920);
            options.releaseOverrides["blocker"]=480;const auto earlier=processor.process(data,options);QCOMPARE(earlier[1].newOff,480);QCOMPARE(earlier[0].fingerChanges.size(),1);QVERIFY(earlier[0].fingerChanges[0].tick>480&&earlier[0].fingerChanges[0].tick<960);
            }
      void nativeSubstitutionExportReading() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);
            auto old=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->downNote();old->setPitch(60,14,14);
            auto finger=new Fingering(score.get());finger->setParent(old);finger->setTrack(old->track());finger->setPlainText("1-3");score->addElement(finger);score->doLayout();
            ExportMidi exporter(score.get());exporter.collectGateData=true;QBuffer output;output.open(QIODevice::WriteOnly);QVERIFY(exporter.write(&output,true,true,mscore->synthesizerState()));
            bool found=false;for(const auto& n:exporter.gateData.notes)if(n.voice==4){found=true;QCOMPARE(n.fingerings,QStringList{"1-3"});QCOMPARE(n.fingerMask,1);QCOMPARE(n.finger,1);}QVERIFY(found);
            }
      void midiSubstitutionMissedAttackAndPartialRoute() {
            MidiGateData data;data.tempos={{0,500000.}};
            MidiGateNote old;old.id="held";old.known=old.piano=true;old.pitch=60;old.hand=0;old.partId=0;old.offOrdinal=0;old.off=old.newOff=1920;old.fingerings=QStringList{"1-3"};old.finger=1;old.fingerMask=1;
            auto blocker=old;blocker.id="blocker";blocker.pitch=64;blocker.off=blocker.newOff=720;blocker.fingerings=QStringList{"3"};blocker.finger=3;blocker.fingerMask=4;blocker.offOrdinal=1;
            auto next=old;next.id="next";next.pitch=62;next.on=480;next.off=next.newOff=959;next.fingerings=QStringList{"1"};next.finger=1;next.fingerMask=1;next.offOrdinal=2;
            data.notes={old,blocker,next};MidiGateOptions options;options.jitter=0;options.lengthRatio=options.nextAttackRatio=1.;MidiGateProcessor processor;
            const auto missed=processor.process(data,options);QVERIFY(missed[0].fingeringUnresolved);QVERIFY(missed[0].fingerChanges.isEmpty());QCOMPARE(missed[0].newOff,1920);
            data.pedals={{0,0,-1,2400,true}};old.fingerings=QStringList{"1-3-5"};blocker.off=blocker.newOff=1920;
            auto protectedKey=blocker;protectedKey.id="protected";protectedKey.pitch=67;protectedKey.voice=2;protectedKey.fingerings=QStringList{"5"};protectedKey.finger=5;protectedKey.fingerMask=16;protectedKey.offOrdinal=3;
            data.notes={old,blocker,next,protectedKey};options.protectedTracks.insert(2);const auto partial=processor.process(data,options);
            QVERIFY(partial[0].fingeringUnresolved);QVERIFY(partial[0].fingerChanges.isEmpty());QCOMPARE(partial[0].newOff,1920);QVERIFY(partial[1].reason!="finger-release");QCOMPARE(partial[3].newOff,1920);
            }
      void nativeSubstitutionFlowAndSave() {
            QFile source(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(source.open(QIODevice::ReadOnly));auto xml=source.readAll();
            const QByteArray ending="</voice>\n    </Measure></Staff>";QVERIFY(xml.contains(ending));
            xml.replace(ending,"</voice><voice><Chord><dots>1</dots><durationType>quarter</durationType><Note><pitch>64</pitch><tpc>18</tpc><Fingering><text>3</text></Fingering><Symbol><name>keyboardPlayWithLH</name></Symbol></Note></Chord><Rest><durationType>half</durationType></Rest><Rest><durationType>eighth</durationType></Rest></voice>\n    </Measure></Staff>");
            const auto path=settings.filePath("gate-substitution-flow.mscx");QFile fixture(path);QVERIFY(fixture.open(QIODevice::WriteOnly));fixture.write(xml);fixture.close();std::unique_ptr<MasterScore> score(mscore->readScore(path));QVERIFY(score);
            auto old=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->downNote();old->setPitch(60,14,14);auto f=new Fingering(score.get());f->setParent(old);f->setTrack(4);f->setPlainText("1-3");score->addElement(f);
            auto next=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(5));const int pitches[]={62,65,69},tpcs[]={16,13,17};for(int i=0;i<3;++i)next->notes()[i]->setPitch(pitches[i],tpcs[i],tpcs[i]);
            f=new Fingering(score.get());f->setParent(next->downNote());f->setTrack(5);f->setPlainText("1");score->addElement(f);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction());pedal->setTicks(Fraction::fromTicks(1920));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);score->doLayout();
            const auto state=score->state();const auto undo=score->undoStack()->getCurIdx();const bool dirty=score->dirty();
            auto verify=[&](MasterScore* target) {ExportMidi exporter(target);QBuffer bytes;bytes.open(QIODevice::WriteOnly);MidiExportOptions options;options.crop=true;options.gate.jitter=0;if(!exporter.write(&bytes,true,true,mscore->synthesizerState(),options))return false;
                  bool held=false,released=false;for(const auto& n:exporter.gateReport){qInfo()<<"Native substitution"<<n.voice<<n.pitch<<n.on<<n.off<<n.hand<<n.fingerings<<n.manual<<n.ambiguous<<n.reason<<n.fingerChanges.size()<<n.handWarning;if(n.voice==4){held=n.fingerChanges.size()==1&&!n.fingeringUnresolved&&n.fingerChanges[0].releasedPitches.contains(64)&&n.newOff>n.fingerChanges[0].tick;}if(n.voice==6)released=n.reason=="finger-release"&&n.newOff<n.off;}
                  return held&&released;};QVERIFY(verify(score.get()));
            MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});panel.show();QTimer timer;timer.setInterval(40);bool shown=false;int tries=0;
            connect(&timer,&QTimer::timeout,&panel,[&]{auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(++tries>100){dialog->reject();return;}auto table=dialog->findChild<QTableView*>();if(!table||table->model()->rowCount()==0)return;
                  bool release=false;int held=-1;for(int row=0;row<table->model()->rowCount();++row){const auto pitch=table->model()->index(row,0).data().toString();if(pitch=="E4")release=table->model()->index(row,5).data().toString().contains(QString::fromUtf8("为按谱换指提前松手"));if(pitch=="C4")held=row;}
                  if(!release||held<0)return;timer.stop();table->selectRow(held);QTest::qWait(30);QVERIFY(table->model()->index(held,5).data().toString().contains(QString::fromUtf8("1→3")));const auto out=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");QVERIFY(!out.isEmpty());QVERIFY(dialog->grab().save(out+"/midi-substitution-release.png"));shown=true;dialog->accept();});
            timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiCropCompare"),Qt::LeftButton);timer.stop();QVERIFY(shown);QVERIFY(score->state()==state);QCOMPARE(score->dirty(),dirty);QCOMPARE(score->undoStack()->getCurIdx(),undo);
            const auto saved=settings.filePath("gate-substitution-flow.mscz");QFileInfo info(saved);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> reopened(mscore->readScore(saved));QVERIFY(reopened);QVERIFY(verify(reopened.get()));
            }



      void nativeStaffHandTextAndRangeEditing() {
            QFile source(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(source.open(QIODevice::ReadOnly));auto xml=source.readAll();xml.replace("<Symbol><name>keyboardPlayWithLH</name></Symbol>","");const auto path=settings.filePath("staff-hand-text.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();std::unique_ptr<MasterScore> score(mscore->readScore(path));QVERIFY(score);
            auto at=[&](int tick){return score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);};
            for(const auto& item:QList<QPair<int,QString>>{{0,"r.h."},{480,"l.h."}}){auto text=new StaffText(score.get());text->setParent(at(item.first));text->setTrack(4);text->setPlainText(item.second);score->addElement(text);}score->doLayout();
            ExportMidi exporter(score.get());QBuffer bytes;bytes.open(QIODevice::WriteOnly);MidiExportOptions opts;opts.splitHands=true;QVERIFY(exporter.write(&bytes,true,true,mscore->synthesizerState(),opts));for(const auto& n:exporter.gateReport){if(n.voice==4)QCOMPARE(n.exportHand,1);if(n.voice==5)QCOMPARE(n.exportHand,0);}
            score->selection().setRange(at(480),at(960),1,2);MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});panel.show();QTimer timer;timer.setInterval(50);int stage=0,tries=0;
            connect(&timer,&QTimer::timeout,&panel,[&]{auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(++tries>150){dialog->reject();return;}auto table=dialog->findChild<QTableView*>("midiHandNotes");auto roll=dialog->findChild<QAbstractScrollArea*>("midiHandRoll");if(!table||!roll||!roll->isEnabled()||!table->model()->rowCount())return;
                  if(stage==0){auto range=dialog->findChild<QPushButton*>("midiHandSelectRange");QVERIFY(range->isEnabled());QTest::mouseClick(range,Qt::LeftButton);QCOMPARE(table->selectionModel()->selectedRows().size(),3);QTest::mouseClick(dialog->findChild<QPushButton*>("midiHandRight"),Qt::LeftButton);++stage;}
                  else if(stage==1){int manual=0;for(int i=0;i<table->model()->rowCount();++i)manual+=table->model()->index(i,4).data().toString().startsWith(QString::fromUtf8("手动指定"));QCOMPARE(manual,3);timer.stop();dialog->accept();++stage;}
                  });timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiHandPreview"),Qt::LeftButton);timer.stop();QCOMPARE(stage,2);
            }
      void midiHandsContextTransferAndBudget() {
            MidiGateData data;data.tempos={{0,500000.}};MidiGateNote left;left.id="left";left.piano=left.known=true;left.partId=0;left.offOrdinal=0;left.hand=left.handHint=0;left.pitch=36;left.off=left.newOff=200;
            auto right=left;right.id="right";right.pitch=84;right.hand=right.handHint=1;right.offOrdinal=1;auto middle=left;middle.id="middle";middle.pitch=64;middle.hand=-1;middle.on=240;middle.off=middle.newOff=450;middle.offOrdinal=2;data.notes={left,right,middle};MidiExportOptions options;options.splitHands=true;
            auto rows=prepareMidiPostprocess(data,options);QCOMPARE(rows[2].exportHand,1);QCOMPARE(rows[2].off,middle.off);
            MidiGateInstruction handoff;handoff.tick=100;handoff.handSet=true;handoff.hand=0;right.instructions={handoff};data.notes={right};rows=prepareMidiPostprocess(data,options);QCOMPARE(rows[0].exportHand,1);QCOMPARE(rows[0].instructions[0].hand,0);
            data.notes.clear();for(int group=0;group<1200;++group)for(int pitch:{43,48,52,55,64,67,71,74}){auto n=middle;n.id=QString("%1-%2").arg(group).arg(pitch);n.on=group*240;n.off=n.newOff=n.on+180;n.pitch=pitch;n.handHint=pitch<60?0:1;n.offOrdinal=group*8+pitch;data.notes.append(n);}
            QElapsedTimer timer;timer.start();rows=prepareMidiPostprocess(data,options);QCOMPARE(rows.size(),9600);qInfo()<<"Hand export 1200 groups / 9600 notes ms"<<timer.elapsed();for(const auto& n:rows){QVERIFY(n.exportHand==0||n.exportHand==1);QCOMPARE(n.off,n.newOff);}int calls=0;QVERIFY(prepareMidiPostprocess(data,options,[&]{return ++calls>3;}).isEmpty());QCOMPARE(calls,4);
            }
      void midiHandRoutingPreservesEvents() {
            MidiFile original;original.setDivision(480);original.setFormat(1);MidiTrack track;track.setOutPort(2);track.setOutChannel(0);
            track.insert(0,MidiEvent(ME_CONTROLLER,0,CTRL_PROGRAM,0));track.insert(0,MidiEvent(ME_CONTROLLER,0,64,127));
            track.insert(0,MidiEvent(ME_NOTEON,0,48,85));track.insert(0,MidiEvent(ME_NOTEON,0,72,98));track.insert(700,MidiEvent(ME_NOTEON,0,48,0));track.insert(900,MidiEvent(ME_NOTEON,0,72,0));track.insert(1200,MidiEvent(ME_CONTROLLER,0,64,0));original.tracks().append(track);
            QVector<MidiGateSource> tags;for(int pitch:{48,72}){MidiGateSource tag;tag.track=0;tag.partId=0;tag.partName="Piano";tag.piano=true;tag.staff=pitch==48?0:1;tag.handHint=tag.staff==0?1:0;tag.hand=pitch==48?0:1;tag.pitch=pitch;tag.displayStaff=tag.staff;tag.sourceTick=0;tags.append(tag);}
            auto data=midiGateSnapshot(original,tags);QCOMPARE(data.notes.size(),2);MidiExportOptions options;options.splitHands=true;auto notes=prepareMidiPostprocess(data,options);QCOMPARE(notes[0].exportHand,0);QCOMPARE(notes[1].exportHand,1);
            auto flattened=[](const MidiFile& file){QVector<QString> rows;for(const auto& track:file.tracks())for(const auto& e:track.events())if(e.second.type()!=ME_META)rows.append(QString("%1:%2:%3:%4:%5:%6").arg(track.outPort()).arg(e.first).arg(e.second.type()).arg(e.second.channel()).arg(e.second.dataA()).arg(e.second.dataB()));std::sort(rows.begin(),rows.end());return rows;};
            auto split=original;applyMidiHands(split,notes);QCOMPARE(flattened(split),flattened(original));QCOMPARE(split.tracks().size(),3);QCOMPARE(split.tracks()[1].events().count(700),size_t(1));
            options.handOverrides[notes[0].id]=1;notes=prepareMidiPostprocess(data,options);auto edited=original;applyMidiHands(edited,notes);QCOMPARE(edited.tracks().size(),2);QCOMPARE(flattened(edited),flattened(original));QVERIFY(notes[0].exportHandSource.startsWith(QString::fromUtf8("手动指定")));
            // Cropping and moving tracks are applied together, before ordinal order changes.
            notes[0].newOff=300;auto shortened=original;applyMidiHands(shortened,notes);bool moved=false;for(const auto& t:shortened.tracks())for(const auto& e:t.events())if(e.first==300&&e.second.type()==ME_NOTEON&&e.second.pitch()==48&&e.second.velo()==0)moved=true;QVERIFY(moved);
            QBuffer bytes;bytes.open(QIODevice::ReadWrite);QVERIFY(!edited.write(&bytes));bytes.seek(0);MidiFile reread;QVERIFY(reread.read(&bytes));int notesAfter=0;bool left=false,right=false;for(const auto& t:reread.tracks())for(const auto& e:t.events()){notesAfter+=e.second.type()==ME_NOTEON&&e.second.velo()>0;if(e.second.type()==ME_META&&e.second.metaType()==META_TRACK_NAME){const auto name=QByteArray(reinterpret_cast<const char*>(e.second.edata()),e.second.len());left|=name.contains("Left Hand");right|=name.contains("Right Hand");}}QCOMPARE(notesAfter,2);QVERIFY(right);QVERIFY(!left);
            }
      void nativeHandTextAndCrossStaffExport() {
            QFile source(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(source.open(QIODevice::ReadOnly));auto xml=source.readAll();xml.replace("<Symbol><name>keyboardPlayWithLH</name></Symbol>","");
            const auto path=settings.filePath("three-hand-export.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();std::unique_ptr<MasterScore> score(mscore->readScore(path));QVERIFY(score);
            auto old=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->downNote();auto f=new Fingering(score.get());f->setParent(old);f->setTrack(4);f->setPlainText("r.h.");score->addElement(f);
            auto next=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(5));next->setStaffMove(-1);score->doLayout();
            const auto state=score->state();const bool dirty=score->dirty();const int undo=score->undoStack()->getCurIdx();
            auto verify=[&](MasterScore* target){ExportMidi plain(target),split(target);QBuffer a,b;a.open(QIODevice::WriteOnly);b.open(QIODevice::WriteOnly);MidiExportOptions options;options.splitHands=true;
                  if(!plain.write(&a,true,true,mscore->synthesizerState())||!split.write(&b,true,true,mscore->synthesizerState(),options))return false;
                  bool text=false,visual=false;for(const auto& n:split.gateReport){if(n.pitch==36)text=n.exportHand==1&&n.hand==1&&n.handSource.contains("r.h.");if(n.voice==5)visual=n.displayStaff==0&&n.handHint==1;}
                  auto frames=[](const MidiFile& midi){QVector<QString> all;for(const auto& t:midi.tracks())for(const auto& e:t.events())if(e.second.type()!=ME_META)all.append(QString("%1:%2:%3:%4:%5:%6").arg(t.outPort()).arg(e.first).arg(e.second.type()).arg(e.second.channel()).arg(e.second.dataA()).arg(e.second.dataB()));std::sort(all.begin(),all.end());return all;};
                  if(frames(plain.mf)!=frames(split.mf)||!text||!visual)return false;
                  const auto out=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");for(const auto& item:QList<QPair<QString,QByteArray>>{{"midi-hand-original.mid",a.data()},{"midi-hand-split.mid",b.data()}}){QFile file(out+"/"+item.first);if(!file.open(QIODevice::WriteOnly)||file.write(item.second)!=item.second.size())return false;}
                  const auto font=qEnvironmentVariable("P0_TEST_SOUNDFONT");if(!font.isEmpty()){auto before=render(plain.mf,split.gateData,font),after=render(split.mf,split.gateData,font);if(before.isEmpty()||before!=after)return false;}
                  return true;};QVERIFY(verify(score.get()));QVERIFY(score->state()==state);QCOMPARE(score->dirty(),dirty);QCOMPARE(score->undoStack()->getCurIdx(),undo);
            const auto saved=settings.filePath("three-hand-export.mscz");QFileInfo info(saved);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> reopened(mscore->readScore(saved));QVERIFY(reopened);QVERIFY(verify(reopened.get()));
            }
      void midiHandPreviewEditsAndExport() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);bool rpns=true;MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};},[&]{return MidiRenderSettings{true,rpns};});panel.show();QTimer timer;timer.setInterval(50);int stage=0,tries=0;QString id;const auto state=score->state();const int undo=score->undoStack()->getCurIdx();
            connect(&timer,&QTimer::timeout,&panel,[&]{auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(++tries>180){dialog->reject();return;}auto table=dialog->findChild<QTableView*>("midiHandNotes");auto roll=dialog->findChild<QAbstractScrollArea*>("midiHandRoll");auto left=dialog->findChild<QPushButton*>("midiHandLeft"),right=dialog->findChild<QPushButton*>("midiHandRight");if(!table||!roll||!left||!roll->isEnabled()||!table->model()->rowCount())return;
                  int row=-1;for(int i=0;i<table->model()->rowCount();++i)if(table->model()->index(i,0).data().toString()=="C2")row=i;if(row<0)return;
                  if(stage==0){QCOMPARE(table->model()->index(row,2).data().toString(),QString::fromUtf8("左手"));table->selectRow(row);QTest::mouseClick(right,Qt::LeftButton);++stage;}
                  else if(stage==1){QCOMPARE(table->model()->index(row,2).data().toString(),QString::fromUtf8("右手"));QVERIFY(panel.splitsHands());dialog->findChild<QAction*>("midiHandUndo")->trigger();++stage;}
                  else if(stage==2){QCOMPARE(table->model()->index(row,2).data().toString(),QString::fromUtf8("左手"));dialog->findChild<QAction*>("midiHandRedo")->trigger();++stage;}
                  else if(stage==3){table->selectAll();QTest::mouseClick(left,Qt::LeftButton);++stage;}
                  else if(stage==4){for(int i=0;i<table->model()->rowCount();++i)QCOMPARE(table->model()->index(i,2).data().toString(),QString::fromUtf8("左手"));dialog->findChild<QAction*>("midiHandUndo")->trigger();++stage;}
                  else if(stage==5){table->selectRow(row);QTest::mouseClick(dialog->findChild<QPushButton*>("midiHandAutomatic"),Qt::LeftButton);++stage;}
                  else if(stage==6){QCOMPARE(table->model()->index(row,2).data().toString(),QString::fromUtf8("左手"));dialog->findChild<QAction*>("midiHandUndo")->trigger();++stage;}
                  else if(stage==7){QCOMPARE(table->model()->index(row,2).data().toString(),QString::fromUtf8("右手"));const auto out=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");QVERIFY(!out.isEmpty());QVERIFY(dialog->grab().save(out+"/midi-hand-export.png"));timer.stop();++stage;dialog->accept();}
                  });timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiHandPreview"),Qt::LeftButton);timer.stop();QCOMPARE(stage,8);QVERIFY(score->state()==state);QCOMPARE(score->undoStack()->getCurIdx(),undo);
            const auto output=settings.filePath("hands.mid");QVERIFY(panel.write(score.get(),output,{}));QFile::copy(output,qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/midi-hand-preview.mid");QFile file(output);QVERIFY(file.open(QIODevice::ReadOnly));MidiFile midi;QVERIFY(midi.read(&file));bool rootRight=false;for(const auto& track:midi.tracks()){bool right=false;for(const auto& e:track.events())if(e.second.type()==ME_META&&e.second.metaType()==META_TRACK_NAME)right|=QByteArray(reinterpret_cast<const char*>(e.second.edata()),e.second.len()).contains("Right Hand");if(right)for(const auto& e:track.events())if(e.second.type()==ME_NOTEON&&e.second.velo()>0&&e.second.pitch()==36)rootRight=true;}QVERIFY(rootRight);
            rpns=false;const auto other=settings.filePath("hands-rpn-off.mid");QVERIFY(panel.write(score.get(),other,{}));QFile alternate(other);QVERIFY(alternate.open(QIODevice::ReadOnly));MidiFile reduced;QVERIFY(reduced.read(&alternate));bool rootRightWithRpnOff=false;
            for(const auto& track:reduced.tracks()){bool right=false;for(const auto& e:track.events())if(e.second.type()==ME_META&&e.second.metaType()==META_TRACK_NAME)right|=QByteArray(reinterpret_cast<const char*>(e.second.edata()),e.second.len()).contains("Right Hand");if(right)for(const auto& e:track.events())if(e.second.type()==ME_NOTEON&&e.second.velo()>0&&e.second.pitch()==36)rootRightWithRpnOff=true;}QVERIFY(rootRightWithRpnOff);
            }
      void midiTiedInstructionProtection() {
            MidiGateData data;data.tempos={{0,500000.}};data.pedals={{0,0,-1,2200,true}};
            MidiGateNote old;old.id="held";old.known=old.piano=true;old.track=1;old.voice=4;old.staff=1;old.pitch=36;old.hand=0;old.partId=0;old.offOrdinal=0;old.off=old.newOff=1920;old.requiredUntil=960;
            MidiGateInstruction point;point.tick=point.sourceTick=960;point.fingeringSet=true;point.finger=2;point.fingerMask=2;point.fingerings=QStringList{"2"};old.instructions={point};
            MidiGateNote next=old;next.id="new";next.pitch=60;next.on=480;next.off=next.newOff=940;next.requiredUntil=-1;next.instructions.clear();next.offOrdinal=1;
            data.notes={old,next};MidiGateOptions options;options.jitter=0;
            MidiGateProcessor processor;const auto result=processor.process(data,options);QCOMPARE(result.size(),2);QVERIFY2(result[0].newOff>960,qPrintable(result[0].reason));QVERIFY(result[0].newOff<1920);
            const auto bounds=processor.releaseBounds(result[0],data.pedals,options);QVERIFY(bounds.first>960);
            options.releaseOverrides["held"]=400;const auto dragged=processor.process(data,options);QVERIFY(dragged[0].newOff>960);
            options.releaseOverrides["held"]=1100;const auto allowed=processor.process(data,options);QCOMPARE(allowed[0].newOff,1100);QCOMPARE(allowed[0].reason,QString("manual-release"));
            }
      void midiTiedHandAndFingerFrames() {
            MidiGateData data;data.tempos={{0,500000.}};data.pedals={{0,0,-1,2400,true}};
            MidiGateNote old;old.id="held";old.known=old.piano=true;old.pitch=60;old.hand=0;old.partId=0;old.offOrdinal=0;old.off=old.newOff=1920;old.finger=1;old.fingerMask=1;old.requiredUntil=480;
            MidiGateInstruction point;point.tick=point.sourceTick=480;point.handSet=point.fingeringSet=true;point.hand=1;point.finger=3;point.fingerMask=4;point.fingerings=QStringList{"3"};old.instructions={point};
            MidiGateNote next=old;next.id="next";next.on=960;next.off=next.newOff=1400;next.requiredUntil=-1;next.instructions.clear();next.offOrdinal=1;next.hand=1;next.pitch=64;next.finger=3;next.fingerMask=4;
            data.notes={old,next};MidiGateOptions options;options.jitter=0;options.lengthRatio=options.nextAttackRatio=1.;MidiGateProcessor processor;
            const auto changed=processor.process(data,options);QCOMPARE(changed[0].reason,QString("hand-release"));QVERIFY(changed[0].newOff>480&&changed[0].newOff<960);QVERIFY(changed[0].handWarning.contains(QString::fromUtf8("指法")));
            next.hand=0;data.notes={old,next};const auto otherHand=processor.process(data,options);QVERIFY(otherHand[0].reason!="hand-release");
            }
      void nativeTiedInstructionOccurrences() {
            QFile source(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(source.open(QIODevice::ReadOnly));auto xml=source.readAll();
            xml.replace("<Chord><durationType>whole</durationType><Note><pitch>36</pitch><tpc>14</tpc><Symbol><name>keyboardPlayWithLH</name></Symbol></Note></Chord>",
                  "<Chord><durationType>half</durationType><Note><pitch>36</pitch><tpc>14</tpc><Symbol><name>keyboardPlayWithLH</name></Symbol></Note></Chord><Chord><durationType>half</durationType><Note><pitch>36</pitch><tpc>14</tpc><Fingering><text>2</text></Fingering><Symbol><name>keyboardPlayWithRH</name></Symbol></Note></Chord>");
            const auto path=settings.filePath("gate-tied-instructions.mscx");QFile fixture(path);QVERIFY(fixture.open(QIODevice::WriteOnly));fixture.write(xml);fixture.close();std::unique_ptr<MasterScore> score(mscore->readScore(path));QVERIFY(score);
            auto first=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->downNote();auto last=toChord(score->tick2segment(Fraction::fromTicks(960),false,SegmentType::ChordRest)->element(4))->downNote();
            auto tie=new Tie(score.get());tie->setStartNote(first);tie->setEndNote(last);first->setTieFor(tie);last->setTieBack(tie);tie->setTrack(4);score->addElement(tie);
            score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction());pedal->setTicks(Fraction::fromTicks(1920));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);score->doLayout();
            const auto state=score->state();const auto undo=score->undoStack()->getCurIdx();const bool dirty=score->dirty();
            auto verify=[&](MasterScore* target,bool repeat,bool pause=false,bool written=false) {ExportMidi exporter(target);exporter.collectGateData=true;QBuffer output;output.open(QIODevice::WriteOnly);MidiExportOptions options;options.crop=true;options.writtenPedal=written;options.gate.jitter=0;
                  if(!exporter.write(&output,repeat,true,mscore->synthesizerState(),options))return false;
                  int found=0;for(const auto& n:exporter.gateData.notes)if(n.pitch==36){
                        qInfo()<<"Tied exported occurrence"<<repeat<<n.on<<n.off<<n.hand<<n.requiredUntil<<n.instructions.size();
                        if(n.hand!=0 || n.instructions.size()!=1 || n.instructions[0].sourceTick!=960 || n.instructions[0].hand!=1 || n.instructions[0].fingerMask!=2 || n.instructions[0].tick!=n.on+960+(pause?1920:0) || n.instructions[0].measure!=1 || n.instructions[0].beat!=3.)return false;
                        if(n.requiredUntil!=n.instructions[0].tick)return false;++found;
                        for(const auto& report:exporter.gateReport)if(report.id==n.id && report.newOff<=n.requiredUntil)return false;
                        }
                  return found==(repeat?2:1);};
            QVERIFY(verify(score.get(),true));QVERIFY(verify(score.get(),false));QVERIFY(score->state()==state);QCOMPARE(score->dirty(),dirty);QCOMPARE(score->undoStack()->getCurIdx(),undo);
            MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});panel.show();QTimer timer;timer.setInterval(40);bool shown=false;int tries=0;
            connect(&timer,&QTimer::timeout,&panel,[&]{
                  auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(++tries>100){dialog->reject();return;}
                  auto table=dialog->findChild<QTableView*>();auto roll=dialog->findChild<QAbstractScrollArea*>("midiCropRoll");if(!table||!roll||table->model()->rowCount()==0)return;
                  for(int row=0;row<table->model()->rowCount();++row)if(table->model()->index(row,0).data().toString()=="C2") {
                        timer.stop();table->selectRow(row);QTest::qWait(30);
                        QVERIFY(table->model()->index(row,5).data().toString().contains(QString::fromUtf8("第1小节第3拍")));
                        const auto range=roll->property("selectedReleaseRange").toPoint();QVERIFY(range.x()>960);
                        const auto tail=roll->property("selectedNewOffHandle").toPoint()-QPoint(3,0);QVERIFY(roll->viewport()->rect().contains(tail));
                        QTest::mousePress(roll->viewport(),Qt::LeftButton,Qt::NoModifier,tail);QTest::mouseMove(roll->viewport(),QPoint(50,tail.y()));QTest::mouseRelease(roll->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(50,tail.y()));
                        const auto moved=roll->property("selectedReleaseRange").toPoint();QCOMPARE(moved,range);
                        const auto out=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");QVERIFY(!out.isEmpty());QVERIFY(dialog->grab().save(out+"/midi-tied-instructions.png"));shown=true;dialog->accept();return;
                        }
                  });
            timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiCropCompare"),Qt::LeftButton);timer.stop();QVERIFY(shown);
            const auto saved=settings.filePath("gate-tied-instructions.mscz");QFileInfo info(saved);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> reopened(mscore->readScore(saved));QVERIFY(reopened);QVERIFY(verify(reopened.get(),true));
            score->tempomap()->setPause(480,.25);QVERIFY(verify(score.get(),true,true));QVERIFY(verify(score.get(),true,true,true));
            }
      void midiInstructionsAfterPhysicalRelease() {
            MidiFile file;file.setDivision(480);MidiTrack track;track.setOutPort(0);track.setOutChannel(0);track.insert(0,MidiEvent(ME_NOTEON,0,60,80));track.insert(400,MidiEvent(ME_NOTEON,0,60,0));file.tracks().append(track);
            MidiGateSource source;source.on=0;source.pitch=60;source.piano=true;MidiGateInstruction late;late.tick=late.sourceTick=480;late.fingeringSet=true;late.fingerMask=4;source.instructions={late};
            auto data=midiGateSnapshot(file,{source});QCOMPARE(data.notes.size(),1);QVERIFY(data.notes[0].known);QVERIFY(data.notes[0].instructions.isEmpty());QCOMPARE(data.notes[0].requiredUntil,-1);
            late.tick=late.sourceTick=400;source.instructions={late};data=midiGateSnapshot(file,{source});QVERIFY(data.notes[0].instructions.isEmpty());
            late.tick=late.sourceTick=240;source.instructions={late};data=midiGateSnapshot(file,{source});QCOMPARE(data.notes[0].instructions.size(),1);QCOMPARE(data.notes[0].requiredUntil,240);
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




      void nativeOnlyNecessaryPreviewAndFile() {
            const bool previousNatural=QSettings().value("export/midi/naturalRelease",true).toBool(),previousEnabled=preferences.getBool("export/midi/cropEnabled");QSettings().setValue("export/midi/naturalRelease",false);preferences.setPreference("export/midi/cropEnabled",true);
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);auto pedal=new Pedal(score.get());pedal->setTick(Fraction());pedal->setTicks(Fraction::fromTicks(1920));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);score->doLayout();const auto state=score->state();const bool dirty=score->dirty();const int undo=score->undoStack()->getCurIdx();
            MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});panel.show();QTimer timer;timer.setInterval(40);int stage=0,tries=0;bool mismatch=false;
            connect(&timer,&QTimer::timeout,&panel,[&]{auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(++tries>150){dialog->reject();return;}auto table=dialog->findChild<QTableView*>("midiCropNotes");auto roll=dialog->findChild<QAbstractScrollArea*>("midiCropRoll");if(!table||!roll||!roll->isEnabled()||!table->model()->rowCount())return;int root=-1,shorter=0;for(int i=0;i<table->model()->rowCount();++i){if(table->model()->index(i,0).data().toString()=="C2")root=i;shorter+=table->model()->index(i,3).data().toInt()<table->model()->index(i,2).data().toInt();}if(root<0)return;
                  if(stage==0){mismatch|=shorter!=1;table->selectRow(root);QTest::mouseClick(dialog->findChild<QPushButton*>("midiCropExclude"),Qt::LeftButton);++stage;}
                  else if(stage==1){mismatch|=shorter!=0;dialog->findChild<QAction*>("midiCropUndoAction")->trigger();++stage;}
                  else if(stage==2){mismatch|=shorter!=1;timer.stop();dialog->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/midi-only-necessary-preview.png");dialog->accept();++stage;}
                  });timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiCropCompare"),Qt::LeftButton);timer.stop();QCOMPARE(stage,3);QVERIFY(!mismatch);
            const auto file=settings.filePath("only-necessary.mid"),original=settings.filePath("only-necessary-original.mid");QVERIFY(panel.write(score.get(),file,original));ExportMidi expected(score.get());QBuffer bytes;bytes.open(QIODevice::WriteOnly);QVERIFY(expected.write(&bytes,preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),preferences.getBool(PREF_IO_MIDI_EXPORTRPNS),mscore->synthesizerState(),panel.options(score.get())));int count=0;for(const auto& n:expected.gateReport)if(n.newOff<n.off){QCOMPARE(n.pitch,36);QCOMPARE(n.reason,QString("hand-release"));++count;}QCOMPARE(count,1);QFile result(file);QVERIFY(result.open(QIODevice::ReadOnly));QCOMPARE(result.readAll(),bytes.data());QVERIFY(score->state()==state);QCOMPARE(score->dirty(),dirty);QCOMPARE(score->undoStack()->getCurIdx(),undo);
            const auto font=qEnvironmentVariable("P0_TEST_SOUNDFONT");QVERIFY(!font.isEmpty());const auto before=render(expected.gateBaseline,expected.gateData,font),after=render(expected.mf,expected.gateData,font);QVERIFY(!before.isEmpty());QCOMPARE(before,after);QSettings().setValue("export/midi/naturalRelease",previousNatural);preferences.setPreference("export/midi/cropEnabled",previousEnabled);
            }
      void midiOnlyNecessaryAndGroupedNatural() {
            auto note=[](QString id,int pitch,int on,int off,int hand){MidiGateNote n;n.id=id;n.pitch=pitch;n.on=on;n.off=n.newOff=off;n.hand=n.handHint=hand;n.piano=n.known=true;n.partId=0;n.voice=hand==0?4:0;n.staff=hand==0?1:0;n.offOrdinal=pitch;n.sourceTick=on;return n;};
            MidiGateData data;data.pedals={{0,0,-1,2500,true}};data.notes={note("root",36,0,1800,0),note("new",60,480,960,0)};MidiGateOptions options;options.naturalRelease=false;MidiGateProcessor processor;auto rows=processor.process(data,options);QCOMPARE(rows[0].reason,QString("hand-release"));QCOMPARE(rows[1].newOff,960);QCOMPARE(rows[1].reason,QString("natural-disabled"));
            options.releaseOverrides["new"]=700;rows=processor.process(data,options);QCOMPARE(rows[1].newOff,700);QCOMPARE(rows[1].reason,QString("manual-release"));
            data.notes.clear();data.pedals={{0,0,-1,241000,true}};for(int group=0;group<100;++group)for(int pitch:{60,64,67})data.notes.append(note(QString("%1-%2").arg(group).arg(pitch),pitch,group*2400,group*2400+960,1));options=MidiGateOptions();QElapsedTimer timer;timer.start();rows=processor.process(data,options);const auto again=processor.process(data,options);QMap<QString,int> ends;
            for(int group=0;group<100;++group){int first=INT_MAX,last=0;for(int k=0;k<3;++k){const int i=group*3+k;QCOMPARE(rows[i].newOff,again[i].newOff);QVERIFY(rows[i].newOff>data.notes[i].on);QVERIFY(rows[i].newOff<data.notes[i].off);first=qMin(first,rows[i].newOff);last=qMax(last,rows[i].newOff);ends[rows[i].id]=rows[i].newOff;}QVERIFY(last-first<=5);}
            std::reverse(data.notes.begin(),data.notes.end());const auto reordered=processor.process(data,options);for(const auto& n:reordered)QCOMPARE(n.newOff,ends[n.id]);qInfo()<<"Natural release 100 chords including replay and reordered input ms"<<timer.elapsed();
            }
      void midiNecessarySettingUseAndCancel() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);QSettings().remove("export/midi/naturalRelease");MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});QVERIFY(panel.options(score.get()).gate.naturalRelease);
            auto configure=[&](MidiCropPanel& target,bool wanted,bool save,bool screenshot){QTimer timer;timer.setInterval(40);bool done=false;int tries=0;connect(&timer,&QTimer::timeout,&target,[&]{auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(++tries>60){dialog->reject();return;}auto check=dialog->findChild<QCheckBox*>("cropNaturalRelease");if(!check)return;timer.stop();if(check->isChecked()!=wanted)QTest::mouseClick(check,Qt::LeftButton,Qt::NoModifier,QPoint(7,check->height()/2));bool enabled=true;for(const auto& name:{"cropNaturalLength","cropNaturalNext","cropNaturalJitter","cropNaturalSeed"}){auto control=dialog->findChild<QWidget*>(name);enabled&=control&&control->isEnabled()==wanted;}done=enabled&&check->isChecked()==wanted;if(screenshot)dialog->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/midi-necessary-settings.png");auto buttons=dialog->findChild<QDialogButtonBox*>();QTest::mouseClick(buttons->button(save?QDialogButtonBox::Ok:QDialogButtonBox::Cancel),Qt::LeftButton);});timer.start();QTest::mouseClick(target.findChild<QPushButton*>("midiCropSettingsButton"),Qt::LeftButton);timer.stop();return done;};
            QVERIFY(configure(panel,false,true,true));QVERIFY(!panel.options(score.get()).gate.naturalRelease);MidiCropPanel reopened(nullptr,[&]{return QList<Score*>{score.get()};});QVERIFY(!reopened.options(score.get()).gate.naturalRelease);QVERIFY(configure(reopened,true,false,false));QVERIFY(!reopened.options(score.get()).gate.naturalRelease);QVERIFY(configure(panel,true,true,false));QVERIFY(panel.options(score.get()).gate.naturalRelease);
            }
      void midiComparisonUsesChosenPedal() {
            std::unique_ptr<MasterScore> score(mscore->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx"));QVERIFY(score);
            auto pedal=new Pedal(score.get());pedal->setTick(Fraction());pedal->setTicks(Fraction::fromTicks(1920));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);PedalPlayback::Settings settings;settings.piano=true;settings.downMs=500;settings.delayFirst=true;score->parts().front()->setProperty(Pid::PEDAL_PLAYBACK,PedalPlayback::encode(settings));score->doLayout();
            ExportMidi expected(score.get()),other(score.get());MidiExportOptions options;options.crop=true;options.writtenPedal=true;QBuffer bytes;bytes.open(QIODevice::WriteOnly);QVERIFY(expected.write(&bytes,true,true,mscore->synthesizerState(),options));options.writtenPedal=false;QBuffer second;second.open(QIODevice::WriteOnly);QVERIFY(other.write(&second,true,true,mscore->synthesizerState(),options));
            int expectedMs=-1,otherMs=-1;MidiGateProcessor clock;clock.setClock(expected.gateData);for(const auto& n:expected.gateReport)if(n.pitch==36)expectedMs=qRound(clock.durationMs(n.on,n.newOff));clock.setClock(other.gateData);for(const auto& n:other.gateReport)if(n.pitch==36)otherMs=qRound(clock.durationMs(n.on,n.newOff));QVERIFY(expectedMs>=0);QVERIFY(expectedMs!=otherMs);
            MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score.get()};});panel.findChild<QCheckBox*>("midiWrittenPedal")->setChecked(true);panel.show();QTimer timer;timer.setInterval(40);int actualMs=-1,tries=0;
            connect(&timer,&QTimer::timeout,&panel,[&]{auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;if(++tries>100){dialog->reject();return;}auto table=dialog->findChild<QTableView*>("midiCropNotes");auto roll=dialog->findChild<QAbstractScrollArea*>("midiCropRoll");if(!table||!roll||!roll->isEnabled()||!table->model()->rowCount())return;for(int row=0;row<table->model()->rowCount();++row)if(table->model()->index(row,0).data().toString()=="C2"){actualMs=table->model()->index(row,3).data().toInt();timer.stop();dialog->accept();return;}});
            timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiCropCompare"),Qt::LeftButton);timer.stop();QSettings().setValue("export/midi/writtenPedal",false);qInfo()<<"Chosen pedal preview/export ms"<<actualMs<<expectedMs<<"piano pedal"<<otherMs;QCOMPARE(actualMs,expectedMs);
            }
      void midiTouchingNotesKeepUniquePairing() {
            MidiFile file;file.setDivision(480);file.setFormat(1);MidiTrack track;track.setOutPort(0);track.setOutChannel(0);
            track.insert(0,MidiEvent(ME_CONTROLLER,0,64,127));track.insert(0,MidiEvent(ME_NOTEON,0,60,85));track.insert(480,MidiEvent(ME_NOTEON,0,60,0));track.insert(480,MidiEvent(ME_NOTEON,0,60,90));track.insert(960,MidiEvent(ME_NOTEON,0,60,0));track.insert(1200,MidiEvent(ME_CONTROLLER,0,64,0));file.tracks().append(track);
            MidiGateSource first;first.piano=true;first.pitch=60;first.staff=first.voice=0;first.hand=1;first.handHint=1;first.partId=0;first.sourceTick=0;auto second=first;second.on=second.sourceTick=480;
            auto data=midiGateSnapshot(file,{first,second});QCOMPARE(data.notes.size(),2);QVERIFY(!data.notes[0].ambiguous);QVERIFY(!data.notes[1].ambiguous);MidiGateProcessor processor;MidiGateOptions options;options.jitter=0;const auto rows=processor.process(data,options);
            for(int i=0;i<2;++i){QVERIFY2(!rows[i].ambiguous,qPrintable(rows[i].reason));QVERIFY(rows[i].newOff<rows[i].off);}
            auto& events=file.tracks()[0].events();auto at=events.equal_range(480);events.erase(at.first,at.second);track=MidiTrack();file.tracks()[0].insert(480,MidiEvent(ME_NOTEON,0,60,90));file.tracks()[0].insert(480,MidiEvent(ME_NOTEON,0,60,0));
            data=midiGateSnapshot(file,{first,second});QVERIFY(data.notes[0].ambiguous);QVERIFY(data.notes[1].ambiguous);const auto overlapping=processor.process(data,options);for(const auto& n:overlapping)QCOMPARE(n.newOff,n.off);
            }
      void midiUncertainHandKeepsCertainRelease() {
            auto note=[](QString id,int pitch,int on,int off,int hand){MidiGateNote n;n.id=id;n.pitch=pitch;n.on=on;n.off=off;n.hand=hand;n.handHint=hand==0?0:1;n.partId=0;n.staff=hand==0?1:0;n.voice=hand==0?4:0;n.known=n.piano=true;n.offOrdinal=pitch;n.sourceTick=on;return n;};
            MidiGateData data;data.pedals={{0,0,0,1900,true}};data.notes={note("root",36,0,1800,0),note("uncertain",100,0,1800,-2),note("c",60,480,960,0),note("e",64,480,960,0),note("g",67,480,960,0)};
            MidiGateProcessor processor;MidiGateOptions options;options.jitter=0;const auto rows=processor.process(data,options);QCOMPARE(rows[0].reason,QString("hand-release"));QVERIFY(rows[0].newOff<480);QCOMPARE(rows[1].hand,-2);QCOMPARE(rows[1].reason,QString("hand-conflict"));QCOMPARE(rows[1].newOff,1800);
            }
      void pianoReleaseKeepsLongerHeldKey() {
            PianoHands::Limits limits[2];PianoHands::Position positions[2];
            QVector<PianoHands::Key> keys={{0,48,1,1,false,true,1},{1,60,1,1,false,true,2},{2,54,1,1,true,false}};
            keys[0].endSeconds=.51;keys[0].releaseSeconds=.44;keys[1].endSeconds=1.9;keys[1].releaseSeconds=.44;keys[2].fingerMask=3;
            const auto plan=PianoHands::plan(keys,limits,positions,.5);QVERIFY(plan.possible);QCOMPARE(plan.released,QVector<int>{0});
            auto duplicate=keys[0];duplicate.id=3;duplicate.endSeconds=1.9;keys[1].endSeconds=.7;keys.append(duplicate);const auto physical=PianoHands::plan(keys,limits,positions,.5);QVERIFY(physical.possible);QCOMPARE(physical.released,QVector<int>{1});QCOMPARE(physical.before[1].count,3);
            }
      void midiNecessaryReleaseMatchesEnumeration() {
            const int oldPitches[]={48,52,55,59},off[]={490,700,1100,1800},newPitches[]={60,64,67};QStringList differences;int cases=0;
            auto canFinger=[](const QVector<int>& masks){std::function<bool(int,int)> assign=[&](int at,int used){if(at==masks.size())return true;for(int f=0;f<5;++f)if((masks[at]&(1<<f))&&!(used&(1<<f))&&assign(at+1,used|(1<<f)))return true;return false;};return assign(0,0);};
            for(int variant=0;variant<3;++variant)for(int oldMask=1;oldMask<16;++oldMask)for(int newMask=1;newMask<8;++newMask){
                  MidiGateData data;data.tempos={{0,500000.}};data.pedals={{0,0,0,2200,true}};QVector<int> oldIndices;
                  for(int k=0;k<7;++k){const bool old=k<4;const int pos=old?k:k-4;if(!(old?oldMask&(1<<pos):newMask&(1<<pos)))continue;MidiGateNote n;n.id=QString::number(k);n.pitch=old?oldPitches[pos]:newPitches[pos];n.hand=n.handHint=0;n.partId=0;n.piano=n.known=true;n.offOrdinal=k;n.voice=old?4:5;n.staff=1;n.on=old?0:480;n.off=n.newOff=old?off[pos]:960;n.sourceTick=n.on;n.finger=old?pos+1:variant==0?pos+2:0;n.fingerMask=old||variant==0?1<<(n.finger-1):variant==1?31:7;if(old)oldIndices.append(data.notes.size());data.notes.append(n);}
                  int bestCount=100,bestLoss=INT_MAX;for(int keep=0;keep<(1<<oldIndices.size());++keep){QVector<int> pitches,masks;int count=0,loss=0;for(int i=0;i<data.notes.size();++i){const auto& n=data.notes[i];const int index=oldIndices.indexOf(i);if(index>=0&&!(keep&(1<<index))){++count;loss+=n.off-423;continue;}pitches.append(n.pitch);masks.append(n.fingerMask);}if(pitches.size()>5||!canFinger(masks))continue;const auto bounds=std::minmax_element(pitches.cbegin(),pitches.cend());const int maximum=PianoHands::white(*bounds.first)&&PianoHands::white(*bounds.second)?16:15;if(*bounds.second-*bounds.first>maximum)continue;if(count<bestCount||count==bestCount&&loss<bestLoss){bestCount=count;bestLoss=loss;}}
                  MidiGateProcessor processor;MidiGateOptions options;options.jitter=0;const auto rows=processor.process(data,options);int actualCount=0,actualLoss=0;for(const auto& n:rows)if(n.reason=="hand-release"){++actualCount;actualLoss+=n.off-n.newOff;}if(bestCount!=actualCount||bestLoss!=actualLoss)differences.append(QString("variant%1 old%2/new%3: %4,%5 != %6,%7").arg(variant).arg(oldMask).arg(newMask).arg(actualCount).arg(actualLoss).arg(bestCount).arg(bestLoss));++cases;
                  }
            qInfo()<<"Necessary release enumeration"<<cases<<"cases,"<<differences.size()<<"different";QVERIFY2(differences.isEmpty(),qPrintable(differences.mid(0,5).join("; ")));
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
      void midiHandChoiceUsesPhysicalRelease() {
            auto note=[](QString id,int pitch,int on,int off,int hand,int hint){MidiGateNote n;n.id=id;n.pitch=pitch;n.on=on;n.off=off;n.hand=hand;n.handHint=hint;n.partId=0;n.staff=hand==0?1:0;n.voice=hand==0?4:0;n.known=n.piano=true;n.offOrdinal=pitch;n.sourceTick=on;return n;};
            MidiGateData data;data.notes={note("left-held",36,0,470,0,0),note("right-free",84,0,24,1,1),note("next",60,480,960,-1,0)};
            MidiGateProcessor processor;MidiGateOptions options;options.jitter=0;
            auto rows=processor.process(data,options);QCOMPARE(rows[2].hand,1);for(int i=0;i<rows.size();++i)QCOMPARE(rows[i].newOff,data.notes[i].off);
            data.notes[0].off=24;data.notes[1].off=470;rows=processor.process(data,options);QCOMPARE(rows[2].hand,0);
            data.notes[0].off=470;data.notes[1].off=24;data.notes[2].hand=0;rows=processor.process(data,options);QCOMPARE(rows[2].hand,0);
            data.notes[2].hand=-1;data.tempos={{0,1000000.},{240,250000.}};rows=processor.process(data,options);QCOMPARE(rows[2].hand,1);
            }
      void midiUncertainFingerKeepsOtherHand() {
            auto note=[](QString id,int pitch,int on,int off,int hand){MidiGateNote n;n.id=id;n.pitch=pitch;n.on=on;n.off=off;n.hand=n.handHint=hand;n.partId=0;n.staff=hand==0?1:0;n.voice=hand==0?4:0;n.known=n.piano=true;n.offOrdinal=pitch;n.sourceTick=on;return n;};
            MidiGateData data;data.pedals={{0,0,0,1900,true}};data.notes={note("root",36,0,1800,0),note("uncertain-right",100,0,1800,1),note("c",60,480,960,0),note("e",64,480,960,0),note("g",67,480,960,0)};data.notes[1].finger=-1;
            MidiGateProcessor processor;MidiGateOptions options;options.jitter=0;const auto rows=processor.process(data,options);QCOMPARE(rows[0].reason,QString("hand-release"));QCOMPARE(rows[1].reason,QString("fingering-uncertain"));QCOMPARE(rows[1].newOff,1800);
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
