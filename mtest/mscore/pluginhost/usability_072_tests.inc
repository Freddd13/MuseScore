#ifdef Q_OS_WIN
#include <qt_windows.h>
static bool nativeToolFocused(QWidget* widget) {
    const auto expected = reinterpret_cast<HWND>(widget->winId());
    for (auto foreground = GetForegroundWindow(); foreground; foreground = GetWindow(foreground, GW_OWNER))
        if (foreground == expected || IsChild(expected, foreground))
            return true;
    return false;
}
#endif
void TestPluginHost::restoredWorkspaceKeepsPersonalTools() {
    auto main = Ms::mscore;
    auto view = Workspace::findMenuFromString("menu-view");
    QVERIFY(view);
    addAIConnectionAction(view);
    QPointer<QAction> ai = main->findChild<QAction*>("kumo-ai-connection");
    QVERIFY(ai);
    QCOMPARE(ai->parent(), static_cast<QObject*>(main));
    QDir().mkpath(dataPath + "/workspaces/global");
    Workspace::writeGlobalMenuBar(main->menuBar());
    QFile saved(dataPath + "/workspaces/global/menubar.xml");
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QByteArray xml = saved.readAll();
    saved.close();
    xml.replace("<action>kumo-ai-connection</action>", "");
    for (int repeat = 0; repeat < 2; ++repeat) {
        XmlReader reader(xml);
        QVERIFY(reader.readNextStartElement());
        Workspace workspace;
        workspace.read(reader);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(ai);
        main->updateMenus();
        main->updateMenus();
        view = Workspace::findMenuFromString("menu-view");
        QVERIFY(view);
        QCOMPARE(view->actions().count(ai), 1);
        QCOMPARE(main->findChild<QAction*>("kumo-ai-connection"), ai.data());
        for (const auto& identifier : {"menu-kumo-workspace", "menu-kumo-system-lock"}) {
            auto menu = Workspace::findMenuFromString(identifier);
            QVERIFY(menu);
            QVERIFY(!menu->title().startsWith("menu-"));
            QCOMPARE(view->actions().count(menu->menuAction()), 1);
        }
        for (auto action : view->actions())
            QVERIFY2(!action->text().startsWith("menu-kumo-"), qPrintable(action->text()));
    }
    ai->trigger();
    auto dialog = main->findChild<QDialog*>("kumoAIConnection");
    QTRY_VERIFY(dialog && dialog->isVisible());
    auto status = dialog->findChild<QLabel*>("aiConnectionStatus");
    QVERIFY(status && !status->text().isEmpty());
    QVERIFY(dialog->findChild<QPushButton*>("aiConfigureCodex"));
    QVERIFY(dialog->findChild<QPushButton*>("aiCheckConnection"));
    dialog->close();
}
void TestPluginHost::contentWidthControlsWrapWithoutOverlap() {
    QWidget window;
    auto flow = new ControlFlowLayout(&window);
    QList<QToolButton*> controls;
    for (const auto& text : QStringList{"A", "Playback", QString::fromUtf8("谱表与声部"),
                                        "12345678901234567890", "B", "C"}) {
        auto button = new QToolButton(&window);
        button->setText(text);
        flow->addWidget(button);
        controls.append(button);
    }
    for (int width : {280, 500, 1200}) {
        window.resize(width, flow->heightForWidth(width));
        window.show();
        QTest::qWait(30);
        for (int i = 0; i < controls.size(); ++i) {
            const auto bounds = controls[i]->geometry();
            QVERIFY2(window.rect().contains(bounds), qPrintable(controls[i]->text()));
            QVERIFY(bounds.width() <= controls[i]->sizeHint().width() + 1);
            for (int j = i + 1; j < controls.size(); ++j)
                QVERIFY2(!bounds.intersects(controls[j]->geometry()), qPrintable(controls[i]->text()));
        }
    }
}
void TestPluginHost::vstFirstUseControls() {
#ifdef KUMO_VST_HOST
    auto main = Ms::mscore;
    QVERIFY(!main->findChild<QDockWidget*>("kumoVstPanel"));
    QSettings settings;
    const auto previous = settings.value("kumo/vst/catalog");
    settings.remove("kumo/vst/catalog");
    struct RestoreCatalog {
        QVariant value;
        ~RestoreCatalog() {
            QSettings settings;
            if (value.isValid()) settings.setValue("kumo/vst/catalog", value);
            else settings.remove("kumo/vst/catalog");
        }
    } restore{previous};
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    showVstPanel();
    auto dock = main->findChild<QDockWidget*>("kumoVstPanel");
    QVERIFY(dock);
    auto catalog = dock->findChild<QComboBox*>("kumoVstCatalog");
    auto scan = dock->findChild<QPushButton*>("kumoVstScan");
    auto forget = dock->findChild<QPushButton*>("kumoVstForget");
    auto choose = dock->findChild<QPushButton*>("kumoVstChoose");
    auto open = dock->findChild<QPushButton*>("kumoVstOpenSource");
    auto original = dock->findChild<QPushButton*>("kumoVstRemoveSource");
    QVERIFY(catalog && scan && forget && choose && open && original);
    QCOMPARE(catalog->count(), 1);
    QVERIFY(!catalog->currentText().isEmpty());
    QVERIFY(catalog->currentData(Qt::UserRole + 1).toMap().isEmpty());
    QVERIFY(!catalog->isEnabled() && scan->isEnabled());
    QVERIFY(!choose->isEnabled() && !forget->isEnabled());
    dock->setFloating(true);
    for (int width : {280, 600}) {
        dock->resize(width, 680);
        QTest::qWait(80);
        QVERIFY(choose->width() <= choose->sizeHint().width() + 1);
        QVERIFY(original->width() <= original->sizeHint().width() + 1);
        QVERIFY(!QRect(choose->mapTo(dock, QPoint()), choose->size()).intersects(
                      QRect(open->mapTo(dock, QPoint()), open->size())));
    }
    dock->close();
#else
    QSKIP("VST host is disabled in this build");
#endif
}
void TestPluginHost::toolWindowScreenAndFullscreenRoundTrip() {
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1040, 760);
    main->show();
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    auto dock = main->findChild<QDockWidget*>("performanceEditorDock");
    QVERIFY(editor && dock);
    // Existing user workspaces can stack several tools beside the score.
    // Their forms must remain scrollable while the bottom editor expands.
    showRegionalVariantPanel();
    auto variants = main->findChild<QDockWidget*>("kumoPanel/regionalVariantsDock");
    QVERIFY(variants && variants->findChild<QScrollArea*>("regionalScrollArea"));
    variants->setFloating(false);
#ifdef KUMO_VST_HOST
    showVstPanel();
    auto sources = main->findChild<QDockWidget*>("kumoVstPanel");
    QVERIFY(sources);
    sources->setFloating(false);
    main->splitDockWidget(variants, sources, Qt::Vertical);
#endif
    // Let the initial open/restore complete before the user's resize gesture.
    QTest::qWait(150);
    main->resize(1040, 760);
    main->resizeDocks({dock}, {main->height() * 3 / 4}, Qt::Vertical);
    QTest::qWait(120);
    if (main->height() > 800 || dock->height() <= main->height() / 2) {
        qInfo() << "main size" << main->size() << "minimum" << main->minimumSize()
                << "hint" << main->minimumSizeHint();
        for (auto child : main->findChildren<QWidget*>())
            if (child->isVisible() && (qobject_cast<QDockWidget*>(child) ||
                child->minimumSizeHint().height() > 100 || child == main->centralWidget()))
                qInfo() << child->metaObject()->className() << child->objectName()
                        << child->size() << child->minimumSize() << child->minimumSizeHint();
    }
    QVERIFY2(dock->height() > main->height() / 2,
             qPrintable(QString("editor %1 / window %2").arg(dock->height()).arg(main->height())));
    QVERIFY2(main->height() <= 800, qPrintable(QString::number(main->height())));
    variants->hide();
#ifdef KUMO_VST_HOST
    sources->hide();
#endif
    auto fullscreen = dock->findChild<QAction*>("kumoPanelFullscreen");
    auto button = dock->findChild<QToolButton*>("kumoPanelFullscreenButton");
    QVERIFY(fullscreen && button && button->isVisible());
    const auto originalArea = main->dockWidgetArea(dock);
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY(dock->isFloating() && dock->isFullScreen());
    QVERIFY(fullscreen->isChecked());
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY(!dock->isFloating() && !dock->isFullScreen());
    QCOMPARE(main->dockWidgetArea(dock), originalArea);
    dock->setFloating(true);
    dock->resize(760, 540);
    dock->move(-8000, -8000);
    dock->hide();
    dock->show();
    QTest::qWait(180);
    auto available = dock->screen()->availableGeometry();
    QVERIFY2(available.contains(dock->frameGeometry()), qPrintable(QString("frame %1,%2 %3x%4")
        .arg(dock->frameGeometry().x()).arg(dock->frameGeometry().y())
        .arg(dock->frameGeometry().width()).arg(dock->frameGeometry().height())));
    const auto before = dock->geometry();
    fullscreen->trigger();
    QTRY_VERIFY(dock->isFullScreen());
    dock->activateWindow();
    dock->setFocus();
    QTest::keyClick(dock->windowHandle(), Qt::Key_Escape);
    QTRY_VERIFY(!dock->isFullScreen() && dock->isFloating());
    QVERIFY(qAbs(dock->width() - before.width()) < 5);
    QVERIFY(qAbs(dock->height() - before.height()) < 5);
    dock->resize(600, 460);
    QTest::qWait(100);
    auto parameters = editor->findChild<QWidget*>("performanceParametersArea");
    QVERIFY(parameters && parameters->isVisible());
    QVERIFY2(parameters->height() >= 48, qPrintable(QString::number(parameters->height())));
    const auto output = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    dock->grab().save(output.filePath("editor-small-floating-072.png"));
    dock->setFloating(false);
    const int savedHeight = dock->height();
    fullscreen->trigger();
    QTRY_VERIFY(dock->isFullScreen());
    finishToolFullscreen(main);
    QTRY_VERIFY(!dock->isFullScreen() && !dock->isFloating());
    QVERIFY(qAbs(dock->height() - savedHeight) < 10);
}
void TestPluginHost::spectrumButtonsAndFirstOpenContent() {
    auto main = Ms::mscore;
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    auto tabs = editor->findChild<QTabBar*>("workflowExpressionTabs");
    QVERIFY(tabs);
    tabs->setCurrentIndex(2);
    auto pane = editor->findChild<QWidget*>("transcriptionSpectrumPane");
    QVERIFY(pane);
    QVERIFY(!pane->findChild<QComboBox*>("transcriptionTracks")->currentText().isEmpty());
    QVERIFY(!pane->findChild<QComboBox*>("transcriptionClips")->currentText().isEmpty());
    editor->findChild<QAction*>("editorArea/float/transcriptionSpectrumPane")->trigger();
    auto dock = main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");
    QVERIFY(dock);
    const auto output = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    for (int width : {720, 1200}) {
        dock->resize(width, 620);
        QTest::qWait(150);
        auto play = pane->findChild<QToolButton*>("transcriptionPlay");
        QVERIFY(play && play->isVisible());
        QVERIFY2(play->width() <= play->sizeHint().width() + 1,
                 qPrintable(QString("play width %1, hint %2").arg(play->width()).arg(play->sizeHint().width())));
        auto tools = pane->findChild<QWidget*>("spectrumTransportTools");
        QVERIFY(tools);
        const auto controls = tools->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (int i = 0; i < controls.size(); ++i) {
            if (controls[i]->isHidden())
                continue;
            for (int j = i + 1; j < controls.size(); ++j)
                if (!controls[j]->isHidden())
                    QVERIFY2(!controls[i]->geometry().intersects(controls[j]->geometry()),
                             qPrintable(controls[i]->objectName() + "/" + controls[j]->objectName()));
        }
        dock->grab().save(output.filePath(QString("spectrum-controls-%1-072.png").arg(width)));
    }
    editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();
    tabs->setCurrentIndex(0);
}
void TestPluginHost::cfxLiveStateKeepsEditorInstance() {
#ifdef KUMO_VST_HOST
    const QString path = qEnvironmentVariable("P2_TEST_CFX_DLL", "E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll");
    if (!QFileInfo::exists(path))
        QSKIP("CFX is not installed");
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->show();
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
    synth->setSampleRate(44100);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti, synth.get());
    QString error;
    const auto descriptors = VstHost::scanVst2(path, &error);
    QVERIFY2(!descriptors.isEmpty(), qPrintable(error));
    auto channel = const_cast<Channel*>(score->midiMapping().front().articulation());
    const auto assigned = assignVstInstrument(score, channel, descriptors.front().toMap());
    QVERIFY2(!assigned.contains("error"), qPrintable(assigned.value("error").toString()));
    showVstPanel(channel);
    auto panel = main->findChild<QDockWidget*>("kumoVstPanel");
    QVERIFY(panel);
    auto engine = static_cast<VstSynth*>(synth->synthesizer("KumoVST"));
    auto plugin = engine->rack().plugin(channel->bank());
    QVERIFY(plugin);
    QVERIFY2(plugin->openEditor(panel, &error), qPrintable(error));
    QPointer<QWidget> window = panel->findChild<QWidget*>("kumoVstPluginEditor");
    QVERIFY(window);
    struct CloseEditor {
        VstHost::Plugin* plugin;
        QDockWidget* panel;
        ~CloseEditor() { plugin->closeEditor(); panel->hide(); }
    } closeEditor{plugin, panel};
    QVERIFY(window->isVisible());
    window->move(-8000, -8000);
    window->hide();
    window->show();
    QTest::qWait(100);
    QVERIFY(window->screen()->availableGeometry().contains(window->frameGeometry()));
    QVERIFY(engine->rack().hasOpenEditors());
    const auto engineRevision = engine->revision();
    const int undo = score->undoStack()->getCurIdx();
    QTest::qWait(2600);
    QVERIFY(window && window->isVisible());
    QCOMPARE(engine->rack().plugin(channel->bank()), plugin);
    QCOMPARE(engine->revision(), engineRevision);
    QCOMPARE(score->undoStack()->getCurIdx(), undo);
    auto parameters = plugin->parameters();
    for (const auto& value : parameters) {
        auto parameter = value.toMap();
        if (parameter["readOnly"].toBool())
            continue;
        const auto parameterValue = parameter["value"].toDouble();
        plugin->setParameter(parameter["id"].toUInt(), parameterValue > .5 ? .25 : .75);
        break;
    }
    saveVstEdits(score);
    QVERIFY(window && window->isVisible());
    QCOMPARE(engine->rack().plugin(channel->bank()), plugin);
    QCOMPARE(engine->revision(), engineRevision);
    const int captured = score->undoStack()->getCurIdx();
    QCOMPARE(captured, undo + 1);
    const auto captureFolder=qEnvironmentVariable("P2_TEST_CFX_CAPTURE");
    auto recordCapture=[&](const QString& label) {
        if(captureFolder.isEmpty()) return;
        const auto group=score->synthesizerState().group("KumoVST");
        const auto saved=group.empty()?QVariantMap():QJsonDocument::fromJson(group.front().data.toUtf8()).toVariant().toMap();
        QFile file(QDir(captureFolder).filePath(label+".json"));
        if(file.open(QIODevice::WriteOnly)) file.write(QJsonDocument::fromVariant(QVariantMap{{"saved",saved},{"actual",engine->configuration()}}).toJson());
    };
    recordCapture("first-capture");
    saveVstEdits(score);
    recordCapture("second-capture");
    QCOMPARE(score->undoStack()->getCurIdx(), captured);
    for(const auto& row:engine->configuration()["slots"].toList()) QVERIFY(!row.toMap().contains("commonPiano"));
    QCOMPARE(engine->rack().plugin(channel->bank()), plugin);
    qInfo() << "CFX live capture undo commands" << undo << captured
            << "engine revision unchanged" << engineRevision;
    if (qEnvironmentVariableIsSet("P2_TEST_CFX_REVIEW")) {
        window->raise();
        window->activateWindow();
        QTest::qWait(45000);
    }
    if (qEnvironmentVariableIsSet("P2_TEST_NATIVE_FOCUS")) {
        window->raise();
        window->activateWindow();
        QTRY_VERIFY(nativeToolFocused(window));
        QTest::qWait(2600);
        QVERIFY(nativeToolFocused(window));
    }
    plugin->closeEditor();
    QTest::qWait(100);
    QVERIFY(!engine->rack().hasOpenEditors());
#else
    QSKIP("Windows x64 VST host");
#endif
}

void TestPluginHost::personalToolWindowsRemainIndependent() {
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->show();
    main->showPerformanceEditor(true);
    showSuggestionPanel(true);
    showRegionalVariantPanel();
    showReferencePanel(true);
    showHarmonyPanel(true);
    const QStringList names{"kumoPanel/ArrangementSuggestions", "kumoPanel/regionalVariantsDock",
                            "kumoPanel/ReferencePlayer_MS3", "kumoPanel/HarmonyAssistant_MS3"};
    QList<QDockWidget*> tools;
    for (const auto& name : names) {
        auto dock = main->findChild<QDockWidget*>(name);
        QVERIFY2(dock, qPrintable(name));
        QVERIFY2(dock->findChild<QAction*>("kumoPanelFullscreen"), qPrintable(name));
        tools.append(dock);
    }
    for (auto dock : tools) {
        dock->show();
        QList<bool> hidden;
        for (auto other : tools)
            hidden.append(other->isHidden());
        dock->close();
        QVERIFY(dock->isHidden());
        for (int i = 0; i < tools.size(); ++i)
            if (tools[i] != dock)
                QCOMPARE(tools[i]->isHidden(), hidden[i]);
        dock->show();
        QVERIFY(!dock->isHidden());
        dock->setFloating(true);
        dock->resize(520, 540);
        QTest::qWait(30);
        const auto originalSize = dock->size();
        auto full = dock->findChild<QAction*>("kumoPanelFullscreen");
        full->trigger();
        QTRY_VERIFY(dock->isFullScreen());
        full->trigger();
        QTRY_VERIFY(dock->isFloating() && !dock->isFullScreen());
        QCOMPARE(dock->size(), originalSize);
        dock->setFloating(false);
    }
    showReferencePanel(false);
    showHarmonyPanel(false);
    showSuggestionPanel(false);
    main->findChild<QDockWidget*>("kumoPanel/regionalVariantsDock")->hide();
}
