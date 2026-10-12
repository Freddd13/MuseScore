// Exercise selection, modifiers and folded-row hit testing through real widgets.
void TestPerformanceEditor::editorUsabilityRepairs()
      {
      auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
      main->setCurrentScoreView(main->appendScore(score));auto view=main->currentScoreView();
      PerformanceEditor editor;editor.resize(980,720);editor.setView(view);editor.show();QTest::qWait(100);
      auto chord=noteAt(score)->chord()->notes();QVERIFY(chord.size()>=2);
      QMap<Note*,VelocityEdit> edits;for(int i=0;i<chord.size();++i)edits[chord[i]]={Note::ValueType::USER_VAL,20+i*30};
      QVERIFY(ParameterEdit::velocities(score,edits));QTest::qWait(80);editor.fit();
      auto canvas=editor.findChild<QWidget*>("performanceParameterCanvas");auto keys=editor.findChild<QComboBox*>("performanceKeyboardMode");auto axis=editor.findChild<QComboBox*>("performanceAxis");auto sensitivity=editor.findChild<QComboBox*>("performanceDragSensitivity");
      QVERIFY(canvas&&keys&&axis&&sensitivity);axis->setCurrentIndex(0);keys->setCurrentIndex(0);sensitivity->setCurrentIndex(2);QTest::qWait(30);
      auto endpoint=[canvas](int value){return QPoint(76+qRound((canvas->width()-90)/4.),qRound(canvas->height()-6-(value-1)*(canvas->height()-12)/126.));};
      PerformanceSelection::apply(score,{chord[0]},chord[0]);editor.selectionChanged();
      const int undoBefore=score->undoStack()->getCurIdx();
      // The pointer is exactly at the other note's endpoint: the selected note
      // at this same onset still wins for a drag, and never changes mid-gesture.
      QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,endpoint(50));
      QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,endpoint(50)-QPoint(0,24));QTest::qWait(80);
      QVERIFY(chord[0]->veloOffset()>20);QCOMPARE(chord[1]->veloOffset(),50);QCOMPARE(score->undoStack()->getCurIdx(),undoBefore+1);
      const int firstChange=chord[0]->veloOffset()-20;
      QVERIFY(ParameterEdit::velocities(score,edits));QTest::qWait(80);sensitivity->setCurrentIndex(1);
      QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,endpoint(50));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,endpoint(50)-QPoint(0,24));QTest::qWait(80);
      QVERIFY(std::abs((chord[0]->veloOffset()-20)*2-firstChange)<=1);QCOMPARE(chord[1]->veloOffset(),50);
      sensitivity->setCurrentIndex(2);const int value=chord[0]->veloOffset();
      QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,endpoint(50));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,endpoint(50)-QPoint(0,2));QTest::qWait(50);QCOMPARE(chord[0]->veloOffset(),value);
      score->setPlayPos(Fraction::fromTicks(1440));
      QTest::mouseDClick(canvas,Qt::LeftButton,Qt::AltModifier,endpoint(50));QCOMPARE(score->playPos().ticks(),1440);
      auto actual=editor.findChild<QCheckBox*>("performanceActualVelocity");QVERIFY(actual);axis->setCurrentIndex(1);
      const auto displayState=score->state();const auto oldType=chord[0]->veloType();actual->setChecked(true);QTest::qWait(20);QVERIFY(score->state()==displayState);QCOMPARE(chord[0]->veloType(),oldType);
      QCOMPARE(editor.findChild<QDoubleSpinBox*>("performanceValue")->suffix(),QString());actual->setChecked(false);axis->setCurrentIndex(0);
      // Pick one voice using the modal matrix. Its native selection is retained.
      QTimer dialogTimer;connect(&dialogTimer,&QTimer::timeout,&editor,[&]{auto table=editor.findChild<QTableWidget*>("performanceTrackTable");if(!table)return;
            for(int row=0;row<table->rowCount();++row)for(int v=0;v<4;++v)table->item(row,v)->setCheckState(row==0&&v==0?Qt::Checked:Qt::Unchecked);
            auto dialog=qobject_cast<QDialog*>(table->window());if(dialog)dialog->accept();});dialogTimer.start(20);
      QTest::mouseClick(editor.findChild<QToolButton*>("performanceTrackFilter"),Qt::LeftButton);dialogTimer.stop();
      keys->setCurrentIndex(2);QTest::qWait(20);
      PerformanceViewport folded;folded.pitchRows={36,48,61,72};folded.topPitch=72;
      for(int pitch:folded.pitchRows){QCOMPARE(folded.pitchAtIndex(folded.pitchIndex(pitch)),double(pitch));}
      QCOMPARE(folded.pitchIndex(72)-folded.pitchIndex(61),1.);QCOMPARE(folded.pitchIndex(61)-folded.pitchIndex(48),1.);
      auto upper=editor.findChild<QWidget*>("performanceNoteCanvas");QVERIFY(upper);QVERIFY(upper->findChild<QScrollBar*>()->maximum()<12700);
      const auto pitchBar=upper->findChild<QScrollBar*>();const double row=upper->height()*100./pitchBar->pageStep();
      QTest::mouseClick(upper,Qt::LeftButton,Qt::NoModifier,QPoint(endpoint(20).x()+5,qRound(row/2)));QVERIFY(chord.back()->selected());
      editor.grab().save("manual-editor-033.png");keys->setCurrentIndex(1);
      PerformanceAppearance appearance;appearance.mode=1;QCOMPARE(appearance.noteColor(60,0),MScore::selectColor[0]);appearance.followNativeVoices=false;QCOMPARE(appearance.noteColor(60,0),appearance.voices[0]);
      // Hold display does not mutate notation and is removed on release.
      for(auto check:editor.findChildren<QCheckBox*>())check->setChecked(false);editor.findChild<QToolButton*>("performanceWheelVelocity")->setChecked(false);
      auto state=score->state();view->setFocus();view->repaint();auto before=view->grab().toImage();
      QTest::keyPress(view,Qt::Key_V,Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier);view->repaint();QVERIFY(before!=view->grab().toImage());QVERIFY(score->state()==state);
      QTest::keyRelease(view,Qt::Key_V,Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier);view->repaint();QCOMPARE(view->grab().toImage(),before);
      editor.findChild<QAction*>("performanceBandPosition0")->trigger();editor.findChild<QAction*>("performanceBandHeight48")->trigger();editor.setScoreBandEnabled(true);view->repaint();
      const QPoint edge(qRound(view->matrix().mapRect(chord[0]->canvasBoundingRect()).center().x()+20),view->height()-100);
      QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,edge);QMouseEvent resize(QEvent::MouseMove,QPointF(edge-QPoint(0,20)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&resize);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,resize.pos());QCOMPARE(QSettings().value("performanceEditor/bandHeight").toInt(),68);QVERIFY(score->state()==state);
      editor.findChild<QAction*>("performanceBandHeight48")->trigger();editor.setScoreBandEnabled(false);editor.hide();
      const QPoint head=view->matrix().mapRect(chord[0]->canvasBoundingRect()).center().toPoint();const auto matrix=view->matrix();const int raw=chord[0]->veloOffset();
      QWheelEvent wheel(QPointF(head+QPoint(20,0)),view->mapToGlobal(head+QPoint(20,0)),QPoint(),QPoint(0,120),Qt::NoButton,Qt::AltModifier|Qt::ShiftModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(view,&wheel);
      QVERIFY(editor.hasPending());QTest::qWait(380);QCOMPARE(chord[0]->veloOffset(),raw+1);QCOMPARE(view->matrix(),matrix);
      // The keyboard and visible-note filters did not silently remove score selection.
      QVERIFY(!score->selection().noteList().empty());QSettings().setValue("performanceEditor/keyboardMode",1);QSettings().setValue("performanceEditor/dragSensitivity",1.);
      }
