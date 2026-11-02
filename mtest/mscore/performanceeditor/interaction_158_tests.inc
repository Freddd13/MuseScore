// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
void TestPerformanceEditor::wheelSelectionCommit158()
{
    expressionHost080(_directory.path()); auto main=Ms::mscore; QVERIFY(main);
    auto score=expressionScore080(main,_directory.filePath("wheel-selection-158.mscx")); QVERIFY(score);
    expressionShowMain080(main); main->showPerformanceEditor(true);
    auto editor=main->performanceEditor(); auto view=main->currentScoreView();
    editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    editor->findChild<QToolButton*>("performanceWheelVelocity")->setChecked(true);
    editor->setScoreBandEnabled(false);
    if(auto dock=main->findChild<QDockWidget*>("performanceEditorDock")) main->resizeDocks({dock},{280},Qt::Vertical);
    QTest::qWait(150);
    auto a=noteAt(score,0), b=noteAt(score,480), c=noteAt(score,960);
    QVERIFY(PerformanceSelection::apply(score,{a,b},a));
    const int original=a->veloOffset(), undo=score->undoStack()->getCurIdx();
    const auto head=view->matrix().mapRect(a->canvasBoundingRect()).center();
    QWheelEvent wheel(head,view->mapToGlobal(head.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(view,&wheel);
    QVERIFY(editor->hasPending());
    const auto other=view->matrix().mapRect(c->canvasBoundingRect()).center().toPoint();
    qInfo()<<"native158"<<view->size()<<"local"<<other<<"main"<<view->mapTo(main,other)<<"hit"<<QApplication::widgetAt(view->mapToGlobal(other));
    QVERIFY(main->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path())+"/t0-wheel-before-158.png"));
    QVERIFY(view->rect().contains(other));
    // Deliver through the top-level native window, as Windows does, rather
    // than bypassing native dispatch with a direct QWidget mouse event.
    auto native=main->windowHandle();QVERIFY(native);
    QTest::mouseClick(native,Qt::LeftButton,Qt::NoModifier,view->mapTo(main,other));
    QTest::qWait(450);
    qInfo()<<"wheel158"<<a->veloOffset()<<original<<"pending"<<editor->hasPending()<<"grab"<<QWidget::mouseGrabber()<<"selected"<<score->selection().noteList().size()<<"undo"<<score->undoStack()->getCurIdx()<<undo;
    QVERIFY(a->veloOffset()!=original);
    QVERIFY(!editor->hasPending());
    QVERIFY(!QWidget::mouseGrabber());
    const auto selected=score->selection().noteList();
    QVERIFY(std::find(selected.begin(),selected.end(),c)!=selected.end());
    QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
    score->undoRedo(true,&ed);QCOMPARE(a->veloOffset(),original);
    score->undoRedo(false,&ed);QVERIFY(a->veloOffset()!=original);
    const int beforeUndo=a->veloOffset();
    Shortcut::load();
    for(const char* id:{"undo","redo"}) {auto shortcut=Shortcut::getShortcut(id);shortcut->setStandardKey(shortcut->standardKey());}
    QVERIFY(!getAction("undo")->shortcut().isEmpty());main->activateWindow();view->setFocus();QTest::qWait(60);
    QWheelEvent next(head,view->mapToGlobal(head.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(view,&next);QVERIFY(editor->hasPending());
    QTest::keyClick(view,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(350);
    QCOMPARE(a->veloOffset(),beforeUndo);QVERIFY(!editor->hasPending());
    QVERIFY(main->saveFile());
    std::unique_ptr<MasterScore> saved(main->readScore(_directory.filePath("wheel-selection-158.mscx")));QVERIFY(saved);
    QCOMPARE(noteAt(saved.get(),0)->veloOffset(),beforeUndo);
}

void TestPerformanceEditor::interruptedGestureWheel158()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;
    auto score=expressionScore080(main,_directory.filePath("interrupted-wheel-158.mscx"));QVERIFY(score);
    expressionShowMain080(main);main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto view=main->currentScoreView();
    editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    editor->findChild<QToolButton*>("performanceWheelVelocity")->setChecked(true);
    if(auto dock=main->findChild<QDockWidget*>("performanceEditorDock"))main->resizeDocks({dock},{300},Qt::Vertical);
    QTest::qWait(120);
    auto canvas=editor->findChild<QWidget*>("performanceParameterCanvas");QVERIFY(canvas&&canvas->height()>20);
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(180,canvas->height()-12));
    QCOMPARE(QWidget::mouseGrabber(),canvas);
    // A wheel event with no held button proves that the old pointer gesture
    // has ended, even if its release was consumed by a native window.
    auto a=noteAt(score,0);const auto head=view->matrix().mapRect(a->canvasBoundingRect()).center();
    QWheelEvent wheel(head,view->mapToGlobal(head.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(view,&wheel);
    const bool released=!QWidget::mouseGrabber();
    qInfo()<<"interrupted wheel158 grab"<<QWidget::mouseGrabber();
    QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(180,canvas->height()-12));
    QVERIFY2(released,"松键后的滚轮输入应结束旧手势并归还鼠标");
}

void TestPerformanceEditor::wheelOtherNoteEdit158()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;
    auto score=expressionScore080(main,_directory.filePath("wheel-other-edit-158.mscx"));QVERIFY(score);
    expressionShowMain080(main);main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto view=main->currentScoreView();
    editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    editor->findChild<QToolButton*>("performanceWheelVelocity")->setChecked(true);
    QTest::qWait(120);
    auto a=noteAt(score,0),b=noteAt(score,480);const int original=a->veloOffset();
    const auto head=view->matrix().mapRect(a->canvasBoundingRect()).center();
    QWheelEvent wheel(head,view->mapToGlobal(head.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(view,&wheel);QVERIFY(editor->hasPending());
    QVERIFY(ParameterEdit::velocities(score,{{b,{Note::ValueType::USER_VAL,57}}}));
    score->select(b);main->endCmd();QTest::qWait(450);
    QCOMPARE(b->veloOffset(),57);
    QVERIFY2(a->veloOffset()!=original,"另一个音的修改应保留这次滚轮调整");
    QVERIFY(!editor->hasPending());QVERIFY(!QWidget::mouseGrabber());
    QWheelEvent again(head,view->mapToGlobal(head.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(view,&again);QVERIFY(editor->hasPending());
    QVERIFY(ParameterEdit::velocities(score,{{a,{Note::ValueType::USER_VAL,76}}}));
    QTest::qWait(450);QCOMPARE(a->veloOffset(),76);QVERIFY(!editor->hasPending());
    QWheelEvent beforeDelete(head,view->mapToGlobal(head.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(view,&beforeDelete);QVERIFY(editor->hasPending());
    const int beforeDeleteUndo=score->undoStack()->getCurIdx();score->select(a);view->cmd("delete");QTest::qWait(450);
    QVERIFY(!editor->hasPending());QCOMPARE(score->undoStack()->getCurIdx(),beforeDeleteUndo+1);
    score->undoRedo(true,&ed);main->endCmd();QTest::qWait(60);
    auto restored=score->findCR(Fraction::fromTicks(0),0);QVERIFY(restored&&restored->isChord());
    QCOMPARE(toChord(restored)->notes().front()->veloOffset(),76);
    auto current=toChord(restored)->notes().front();
    const auto currentHead=view->matrix().mapRect(current->canvasBoundingRect()).center();
    QWheelEvent beforeHide(currentHead,view->mapToGlobal(currentHead.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(view,&beforeHide);QVERIFY(editor->hasPending());
    QVERIFY(ParameterEdit::velocities(score,{{b,{Note::ValueType::USER_VAL,58}}}));
    main->hide();QTest::qWait(800);
    const bool committedWhileHidden=!editor->hasPending();main->show();QTest::qWait(50);
    QVERIFY2(committedWhileHidden,"收起窗口后结束尚未提交的滚轮调整");
    QCOMPARE(current->veloOffset(),77);QCOMPARE(b->veloOffset(),58);
}

void TestPerformanceEditor::playBoundaryKeepsGesture158()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;
    auto score=expressionScore080(main,_directory.filePath("play-boundary-158.mscx"));QVERIFY(score);
    expressionShowMain080(main);main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto view=main->currentScoreView();
    editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    QCheckBox* handles=nullptr;for(auto check:editor->findChildren<QCheckBox*>())if(check->text()==QString::fromUtf8("音旁手柄"))handles=check;
    QVERIFY(handles);handles->setChecked(true);
    editor->setScoreBandEnabled(false);QTest::qWait(120);
    auto note=noteAt(score,0),otherNote=noteAt(score,480);const int original=note->veloOffset(),otherOriginal=otherNote->veloOffset();
    QVERIFY(PerformanceSelection::apply(score,{note,otherNote},note));view->repaint();
    const auto handle=(view->matrix().mapRect(note->canvasBoundingRect()).topRight()+QPointF(12,-12)).toPoint();
    QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,handle);
    QCOMPARE(QWidget::mouseGrabber(),view);
    QMouseEvent move(QEvent::MouseMove,handle-QPoint(0,20),view->mapToGlobal(handle-QPoint(0,20)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(view,&move);QTest::qWait(25);QVERIFY(editor->hasPending());
    main->scoreStateChanged(STATE_PLAY);QTest::qWait(50);
    QVERIFY(!QWidget::mouseGrabber());QVERIFY(!editor->hasPending());
    QVERIFY2(note->veloOffset()!=original,"开始播放时保留已拖出的力度");
    QVERIFY(otherNote->veloOffset()!=otherOriginal);
    QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,handle-QPoint(0,20));
}
