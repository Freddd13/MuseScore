// Actual menu ownership during startup, and CFX's loading event loop.
void TestPluginHost::systemLockMenuOwner157()
{
    auto main = Ms::mscore;
    auto canonical = main->findChild<QMenu*>("kumo-system-lock-menu");
    QVERIFY(canonical);
    QMenu view(main->menuBar());
    auto restored = view.addMenu("menu-kumo-system-lock");
    auto custom = restored->addAction("Saved user action");
    Workspace::addMenuAndString(restored, "menu-kumo-system-lock");
    auto cleanup = qScopeGuard([&] {
        Workspace::addMenuAndString(canonical, "menu-kumo-system-lock");
        canonical->removeAction(custom);
        delete custom;
    });
    QScopedValueRollback<MuseScore*> constructing(Ms::mscore, nullptr);
    addSystemLockActions(&view);
    QCOMPARE(view.actions().size(), 1);
    QCOMPARE(view.actions().front()->menu(), canonical);
    QCOMPARE(canonical->parentWidget(), static_cast<QWidget*>(main));
    QVERIFY(canonical->actions().contains(custom));
    QCOMPARE(canonical->title(), QString::fromUtf8("谱行锁定"));
    for (const auto key : {"current", "selected", "unlock", "all"})
        QVERIFY(main->findChild<QAction*>(QString("kumo-system-lock-") + key));
}

void TestPluginHost::cfxRestoredPanelFollowsSource157()
{
#ifdef KUMO_VST_HOST
    const auto fixture = qEnvironmentVariable("P2_TEST_CFX_SCORE");
    if (fixture.isEmpty() || !QFileInfo::exists(fixture))
        QSKIP("Set P2_TEST_CFX_SCORE to an independent score with the Compact piano loaded");
    auto main = Ms::mscore;
    if (auto old = main->findChild<QDockWidget*>("kumoVstPanel")) delete old;
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
    synth->setSampleRate(44100);
    QScopedValueRollback<MasterSynthesizer*> active(Ms::synti, synth.get());
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    auto score = main->readScore(fixture);
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1280, 850);
    main->show();
    restoreVstForScore(score);
    QScopedValueRollback<bool> sequencing(noSeq, false);
    auto sequence = new Seq;
    Ms::seq = sequence;
    const auto stopSequence = qScopeGuard([&] {
        sequence->stopWait();
        sequence->synchronizeStoppedGui();
        sequence->waitForStoppedRendering();
        sequence->setScoreView(nullptr);
        delete sequence;
        Ms::seq = nullptr;
    });
    sequence->setDriver(new SourcePlaybackDriver(sequence));
    sequence->setMasterSynthesizer(synth.get());
    QVERIFY(sequence->init());
    sequence->setScoreView(main->currentScoreView());
    QScopedValueRollback<Sequencer*> midiSequence(MScore::seq, sequence);
    auto channel = const_cast<Channel*>(score->midiMapping().front().articulation());
    QCOMPARE(channel->synti(), QString("KumoVST"));
    showVstPanel(channel);
    auto panel = main->findChild<QDockWidget*>("kumoVstPanel");
    VstPanelCleanup088 cleanup{panel};
    QVERIFY(panel);
    auto current = panel->findChild<QLabel*>("kumoVstCurrentSource");
    auto open = panel->findChild<QPushButton*>("kumoVstOpenSource");
    auto remove = panel->findChild<QPushButton*>("kumoVstRemoveSource");
    auto restore = panel->findChild<QPushButton*>("kumoVstRestoreSource");
    QVERIFY(current && open && remove && restore);
    QVERIFY(current->text().contains("VST"));
    QVERIFY(open->isEnabled());
    QTest::mouseClick(remove, Qt::LeftButton);
    QCOMPARE(channel->synti(), QString("Fluid"));
    QVERIFY(!open->isEnabled());
    QVERIFY(restore->isEnabled());
    // CFX pumps Windows messages while loading samples. Keep the panel open
    // throughout, as in the reported desktop operation.
    QTest::mouseClick(restore, Qt::LeftButton);
    QCOMPARE(channel->synti(), QString("KumoVST"));
    QTRY_VERIFY_WITH_TIMEOUT(open->isEnabled(), 4000);
    QVERIFY2(current->text().contains("VST"), qPrintable(current->text()));
    QVERIFY(!restore->isEnabled());
    QTest::qWait(1100);
    QVERIFY(open->isEnabled());
    QVERIFY(current->text().contains("VST"));
    // Native patch changes may occur after the plugin revision was observed
    // during CFX's load dialog. Check the channel separately from rack changes.
    MidiPatch sf2;
    sf2.synti = "Fluid";
    sf2.bank = 0;
    sf2.prog = 0;
    sf2.drum = false;
    sf2.sfid = 0;
    score->startCmd();
    score->undo(new ChangePatch(score, channel, &sf2));
    score->endCmd();
    QTRY_VERIFY_WITH_TIMEOUT(!open->isEnabled(), 2000);
    QVERIFY(current->text().contains("SF2"));
    EditData edit;
    score->undoRedo(true, &edit);
    QTRY_VERIFY_WITH_TIMEOUT(open->isEnabled(), 2000);
    QVERIFY(current->text().contains("VST"));
    const auto output = qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path());
    QVERIFY(panel->grab().save(output + "/cfx-restored-panel-157.png"));
    QFileInfo saved(output + "/cfx-restored-panel-157.mscz");
    QVERIFY(score->saveCompressedFile(saved, false, false));
#else
    QSKIP("Windows VST host");
#endif
}
