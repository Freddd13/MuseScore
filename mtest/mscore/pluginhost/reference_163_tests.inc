// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mscore/workflow/regionalcontrols.h"
static MasterScore* performanceScore123(const QString&, int);
static QToolButton* referenceButton163(ScoreView* view)
{
    for (auto button : view->findChildren<QToolButton*>())
        if (button->objectName().startsWith("referenceScoreClip/") && button->isVisible())
            return button;
    return nullptr;
}
static QVariantList referenceRows163()
{
    return {QVariantMap{{"id", "clip-a"},
                        {"name", QString::fromUtf8("主旋律")},
                        {"start", 480},
                        {"end", 2880},
                        {"firstTrack", 0},
                        {"endTrack", 8},
                        {"from", 1000.},
                        {"until", 3500.},
                        {"active", true}},
            QVariantMap{{"id", "clip-b"},
                        {"name", QString::fromUtf8("另一段")},
                        {"start", 480},
                        {"end", 2880},
                        {"firstTrack", 0},
                        {"endTrack", 8},
                        {"from", 4000.},
                        {"until", 6500.},
                        {"active", false}}};
}
void TestPluginHost::referenceControls163()
{
    releaseHostPanels();
    auto main = Ms::mscore;
    auto score = performanceScore123(_settings.filePath("reference-163.mscx"), 4);
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1280, 960);
    main->show();
    main->showPerformanceEditor(false);
    auto project = referenceProject();
    QVERIFY(project->property("available").toBool());
    QVariantMap result;
    const QVariantList tracks{QVariantMap{{"id", "track-a"},
                                          {"name", QString::fromUtf8("原曲")},
                                          {"associations", referenceRows163()}}};
    QVERIFY(QMetaObject::invokeMethod(project, "setTracks", Q_RETURN_ARG(QVariantMap, result),
                                      Q_ARG(QVariantList, tracks)));
    QVERIFY(result.value("ok").toBool());
    workflowContext()->setReferenceScore(score);
    project->setProperty("scoreControls", true);
    project->setProperty("scoreRangeDisplay", "always");
    const auto cleanup = qScopeGuard([&] {
        QMetaObject::invokeMethod(project, "stop");
        project->setProperty("scoreControls", false);
        project->setProperty("scoreRangeDisplay", "hover");
    });
    auto view = main->currentScoreView();
    QTRY_VERIFY(referenceButton163(view));
    auto button = referenceButton163(view);
    QCOMPARE(button->popupMode(), QToolButton::MenuButtonPopup);
    QCOMPARE(button->property("referenceClips").toList().size(), 2);
    const auto bounds = button->property("referenceActualRange").toList();
    QVERIFY(!bounds.isEmpty());
    const auto before = button->pos();
    const auto undo = score->undoStack()->getCurIdx();
    QTest::mousePress(button, Qt::LeftButton, Qt::NoModifier, QPoint(10, 8));
    const QPoint target(90, 45);
    QMouseEvent move(QEvent::MouseMove, target, button->mapToGlobal(target), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(button, &move);
    QTest::mouseRelease(button, Qt::LeftButton, Qt::NoModifier, target);
    QTRY_COMPARE(score->undoStack()->getCurIdx(), undo + 1);
    QTRY_VERIFY(button->pos() != before);
    QCOMPARE(button->property("referenceActualRange").toList(), bounds);
    QVERIFY(!QWidget::mouseGrabber());
    const auto placed = button->pos();
    getAction("undo")->trigger();
    QTRY_COMPARE(button->pos(), before);
    getAction("redo")->trigger();
    QTRY_COMPARE(button->pos(), placed);
    auto menu = button->menu();
    QVERIFY(menu);
    QTimer::singleShot(60, [menu] {
        for (auto action : menu->actions())
            if (action->data().toString() == "track-a/clip-b") {
                action->trigger();
                break;
            }
        menu->close();
    });
    QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier,
                      QPoint(button->width() - 5, button->height() / 2));
    QTRY_COMPARE(button->property("referenceSelectedClip").toString(), QString("track-a/clip-b"));
    QSignalSpy requested(project, SIGNAL(associationRequested(QString, QString)));
    QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, QPoint(10, 8));
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.front()[1].toString(), QString("clip-b"));
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    QVERIFY(main->grab().save(out.filePath("reference-range-163.png")));
    QTimer::singleShot(60, [menu] {
        for (auto action : menu->actions())
            if (action->objectName() == "referenceHideControl") {
                action->trigger();
                break;
            }
        menu->close();
    });
    QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier,
                      QPoint(button->width() - 5, button->height() / 2));
    QTRY_VERIFY(!button->isVisible());
    getAction("undo")->trigger();
    QTRY_VERIFY(button->isVisible());
    const auto file = out.filePath("reference-control-163.mscz");
    QFileInfo info(file);
    QVERIFY(score->saveCompressedFile(info, false, false));
    std::unique_ptr<MasterScore> reopened(main->readScore(file));
    QVERIFY(reopened);
    QCOMPARE(reopened->metaTag("kumo.referenceControls"), score->metaTag("kumo.referenceControls"));
    PluginAPI::Score wrapped(score);
    PluginAPI::ScoreObserver observer;
    observer.setScore(&wrapped);
    QVERIFY(observer.setWorkflowData("referenceControls", {{"schema", 1}}).value("ok").toBool());
    QTRY_COMPARE(button->pos(), before);
    getAction("undo")->trigger();
    QTRY_COMPARE(button->pos(), placed);
    project->setProperty("scoreRangeDisplay", "playing");
    QTRY_VERIFY(!view->findChild<QWidget*>("referenceScoreRanges")->isVisible());
    project->setProperty("scoreRangeDisplay", "always");
    QTRY_VERIFY(view->findChild<QWidget*>("referenceScoreRanges")->isVisible());
    project->setProperty("scoreControls", false);
    QTRY_VERIFY(!button->isVisible());
    project->setProperty("scoreControls", true);
    QTRY_VERIFY(button->isVisible());
    auto chord = toChord(score->findCR(Fraction::fromTicks(960), 0));
    QVERIFY(chord);
    auto note = chord->upNote();
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
                      view->toPhysical(note->canvasPos()).toPoint());
    const auto selected = score->selection().noteList();
    QVERIFY(std::find(selected.begin(), selected.end(), note) != selected.end());
}
void TestPluginHost::referenceControlsPlayback163()
{
    auto main = Ms::mscore;
    auto score = performanceScore123(_settings.filePath("reference-play-163.mscx"), 4);
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->show();
    const auto path = spectrumAudio078(_settings.filePath("reference-play-163.wav"));
    QVERIFY(!path.isEmpty());
    auto project = referenceProject();
    QVariantMap result;
    const QVariantList tracks{QVariantMap{{"id", "track-a"},
                                          {"name", "Audio"},
                                          {"path", path},
                                          {"associations", referenceRows163()}}};
    QVERIFY(QMetaObject::invokeMethod(project, "setTracks", Q_RETURN_ARG(QVariantMap, result),
                                      Q_ARG(QVariantList, tracks)));
    workflowContext()->setReferenceScore(score);
    project->setProperty("scoreControls", true);
    project->setProperty("scoreRangeDisplay", "playing");
    const auto cleanup = qScopeGuard([&] {
        QMetaObject::invokeMethod(project, "stop");
        project->setProperty("scoreControls", false);
    });
    auto view = main->currentScoreView();
    QTRY_VERIFY(referenceButton163(view));
    auto button = referenceButton163(view);
    const QVariantMap piece{{"from", 1000.}, {"until", 3500.}, {"at", 0.}, {"duration", 2500.}};
    const QVariantMap clip{{"id", "track-a"}, {"path", path}, {"pieces", QVariantList{piece}}};
    const QVariantMap plan{{"owner", "check-163"}, {"clips", QVariantList{clip}}, {"volume", 0.}};
    for (int pass = 0; pass < 3; ++pass) {
        QVERIFY(QMetaObject::invokeMethod(project, "start", Q_RETURN_ARG(QVariantMap, result),
                                          Q_ARG(QVariantMap, plan)));
        QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
        project->setProperty("activeAssociation", "clip-b");
        QTRY_COMPARE(button->text(), QString::fromUtf8("停止"));
        QTRY_VERIFY(view->findChild<QWidget*>("referenceScoreRanges")->isVisible());
        QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, QPoint(10, 8));
        QTRY_VERIFY(!project->property("playing").toBool());
        QVERIFY(project->property("activeAssociation").toString().isEmpty());
        QVERIFY(!QWidget::mouseGrabber());
    }
}
void TestPluginHost::referenceControlsRange163()
{
    auto main = Ms::mscore;
    auto score = performanceScore123(_settings.filePath("reference-lines-163.mscx"), 4);
    QVERIFY(score);
    score->startCmd();
    auto line = new LayoutBreak(score);
    line->setLayoutBreakType(LayoutBreak::LINE);
    line->setParent(score->firstMeasure()->nextMeasure());
    score->undoAddElement(line);
    score->endCmd();
    score->doLayout();
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1280, 960);
    main->show();
    auto project = referenceProject();
    auto rows = referenceRows163();
    auto row = rows.first().toMap();
    row["end"] = 5760;
    QVariantMap result;
    const QVariantList tracks{
        QVariantMap{{"id", "track-a"}, {"name", "Audio"}, {"associations", QVariantList{row}}}};
    QVERIFY(QMetaObject::invokeMethod(project, "setTracks", Q_RETURN_ARG(QVariantMap, result),
                                      Q_ARG(QVariantList, tracks)));
    workflowContext()->setReferenceScore(score);
    project->setProperty("scoreControls", true);
    project->setProperty("scoreRangeDisplay", "always");
    const auto cleanup = qScopeGuard([&] { project->setProperty("scoreControls", false); });
    auto view = main->currentScoreView();
    QTRY_VERIFY(referenceButton163(view));
    auto button = referenceButton163(view);
    QTRY_VERIFY(button->property("referenceActualRange").toList().size() >= 2);
    const auto spans = button->property("referenceActualRange").toList();
    const auto start = score->tick2segment(Fraction::fromTicks(480), false, SegmentType::ChordRest);
    QVERIFY(start);
    QVERIFY(qAbs(spans.front().toRectF().left() - view->toPhysical(start->canvasPos()).x()) < 1.);
    auto last = score->firstMeasure()->nextMeasure()->nextMeasure();
    const auto expected = view->toPhysical(last->canvasPos() + QPointF(last->width(), 0)).x();
    QVERIFY(qAbs(spans.back().toRectF().right() - expected) < 1.);
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    QVERIFY(main->grab().save(out.filePath("reference-cross-line-163.png")));
    // Moving a writing selector uses the same per-score placement and undo operations.
    const auto group =
        RegionalVariants::create(score, 0, 1920, {0, 1, 2, 3}, QString::fromUtf8("第一段"));
    QVERIFY(!group.contains("error"));
    setRegionalControlsVisible(true);
    view->update();
    QToolButton* writing = nullptr;
    QTRY_VERIFY(([&] {
        for (auto b : view->findChildren<QToolButton*>())
            if (b->objectName().startsWith("regionalScoreSwitch/") && b->isVisible()) {
                writing = b;
                return true;
            }
        return false;
    })());
    const auto automatic = writing->pos();
    QTest::mousePress(writing, Qt::LeftButton, Qt::NoModifier, QPoint(10, 8));
    QMouseEvent move(QEvent::MouseMove, QPoint(70, 25), writing->mapToGlobal(QPoint(70, 25)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(writing, &move);
    QTest::mouseRelease(writing, Qt::LeftButton, Qt::NoModifier, QPoint(70, 25));
    QTRY_VERIFY(writing->pos() != automatic);
    getAction("undo")->trigger();
    QTRY_COMPARE(writing->pos(), automatic);
    setRegionalControlsVisible(false);
}
void TestPluginHost::referenceSettings163()
{
    releaseHostPanels();
    auto main = Ms::mscore;
    auto score = performanceScore123(_settings.filePath("reference-settings-163.mscx"), 4);
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->show();
    auto project = referenceProject();
    project->setProperty("scoreRangeDisplay", "hover");
    showReferencePanel(true);
    QPointer<QmlPlugin> panel;
    for (auto window : QGuiApplication::allWindows())
        if (auto view = qobject_cast<QQuickView*>(window))
            if (auto root = qobject_cast<QmlPlugin*>(view->rootObject()))
                if (root->menuPath() == "Plugins.Reference Player")
                    panel = root;
    QVERIFY(panel);
    const auto cleanup = qScopeGuard([&] {
        releaseHostPanels();
        project->setProperty("scoreRangeDisplay", "hover");
        project->setProperty("scoreControls", false);
    });
    QVERIFY(QMetaObject::invokeMethod(panel, "setPanelFloating", Q_ARG(bool, true)));
    QVERIFY(QMetaObject::invokeMethod(panel, "resizePanel", Q_ARG(int, 1050), Q_ARG(int, 880)));
    auto select = panel->findChild<QQuickItem*>("referenceScoreRangeDisplay");
    auto restore = panel->findChild<QQuickItem*>("referenceRestoreScoreControls");
    QVERIFY(select && restore);
    auto click = [&](QQuickItem* item) {
        auto scroll = panel->findChild<QQuickItem*>("referencePanelScroll");
        QVERIFY(scroll);
        auto value = scroll->property("contentItem");
        auto object = value.canConvert<QJSValue>() ? value.value<QJSValue>().toQObject()
                                                   : value.value<QObject*>();
        auto flick = qobject_cast<QQuickItem*>(object);
        QVERIFY(flick);
        const auto point = item->mapToItem(flick, QPointF());
        flick->setProperty("contentY", qBound(0., point.y() - 50.,
                                              qMax(0., flick->property("contentHeight").toDouble() -
                                                           flick->height())));
        QTest::qWait(80);
        QTest::mouseClick(panel->window(), Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(15, item->height() / 2)).toPoint());
    };
    click(select);
    QTest::qWait(100);
    QTest::mouseClick(panel->window(), Qt::LeftButton, Qt::NoModifier,
                      select->mapToScene(QPointF(15, select->height() + 42)).toPoint());
    QTRY_COMPARE(project->property("scoreRangeDisplay").toString(), QString("always"));
    PluginAPI::Score wrapped(score);
    PluginAPI::ScoreObserver observer;
    observer.setScore(&wrapped);
    const QVariantMap data{
        {"schema", 1},
        {"controls", QVariantMap{{"track/clip", QVariantMap{{"hidden", true}, {"x", 2.}}}}}};
    QVERIFY(observer.setWorkflowData("referenceControls", data).value("ok").toBool());
    click(restore);
    QTRY_VERIFY(observer.workflowData("referenceControls").value("controls").toMap().isEmpty());
    getAction("undo")->trigger();
    QCOMPARE(observer.workflowData("referenceControls"), data);
    const auto out = qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path());
    QVERIFY(panel->window()->grabWindow().save(out + "/reference-control-settings-163.png"));
}
