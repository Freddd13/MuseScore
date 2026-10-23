// Read the same staff dynamics while editing, scrolling and filtering real widgets.
#include "libmscore/hairpin.h"
static bool chooseVelocityTracks(PerformanceEditor& editor, const QSet<int>& tracks, bool ghost)
{
    bool chosen = false;
    QTimer::singleShot(30, &editor, [&] {
        auto table = editor.findChild<QTableWidget*>("performanceTrackTable");
        if (!table) return;
        for (int row = 0; row < table->rowCount(); ++row)
            for (int voice = 0; voice < 4; ++voice)
                table->item(row, voice)->setCheckState(tracks.contains(row*4 + voice) ? Qt::Checked : Qt::Unchecked);
        auto dialog = qobject_cast<QDialog*>(table->window());
        if (!dialog) return;
        if (auto option = dialog->findChild<QCheckBox*>()) option->setChecked(ghost);
        chosen = true;
        dialog->accept();
    });
    QTest::mouseClick(editor.findChild<QToolButton*>("performanceTrackFilter"), Qt::LeftButton);
    return chosen;
}

void TestPerformanceEditor::velocityReferencesAcrossRestsAndFilters()
{
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    auto first = score->tick2segment(Fraction::fromTicks(0), false, SegmentType::ChordRest);
    score->startCmd();
    for (const auto setting : QList<QPair<int, int>>{{0, 80}, {4, 48}}) {
        auto mark = new Dynamic(score);
        mark->setTrack(setting.first);
        mark->setDynamicType(setting.first ? "p" : "mf");
        mark->setVelocity(setting.second);
        mark->setDynRange(Dynamic::Range::STAFF);
        mark->setParent(first);
        score->undoAddElement(mark);
    }
    score->endCmd();
    PerformanceEditor editor;
    editor.resize(1160, 760);
    editor.setView(main->currentScoreView());
    editor.show();
    editor.findChild<QComboBox*>("performanceScope")->setCurrentIndex(2);
    editor.findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    editor.findChild<QComboBox*>("performanceVelocityArrangement")->setCurrentIndex(0);
    QTest::qWait(100);
    editor.fit();
    auto canvas = editor.findChild<QWidget*>("performanceParameterCanvas");
    auto references = editor.findChild<QCheckBox*>("performanceActualVelocity");
    QScrollBar* time = nullptr;
    for (auto scroll : editor.findChildren<QScrollBar*>(QString(), Qt::FindDirectChildrenOnly))
        if (scroll->orientation() == Qt::Horizontal)
            time = scroll;
    QVERIFY(canvas && references && time);
    // Zoom the timeline into the rests after both staves' last note. The mf/p
    // reference values continue to apply here and must stay readable.
    for (int step = 0; step < 8; ++step) {
        const QPoint point(400, canvas->height()/2);
        QWheelEvent zoom(point, canvas->mapToGlobal(point), QPoint(), QPoint(0, 120),
                         Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas, &zoom);
    }
    time->setValue(1100);
    QVERIFY(time->value() >= 960);
    QTest::qWait(50);
    const auto content = score->state();
    const int undo = score->undoStack()->getCurIdx();
    references->setChecked(false);
    const auto plain = canvas->grab().toImage();
    references->setChecked(true);
    const auto labelled = canvas->grab().toImage();
    const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _directory.path()));
    QVERIFY(plain.save(output.filePath("velocity-rests-off-090.png")));
    QVERIFY(labelled.save(output.filePath("velocity-rests-on-090.png")));
    QVERIFY2(plain != labelled, "滚到长音或休止中间时，两谱表的力度参考线继续显示");
    auto differsAt = [&](int value, const QImage& off, const QImage& on) {
        const int y = qRound(canvas->height()-6-(value-1)*(canvas->height()-12)/126.);
        const QRect strip(canvas->width()/2, y-4, 80, 9);
        return off.copy(strip) != on.copy(strip);
    };
    QVERIFY(differsAt(80, plain, labelled));
    QVERIFY(differsAt(48, plain, labelled));
    QVERIFY(score->state() == content);
    QCOMPARE(score->undoStack()->getCurIdx(), undo);

    // A dynamic and a crescendo continue through rests and the middle of a
    // sounding note, even when there is no new note at the change position.
    auto rest = score->tick2segment(Fraction::fromTicks(960), false, SegmentType::ChordRest);
    QVERIFY(rest);
    score->startCmd();
    auto loud = new Dynamic(score);
    loud->setTrack(0); loud->setDynamicType("ff"); loud->setVelocity(112);
    loud->setDynRange(Dynamic::Range::STAFF); loud->setParent(rest);
    score->undoAddElement(loud);
    auto hairpin = new Hairpin(score);
    hairpin->setTrack(4); hairpin->setTrack2(4);
    hairpin->setTick(Fraction::fromTicks(960));
    hairpin->setTicks(Fraction::fromTicks(960));
    hairpin->setHairpinType(HairpinType::CRESC_HAIRPIN);
    hairpin->setDynRange(Dynamic::Range::STAFF);
    hairpin->setVeloChange(32);
    score->undoAddElement(hairpin);
    score->endCmd();
    score->updateVelo();
    editor.updateAll();
    QTest::qWait(100);
    references->setChecked(false);
    const auto changedPlain = canvas->grab().toImage();
    references->setChecked(true);
    const auto changedLines = canvas->grab().toImage();
    QVERIFY(differsAt(112, changedPlain, changedLines));
    QVERIFY(!differsAt(80, changedPlain, changedLines));
    const int centerTick = time->value() + time->pageStep()/2;
    QVERIFY(score->staff(1)->velocities().val(Fraction::fromTicks(centerTick)) > 48);
    QVERIFY(changedLines.save(output.filePath("velocity-rests-crescendo-090.png")));
    auto paintTime = [&] {
        QVector<double> elapsed;
        QImage image(canvas->size(), QImage::Format_ARGB32_Premultiplied);
        for (int frame = 0; frame < 30; ++frame) {
            QElapsedTimer timer; timer.start(); canvas->render(&image);
            elapsed.append(timer.nsecsElapsed()/1e6);
        }
        std::sort(elapsed.begin(), elapsed.end());
        return qMakePair(elapsed[15], elapsed[28]);
    };
    references->setChecked(false); const auto withoutLines = paintTime();
    references->setChecked(true); const auto withLines = paintTime();
    qInfo("two-staff dynamics paint: guides off median %.3f / P95 %.3f ms, on %.3f / %.3f ms",
          withoutLines.first, withoutLines.second, withLines.first, withLines.second);

    QVERIFY(chooseVelocityTracks(editor, {0}, false));
    references->setChecked(false); const auto filteredPlain = canvas->grab().toImage();
    references->setChecked(true); const auto filteredLines = canvas->grab().toImage();
    QVERIFY(differsAt(112, filteredPlain, filteredLines));
    QVERIFY(!differsAt(48, filteredPlain, filteredLines));
    auto arrangement = editor.findChild<QComboBox*>("performanceVelocityArrangement");
    arrangement->setCurrentIndex(1);
    const auto oneStaff = canvas->grab().toImage();
    QVERIFY(chooseVelocityTracks(editor, {0}, true));
    const auto ghostStaff = canvas->grab().toImage();
    QVERIFY(oneStaff != ghostStaff);
    QVERIFY(ghostStaff.save(output.filePath("velocity-filtered-reference-090.png")));
    QVERIFY(chooseVelocityTracks(editor, {0, 4}, false));
    editor.fit();
    QVERIFY(editor.grab().save(output.filePath("velocity-staff-sections-090.png")));
}

void TestPerformanceEditor::velocityGroupEditingKeepsStaffValues()
{
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    auto segment = score->tick2segment(Fraction::fromTicks(0), false, SegmentType::ChordRest);
    score->startCmd();
    for (const auto setting : QList<QPair<int, int>>{{0, 80}, {4, 48}}) {
        auto mark = new Dynamic(score);
        mark->setTrack(setting.first); mark->setDynamicType(setting.first ? "p" : "mf");
        mark->setVelocity(setting.second); mark->setDynRange(Dynamic::Range::STAFF);
        mark->setParent(segment); score->undoAddElement(mark);
    }
    score->endCmd();
    auto upper = noteAt(score), lower = noteAt(score, 0, 4);
    QVERIFY(ParameterEdit::velocities(score, {{upper, {Note::ValueType::USER_VAL, 80}},
                                            {lower, {Note::ValueType::OFFSET_VAL, 0}}}));
    PerformanceEditor editor;
    editor.resize(1160, 800); editor.setView(main->currentScoreView()); editor.show();
    editor.findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    editor.findChild<QComboBox*>("performanceVelocityArrangement")->setCurrentIndex(0);
    editor.findChild<QComboBox*>("performanceDragSensitivity")->setCurrentIndex(2);
    editor.findChild<QComboBox*>("performanceTool")->setCurrentIndex(0);
    auto group = editor.findChild<QComboBox*>("performanceGroupVelocity");
    group->setCurrentIndex(0);
    QTest::qWait(100); editor.fit();
    PerformanceSelection::apply(score, {upper, lower}, upper); editor.selectionChanged();
    auto canvas = editor.findChild<QWidget*>("performanceParameterCanvas");
    auto dragEight = [&] {
        const QPoint from(76 + qRound((canvas->width()-90)/4.),
                          qRound(canvas->height()-6-79*(canvas->height()-12)/126.));
        const QPoint to = from - QPoint(0, qRound(8*(canvas->height()-12)/126.));
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
    };
    const int undo = score->undoStack()->getCurIdx();
    dragEight();
    QTRY_COMPARE(score->undoStack()->getCurIdx(), undo+1);
    QVERIFY(qAbs(upper->veloOffset()-88) <= 1);
    QVERIFY(qAbs(NoteVelocity::effective(48, lower->veloType(), lower->veloOffset())-53) <= 1);
    QCOMPARE(upper->veloType(), Note::ValueType::USER_VAL);
    QCOMPARE(lower->veloType(), Note::ValueType::OFFSET_VAL);
    auto actualMidi = [&](Note* note) {
        EventMap events; score->renderMidi(&events, false, false, defaultState);
        for (const auto& pair : events)
            if (pair.second.type() == ME_NOTEON && pair.second.velo() && pair.second.note() == note)
                return pair.second.velo();
        return -1;
    };
    QCOMPARE(actualMidi(upper), upper->veloOffset());
    QCOMPARE(actualMidi(lower), NoteVelocity::effective(48, lower->veloType(), lower->veloOffset()));
    score->undoRedo(true, &ed); QTest::qWait(80);
    QCOMPARE(actualMidi(upper), 80); QCOMPARE(actualMidi(lower), 48);
    group->setCurrentIndex(1); dragEight(); QTest::qWait(80);
    QVERIFY(qAbs(actualMidi(upper)-88) <= 1);
    QVERIFY(qAbs(actualMidi(lower)-56) <= 1);
    const auto values = qMakePair(actualMidi(upper), actualMidi(lower));
    editor.convertSelected(Note::ValueType::USER_VAL); QTest::qWait(80);
    QCOMPARE(qMakePair(actualMidi(upper), actualMidi(lower)), values);
    editor.convertSelected(Note::ValueType::OFFSET_VAL); QTest::qWait(80);
    QVERIFY(qAbs(actualMidi(upper)-values.first) <= 1);
    QVERIFY(qAbs(actualMidi(lower)-values.second) <= 1);
    editor.findChild<QComboBox*>("performanceAxis")->setCurrentIndex(1);
    editor.setSelectedValue(10); QTest::qWait(80);
    // Native integer percentages retain 48 + (48*10/100) = 52.
    QCOMPARE(actualMidi(upper), 88); QCOMPARE(actualMidi(lower), 52);
    const int preservedLower = lower->veloOffset();
    QVERIFY(chooseVelocityTracks(editor, {0}, false));
    editor.setSelectedValue(20); QTest::qWait(80);
    QCOMPARE(lower->veloOffset(), preservedLower);
    QCOMPARE(actualMidi(upper), 96);
    QVERIFY(lower->selected());
    const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _directory.path()));
    QFileInfo saved(output.filePath("velocity-group-090.mscz"));
    QVERIFY(score->saveCompressedFile(saved, false, false));
    std::unique_ptr<MasterScore> restored(main->readScore(saved.filePath()));
    QVERIFY(restored);
    QCOMPARE(noteAt(restored.get())->veloOffset(), upper->veloOffset());
    QCOMPARE(noteAt(restored.get(), 0, 4)->veloOffset(), lower->veloOffset());
}

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
void TestPerformanceEditor::nearestVelocityThroughWindowsInput()
{
#ifdef Q_OS_WIN
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1280, 900); main->show(); main->raise(); main->activateWindow();
    auto view = main->currentScoreView();
    auto editor = main->performanceEditor();
    editor->setView(view);
    for (auto check : editor->findChildren<QCheckBox*>()) check->setChecked(false);
    editor->findChild<QToolButton*>("performanceWheelVelocity")->setChecked(false);
    main->showPerformanceEditor(false);
    auto note = noteAt(score);
    QVERIFY(ParameterEdit::velocities(score, {{note, {Note::ValueType::USER_VAL, 64}}}));
    view->setFocus();
    QTest::qWait(100);
    QVERIFY(QTest::qWaitForWindowActive(main));
    const auto window = reinterpret_cast<HWND>(main->winId());
    const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD currentThread = GetCurrentThreadId();
    const bool attached = foregroundThread != currentThread
                          && AttachThreadInput(currentThread, foregroundThread, TRUE);
    ShowWindow(window, SW_RESTORE); BringWindowToTop(window); SetForegroundWindow(window);
    if (attached) AttachThreadInput(currentThread, foregroundThread, FALSE);
    QTest::qWait(100);
    DWORD owner = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &owner);
    QCOMPARE(owner, GetCurrentProcessId());
    QVERIFY(!(GetAsyncKeyState(VK_MENU)&0x8000) && !(GetAsyncKeyState(VK_SHIFT)&0x8000));
    struct NativeInput {
        POINT cursor{};
        HKL layout = GetKeyboardLayout(0);
        bool held = false;
        NativeInput() { GetCursorPos(&cursor); }
        void keys(bool down) {
            INPUT input[2]{};
            for (int index = 0; index < 2; ++index) {
                input[index].type = INPUT_KEYBOARD;
                input[index].ki.wVk = index ? VK_LSHIFT : VK_LMENU;
                input[index].ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
            }
            SendInput(2, input, sizeof(INPUT)); held = down;
        }
        ~NativeInput() {
            if (held) keys(false);
            ActivateKeyboardLayout(layout, 0); SetCursorPos(cursor.x, cursor.y);
        }
    } input;
    const QPoint head = view->matrix().mapRect(note->canvasBoundingRect()).center().toPoint();
    QVERIFY(view->rect().contains(head));
    const QPoint screen = view->mapToGlobal(head);
    SetCursorPos(screen.x(), screen.y());
    const auto matrix = view->matrix();
    const int undo = score->undoStack()->getCurIdx();
    input.keys(true);
    QTest::qWait(50);
    QVERIFY(QApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier));
    QVERIFY(QApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier));
    INPUT wheel{}; wheel.type = INPUT_MOUSE;
    wheel.mi.dwFlags = MOUSEEVENTF_WHEEL; wheel.mi.mouseData = WHEEL_DELTA;
    QCOMPARE(SendInput(1, &wheel, sizeof(INPUT)), UINT(1));
    QTRY_VERIFY(editor->hasPending());
    input.keys(false);
    QTRY_COMPARE(note->veloOffset(), 65);
    QCOMPARE(view->matrix(), matrix);
    QCOMPARE(score->undoStack()->getCurIdx(), undo+1);
    const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _directory.path()));
    QVERIFY(view->grab().save(output.filePath("velocity-windows-wheel-090.png")));
    // In relative view +1% can still round to the same MIDI value. The badge
    // keeps the saved percentage rather than reverse-calculating it from MIDI.
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(1);
    QVERIFY(ParameterEdit::velocities(score, {{note, {Note::ValueType::OFFSET_VAL, 0}}}));
    QTest::qWait(100);
    input.keys(true); QTest::qWait(40);
    QCOMPARE(SendInput(1, &wheel, sizeof(INPUT)), UINT(1));
    QTRY_VERIFY(editor->hasPending()); input.keys(false);
    QTRY_COMPARE(note->veloOffset(), 1);
    QCOMPARE(note->customizeVelocity(80), 80);
    QVERIFY(view->grab().save(output.filePath("velocity-relative-wheel-090.png")));
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
#else
    QSKIP("Windows key and wheel delivery");
#endif
}

void TestPerformanceEditor::detachedRollVelocityValues()
{
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    PerformanceEditor editor;
    editor.resize(1100, 760); editor.setView(main->currentScoreView()); editor.show();
    QTest::qWait(100); editor.fit();
    auto notes = editor.findChild<QWidget*>("performanceNoteCanvas");
    QVERIFY(notes);
    QAction* values = nullptr;
    for (auto action : editor.findChildren<QAction*>())
        if (action->text() == QString::fromUtf8("一直显示力度（谱面与卷帘）")) values = action;
    QVERIFY(values); values->setChecked(false);
    auto outside = editor.findChild<QAction*>("editorArea/float/performanceNotesArea");
    QVERIFY(outside); outside->trigger();
    QTest::qWait(100);
    notes->window()->resize(900, 480); notes->show(); notes->setFocus();
    QTest::qWait(60);
    const auto content = score->state();
    const int undo = score->undoStack()->getCurIdx();
    const auto before = notes->grab().toImage();
    QTest::keyPress(notes, Qt::Key_V, Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier);
    const auto temporary = notes->grab().toImage();
    QTest::keyRelease(notes, Qt::Key_V, Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier);
    const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _directory.path()));
    QVERIFY(temporary.save(output.filePath("velocity-detached-roll-090.png")));
    QVERIFY2(before != temporary, "卷帘移出后，按住查看力度仍作用于这一块卷帘");
    QCOMPARE(notes->grab().toImage(), before);
    values->setChecked(true);
    QVERIFY(notes->grab().toImage() != before);
    values->setChecked(false);
    QVERIFY(score->state() == content);
    QCOMPARE(score->undoStack()->getCurIdx(), undo);
    editor.findChild<QAction*>("editorArea/restore/performanceNotesArea")->trigger();
}
