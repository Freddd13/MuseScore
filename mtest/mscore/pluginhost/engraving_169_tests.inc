// SPDX-License-Identifier: GPL-2.0-or-later
#include "libmscore/dynamic.h"
#include "libmscore/spacer.h"
#include <QCheckBox>

void TestPluginHost::engravingHandles169()
{
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1320, 900);
    main->show();
    auto view = main->currentScoreView();
    auto engrave = main->findChild<QAction*>("kumo-workspace-engrave");
    auto arrange = main->findChild<QAction*>("kumo-workspace-arrange");
    QVERIFY(engrave && arrange && view);
    arrange->trigger();
    main->switchLayoutMode(LayoutMode::LINE);
    engrave->trigger();
    QVERIFY(score->pageMode());
    const auto restore = qScopeGuard([&] { arrange->trigger(); });
    auto dock = main->findChild<QDockWidget*>("kumoEngravingTools");
    QVERIFY(dock && dock->isVisible());
    auto mode = dock->findChild<QComboBox*>("kumoEngravingTool");
    auto spacing = dock->findChild<QComboBox*>("kumoEngravingSpacingTarget");
    auto handles = dock->findChild<QCheckBox*>("kumoEngravingHandles");
    QVERIFY(mode && spacing && handles && handles->isChecked());
    auto measure = score->firstMeasure();
    auto segment = measure->first(SegmentType::ChordRest)->next(SegmentType::ChordRest);
    auto note = toChord(segment->element(0))->upNote();
    score->select(note);
    main->endCmd();
    view->setFocus();
    view->setLogicalZoom(ZoomIndex::ZOOM_PAGE_WIDTH, view->calculateLogicalZoomLevel(ZoomIndex::ZOOM_PAGE_WIDTH));
    view->adjustCanvasPosition(measure, false);
    mode->setCurrentIndex(1);
    QApplication::processEvents();
    view->grab(); // Paint the visible handle before sending the same mouse events a user sends.
    const auto undo = score->undoStack();
    const auto originalLeading = segment->extraLeadingSpace().val();
    const auto originalPitch = note->pitch();
    const auto baseline = undo->getCurIdx();
    auto rowBox = [&] { return view->matrix().mapRect(measure->system()->canvasBoundingRect()); };
    auto top = [&] { return qRound(qMax(13.0, rowBox().top() - 13.0)); };
    auto move = [&](QPoint to) {
        QMouseEvent event(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &event);
    };
    auto release = [&](QPoint to) { QTest::mouseRelease(view, Qt::LeftButton, {}, to); };
    auto start = [&](QPoint from, QPoint delta) {
        QVERIFY(view->rect().contains(from));
        QTest::mousePress(view, Qt::LeftButton, {}, from);
        move(from + delta);
    };
    const auto dx = QPoint(22, 0);
    const QPoint beat(qRound(view->matrix().map(segment->canvasPos()).x()), top());
    start(beat, dx);
    QCOMPARE(undo->getCurIdx(), baseline);
    QCOMPARE(segment->extraLeadingSpace().val(), originalLeading);
    const auto out = qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path());
    QVERIFY(view->grab().save(QDir(out).filePath("engraving-beat-drag-169.png")));
    release(beat + dx);
    QCOMPARE(undo->getCurIdx(), baseline + 1);
    QVERIFY(segment->extraLeadingSpace().val() > originalLeading);
    QCOMPARE(note->pitch(), originalPitch);

    // Both publication routes should lay out the changed time column the same way.
    const auto output = QDir(out);
    QVERIFY(main->savePdf(score, output.filePath("engraving-publication-169.pdf")));
    ScoreVideoDocument video;
    video.setScore(score);
    video.prepare({{"staves", QVariantList{0, 1}}, {"audioKind", "none"}});
    QTRY_VERIFY_WITH_TIMEOUT(video.ready(), 6000);
    const auto videoMeasure = video.preparedScore()->firstMeasure();
    const auto videoSegment = videoMeasure->first(SegmentType::ChordRest)->next(SegmentType::ChordRest);
    QCOMPARE(videoMeasure->system()->lastMeasure()->tick(), measure->system()->lastMeasure()->tick());
    QVERIFY(qAbs((videoSegment->pagePos().x() - videoMeasure->pagePos().x())
                - (segment->pagePos().x() - measure->pagePos().x())) < 0.1);
    QVERIFY(QImage(video.snapshot().pictures.front().file).save(output.filePath("engraving-publication-169.png")));

    getAction("undo")->trigger();
    QCOMPARE(segment->extraLeadingSpace().val(), originalLeading);

    score->select(note);
    main->endCmd();
    spacing->setCurrentIndex(1);
    QApplication::processEvents();
    const auto ratio = measure->userStretch();
    const QPoint bar(qRound(view->matrix().mapRect(measure->canvasBoundingRect()).right()), top());
    start(bar, dx);
    QTest::keyClick(view, Qt::Key_Escape);
    release(bar + dx);
    QCOMPARE(undo->getCurIdx(), baseline);
    QCOMPARE(measure->userStretch(), ratio);

    score->select(note);
    main->endCmd();
    mode->setCurrentIndex(2);
    QApplication::processEvents();
    QVERIFY(!measure->vspacerDown(0));
    const auto system = measure->system();
    const qreal bottom = system->staffCanvasYpage(0) + system->staff(0)->bbox().height();
    const QPoint staff(qRound(qMin(rowBox().right() - 14., view->width() - 14.)),
                       qRound(view->matrix().map(QPointF(0, bottom)).y() + 12));
    const auto dy = QPoint(0, 20);
    start(staff, dy);
    release(staff + dy);
    QCOMPARE(undo->getCurIdx(), baseline + 1);
    QVERIFY(measure->vspacerDown(0) && measure->vspacerDown(0)->gap() > 0);
    getAction("undo")->trigger();
    QVERIFY(!measure->vspacerDown(0));

    auto dynamic = new Dynamic(score);
    dynamic->setDynamicType(QStringLiteral("mf"));
    dynamic->setTrack(0);
    dynamic->setParent(segment);
    score->startCmd();
    score->undoAddElement(dynamic);
    score->endCmd();
    score->select(dynamic);
    main->endCmd();
    mode->setCurrentIndex(0);
    QApplication::processEvents();
    const auto initialOffset = dynamic->offset();
    const auto symbolUndo = undo->getCurIdx();
    const QPoint symbol = (view->matrix().mapRect(dynamic->canvasBoundingRect()).topRight() + QPointF(8, -7)).toPoint();
    start(symbol, dx + dy);
    QCOMPARE(dynamic->offset(), initialOffset);
    release(symbol + dx + dy);
    QCOMPARE(undo->getCurIdx(), symbolUndo + 1);
    QVERIFY(dynamic->offset().x() > initialOffset.x() && dynamic->offset().y() > initialOffset.y());
    getAction("undo")->trigger();
    QCOMPARE(dynamic->offset(), initialOffset);
    arrange->trigger();
    QCOMPARE(score->layoutMode(), LayoutMode::LINE);
}
