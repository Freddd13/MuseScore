#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QScrollBar>
#include <QSpinBox>
#include "mscore/inspector/inspectorNote.h"
#include <QElapsedTimer>
#include <QFile>
#include <memory>
#include "mtest/testutils.h"
#include "mscore/performanceeditor/performanceeditor.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "libmscore/chord.h"
#include "libmscore/excerpt.h"
#include "libmscore/part.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/measure.h"
#include "libmscore/dynamic.h"
#include "libmscore/tempotext.h"
#include "libmscore/pedal.h"
#include "libmscore/undo.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/rendermidi.h"
#include "libmscore/tie.h"
#include "audio/midi/event.h"
#include "mscore/workspace.h"

using namespace Ms;
class TestPerformanceEditor : public QObject, public MTest {
      Q_OBJECT
      QTemporaryDir _directory;
      MasterScore* fixture() { return readScore("mscore/scoreobserver/piano.mscx"); }
      Note* noteAt(Score* score, int tick = 480, int track = 0)
            { return toChord(score->tick2segment(Fraction::fromTicks(tick), false, SegmentType::ChordRest)->element(track))->notes().front(); }
   private slots:
      void initTestCase() { initMTest(); QVERIFY(_directory.isValid()); }
      void cleanupTestCase() { delete Ms::mscore; Ms::mscore = nullptr; }
      void integerPercentAndNearestConversion()
            {
            QCOMPARE(NoteVelocity::effective(180, Note::ValueType::OFFSET_VAL, -50), 90);
            QCOMPARE(NoteVelocity::effective(80, Note::ValueType::OFFSET_VAL, 10), 88);
            QCOMPARE(NoteVelocity::effective(80, Note::ValueType::OFFSET_VAL, -10), 72);
            QCOMPARE(NoteVelocity::offsetFor(20, 127), 535);
            QCOMPARE(NoteVelocity::effective(1, Note::ValueType::OFFSET_VAL, 12600), 127);
            QCOMPARE(NoteVelocity::effective(127, Note::ValueType::OFFSET_VAL, std::numeric_limits<int>::max()), 127);
            for (int base = 1; base <= 127; ++base)
                  for (int value = 1; value <= 127; ++value) {
                        const int offset = NoteVelocity::offsetFor(base, value);
                        const int error = std::abs(NoteVelocity::effective(base, Note::ValueType::OFFSET_VAL, offset) - value);
                        QVERIFY(error <= 1);
                        for (int other = qMax(NoteVelocity::minOffset, offset - 3); other <= qMin(NoteVelocity::maxOffset, offset + 3); ++other)
                              QVERIFY(std::abs(NoteVelocity::effective(base, Note::ValueType::OFFSET_VAL, other) - value) >= error);
                        }
            }
      void nativeVelocityUndoAndPersistence()
            {
            std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
            Note* note = noteAt(score.get());
            QVERIFY(ParameterEdit::velocities(score.get(), {{note, {Note::ValueType::OFFSET_VAL, 535}}}));
            QCOMPARE(note->veloOffset(), 535);
            std::unique_ptr<Element> restored(writeReadElement(note));
            QCOMPARE(toNote(restored.get())->veloOffset(), 535);
            score->undoRedo(true, &ed); QCOMPARE(note->veloOffset(), 0);
            score->undoRedo(false, &ed); QCOMPARE(note->veloOffset(), 535);
            QVERIFY(!ParameterEdit::velocities(score.get(), {{note, {Note::ValueType::OFFSET_VAL, 535}}}));
            score->setIsPlaying(true);
            QVERIFY(!ParameterEdit::velocities(score.get(), {{note, {Note::ValueType::USER_VAL, 100}}}));
            QCOMPARE(note->veloType(), Note::ValueType::OFFSET_VAL);
            score->setIsPlaying(false);
            }
      void staffSpecificBaseAndMidi()
            {
            std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
            auto segment = score->tick2segment(Fraction::fromTicks(0), false, SegmentType::ChordRest);
            auto dynamic = new Dynamic(score.get()); dynamic->setDynamicType("p"); dynamic->setVelocity(40);
            dynamic->setTrack(4); dynamic->setDynRange(Dynamic::Range::STAFF); dynamic->setParent(segment);
            score->startCmd(); score->undoAddElement(dynamic); score->endCmd(); score->updateVelo();
            auto note = noteAt(score.get(), 0, 4);
            QCOMPARE(NoteVelocity::referenceBase(note), 40);
            QVERIFY(NoteVelocity::referenceBase(noteAt(score.get())) != 40);
            ParameterEdit::velocities(score.get(), {{note, {Note::ValueType::OFFSET_VAL, 10}}});
            EventMap events; score->renderMidi(&events, false, false, defaultState);
            bool found = false;
            for (const auto& pair : events) if (pair.second.type() == ME_NOTEON && pair.second.velo() && pair.second.note() == note) {
                  QCOMPARE(pair.second.velo(), 44); found = true;
                  }
            QVERIFY(found);
            }
      void tempoProtectionBatchUndoAndText()
            {
            std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
            auto segment = score->tick2segment(Fraction::fromTicks(0), false, SegmentType::ChordRest);
            auto text = new TempoText(score.get()); text->setTrack(0); text->setParent(segment);
            text->setXmlText("Allegro <sym>metNoteHalfUp</sym> = 60"); text->setTempo(2.0); text->setFollowText(true);
            score->startCmd(); score->undoAddElement(text); score->endCmd();
            QVERIFY(ParameterEdit::tempos(score.get(), {{0, 90}, {480, 80}}, -1));
            QCOMPARE(text->tempo(), 2.0); QCOMPARE(score->tempo(Fraction::fromTicks(480)), 80.0 / 60);
            score->undoRedo(true, &ed); QCOMPARE(score->tempo(Fraction::fromTicks(480)), 2.0);
            score->undoRedo(false, &ed); QCOMPARE(score->tempo(Fraction::fromTicks(480)), 80.0 / 60);
            QVERIFY(ParameterEdit::tempos(score.get(), {{0, 90}}, 0));
            QCOMPARE(text->tempo(), 1.5); QVERIFY(text->xmlText().contains("Allegro")); QVERIFY(text->xmlText().contains("45.00"));
            QVERIFY(!text->followText());
            score->undoRedo(true, &ed); QCOMPARE(text->tempo(), 2.0); QVERIFY(text->followText());
            QString error; QVERIFY(!ParameterEdit::tempos(score.get(), {{0, -10}}, 0, &error)); QVERIFY(!error.isEmpty());
            QCOMPARE(score->_tempoEditDepth, 0);
            const QString path = _directory.path() + "/tempo-roundtrip.mscx";
            QVERIFY(saveScore(score.get(), path));
            std::unique_ptr<MasterScore> reopened(readCreatedScore(path)); QVERIFY(reopened);
            QCOMPARE(reopened->tempo(Fraction::fromTicks(480)), 80.0 / 60);
            }
      void pedalIntervalsAndUndo()
            {
            std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
            QVERIFY(ParameterEdit::pedal(score.get(), 4, 0, 960, nullptr));
            Pedal* pedal = nullptr;
            for (const auto& pair : score->spannerMap().map()) if (pair.second->isPedal()) pedal = toPedal(pair.second);
            QVERIFY(pedal); QCOMPARE(pedal->tick2().ticks(), 960);
            QString error; QVERIFY(!ParameterEdit::pedal(score.get(), 0, 480, 1920, nullptr, &error)); QVERIFY(!error.isEmpty());
            QVERIFY(ParameterEdit::pedal(score.get(), 4, 0, 1920, pedal));
            QCOMPARE(pedal->tick2().ticks(), 1920);
            score->undoRedo(true, &ed); QCOMPARE(pedal->tick2().ticks(), 960);
            EventMap events; score->renderMidi(&events, false, false, defaultState);
            bool on = false, off = false;
            for (const auto& pair : events) if (pair.second.type() == ME_CONTROLLER && pair.second.controller() == CTRL_SUSTAIN) {
                  if (pair.second.value() == 127) on = true;
                  if (pair.second.value() == 0) off = true;
                  }
            QVERIFY(on); QVERIFY(off);
            const QString path = _directory.path() + "/pedal-roundtrip.mscx";
            QVERIFY(saveScore(score.get(), path));
            std::unique_ptr<MasterScore> reopened(readCreatedScore(path)); QVERIFY(reopened);
            bool restored = false;
            for (const auto& pair : reopened->spannerMap().map()) if (pair.second->isPedal()) {
                  QCOMPARE(pair.second->tick().ticks(), 0); QCOMPARE(pair.second->tick2().ticks(), 960); restored = true;
                  }
            QVERIFY(restored);
            }
      void linkedPartParameters()
            {
            std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
            score->startCmd(); score->insertMeasure(ElementType::VBOX, score->first()); score->endCmd();
            auto partScore = new Score(score.get());
            auto excerpt = new Excerpt(score.get()); excerpt->setPartScore(partScore);
            excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano");
            Excerpt::createExcerpt(excerpt); score->excerpts().append(excerpt);
            partScore->doLayout();
            QVERIFY(ParameterEdit::tempos(partScore, {{0, 100}, {480, 80}, {960, 90}}, -1));
            auto segment = partScore->tick2segment(Fraction::fromTicks(480), false, SegmentType::ChordRest);
            TempoText* linkedTempo = nullptr;
            for (auto annotation : segment->annotations()) if (annotation->isTempoText()) linkedTempo = toTempoText(annotation);
            QVERIFY(linkedTempo); QCOMPARE(linkedTempo->tempo(), 80.0 / 60); QVERIFY(!linkedTempo->visible());
            score->undoRedo(true, &ed); QVERIFY(segment->annotations().empty());
            score->undoRedo(false, &ed); QCOMPARE(linkedTempo->tempo(), 80.0 / 60);
            auto linkedNote = noteAt(partScore);
            QVERIFY(ParameterEdit::velocities(partScore, {{linkedNote, {Note::ValueType::USER_VAL, 93}}}));
            // MuseScore deliberately leaves note velocity unlinked between master and parts.
            QCOMPARE(linkedNote->veloOffset(), 93); QCOMPARE(noteAt(score.get())->veloOffset(), 0);
            score->undoRedo(true, &ed); QCOMPARE(linkedNote->veloOffset(), 0);
            QVERIFY(ParameterEdit::pedal(partScore, 4, 0, 960, nullptr));
            bool linkedPedal = false;
            for (const auto& pair : score->spannerMap().map()) if (pair.second->isPedal()) linkedPedal = true;
            QVERIFY(linkedPedal);
            score->undoRedo(true, &ed);
            for (const auto& pair : partScore->spannerMap().map()) QVERIFY(!pair.second->isPedal());
            }
      void actualHostAndMouseInteraction()
            {
            QCoreApplication::setOrganizationName("PerformanceRegression");
            QCoreApplication::setApplicationName("PerformanceEditor");
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, _directory.path());
            dataPath = _directory.path(); MScore::noGui = true; noSeq = true; converterMode = true;
            initMuseScoreResources(); QStringList args; MuseScore::init(args); QVERIFY(Ms::mscore);
            auto main = Ms::mscore;
            auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));
            main->resize(1100, 850); main->show();
            auto action = main->findChild<QAction*>("performance-editor"); QVERIFY(action); action->trigger();
            auto editor = main->findChild<PerformanceEditor*>(); QVERIFY(editor);
            QTest::qWait(150); QVERIFY(editor->isVisible());
            auto canvas = editor->findChildren<QWidget*>();
            QWidget* timeline = nullptr;
            for (auto child : canvas) if (child->focusPolicy() == Qt::StrongFocus && child->minimumHeight() == 190) timeline = child;
            QVERIFY(timeline);
            auto note = noteAt(score);
            score->select(note); score->update(); main->endCmd(); editor->selectionChanged(); QTest::qWait(80);
            editor->setSelectedValue(100); QTest::qWait(80);
            QCOMPARE(note->customizeVelocity(NoteVelocity::referenceBase(note)), 100);
            editor->convertSelected(Note::ValueType::USER_VAL); QTest::qWait(80); QCOMPARE(note->veloType(), Note::ValueType::USER_VAL);
            getAction("inspector")->setChecked(true); main->cmd(getAction("inspector")); QTest::qWait(50);
            auto inspector = main->findChild<InspectorNote*>(); QVERIFY(inspector);
            auto type = inspector->findChild<QComboBox*>("velocityType"); QVERIFY(type);
            auto raw = inspector->findChild<QSpinBox*>("velocity"); QVERIFY(raw);
            QCOMPARE(raw->value(), note->veloOffset());
            type->setCurrentIndex(0); QTest::qWait(80);
            QCOMPARE(note->customizeVelocity(NoteVelocity::referenceBase(note)), 100);
            QVERIFY(raw->maximum() >= 12600);
            type->setCurrentIndex(1); QTest::qWait(80); QCOMPARE(note->veloOffset(), 100);
            const int saved = note->veloOffset();
            score->setIsPlaying(true); editor->setSelectedValue(105); QVERIFY(editor->hasPending()); QCOMPARE(note->veloOffset(), saved); QCOMPARE(raw->value(), saved);
            score->setIsPlaying(false); editor->flushPending(); QTest::qWait(80); QCOMPARE(note->veloOffset(), 105);
            const int old = note->veloOffset();
            auto tool = editor->findChild<QComboBox*>("performanceTool"); QVERIFY(tool); tool->setCurrentIndex(2);
            QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(48, 120));
            QMouseEvent move(QEvent::MouseMove, QPointF(timeline->width() - 12, timeline->height() - 25), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(timeline, &move);
            QCOMPARE(note->veloOffset(), old); QVERIFY(editor->hasPending());
            QTest::keyClick(timeline, Qt::Key_Escape); QVERIFY(!editor->hasPending());
            QTest::mouseRelease(timeline, Qt::LeftButton);
            QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(48, 120)); QApplication::sendEvent(timeline, &move);
            QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, move.pos()); QTest::qWait(80);
            QVERIFY(note->veloOffset() != old); QVERIFY(!editor->hasPending());
            auto handles = editor->findChildren<QCheckBox*>(); for (auto handle : handles) handle->setChecked(true);
            tool->setCurrentIndex(0); QTest::qWait(80);
            auto view = main->currentScoreView();
            const QPoint handle = (view->matrix().mapRect(note->canvasBoundingRect()).topRight() + QPointF(12, -12)).toPoint();
            QVERIFY(view->rect().contains(handle));
            const int beforeHandle = note->veloOffset();
            QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, handle);
            QMouseEvent handleMove(QEvent::MouseMove, QPointF(handle - QPoint(0, 12)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view, &handleMove); QTest::qWait(50);
            QMouseEvent handleMove2(QEvent::MouseMove, QPointF(handle - QPoint(0, 24)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view, &handleMove2);
            QCOMPARE(note->veloOffset(), beforeHandle); QVERIFY(editor->hasPending());
            QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, handleMove2.pos()); QTest::qWait(80);
            QVERIFY(note->veloOffset() > beforeHandle); QCOMPARE(raw->value(), note->veloOffset());
            main->grab().save("performance-editor.png");
            auto parameter = editor->findChild<QComboBox*>("performanceParameter"); QVERIFY(parameter);
            editor->fit(); QTest::qWait(50);
            auto x = [timeline](int tick) { return 48 + qRound((timeline->width() - 70) * tick / 1920.0); };
            auto draw = [timeline](QPoint from, QPoint until) {
                  QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, from);
                  QMouseEvent motion(QEvent::MouseMove, QPointF(until), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                  QApplication::sendEvent(timeline, &motion);
                  QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, until);
                  QTest::qWait(80);
                  };
            const double previousTempo = score->tempo(Fraction::fromTicks(1440));
            parameter->setCurrentIndex(1); tool->setCurrentIndex(2); QTest::qWait(50);
            draw(QPoint(x(0), timeline->height() - 30), QPoint(x(480), 125));
            bool hiddenTempo = false;
            for (auto annotation : score->tick2segment(Fraction::fromTicks(0), false, SegmentType::ChordRest)->annotations())
                  if (annotation->isTempoText()) { QVERIFY(!annotation->visible()); hiddenTempo = true; }
            QVERIFY(hiddenTempo);
            QVERIFY(score->tempo(Fraction::fromTicks(0)) != previousTempo);
            QCOMPARE(score->tempo(Fraction::fromTicks(1440)), previousTempo);
            parameter->setCurrentIndex(2); QTest::qWait(50);
            const int pedalY = 96 + qMax(65, timeline->height() - 120) / 2;
            draw(QPoint(x(0), pedalY), QPoint(x(960), pedalY));
            Pedal* drawnPedal = nullptr;
            for (const auto& pair : score->spannerMap().map()) if (pair.second->isPedal()) drawnPedal = toPedal(pair.second);
            QVERIFY(drawnPedal); QCOMPARE(drawnPedal->tick().ticks(), 0); QCOMPARE(drawnPedal->tick2().ticks(), 960);
            draw(QPoint(x(960), pedalY), QPoint(x(1920), pedalY));
            QCOMPARE(drawnPedal->tick2().ticks(), 1920);
            parameter->setCurrentIndex(0);
            // Closing a score with a live observer and reopening must not retain model pointers.
            editor->setView(nullptr); score->doLayout(); editor->setView(main->currentScoreView()); QTest::qWait(80);
            QVERIFY(editor->isVisible());
            }
      void largeScoreBenchmark()
            {
            if (!qEnvironmentVariableIsSet("PERFORMANCE_BENCHMARK")) return;
            QVERIFY(Ms::mscore);
            auto main = Ms::mscore;
            auto editor = main->findChild<PerformanceEditor*>(); QVERIFY(editor);
            QWidget* timeline = nullptr;
            for (auto child : editor->findChildren<QWidget*>()) if (child->focusPolicy() == Qt::StrongFocus && child->minimumHeight() == 190) timeline = child;
            QVERIFY(timeline);
            for (int wanted : {10000, 50000}) {
                  QByteArray xml("<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part><Staff id=\"1\">");
                  const int measures = (wanted + 31) / 32;
                  for (int m = 0; m < measures; ++m) {
                        xml += "<Measure><voice>";
                        if (m == 0) xml += "<Clef><concertClefType>G</concertClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
                        for (int c = 0; c < 4; ++c) {
                              xml += "<Chord><durationType>quarter</durationType>";
                              for (int n = 0; n < 8; ++n) xml += "<Note><pitch>" + QByteArray::number(60 + n) + "</pitch><tpc>14</tpc></Note>";
                              xml += "</Chord>";
                              }
                        xml += "</voice></Measure>";
                        }
                  xml += "</Staff></Score></museScore>";
                  const QString path = _directory.path() + QString("/benchmark-%1.mscx").arg(wanted);
                  QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(xml); file.close();
                  auto score = main->readScore(path); QVERIFY(score);
                  QElapsedTimer timer; timer.start();
                  main->setCurrentScoreView(main->appendScore(score)); QTest::qWait(100);
                  const double snapshot = timer.nsecsElapsed() / 1e6;
                  score->select(noteAt(score)); score->update(); main->endCmd(); editor->selectionChanged();
                  for (auto check : editor->findChildren<QCheckBox*>()) check->setChecked(false);
                  auto tool = editor->findChild<QComboBox*>("performanceTool"); tool->setCurrentIndex(2);
                  auto scroll = editor->findChild<QScrollBar*>();
                  for (bool overview : {true, false}) {
                        editor->fit(!overview); QTest::qWait(50);
                        const int stableScroll = scroll->value();
                        QVector<double> samples;
                        QPixmap surface(timeline->size());
                        QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(48, 125));
                        for (int frame = 0; frame < 40; ++frame) {
                              QMouseEvent move(QEvent::MouseMove, QPointF(timeline->width() - 16, 135 + frame % 20), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                              timer.start(); QApplication::sendEvent(timeline, &move); timeline->render(&surface);
                              samples.append(timer.nsecsElapsed() / 1e6);
                              }
                        QTest::keyClick(timeline, Qt::Key_Escape); QTest::mouseRelease(timeline, Qt::LeftButton);
                        QCOMPARE(scroll->value(), stableScroll); QVERIFY(!editor->hasPending());
                        std::sort(samples.begin(), samples.end());
                        qInfo("Performance %d notes, %s: snapshot %.2f ms, frame P95 %.2f ms, max %.2f ms", measures * 32, overview ? "overview" : "local", snapshot, samples[37], samples.back());
                        }
                  }
            }
      };
QTEST_MAIN(TestPerformanceEditor)
#include "tst_performanceeditor.moc"
