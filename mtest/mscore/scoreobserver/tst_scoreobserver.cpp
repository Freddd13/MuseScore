//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2026 Freddd13 and contributors
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include <QtTest/QtTest>
#include <QImage>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QPainter>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QUrl>
#include <memory>
#include "mtest/testutils.h"
#include "mscore/musescore.h"
#include "mscore/notepreview.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/plugin/api/score.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/undo.h"
#include "libmscore/pedal.h"
#include "libmscore/tie.h"
#include "libmscore/measure.h"
#include "libmscore/harmony.h"
#include "libmscore/chordlist.h"

using namespace Ms;
class TestScoreObserver : public QObject, public MTest {
      Q_OBJECT
      QTemporaryDir _configuration { QDir::currentPath()+"/observer-XXXXXX" };
   private slots:
      void initTestCase() { initMTest();QVERIFY(_configuration.isValid());dataPath=_configuration.path(); }
      void sustainedNotesAndScope()
            {
            std::unique_ptr<MasterScore> score(readScore("mscore/scoreobserver/piano.mscx"));
            QVERIFY(score);
            PluginAPI::Score wrapped(score.get());
            PluginAPI::ScoreObserver observer;
            observer.setScore(&wrapped);
            const auto snapshot = observer.snapshot(480, 0, 8);
            QCOMPARE(snapshot.value("notes").toList().size(), 6);
            QCOMPARE(snapshot.value("bar").toInt(), 1);
            QCOMPARE(snapshot.value("beat").toInt(), 2);
            const int count = observer.indexBuildCount();
            for (int tick = 480; tick < 950; ++tick) observer.snapshot(tick, 0, 8);
            QCOMPARE(observer.indexBuildCount(), count);
            QCOMPARE(observer.snapshot(960, 0, 8).value("notes").toList().size(), 0);
            QCOMPARE(observer.snapshot(480, 0, 4).value("notes").toList().size(), 3);
            QCOMPARE(observer.snapshot(480, 4, 8).value("notes").toList().size(), 3);
            QCOMPARE(observer.snapshot(480, -20, 999).value("notes").toList().size(), 6);
            score.reset();
            QCOMPARE(observer.snapshot(480, 0, 8).value("notes").toList().size(), 0);
            QVERIFY(!observer.wrappedScore());
            }
      void pedalReleaseAndAnalysisFrames()
            {
            std::unique_ptr<MasterScore> score(readScore("mscore/scoreobserver/piano.mscx"));
            auto pedal = new Pedal(score.get());
            pedal->setTick(Fraction::fromTicks(0));
            pedal->setTicks(Fraction::fromTicks(1440));
            pedal->setTrack(4); pedal->setTrack2(4);
            score->addSpanner(pedal);
            PluginAPI::Score wrapped(score.get());
            PluginAPI::ScoreObserver observer; observer.setScore(&wrapped);
            QCOMPARE(observer.snapshot(1000,0,8).value("notes").toList().size(),0);
            QCOMPARE(observer.contextSnapshot(1000,0,8,true,0).value("analysisNotes").toList().size(),6);
            QCOMPARE(observer.contextSnapshot(1000,0,8,false,0).value("analysisNotes").toList().size(),0);
            QCOMPARE(observer.contextSnapshot(1440,0,8,true,0).value("analysisNotes").toList().size(),0);
            QCOMPARE(observer.contextSnapshot(1000,0,4,true,0).value("analysisNotes").toList().size(),3);
            const auto first=observer.analysisFrames(0,2,0,8,true,0);
            QCOMPARE(first.value("frames").toList().size(),2);
            QVERIFY(first.value("nextTick").toInt()>0);
            QCOMPARE(first.value("fingerprint").toString().size(),64);
            const auto rest=observer.analysisFrames(first.value("nextTick").toInt(),128,0,8,true,0);
            QCOMPARE(rest.value("nextTick").toInt(),-1);
            QCOMPARE(first.value("fingerprint"),rest.value("fingerprint"));
            }
      void arpeggioWindowAndRest()
            {
            std::unique_ptr<MasterScore> score(readScore("mscore/scoreobserver/arpeggio.mscx"));
            QVERIFY(score);
            PluginAPI::Score wrapped(score.get());
            PluginAPI::ScoreObserver observer; observer.setScore(&wrapped);
            QCOMPARE(observer.snapshot(960,0,8).value("notes").toList().size(),1);
            QCOMPARE(observer.contextSnapshot(960,0,8,false,960).value("analysisNotes").toList().size(),3);
            QCOMPARE(observer.contextSnapshot(960,0,8,false,0).value("analysisNotes").toList().size(),1);
            QCOMPARE(observer.contextSnapshot(1440,0,8,false,1920).value("analysisNotes").toList().size(),0);
            }
      void atomicConfigurationRoundTrip()
            {
            QStandardPaths::setTestModeEnabled(true);
            PluginAPI::ScoreObserver observer;
            QVariantMap value {{"schema",1},{"label",QString::fromUtf8("降七")}};
            QVERIFY(observer.saveConfiguration("HarmonyAssistantRegression",value));
            QCOMPARE(observer.loadConfiguration("HarmonyAssistantRegression"),value);
            QVERIFY(!observer.saveConfiguration("../invalid",value));
            QTemporaryDir directory;
            const auto path=directory.filePath("analysis.json");
            QVERIFY(observer.writeTextFile(QUrl::fromLocalFile(path).toString(),QString::fromUtf8("和弦")));
            QCOMPARE(observer.readTextFile(path),QString::fromUtf8("和弦"));
            }
      void previewDoesNotChangeScoreOrExport()
            {
            std::unique_ptr<MasterScore> score(readScore("mscore/scoreobserver/piano.mscx"));
            QVERIFY(score);
            const auto segment = score->firstSegment(SegmentType::ChordRest);
            auto note = toChord(segment->element(4))->notes().front();
            note->setColor(QColor("#123456"));
            const QColor original = note->color();
            const auto state = score->state();
            QTemporaryDir directory;
            QVERIFY(directory.isValid());
            const auto before = directory.filePath("before.mscx");
            const auto after = directory.filePath("after.mscx");
            QVERIFY(saveScore(score.get(), before));
            PluginAPI::Score wrapped(score.get());
            PluginAPI::ScoreObserver observer;
            observer.setScore(&wrapped);
            auto descriptors = observer.snapshot(0, 0, 8).value("notes").toList();
            for (auto& descriptor : descriptors) {
                  auto map = descriptor.toMap(); map.insert("color", "#B75555"); descriptor = map;
                  }
            observer.setNotePreviewColors(descriptors);
            observer.clearNotePreviewColors();
            QCOMPARE(note->color(), original);
            QVERIFY(score->state() == state);
            QVERIFY(saveScore(score.get(), after));
            QFile a(before), b(after);
            QVERIFY(a.open(QIODevice::ReadOnly)); QVERIFY(b.open(QIODevice::ReadOnly));
            QCOMPARE(a.readAll(), b.readAll());
            auto render = [note](bool preview) {
                  QImage image(120, 120, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
                  QPainter painter(&image); painter.translate(40, 60); painter.scale(3, 3);
                  if (preview) note->draw(&painter, QColor("#B75555")); else note->draw(&painter);
                  return image;
                  };
            const auto regular = render(false);
            QVERIFY(regular != render(true));
            QCOMPARE(regular, render(false));
            QCOMPARE(note->color(), original);
            }
      void undoInvalidatesNumericIndex()
            {
            std::unique_ptr<MasterScore> score(readScore("mscore/scoreobserver/piano.mscx"));
            QVERIFY(score);
            PluginAPI::Score wrapped(score.get());
            PluginAPI::ScoreObserver observer;
            observer.setScore(&wrapped);
            const auto original = observer.snapshot(480, 4, 8).value("notes").toList();
            auto note = toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->notes().front();
            score->startCmd();
            score->undo(new ChangePitch(note, note->pitch()+1, 21, 21));
            score->endCmd();
            QVERIFY(observer.snapshot(480, 4, 8).value("notes").toList() != original);
            score->undoRedo(true, nullptr);
            QCOMPARE(observer.snapshot(480, 4, 8).value("notes").toList(), original);
            }
      void layerIsolationAndRemoval()
            {
            NotePreviewLayers layers;
            const auto note = reinterpret_cast<const Note*>(quintptr(1)); // opaque key, never dereferenced
            int first = 0, second = 0;
            const NotePreviewColors red {{note, {Qt::red, QRectF(10, 10, 20, 20)}}};
            const NotePreviewColors blue {{note, {Qt::blue, QRectF(10, 10, 20, 20)}}};
            QVERIFY(!layers.replace(&first, red).isEmpty());
            QVERIFY(layers.replace(&first, red).isEmpty());
            layers.replace(&second, blue);
            QCOMPARE(layers.color(note), QColor(Qt::blue));
            layers.replace(&second, {});
            QCOMPARE(layers.color(note), QColor(Qt::red));
            layers.remove(note);
            QVERIFY(!layers.color(note).isValid());
            layers.clear();
            QVERIFY(layers.empty());
            }
      void fixedMarkerHighlightAndStyle()
            {
            NotePreviewLayers layers; QObject owner;
            const auto first=reinterpret_cast<const Element*>(quintptr(1));
            const auto second=reinterpret_cast<const Element*>(quintptr(2));
            NotePreviewEntry entry;
            entry.chord="Cmaj7";entry.degree="Imaj7";entry.spatium=5;
            entry.chordFont=QFont("Arial");entry.chordFont.setPixelSize(12);
            entry.degreeFont=entry.chordFont;
            const auto horizontal=layoutPreviewChord(entry,0);
            QVERIFY(entry.primaryBox.left()<entry.secondaryBox.left());
            layoutPreviewChord(entry,1);QVERIFY(entry.primaryBox.left()>entry.secondaryBox.left());
            const auto vertical=layoutPreviewChord(entry,2);
            QVERIFY(entry.primaryBox.top()<entry.secondaryBox.top());
            QVERIFY(vertical.height()>horizontal.height());
            layoutPreviewChord(entry,3);QVERIFY(entry.primaryBox.top()>entry.secondaryBox.top());
            entry.chordBox=QRectF(10,10,70,20);entry.bounds=entry.chordBox;
            entry.chordTick=0;entry.chordUntil=960;
            NotePreviewColors base {{first,entry}};
            entry.chordTick=960;entry.chordUntil=1440;entry.chordBox.translate(100,0);entry.bounds=entry.chordBox;
            base.insert(second,entry);layers.replace(&owner,base);
            int activeTick=-1,count=0;
            auto inspect=[&]() {activeTick=-1;count=0;layers.forEachChord([&](const NotePreviewEntry& value,bool active){
                  QCOMPARE(value.chordBox.top(),qreal(10));++count;if(active)activeTick=value.chordTick;});};
            QVERIFY(!layers.setActiveChord(&owner,480).isEmpty());inspect();QCOMPARE(activeTick,0);QCOMPARE(count,2);
            QVERIFY(layers.setActiveChord(&owner,720).isEmpty());
            layers.setActiveChord(&owner,960);inspect();QCOMPARE(activeTick,960);
            // A current note/function layer must not cover the fixed chord underneath.
            QObject foreground;layers.replace(&foreground,{{second,{Qt::blue,QRectF(100,30,20,20)}}});
            inspect();QCOMPARE(count,2);QCOMPARE(activeTick,960);
            layers.setActiveChord(&owner,1440);inspect();QCOMPARE(activeTick,-1);
            layers.remove(first);inspect();QCOMPARE(count,1);
            }
      void multipleChangesOnOneSustainedNote()
            {
            NotePreviewLayers layers;QObject owner;
            const auto source=reinterpret_cast<const Element*>(quintptr(1));
            NotePreviewEntry first;first.sourceAnchor=source;first.chord="C";
            first.chordTick=0;first.chordUntil=240;first.chordBox=QRectF(10,10,40,20);first.bounds=first.chordBox;
            auto second=first;second.chord="Am";second.chordTick=240;second.chordUntil=480;
            second.chordBox.translate(60,0);second.bounds=second.chordBox;second.chordMask=false;
            const NotePreviewMarkers markers{first,second};
            layers.replace(&owner,{{source,{Qt::red,QRectF(10,40,10,10)}}},markers);
            layers.setActiveChord(&owner,240);
            int count=0,active=-1;
            layers.forEachChord([&](const NotePreviewEntry& entry,bool enabled){++count;if(enabled)active=entry.chordTick;});
            QCOMPARE(count,2);QCOMPARE(active,240);QCOMPARE(layers.color(source),QColor(Qt::red));
            QVERIFY(layers.replace(&owner,{{source,{Qt::red,QRectF(10,40,10,10)}}},markers).isEmpty());
            layers.remove(source);count=0;
            layers.forEachChord([&](const NotePreviewEntry&,bool){++count;});
            QCOMPARE(count,0);QVERIFY(!layers.color(source).isValid());
            // Removal compares opaque identities only: source is deliberately not dereferenceable.
            }
      void fixedMarkerHighlightPerformance()
            {
            NotePreviewLayers layers;QObject owner;NotePreviewColors colors;
            for(int i=0;i<6000;++i) {
                  NotePreviewEntry entry;entry.color=Qt::blue;entry.bounds=QRectF(i,30,2,2);
                  if(i%6==0){entry.chord="C";entry.chordBox=QRectF(i,10,10,8);entry.chordTick=i*80;entry.chordUntil=(i+6)*80;}
                  colors.insert(reinterpret_cast<const Element*>(quintptr(i+1)),entry);
                  }
            layers.replace(&owner,colors);QElapsedTimer timer;timer.start();
            for(int i=0;i<1000;++i)layers.setActiveChord(&owner,i*480);
            qInfo("6000 notes / 1000 fixed markers: 1000 indexed highlight changes %.3f ms",double(timer.nsecsElapsed())/1000000.0);
            }
      void longScorePerformance()
            {
            QFile fixture(root + "/mscore/scoreobserver/piano.mscx");
            QVERIFY(fixture.open(QIODevice::ReadOnly));
            QDomDocument document;
            QVERIFY(document.setContent(fixture.readAll()));
            const auto scoreElement = document.documentElement().firstChildElement("Score");
            for (auto staff = scoreElement.firstChildElement("Staff"); !staff.isNull(); staff = staff.nextSiblingElement("Staff")) {
                  const auto measure = staff.firstChildElement("Measure");
                  for (int bar = 2; bar <= 1000; ++bar) {
                        auto next = measure.cloneNode(true).toElement();
                        next.setAttribute("number", bar);
                        auto voice = next.firstChildElement("voice");
                        voice.removeChild(voice.firstChildElement("Clef"));
                        voice.removeChild(voice.firstChildElement("TimeSig"));
                        staff.appendChild(next);
                        }
                  }
            QTemporaryDir directory;
            QFile longScore(directory.filePath("long.mscx"));
            QVERIFY(longScore.open(QIODevice::WriteOnly));
            longScore.write(document.toByteArray());
            longScore.close();
            std::unique_ptr<MasterScore> score(readCreatedScore(longScore.fileName()));
            QVERIFY(score);
            PluginAPI::Score wrapped(score.get());
            PluginAPI::ScoreObserver observer;
            observer.setScore(&wrapped);
            QCOMPARE(observer.snapshot(480, 0, 8).value("notes").toList().size(), 6);
            QElapsedTimer timer;
            timer.start();
            for (int bar = 0; bar < 1000; ++bar)
                  QCOMPARE(observer.snapshot(bar * 1920 + 480, 0, 8).value("notes").toList().size(), 6);
            qInfo("1000 bars / 6000 notes: index %.3f ms; 1000 cached snapshots %.3f ms",
                  observer.indexBuildMilliseconds(), double(timer.nsecsElapsed()) / 1000000.0);
            QCOMPARE(observer.indexBuildCount(), 1);
            timer.restart();
            for (int bar=0;bar<1000;++bar)
                  QCOMPARE(observer.contextSnapshot(bar*1920+480,0,8,true,480).value("analysisNotes").toList().size(),6);
            qInfo("1000 cached context snapshots %.3f ms",double(timer.nsecsElapsed())/1000000.0);
            timer.restart();
            QVariantList preview;
            for (int from=0;from>=0;) {
                  const auto batch=observer.analysisFrames(from,128,0,8,false,0);
                  for(const auto& frame:batch.value("frames").toList())
                        for(const auto& note:frame.toMap().value("notes").toList()) {
                              auto descriptor=note.toMap();
                              if(descriptor.value("tick")!=frame.toMap().value("tick"))continue;
                              descriptor.insert("color","#005d5d"); preview.append(descriptor);
                              }
                  from=batch.value("nextTick").toInt();
                  }
            QCOMPARE(preview.size(),6000);
            qInfo("6000-note numeric analysis frames %.3f ms",double(timer.nsecsElapsed())/1000000.0);
            timer.restart(); observer.setScorePreview(preview);
            qInfo("6000-note base color geometry %.3f ms",double(timer.nsecsElapsed())/1000000.0);
            }
      void cachedSnapshotBenchmark()
            {
            std::unique_ptr<MasterScore> score(readScore("mscore/scoreobserver/piano.mscx"));
            QVERIFY(score);
            PluginAPI::Score wrapped(score.get());
            PluginAPI::ScoreObserver observer;
            observer.setScore(&wrapped);
            observer.snapshot(480, 0, 8);
            QBENCHMARK { observer.snapshot(480, 0, 8); }
            QCOMPARE(observer.indexBuildCount(), 1);
            }

      void tiedAttackAcrossBar()
            {
            std::unique_ptr<MasterScore> score(readScore("libmscore/inputrhythm/blank.mscx"));
            QVERIFY(score);score->startCmd();
            score->setNoteRest(score->firstMeasure()->first(SegmentType::ChordRest),0,NoteVal(60),Fraction(1,1));
            score->setNoteRest(score->tick2segment(Fraction(1,1),false,SegmentType::ChordRest),0,NoteVal(60),Fraction(1,4));
            auto before=toChord(score->findCR(Fraction(),0))->findNote(60);
            auto after=toChord(score->findCR(Fraction(1,1),0))->findNote(60);
            auto tie=new Tie(score.get());tie->setStartNote(before);tie->setEndNote(after);
            tie->setTick(Fraction());tie->setTick2(Fraction(1,1));tie->setTrack(0);score->undoAddElement(tie);score->endCmd();
            auto pedal=new Pedal(score.get());pedal->setTrack(0);pedal->setTick(Fraction());
            pedal->setTick2(Fraction::fromTicks(2400));score->addElement(pedal);
            PluginAPI::Score wrapped(score.get());PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            const auto context=observer.contextSnapshot(1920,0,8,true,0);
            const auto notes=context.value("analysisNotes").toList();QVERIFY(!notes.isEmpty());
            QCOMPARE(notes.front().toMap().value("attackTick").toInt(),0);
            QCOMPARE(context.value("pedalWindows").toList().front().toMap().value("start").toInt(),0);
            }

      void boundedPedalMetadataAndNativeRenderingIsolation()
            {
            std::unique_ptr<MasterScore> score(readScore("mscore/scoreobserver/piano.mscx"));
            QVERIFY(score);score->doLayout();
            auto pedal=new Pedal(score.get());pedal->setTrack(4);pedal->setTick(Fraction());
            pedal->setTick2(Fraction::fromTicks(960));score->addElement(pedal);
            PluginAPI::Score wrapped(score.get());PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            const auto context=observer.contextSnapshot(480,0,8,true,0);
            QCOMPARE(context.value("parts").toList().size(),1);
            QCOMPARE(context.value("pedalWindows").toList().size(),1);
            QCOMPARE(context.value("pedalWindows").toList().front().toMap().value("end").toInt(),960);
            QCOMPARE(observer.contextSnapshot(960,0,8,true,0).value("pedalWindows").toList().size(),0);
            for (const auto note:context.value("analysisNotes").toList())
                  QVERIFY(note.toMap().contains("attackTick"));
            const auto state=score->state();
            const auto descriptions=score->style().chordList()->size();
            auto anchor=context.value("analysisNotes").toList().front().toMap();
            anchor["chord"]="C7(b9,#11)/E";anchor["chordTick"]=0;anchor["chordUntil"]=960;
            observer.setScorePreview({anchor});
            QCOMPARE(score->style().chordList()->size(),descriptions);QVERIFY(score->state()==state);
            observer.clearAllPreviews();
            }
      };
QTEST_MAIN(TestScoreObserver)
#include "tst_scoreobserver.moc"
