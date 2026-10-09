void TestPerformanceEditor::envelopeInspectorAndPreview()
      {
      if(!Ms::mscore) {
            QCoreApplication::setOrganizationName("PerformanceRegression"); QCoreApplication::setApplicationName("EnvelopeInspector");
            QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,_directory.path());
            dataPath=_directory.path(); MScore::noGui=true; noSeq=true; converterMode=true; initMuseScoreResources(); QStringList args; MuseScore::init(args);
            }
      auto main=Ms::mscore; QVERIFY(main); main->resize(1250,850); main->show();
      std::unique_ptr<PalettePanel> palette(MuseScore::newTremoloPalettePanel());
      for(int i=0;i<palette->ncells();++i) { auto cell=palette->cell(i); if(!cell->element->isTremolo()) continue; auto t=toTremolo(cell->element.get()); QCOMPARE(t->getProperty(Pid::ENVELOPE_MODE).toInt(),t->isBuzzRoll() ? 0 : 1); }
      auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/midi/envelope.mscx"); QVERIFY(score); main->setCurrentScoreView(main->appendScore(score));
      auto chord=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0)); auto note=chord->downNote();
      note->setVeloType(Note::ValueType::USER_VAL); note->setVeloOffset(80);
      auto tremolo=new Tremolo(score); tremolo->setTremoloType(TremoloType::R16); tremolo->setTrack(0); tremolo->setParent(chord); tremolo->setChords(chord,nullptr);
      score->startCmd(); score->undoAddElement(tremolo); score->select(tremolo,SelectType::SINGLE,0); score->endCmd();
      getAction("inspector")->setChecked(true); main->cmd(getAction("inspector")); QTest::qWait(60);
      auto panel=main->findChild<InspectorTremolo*>(); QVERIFY(panel); auto mode=panel->findChild<QComboBox*>("envelopeMode"); QVERIFY(mode); QCOMPARE(mode->currentIndex(),0);
      int before=score->undoStack()->getCurIdx(); mode->setCurrentIndex(2); QTRY_COMPARE(tremolo->getProperty(Pid::ENVELOPE_MODE).toInt(),2);
      QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_START).toDouble(),80.0); QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_END).toDouble(),100.0); QCOMPARE(score->undoStack()->getCurIdx(),before+1);
      score->undoRedo(true,&ed); QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_MODE).toInt(),0); score->undoRedo(false,&ed);
      score->startCmd(); score->select(tremolo,SelectType::SINGLE,0); score->endCmd(); QTest::qWait(40); panel=main->findChild<InspectorTremolo*>();
      auto spin=panel->findChild<QDoubleSpinBox*>("envelopeEnd"); auto label=panel->findChild<QLabel*>("envelopeEndLabel"); QVERIFY(spin && label);
      QPoint p=label->rect().center(),to=p+QPoint(10,0); QTest::mousePress(label,Qt::LeftButton,Qt::NoModifier,p);
      QMouseEvent motion(QEvent::MouseMove,to,to,label->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier); QApplication::sendEvent(label,&motion);
      QCOMPARE(spin->value(),101.0); QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_END).toDouble(),100.0); QTest::mouseRelease(label,Qt::LeftButton,Qt::NoModifier,to);
      QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_END).toDouble(),101.0); score->undoRedo(true,&ed); QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_END).toDouble(),100.0);
      score->startCmd(); score->select(tremolo,SelectType::SINGLE,0); score->endCmd(); QTest::qWait(40); panel=main->findChild<InspectorTremolo*>(); spin=panel->findChild<QDoubleSpinBox*>("envelopeEnd"); label=panel->findChild<QLabel*>("envelopeEndLabel");
      QTest::mousePress(label,Qt::LeftButton,Qt::NoModifier,p); QApplication::sendEvent(label,&motion); QTest::keyClick(label,Qt::Key_Escape); QTest::mouseRelease(label,Qt::LeftButton,Qt::NoModifier,to); QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_END).toDouble(),100.0);
      spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin,"95"); QTest::keyClick(spin,Qt::Key_Return); QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_END).toDouble(),95.0);
      auto editor=main->performanceEditor(); score->setIsPlaying(true); mode=panel->findChild<QComboBox*>("envelopeMode"); mode->setCurrentIndex(1); QTest::qWait(40);
      QVERIFY(editor->hasPending()); QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_MODE).toInt(),2); score->setIsPlaying(false); QTRY_COMPARE(tremolo->getProperty(Pid::ENVELOPE_MODE).toInt(),1); QCOMPARE(tremolo->getProperty(Pid::ENVELOPE_END).toDouble(),90.0);
      panel=main->findChild<InspectorTremolo*>(); panel->grab().save("envelope-inspector.png");
      main->showPerformanceEditor(true); editor->findChild<QComboBox*>("performanceParameter")->setCurrentIndex(0); editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
      score->startCmd(); score->select(note,SelectType::SINGLE,0); score->endCmd(); QTest::qWait(100); editor->fit();
      auto canvas=editor->findChild<QWidget*>("performanceNoteCanvas"); QVERIFY(canvas); editor->grab().save("envelope-editor.png");
      // Public cached diagnostics use the same velocity function as rendering.
      QCOMPARE(editor->actualVelocityRange(note),qMakePair(72,80));
      editor->setSelectedValue(90); QTest::qWait(40); QCOMPARE(note->veloOffset(),90); QCOMPARE(editor->actualVelocityRange(note),qMakePair(81,90));
      score->undoRedo(true,&ed); QTest::qWait(60); QCOMPARE(editor->actualVelocityRange(note),qMakePair(72,80));
      editor->convertSelected(Note::ValueType::OFFSET_VAL); QTest::qWait(50); QCOMPARE(editor->actualVelocityRange(note),qMakePair(72,80));
      editor->convertSelected(Note::ValueType::USER_VAL); QTest::qWait(50); QCOMPARE(editor->actualVelocityRange(note),qMakePair(72,80));
      QVERIFY(saveScore(score,"envelope-validation.mscx"));
      score->startCmd(); score->undoRemoveElement(tremolo); auto trill=new Trill(score); trill->setTrack(0); trill->setTick(Fraction(0,1)); trill->setTicks(Fraction(1,4)); trill->applyEnvelopePreset(1); score->undoAddElement(trill); score->endCmd();
      QVERIFY(!trill->spannerSegments().empty()); auto segment=trill->spannerSegments().front(); score->startCmd(); score->select(segment,SelectType::SINGLE,0); score->endCmd(); QTest::qWait(50);
      auto trillPanel=main->findChild<InspectorTrill*>(); QVERIFY(trillPanel); auto trillMode=trillPanel->findChild<QComboBox*>("envelopeMode"); QVERIFY(trillMode); QCOMPARE(trillMode->currentIndex(),1);
      trillMode->setCurrentIndex(3); QTRY_COMPARE(trill->getProperty(Pid::ENVELOPE_MODE).toInt(),3); QCOMPARE(trill->getProperty(Pid::ENVELOPE_END).toDouble(),80.0);
      score->startCmd(); score->undoRemoveElement(trill); score->select(note,SelectType::SINGLE,0); score->addArticulation(SymId::ornamentShortTrill); score->endCmd();
      auto ornament=chord->articulations().front(); score->startCmd(); score->select(ornament,SelectType::SINGLE,0); score->endCmd(); QTest::qWait(50);
      auto articulationPanel=main->findChild<InspectorArticulation*>(); QVERIFY(articulationPanel); mode=articulationPanel->findChild<QComboBox*>("envelopeMode"); QVERIFY(mode); QCOMPARE(mode->currentIndex(),1);
      auto alt=articulationPanel->findChild<QDoubleSpinBox*>("envelopeAlternate"); QVERIFY(alt); alt->setValue(12.0); QCOMPARE(ornament->getProperty(Pid::ENVELOPE_ALTERNATE).toDouble(),12.0);
      }
void TestPerformanceEditor::envelopeRealSequencer()
      {
      auto main=Ms::mscore; QVERIFY(main); QVERIFY(!Ms::seq);
      getAction("toggle-piano")->setChecked(false); main->cmd(getAction("toggle-piano")); QVERIFY(main->pianoTools());
      auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/midi/envelope.mscx"); QVERIFY(score); main->setCurrentScoreView(main->appendScore(score));
      auto first=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0)); auto second=toChord(first->segment()->next(SegmentType::ChordRest)->element(0));
      first->downNote()->setVeloType(Note::ValueType::USER_VAL); first->downNote()->setVeloOffset(80); second->downNote()->setVeloType(Note::ValueType::USER_VAL); second->downNote()->setVeloOffset(100); second->downNote()->setPitch(64); second->downNote()->setTpcFromPitch();
      auto tremolo=new Tremolo(score); tremolo->setTremoloType(TremoloType::C16); tremolo->applyEnvelopePreset(1); tremolo->setTrack(0); tremolo->setParent(first); tremolo->setChords(first,second);
      score->startCmd(); score->undoAddElement(tremolo); score->endCmd();
      auto editor=main->performanceEditor(); main->showPerformanceEditor(true); QTest::qWait(70);
      QCOMPARE(editor->actualVelocityRange(first->downNote()),qMakePair(72,80)); QCOMPARE(editor->actualVelocityRange(second->downNote()),qMakePair(85,85));
      MasterSynthesizer synth; MScore::sampleRate=44100; auto recording=new PerformanceRecordingSynth; synth.registerSynthesizer(recording); synth.setSampleRate(44100); synth.init();
      Ms::seq=new PerformanceAuditionSeq; auto sequence=Ms::seq; std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
      auto driver=new PerformanceTestDriver(sequence); sequence->setDriver(driver); sequence->setMasterSynthesizer(&synth); QVERIFY(sequence->init()); sequence->setScoreView(main->currentScoreView());
      score->setPlayPos(Fraction::fromTicks(0)); sequence->start(); QApplication::processEvents();
      for(int i=0;i<30 && recording->onPitches.size()<8;++i){driver->pulse();QTest::qWait(1);} QVERIFY(recording->onPitches.size()>=8);
      QCOMPARE(recording->onVelocities.mid(0,8),QVector<int>({80,85,72,85,72,85,72,85}));
      sequence->stopWait(); sequence->waitForStoppedRendering(); QApplication::processEvents(); recording->onFrames.clear(); recording->onPitches.clear(); recording->onVelocities.clear();
      score->setPlayPos(Fraction::fromTicks(480)); sequence->start(); QApplication::processEvents(); driver->pulse();
      QCOMPARE(recording->onPitches.front(),60); QCOMPARE(recording->onVelocities.front(),72);
      getAction("loop")->setChecked(true); score->setLoopInTick(Fraction::fromTicks(480)); score->setLoopOutTick(Fraction::fromTicks(960)); bool looped=false; int previous=sequence->getCurTick();
      for(int i=0;i<35;++i){driver->pulse();QTest::qWait(2);int current=sequence->getCurTick();if(current<previous){looped=true;break;}previous=current;} QVERIFY(looped); getAction("loop")->setChecked(false);
      }
