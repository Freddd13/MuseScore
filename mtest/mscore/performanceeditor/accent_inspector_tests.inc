void TestPerformanceEditor::accentInspectorAndNativeDefault()
      {
      auto main = Ms::mscore; QVERIFY(main);
      std::unique_ptr<PalettePanel> palette(MuseScore::newArticulationsPalettePanel());
      int lightCount = 0;
      for (int i = 0; i < palette->ncells(); ++i) {
            auto cell = palette->cell(i); if (!cell->element || !cell->element->isArticulation()) continue;
            auto a = toArticulation(cell->element.get());
            QCOMPARE(a->getProperty(Pid::ARTIC_VELOCITY_MODE).toBool(), a->hasLightAccentPreset());
            if (a->hasLightAccentPreset()) {
                  ++lightCount; QCOMPARE(a->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 115.0);
                  auto restored = PaletteCell::readMimeData(cell->mimeData()); QVERIFY(restored && restored->element);
                  QVERIFY(restored->element->getProperty(Pid::ARTIC_VELOCITY_MODE).toBool());
                  }
            }
      QVERIFY(lightCount >= 5);
      auto score = main->readScore(QString(TESTROOT) + "/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score);
      main->setCurrentScoreView(main->appendScore(score));
      auto chord = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      score->startCmd(); score->select(chord->downNote(), SelectType::SINGLE, 0); score->addArticulation(SymId::articAccentAbove); score->endCmd();
      auto accent = chord->articulations().front();
      score->startCmd(); score->select(accent, SelectType::SINGLE, 0); score->endCmd(); QTest::qWait(50);
      getAction("inspector")->setChecked(true); main->cmd(getAction("inspector")); QTest::qWait(60);
      auto panel = main->findChild<InspectorArticulation*>(); QVERIFY(panel);
      auto spin = panel->findChild<QDoubleSpinBox*>("articVelocityPercent"); auto mode = panel->findChild<QCheckBox*>("articVelocityMode");
      auto label = panel->findChild<QLabel*>("articVelocityPercentLabel"); QVERIFY(spin && mode && label); QCOMPARE(spin->value(), 115.0); QVERIFY(spin->isEnabled());
      QPoint press = label->rect().center(), moved = press + QPoint(10, 0);
      QTest::mousePress(label, Qt::LeftButton, Qt::NoModifier, press);
      QMouseEvent move(QEvent::MouseMove, moved, moved, label->mapToGlobal(moved), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
      QApplication::sendEvent(label, &move); QCOMPARE(spin->value(), 116.0); QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 115.0);
      QTest::mouseRelease(label, Qt::LeftButton, Qt::NoModifier, moved); QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 116.0);
      score->undoRedo(true, &ed); QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 115.0); score->undoRedo(false, &ed);
      score->startCmd(); score->select(accent, SelectType::SINGLE, 0); score->endCmd(); QTest::qWait(40);
      panel = main->findChild<InspectorArticulation*>(); label = panel->findChild<QLabel*>("articVelocityPercentLabel"); spin = panel->findChild<QDoubleSpinBox*>("articVelocityPercent");
      spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin, "112"); QTest::keyClick(spin, Qt::Key_Return);
      QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 112.0);
      score->undoRedo(true, &ed); QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 116.0);
      score->startCmd(); score->select(accent, SelectType::SINGLE, 0); score->endCmd(); QTest::qWait(40);
      panel = main->findChild<InspectorArticulation*>(); label = panel->findChild<QLabel*>("articVelocityPercentLabel"); spin = panel->findChild<QDoubleSpinBox*>("articVelocityPercent");
      QTest::mousePress(label, Qt::LeftButton, Qt::NoModifier, press); QApplication::sendEvent(label, &move); QTest::keyClick(label, Qt::Key_Escape);
      QTest::mouseRelease(label, Qt::LeftButton, Qt::NoModifier, moved); QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 116.0);
      auto editor = main->performanceEditor(); score->setIsPlaying(true); spin->setValue(108.0);
      QVERIFY(editor->hasPending()); QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 116.0);
      score->setIsPlaying(false); QTRY_COMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 108.0);
      panel = main->findChild<InspectorArticulation*>(); mode = panel->findChild<QCheckBox*>("articVelocityMode"); QTest::mouseClick(mode, Qt::LeftButton);
      QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_MODE).toBool(), false);
      auto preset = panel->findChild<QPushButton*>("articLightPreset"); QVERIFY(preset); QTest::mouseClick(preset, Qt::LeftButton);
      QTRY_VERIFY(accent->getProperty(Pid::ARTIC_VELOCITY_MODE).toBool()); QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 115.0);
      panel->grab().save("accent-inspector.png");
      score->undoRedo(true, &ed); QVERIFY(!accent->getProperty(Pid::ARTIC_VELOCITY_MODE).toBool()); QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 108.0);
      }
