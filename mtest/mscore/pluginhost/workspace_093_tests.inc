// Real side docks, central score and palette input through repeated work views.
#include "mscore/palette/palettewidget.h"
#include "mscore/seq.h"
#include "mscore/palette/paletteworkspace.h"
#include "mscore/palette/palettemodel.h"
#include "mscore/palette/palettetree.h"

void TestPluginHost::workspaceSideWidthsRoundTrip()
{
    auto main = Ms::mscore;
    guiScaling = 1.0;
    MuseScore::updateUiStyleAndTheme();
    if (!WorkspacesManager::currentWorkspace()) {
        WorkspacesManager::initCurrentWorkspace();
    }
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->showNormal(); main->resize(1360, 920); main->show();
    if (!main->getPaletteWidget() || main->getPaletteWidget()->isHidden())
        getAction("toggle-palette")->trigger();
    if (!main->findChild<QDockWidget*>("inspector") || main->findChild<QDockWidget*>("inspector")->isHidden())
        getAction("inspector")->trigger();
    main->showPerformanceEditor(true);
    auto palette = main->getPaletteWidget();
    auto inspector = main->findChild<QDockWidget*>("inspector");
    auto editor = main->findChild<QDockWidget*>("performanceEditorDock");
    QVERIFY(palette && inspector && editor);
    QTest::qWait(150);
    main->resizeDocks({palette, inspector}, {240, 290}, Qt::Horizontal);
    main->resizeDocks({editor}, {500}, Qt::Vertical);
    QTest::qWait(150);
    const auto before = QList<int>{palette->width(), inspector->width(), main->centralWidget()->width()};
    const auto output = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    auto captureWidth = [&]() {
        auto image = main->grab();
        if (auto quick = const_cast<QQuickView*>(palette->view())) {
            QPainter painter(&image); painter.drawImage(palette->widget()->mapTo(main, QPoint()), quick->grabWindow());
        }
        return image;
    };
    QVERIFY(captureWidth().save(output.filePath("workspace-width-before-093.png")));
    qInfo() << "before side widths / score" << before;
    for (int round = 0; round < 3; ++round) {
        for (const char* mode : {"daw", "engrave", "post", "arrange"}) {
            main->findChild<QAction*>(QString("kumo-workspace-") + mode)->trigger();
            QTest::qWait(120);
            if (QString(mode) == "daw") {
                palette->show(); inspector->show();
                QTest::qWait(100);
            }
            qInfo() << "mode" << mode << "round" << round << "palette / inspector / score"
                    << palette->width() << inspector->width() << main->centralWidget()->width();
            if (QString(mode) != "daw") {
                QVERIFY(qAbs(palette->width() - before[0]) < 8);
                QVERIFY(qAbs(inspector->width() - before[1]) < 8);
            }
        }
        QVERIFY(captureWidth().save(output.filePath("workspace-width-return-093.png")));
        QVERIFY2(qAbs(palette->width() - before[0]) < 8, "返回编曲后保留符号面板宽度");
        QVERIFY2(qAbs(inspector->width() - before[1]) < 8, "返回编曲后保留检视器宽度");
        QVERIFY2(qAbs(main->centralWidget()->width() - before[2]) < 12, "返回编曲后保留谱面宽度");
    }
    // Existing layouts from before sideWidths keep their own dimensions.
    main->findChild<QAction*>("kumo-workspace-engrave")->trigger(); QTest::qWait(120);
    main->resizeDocks({palette, inspector}, {310, 260}, Qt::Horizontal); QTest::qWait(100);
    const auto engraving = QList<int>{palette->width(), inspector->width()};
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger(); QTest::qWait(100);
    QSettings().remove("personalWorkspaceViews/engrave/sideWidths");
    QSettings().remove("personalWorkspaceViews/engrave/windowWidth");
    main->findChild<QAction*>("kumo-workspace-engrave")->trigger(); QTest::qWait(100);
    QVERIFY(qAbs(palette->width() - engraving[0]) < 8);
    QVERIFY(qAbs(inspector->width() - engraving[1]) < 8);
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger(); QTest::qWait(100);
    // Rapid view changes must not save an unfinished intermediate dock layout.
    for (const char* mode : {"daw", "engrave", "post", "arrange"})
        main->findChild<QAction*>(QString("kumo-workspace-") + mode)->trigger();
    QTest::qWait(180);
    QCOMPARE(palette->width(), before[0]);
    QCOMPARE(inspector->width(), before[1]);
    QVERIFY(qAbs(main->centralWidget()->width() - before[2]) < 12);
}

class PalettePlaybackDriver093 : public Driver {
    Transport _state = Transport::STOP;
    float _buffer[8192] = {};
public:
    explicit PalettePlaybackDriver093(Seq* sequence) : Driver(sequence) {}
    bool init(bool = false) override { return true; }
    bool start(bool = false) override { return true; }
    bool stop() override { stopTransport(); return true; }
    void stopTransport() override { _state = Transport::STOP; seq->process(0, _buffer); }
    void startTransport() override { _state = Transport::PLAY; seq->process(0, _buffer); }
    Transport getState() override { return _state; }
    int sampleRate() const override { return 44100; }
    void pulse() { seq->process(2048, _buffer); }
};

void TestPluginHost::paletteRemainsInteractiveAfterPlayback()
{
    auto main = Ms::mscore;
    guiScaling = 1.0;
    MuseScore::updateUiStyleAndTheme();
    if (!WorkspacesManager::currentWorkspace()) {
        WorkspacesManager::initCurrentWorkspace();
    }
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
    synth->setSampleRate(44100); synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti, synth.get());
    QScopedValueRollback<bool> sequencing(noSeq, false);
    QVERIFY(!Ms::seq);
    auto sequence = new Seq; Ms::seq = sequence;
    std::unique_ptr<Seq, std::function<void(Seq*)>> guard(sequence, [](Seq* s) {
        s->stopWait(); s->synchronizeStoppedGui(); s->waitForStoppedRendering();
        s->setScoreView(nullptr); delete s; Ms::seq = nullptr;
    });
    auto driver = new PalettePlaybackDriver093(sequence);
    sequence->setDriver(driver); sequence->setMasterSynthesizer(synth.get());
    QVERIFY(sequence->init());
    QObject::connect(sequence, SIGNAL(started()), main, SLOT(seqStarted()));
    QObject::connect(sequence, SIGNAL(stopped()), main, SLOT(seqStopped()));
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->resize(1360, 920); main->show();
    if (!main->getPaletteWidget() || main->getPaletteWidget()->isHidden())
        getAction("toggle-palette")->trigger();
    auto palette = main->getPaletteWidget(); QVERIFY(palette);
    main->resizeDocks({palette}, {240}, Qt::Horizontal);
    QTest::qWait(150);
    auto window = const_cast<QQuickView*>(palette->view());
    QVERIFY(window && window->rootObject());
    auto header = [&]() -> QQuickItem* {
        QList<QQuickItem*> pending{window->rootObject()};
        while (!pending.isEmpty()) {
            auto item = pending.takeFirst(); pending.append(item->childItems());
            if (QByteArray(item->metaObject()->className()).startsWith("TreePaletteHeader") &&
                item->isVisible() && item->width() > 40 &&
                window->rootObject()->contains(item->mapToItem(window->rootObject(), QPointF(10, item->height()/2))))
                return item;
        }
        return nullptr;
    };
    auto clickHeader = [&]() {
        auto item = header();
        if (!item) return false;
        const bool before = item->property("expanded").toBool();
        const auto point = item->mapToScene(QPointF(10, item->height()/2)).toPoint();
        QTest::mouseMove(window, point); QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(80);
        return item->property("expanded").toBool() != before;
    };
    QVERIFY2(clickHeader(), "播放前可展开或收起符号组");
    QSignalSpy messages(sequence, SIGNAL(toGui(int,int)));
    QSignalSpy stops(sequence, SIGNAL(stopped()));
    for (int round = 0; round < 4; ++round) {
        messages.clear(); stops.clear();
        struct PressedSurface {
            QPointer<QWidget> widget;
            QPoint point;
            void release() { if (widget) { QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, point); widget.clear(); } }
            ~PressedSurface() { release(); }
        } pressed;
        if (round == 1) {
            main->showPerformanceEditor(true);
            auto editor = main->performanceEditor();
            editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
            main->findChild<QDockWidget*>("performanceEditorDock")->raise();
            QTest::qWait(150);
            pressed.widget = editor->findChild<QWidget*>("performanceRuler");
            QVERIFY(pressed.widget && pressed.widget->isVisible());
            pressed.point = QPoint(110, pressed.widget->height()/2);
            QTest::mousePress(pressed.widget, Qt::LeftButton, Qt::NoModifier, pressed.point);
            QCOMPARE(QWidget::mouseGrabber(), pressed.widget.data());
        }
        if (round == 3) {
            main->showPerformanceEditor(true);
            auto editor = main->performanceEditor();
            editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
            score->select(score->firstMeasure()->first()->element(0));
            rollWritingAction()->setChecked(true);
            auto writing = editor->findChild<RollWritingBar*>("rollWritingBar");
            QVERIFY(writing);
            QTRY_VERIFY(writing->state()["ready"].toBool());
            editor->fit(false); QTest::qWait(80);
            pressed.widget = editor->findChild<QWidget*>("performanceNoteCanvas");
            QVERIFY(pressed.widget && pressed.widget->isVisible());
            pressed.point = QPoint(qRound(writing->state()["fromX"].toDouble() +
                                          writing->state()["quarterPixels"].toDouble() * .5),
                                   pressed.widget->height()/2);
            QTest::mousePress(pressed.widget, Qt::LeftButton, Qt::NoModifier, pressed.point);
            QCOMPARE(QWidget::mouseGrabber(), pressed.widget.data());
        }
        score->setPlayPos(Fraction());
        getAction("play")->trigger();
        QTRY_VERIFY(sequence->isPlaying());
        for (int step=0; step<4; ++step) { driver->pulse(); QTest::qWait(5); }
        QTRY_VERIFY2(QWidget::mouseGrabber() == nullptr, "播放开始后释放编辑器占用的鼠标，符号面板和原曲参考继续可用");
        pressed.release();
        if (round == 3) rollWritingAction()->setChecked(false);
        qInfo() << "playing" << main->state() << palette->isEnabled() << window->rootObject()->isEnabled();
        QVERIFY(!palette->isEnabled());
        if (round == 0) main->showPerformanceEditor(true);
        if (round == 1) { showExpressionPanel(); showReferencePanel(true); QTest::qWait(150); }
        if (round == 2) main->findChild<QAction*>("kumo-workspace-daw")->trigger();
        if (round == 0) getAction("play")->trigger();
        else if (round == 1) sequence->stopWait();
        else for (int step=0; step<500 && sequence->isPlaying(); ++step) driver->pulse();
        QTRY_VERIFY(!sequence->isPlaying());
        QCoreApplication::processEvents();
        qInfo() << "after stop round" << round << main->state() << main->currentScoreView()->mscoreState()
                << palette->isEnabled() << window->rootObject()->isEnabled() << "score" << sequence->score()
                << "messages" << messages << "stops" << stops.count() << "blocked" << sequence->signalsBlocked();
        QTRY_VERIFY(palette->isEnabled());
        QTRY_VERIFY(window->rootObject()->isEnabled());
        main->findChild<QAction*>("kumo-workspace-daw")->trigger(); QTest::qWait(100);
        main->findChild<QAction*>("kumo-workspace-arrange")->trigger(); QTest::qWait(150);
        qInfo() << "stopped" << main->state() << palette->isEnabled() << window->rootObject()->isEnabled()
                << "grabber" << QWidget::mouseGrabber() << window->mouseGrabberItem()
                << "modal" << QGuiApplication::modalWindow();
        QVERIFY2(clickHeader(), "停播并切回编曲后可继续点击符号组");
        showReferencePanel(true); QTest::qWait(120);
        QmlPlugin* reference = nullptr;
        for (auto candidate : QGuiApplication::allWindows())
            if (auto view = qobject_cast<QQuickView*>(candidate))
                if (auto root = qobject_cast<QmlPlugin*>(view->rootObject()))
                    if (root->menuPath() == "Plugins.Reference Player") reference = root;
        QVERIFY(reference);
        QQuickItem* loop = nullptr;
        for (auto item : reference->findChildren<QQuickItem*>())
            if (item->property("text").toString() == QString::fromUtf8("循环") && item->isVisible()) { loop = item; break; }
        QVERIFY(loop);
        const bool loopBefore = reference->property("looping").toBool();
        const auto point = loop->mapToScene(QPointF(loop->width()/2, loop->height()/2)).toPoint();
        QTest::mouseMove(reference->window(), point);
        QTest::mouseClick(reference->window(), Qt::LeftButton, Qt::NoModifier, point);
        QTRY_COMPARE(reference->property("looping").toBool(), !loopBefore);
    }
    // Apply an actual palette symbol, then undo and redo it on the same score.
    QString symbolName;
    for (const auto& panel : main->getPaletteWorkspace()->userPaletteModel()->paletteTree()->palettes)
        if (panel->type() == PalettePanel::Type::Articulation)
            for (int index = 0; index < panel->ncells(); ++index) {
                auto cell = panel->cell(index);
                if (cell->element && cell->element->isArticulation()) {
                    symbolName = cell->translatedName(); break;
                }
            }
    QVERIFY(!symbolName.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(window->rootObject(), "requestPaletteSearch"));
    QList<QQuickItem*> pending{window->rootObject()};
    QQuickItem* search = nullptr;
    while (!pending.isEmpty()) {
        auto item = pending.takeFirst(); pending.append(item->childItems());
        if (!item->property("placeholderText").toString().isEmpty()) { search = item; break; }
    }
    QVERIFY(search); QVERIFY(search->setProperty("text", symbolName));
    QTest::qWait(150);
    QQuickItem* symbol = nullptr;
    pending = {window->rootObject()};
    while (!pending.isEmpty()) {
        auto item = pending.takeFirst(); pending.append(item->childItems());
        if (item->property("parentModelIndex").isValid() && item->isVisible() &&
            item->property("text").toString() == symbolName) { symbol = item; break; }
    }
    QVERIFY(symbol);
    const auto chord = toChord(score->tick2segment(Fraction::fromTicks(480), false, SegmentType::ChordRest)->element(0));
    auto note = chord->notes().front(); score->select(note);
    QCOMPARE(main->currentScore(), score);
    const auto count = chord->articulations().size();
    QPoint point(-1, -1);
    QVERIFY(QTest::qWaitFor([&] {
        const auto actual = symbol->mapToScene(QPointF(symbol->width()/2, symbol->height()/2)).toPoint();
        const bool stable = actual == point && window->rootObject()->contains(actual);
        point = actual; return stable;
    }, 1500));
    qInfo() << "symbol" << symbolName << point << "window" << window->size()
            << "palette" << palette->geometry() << "selection" << score->selection().elements().size()
            << "score playing" << score->isPlaying() << "state" << main->state();
    QVERIFY(window->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path())).filePath("palette-before-apply-093.png")));
    QVERIFY(main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path())).filePath("palette-layout-before-apply-093.png")));
    QTest::mouseMove(window, point);
    qInfo() << "palette hover" << symbol->property("hovered");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTRY_COMPARE(chord->articulations().size(), count + 1);
    getAction("undo")->trigger(); QTRY_COMPARE(chord->articulations().size(), count);
    getAction("redo")->trigger(); QTRY_COMPARE(chord->articulations().size(), count + 1);
    QmlPlugin* reference = nullptr;
    for (auto candidate : QGuiApplication::allWindows())
        if (auto view = qobject_cast<QQuickView*>(candidate))
            if (auto root = qobject_cast<QmlPlugin*>(view->rootObject()))
                if (root->menuPath() == "Plugins.Reference Player") reference = root;
    QVERIFY(reference);
    showReferencePanel(true); QTest::qWait(120);
    QQuickItem* looping = nullptr;
    for (auto item : reference->findChildren<QQuickItem*>())
        if (item->property("text").toString() == QString::fromUtf8("循环") && item->isVisible()) { looping = item; break; }
    QVERIFY(looping);
    const bool loopBefore = reference->property("looping").toBool();
    auto referenceWindow = reference->window();
    const auto loopPoint = looping->mapToScene(QPointF(looping->width()/2, looping->height()/2)).toPoint();
    QTest::mouseMove(referenceWindow, loopPoint);
    QTest::mouseClick(referenceWindow, Qt::LeftButton, Qt::NoModifier, loopPoint);
    QTRY_COMPARE(reference->property("looping").toBool(), !loopBefore);
    QVERIFY(window->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path())).filePath("palette-symbol-applied-093.png")));
    auto capture = main->grab();
    { QPainter painter(&capture); painter.drawImage(palette->widget()->mapTo(main, QPoint()), window->grabWindow());
      if (auto dock = main->findChild<QDockWidget*>("kumoPanel/ReferencePlayer_MS3"))
          painter.drawImage(dock->widget()->mapTo(main, QPoint()), referenceWindow->grabWindow()); }
    QVERIFY(capture.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path())).filePath("palette-after-playback-093.png")));
    showReferencePanel(false);
}
