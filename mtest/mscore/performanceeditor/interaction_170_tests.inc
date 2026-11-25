// SPDX-License-Identifier: GPL-2.0-or-later
void TestPerformanceEditor::timelineLeadingSpace170()
{
    expressionHost080(_directory.path());
    auto main = Ms::mscore;
    auto score = expressionScore080(main, _directory.filePath("first-note-170.mscx"));
    QVERIFY(score);
    expressionShowMain080(main);
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
    editor->findChild<QComboBox*>("performanceParameter")->setCurrentIndex(0);
    auto ruler = editor->findChild<QWidget*>("performanceRuler");
    QVERIFY(ruler && ruler->isVisible());
    QTest::qWait(100);
    editor->fit();
    const auto first = QPoint(PerformanceViewport::timelineStart, ruler->height() / 2);
    QTest::mouseClick(ruler, Qt::LeftButton, {}, first);
    QCOMPARE(score->playPos().ticks(), 0);
    QWheelEvent zoom(first + QPoint(120, 0), ruler->mapToGlobal(first + QPoint(120, 0)),
                     {}, QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(ruler, &zoom);
    const auto scrollbar = editor->findChildren<QScrollBar*>();
    QScrollBar* horizontal = nullptr;
    for (auto bar : scrollbar)
        if (bar->orientation() == Qt::Horizontal && bar->parentWidget() == editor) horizontal = bar;
    QVERIFY(horizontal);
    horizontal->setValue(480);
    QTest::mouseClick(ruler, Qt::LeftButton, {}, first);
    QCOMPARE(score->playPos().ticks(), horizontal->value());
    horizontal->setValue(0);
    auto canvas = editor->findChild<QWidget*>("performanceParameterCanvas");
    QVERIFY(canvas);
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _directory.path()));
    QVERIFY(editor->grab().save(out.filePath("timeline-first-note-170.png")));
}
