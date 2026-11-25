// SPDX-License-Identifier: GPL-2.0-or-later
void TestPerformanceEditor::rollTimingAndSelection1701()
{
    expressionHost080(_directory.path());
    auto main = Ms::mscore;
    auto score = expressionScore080(main, _directory.filePath("roll1701.mscx"));
    QVERIFY(score);
    expressionShowMain080(main);
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    editor->showMidiTracks(false);
    editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
    auto canvas = editor->findChild<QWidget*>("performanceNoteCanvas");
    auto control = editor->findChild<TimingEditor*>();
    auto toggle = editor->findChild<QToolButton*>("performanceTimingToggle");
    QVERIFY(canvas && control && toggle);
    canvas->parentWidget()->show();
    if (auto split = editor->findChild<QSplitter*>("performanceSplitter")) split->setSizes({260,160,0});
    toggle->setChecked(true);
    QTRY_VERIFY(control->state()["ready"].toBool());
    editor->fit();
    QTest::qWait(80);
    QVariantMap first;
    for (auto value : control->state()["rows"].toList()) {
        auto row = value.toMap(); auto rect = row["rect"].toRectF();
        if (row["enabled"].toBool() && rect.width() > 30 && canvas->rect().adjusted(80,4,-40,-4).contains(rect.center().toPoint())) { first=row; break; }
    }
    QVERIFY(!first.isEmpty());
    auto find = [&]() {
        for (auto value : control->state()["rows"].toList()) {
            auto row=value.toMap();
            if (row["originalOn"]==first["originalOn"] && row["pitch"]==first["pitch"] && row["track"]==first["track"]) return row;
        }
        return QVariantMap();
    };
    auto right = [&]() {auto rect=find()["rect"].toRectF(); return QPoint(qRound(rect.right()-1),qRound(rect.center().y()));};
    const int before=first["end"].toInt(), undo=score->undoStack()->getCurIdx();
    const QPoint start=right(), middle=start+QPoint(15,0), end=start+QPoint(29,0);
    QTest::mousePress(canvas,Qt::LeftButton,Qt::AltModifier,start);
    QMouseEvent move(QEvent::MouseMove,middle,canvas->mapToGlobal(middle),Qt::NoButton,Qt::LeftButton,Qt::AltModifier);
    QApplication::sendEvent(canvas,&move);
    const int preview=find()["end"].toInt(); QVERIFY(preview>before);
    // A passive hover from another widget must not end a held drag.
    QWidget unrelated;
    QMouseEvent hover(QEvent::MouseMove,QPointF(1,1),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&unrelated,&hover);
    QTest::mouseRelease(canvas,Qt::LeftButton,Qt::AltModifier,end);
    QTRY_VERIFY(find()["end"].toInt()>preview);
    QTRY_COMPARE(score->undoStack()->getCurIdx(),undo+1);
    QTest::keyClick(canvas,Qt::Key_Z,Qt::ControlModifier);
    QTRY_COMPARE(find()["end"].toInt(),before);
    auto grid=editor->findChild<QComboBox*>("performanceTimingGrid"); QVERIFY(grid);
    grid->setCurrentIndex(grid->count()-1);
    const auto shortDragStart=right();
    // No intermediate move: the final release still carries the edit.
    QTest::mousePress(canvas,Qt::LeftButton,{},shortDragStart);
    QTest::mouseRelease(canvas,Qt::LeftButton,{},shortDragStart+QPoint(3,0));
    QTRY_VERIFY(find()["end"].toInt()>before);
    QTest::keyClick(canvas,Qt::Key_Z,Qt::ControlModifier);
    QTRY_COMPARE(find()["end"].toInt(),before);
    const auto rect=find()["rect"].toRectF();
    const auto a=rect.topLeft().toPoint()-QPoint(3,3), b=rect.bottomRight().toPoint()+QPoint(3,3);
    QTest::mousePress(canvas,Qt::RightButton,{},a);
    QMouseEvent box(QEvent::MouseMove,b,canvas->mapToGlobal(b),Qt::NoButton,Qt::RightButton,{});
    QApplication::sendEvent(canvas,&box);
    QTest::mouseRelease(canvas,Qt::RightButton,{},b);
    QVERIFY(!score->selection().noteList().empty());
    QVERIFY(QWidget::mouseGrabber()!=canvas);
    const auto selected=score->selection().noteList().size();
    QTest::mousePress(canvas,Qt::RightButton,{},a);
    QApplication::sendEvent(canvas,&box);
    QTest::keyClick(canvas,Qt::Key_Escape);
    QTest::mouseRelease(canvas,Qt::RightButton,{},b);
    QCOMPARE(score->selection().noteList().size(),selected);
    auto line=editor->findChild<QWidget*>("performanceContinuousPlayhead");
    QVERIFY(line && line->testAttribute(Qt::WA_TransparentForMouseEvents));
    score->setPlayPos(Fraction::fromTicks(240));
    QTest::qWait(80);
    QVERIFY(line->isVisible());
    QVERIFY(line->height()>canvas->height());
    auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    QVERIFY(editor->grab().save(out.filePath("roll1701-playhead.png")));
    toggle->setChecked(false);
    // The same right gesture works with ordinary roll selection and writing IDs.
    QTest::mousePress(canvas,Qt::RightButton,{},a);
    QApplication::sendEvent(canvas,&box);
    QTest::mouseRelease(canvas,Qt::RightButton,{},b);
    QVERIFY(!score->selection().noteList().empty());
    auto writing=editor->findChild<RollWritingBar*>(); QVERIFY(writing);
    rollWritingAction()->setChecked(true);
    QTRY_VERIFY(writing->state()["ready"].toBool());
    const int writingUndo=score->undoStack()->getCurIdx();
    QTest::mousePress(canvas,Qt::RightButton,{},a);
    QApplication::sendEvent(canvas,&box);
    QTest::mouseRelease(canvas,Qt::RightButton,{},b);
    QVERIFY(!writing->state()["selected"].toList().empty());
    QCOMPARE(score->undoStack()->getCurIdx(),writingUndo);
    rollWritingAction()->setChecked(false);
    editor->showMidiTracks(true);
    auto tracks=editor->findChild<MidiTrackPane*>(); QVERIFY(tracks);
    tracks->importFile(QString(TESTROOT)+"/mtest/importmidi/chord_legato.mid");
    QTRY_VERIFY_WITH_TIMEOUT(!tracks->state()["busy"].toBool() && !tracks->state()["visibleNotes"].toList().empty(),10000);
    const QPoint midiFrom(PerformanceViewport::gutter+1,1), midiTo(canvas->width()-20,canvas->height()-1);
    QTest::mousePress(canvas,Qt::RightButton,{},midiFrom);
    QMouseEvent midiBox(QEvent::MouseMove,midiTo,canvas->mapToGlobal(midiTo),Qt::NoButton,Qt::RightButton,{});
    QApplication::sendEvent(canvas,&midiBox);
    QTest::mouseRelease(canvas,Qt::RightButton,{},midiTo);
    QVERIFY(tracks->state()["selectedNotes"].toInt()>0);
    editor->showMidiTracks(false);
}
