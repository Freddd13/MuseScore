void TestPerformanceEditor::ritNativeCommandsAndConflicts()
      {
      auto main=Ms::mscore; QVERIFY(main);
      auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score);
      main->setCurrentScoreView(main->appendScore(score));
      auto start=score->firstMeasure()->first(SegmentType::ChordRest), end=score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest);
      score->startCmd(); score->selection().setRange(start,end,0,2); score->selection().updateSelectedElements(); score->endCmd();
      main->cmd(getAction("add-rit-curve")); auto curves=TempoExpression::curves(score); QCOMPARE(curves.size(),1); auto line=curves.front();
      QCOMPARE(line->ritTarget(),80.0); QCOMPARE(line->tick2().ticks(),1920); QCOMPARE(score->tempo(Fraction::fromTicks(1920)),1.6);
      QString error; QVERIFY(!ParameterEdit::tempos(score,{{480,100.0}},-1,&error)); QVERIFY(!error.isEmpty());
      const int index=score->undoStack()->getCurIdx(); QVERIFY(!ParameterEdit::tempoCurve(score,480,2400,nullptr,false,&error)); QCOMPARE(score->undoStack()->getCurIdx(),index);
      score->startCmd(); score->select(toChord(end->element(0))->downNote(),SelectType::SINGLE,0); score->endCmd();
      main->cmd(getAction("add-a-tempo")); TempoText* restore=nullptr; for(auto item:end->annotations()) if(item->isTempoText()) restore=toTempoText(item);
      QVERIFY(restore); QCOMPARE(restore->restoreMode(),1); QCOMPARE(score->tempo(end->tick()),2.0);
      score->undoRedo(true,&ed); QCOMPARE(score->tempo(end->tick()),1.6); score->undoRedo(false,&ed); QCOMPARE(score->tempo(end->tick()),2.0);
      QVERIFY(ParameterEdit::detachTempoCurve(line)); QVERIFY(!line->ritPlay());
      QVERIFY(ParameterEdit::tempos(score,{{480,100.0}},-1,&error));
      QVERIFY(!ParameterEdit::tempoCurve(score,0,1920,line,false,&error));
      const int replaceIndex=score->undoStack()->getCurIdx(); QVERIFY(ParameterEdit::tempoCurve(score,0,1920,line,true,&error));
      QCOMPARE(score->undoStack()->getCurIdx(),replaceIndex+1); QVERIFY(line->ritPlay());
      auto middle=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest); for(auto annotation:middle->annotations()) QVERIFY(!annotation->isTempoText());
      score->undoRedo(true,&ed); QVERIFY(!line->ritPlay()); bool found=false; for(auto annotation:middle->annotations()) found |= annotation->isTempoText(); QVERIFY(found);
      score->undoRedo(false,&ed); QVERIFY(line->ritPlay());
      }
void TestPerformanceEditor::ritInspectorAndCurveGestures()
      {
      auto main=Ms::mscore; auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score); main->setCurrentScoreView(main->appendScore(score));
      QVERIFY(ParameterEdit::tempoCurve(score,0,1920,nullptr,false)); auto line=TempoExpression::curves(score).front(); QVERIFY(!line->spannerSegments().empty());
      score->startCmd(); score->select(line->frontSegment(),SelectType::SINGLE,0); score->endCmd();
      getAction("inspector")->setChecked(true); main->cmd(getAction("inspector")); QTest::qWait(60);
      auto inspector=main->findChild<InspectorTextLine*>(); QVERIFY(inspector); auto spin=inspector->findChild<QDoubleSpinBox*>("ritTarget"); QVERIFY(spin); QCOMPARE(spin->value(),80.0);
      spin->setValue(75); QCOMPARE(line->ritTarget(),75.0); score->undoRedo(true,&ed); QCOMPARE(line->ritTarget(),80.0);
      main->showPerformanceEditor(true); auto editor=main->performanceEditor(); QVERIFY(editor);
      auto parameter=editor->findChild<QComboBox*>("performanceParameter"); parameter->setCurrentIndex(1); editor->findChild<QComboBox*>("performanceTool")->setCurrentIndex(0);
      QTest::qWait(80); editor->fit(); QTest::mouseClick(editor->findChild<QToolButton*>("performanceRangeReset"),Qt::LeftButton); QTest::qWait(40);
      auto canvas=editor->findChild<QWidget*>("performanceParameterCanvas"); QVERIFY(canvas);
      auto x=[&](int tick){return qRound(76+(canvas->width()-90)*tick/3840.0);};
      auto y=[&](double bpm){return qRound(canvas->height()-6-(bpm-5)*(canvas->height()-12)/994.0);};
      QPoint endpoint(x(1920),y(96)),moved=endpoint+QPoint(0,-8); int index=score->undoStack()->getCurIdx();
      QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,endpoint); QMouseEvent motion(QEvent::MouseMove,moved,Qt::NoButton,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(canvas,&motion);
      QTest::qWait(30); QCOMPARE(line->ritTarget(),80.0); QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,moved); QTest::qWait(60);
      QVERIFY(line->ritTarget()>80); QCOMPARE(score->undoStack()->getCurIdx(),index+1); score->undoRedo(true,&ed); QCOMPARE(line->ritTarget(),80.0); QTest::qWait(60);
      QPoint midpoint(x(960),y(108)); QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,midpoint); QMouseEvent middleMove(QEvent::MouseMove,midpoint+QPoint(0,10),Qt::NoButton,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(canvas,&middleMove); QTest::keyClick(canvas,Qt::Key_Escape); QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,midpoint+QPoint(0,10)); QCOMPARE(line->ritCurve(),1.0); QCOMPARE(editor->findChild<QDoubleSpinBox*>("performanceValue")->value(),1.0);
      QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,midpoint); editor->setSelectedValue(2); QTest::qWait(60); QCOMPARE(line->ritCurve(),2.0); score->undoRedo(true,&ed); QCOMPARE(line->ritCurve(),1.0);
      editor->grab().save("rit-editor.png"); inspector=main->findChild<InspectorTextLine*>(); if(inspector) inspector->grab().save("rit-inspector.png");
      QVERIFY(ParameterEdit::restoreTempo(score,1920)); QVERIFY(saveScore(score,"rit-validation.mscx"));
      }

void TestPerformanceEditor::ritRealSequencerTiming()
      {
      auto main = Ms::mscore; QVERIFY(main); QVERIFY(!Ms::seq);
      auto score = main->readScore(QString(TESTROOT)+"/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score);
      main->setCurrentScoreView(main->appendScore(score));
      QVERIFY(ParameterEdit::tempoCurve(score,0,1920,nullptr,false));
      MasterSynthesizer synth; MScore::sampleRate = 44100;
      auto recording = new PerformanceRecordingSynth; synth.registerSynthesizer(recording); synth.setSampleRate(44100); synth.init();
      Ms::seq = new PerformanceAuditionSeq; auto sequence = Ms::seq;
      std::unique_ptr<Seq, std::function<void(Seq*)>> guard(sequence, [](Seq* s) { s->stopWait(); s->waitForStoppedRendering(); s->setScoreView(nullptr); delete s; Ms::seq = nullptr; });
      auto driver = new PerformanceTestDriver(sequence); sequence->setDriver(driver); sequence->setMasterSynthesizer(&synth); QVERIFY(sequence->init()); sequence->setScoreView(main->currentScoreView());
      score->setPlayPos(Fraction::fromTicks(0)); sequence->start(); QApplication::processEvents();
      for (int i=0;i<70 && recording->onFrames.size()<9;++i) { driver->pulse(); QTest::qWait(1); }
      QCOMPARE(recording->onFrames.size(),9);
      const unsigned expected = qRound(score->tempomap()->tick2time(1920)*44100);
      QVERIFY2(std::abs(qint64(recording->onFrames[6])-recording->onFrames[0]-expected)<=2,"Sequencer must use the same derived tempo map as export");
      sequence->stopWait(); sequence->waitForStoppedRendering(); QApplication::processEvents();
      recording->onFrames.clear(); recording->onPitches.clear();
      score->setPlayPos(Fraction::fromTicks(1920)); sequence->start(); QApplication::processEvents(); driver->pulse();
      QCOMPARE(recording->onPitches,QVector<int>({60,64,67}));
      getAction("loop")->setChecked(true); score->setLoopInTick(Fraction::fromTicks(1920)); score->setLoopOutTick(Fraction::fromTicks(3840));
      bool looped=false; int previous=sequence->getCurTick();
      for(int i=0;i<70;++i) { driver->pulse(); QTest::qWait(2); const int current=sequence->getCurTick(); if(current<previous){ QCOMPARE(current,1920);looped=true;break;}previous=current; }
      QVERIFY(looped); getAction("loop")->setChecked(false);
      }

void TestPerformanceEditor::ritLongCurveBenchmark()
      {
      if (!qEnvironmentVariableIsSet("RIT_BENCHMARK")) return;
      if (!Ms::mscore) {
            QCoreApplication::setOrganizationName("PerformanceRegression"); QCoreApplication::setApplicationName("RitBenchmark");
            QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,_directory.path());
            dataPath=_directory.path(); MScore::noGui=true; noSeq=true; converterMode=true;
            initMuseScoreResources(); QStringList args; MuseScore::init(args);
            }
      QVERIFY(Ms::mscore); Ms::mscore->resize(1280,800); Ms::mscore->show();
      AboutBoxDialog about; auto label=about.findChild<QLabel*>("versionLabel"); QVERIFY(label);
      QFile version(QString(TESTROOT)+"/personal/VERSION"); QVERIFY(version.open(QIODevice::ReadOnly));
      QVERIFY(label->text().contains(QString::fromUtf8(version.readAll()).trimmed()));
      QVERIFY(label->text().contains("Kumo branch")); QVERIFY(label->text().contains("Freddd13"));
      about.show(); QTest::qWait(20); about.grab().save("rit-about.png"); about.hide();
      auto main=Ms::mscore; QVERIFY(main); auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score);
      main->setCurrentScoreView(main->appendScore(score)); score->startCmd(); score->appendMeasures(126); score->endCmd();
      const int end=score->lastMeasure()->endTick().ticks(); QCOMPARE(end,128*1920);
      QElapsedTimer timer; timer.start(); QVERIFY(ParameterEdit::tempoCurve(score,0,end,nullptr,false)); const double prepare=timer.nsecsElapsed()/1e6;
      const double expected=-1280*std::log(.8), actual=score->tempomap()->tick2time(end);
      QVERIFY(std::abs(actual-expected)/expected*60<.005);
      main->showPerformanceEditor(true); auto editor=main->performanceEditor(); editor->findChild<QComboBox*>("performanceParameter")->setCurrentIndex(1); QTest::qWait(80); editor->fit();
      auto canvas=editor->findChild<QWidget*>("performanceParameterCanvas"); QVERIFY(canvas); QPixmap pixmap(canvas->size());
      timer.restart(); for(int i=0;i<30;++i) canvas->render(&pixmap); const double average=timer.nsecsElapsed()/30e6;
      qInfo("rit benchmark: measures=128 map_nodes=%llu preparation_ms=%.3f overview_paint_ms=%.3f time_error_ms=%.6f",quint64(score->tempomap()->size()),prepare,average,std::abs(actual-expected)*1000);
      QVERIFY2(average<100,"Long tempo curves must remain interactive");
      QVERIFY(saveScore(score,"rit-long-validation.mscx"));
      std::unique_ptr<MasterScore> restored(readCreatedScore("rit-long-validation.mscx")); QVERIFY(restored);
      auto curves=TempoExpression::curves(restored.get()); QCOMPARE(curves.size(),1); QCOMPARE(curves.front()->tick2().ticks(),end);
      QVERIFY(std::abs(restored->tempomap()->tick2time(end)-actual)<1e-8);
      }
