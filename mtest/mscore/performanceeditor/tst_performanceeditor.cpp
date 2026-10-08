#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QWheelEvent>
#include <QToolButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMenu>
#include <QHelpEvent>
#include <QToolTip>
#include <QPaintEvent>
#include <QToolBar>
#include <QLabel>
#include <QClipboard>
#include "mscore/mssplashscreen.h"
#include "mscore/musescoredialogs.h"
#include "personalbranding.h"
#include "mscore/pianoroll/pianoview.h"
#include "libmscore/ottava.h"
#include "libmscore/instrument.h"
#include "libmscore/staff.h"
#include "mscore/performanceeditor/performanceselection.h"
#include "libmscore/select.h"
#include "libmscore/repeatlist.h"
#include "mscore/inspector/inspectorNote.h"
#include "mscore/inspector/inspectorArpeggio.h"
#include "libmscore/arpeggio.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
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
#include "libmscore/system.h"
#include "libmscore/layoutbreak.h"
#include "libmscore/dynamic.h"
#include "libmscore/tempotext.h"
#include "libmscore/pedal.h"
#include "libmscore/undo.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/rendermidi.h"
#include "libmscore/tie.h"
#include "audio/midi/event.h"
#include "mscore/workspace.h"
#include "mscore/seq.h"
#include "audio/midi/msynthesizer.h"
#include "audio/midi/synthesizer.h"
#include <functional>

using namespace Ms;
// Exercise the real sequencer without touching the machine's audio/MIDI devices.
class PerformanceSilentSynth : public Synthesizer {
      QList<MidiPatch*> _patches;
   public:
      const char* name() const override { return "Fluid"; }
      bool loadSoundFonts(const QStringList&) override { return true; }
      std::vector<SoundFontInfo> soundFontsInfo() const override { return {}; }
      void process(unsigned, float*, float*, float*) override {}
      void play(const PlayEvent&) override {}
      const QList<MidiPatch*>& getPatchInfo() const override { return _patches; }
      SynthesizerGroup state() const override { return SynthesizerGroup("Fluid", {}); }
      bool setState(const SynthesizerGroup&) override { return true; }
};
class PerformanceAuditionSeq : public Seq {
   public:
      int previews = 0, previewPitch = -1, previewVelocity = -1;
      void startNote(int channel, int pitch, int velocity, int duration, double tuning) override
            { ++previews; previewPitch = pitch; previewVelocity = velocity; Seq::startNote(channel, pitch, velocity, duration, tuning); }
};
class PerformancePaintDamage : public QObject {
   public:
      QRegion region;
      bool eventFilter(QObject*, QEvent* event) override
            { if (event->type() == QEvent::Paint) region |= static_cast<QPaintEvent*>(event)->region(); return false; }
};
class PerformanceRecordingSynth : public PerformanceSilentSynth {
   public:
      unsigned frames = 0;
      QVector<unsigned> onFrames;
      QVector<int> onPitches;
      void process(unsigned n, float*, float*, float*) override { frames += n; }
      void play(const PlayEvent& event) override { if (event.type() == ME_NOTEON && event.velo()) { onFrames.append(frames); onPitches.append(event.pitch()); } }
};
class PerformanceTestDriver : public Driver {
      Transport _state = Transport::STOP;
      float _buffer[8192] = {};
   public:
      explicit PerformanceTestDriver(Seq* sequence) : Driver(sequence) {}
      bool init(bool = false) override { return true; }
      bool start(bool = false) override { return true; }
      bool stop() override { stopTransport(); return true; }
      void stopTransport() override { _state = Transport::STOP; seq->process(0, _buffer); }
      void startTransport() override { _state = Transport::PLAY; seq->process(0, _buffer); }
      Transport getState() override { return _state; }
      int sampleRate() const override { return 44100; }
      void pulse(unsigned frames = 2048) { seq->process(frames, _buffer); }
};
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
            QWidget* timeline = editor->findChild<QWidget*>("performanceParameterCanvas");
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
            main->grab().save("performance-editor-initial.png");
            qInfo("Timeline %dx%d; tool %d", timeline->width(), timeline->height(), tool->currentIndex());
            QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 25));
            QMouseEvent move(QEvent::MouseMove, QPointF(timeline->width() - 12, timeline->height() - 25), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(timeline, &move);
            QTest::qWait(25); QCOMPARE(note->veloOffset(), old); QTRY_VERIFY(editor->hasPending());
            QTest::keyClick(timeline, Qt::Key_Escape); QVERIFY(!editor->hasPending());
            QTest::mouseRelease(timeline, Qt::LeftButton);
            QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 25)); QApplication::sendEvent(timeline, &move);
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
            QTest::qWait(25); QCOMPARE(note->veloOffset(), beforeHandle); QVERIFY(editor->hasPending());
            QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, handleMove2.pos()); QTest::qWait(80);
            QVERIFY(note->veloOffset() > beforeHandle); QCOMPARE(raw->value(), note->veloOffset());
            main->grab().save("performance-editor.png");
            auto parameter = editor->findChild<QComboBox*>("performanceParameter"); QVERIFY(parameter);
            editor->fit(); QTest::qWait(50);
            auto x = [timeline](int tick) { return 76 + qRound((timeline->width() - 90) * tick / 1920.0); };
            auto draw = [timeline](QPoint from, QPoint until) {
                  QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, from);
                  QMouseEvent motion(QEvent::MouseMove, QPointF(until), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                  QApplication::sendEvent(timeline, &motion);
                  QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, until);
                  QTest::qWait(80);
                  };
            const double previousTempo = score->tempo(Fraction::fromTicks(1440));
            parameter->setCurrentIndex(1); tool->setCurrentIndex(2); QTest::qWait(50);
            draw(QPoint(x(0), timeline->height() - 30), QPoint(x(480), 30));
            bool hiddenTempo = false;
            for (auto annotation : score->tick2segment(Fraction::fromTicks(0), false, SegmentType::ChordRest)->annotations())
                  if (annotation->isTempoText()) { QVERIFY(!annotation->visible()); hiddenTempo = true; }
            QVERIFY(hiddenTempo);
            QVERIFY(score->tempo(Fraction::fromTicks(0)) != previousTempo);
            QCOMPARE(score->tempo(Fraction::fromTicks(1440)), previousTempo);
            parameter->setCurrentIndex(2); QTest::qWait(50);
            const int pedalY = timeline->height() / 2;
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
      void independentViewsSelectionAndSeek()
            {
            auto main = Ms::mscore; QVERIFY(main);
            auto editor = main->findChild<PerformanceEditor*>(); QVERIFY(editor);
            auto score = main->currentScore(); auto note = noteAt(score);
            auto notes = editor->findChild<QWidget*>("performanceNoteCanvas"); auto ruler = editor->findChild<QWidget*>("performanceRuler");
            auto parameter = editor->findChild<QWidget*>("performanceParameterCanvas"); QVERIFY(notes); QVERIFY(ruler); QVERIFY(parameter);
            PerformanceSelection::apply(score, {note}, note); editor->selectionChanged(); editor->fit();
            for (auto action : editor->findChildren<QAction*>()) if (action->text() == QString::fromUtf8("定位选音")) action->trigger();
            QTest::qWait(50);
            score->setPlayPos(Fraction::fromTicks(1440));
            const auto state = score->masterScore()->state(); const bool dirty = score->dirty();
            auto pitchScroll = notes->findChild<QScrollBar*>(); QVERIFY(pitchScroll);
            const double top = 127 - pitchScroll->value() / 100.0;
            auto point = [notes, top](Note* target) { return QPoint(76 + qRound((notes->width() - 90) * target->tick().ticks() / 1920.0) + 5, qRound((top - target->ppitch()) * 12 + 6)); };
            qInfo("Notes %dx%d top %.2f note %d point %d,%d", notes->width(), notes->height(), top, note->ppitch(), point(note).x(), point(note).y());
            main->grab().save("performance-editor-select.png");
            QTest::mouseClick(notes, Qt::LeftButton, Qt::NoModifier, point(note));
            QVERIFY(note->selected()); QCOMPARE(score->playPos().ticks(), 1440); QVERIFY(score->masterScore()->state() == state); QCOMPARE(score->dirty(), dirty);
            auto other = toChord(note->parent())->notes().at(1);
            QTest::mouseClick(notes, Qt::LeftButton, Qt::ControlModifier, point(other));
            QVERIFY(note->selected()); QVERIFY(other->selected()); QCOMPARE(score->playPos().ticks(), 1440);
            QTest::mouseClick(notes, Qt::LeftButton, Qt::ControlModifier, point(other)); QVERIFY(!other->selected());
            // Blank-start marquee previews until release and preserves the transport.
            QTest::mousePress(notes, Qt::LeftButton, Qt::NoModifier, QPoint(78, 2));
            QMouseEvent motion(QEvent::MouseMove, QPointF(notes->width() - 20, notes->height() - 3), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(notes, &motion); QCOMPARE(score->selection().noteList().size(), 1);
            QTest::mouseRelease(notes, Qt::LeftButton, Qt::NoModifier, motion.pos()); QVERIFY(score->selection().noteList().size() > 1); QCOMPARE(score->playPos().ticks(), 1440);
            auto split = editor->findChild<QSplitter*>(); QVERIFY(split); const int previousWidth = parameter->width(); split->setSizes({180, 300}); QTest::qWait(20); QCOMPARE(notes->width(), parameter->width()); QCOMPARE(parameter->width(), previousWidth);
            const int x = 76 + (ruler->width() - 90) / 2;
            QTest::mousePress(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(x, 14)); QCOMPARE(score->playPos().ticks(), 1440);
            QTest::mouseRelease(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(x, 14)); QVERIFY(std::abs(score->playPos().ticks() - 960) <= 4);
            QVERIFY(score->masterScore()->state() == state); QCOMPARE(score->dirty(), dirty);
            main->grab().save("performance-editor-010.png");
            { PerformanceEditor narrow; narrow.resize(430, 650); narrow.setView(main->currentScoreView()); narrow.show(); QTest::qWait(60); auto upper = narrow.findChild<QWidget*>("performanceNoteCanvas"); auto lower = narrow.findChild<QWidget*>("performanceParameterCanvas"); QVERIFY(upper->width() == lower->width());
                  const QImage parameterBefore = lower->grab().toImage(); auto scroll = upper->findChild<QScrollBar*>(); const int pitchSpan = scroll->pageStep();
                  QWheelEvent pitchZoom(QPointF(200, 50), QPointF(200, 50), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier | Qt::ShiftModifier, Qt::NoScrollPhase, false); QApplication::sendEvent(upper, &pitchZoom);
                  QVERIFY(scroll->pageStep() < pitchSpan); QCOMPARE(lower->grab().toImage(), parameterBefore);
                  const QImage notesBefore = upper->grab().toImage();
                  QWheelEvent valueZoom(QPointF(200, 50), QPointF(200, 50), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier | Qt::ShiftModifier, Qt::NoScrollPhase, false); QApplication::sendEvent(lower, &valueZoom);
                  QCOMPARE(upper->grab().toImage(), notesBefore); QVERIFY(lower->grab().toImage() != parameterBefore); narrow.grab().save("performance-editor-narrow.png"); }
            }
      void atomicSelectionAndIntervalCoverage()
            {
            std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
            auto first = noteAt(score.get(), 0, 4); auto second = noteAt(score.get(), 480);
            score->setPlayPos(Fraction::fromTicks(1440)); const auto state = score->state();
            QVERIFY(score->selectNoteList({first, second, first}, second)); QCOMPARE(score->selection().noteList().size(), 2); QCOMPARE(score->selection().element(), static_cast<Element*>(nullptr));
            QCOMPARE(score->inputState().track(), second->track()); QCOMPARE(score->playPos().ticks(), 1440); QVERIFY(score->state() == state);
            std::unique_ptr<MasterScore> foreign(fixture()); QVERIFY(!score->selectNoteList({noteAt(foreign.get())})); QCOMPARE(score->selection().noteList().size(), 2);
            QVERIFY(score->selectNoteList({})); QVERIFY(score->selection().isNone()); QCOMPARE(score->playPos().ticks(), 1440);
            PerformanceIntervalIndex index; index.add(0, 0, 1920, 60); index.add(1, 480, 960, 60); index.add(2, 960, 1440, 64);
            auto visible = index.query(1000, 1200); QVERIFY(visible.contains(0)); QVERIFY(visible.contains(2)); QVERIFY(!visible.contains(1)); QCOMPARE(index.query(1000, 1200, 60, 60), QVector<int>{0});
            PerformanceViewport view; const auto midi = view.ranges[0]; view.zoomRange(1, 2, 0); QCOMPARE(view.ranges[0].minimum, midi.minimum); QCOMPARE(view.ranges[0].maximum, midi.maximum); QVERIFY(view.ranges[1].maximum - view.ranges[1].minimum < 200);
            }
      void voiceFilteringAndDisplayPersistence()
            {
            auto main = Ms::mscore; QVERIFY(main); auto score = main->currentScore(); auto editor = main->findChild<PerformanceEditor*>(); QVERIFY(editor);
            auto original = noteAt(score); auto copy = original->chord()->clone(); copy->setTrack(1); copy->setParent(original->chord()->segment());
            score->startCmd(); score->undoAddElement(copy); score->endCmd(); QTest::qWait(80);
            auto excluded = copy->notes().front(); const int excludedRaw = excluded->veloOffset();
            QVERIFY(PerformanceSelection::apply(score, {original, excluded}, original)); editor->selectionChanged();
            QAction* voice2 = nullptr; for (auto action : editor->findChildren<QAction*>()) if (action->text() == QString::fromUtf8("声部 2")) voice2 = action;
            QVERIFY(voice2); voice2->setChecked(false); QCOMPARE(score->selection().noteList().size(), size_t(2));
            editor->setSelectedValue(61); QTest::qWait(80); QCOMPARE(excluded->veloOffset(), excludedRaw); QCOMPARE(original->customizeVelocity(NoteVelocity::referenceBase(original)), 61);
            voice2->setChecked(true);
            for (auto action : editor->findChildren<QAction*>()) if (action->text() == QString::fromUtf8("定位选音")) action->trigger();
            auto notes = editor->findChild<QWidget*>("performanceNoteCanvas"); auto pitchScroll = notes->findChild<QScrollBar*>();
            const double top = 127 - pitchScroll->value() / 100.0; const QPoint hit(76 + qRound((notes->width() - 90) / 4.0) + 4, qRound((top - original->ppitch()) * 12 + 6));
            int candidates = 0; QTimer menuTimer; menuTimer.setInterval(20);
            connect(&menuTimer, &QTimer::timeout, this, [&] {
                  auto menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()); if (!menu) return;
                  candidates = menu->actions().size(); for (auto action : menu->actions()) if (action->text().contains(QString::fromUtf8("声部 2"))) action->trigger();
                  menu->close(); menuTimer.stop();
                  });
            menuTimer.start(); QTest::mouseClick(notes, Qt::RightButton, Qt::NoModifier, hit); menuTimer.stop(); QCOMPARE(candidates, 2); QVERIFY(excluded->selected());
            // The actual modal palette editor restores defaults and accepts once.
            PerformanceAppearance custom; custom.mode = 2; custom.colors[PerformanceAppearance::Background] = QColor("#123456"); custom.gradient[1] = QColor("#234567"); custom.save();
            PerformanceAppearance restored; restored.load(); QCOMPARE(restored.mode, 2); QCOMPARE(restored.colors[PerformanceAppearance::Background], QColor("#123456"));
            const QColor native = original->color();
            QTimer::singleShot(30, [] {
                  auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog) return;
                  auto box = dialog->findChild<QDialogButtonBox*>(); if (!box) return;
                  box->button(QDialogButtonBox::RestoreDefaults)->click(); box->button(QDialogButtonBox::Ok)->click();
                  });
            QVERIFY(restored.edit(main)); PerformanceAppearance defaults; restored.load(); QCOMPARE(restored.colors[PerformanceAppearance::Background], defaults.colors[PerformanceAppearance::Background]); QCOMPARE(original->color(), native);
            PerformanceAppearance previous;
            previous.colors[0] = QColor("#333333"); previous.colors[1] = QColor("#464646"); previous.colors[2] = QColor("#3b3b3b"); previous.colors[3] = QColor("#626262");
            previous.gradient = {{QColor("#8194a6"), QColor("#94a987"), QColor("#b3ad83"), QColor("#bc958c")}}; previous.save();
            PerformanceAppearance migrated; migrated.load(); QCOMPARE(migrated.colors, defaults.colors); QCOMPARE(migrated.gradient, defaults.gradient);
            previous.colors[0] = QColor("#123456"); previous.save(); migrated.load(); QCOMPARE(migrated.colors, previous.colors); QCOMPARE(migrated.gradient, previous.gradient); defaults.save();
            }
      void personalBrandingAndToolbarEntry()
            {
            auto main = Ms::mscore; QVERIFY(main);
            auto action = main->findChild<QAction*>("performance-editor"); QVERIFY(action); QVERIFY(!action->icon().isNull());
            auto toolbar = main->findChild<QToolBar*>("alternative-operations"); QVERIFY(toolbar);
            for (int i = 0; i < 3; ++i) {
                  main->populateAlternativeOperations();
                  auto buttons = toolbar->findChildren<QToolButton*>("performance-editor-button"); QCOMPARE(buttons.size(), 1);
                  QCOMPARE(buttons.front()->defaultAction(), action);
                  QCOMPARE(toolbar->widgetForAction(toolbar->actions().back()), static_cast<QWidget*>(buttons.front()));
                  }
            auto button = toolbar->findChild<QToolButton*>("performance-editor-button"); QVERIFY(button);
            const bool wasOpen = action->isChecked(); button->click(); QCOMPARE(action->isChecked(), !wasOpen); button->click(); QCOMPARE(action->isChecked(), wasOpen);
            QFile version(QString(TESTROOT) + "/personal/VERSION"); QVERIFY(version.open(QIODevice::ReadOnly)); QCOMPARE(QString::fromUtf8(version.readAll()).trimmed(), QString(KUMO_PERSONAL_VERSION));
            AboutBoxDialog about; about.show(); QTest::qWait(25);
            auto label = about.findChild<QLabel*>("versionLabel"); QVERIFY(label); QVERIFY(label->text().contains(personalBuildLabel())); QVERIFY(label->text().contains("3.7"));
            auto credits = about.findChild<QLabel*>("copyrightLabel"); QVERIFY(credits); QVERIFY(credits->text().contains(personalReleaseUrl()));
            QVERIFY(QMetaObject::invokeMethod(&about, "copyRevisionToClipboard")); QVERIFY(QApplication::clipboard()->text().contains(personalBuildLabel()));
            about.grab().save("kumo-about-012.png"); about.hide();
            MsSplashScreen splash; splash.show(); QTest::qWait(30); splash.grab().save("kumo-splash-012.png"); splash.hide();
            }
      void wheelVelocityBurstAndScoreTargets()
            {
            auto main = Ms::mscore; auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score)); auto editor = main->findChild<PerformanceEditor*>(); QVERIFY(editor); QTest::qWait(80);
            auto note = noteAt(score); auto other = noteAt(score, 0, 4);
            QVERIFY(ParameterEdit::velocities(score, {{note, {Note::ValueType::USER_VAL, 40}}, {other, {Note::ValueType::USER_VAL, 50}}})); QTest::qWait(80);
            PerformanceSelection::apply(score, {note, other}, note); editor->selectionChanged(); editor->fit(true);
            auto axis = editor->findChild<QComboBox*>("performanceAxis"); axis->setCurrentIndex(0);
            QTest::mouseClick(editor->findChild<QToolButton*>("performancePitchReset"), Qt::LeftButton);
            auto notes = editor->findChild<QWidget*>("performanceNoteCanvas"); auto canvas = editor->findChild<QWidget*>("performanceParameterCanvas");
            auto toggle = editor->findChild<QToolButton*>("performanceWheelVelocity"); auto timer = editor->findChild<QTimer*>("performanceWheelTimer"); QVERIFY(toggle); QVERIFY(timer);
            auto pitch = notes->findChild<QScrollBar*>(); pitch->setValue((127 - note->ppitch() - 3) * 100);
            const auto hit = [&]() { const double row = notes->height() * 100.0 / pitch->pageStep(); return QPoint(80 + (notes->width() - 90) / 2, qRound((127 - pitch->value() / 100.0 - note->ppitch()) * row + row / 2)); };
            const auto wheel = [](QWidget* target, QPoint point, int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QWheelEvent event(point, target->mapToGlobal(point), QPoint(), QPoint(0, delta), Qt::NoButton, modifiers, Qt::NoScrollPhase, false); QApplication::sendEvent(target, &event); };
            toggle->setChecked(true); score->setPlayPos(Fraction::fromTicks(1440)); const auto state = score->state(); const int undo = score->undoStack()->getCurIdx();
            // Fractional wheel detents accumulate, and a multi-selection does not multiply the target.
            wheel(notes, hit(), 60); QVERIFY(!editor->hasPending()); wheel(notes, hit(), 60); wheel(notes, hit(), 120, Qt::ShiftModifier);
            QVERIFY(editor->hasPending()); QCOMPARE(note->veloOffset(), 40); QVERIFY(score->state() == state); QCOMPARE(score->selection().noteList().size(), size_t(2));
            main->grab().save("performance-wheel-preview-012.png");
            QTRY_COMPARE(note->veloOffset(), 49); QCOMPARE(other->veloOffset(), 50); QCOMPARE(score->undoStack()->getCurIdx(), undo + 1); QCOMPARE(score->playPos().ticks(), 1440); QVERIFY(!timer->isActive());
            score->undoRedo(true, &ed); QTest::qWait(80); QCOMPARE(note->veloOffset(), 40); score->undoRedo(false, &ed); QTest::qWait(80); QCOMPARE(note->veloOffset(), 49);
            wheel(notes, hit(), 120); QVERIFY(editor->hasPending()); QTest::keyClick(notes, Qt::Key_Escape); QVERIFY(!editor->hasPending()); QVERIFY(!timer->isActive()); QCOMPARE(note->veloOffset(), 49);
            wheel(notes, hit(), 120); QVERIFY(editor->hasPending()); QTest::mouseClick(editor->findChild<QPushButton*>("performanceCancelPreview"), Qt::LeftButton); QVERIFY(!editor->hasPending()); QCOMPARE(note->veloOffset(), 49);
            // Ctrl remains viewport-only while the edit switch is on.
            const int pitchPage = pitch->pageStep(); wheel(notes, hit(), 120, Qt::ControlModifier | Qt::ShiftModifier); QVERIFY(pitch->pageStep() != pitchPage); QVERIFY(!editor->hasPending()); QCOMPARE(note->veloOffset(), 49);
            toggle->setChecked(false); wheel(notes, hit(), 120, Qt::AltModifier); QTRY_COMPARE(note->veloOffset(), 50);
            // Endpoint moving below a stationary cursor remains editable for the entire burst.
            QTest::mouseClick(editor->findChild<QToolButton*>("performanceRangeReset"), Qt::LeftButton); QTest::qWait(20);
            const int x = 76 + (canvas->width() - 90) / 2; const QPoint endpoint(x, qRound(18 + (127.0 - 50) / 126 * (canvas->height() - 36)));
            toggle->setChecked(true); wheel(canvas, endpoint, -1200); wheel(canvas, endpoint, -1200); QTRY_COMPARE(note->veloOffset(), 30); QCOMPARE(other->veloOffset(), 50);
            auto view = main->currentScoreView();
            for (auto check : editor->findChildren<QCheckBox*>()) check->setChecked(true);
            PerformanceSelection::apply(score, {note}, note); editor->selectionChanged(); view->repaint();
            const QPoint handle = (view->matrix().mapRect(note->canvasBoundingRect()).topRight() + QPointF(12, -12)).toPoint();
            const auto offset = view->matrix(); wheel(view, handle, 120, Qt::ShiftModifier); QTRY_COMPARE(note->veloOffset(), 38); QCOMPARE(view->matrix(), offset); QTest::qWait(40);
            const QPoint head = view->matrix().mapRect(note->canvasBoundingRect()).center().toPoint();
            PerformanceSelection::apply(score, {}, nullptr); editor->selectionChanged();
            wheel(view, head, 120); QTRY_COMPARE(note->veloOffset(), 39); QVERIFY(score->selection().isNone()); QCOMPARE(score->playPos().ticks(), 1440);
            PerformanceSelection::apply(score, {note}, note); editor->selectionChanged();
            axis->setCurrentIndex(1); editor->convertSelected(Note::ValueType::OFFSET_VAL); QTest::qWait(80); const int raw = note->veloOffset();
            wheel(view, head, 120); QTRY_COMPARE(note->veloOffset(), raw + 1); QCOMPARE(note->veloType(), Note::ValueType::OFFSET_VAL);
            wheel(view, head, 120); QVERIFY(timer->isActive()); editor->flushPending(); QVERIFY(!timer->isActive()); QVERIFY(!editor->hasPending()); QCOMPARE(note->veloOffset(), raw + 2);
            toggle->setChecked(false); for (auto check : editor->findChildren<QCheckBox*>()) check->setChecked(false); axis->setCurrentIndex(0);
            main->grab().save("performance-wheel-012.png");
            }
      void endpointPickingZoomAndDirectLocation()
            {
            auto main = Ms::mscore; auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score)); auto editor = main->findChild<PerformanceEditor*>(); QVERIFY(editor); QTest::qWait(80);
            auto note = noteAt(score); const auto chord = note->chord()->notes(); QVERIFY(chord.size() >= 2);
            QMap<Note*, VelocityEdit> edits; for (int i = 0; i < chord.size(); ++i) edits[chord[i]] = {Note::ValueType::USER_VAL, 20 + i * 30};
            QVERIFY(ParameterEdit::velocities(score, edits)); QTest::qWait(80); editor->fit();
            auto canvas = editor->findChild<QWidget*>("performanceParameterCanvas"); auto notes = editor->findChild<QWidget*>("performanceNoteCanvas");
            auto tool = editor->findChild<QComboBox*>("performanceTool"); tool->setCurrentIndex(0);
            auto axis = editor->findChild<QComboBox*>("performanceAxis"); axis->setCurrentIndex(0); QTest::qWait(50);
            auto endpoint = [canvas](int value) { return QPoint(76 + qRound((canvas->width() - 90) / 4.0), qRound(canvas->height() - 18 - (value - 1) * (canvas->height() - 36) / 126.0)); };
            score->setPlayPos(Fraction::fromTicks(1440)); const auto state = score->state();
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, endpoint(20)); QVERIFY(chord[0]->selected());
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, endpoint(50)); QVERIFY(chord[1]->selected()); QVERIFY(!chord[0]->selected());
            QCOMPARE(score->playPos().ticks(), 1440); QVERIFY(score->state() == state);
            QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, endpoint(50)); QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, endpoint(50) - QPoint(0, 12)); QTest::qWait(80);
            QCOMPARE(chord[0]->veloOffset(), 20); QVERIFY(chord[1]->veloOffset() > 50);
            edits.clear(); for (auto target : chord) edits[target] = {Note::ValueType::USER_VAL, 20}; QVERIFY(ParameterEdit::velocities(score, edits)); QTest::qWait(80);
            PerformanceSelection::apply(score, {chord[0]}, chord[0]); editor->selectionChanged();
            const auto cycleState = score->state(); QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, endpoint(20)); QVERIFY(chord[1]->selected()); QVERIFY(score->state() == cycleState);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::AltModifier, endpoint(20)); QCOMPARE(score->selection().noteList().size(), 1); QVERIFY(score->state() == cycleState);
            PerformanceSelection::apply(score, {chord[0]}, chord[0]); editor->selectionChanged(); QTest::mouseClick(editor->findChild<QToolButton*>("performancePitchReset"), Qt::LeftButton);
            const auto pitchBar = notes->findChild<QScrollBar*>(); const double row = notes->height() * 100.0 / pitchBar->pageStep(), top = 127 - pitchBar->value() / 100.0;
            const QPoint noteHit(endpoint(20).x() + 5, qRound((top - chord[0]->ppitch()) * row + row / 2));
            QTest::mousePress(notes, Qt::LeftButton, Qt::AltModifier, noteHit);
            QMouseEvent velocityMove(QEvent::MouseMove, QPointF(noteHit - QPoint(0, 12)), Qt::NoButton, Qt::LeftButton, Qt::AltModifier); QApplication::sendEvent(notes, &velocityMove);
            QTest::mouseRelease(notes, Qt::LeftButton, Qt::AltModifier, velocityMove.pos()); QTest::qWait(80); QVERIFY(chord[0]->veloOffset() > 20); for (int i = 1; i < chord.size(); ++i) QCOMPARE(chord[i]->veloOffset(), 20);
            QVERIFY(ParameterEdit::velocities(score, edits)); QTest::qWait(80); const auto navigationState = score->state();
            auto rangeFit = editor->findChild<QToolButton*>("performanceRangeFit"); auto rangeScroll = editor->findChild<QScrollBar*>("performanceValueScroll"); QVERIFY(rangeFit); QVERIFY(rangeScroll);
            const QImage before = canvas->grab().toImage(); const int pitchPage = notes->findChild<QScrollBar*>()->pageStep();
            QTest::mouseClick(rangeFit, Qt::LeftButton); QVERIFY(before != canvas->grab().toImage()); QVERIFY(rangeScroll->maximum() > 0); QCOMPARE(notes->findChild<QScrollBar*>()->pageStep(), pitchPage);
            rangeScroll->setValue(5000); for (auto target : chord) QCOMPARE(target->veloOffset(), 20);
            const int axisPosition = rangeScroll->value();
            QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(25, 40));
            QMouseEvent pan(QEvent::MouseMove, QPointF(25, 55), Qt::NoButton, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(canvas, &pan);
            QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, pan.pos()); QVERIFY(rangeScroll->value() != axisPosition); for (auto target : chord) QCOMPARE(target->veloOffset(), 20);
            const int rangePosition = rangeScroll->value(); QTest::mouseClick(editor->findChild<QToolButton*>("performancePitchIn"), Qt::LeftButton); QVERIFY(notes->findChild<QScrollBar*>()->pageStep() < pitchPage); QCOMPARE(rangeScroll->value(), rangePosition);
            QTest::mouseClick(editor->findChild<QToolButton*>("performanceLocateSelection"), Qt::LeftButton); QCOMPARE(score->playPos().ticks(), 480);
            notes->findChild<QScrollBar*>()->setValue(0); // Guarantee an empty high-pitch row after the independent zoom tests.
            const QPoint blank(76 + (notes->width() - 90) / 2, 2); QTest::mouseClick(notes, Qt::LeftButton, Qt::NoModifier, blank); QVERIFY(std::abs(score->playPos().ticks() - 960) <= 4); QVERIFY(score->state() == navigationState);
            main->grab().save("performance-editor-011.png");
            }
      void reaperVelocityBaselineAndCompactStrip()
            {
            auto main = Ms::mscore; auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score)); auto editor = main->findChild<PerformanceEditor*>(); QTest::qWait(80);
            auto note = noteAt(score); QVERIFY(ParameterEdit::velocities(score, {{note, {Note::ValueType::USER_VAL, 20}}})); QTest::qWait(80);
            PerformanceSelection::apply(score, {note}, note); editor->selectionChanged(); editor->fit();
            auto canvas = editor->findChild<QWidget*>("performanceParameterCanvas"); editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
            QTest::mouseClick(editor->findChild<QToolButton*>("performanceRangeFit"), Qt::LeftButton);
            const auto state = score->state(); PerformanceAppearance appearance;
            const QImage image = canvas->grab().toImage(); const qreal dpr = image.devicePixelRatio();
            // No time-grid, velocity stem or playhead beneath the legal MIDI baseline.
            for (int x = 78; x < canvas->width() - 16; x += 13) QCOMPARE(image.pixelColor(qRound(x * dpr), qRound((canvas->height() - 8) * dpr)), appearance.colors[PerformanceAppearance::Background]);
            PerformanceViewport viewport; viewport.zoomRange(0, 4, 20); QCOMPARE(viewport.ranges[0].minimum, 1.0); QVERIFY(viewport.ranges[0].maximum < 40);
            QVERIFY(appearance.noteColor(60, 0).saturation() > 200); QVERIFY(appearance.noteColor(105, 0).green() > 200); QVERIFY(appearance.noteColor(105, 0).red() > 180);
            auto view = main->currentScoreView(); for (auto toggle : editor->findChildren<QCheckBox*>()) toggle->setChecked(true); view->repaint();
            auto system = note->chord()->measure()->system(); const int left = qMax(12, qRound(view->matrix().map(system->firstMeasure()->canvasPos()).x()));
            const int right = qMin(view->width() - 12, qRound(view->matrix().map(system->lastMeasure()->canvasPos() + QPointF(system->lastMeasure()->width(), 0)).x()));
            const QPoint blank(qMin(right - 8, left + 16), view->height() - 55);
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, blank); QVERIFY(note->selected()); QVERIFY(score->state() == state);
            view->repaint(); const QImage strip = view->grab().toImage();
            view->grab().save("performance-strip-013.png");
            QCOMPARE(strip.pixelColor(qRound((left + 8) * strip.devicePixelRatio()), qRound((view->height() - 24) * strip.devicePixelRatio())), QColor("#f1f2ef"));
            // Losing native selection keeps the current strip and content visible.
            PerformanceSelection::apply(score, {}, nullptr); editor->selectionChanged(); view->repaint();
            auto toggle = editor->findChild<QToolButton*>("performanceWheelVelocity"); toggle->setChecked(true);
            const QPoint head = view->matrix().mapRect(note->canvasBoundingRect()).center().toPoint();
            QMouseEvent hover(QEvent::MouseMove, QPointF(head), Qt::NoButton, Qt::NoButton, Qt::NoModifier); QApplication::sendEvent(view, &hover);
            QHelpEvent help(QEvent::ToolTip, head, view->mapToGlobal(head)); QApplication::sendEvent(view, &help); QVERIFY(QToolTip::text().contains(QString("MIDI %1").arg(note->ppitch()))); QToolTip::hideText();
            const QPoint handle = (view->matrix().mapRect(note->canvasBoundingRect()).topRight() + QPointF(12, -12)).toPoint();
            for (const QPoint point : {(head + handle) / 2, handle}) {
                  QMouseEvent move(QEvent::MouseMove, QPointF(point), Qt::NoButton, Qt::NoButton, Qt::NoModifier); QApplication::sendEvent(view, &move);
                  QHelpEvent tip(QEvent::ToolTip, point, view->mapToGlobal(point)); QApplication::sendEvent(view, &tip); QVERIFY(QToolTip::text().contains(QString("MIDI %1").arg(note->ppitch()))); QToolTip::hideText();
                  }
            QWheelEvent alt(head, view->mapToGlobal(head), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase, false); QApplication::sendEvent(view, &alt); QVERIFY(!editor->hasPending()); QCOMPARE(note->veloOffset(), 20);
            toggle->setChecked(false); main->grab().save("performance-reaper-013.png"); for (auto check : editor->findChildren<QCheckBox*>()) check->setChecked(false);
            }
      void overlappingNotesCycleEveryVoice()
            {
            auto main = Ms::mscore; auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score)); auto editor = main->findChild<PerformanceEditor*>(); auto original = noteAt(score);
            QList<Note*> targets {original}; score->startCmd();
            for (int voice = 1; voice <= 2; ++voice) { auto chord = original->chord()->clone(); chord->setTrack(voice); chord->setParent(original->chord()->segment()); score->undoAddElement(chord); targets.append(chord->notes().front()); }
            score->endCmd(); QTest::qWait(80); PerformanceSelection::apply(score, {original}, original); editor->selectionChanged(); editor->fit(true);
            QTest::mouseClick(editor->findChild<QToolButton*>("performancePitchReset"), Qt::LeftButton);
            auto notes = editor->findChild<QWidget*>("performanceNoteCanvas"); auto scroll = notes->findChild<QScrollBar*>(); const double row = notes->height() * 100.0 / scroll->pageStep(), top = 127 - scroll->value() / 100.0;
            const QPoint hit(80, qRound((top - original->ppitch()) * row + row / 2)); const auto state = score->state(); score->setPlayPos(Fraction::fromTicks(1440));
            for (int i : {1, 2, 0}) { QTest::mouseClick(notes, Qt::LeftButton, Qt::NoModifier, hit); QCOMPARE(score->selection().noteList().size(), size_t(1)); QVERIFY(targets[i]->selected()); QCOMPARE(score->playPos().ticks(), 1440); QVERIFY(score->state() == state); }
            main->grab().save("performance-editor-011-overlap.png");
            }
      void overlayBidirectionalHoverAndScrollDamage()
            {
            auto main = Ms::mscore; auto score = main->currentScore(); auto view = main->currentScoreView(); auto editor = main->findChild<PerformanceEditor*>(); QVERIFY(editor);
            for (auto toggle : editor->findChildren<QCheckBox*>()) toggle->setChecked(true);
            QVERIFY(view->hasMouseTracking());
            auto note = noteAt(score); PerformanceSelection::apply(score, {note}, note); editor->selectionChanged(); editor->fit(true);
            QTest::mouseClick(editor->findChild<QToolButton*>("performancePitchReset"), Qt::LeftButton); QTest::qWait(60);
            auto notes = editor->findChild<QWidget*>("performanceNoteCanvas"); auto pitch = notes->findChild<QScrollBar*>();
            const double top = 127 - pitch->value() / 100.0, row = notes->height() * 100.0 / pitch->pageStep(); const QPoint hit(80, qRound((top - note->ppitch()) * row + row / 2));
            QMouseEvent hover(QEvent::MouseMove, QPointF(hit), Qt::NoButton, Qt::NoButton, Qt::NoModifier); QApplication::sendEvent(notes, &hover); QTest::qWait(30);
            const QPoint scoreHit = view->matrix().mapRect(note->canvasBoundingRect()).center().toPoint();
            QHelpEvent help(QEvent::ToolTip, scoreHit, view->mapToGlobal(scoreHit)); QApplication::sendEvent(view, &help); QVERIFY(QToolTip::text().contains(QString("MIDI %1").arg(note->ppitch()))); QToolTip::hideText();
            QMouseEvent back(QEvent::MouseMove, QPointF(scoreHit), Qt::NoButton, Qt::NoButton, Qt::NoModifier); QApplication::sendEvent(view, &back);
            QHelpEvent backHelp(QEvent::ToolTip, hit, notes->mapToGlobal(hit)); QApplication::sendEvent(notes, &backHelp); QVERIFY(QToolTip::text().contains(QString("MIDI %1").arg(note->ppitch()))); QToolTip::hideText();
            view->repaint(); PerformancePaintDamage recorder; view->installEventFilter(&recorder);
            const QPoint oldOffset(qRound(view->matrix().dx()), qRound(view->matrix().dy()));
            QWheelEvent wheel(QPointF(150, 150), view->mapToGlobal(QPoint(150, 150)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false); QApplication::sendEvent(view, &wheel); QApplication::processEvents();
            const QPoint delta(qRound(view->matrix().dx()) - oldOffset.x(), qRound(view->matrix().dy()) - oldOffset.y()); QVERIFY(!delta.isNull());
            // Both the old fixed strip and its scrolled copy must receive score repaints.
            const QPoint band(qRound(view->matrix().map(note->chord()->measure()->system()->firstMeasure()->canvasPos()).x()) + 8, view->height() - 90); QVERIFY(recorder.region.contains(band));
            if (view->rect().contains(band + delta)) QVERIFY(recorder.region.contains(band + delta));
            view->removeEventFilter(&recorder); main->grab().save("performance-overlay-011.png");
            }
      void legacyRollSelectionAndPitchTooltip()
            {
            auto main = Ms::mscore; QVERIFY(main); auto score = fixture(); QVERIFY(score);
            auto note = noteAt(score); note->part()->instrument()->setTranspose(Interval(-1, -2));
            score->style().set(Sid::concertPitch, false); note->setTpcFromPitch();
            auto ottava = new Ottava(score); ottava->setTrack(0); ottava->setTick(Fraction::fromTicks(480)); ottava->setTick2(Fraction::fromTicks(960));
            score->startCmd(); score->undoAddElement(ottava); score->endCmd(); note->staff()->updateOttava();
            QCOMPARE(note->ppitch(), note->pitch() + 12); QCOMPARE(note->epitch(), note->pitch() + 2);
            main->setCurrentScoreView(main->appendScore(score));
            auto editor = main->findChild<PerformanceEditor*>(); QVERIFY(editor); QTest::qWait(80);
            auto other = noteAt(score, 0, 4); QVERIFY(PerformanceSelection::apply(score, {note, other}, note)); editor->selectionChanged();
            Pos locators[3]; for (auto& locator : locators) locator.setContext(score->tempomap(), score->sigmap());
            PianoView roll; roll.setScope(PianoRollScope::PART); roll.setStaff(score->staff(0), locators); roll.resize(700, 360); roll.show(); roll.updateNotes(); QTest::qWait(30);
            auto items = roll.getSelectedItems(); QCOMPARE(items.size(), 2); QSet<Note*> selected; for (auto item : items) selected.insert(item->note()); QVERIFY(selected.contains(note)); QVERIFY(selected.contains(other));
            connect(&roll, &PianoView::selectionChanged, editor, [main, score] { main->selectionChanged(score->selection().state()); });
            QTest::mouseClick(roll.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(roll.viewport()->width() - 20, 8));
            QVERIFY(score->selection().noteList().empty());
            QVERIFY(PerformanceSelection::apply(score, {note}, note)); editor->selectionChanged(); QCOMPARE(roll.getSelectedItems().size(), 1);
            roll.setStaff(nullptr, nullptr); roll.hide();
            editor->fit(true); for (auto action : editor->findChildren<QAction*>()) if (action->text() == QString::fromUtf8("定位选音")) action->trigger();
            auto notes = editor->findChild<QWidget*>("performanceNoteCanvas"); auto scroll = notes->findChild<QScrollBar*>();
            const double top = 127 - scroll->value() / 100.0; const QPoint hit(80, qRound((top - note->ppitch()) * 12 + 6));
            QHelpEvent help(QEvent::ToolTip, hit, notes->mapToGlobal(hit)); QApplication::sendEvent(notes, &help);
            QVERIFY(QToolTip::text().contains(QString("MIDI %1").arg(note->ppitch()))); QVERIFY(QToolTip::text().contains(QString::fromUtf8("记谱："))); QVERIFY(QToolTip::text().contains(QString::fromUtf8("声部 1"))); QToolTip::hideText();
            }
      void realSequencerPlaybackAndLifecycle()
            {
            auto main = Ms::mscore; QVERIFY(main); QVERIFY(!Ms::seq);
            // This slot creates its own transport-bound editor. Keep the host's
            // unrelated editor from drawing a second overlay onto the same view.
            for (auto check : main->findChild<PerformanceEditor*>()->findChildren<QCheckBox*>()) check->setChecked(false);
            getAction("toggle-piano")->setChecked(false); main->cmd(getAction("toggle-piano")); QVERIFY(main->pianoTools());
            const QByteArray xml("<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part><Staff id=\"1\"><Measure><voice><Clef><concertClefType>G</concertClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>whole</durationType><Note><pitch>60</pitch><tpc>14</tpc></Note></Chord></voice></Measure><Measure><voice><Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice><endRepeat>2</endRepeat></Measure><Measure><voice><TimeSig><sigN>3</sigN><sigD>8</sigD></TimeSig><Chord><durationType>quarter</durationType><dots>1</dots><Note><pitch>64</pitch><tpc>18</tpc></Note></Chord></voice></Measure></Staff></Score></museScore>");
            const QString path = _directory.path() + "/transport.mscx"; QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(xml); file.close();
            auto score = main->readScore(path); QVERIFY(score); main->setCurrentScoreView(main->appendScore(score)); score->firstMeasure()->setRepeatStart(true); score->setExpandRepeats(true);
            auto lineBreak = new LayoutBreak(score); lineBreak->setLayoutBreakType(LayoutBreak::LINE); lineBreak->setParent(score->firstMeasure()); score->firstMeasure()->add(lineBreak); score->doLayout();
            QVERIFY(score->firstMeasure()->system() != score->lastMeasure()->system());
            MasterSynthesizer synth; MScore::sampleRate = 44100; synth.registerSynthesizer(new PerformanceSilentSynth); synth.setSampleRate(44100); synth.init();
            Ms::seq = new PerformanceAuditionSeq; auto sequence = static_cast<PerformanceAuditionSeq*>(Ms::seq);
            std::unique_ptr<Seq, std::function<void(Seq*)>> sequenceGuard(sequence, [](Seq* s) { s->stopWait(); s->waitForStoppedRendering(); s->setScoreView(nullptr); delete s; Ms::seq = nullptr; }); auto driver = new PerformanceTestDriver(sequence); sequence->setDriver(driver); sequence->setMasterSynthesizer(&synth); QVERIFY(sequence->init()); sequence->setScoreView(main->currentScoreView());
            connect(sequence, &Seq::started, score, [score] { score->setIsPlaying(true); }); connect(sequence, &Seq::stopped, score, [score] { score->setIsPlaying(false); });
            {
                  PerformanceEditor editor; editor.resize(1000, 660); editor.setView(main->currentScoreView()); editor.show(); QTest::qWait(80); editor.fit();
                  auto ruler = editor.findChild<QWidget*>("performanceRuler"); QVERIFY(ruler);
                  auto playbackTimer = editor.findChild<QTimer*>("performancePlaybackTimer"); QVERIFY(playbackTimer);
                  auto transport = editor.findChild<QToolButton*>("performancePlay"); auto audition = editor.findChild<QToolButton*>("performanceAudition"); QVERIFY(transport); QVERIFY(audition);
                  auto previewNotes = editor.findChild<QWidget*>("performanceNoteCanvas"); auto pitchScroll = previewNotes->findChild<QScrollBar*>();
                  QTest::mouseClick(editor.findChild<QToolButton*>("performancePitchReset"), Qt::LeftButton); editor.fit(); QTest::qWait(30);
                  auto previewNote = noteAt(score, 0); const QPoint previewHit(80, qRound((127 - pitchScroll->value() / 100.0 - previewNote->ppitch()) * 12 + 6));
                  score->setPlayPos(Fraction::fromTicks(1920)); const auto auditionState = score->state();
                  audition->setChecked(true); QTest::mouseClick(previewNotes, Qt::LeftButton, Qt::NoModifier, previewHit); QCOMPARE(sequence->previews, 1); QCOMPARE(sequence->previewPitch, previewNote->ppitch()); QCOMPARE(score->playPos().ticks(), 1920); QVERIFY(score->state() == auditionState);
                  audition->setChecked(false); QTest::mouseClick(previewNotes, Qt::LeftButton, Qt::NoModifier, previewHit); QCOMPARE(sequence->previews, 1);
                  QTest::mouseClick(transport, Qt::LeftButton); QApplication::processEvents(); QVERIFY(sequence->isPlaying());
                  QTest::mouseClick(transport, Qt::LeftButton); sequence->waitForStoppedRendering(); QApplication::processEvents(); QVERIFY(!sequence->isPlaying());
                  QTest::mouseDClick(previewNotes, Qt::LeftButton, Qt::NoModifier, previewHit); QApplication::processEvents(); QVERIFY(sequence->isPlaying()); QVERIFY(sequence->getCurTick() < 480);
                  QTest::keyClick(previewNotes, Qt::Key_Space); sequence->waitForStoppedRendering(); QApplication::processEvents(); QVERIFY(!sequence->isPlaying());
                  QSignalSpy beats(sequence, &Seq::heartBeat); QSignalSpy starts(sequence, &Seq::started);
                  score->setPlayPos(Fraction::fromTicks(0)); sequence->start(); QApplication::processEvents(); QVERIFY(sequence->isPlaying()); QVERIFY(starts.count() > 0); QTRY_VERIFY(playbackTimer->isActive());
                  auto lineX = [ruler]() {
                        const QImage image = ruler->grab().toImage(); int result = -1;
                        for (int x = 76; x < image.width() - 14; ++x) { const QColor color = image.pixelColor(x, 4); if (color.green() > 185 && color.red() > 80 && color.red() < 170 && color.blue() > 120) result = x; }
                        return result;
                        };
                  int previous = lineX();
                  for (int i = 0; i < 5; ++i) { driver->pulse(); QTest::qWait(22); QTRY_VERIFY(lineX() > previous); previous = lineX(); }
                  for (auto check : editor.findChildren<QCheckBox*>()) check->setChecked(true);
                  auto scoreView = main->currentScoreView(); score->doLayout(); QTest::qWait(50); scoreView->repaint();
                  const QImage liveStrip = scoreView->grab().toImage(); auto liveSystem = score->firstMeasure()->system();
                  const int liveLeft = qMax(12, qRound(scoreView->matrix().map(liveSystem->firstMeasure()->canvasPos()).x()));
                  scoreView->grab().save("performance-live-013.png");
                  QCOMPARE(liveStrip.pixelColor(qRound((liveLeft + 8) * liveStrip.devicePixelRatio()), qRound((scoreView->height() - 24) * liveStrip.devicePixelRatio())), QColor("#f1f2ef"));
                  QVERIFY(beats.count() > 0); QCOMPARE(beats.last().at(0).toInt(), 0); // Long note: heartbeat event tick has not moved.
                  auto note = noteAt(score, 0); const int tick = sequence->getCurTick(); QVERIFY(PerformanceSelection::apply(score, {note}, note)); QCOMPARE(sequence->getCurTick(), tick);
                  PerformanceEditor late; late.resize(700, 600); late.setView(main->currentScoreView()); late.show(); QTest::qWait(60);
                  auto lateAxis = late.findChild<QComboBox*>("performanceAxis"); QVERIFY(lateAxis); lateAxis->setCurrentIndex(1); late.setSelectedValue(25); QVERIFY(late.hasPending()); QCOMPARE(note->veloOffset(), 0);
                  late.fit(true); QTest::mouseClick(late.findChild<QToolButton*>("performancePitchReset"), Qt::LeftButton);
                  auto lateNotes = late.findChild<QWidget*>("performanceNoteCanvas"); auto latePitch = lateNotes->findChild<QScrollBar*>();
                  const double lateRow = lateNotes->height() * 100.0 / latePitch->pageStep();
                  const QPoint lateHit(80, qRound((127 - latePitch->value() / 100.0 - note->ppitch()) * lateRow + lateRow / 2));
                  QWheelEvent stagedWheel(lateHit, lateNotes->mapToGlobal(lateHit), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase, false);
                  QApplication::sendEvent(lateNotes, &stagedWheel); QTest::qWait(320); QVERIFY(late.hasPending()); QCOMPARE(note->veloOffset(), 0);
                  sequence->seek(score->repeatList().tick2utick(1920)); driver->pulse(); QTest::qWait(25); const int restStart = lineX(); driver->pulse(); QTRY_VERIFY(lineX() > restStart);
                  const QImage restStrip = scoreView->grab().toImage(); bool stripLine = false;
                  for (int x = 12; x < scoreView->width() - 12; ++x) if (restStrip.pixelColor(qRound(x * restStrip.devicePixelRatio()), qRound((scoreView->height() - 40) * restStrip.devicePixelRatio())) == QColor("#4b7658")) { stripLine = true; break; }
                  QVERIFY(stripLine); // Playback crossed a system while the selected first note stayed fixed.
                  const auto restSystem = score->firstMeasure()->nextMeasure()->system();
                  const int restRight = qRound(scoreView->matrix().map(restSystem->lastMeasure()->canvasPos() + QPointF(restSystem->lastMeasure()->width(), 0)).x());
                  if (restRight + 10 < scoreView->width()) QVERIFY(restStrip.pixelColor(qRound((restRight + 10) * restStrip.devicePixelRatio()), qRound((scoreView->height() - 24) * restStrip.devicePixelRatio())) != QColor("#f1f2ef"));
                  scoreView->grab().save("performance-rest-system-013.png");
                  // Second repeated occurrence maps back to the written first measure.
                  sequence->seek(3840); driver->pulse(); QTest::qWait(25); QVERIFY(score->repeatList().utick2tick(sequence->getCurTick()) < 480);
                  getAction("loop")->setChecked(true); score->setLoopInTick(Fraction::fromTicks(0)); score->setLoopOutTick(Fraction::fromTicks(1920));
                  sequence->seek(1840);
                  for (int i = 0; i < 8; ++i) { driver->pulse(); QTest::qWait(10); if (i > 1 && score->repeatList().utick2tick(sequence->getCurTick()) < 480) break; }
                  QVERIFY(score->repeatList().utick2tick(sequence->getCurTick()) < 480); getAction("loop")->setChecked(false);
                  editor.hide(); driver->pulse(); QTest::qWait(30); QVERIFY(!editor.isVisible()); QVERIFY(!playbackTimer->isActive());
                  editor.show(); QTest::qWait(25); QVERIFY(editor.isVisible()); QTRY_VERIFY(playbackTimer->isActive());
                  sequence->stopWait(); sequence->waitForStoppedRendering(); QApplication::processEvents(); QVERIFY(!sequence->isPlaying()); QVERIFY(!playbackTimer->isActive()); QTRY_VERIFY(!late.hasPending()); QCOMPARE(note->veloOffset(), 26);
                  editor.setView(nullptr); score->doLayout(); editor.setView(main->currentScoreView()); QTest::qWait(30);
                  }
            sequenceGuard.reset();
            }
      void arpeggioInspectorScrubAndDeferredCommit()
            {
            auto main = Ms::mscore; QVERIFY(main);
            auto score = main->readScore(QString(TESTROOT) + "/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));
            auto chord = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
            auto a = new Arpeggio(score); a->setParent(chord); a->setTrack(0); a->setProperty(Pid::ARP_TIMING_MODE, 2);
            score->startCmd(); score->undoAddElement(a); score->select(a, SelectType::SINGLE, 0); score->endCmd();
            getAction("inspector")->setChecked(true); main->cmd(getAction("inspector")); QTest::qWait(100);
            auto panel = main->findChild<InspectorArpeggio*>(); QVERIFY(panel);
            auto spin = panel->findChild<QDoubleSpinBox*>("arpIntervalMs"); QVERIFY(spin);
            auto label = panel->findChild<QLabel*>("arpIntervalMsLabel"); QVERIFY(label);
            const QPoint press = label->rect().center(), moved = press + QPoint(10, 0);
            QTest::mousePress(label, Qt::LeftButton, Qt::NoModifier, press);
            QMouseEvent move(QEvent::MouseMove, moved, moved, label->mapToGlobal(moved), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(label, &move);
            QCOMPARE(a->intervalMs(), 65.0); QCOMPARE(spin->value(), 75.0);
            QTest::mouseRelease(label, Qt::LeftButton, Qt::NoModifier, moved); QTest::qWait(40);
            QCOMPARE(a->intervalMs(), 75.0);
            EditData ed; score->undoRedo(true, &ed); QCOMPARE(a->intervalMs(), 65.0);
            score->undoRedo(false, &ed); QCOMPARE(a->intervalMs(), 75.0);
            score->startCmd(); score->select(a, SelectType::SINGLE, 0); score->endCmd(); QTest::qWait(50);
            panel = main->findChild<InspectorArpeggio*>(); label = panel->findChild<QLabel*>("arpIntervalMsLabel"); spin = panel->findChild<QDoubleSpinBox*>("arpIntervalMs");
            QCOMPARE(score->selection().element(), static_cast<Element*>(a));
            QTest::mousePress(label, Qt::LeftButton, Qt::NoModifier, press); QApplication::sendEvent(label, &move);
            QTest::keyClick(label, Qt::Key_Escape); QTest::mouseRelease(label, Qt::LeftButton, Qt::NoModifier, moved);
            QCOMPARE(a->intervalMs(), 75.0); QCOMPARE(spin->value(), 75.0);
            QCOMPARE(score->selection().element(), static_cast<Element*>(a));
            auto editor = main->performanceEditor(); score->setIsPlaying(true); spin->setValue(90.0);
            QCOMPARE(a->intervalMs(), 75.0); QVERIFY(editor->hasPending());
            score->setIsPlaying(false); QTRY_COMPARE(a->intervalMs(), 90.0); QVERIFY(!editor->hasPending());
            score->startCmd(); a->undoChangeProperty(Pid::ARP_TIMING_MODE, 0); a->undoChangeProperty(Pid::ARP_OFFSET_MS, 35.0); score->endCmd(); QTest::qWait(30);
            panel = main->findChild<InspectorArpeggio*>(); QVERIFY(panel);
            auto preset = panel->findChild<QPushButton*>("arpeggioApplyPreset"); QVERIFY(preset);
            QTest::mouseClick(preset, Qt::LeftButton); QTRY_COMPARE(a->timingMode(), 2);
            QCOMPARE(a->intervalMs(), 65.0); QCOMPARE(a->offsetMs(), 0.0);
            panel->grab().save("arpeggio-inspector-014.png");
            score->undoRedo(true, &ed); QCOMPARE(a->timingMode(), 0); QCOMPARE(a->intervalMs(), 90.0); QCOMPARE(a->offsetMs(), 35.0);

            }
      void arpeggioRealSequencerPreRoll()
            {
            auto main = Ms::mscore; QVERIFY(main); QVERIFY(!Ms::seq);
            auto score = main->readScore(QString(TESTROOT) + "/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));
            auto chord = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
            auto a = new Arpeggio(score); a->setParent(chord); a->setTrack(0); a->setProperty(Pid::ARP_TIMING_MODE, 2); chord->add(a);
            auto second = toChord(score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest)->element(0));
            auto b = new Arpeggio(score); b->setParent(second); b->setTrack(0); b->setProperty(Pid::ARP_TIMING_MODE, 2); second->add(b);
            MasterSynthesizer synth; MScore::sampleRate = 44100;
            auto recording = new PerformanceRecordingSynth; synth.registerSynthesizer(recording); synth.setSampleRate(44100); synth.init();
            Ms::seq = new PerformanceAuditionSeq; auto sequence = Ms::seq;
            std::unique_ptr<Seq, std::function<void(Seq*)>> guard(sequence, [](Seq* s) { s->stopWait(); s->waitForStoppedRendering(); s->setScoreView(nullptr); delete s; Ms::seq = nullptr; });
            auto driver = new PerformanceTestDriver(sequence); sequence->setDriver(driver); sequence->setMasterSynthesizer(&synth); QVERIFY(sequence->init()); sequence->setScoreView(main->currentScoreView());
            score->setPlayPos(Fraction::fromTicks(0)); sequence->start(); QApplication::processEvents(); QVERIFY(sequence->isPlaying());
            driver->pulse(); QCOMPARE(sequence->getCurTick(), 0);
            QVERIFY(recording->onFrames.size() >= 1);
            for (int i = 0; i < 4; ++i) driver->pulse();
            QVERIFY(sequence->getCurTick() > 0);
            QVERIFY(recording->onFrames.size() >= 3);
            QVERIFY(recording->onFrames[1] > recording->onFrames[0] + 2600);
            sequence->stopWait(); sequence->waitForStoppedRendering(); QApplication::processEvents();
            recording->onFrames.clear(); recording->onPitches.clear();
            score->setPlayPos(Fraction::fromTicks(1920)); sequence->start(); QApplication::processEvents();
            driver->pulse(); QCOMPARE(sequence->getCurTick(), 1920);
            for (int i = 0; i < 4; ++i) driver->pulse();
            QCOMPARE(recording->onPitches, QVector<int>({60, 64, 67}));
            getAction("loop")->setChecked(true); score->setLoopInTick(Fraction::fromTicks(1920)); score->setLoopOutTick(Fraction::fromTicks(3840));
            bool looped = false; int previous = sequence->getCurTick();
            for (int i = 0; i < 60; ++i) {
                  driver->pulse(); QTest::qWait(2); int current = sequence->getCurTick();
                  if (current < previous) { QCOMPARE(current, 1920); looped = true; break; } previous = current;
                  }
            QVERIFY(looped); getAction("loop")->setChecked(false);
            sequence->stopWait(); sequence->waitForStoppedRendering(); QApplication::processEvents();
            }
      void displaySettingsAcrossProcesses()
            {
            const QByteArray mode = qgetenv("PERFORMANCE_APPEARANCE_CHILD");
            if (!mode.isEmpty()) {
                  QCoreApplication::setOrganizationName("PerformanceRestartRegression"); QCoreApplication::setApplicationName("Appearance");
                  QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, qEnvironmentVariable("PERFORMANCE_APPEARANCE_DIRECTORY"));
                  PerformanceAppearance appearance;
                  if (mode == "write") {
                        appearance.mode = 2; appearance.colors[PerformanceAppearance::Background] = QColor("#123456"); appearance.gradient[1] = QColor("#234567"); appearance.stops[1] = 35; appearance.save();
                        QSettings settings; settings.setValue("performanceEditor/wheelVelocity", true); settings.sync(); QCOMPARE(settings.status(), QSettings::NoError);
                        }
                  else {
                        appearance.load(); QCOMPARE(appearance.mode, 2); QCOMPARE(appearance.colors[PerformanceAppearance::Background], QColor("#123456")); QCOMPARE(appearance.gradient[1], QColor("#234567")); QCOMPARE(appearance.stops[1], 35);
                        PerformanceEditor editor; QVERIFY(editor.findChild<QToolButton*>("performanceWheelVelocity")->isChecked()); editor.resize(600, 520); editor.show(); QTest::qWait(25); auto canvas = editor.findChild<QWidget*>("performanceParameterCanvas"); QVERIFY(canvas);
                        const QImage rendered = canvas->grab().toImage(); QCOMPARE(rendered.pixelColor(10, rendered.height() - 3), QColor("#123456"));
                        }
                  return;
                  }
            for (const char* child : {"write", "read"}) {
                  QProcess process; auto environment = QProcessEnvironment::systemEnvironment(); environment.insert("PERFORMANCE_APPEARANCE_CHILD", child); environment.insert("PERFORMANCE_APPEARANCE_DIRECTORY", _directory.path()); process.setProcessEnvironment(environment);
                  process.start(QCoreApplication::applicationFilePath(), {"displaySettingsAcrossProcesses", "-o", _directory.path() + QString("/appearance-%1.txt,txt").arg(child)});
                  QVERIFY(process.waitForStarted(5000)); QVERIFY(process.waitForFinished(10000));
                  const QByteArray diagnostics = process.readAllStandardOutput() + process.readAllStandardError(); QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, diagnostics.constData());
                  }
            }
      void largeScoreBenchmark()
            {
            if (!qEnvironmentVariableIsSet("PERFORMANCE_BENCHMARK")) return;
            QVERIFY(Ms::mscore); auto main = Ms::mscore; auto editor = main->findChild<PerformanceEditor*>(); QVERIFY(editor);
            auto notes = editor->findChild<QWidget*>("performanceNoteCanvas"); auto parameter = editor->findChild<QWidget*>("performanceParameterCanvas"); QVERIFY(notes); QVERIFY(parameter);
            for (int wanted : {10000, 50000}) {
                  QByteArray xml("<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division><Part>");
                  for (int staff = 1; staff <= 2; ++staff) xml += "<Staff id=\"" + QByteArray::number(staff) + "\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>";
                  xml += "<trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part>";
                  const int measures = (wanted + 111) / 112;
                  for (int staff = 1; staff <= 2; ++staff) {
                        xml += "<Staff id=\"" + QByteArray::number(staff) + "\">";
                        for (int m = 0; m < measures; ++m) {
                              xml += "<Measure>";
                              for (int voice = 0; voice < 4; ++voice) {
                                    xml += "<voice>";
                                    if (m == 0 && voice == 0) xml += "<Clef><concertClefType>G</concertClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
                                    for (int chord = 0; chord < (voice ? 4 : 1); ++chord) {
                                          xml += "<Chord><durationType>"; xml += voice ? "quarter" : "whole"; xml += "</durationType>";
                                          for (int n = 0; n < (voice ? 4 : 8); ++n) xml += "<Note><pitch>" + QByteArray::number(48 + staff * 12 + n) + "</pitch><tpc>14</tpc></Note>";
                                          xml += "</Chord>";
                                          }
                                    xml += "</voice>";
                                    }
                              xml += "</Measure>";
                              }
                        xml += "</Staff>";
                        }
                  xml += "</Score></museScore>";
                  const QString path = _directory.path() + QString("/benchmark-%1.mscx").arg(wanted);
                  QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(xml); file.close();
                  auto score = main->readScore(path); QVERIFY(score); QElapsedTimer timer; timer.start();
                  main->setCurrentScoreView(main->appendScore(score)); QTest::qWait(100); const double snapshot = timer.nsecsElapsed() / 1e6;
                  QList<Note*> all; for (auto segment = score->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest)) for (int track = 0; track < 8; ++track) { auto element = segment->element(track); if (element && element->isChord()) for (auto note : toChord(element)->notes()) all.append(note); }
                  timer.start(); QVERIFY(PerformanceSelection::apply(score, all, all.front())); const double batch = timer.nsecsElapsed() / 1e6;
                  editor->selectionChanged(); QTest::qWait(20);
                  auto voice = editor->findChild<QComboBox*>("performanceVoice"); timer.start(); voice->setCurrentIndex(1); const double filter = timer.nsecsElapsed() / 1e6; voice->setCurrentIndex(0);
                  timer.start(); editor->setSelectedValue(100); const double commit = timer.nsecsElapsed() / 1e6; QTest::qWait(100);
                  PerformanceSelection::apply(score, all.mid(0, 1000), all.front()); editor->selectionChanged();
                  for (auto check : editor->findChildren<QCheckBox*>()) check->setChecked(false);
                  auto tool = editor->findChild<QComboBox*>("performanceTool"); tool->setCurrentIndex(2);
                  auto scroll = editor->findChild<QScrollBar*>(QString(), Qt::FindDirectChildrenOnly); QVERIFY(scroll);
                  for (bool overview : {true, false}) {
                        editor->fit(!overview); QTest::qWait(40); const int stableScroll = scroll->value();
                        QVector<double> paintSamples, dragSamples, playSamples; QPixmap upper(notes->size()), lower(parameter->size());
                        QTest::mousePress(parameter, Qt::LeftButton, Qt::NoModifier, QPoint(100, 25));
                        for (int frame = 0; frame < 40; ++frame) {
                              QMouseEvent move(QEvent::MouseMove, QPointF(parameter->width() - 16, 35 + frame % 20), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                              timer.start(); QApplication::sendEvent(parameter, &move); QTest::qWait(17); dragSamples.append(timer.nsecsElapsed() / 1e6 - 17);
                              timer.start(); notes->render(&upper); parameter->render(&lower); paintSamples.append(timer.nsecsElapsed() / 1e6);
                              timer.start(); score->setPlayPos(Fraction::fromTicks(frame * 30)); QApplication::processEvents(); playSamples.append(timer.nsecsElapsed() / 1e6);
                              }
                        QTest::keyClick(parameter, Qt::Key_Escape); QTest::mouseRelease(parameter, Qt::LeftButton); QCOMPARE(scroll->value(), stableScroll); QVERIFY(!editor->hasPending());
                        for (auto samples : {&paintSamples, &dragSamples, &playSamples}) std::sort(samples->begin(), samples->end());
                        qInfo("Performance %d notes, %s: snapshot %.2f ms, filter %.2f ms, batch selection %.2f ms, native commit %.2f ms; two-area paint P95 %.2f ms, drag P95 %.2f ms, locator paint P95 %.2f ms", all.size(), overview ? "overview" : "local", snapshot, filter, batch, commit, paintSamples[37], dragSamples[37], playSamples[37]);
                        }
                  // Measure the new native score overlay path separately from MIDI canvases.
                  QCheckBox* band = nullptr; for (auto check : editor->findChildren<QCheckBox*>()) if (check->text().contains(QString::fromUtf8("谱行参数带"))) band = check;
                  QVERIFY(band); PerformanceSelection::apply(score, {all.front()}, all.front()); editor->selectionChanged(); band->setChecked(true); QTest::qWait(40);
                  auto view = main->currentScoreView(); view->repaint(); QVector<double> overlaySamples;
                  for (int frame = 0; frame < 40; ++frame) {
                        QWheelEvent wheel(QPointF(150, 150), view->mapToGlobal(QPoint(150, 150)), QPoint(), QPoint(0, frame % 2 ? 120 : -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                        timer.start(); QApplication::sendEvent(view, &wheel); QApplication::processEvents(); overlaySamples.append(timer.nsecsElapsed() / 1e6);
                        }
                  std::sort(overlaySamples.begin(), overlaySamples.end()); qInfo("Performance overlay %d notes: native wheel/overlay repaint P95 %.2f ms, max %.2f ms", all.size(), overlaySamples[37], overlaySamples.back()); band->setChecked(false);
                  editor->fit(true); QTest::mouseClick(editor->findChild<QToolButton*>("performancePitchReset"), Qt::LeftButton);
                  auto pitch = notes->findChild<QScrollBar*>(); const double row = notes->height() * 100.0 / pitch->pageStep();
                  const QPoint hit(80, qRound((127 - pitch->value() / 100.0 - all.front()->ppitch()) * row + row / 2));
                  auto wheelToggle = editor->findChild<QToolButton*>("performanceWheelVelocity"); wheelToggle->setChecked(true); QTest::qWait(20);
                  const auto wheelState = score->state(); QVector<double> velocitySamples;
                  for (int frame = 0; frame < 40; ++frame) {
                        QWheelEvent wheel(hit, notes->mapToGlobal(hit), QPoint(), QPoint(0, frame % 2 ? 120 : -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                        timer.start(); QApplication::sendEvent(notes, &wheel); QApplication::processEvents(); velocitySamples.append(timer.nsecsElapsed() / 1e6); if (!frame) QVERIFY(editor->hasPending());
                        }
                  QVERIFY(score->state() == wheelState); QTest::keyClick(notes, Qt::Key_Escape); QVERIFY(!editor->hasPending()); wheelToggle->setChecked(false);
                  std::sort(velocitySamples.begin(), velocitySamples.end()); qInfo("Performance velocity wheel %d notes: preview/two-area repaint P95 %.2f ms, max %.2f ms", all.size(), velocitySamples[37], velocitySamples.back());
                  wheelToggle->setChecked(true);
                  QWheelEvent finalWheel(hit, notes->mapToGlobal(hit), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false); QApplication::sendEvent(notes, &finalWheel); QVERIFY(editor->hasPending());
                  timer.start(); QVERIFY(QMetaObject::invokeMethod(editor->findChild<QTimer*>("performanceWheelTimer"), "timeout", Qt::DirectConnection)); QApplication::processEvents();
                  qInfo("Performance velocity wheel %d notes: single native commit/cache refresh %.2f ms", all.size(), timer.nsecsElapsed() / 1e6); QVERIFY(!editor->hasPending()); wheelToggle->setChecked(false);
                  }
            }
      };
QTEST_MAIN(TestPerformanceEditor)
#include "tst_performanceeditor.moc"
