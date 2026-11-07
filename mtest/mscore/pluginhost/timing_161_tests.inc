// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "libmscore/performancetiming.h"
#include "libmscore/rendermidi.h"
#include "mscore/performanceeditor/timingeditor.h"
namespace
{
QPair<int, int> timingPair161(MasterScore* score, const Note* owner, int index)
{
    EventMap events;
    score->renderMidi(&events, false, false, Ms::mscore->synthesizerState());
    QPair<int, int> result{INT_MIN, INT_MIN};
    for (const auto& item : events)
        if (item.second.type() == ME_NOTEON && item.second.noteEventOwner() == owner &&
            item.second.noteEventIndex() == index) {
            if (item.second.velo())
                result.first = item.first;
            else
                result.second = item.first;
        }
    return result;
}
} // namespace
void TestPluginHost::performanceTimingPersistence161()
{
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    auto chord = toChord(score->firstSegment(SegmentType::ChordRest)->element(4));
    QVERIFY(chord);
    auto note = chord->notes().front();
    const auto duration = chord->actualTicks();
    const auto before = timingPair161(score, note, 0);
    QVERIFY(before.second > before.first + 300);
    const auto events = note->playEvents();
    const auto mode = chord->playEventType();
    auto edit = PerformanceTiming::value(note, 0);
    edit.onset = 120;
    edit.release = -240;
    const int undo = score->undoStack()->getCurIdx();
    score->startCmd();
    QVERIFY(PerformanceTiming::change(score, note, {edit}));
    score->endCmd();
    QCOMPARE(score->undoStack()->getCurIdx(), undo + 1);
    auto after = timingPair161(score, note, 0);
    QCOMPARE(after.first, before.first + 120);
    QCOMPARE(after.second, before.second - 240);
    QCOMPARE(chord->actualTicks(), duration);
    QCOMPARE(chord->playEventType(), mode);
    QCOMPARE(note->playEvents(), events);
    const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    QFileInfo saved(output.filePath("performance-timing-161.mscz"));
    QVERIFY(score->saveCompressedFile(saved, false, false));
    std::unique_ptr<MasterScore> loaded(main->readScore(saved.filePath()));
    QVERIFY(loaded);
    auto other = toChord(loaded->firstSegment(SegmentType::ChordRest)->element(4))->notes().front();
    QCOMPARE(PerformanceTiming::values(other), PerformanceTiming::values(note));
    QCOMPARE(timingPair161(loaded.get(), other, 0), after);
    const auto midi = output.filePath("performance-timing-161.mid");
    QVERIFY(main->saveMidi(score, midi));
    std::atomic_bool canceled{false};
    const auto decoded = readPerformanceMidi(midi, canceled);
    QVERIFY(!decoded.contains("error"));
    bool found = false;
    for (const auto& value : decoded.value("notes").toList()) {
        const auto row = value.toMap();
        if (row.value("pitch").toInt() != note->ppitch())
            continue;
        QVERIFY(qAbs(row.value("seconds").toDouble() - after.first / 960.) < 1. / 960.);
        QVERIFY(qAbs(row.value("endSeconds").toDouble() - after.second / 960.) < 1. / 960.);
        found = true;
    }
    QVERIFY(found);
    {
        Note copy(*note);
        QCOMPARE(PerformanceTiming::values(&copy), PerformanceTiming::values(note));
    }
    main->cmd(getAction("undo"));
    QCOMPARE(timingPair161(score, note, 0), before);
    QVERIFY(PerformanceTiming::values(note).isEmpty());
    main->cmd(getAction("redo"));
    QCOMPARE(timingPair161(score, note, 0), after);
    score->startCmd();
    QVERIFY(PerformanceTiming::change(score, note, {}));
    score->endCmd();
    QCOMPARE(timingPair161(score, note, 0), before);
    main->cmd(getAction("undo"));
    QCOMPARE(timingPair161(score, note, 0), after);
    QCOMPARE(PerformanceTiming::lookahead(score), 0);
    auto stale = edit;
    stale.count += 1;
    stale.onset = -24000;
    score->startCmd();
    PerformanceTiming::change(score, note, {stale});
    score->endCmd();
    QCOMPARE(timingPair161(score, note, 0), before);
    QVERIFY(!PerformanceTiming::values(note).isEmpty());
    QCOMPARE(PerformanceTiming::lookahead(score), 24000);
    MidiRenderer renderer(score);
    renderer.setMinChunkSize(1);
    QCOMPARE(renderer.timingLookahead(), 24000);
}
void TestPluginHost::performanceTimingOrnaments161_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("crossMeasure");
    QTest::newRow("ties") << QString("note_input_tie") << false;
    QTest::newRow("cross-measure-ties") << QString("note_input_tie") << true;
    QTest::newRow("grace") << QString("palette_gracenotes_1") << false;
    QTest::newRow("arpeggio") << QString("palette_arpeggio_gliss_1") << false;
    QTest::newRow("tremolo") << QString("palette_tremolo_1") << false;
}
void TestPluginHost::performanceTimingLinked161()
{
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    score->startCmd();
    score->insertMeasure(ElementType::VBOX, score->first());
    score->endCmd();
    auto partScore = new Score(score);
    auto excerpt = new Excerpt(score);
    excerpt->setPartScore(partScore);
    excerpt->setParts({score->parts().front()});
    excerpt->setTitle("Piano");
    Excerpt::createExcerpt(excerpt);
    score->excerpts().append(excerpt);
    partScore->doLayout();
    auto masterNote =
        toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->notes().front();
    auto partNote =
        toChord(partScore->firstSegment(SegmentType::ChordRest)->element(4))->notes().front();
    score->createPlayEvents();
    partScore->createPlayEvents();
    auto edit = PerformanceTiming::value(partNote, 0);
    edit.onset = 30;
    edit.release = -60;
    partScore->startCmd();
    QVERIFY(PerformanceTiming::change(partScore, partNote, {edit}));
    partScore->endCmd();
    QCOMPARE(PerformanceTiming::values(masterNote), PerformanceTiming::values(partNote));
    EditData data;
    score->undoRedo(true, &data);
    QVERIFY(PerformanceTiming::values(masterNote).isEmpty());
    QVERIFY(PerformanceTiming::values(partNote).isEmpty());
    score->undoRedo(false, &data);
    QCOMPARE(PerformanceTiming::values(masterNote), PerformanceTiming::values(partNote));
    QCOMPARE(PerformanceTiming::value(masterNote, 0).onset, 30);
}
void TestPluginHost::performanceTimingOrnaments161()
{
    QFETCH(QString, name);
    QFETCH(bool, crossMeasure);
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/testscript/scripts/" + name + ".mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    EventMap events;
    score->renderMidi(&events, false, false, main->synthesizerState());
    Note* note = nullptr;
    int index = 0;
    for (const auto& pair : events) {
        const auto& event = pair.second;
        if (event.type() != ME_NOTEON || !event.velo() || !event.noteEventOwner())
            continue;
        auto candidate = const_cast<Note*>(event.noteEventOwner());
        if (name == "note_input_tie" && !candidate->tieFor())
            continue;
        if (crossMeasure &&
            candidate->lastTiedNote()->chord()->measure() == candidate->chord()->measure())
            continue;
        if (name == "palette_gracenotes_1" && !candidate->chord()->isGrace() &&
            !event.note()->chord()->isGrace())
            continue;
        if (name == "palette_arpeggio_gliss_1" && !candidate->chord()->arpeggio())
            continue;
        if (name == "palette_tremolo_1" && candidate->playEvents().size() < 2)
            continue;
        note = candidate;
        index = event.noteEventIndex();
        break;
    }
    QVERIFY2(note, qPrintable(name));
    const auto before = timingPair161(score, note, index);
    QVERIFY(before.second >= before.first);
    auto adjustment = PerformanceTiming::value(note, index);
    adjustment.onset = -10;
    adjustment.release = 15;
    const auto original = note->playEvents();
    const auto mode = note->chord()->playEventType();
    score->startCmd();
    QVERIFY(PerformanceTiming::change(score, note, {adjustment}));
    score->endCmd();
    auto after = timingPair161(score, note, index);
    QCOMPARE(after.first, before.first - 10);
    QCOMPARE(after.second, before.second + 15);
    QCOMPARE(note->playEvents(), original);
    QCOMPARE(note->chord()->playEventType(), mode);
    if (original.size() > 1) {
        const int other = index ? 0 : 1;
        const auto otherBefore = timingPair161(score, note, other);
        score->startCmd();
        PerformanceTiming::change(score, note, {});
        score->endCmd();
        QCOMPARE(timingPair161(score, note, other), otherBefore);
    }
    qInfo() << name << "event" << index << before << after;
}
void TestPluginHost::performanceTimingEditor161()
{
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1240, 850);
    main->show();
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    QVERIFY(editor);
    editor->showMidiTracks(false);
    if (auto tabs = editor->findChild<QTabBar*>("workflowExpressionTabs"))
        tabs->setCurrentIndex(0);
    auto button = editor->findChild<QToolButton*>("performanceTimingToggle");
    auto control = editor->findChild<TimingEditor*>();
    QVERIFY(button && control);
    QVERIFY(!button->isChecked());
    auto notes = editor->findChild<QWidget*>("performanceNoteCanvas");
    QVERIFY(notes);
    notes->parentWidget()->show();
    if (auto split = editor->findChild<QSplitter*>("performanceSplitter"))
        split->setSizes({280, 160, 0});
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(control->state().value("ready").toBool(), 5000);
    editor->fit();
    QTest::qWait(60);
    QVariantMap first;
    for (const auto& value : control->state().value("rows").toList()) {
        const auto row = value.toMap();
        const auto rect = row.value("rect").toRectF();
        if (row.value("enabled").toBool() && rect.width() > 45 && rect.center().x() > 90 &&
            notes->rect().adjusted(0, 5, 0, -5).contains(rect.center().toPoint())) {
            first = row;
            break;
        }
    }
    QVERIFY2(!first.isEmpty(), qPrintable(control->state().value("message").toString()));
    auto find = [&] {
        for (const auto& value : control->state().value("rows").toList()) {
            auto row = value.toMap();
            if (row.value("pitch") == first.value("pitch") &&
                row.value("track") == first.value("track") &&
                row.value("event") == first.value("event"))
                return row;
        }
        return QVariantMap();
    };
    auto drag = [&](QPoint start, QPoint end, bool cancel = false) {
        QTest::mousePress(notes, Qt::LeftButton, Qt::AltModifier, start);
        QMouseEvent move(QEvent::MouseMove, end, notes->mapToGlobal(end), Qt::NoButton,
                         Qt::LeftButton, Qt::AltModifier);
        QApplication::sendEvent(notes, &move);
        if (cancel)
            QTest::keyClick(notes, Qt::Key_Escape);
        QTest::mouseRelease(notes, Qt::LeftButton, Qt::AltModifier, end);
    };
    const int undo = score->undoStack()->getCurIdx();
    auto rectangle = first.value("rect").toRectF();
    drag(rectangle.center().toPoint(), rectangle.center().toPoint() + QPoint(22, 0));
    QTRY_VERIFY(find().value("on").toInt() > first.value("on").toInt());
    QCOMPARE(score->undoStack()->getCurIdx(), undo + 1);
    const auto moved = find();
    QCOMPARE(moved.value("end").toInt() - moved.value("on").toInt(),
             first.value("end").toInt() - first.value("on").toInt());
    QTest::keyClick(notes, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE(find().value("on").toInt(), first.value("on").toInt());
    rectangle = find().value("rect").toRectF();
    drag(QPoint(qRound(rectangle.right() - 1), qRound(rectangle.center().y())),
         QPoint(qRound(rectangle.right() + 20), qRound(rectangle.center().y())));
    QTRY_VERIFY(find().value("end").toInt() > first.value("end").toInt());
    QCOMPARE(find().value("on").toInt(), first.value("on").toInt());
    const auto lengthened = find();
    const int savedUndo = score->undoStack()->getCurIdx();
    rectangle = lengthened.value("rect").toRectF();
    drag(rectangle.center().toPoint(), rectangle.center().toPoint() + QPoint(30, 0), true);
    QCOMPARE(score->undoStack()->getCurIdx(), savedUndo);
    QCOMPARE(find().value("on").toInt(), lengthened.value("on").toInt());
    QVERIFY(QWidget::mouseGrabber() != notes);
    const int builds = control->state().value("builds").toInt();
    auto selected = score->selection().noteList();
    QVERIFY(!selected.empty());
    const int velocityBefore = selected.front()->veloOffset();
    rectangle = find().value("rect").toRectF();
    const QPoint wheelPoint(qRound(rectangle.right() - 6), qRound(rectangle.center().y()));
    QWheelEvent wheel(wheelPoint, notes->mapToGlobal(wheelPoint), QPoint(), QPoint(0, 480),
                      Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(notes, &wheel);
    QTRY_VERIFY(selected.front()->veloOffset() != velocityBefore);
    QTRY_VERIFY(!control->state().value("rows").toList().isEmpty());
    QCOMPARE(control->state().value("builds").toInt(), builds);
    QTest::qWait(80);
    main->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS") +
                      "/performance-timing-editor-161.png");
    editor->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS") +
                        "/performance-timing-controls-161.png");
    auto restore = editor->findChild<QAction*>("performanceTimingRestore0");
    QVERIFY(restore);
    restore->trigger();
    QTRY_COMPARE(find().value("on").toInt(), first.value("on").toInt());
    QTRY_COMPARE(find().value("end").toInt(), first.value("end").toInt());
    auto chord = toChord(
        score->tick2segment(Fraction::fromTicks(480), false, SegmentType::ChordRest)->element(0));
    score->select(chord->notes()[0], SelectType::SINGLE);
    score->select(chord->notes()[1], SelectType::ADD);
    editor->selectionChanged();
    QTest::qWait(30);
    const auto beforeGroup = find();
    const int groupUndo = score->undoStack()->getCurIdx();
    rectangle = beforeGroup.value("rect").toRectF();
    drag(rectangle.center().toPoint(), rectangle.center().toPoint() + QPoint(24, 0));
    QTRY_VERIFY(find().value("on").toInt() > beforeGroup.value("on").toInt());
    const int shift = find().value("on").toInt() - beforeGroup.value("on").toInt();
    int selectedRows = 0;
    for (const auto& value : control->state().value("rows").toList()) {
        const auto row = value.toMap();
        if (!row.value("selected").toBool())
            continue;
        QCOMPARE(row.value("on").toInt() - row.value("originalOn").toInt(), shift);
        ++selectedRows;
    }
    QCOMPARE(selectedRows, 2);
    QCOMPARE(score->undoStack()->getCurIdx(), groupUndo + 1);
    restore->trigger();
    QTRY_COMPARE(find().value("on").toInt(), first.value("on").toInt());
    rectangle = find().value("rect").toRectF();
    const auto start = rectangle.center().toPoint();
    const auto end = start + QPoint(20, 0);
    const int focusUndo = score->undoStack()->getCurIdx();
    QTest::mousePress(notes, Qt::LeftButton, Qt::AltModifier, start);
    QMouseEvent move(QEvent::MouseMove, end, notes->mapToGlobal(end), Qt::NoButton, Qt::LeftButton,
                     Qt::AltModifier);
    QApplication::sendEvent(notes, &move);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(editor, &deactivate);
    QTest::mouseRelease(notes, Qt::LeftButton, Qt::AltModifier, end);
    QVERIFY(QWidget::mouseGrabber() != notes);
    QTRY_COMPARE(score->undoStack()->getCurIdx(), focusUndo + 1);
    QTRY_VERIFY(find().value("on").toInt() > first.value("on").toInt());
    restore->trigger();
    QTRY_COMPARE(find().value("on").toInt(), first.value("on").toInt());
    QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY(!control->state().value("enabled").toBool());
}
