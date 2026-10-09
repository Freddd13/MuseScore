      void TestPerformanceEditor::graceInspectorAndNativePreset()
            {
            auto main = Ms::mscore; QVERIFY(main);
            auto score = main->readScore(QString(TESTROOT) + "/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));
            auto chord = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
            score->startCmd(); score->select(chord->downNote(), SelectType::SINGLE, 0); score->endCmd();
            main->cmd(getAction("fast-grace")); QTest::qWait(70);
            QCOMPARE(chord->getProperty(Pid::GRACE_PLAY_MODE).toInt(), 1);
            QCOMPARE(chord->graceNotes().size(), 1);
            auto grace = chord->graceNotes().front()->downNote();
            QCOMPARE(grace->chord()->noteType(), NoteType::APPOGGIATURA);
            getAction("inspector")->setChecked(true); main->cmd(getAction("inspector")); QTest::qWait(70);
            auto panel = main->findChild<InspectorNote*>(); QVERIFY(panel);
            auto spin = panel->findChild<QDoubleSpinBox*>("graceDuration"); QVERIFY(spin && spin->isEnabled());
            auto label = panel->findChild<QLabel*>("graceDurationLabel"); QVERIFY(label);
            QPoint press = label->rect().center(), moved = press + QPoint(10, 0);
            QTest::mousePress(label, Qt::LeftButton, Qt::NoModifier, press);
            QMouseEvent move(QEvent::MouseMove, moved, moved, label->mapToGlobal(moved), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
            QApplication::sendEvent(label, &move); QCOMPARE(spin->value(), 66.0);
            QCOMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 65.0);
            QTest::mouseRelease(label, Qt::LeftButton, Qt::NoModifier, moved);
            QCOMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 66.0);
            score->undoRedo(true, &ed); QCOMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 65.0);
            score->undoRedo(false, &ed); QCOMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 66.0);
            score->startCmd(); score->select(grace, SelectType::SINGLE, 0); score->endCmd(); QTest::qWait(40);
            panel = main->findChild<InspectorNote*>(); label = panel->findChild<QLabel*>("graceDurationLabel"); spin = panel->findChild<QDoubleSpinBox*>("graceDuration");
            QTest::mousePress(label, Qt::LeftButton, Qt::NoModifier, press); QApplication::sendEvent(label, &move);
            QTest::keyClick(label, Qt::Key_Escape); QTest::mouseRelease(label, Qt::LeftButton, Qt::NoModifier, moved);
            QCOMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 66.0);
            QCOMPARE(score->selection().element(), static_cast<Element*>(grace));
            auto editor = main->performanceEditor(); score->setIsPlaying(true); spin->setValue(82.0);
            QCOMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 66.0); QVERIFY(editor->hasPending());
            score->setIsPlaying(false); QTRY_COMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 82.0);
            score->startCmd(); chord->undoChangeProperty(Pid::GRACE_PLAY_MODE, 0);
            grace->chord()->undoChangeProperty(Pid::GRACE_APPEARANCE, Chord::graceAppearanceValue(NoteType::ACCIACCATURA, grace->chord()->durationType())); score->endCmd(); QTest::qWait(40);
            panel = main->findChild<InspectorNote*>(); QVERIFY(panel);
            auto preset = panel->findChild<QPushButton*>("graceApplyPreset"); QVERIFY(preset);
            QTest::mouseClick(preset, Qt::LeftButton); QTRY_COMPARE(chord->getProperty(Pid::GRACE_PLAY_MODE).toInt(), 1);
            QCOMPARE(grace->chord()->noteType(), NoteType::APPOGGIATURA);
            QCOMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 65.0);
            panel->grab().save("grace-inspector-015.png");
            score->undoRedo(true, &ed); QCOMPARE(chord->getProperty(Pid::GRACE_PLAY_MODE).toInt(), 0);
            QCOMPARE(grace->chord()->noteType(), NoteType::ACCIACCATURA); QCOMPARE(chord->getProperty(Pid::GRACE_DURATION).toDouble(), 82.0);
            }

      void TestPerformanceEditor::graceRealSequencerPreRoll()
            {
            auto main = Ms::mscore; QVERIFY(main); QVERIFY(!Ms::seq);
            auto score = main->readScore(QString(TESTROOT) + "/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));
            auto first = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
            auto second = toChord(score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest)->element(0));
            for (auto chord : {first, second}) {
                  score->startCmd(); score->setGraceNote(chord, 62, NoteType::APPOGGIATURA, DIVISION / 2); score->endCmd();
                  chord->setProperty(Pid::GRACE_PLAY_MODE, 1);
                  }
            MasterSynthesizer synth; MScore::sampleRate = 44100;
            auto recording = new PerformanceRecordingSynth; synth.registerSynthesizer(recording); synth.setSampleRate(44100); synth.init();
            Ms::seq = new PerformanceAuditionSeq; auto sequence = Ms::seq;
            std::unique_ptr<Seq, std::function<void(Seq*)>> guard(sequence, [](Seq* s) { s->stopWait(); s->waitForStoppedRendering(); s->setScoreView(nullptr); delete s; Ms::seq = nullptr; });
            auto driver = new PerformanceTestDriver(sequence); sequence->setDriver(driver); sequence->setMasterSynthesizer(&synth); QVERIFY(sequence->init()); sequence->setScoreView(main->currentScoreView());
            score->setPlayPos(Fraction::fromTicks(0)); sequence->start(); QApplication::processEvents();
            driver->pulse(); QCOMPARE(sequence->getCurTick(), 0); QCOMPARE(recording->onPitches.front(), 62);
            for (int i = 0; i < 4; ++i) driver->pulse();
            QVERIFY(recording->onFrames.size() >= 4);
            QVERIFY(recording->onFrames[1] - recording->onFrames[0] >= 2600);
            sequence->stopWait(); sequence->waitForStoppedRendering(); QApplication::processEvents();
            recording->onFrames.clear(); recording->onPitches.clear();
            score->setPlayPos(Fraction::fromTicks(1920)); sequence->start(); QApplication::processEvents();
            driver->pulse(); QCOMPARE(sequence->getCurTick(), 1920);
            for (int i = 0; i < 4; ++i) driver->pulse();
            QCOMPARE(recording->onPitches, QVector<int>({62, 60, 64, 67}));
            sequence->stopWait(); sequence->waitForStoppedRendering(); QApplication::processEvents();
            }
