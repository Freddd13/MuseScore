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
#include <memory>
#include "mtest/testutils.h"
#include "mscore/notepreview.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/plugin/api/score.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/undo.h"

using namespace Ms;
class TestScoreObserver : public QObject, public MTest {
      Q_OBJECT
   private slots:
      void initTestCase() { initMTest(); }
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
      };
QTEST_MAIN(TestScoreObserver)
#include "tst_scoreobserver.moc"
