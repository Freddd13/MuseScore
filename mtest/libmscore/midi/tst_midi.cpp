//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2012 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include <QCoreApplication>
#include <QFile>
#include <QBuffer>
#include <QIODevice>
#include <QTextStream>
#include <QtTest/QtTest>

#include "audio/exports/exportmidi.h"

#include "libmscore/chord.h"
#include "libmscore/articulation.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include "libmscore/staff.h"
#include "libmscore/notevelocity.h"
#include "libmscore/excerpt.h"
#include <limits>
#include "libmscore/arpeggio.h"
#include "libmscore/tie.h"
#include "libmscore/tremolo.h"
#include "libmscore/trill.h"
#include "libmscore/playbackenvelope.h"
#include "libmscore/rendermidi.h"
#include <QElapsedTimer>
#include "libmscore/playbacktiming.h"
#include "libmscore/note.h"
#include "libmscore/tempotext.h"
#include "libmscore/tempo.h"
#include "libmscore/textline.h"
#include "libmscore/tempoexpression.h"
#include "libmscore/repeatlist.h"
#include "libmscore/fermata.h"
#include <memory>
#include <QTemporaryDir>
#include "libmscore/durationtype.h"
#include "libmscore/keysig.h"
#include "libmscore/mcursor.h"
#include "libmscore/measure.h"
#include "libmscore/mscore.h"
#include "libmscore/note.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"

#include "mtest/testutils.h"
#define DIR QString("libmscore/midi/")

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
#define endl Qt::endl
#define dec Qt::dec
#endif

namespace Ms {
      extern Score::FileError importMidi(MasterScore*, const QString&);
      bool graceNotesMerged(Chord*);
      }

using namespace Ms;

//---------------------------------------------------------
//   TestMidi
//---------------------------------------------------------

class TestMidi : public QObject, public MTest
      {
      Q_OBJECT
      void midiExportTestRef(const QString& file);
      void testMidiExport(MasterScore* score, const QString& writeFile, const QString& refFile);

      void testTimeStretchFermata(MasterScore* score, const QString& file, const QString& testName);
      void testTimeStretchFermataTempoEdit(MasterScore* score, const QString& file, const QString& testName);

   private slots:
      void initTestCase();
      void midi01();
      void envelopeSingleAndTies();
      void envelopeDoubleAndCustom();
      void envelopeTrillAndPersistence();
      void envelopeLinkedParts();
      void envelopeChunkAndDensity();
      void ritCurveTimingAndLayout();
      void ritRestoreAndFermata();
      void ritConflictPersistenceAndParts();
      void accentVelocityPlaybackAndBase();
      void accentPresetsCloneUndoAndPersistence();
      void accentLinkedParts();
      void timedArpeggio();
      void timedArpeggioSpanAndCap();
      void timedArpeggioTies();
      void timedGracePositions();
      void timedGracePrecedingAndCustom();
      void timedGracePersistence();
      void timedGraceRepeatJump();
      void timedGraceMergedTrill();
      void timedGraceGeneratedEvents();
      void timedGraceTiedContinuations();
      void timedGraceShortPredecessor();
      void midi02();
      void midi03();
      void events_data();
      void events();
      void midiBendsExport1() { midiExportTestRef("testBends1"); }
      void midiBendsExport2() { midiExportTestRef("testBends2"); }      // Play property test
      void midiPortExport()   { midiExportTestRef("testMidiPort"); }
      void midiArpeggio()     { midiExportTestRef("testArpeggio"); }
      void midiMutedUnison()  { midiExportTestRef("testMutedUnison"); }
      void midi184376ExportMidiInitialKeySig()
            {
            midiExportTestRef("testInitialKeySigThenRepeatToMeas2");    // tick 0 has Bb keysig.  Meas 2 has no key sig. Meas 2 repeats back to start of Meas 2.  Result should have initial Bb keysig
            midiExportTestRef("testRepeatsWithKeySigs");                // 5 measures, with a key sig on every measure. Meas 3-4 are repeated.
            midiExportTestRef("testRepeatsWithKeySigsExceptFirstMeas"); // 5 measures, with a key sig on every measure except meas 0.  Meas 3-4 are repeated.
            }
      void midiVolta()
          {
          midiExportTestRef("testVoltaTemp"); // test changing temp in prima and seconda volta
          midiExportTestRef("testVoltaDynamic"); // test changing Dynamic in prima and seconda volta
          midiExportTestRef("testVoltaStaffText"); // test changing StaffText in prima and seconda volta
          }
      void midiTimeStretchFermata();
      void midiTimeStretchFermataContinuousView();
      void midiTimeStretchFermataTempoEdit();
      void midiTimeStretchFermataTempoEditContinuousView();
      void midiSingleNoteDynamics();
      };

//---------------------------------------------------------
//   initTestCase
//---------------------------------------------------------

void TestMidi::initTestCase()
      {
      initMTest();
      }


void TestMidi::events_data()
      {
      QTest::addColumn<QString>("file");
      // Test Metronome
      QTest::newRow("testMetronomeSimple") <<  "testMetronomeSimple";
      QTest::newRow("testMetronomeCompound") <<  "testMetronomeCompound";
      QTest::newRow("testMetronomeAnacrusis") <<  "testMetronomeAnacrusis";
      // Test Eighth Swing
      QTest::newRow("testSwing8thSimple") <<  "testSwing8thSimple";
      QTest::newRow("testSwing8thTies") <<  "testSwing8thTies";
      QTest::newRow("testSwing8thTriplets") <<  "testSwing8thTriplets";
      QTest::newRow("testSwing8thDots") <<  "testSwing8thDots";
      // Test Sixteenth Swing
      QTest::newRow("testSwing16thSimple") <<  "testSwing16thSimple";
      QTest::newRow("testSwing16thTies") <<  "testSwing16thTies";
      QTest::newRow("testSwing16thTriplets") <<  "testSwing16thTriplets";
      QTest::newRow("testSwing16thDots") <<  "testSwing16thDots";
      QTest::newRow("testSwingOdd") <<  "testSwingOdd";
      QTest::newRow("testSwingPickup") <<  "testSwingPickup";
      // Test Text Cominations
      QTest::newRow("testSwingStyleText") <<  "testSwingStyleText";
//TODO::ws      QTest::newRow("testSwingTexts") <<  "testSwingTexts";
      // ornaments
      QTest::newRow("testMordents") <<  "testMordents";
      //QTest::newRow("testBaroqueOrnaments") << "testBaroqueOrnaments"; // fail, at least a problem with the first note and stretch
      QTest::newRow("testOrnamentAccidentals") << "testOrnamentAccidentals";
//TODO      QTest::newRow("testGraceBefore") <<  "testGraceBefore";
      QTest::newRow("testBeforeAfterGraceTrill") <<  "testBeforeAfterGraceTrill";
      QTest::newRow("testBeforeAfterGraceTrillPlay=false") <<  "testBeforeAfterGraceTrillPlay=false";
      QTest::newRow("testKantataBWV140Excerpts") <<  "testKantataBWV140Excerpts";
      QTest::newRow("testTrillTransposingInstrument") <<  "testTrillTransposingInstrument";
      QTest::newRow("testAndanteExcerpts") <<  "testAndanteExcerpts";
      QTest::newRow("testTrillLines") << "testTrillLines";
      QTest::newRow("testTrillTempos") << "testTrillTempos";
//      QTest::newRow("testTrillCrossStaff") << "testTrillCrossStaff";
      QTest::newRow("testOrnaments") << "testOrnaments";
      QTest::newRow("testOrnamentsTrillsOttava") << "testOrnamentsTrillsOttava";
      QTest::newRow("testTieTrill") << "testTieTrill";
      // glissando
      QTest::newRow("testGlissando") << "testGlissando";
      QTest::newRow("testGlissandoAcrossStaffs") << "testGlissandoAcrossStaffs";
      QTest::newRow("testGlissando-71826") << "testGlissando-71826";
      // pedal
//      QTest::newRow("testPedal") <<  "testPedal";
      // multi note tremolo
      QTest::newRow("testMultiNoteTremolo") << "testMultiNoteTremolo";
      QTest::newRow("testMultiNoteTremoloTuplet") << "testMultiNoteTremoloTuplet";
      // Test Pauses
      QTest::newRow("testPauses") <<  "testPauses";
      QTest::newRow("testPausesRepeats") <<  "testPausesRepeats";
      QTest::newRow("testPausesTempoTimesigChange") <<  "testPausesTempoTimesigChange";
      QTest::newRow("testGuitarTrem") <<  "testGuitarTrem";
      QTest::newRow("testPlayArticulation") << "testPlayArticulation";
      QTest::newRow("testTremoloDynamics") << "testTremoloDynamics";
      QTest::newRow("testRepeatsDynamics") << "testRepeatsDynamics";
      QTest::newRow("testArticulationDynamics") << "testArticulationDynamics";
      QTest::newRow("testChannelsDynamics") << "testChannelsDynamics";
      }

//---------------------------------------------------------
//   saveMidi
//---------------------------------------------------------

bool saveMidi(Score* score, const QString& name)
      {
      ExportMidi em(score);
      return em.write(name, true, true);
      }


//---------------------------------------------------------
//   compareElements
//---------------------------------------------------------

bool compareElements(Element* e1, Element* e2)
      {
      if (e1->type() != e2->type())
            return false;
      if (e1->type() == ElementType::TIMESIG) {
            }
      else if (e1->type() == ElementType::KEYSIG) {
            KeySig* ks1 = static_cast<KeySig*>(e1);
            KeySig* ks2 = static_cast<KeySig*>(e2);
            if (ks1->key() != ks2->key()) {
                  qDebug("      key signature %d  !=  %d", int(ks1->key()), int(ks2->key()));
                  return false;
                  }
            }
      else if (e1->type() == ElementType::CLEF) {
            }
      else if (e1->type() == ElementType::REST) {
            }
      else if (e1->type() == ElementType::CHORD) {
            Ms::Chord* c1 = static_cast<Ms::Chord*>(e1);
            Ms::Chord* c2 = static_cast<Ms::Chord*>(e2);
            if (c1->ticks() != c2->ticks()) {
                  Fraction f1 = c1->ticks();
                  Fraction f2 = c2->ticks();
                  qDebug("      chord duration %d/%d  !=  %d/%d",
                     f1.numerator(), f1.denominator(),
                     f2.numerator(), f2.denominator()
                     );
                  return false;
                  }
            if (c1->notes().size() != c2->notes().size()) {
                  qDebug("      != note count");
                  return false;
                  }
            int n = c1->notes().size();
            for (int i = 0; i < n; ++i) {
                  Note* n1 = c1->notes()[i];
                  Note* n2 = c2->notes()[i];
                  if (n1->pitch() != n2->pitch()) {
                        qDebug("      != pitch note %d", i);
                        return false;
                        }
                  if (n1->tpc() != n2->tpc()) {
                        qDebug("      note tcp %d != %d", n1->tpc(), n2->tpc());
                        // return false;
                        }
                  }
            }

      return true;
      }

//---------------------------------------------------------
//   compareScores
//---------------------------------------------------------

bool compareScores(Score* score1, Score* score2)
      {
      int staves = score1->nstaves();
      if (score2->nstaves() != staves) {
            printf("   stave count different %d %d\n", staves, score2->nstaves());
            return false;
            }
      Segment* s1 = score1->firstMeasure()->first();
      Segment* s2 = score2->firstMeasure()->first();

      int tracks = staves * VOICES;
      for (;;) {
            for (int track = 0; track < tracks; ++track) {
                  Element* e1 = s1->element(track);
                  Element* e2 = s2->element(track);
                  if ((e1 && !e2) || (e2 && !e1)) {
                        printf("   elements different\n");
                        return false;
                        }
                  if (e1 == 0)
                        continue;
                  if (!compareElements(e1, e2)) {
                        printf("   %s != %s\n", e1->name(), e2->name());
                        return false;
                        }
                  printf("   ok: %s\n", e1->name());
                  }
            s1 = s1->next1();
            s2 = s2->next1();
            if ((s1 && !s2) || (s2 && !s1)) {
                  printf("   segment count different\n");
                  return false;
                  }
            if (s1 == 0)
                  break;
            }
      return true;
      }

//---------------------------------------------------------
///   midi01
///   write/read midi file with timesig 4/4
//---------------------------------------------------------

void TestMidi::midi01()
      {
      MCursor c;
      c.setTimeSig(Fraction(4,4));
      c.createScore("test1a");
      c.addPart("voice");
      c.move(0, Fraction(0,1));     // move to track 0 tick 0

      c.addKeySig(Key(1));
      c.addTimeSig(Fraction(4,4));
      c.addChord(60, TDuration(TDuration::DurationType::V_QUARTER));
      c.addChord(61, TDuration(TDuration::DurationType::V_QUARTER));
      c.addChord(62, TDuration(TDuration::DurationType::V_QUARTER));
      c.addChord(63, TDuration(TDuration::DurationType::V_QUARTER));
      MasterScore* score = c.score();

      score->doLayout();
      score->rebuildMidiMapping();
      c.saveScore();
      saveMidi(score, "test1.mid");

      MasterScore* score2 = new MasterScore(mscore->baseStyle());
      score2->setName("test1b");
      QCOMPARE(importMidi(score2, "test1.mid"), Score::FileError::FILE_NO_ERROR);

      score2->doLayout();
      score2->rebuildMidiMapping();
      MCursor c2(score2);
      c2.saveScore();

      QVERIFY(compareScores(score, score2));

      delete score;
      delete score2;
      }

//---------------------------------------------------------
///   midi02
///   write/read midi file with timesig 3/4
//---------------------------------------------------------

void TestMidi::midi02()
      {
      MCursor c;
      c.setTimeSig(Fraction(3,4));
      c.createScore("test2a");
      c.addPart("voice");
      c.move(0, Fraction(0,1));     // move to track 0 tick 0

      c.addKeySig(Key(2));
      c.addTimeSig(Fraction(3,4));
      c.addChord(60, TDuration(TDuration::DurationType::V_QUARTER));
      c.addChord(61, TDuration(TDuration::DurationType::V_QUARTER));
      c.addChord(62, TDuration(TDuration::DurationType::V_QUARTER));
      MasterScore* score = c.score();

      score->doLayout();
      score->rebuildMidiMapping();
      c.saveScore();
      saveMidi(score, "test2.mid");

      MasterScore* score2 = new MasterScore(mscore->baseStyle());
      score2->setName("test2b");

      QCOMPARE(importMidi(score2, "test2.mid"), Score::FileError::FILE_NO_ERROR);

      score2->doLayout();
      score2->rebuildMidiMapping();
      MCursor c2(score2);
      c2.saveScore();

      QVERIFY(compareScores(score, score2));

      delete score;
      delete score2;
      }

//---------------------------------------------------------
///   midi03
///   write/read midi file with key sig
//---------------------------------------------------------

void TestMidi::midi03()
      {
      MCursor c;
      c.setTimeSig(Fraction(4,4));
      c.createScore("test3a");
      c.addPart("voice");
      c.move(0, Fraction(0,1));     // move to track 0 tick 0

      c.addKeySig(Key(1));
      c.addTimeSig(Fraction(4,4));
      c.addChord(60, TDuration(TDuration::DurationType::V_QUARTER));
      c.addChord(61, TDuration(TDuration::DurationType::V_QUARTER));
      c.addChord(62, TDuration(TDuration::DurationType::V_QUARTER));
      c.addChord(63, TDuration(TDuration::DurationType::V_QUARTER));
      MasterScore* score = c.score();

      score->doLayout();
      score->rebuildMidiMapping();
      c.saveScore();
      saveMidi(score, "test3.mid");

      MasterScore* score2 = new MasterScore(mscore->baseStyle());
      score2->setName("test3b");
      QCOMPARE(importMidi(score2, "test3.mid"), Score::FileError::FILE_NO_ERROR);

      score2->doLayout();
      score2->rebuildMidiMapping();
      MCursor c2(score2);
      c2.saveScore();

      QVERIFY(compareScores(score, score2));

      delete score;
      delete score2;
      }

//---------------------------------------------------------
//   testTimeStretchFermata
//---------------------------------------------------------

void TestMidi::testTimeStretchFermata(MasterScore* score, const QString& file, const QString& testName)
      {
      const QString writeFile = QString("%1-%2-test-%3.mid").arg(file).arg(testName);
      const QString reference(DIR + file + "-ref.mid");

      testMidiExport(score, writeFile.arg(1), reference);

      const Fraction frac1 = 2 * Fraction(4, 4) + Fraction(2, 4); // 3rd measure, 3rd beat
      score->doLayoutRange(frac1, frac1);
      testMidiExport(score, writeFile.arg(2), reference);

      const Fraction frac2 = 6 * Fraction(4, 4); // 7th measure
      score->doLayoutRange(frac2, frac2);
      testMidiExport(score, writeFile.arg(3), reference);
      }

//---------------------------------------------------------
//   midiTimeStretchFermata
//---------------------------------------------------------

void TestMidi::midiTimeStretchFermata()
      {
      const QString file("testTimeStretchFermata");
      const QString readFile(DIR + file + ".mscx");

      MasterScore* score = readScore(readFile);

      testTimeStretchFermata(score, file, "page");

      delete score;
      }

//---------------------------------------------------------
//   midiTimeStretchFermataContinuousView
///   Checks continuous view tempo issues like #289922.
//---------------------------------------------------------

void TestMidi::midiTimeStretchFermataContinuousView()
      {
      const QString file("testTimeStretchFermata");
      const QString readFile(DIR + file + ".mscx");

      MasterScore* score = readScore(readFile);
      score->setLayoutMode(LayoutMode::LINE);
      score->doLayout();

      testTimeStretchFermata(score, file, "linear");

      delete score;
      }

//---------------------------------------------------------
//   testTimeStretchFermataTempoEdit
///   see the issue #290997
//---------------------------------------------------------

void TestMidi::testTimeStretchFermataTempoEdit(MasterScore* score, const QString& file, const QString& testName)
      {
      const QString writeFile = QString("%1-%2-test-%3.mid").arg(file).arg(testName);
      const QString reference(DIR + file + "-%1-ref.mid");

      Element* tempo = score->firstSegment(SegmentType::ChordRest)->findAnnotation(ElementType::TEMPO_TEXT, -1, 3);
      Q_ASSERT(tempo && tempo->isTempoText());

      const int scoreTempo = 200;
      const int defaultTempo = 120;
      const qreal defaultTempoBps = defaultTempo / 60.0;

      testMidiExport(score, writeFile.arg("init"), reference.arg(scoreTempo));

      score->startCmd();
      tempo->undoChangeProperty(Pid::TEMPO_FOLLOW_TEXT, false, PropertyFlags::UNSTYLED);
      tempo->undoChangeProperty(Pid::TEMPO, defaultTempoBps, PropertyFlags::UNSTYLED);
      score->endCmd();
      testMidiExport(score, writeFile.arg("change-tempo"), reference.arg(defaultTempo));

      // undo the last changes
      score->startCmd();
      score->undoRedo(/* undo */ true, /* EditData */ nullptr);
      score->endCmd();
      testMidiExport(score, writeFile.arg("undo-change-tempo"), reference.arg(scoreTempo));

      score->startCmd();
      score->undoRemoveElement(tempo);
      score->endCmd();
      testMidiExport(score, writeFile.arg("remove-tempo"), reference.arg(defaultTempo));

      // undo the last changes
      score->startCmd();
      score->undoRedo(/* undo */ true, /* EditData */ nullptr);
      score->endCmd();
      testMidiExport(score, writeFile.arg("undo-remove-tempo"), reference.arg(scoreTempo));
      }

//---------------------------------------------------------
//   midiTimeStretchFermataTempoEdit
//---------------------------------------------------------

void TestMidi::midiTimeStretchFermataTempoEdit()
      {
      const QString file("testTimeStretchFermataTempoEdit");
      const QString readFile(DIR + file + ".mscx");

      MasterScore* score = readScore(readFile);

      testTimeStretchFermataTempoEdit(score, file, "page");

      delete score;
      }

//---------------------------------------------------------
//   midiTimeStretchFermataTempoEditContinuousView
//---------------------------------------------------------

void TestMidi::midiTimeStretchFermataTempoEditContinuousView()
      {
      const QString file("testTimeStretchFermataTempoEdit");
      const QString readFile(DIR + file + ".mscx");

      MasterScore* score = readScore(readFile);
      score->setLayoutMode(LayoutMode::LINE);
      score->doLayout();

      testTimeStretchFermataTempoEdit(score, file, "linear");

      delete score;
      }

//---------------------------------------------------------
//   midiSingleNoteDynamics
//---------------------------------------------------------

void TestMidi::midiSingleNoteDynamics()
      {
      const QString file("testSingleNoteDynamics");
      QString readFile(DIR   + file + ".mscx");
      QString writeFile(file + "-test.mid");
      QString reference(DIR + file + "-ref.mid");

      MasterScore* score = readScore(readFile);
      score->doLayout();
      testMidiExport(score, writeFile, reference);

      delete score;
      }

void TestMidi::timedGracePositions()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx"));
      auto main = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      score->startCmd();
      auto grace = score->setGraceNote(main, 62, NoteType::APPOGGIATURA, DIVISION / 2);
      score->endCmd();
      SynthesizerState state; EventMap events;
      auto ticks = [&](const Note* note, bool on) {
            QVector<int> result;
            for (const auto& event : events)
                  if (event.second.note() == note && bool(event.second.velo()) == on) result.append(event.first);
            return result;
            };
      score->renderMidi(&events, false, false, state);
      QCOMPARE(ticks(grace, true), QVector<int>({0})); // Legacy long appoggiatura.
      QCOMPARE(ticks(main->downNote(), true), QVector<int>({240}));
      main->setProperty(Pid::GRACE_PLAY_MODE, 1);
      events.clear(); score->renderMidi(&events, false, false, state);
      int begin = ticks(grace, true).front();
      QVERIFY(qAbs(begin + 62) <= 1);
      QCOMPARE(ticks(main->downNote(), true), QVector<int>({0}));
      QVERIFY(qAbs(ticks(grace, false).front() + 1) <= 1);
      QCOMPARE(ticks(main->downNote(), false), QVector<int>({479}));
      QCOMPARE(PlaybackTiming::startTick(events, 0, score.get()), begin);
      ExportMidi exporter(score.get()); QBuffer buffer;
      QVERIFY(exporter.write(&buffer, false, false, state));
      bool first = false, mainOn = false, marker = false;
      for (const auto& track : exporter.mf.tracks()) for (const auto& event : track.events()) {
            QVERIFY(event.first >= 0);
            if (event.second.type() == ME_NOTEON && event.second.velo()) {
                  if (event.second.pitch() == 62 && event.first == 0) first = true;
                  if (event.second.pitch() == 60 && event.first == -begin) mainOn = true;
                  }
            if (event.second.type() == ME_META && event.second.metaType() == META_MARKER) { marker = true; QCOMPARE(event.first, -begin); }
            }
      QVERIFY(first && mainOn && marker);
      main->setProperty(Pid::GRACE_PLAY_MODE, 2);
      events.clear(); score->renderMidi(&events, false, false, state);
      QCOMPARE(ticks(grace, true), QVector<int>({0}));
      QVERIFY(qAbs(ticks(main->downNote(), true).front() - 62) <= 1);
      QVERIFY(qAbs(ticks(main->downNote(), false).front() - 479) <= 1);
      main->setProperty(Pid::GRACE_PLAY_MODE, 3);
      events.clear(); score->renderMidi(&events, false, false, state);
      QCOMPARE(ticks(main->downNote(), true), QVector<int>({0}));
      QVERIFY(qAbs(ticks(grace, true).front() - 418) <= 1);
      QVERIFY(qAbs(ticks(grace, false).front() - 479) <= 1);
      QVERIFY(ticks(main->downNote(), false).front() < ticks(grace, true).front());
      main->setProperty(Pid::GRACE_DURATION_MODE, 1); main->setProperty(Pid::GRACE_DURATION, 25.0);
      QCOMPARE(PlaybackTiming::graceSpanMs(main), 125.0);
      main->setProperty(Pid::GRACE_DURATION, 1000.0); QCOMPARE(PlaybackTiming::graceSpanMs(main), 250.0);
      main->setProperty(Pid::GRACE_DURATION_MODE, 0);
      score->startCmd(); auto other = score->setGraceNote(main, 65, NoteType::GRACE8_AFTER, DIVISION / 2); score->endCmd();
      events.clear(); score->renderMidi(&events, false, false, state);
      QVERIFY(ticks(grace, true).front() < ticks(other, true).front());
      QCOMPARE(main->actualTicks().ticks(), 480);
      QCOMPARE(grace->chord()->noteType(), NoteType::APPOGGIATURA);
      QCOMPARE(other->chord()->noteType(), NoteType::GRACE8_AFTER);
      }

void TestMidi::timedGracePrecedingAndCustom()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx"));
      auto first = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto segment = first->segment()->next(SegmentType::ChordRest);
      auto rest = segment->element(0); segment->remove(rest); delete rest;
      auto second = new Ms::Chord(*first); second->setParent(segment); segment->add(second);
      score->startCmd(); auto grace = score->setGraceNote(second, 62, NoteType::APPOGGIATURA, DIVISION / 2); score->endCmd();
      second->setProperty(Pid::GRACE_PLAY_MODE, 1);
      SynthesizerState state; EventMap events; score->renderMidi(&events, false, false, state);
      int begin = -1, off = -1;
      for (const auto& event : events) {
            if (event.second.note() == grace && event.second.velo()) begin = event.first;
            if (event.second.note() == first->downNote() && !event.second.velo()) off = event.first;
            }
      QVERIFY(qAbs(begin - 418) <= 1); QCOMPARE(begin, PlaybackTiming::graceStartTick(second)); QCOMPARE(off, begin - 1);
      auto otherStaff = toChord(first->segment()->element(4))->downNote();
      for (const auto& event : events) if (event.second.note() == otherStaff && !event.second.velo()) QCOMPARE(event.first, 479);
      QCOMPARE(first->downNote()->playEvents().front().len(), 1000); // No model mutation.
      first->setPlayEventType(PlayEventType::User);
      events.clear(); score->renderMidi(&events, false, false, state);
      for (const auto& event : events) if (event.second.note() == first->downNote() && !event.second.velo()) QCOMPARE(event.first, 479);
      grace->chord()->setPlayEventType(PlayEventType::User);
      auto& custom = grace->playEvents(); custom.clear(); custom.append(NoteEvent(0, -180, 100));
      events.clear(); score->renderMidi(&events, false, false, state);
      QCOMPARE(custom.front().ontime(), -180); QCOMPARE(custom.front().len(), 100);
      QCOMPARE(PlaybackTiming::graceSpanMs(second), 0.0);
      }

void TestMidi::timedGraceRepeatJump()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-grace-repeat.mscx")); QVERIFY(score);
      SynthesizerState state; EventMap events; score->renderMidi(&events, false, true, state);
      auto last = toChord(score->tick2segment(Fraction::fromTicks(1440), false, SegmentType::ChordRest)->element(0))->downNote();
      auto first = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto afterRepeat = toChord(score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest)->element(0));
      QVector<int> offs, starts;
      for (const auto& event : events) {
            if (event.second.note() == last && !event.second.velo()) offs.append(event.first);
            if (event.second.note() && event.second.note()->chord()->isGrace() && event.second.velo()) starts.append(event.first);
            }
      QCOMPARE(offs.size(), 2); QCOMPARE(starts.size(), 3);
      QCOMPARE(offs[0], starts[1] - 1); QCOMPARE(offs[1], starts[2] - 1);
      QVERIFY(qAbs(starts[1] - (1920 + PlaybackTiming::graceStartTick(first))) <= 1);
      QVERIFY(qAbs(starts[2] - (1920 + PlaybackTiming::graceStartTick(afterRepeat))) <= 1);
      }

void TestMidi::timedGraceMergedTrill()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "testBeforeAfterGraceTrill.mscx")); QVERIFY(score);
      SynthesizerState state; EventMap before, after; score->renderMidi(&before, false, false, state);
      for (auto s = score->firstMeasure()->first(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (int track = 0; track < score->ntracks(); ++track) {
                  auto element = s->element(track);
                  if (element && element->isChord() && graceNotesMerged(toChord(element))) element->setProperty(Pid::GRACE_PLAY_MODE, 1);
                  }
      score->renderMidi(&after, false, false, state);
      QCOMPARE(after.size(), before.size());
      auto a = after.begin();
      for (auto b = before.begin(); b != before.end(); ++b, ++a) {
            QCOMPARE(a->first, b->first); QCOMPARE(a->second.type(), b->second.type());
            QCOMPARE(a->second.pitch(), b->second.pitch()); QCOMPARE(a->second.velo(), b->second.velo());
            }
      }

void TestMidi::timedGraceGeneratedEvents()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx"));
      auto main = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      score->startCmd(); auto grace = score->setGraceNote(main, 62, NoteType::APPOGGIATURA, DIVISION / 2); score->endCmd();
      auto tremolo = new Tremolo(score.get()); tremolo->setTremoloType(TremoloType::R16); main->add(tremolo);
      SynthesizerState state; EventMap events;
      for (int mode : {2, 3}) {
            main->setProperty(Pid::GRACE_PLAY_MODE, mode);
            events.clear(); score->renderMidi(&events, false, false, state);
            QVector<int> on, off; int graceOn = -1;
            for (const auto& event : events) {
                  if (event.second.note() == main->downNote()) (event.second.velo() ? on : off).append(event.first);
                  if (event.second.note() == grace && event.second.velo()) graceOn = event.first;
                  }
            QCOMPARE(on.size(), 4); QCOMPARE(off.size(), 4);
            if (mode == 2) { QVERIFY(qAbs(on.front() - 62) <= 1); QVERIFY(qAbs(off.back() - 479) <= 1); QCOMPARE(graceOn, 0); }
            else { QCOMPARE(on.front(), 0); QVERIFY(off.back() < graceOn); QVERIFY(qAbs(graceOn - 418) <= 1); }
            }
      }

void TestMidi::timedGraceTiedContinuations()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx"));
      auto first = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto segment = first->segment()->next(SegmentType::ChordRest);
      auto rest = segment->element(0); segment->remove(rest); delete rest;
      auto second = new Ms::Chord(*first); second->setParent(segment); segment->add(second);
      auto tie = new Tie(score.get()); tie->setStartNote(first->downNote()); tie->setEndNote(second->downNote()); first->downNote()->add(tie);
      score->startCmd(); score->setGraceNote(second, 62, NoteType::APPOGGIATURA, DIVISION / 2); score->endCmd();
      SynthesizerState state; EventMap events;
      for (int mode : {1, 2}) {
            second->setProperty(Pid::GRACE_PLAY_MODE, mode); events.clear(); score->renderMidi(&events, false, false, state);
            int off = -1, repeated = 0;
            for (const auto& event : events) {
                  if (event.second.note() == first->downNote() && !event.second.velo()) off = event.first;
                  if (event.second.note() == second->downNote() && event.second.velo()) ++repeated;
                  }
            QCOMPARE(off, 959); QCOMPARE(repeated, 0);
            }
      second->setProperty(Pid::GRACE_PLAY_MODE, 1);
      score->startCmd(); score->setGraceNote(first, 65, NoteType::APPOGGIATURA, DIVISION / 2); score->endCmd();
      first->setProperty(Pid::GRACE_PLAY_MODE, 3);
      events.clear(); score->renderMidi(&events, false, false, state);
      for (const auto& event : events) if (event.second.note() == first->downNote() && !event.second.velo()) QCOMPARE(event.first, 959);
      QCOMPARE(first->downNote()->tieFor(), tie); QCOMPARE(second->downNote()->tieBack(), tie);
      }

void TestMidi::timedGraceShortPredecessor()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-grace-short.mscx")); QVERIFY(score);
      auto first = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto second = toChord(first->segment()->next(SegmentType::ChordRest)->element(0));
      QCOMPARE(second->tick().ticks(), 30);
      QVERIFY(qAbs(PlaybackTiming::graceSpanMs(second) - 15.625) < 0.01);
      SynthesizerState state; EventMap events; score->renderMidi(&events, false, false, state);
      int off = -1, graceOn = -1;
      for (const auto& event : events) {
            if (event.second.note() == first->downNote() && !event.second.velo()) off = event.first;
            if (event.second.note() && event.second.note()->chord()->isGrace() && event.second.velo()) graceOn = event.first;
            }
      QVERIFY(off > 0 && off < graceOn); QVERIFY(graceOn >= 15 && graceOn < 30);
      QCOMPARE(PlaybackTiming::exportOffset(events), 0);
      }

void TestMidi::timedGracePersistence()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx"));
      auto main = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      score->startCmd(); auto grace = score->setGraceNote(main, 62, NoteType::APPOGGIATURA, DIVISION / 2); score->endCmd();
      TDuration written(TDuration::DurationType::V_16TH); written.setDots(1);
      score->startCmd(); grace->chord()->undoChangeProperty(Pid::GRACE_APPEARANCE, Ms::Chord::graceAppearanceValue(NoteType::GRACE16, written)); score->endCmd();
      QCOMPARE(grace->chord()->durationType(), written); QCOMPARE(grace->chord()->noteType(), NoteType::GRACE16);
      EditData appearanceEdit; score->undoRedo(true, &appearanceEdit);
      QCOMPARE(grace->chord()->durationType(), TDuration(TDuration::DurationType::V_EIGHTH));
      QCOMPARE(grace->chord()->noteType(), NoteType::APPOGGIATURA);
      score->undoRedo(false, &appearanceEdit); QCOMPARE(grace->chord()->durationType(), written);
      score->undoRedo(true, &appearanceEdit); // Continue with the original eighth appearance.
      score->startCmd(); grace->chord()->propertyDelegate(Pid::GRACE_PLAY_MODE)->undoChangeProperty(Pid::GRACE_PLAY_MODE, 1);
      main->undoChangeProperty(Pid::GRACE_DURATION, 73.0); score->endCmd();
      EditData ed; score->undoRedo(true, &ed); QCOMPARE(main->getProperty(Pid::GRACE_PLAY_MODE).toInt(), 0);
      QCOMPARE(main->getProperty(Pid::GRACE_DURATION).toDouble(), 65.0);
      score->undoRedo(false, &ed); QCOMPARE(grace->chord()->getProperty(Pid::GRACE_PLAY_MODE).toInt(), 1);
      std::unique_ptr<Ms::Chord> copy(new Ms::Chord(*main)); QCOMPARE(copy->getProperty(Pid::GRACE_DURATION).toDouble(), 73.0);
      QTemporaryDir dir; QVERIFY(dir.isValid());
      for (const auto& suffix : {"mscx", "mscz"}) {
            QString path = dir.path() + "/grace." + suffix;
            if (QString(suffix) == "mscz") { QFileInfo info(path); QVERIFY(score->saveCompressedFile(info, false, false)); }
            else QVERIFY(saveScore(score.get(), path));
            std::unique_ptr<MasterScore> restored(readCreatedScore(path)); QVERIFY(restored);
            auto chord = toChord(restored->firstMeasure()->first(SegmentType::ChordRest)->element(0));
            QCOMPARE(chord->getProperty(Pid::GRACE_PLAY_MODE).toInt(), 1);
            QCOMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 73.0);
            QCOMPARE(chord->graceNotes().size(), 1);
            QCOMPARE(chord->graceNotes().front()->noteType(), NoteType::APPOGGIATURA);
            }
      }

void TestMidi::timedArpeggio()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx"));
      QVERIFY(score);
      auto chord = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto a = new Arpeggio(score.get()); a->setParent(chord); a->setTrack(0);
      chord->add(a);
      QCOMPARE(a->timingMode(), 0); // Constructors/readers preserve legacy scores.
      std::unique_ptr<Element> old(writeReadElement(a)); QCOMPARE(toArpeggio(old.get())->timingMode(), 0);
      a->setProperty(Pid::ARP_TIMING_MODE, 2);
      std::unique_ptr<Element> restored(writeReadElement(a)); QCOMPARE(toArpeggio(restored.get())->timingMode(), 2);
      SynthesizerState state; EventMap events; score->renderMidi(&events, false, false, state);
      QVector<int> onset, off;
      for (auto& event : events) if (event.second.note() && event.second.note()->chord() == chord)
            (event.second.velo() ? onset : off).append(event.first);
      QCOMPARE(onset.size(), 3); QCOMPARE(onset.back(), 0);
      QVERIFY(qAbs(onset.front() + 125) <= 2);
      for (int tick : off) QCOMPARE(tick, 479);
      QCOMPARE(PlaybackTiming::startTick(events, 0, score.get()), onset.front());
      QVERIFY(PlaybackTiming::time(score.get(), onset.front()) < -0.12);
      ExportMidi exporter(score.get()); QBuffer midi;
      QVERIFY(exporter.write(&midi, false, false, state));
      bool marker = false, finalOnBeat = false, firstAtZero = false;
      for (const auto& track : exporter.mf.tracks()) for (const auto& event : track.events()) {
            QVERIFY(event.first >= 0);
            if (event.second.type() == ME_META && event.second.metaType() == META_MARKER) {
                  QCOMPARE(event.first, -onset.front()); marker = true;
                  }
            if (event.second.type() == ME_NOTEON && event.second.velo()) {
                  if (event.second.pitch() == 67 && event.first == -onset.front()) finalOnBeat = true;
                  if (event.second.pitch() == 60 && event.first == 0) firstAtZero = true;
                  }
            }
      QVERIFY(marker); QVERIFY(firstAtZero); QVERIFY(finalOnBeat);

      auto second = toChord(score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest)->element(0));
      auto b = new Arpeggio(score.get()); b->setParent(second); b->setTrack(0); b->setProperty(Pid::ARP_TIMING_MODE, 2); second->add(b);
      events.clear(); score->renderMidi(&events, false, false, state);
      int start = PlaybackTiming::startTick(events, second->tick().ticks(), score.get());
      QVERIFY(start > 1700 && start < 1920); // Earlier occurrences of the same pitch are excluded.
      a->setProperty(Pid::ARP_TIMING_MODE, 1); a->setArpeggioType(ArpeggioType::DOWN);
      events.clear(); score->renderMidi(&events, false, false, state);
      QCOMPARE(chord->upNote()->playEvents().front().ontime(), 0);
      QVERIFY(chord->downNote()->playEvents().front().ontime() > 0);
      }

void TestMidi::timedArpeggioTies()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx"));
      auto first = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto segment = first->segment()->next(SegmentType::ChordRest);
      auto rest = segment->element(0); segment->remove(rest); delete rest;
      auto second = new Ms::Chord(*first); second->setParent(segment); segment->add(second);
      auto tie = new Tie(score.get()); tie->setStartNote(first->downNote()); tie->setEndNote(second->downNote()); first->downNote()->add(tie);
      for (auto chord : {first, second}) {
            auto a = new Arpeggio(score.get()); a->setParent(chord); a->setTrack(0); a->setProperty(Pid::ARP_TIMING_MODE, 2); chord->add(a);
            }
      SynthesizerState state; EventMap events; score->renderMidi(&events, false, false, state);
      int off = -1, repeated = 0;
      for (const auto& event : events) {
            if (event.second.note() == first->downNote() && !event.second.velo()) off = event.first;
            if (event.second.note() == second->downNote() && event.second.velo()) ++repeated;
            }
      QCOMPARE(repeated, 0); QCOMPARE(off, 959);
      QCOMPARE(second->downNote()->playEvents().size(), 1);
      QCOMPARE(first->downNote()->tieFor(), tie); QCOMPARE(second->downNote()->tieBack(), tie);
      }

void TestMidi::timedArpeggioSpanAndCap()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx"));
      SynthesizerState state; EventMap events;
      auto segment = score->firstMeasure()->first(SegmentType::ChordRest);
      auto top = toChord(segment->element(0)), bottom = toChord(segment->element(4));
      auto a = new Arpeggio(score.get()); a->setParent(top); a->setTrack(0); a->setSpan(2);
      a->setProperty(Pid::ARP_TIMING_MODE, 2); a->setProperty(Pid::ARP_INTERVAL_MS, 1000.0); top->add(a);
      QCOMPARE(PlaybackTiming::arpeggio(bottom), a);
      QCOMPARE(PlaybackTiming::arpeggioNotes(a).size(), 6);
      QVERIFY(qAbs(PlaybackTiming::intervalMs(a) - 50.0) < 0.01);
      events.clear(); score->renderMidi(&events, false, false, state);
      QCOMPARE(top->upNote()->playEvents().front().ontime(), 0);
      QCOMPARE(bottom->downNote()->playEvents().front().ontime(), -500);
      bottom->downNote()->setPlay(false);
      QCOMPARE(PlaybackTiming::arpeggioNotes(a).size(), 5);
      a->setArpeggioType(ArpeggioType::DOWN); events.clear(); score->renderMidi(&events, false, false, state);
      QCOMPARE(bottom->notes()[1]->playEvents().front().ontime(), 0);
      }

//---------------------------------------------------------
//   events
//---------------------------------------------------------

void TestMidi::events()
      {
      QFETCH(QString, file);

      QString readFile(DIR   + file + ".mscx");
      QString writeFile(file + "-test.txt");
      QString reference(DIR + file + "-ref.txt");

      MasterScore* score = readScore(readFile);
      EventMap events;
      // a temporary, uninitialized synth state so we can render the midi - should fall back correctly
      SynthesizerState ss;
      score->renderMidi(&events, ss);
      qDebug() << "Opened score " << readFile;
      QFile filehandler(writeFile);
      filehandler.open(QIODevice::WriteOnly | QIODevice::Text);
      QTextStream out(&filehandler);

      for (auto iter = events.begin(); iter!= events.end(); ++iter){
            if (iter->second.discard())
                  continue;
            out << qSetFieldWidth(5) << "Tick  =  ";
            out << qSetFieldWidth(5) << iter->first;
            out << qSetFieldWidth(5) << "   Type  = ";
            out << qSetFieldWidth(5) << iter->second.type();
            out << qSetFieldWidth(5) << "   Pitch  = ";
            out << qSetFieldWidth(5) << iter->second.dataA();
            out << qSetFieldWidth(5) << "   Velocity  = ";
            out << qSetFieldWidth(5) << iter->second.dataB();
            out << qSetFieldWidth(5) << "   Channel  = ";
            out << qSetFieldWidth(5) << iter->second.channel();
            out << endl;
            }
      filehandler.close();

      QVERIFY(score);
      QVERIFY(compareFiles(writeFile, reference));
     // QVERIFY(saveCompareScore(score, writeFile, reference));

      delete score;
      }

//---------------------------------------------------------
//   testMidiExport
//---------------------------------------------------------

void TestMidi::testMidiExport(MasterScore* score, const QString& writeFile, const QString& refFile)
      {
      Q_ASSERT(writeFile.endsWith(".mid") && refFile.endsWith(".mid"));
      QVERIFY(saveMidi(score, writeFile));
      QVERIFY(compareFiles(writeFile, refFile));
      }

//---------------------------------------------------------
//   midiExportTest
//   read a MuseScore mscx file, write to a MIDI file and verify against reference
//---------------------------------------------------------

void TestMidi::midiExportTestRef(const QString& file)
      {
      MScore::debugMode = true;
      MasterScore* score = readScore(DIR + file + ".mscx");
      QVERIFY(score);
      score->doLayout();
      score->rebuildMidiMapping();
      testMidiExport(score, QString(file) + ".mid", DIR + QString(file) + "-ref.mid");
      delete score;
      }

#include "accent_playback_tests.inc"
#include "rit_playback_tests.inc"
#include "envelope_playback_tests.inc"
QTEST_MAIN(TestMidi)

#include "tst_midi.moc"
