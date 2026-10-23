// Actual combined windows: preserve the user's proportions and fit the available screen.
void TestPluginHost::combinedToolsKeepWorkspaceAndScreenBounds() {
  auto main = Ms::mscore;
  MuseScore::updateUiStyleAndTheme();
  auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
  QVERIFY(score);
  main->setCurrentScoreView(main->appendScore(score));
  main->findChild<QAction *>("kumo-workspace-arrange")->trigger();
  const QRect available = main->screen()->availableGeometry();
  const QSize wanted = QSize(1360, 920).boundedTo(available.size() - QSize(24, 70));
  main->showNormal();
  main->resize(wanted);
  main->move(available.topLeft() + QPoint(8, 8));
  main->show();
  std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
  synth->setSampleRate(44100);
  QScopedValueRollback<MasterSynthesizer *> current(Ms::synti, synth.get());
  QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
  Mixer mixer(main);
  mixer.setObjectName("mixerCombined089");
  mixer.setScore(score);
  mixer.showDetailsToggled(true);
  main->addDockWidget(Qt::RightDockWidgetArea, &mixer);
  mixer.show();
  showHarmonyPanel(true);
  showRegionalVariantPanel();
  showReferencePanel(true);
  main->showPerformanceEditor(true);
  auto editor = main->performanceEditor();
  auto editorDock = main->findChild<QDockWidget *>("performanceEditorDock");
  auto harmony = main->findChild<QDockWidget *>("kumoPanel/HarmonyAssistant_MS3");
  auto variants = main->findChild<QDockWidget *>("kumoPanel/regionalVariantsDock");
  auto reference = main->findChild<QDockWidget *>("kumoPanel/ReferencePlayer_MS3");
  QVERIFY(editorDock && harmony && variants && reference);
  main->tabifyDockWidget(&mixer, harmony);
  main->tabifyDockWidget(&mixer, variants);
  main->tabifyDockWidget(editorDock, reference);
  QmlPlugin *harmonyRoot = nullptr;
  for (auto window : QGuiApplication::allWindows())
    if (auto view = qobject_cast<QQuickView *>(window))
      if (auto root = qobject_cast<QmlPlugin *>(view->rootObject()))
        if (root->menuPath() == "Plugins.Harmony Assistant")
          harmonyRoot = root;
  QVERIFY(harmonyRoot);
  QVERIFY(QMetaObject::invokeMethod(harmonyRoot, "changeSummary", Q_ARG(QVariant, "dualPanel"),
                                   Q_ARG(QVariant, true)));
  harmonyRoot->showSummaryPanel(true);
  QTest::qWait(250);
  main->resize(wanted);
  main->resizeDocks({editorDock}, {qRound(main->height() * .6)}, Qt::Vertical);
  editorDock->raise();
  QTest::qWait(200);
  const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
  auto snapshot = [&](const QString &name) { return main->grab().save(output.filePath(name)); };
  QList<QImage> iconsBefore;
  const QList<const char *> shortcutKeys{"harmony-assistant", "reference-player", "performance-expression"};
  for (const auto key : shortcutKeys)
    iconsBefore.append(getAction(key)->icon().pixmap(24, 24).toImage());
  QVERIFY(iconsBefore[0] != iconsBefore[1] && iconsBefore[1] != iconsBefore[2]);
  Shortcut::refreshIcons();
  for (int index = 0; index < shortcutKeys.size(); ++index)
    QCOMPARE(getAction(shortcutKeys[index])->icon().pixmap(24, 24).toImage(), iconsBefore[index]);
  const QList<QPair<QString, QString>> legend{
      {"harmony-assistant", "和弦"}, {"reference-player", "原曲"},
      {"performance-expression", "表达"}, {"kumo-spectrum", "扒谱"},
      {"kumo-rhythm", "写节奏"}, {"kumo-roll-writing", "写音"},
      {"kumo-vst", "音源"}, {"arrangement-suggestions", "建议"}};
  QImage iconImage(legend.size() * 90, 62, QImage::Format_ARGB32_Premultiplied);
  iconImage.fill(qApp->palette().color(QPalette::Window));
  {
    QPainter painter(&iconImage);
    painter.setPen(qApp->palette().color(QPalette::WindowText));
    for (int index = 0; index < legend.size(); ++index) {
      painter.drawPixmap(index * 90 + 33, 6, personalToolIcon(legend[index].first).pixmap(24, 24));
      painter.drawText(QRect(index * 90, 34, 90, 24), Qt::AlignCenter, legend[index].second);
    }
  }
  QVERIFY(iconImage.save(output.filePath("layout-icons-089.png")));
  qInfo() << "available" << available << "main" << main->frameGeometry() << "editor"
          << editorDock->size() << "mixer minimum" << mixer.minimumSizeHint();
  QVERIFY(snapshot("layout-combination-089.png"));
  QVERIFY2(available.contains(main->frameGeometry()), "组合面板后主窗口保持在可用屏幕内");
  QVERIFY2(editorDock->height() > main->height() / 2, "多个工具共同使用时编辑器仍能向上展开");
  auto tabs = editor->findChild<QTabBar *>("workflowExpressionTabs");
  QVERIFY(tabs);
  tabs->setCurrentIndex(0);
  auto split = editor->findChild<QSplitter *>("performanceMainSplitter");
  auto notes = editor->findChild<QWidget *>("performanceNotesArea");
  auto parameters = editor->findChild<QWidget *>("performanceParametersArea");
  QVERIFY(split && notes && parameters);
  auto sizes = split->sizes();
  sizes[split->indexOf(notes)] = 120;
  sizes[split->indexOf(parameters)] = 180;
  split->setSizes(sizes);
  QTest::qWait(120);
  const int editorHeight = editorDock->height();
  const double ratio = double(notes->height()) / (notes->height() + parameters->height());
  reference->raise();
  QTest::qWait(120);
  qInfo("after reference tab editor height %d/%d", editorDock->height(), editorHeight);
  QVERIFY(snapshot("layout-reference-tab-089.png"));
  editorDock->raise();
  mixer.raise();
  QTest::qWait(120);
  qInfo("after mixer tab editor height %d/%d", editorDock->height(), editorHeight);
  QVERIFY(mixer.grab().save(output.filePath("layout-mixer-089.png")));
  auto details = mixer.findChild<MixerDetails *>();
  QVERIFY(details);
  qInfo() << "mixer details hint" << details->minimumSizeHint() << details->sizeHint();
  QVERIFY(snapshot("layout-mixer-combination-089.png"));
  for (int repeat = 0; repeat < 2; ++repeat) {
    for (const char *mode : {"daw", "engrave", "post", "arrange"}) {
      main->findChild<QAction *>(QString("kumo-workspace-") + mode)->trigger();
      QTest::qWait(120);
      qInfo("after workspace %s: editor %d, reference %d", mode, editorDock->height(), reference->height());
    }
    editorDock->raise();
    QTRY_VERIFY(editorDock->isVisible());
    qInfo("combined geometry after return %d: editor %d/%d, main %dx%d", repeat,
          editorDock->height(), editorHeight, main->width(), main->height());
    QVERIFY(snapshot("layout-combined-view-return-089.png"));
    QTRY_VERIFY(qAbs(editorDock->height() - editorHeight) < 12);
    const double actual = double(notes->height()) / (notes->height() + parameters->height());
    qInfo("combined return %d: editor %d/%d, ratio %.3f/%.3f", repeat, editorDock->height(),
          editorHeight, actual, ratio);
    QVERIFY(qAbs(actual - ratio) < .04);
  }
  // Float two tools together; closing one leaves the other available.
  const QList<QPair<QString, int>> tools{{"embeddedExpressionPage", 1},
                                         {"transcriptionSpectrumPane", 2}};
  QList<QPointer<QDockWidget>> outside;
  for (const auto &tool : tools) {
    tabs->setCurrentIndex(tool.second);
    QTest::qWait(100);
    auto action = editor->findChild<QAction *>("editorArea/float/" + tool.first);
    QVERIFY(action);
    action->trigger();
    auto dock = main->findChild<QDockWidget *>("kumoEditorArea/" + tool.first);
    QVERIFY(dock);
    outside.append(dock);
    dock->resize(QSize(760, 520).boundedTo(available.size() - QSize(30, 90)));
    dock->move(-5000, -5000);
    dock->hide();
    dock->show();
    QTRY_VERIFY(available.contains(dock->frameGeometry()));
    const QSize original = dock->size();
    auto full = dock->findChild<QAction *>("kumoPanelFullscreen");
    QVERIFY(full);
    full->trigger();
    QTRY_VERIFY(dock->isFullScreen());
    full->trigger();
    QTRY_VERIFY(dock->isFloating() && !dock->isFullScreen());
    QCOMPARE(dock->size(), original);
    QVERIFY(dock->grab().save(output.filePath(tool.first + "-089.png")));
  }
  outside[0]->close();
  QVERIFY(outside[0]->isHidden());
  QVERIFY(!outside[1]->isHidden());
  outside[0]->show();
  QVERIFY(!outside[0]->isHidden());
  for (const auto &tool : tools)
    editor->findChild<QAction *>("editorArea/restore/" + tool.first)->trigger();
  tabs->setCurrentIndex(0);
  QTest::qWait(120);
  QVERIFY(notes->isVisible() && parameters->isVisible());
  QVERIFY(snapshot("layout-combined-return-089.png"));
  mixer.setFloating(true);
  mixer.resize(680, 560);
  mixer.show();
  QTest::qWait(100);
  QVERIFY(mixer.grab().save(output.filePath("layout-mixer-floating-089.png")));
  harmonyRoot->showSummaryPanel(false);
  showHarmonyPanel(false);
  showReferencePanel(false);
  variants->hide();
  mixer.hide();
}
