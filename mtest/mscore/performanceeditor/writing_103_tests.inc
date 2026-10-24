// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "libmscore/tie.h"
void TestPerformanceEditor::rollWritingBesideBoundaryTie()
{
    expressionHost080(_directory.path());
    auto main = Ms::mscore;
    auto score = expressionScore080(main, _directory.filePath("roll-boundary-103.mscx"));
    QVERIFY(score);
    expressionShowMain080(main);
    auto a = toChord(score->findCR(Fraction::fromTicks(1440), 0))->notes().front();
    auto b = toChord(score->findCR(Fraction::fromTicks(1920), 0))->notes().front();
    b->setPitch(a->pitch());
    b->setTpc1(a->tpc1());
    b->setTpc2(a->tpc2());
    score->startCmd();
    auto tie = new Tie(score);
    tie->setStartNote(a);
    tie->setEndNote(b);
    tie->setTrack(0);
    score->undoAddElement(tie);
    score->endCmd();
    score->doLayout();
    QVERIFY(a->tieFor() && b->tieBack());
    score->select(score->findCR(Fraction(), 0));
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    auto tabs = editor->findChild<QTabBar *>("workflowExpressionTabs");
    QVERIFY(tabs);
    tabs->setCurrentIndex(0);
    auto bar = editor->findChild<RollWritingBar *>("rollWritingBar");
    auto toggle = editor->findChild<QToolButton *>("rollWriteToggle");
    QVERIFY(bar && toggle);
    struct Restore
    {
        ~Restore() { rollWritingAction()->setChecked(false); }
    } restore;
    if (!toggle->isChecked())
        QTest::mouseClick(toggle, Qt::LeftButton);
    QTest::qWait(250);
    auto status = bar->findChild<QLabel *>("rollWriteStatus");
    QVERIFY(status);
    qInfo() << "Boundary writing scope" << bar->state()["scope"] << "status" << status->text();
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _directory.path()));
    QVERIFY(bar->grab().save(out.filePath("roll-boundary-before-103.png")));
    QTRY_VERIFY2_WITH_TIMEOUT(bar->state()["ready"].toBool(), qUtf8Printable(status->text()), 3000);
    auto scope = bar->state()["scope"].toMap();
    QCOMPARE(scope["start"].toInt(), 0);
    QCOMPARE(scope["end"].toInt(), 1920);
    const auto sourceScope = QVariantMap{{"start", 0}, {"end", 7680}, {"firstTrack", 0}, {"endTrack", 1}};
    const auto sourceBefore = rollNotationIdentity(score, sourceScope);
    auto events = [&]
    {
        const auto material = renderPlaybackPreview(score, score);
        QStringList result;
        for (const auto &v : material.events)
        {
            const auto &e = v.second;
            if ((e.type() == ME_NOTEON || e.type() == ME_NOTEOFF) && !(e.pitch() == 72 && v.first < 960))
                result << QString("%1:%2:%3:%4:%5")
                              .arg(v.first)
                              .arg(e.type())
                              .arg(e.channel())
                              .arg(e.pitch())
                              .arg(e.velo());
        }
        return result;
    };
    const auto midiBefore = events();
    auto track = bar->findChild<QComboBox *>("rollWriteTrack");
    auto grid = bar->findChild<QComboBox *>("rollWriteGrid");
    auto canvas = editor->findChild<QWidget *>("performanceNoteCanvas");
    QVERIFY(track && grid && canvas);
    track->setCurrentIndex(track->findData(1));
    grid->setCurrentIndex(1);
    editor->fit(false);
    QTest::qWait(80);
    auto pitchScroll = canvas->findChild<QScrollBar *>();
    QVERIFY(pitchScroll);
    pitchScroll->setValue((127 - 78) * 100);
    QTest::qWait(30);
    const double row = canvas->height() * 100. / pitchScroll->pageStep(),
                 top = 127 - pitchScroll->value() / 100.;
    const double x = bar->state()["fromX"].toDouble(), step = bar->state()["quarterPixels"].toDouble();
    const QPoint start(qRound(x + .5 * step), qRound((top - 72 + .5) * row)),
        end(qRound(x + 1.5 * step), start.y());
    QVERIFY(canvas->rect().contains(start) && canvas->rect().contains(end));
    const int undo = score->undoStack()->getCurIdx();
    const auto rowsBefore = bar->state()["notes"].toList();
    for (int interrupted = 0; interrupted < 3; ++interrupted)
    {
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, end, 30);
        QCOMPARE(QWidget::mouseGrabber(), canvas);
        if (interrupted == 0)
        {
            QEvent event(QEvent::ApplicationDeactivate);
            QApplication::sendEvent(qApp, &event);
        }
        else if (interrupted == 1)
        {
            QWidget receiver;
            receiver.show();
            receiver.grabMouse();
            QCOMPARE(QWidget::mouseGrabber(), &receiver);
            receiver.releaseMouse();
        }
        else
        {
            QEvent event(QEvent::WindowDeactivate);
            QApplication::sendEvent(canvas->window(), &event);
        }
        qInfo() << "Interrupted writing" << interrupted << "rows" << bar->state()["notes"].toList().size()
                << "grabber" << QWidget::mouseGrabber();
        QTRY_VERIFY(!QWidget::mouseGrabber());
        QCOMPARE(bar->state()["notes"].toList(), rowsBefore);
        QCOMPARE(score->undoStack()->getCurIdx(), undo);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
        main->activateWindow();
        QTest::qWait(60);
    }
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(canvas, end, 30);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    QTRY_VERIFY2_WITH_TIMEOUT(bar->state()["ready"].toBool(), qUtf8Printable(status->text()), 10000);
    auto added = score->findCR(Fraction::fromTicks(240), 1);
    QVERIFY2(added && added->isChord(), qUtf8Printable(status->text()));
    QCOMPARE(toChord(added)->notes().front()->pitch(), 72);
    QCOMPARE(added->actualTicks().ticks(), 480);
    QCOMPARE(score->undoStack()->getCurIdx(), undo + 1);
    QCOMPARE(rollNotationIdentity(score, sourceScope), sourceBefore);
    QCOMPARE(events(), midiBefore);
    auto tied = toChord(score->findCR(Fraction::fromTicks(1440), 0))->notes().front();
    QVERIFY(tied->tieFor());
    QCOMPARE(tied->tieFor()->endNote()->chord()->tick().ticks(), 1920);
    QFileInfo saved(_directory.filePath("roll-boundary-applied-103.mscz"));
    QVERIFY(score->saveCompressedFile(saved, false, false));
    std::unique_ptr<MasterScore> reopened(main->readScore(saved.absoluteFilePath()));
    QVERIFY(reopened);
    QCOMPARE(rollNotationIdentity(reopened.get(), sourceScope), sourceBefore);
    QVERIFY(toChord(reopened->findCR(Fraction::fromTicks(1440), 0))->notes().front()->tieFor());
    getAction("undo")->trigger();
    QTRY_VERIFY(bar->state()["ready"].toBool());
    auto erased = score->findCR(Fraction::fromTicks(240), 1);
    QVERIFY(!erased || !erased->isChord());
    QCOMPARE(events(), midiBefore);
    getAction("redo")->trigger();
    QTRY_VERIFY(bar->state()["ready"].toBool());
    QVERIFY(score->findCR(Fraction::fromTicks(240), 1)->isChord());
    QCOMPARE(events(), midiBefore);
    QTest::qWait(80);
    QVERIFY(editor->grab().save(out.filePath("roll-boundary-written-103.png")));
}

void TestPerformanceEditor::rollWritingBetweenBoundaryTies()
{
    expressionHost080(_directory.path());
    auto main = Ms::mscore;
    auto score = expressionScore080(main, _directory.filePath("roll-through-ties-103.mscx"));
    QVERIFY(score);
    expressionShowMain080(main);
    auto noteAt = [&](int tick)
    { return toChord(score->findCR(Fraction::fromTicks(tick), 0))->notes().front(); };
    score->startCmd();
    for (int tick : {1440, 3360})
    {
        auto a = noteAt(tick), b = noteAt(tick + 480);
        b->setPitch(a->pitch());
        b->setTpc1(a->tpc1());
        b->setTpc2(a->tpc2());
        auto tie = new Tie(score);
        tie->setStartNote(a);
        tie->setEndNote(b);
        tie->setTrack(0);
        score->undoAddElement(tie);
    }
    score->endCmd();
    score->doLayout();
    auto target = noteAt(2400);
    target->setVeloType(Note::ValueType::USER_VAL);
    target->setVeloOffset(91);
    auto events = [&]
    {
        const auto material = renderPlaybackPreview(score, score);
        QStringList result;
        for (const auto &v : material.events)
        {
            const auto &e = v.second;
            if (e.type() == ME_NOTEON || e.type() == ME_NOTEOFF)
                result << QString("%1:%2:%3:%4:%5")
                              .arg(v.first)
                              .arg(e.type())
                              .arg(e.channel())
                              .arg(e.pitch())
                              .arg(e.velo());
        }
        return result;
    };
    const auto midiBefore = events();
    const int oldPitch = target->pitch(), oldTpc1 = target->tpc1(), oldTpc2 = target->tpc2(),
              newTpc = pitch2tpc(70, score->staff(0)->key(Fraction::fromTicks(2400)), Prefer::NEAREST);
    target->setPitch(70);
    target->setTpc1(newTpc);
    target->setTpc2(newTpc);
    const auto midiExpected = events();
    target->setPitch(oldPitch);
    target->setTpc1(oldTpc1);
    target->setTpc2(oldTpc2);
    const auto left = QVariantMap{{"start", 0}, {"end", 1920}, {"firstTrack", 0}, {"endTrack", 1}},
               right = QVariantMap{{"start", 3840}, {"end", 7680}, {"firstTrack", 0}, {"endTrack", 1}};
    const auto beforeLeft = rollNotationIdentity(score, left),
               beforeRight = rollNotationIdentity(score, right);
    score->select(score->findCR(Fraction::fromTicks(2400), 0));
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    editor->findChild<QTabBar *>("workflowExpressionTabs")->setCurrentIndex(0);
    auto bar = editor->findChild<RollWritingBar *>("rollWritingBar");
    QVERIFY(bar);
    struct Restore
    {
        ~Restore() { rollWritingAction()->setChecked(false); }
    } restore;
    rollWritingAction()->setChecked(true);
    QTRY_VERIFY(bar->state()["ready"].toBool());
    const auto scope = bar->state()["scope"].toMap();
    QCOMPARE(scope["start"].toInt(), 1920);
    QCOMPARE(scope["end"].toInt(), 3840);
    const auto rows = bar->state()["notes"].toList();
    QCOMPARE(rows.size(), 4);
    QVERIFY(rows.front().toMap()["boundaryStart"].toBool());
    QVERIFY(rows.back().toMap()["boundaryEnd"].toBool());
    auto canvas = editor->findChild<QWidget *>("performanceNoteCanvas");
    auto track = bar->findChild<QComboBox *>("rollWriteTrack");
    track->setCurrentIndex(track->findData(0));
    editor->fit(false);
    QTest::qWait(80);
    auto pitchScroll = canvas->findChild<QScrollBar *>();
    pitchScroll->setValue((127 - 74) * 100);
    QTest::qWait(30);
    const double row = canvas->height() * 100. / pitchScroll->pageStep(),
                 top = 127 - pitchScroll->value() / 100., x = bar->state()["fromX"].toDouble(),
                 step = bar->state()["quarterPixels"].toDouble();
    const QPoint start(qRound(x + 1.3 * step), qRound((top - 69 + .5) * row)),
        end(start.x(), qRound((top - 70 + .5) * row));
    qInfo() << "Boundary voice drag" << canvas->size() << start << end << top << row << x << step;
    QVERIFY(canvas->rect().contains(start) && canvas->rect().contains(end));
    const int undo = score->undoStack()->getCurIdx();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(canvas, end, 30);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    QTRY_VERIFY_WITH_TIMEOUT(bar->state()["ready"].toBool(), 10000);
    QCOMPARE(noteAt(2400)->pitch(), 70);
    QCOMPARE(noteAt(2400)->veloOffset(), 91);
    QCOMPARE(noteAt(2400)->veloType(), Note::ValueType::USER_VAL);
    QCOMPARE(score->undoStack()->getCurIdx(), undo + 1);
    QCOMPARE(rollNotationIdentity(score, left), beforeLeft);
    QCOMPARE(rollNotationIdentity(score, right), beforeRight);
    QVERIFY(noteAt(1440)->tieFor());
    QVERIFY(noteAt(1920)->tieBack());
    QVERIFY(noteAt(3360)->tieFor());
    QVERIFY(noteAt(3840)->tieBack());
    QCOMPARE(events(), midiExpected);
    QFileInfo saved(_directory.filePath("roll-through-ties-applied-103.mscz"));
    QVERIFY(score->saveCompressedFile(saved, false, false));
    std::unique_ptr<MasterScore> reopened(main->readScore(saved.absoluteFilePath()));
    QVERIFY(reopened);
    QVERIFY(toChord(reopened->findCR(Fraction::fromTicks(1920), 0))->notes().front()->tieBack());
    QVERIFY(toChord(reopened->findCR(Fraction::fromTicks(3360), 0))->notes().front()->tieFor());
    getAction("undo")->trigger();
    QTRY_VERIFY(bar->state()["ready"].toBool());
    QCOMPARE(noteAt(2400)->pitch(), 69);
    QCOMPARE(events(), midiBefore);
    getAction("redo")->trigger();
    QTRY_VERIFY(bar->state()["ready"].toBool());
    QCOMPARE(events(), midiExpected);
}
