void TestPerformanceEditor::rollDraftReopenAndContinue()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);expressionShowMain080(main);
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));score->select(score->findCR(Fraction(),0));main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto bar=editor->findChild<RollWritingBar*>();auto canvas=editor->findChild<QWidget*>("performanceNoteCanvas");
    auto toggle=editor->findChild<QToolButton*>("rollWriteToggle");auto mode=bar->findChild<QComboBox*>("rollWriteMode");QVERIFY(bar&&canvas&&toggle&&mode);
    auto floatAction=editor->findChild<QAction*>("editorArea/float/performanceNotesArea");auto restore=editor->findChild<QAction*>("editorArea/restore/performanceNotesArea");QVERIFY(floatAction&&restore);
    floatAction->trigger();auto dock=qobject_cast<QDockWidget*>(canvas->parentWidget()->property("kumoDetachedDock").value<QObject*>());QVERIFY(dock);
    std::unique_ptr<QWidget,std::function<void(QWidget*)>> cleanup(editor,[&](QWidget*){rollWritingAction()->setChecked(false);restore->trigger();});
    dock->resize(QSize(980,650).boundedTo(dock->screen()->availableGeometry().size()-QSize(24,48)));QTest::qWait(100);
    if(!toggle->isChecked())QTest::mouseClick(toggle,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(bar->state()["ready"].toBool(),10000);mode->setCurrentIndex(1);QTRY_VERIFY(bar->state()["ready"].toBool());
    const auto scope=bar->state()["scope"].toMap();const auto original=rollNotationIdentity(score,scope);const int before=score->undoStack()->getCurIdx();
    const double x=bar->state()["fromX"].toDouble(),step=bar->state()["quarterPixels"].toDouble();
    const QPoint start(qRound(x+.5*step),canvas->height()/2),end(qRound(x+1.5*step),start.y());QVERIFY(canvas->rect().contains(start)&&canvas->rect().contains(end));
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(canvas,end,30);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,end);
    QTRY_VERIFY_WITH_TIMEOUT(bar->state()["ready"].toBool(),10000);QCOMPARE(score->undoStack()->getCurIdx(),before+1);QCOMPARE(rollNotationIdentity(score,scope),original);
    const auto draft=QJsonDocument::fromVariant(bar->state()["notes"]).toJson(QJsonDocument::Compact);QVERIFY(!draft.isEmpty());
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QFileInfo saved(output.filePath("roll-draft-087.mscz"));QVERIFY(score->saveCompressedFile(saved,false,false));
    rollWritingAction()->setChecked(false);mode->setCurrentIndex(0);
    auto reopened=main->readScore(saved.absoluteFilePath());QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));reopened->select(reopened->findCR(Fraction(),0));
    QTest::mouseClick(toggle,Qt::LeftButton);QTRY_VERIFY(bar->state()["ready"].toBool());
    auto savedButton=bar->findChild<QToolButton*>("rollWriteSaved");QVERIFY(savedButton&&savedButton->menu());
    auto menu=savedButton->menu();bool opened=false;QTimer::singleShot(0,menu,[&]{opened=menu->isVisible();for(auto action:menu->actions())if(action->isEnabled()&&action->text().startsWith(QString::fromUtf8("第 "))){QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());break;}QTimer::singleShot(150,menu,&QMenu::hide);});
    QTest::mouseClick(savedButton,Qt::LeftButton);QVERIFY(opened);
    QTRY_VERIFY_WITH_TIMEOUT(bar->state()["ready"].toBool(),10000);QCOMPARE(mode->currentIndex(),1);
    QCOMPARE(QJsonDocument::fromVariant(bar->state()["notes"]).toJson(QJsonDocument::Compact),draft);
    auto apply=bar->findChild<QToolButton*>("rollWriteApply");QTRY_VERIFY(apply->isEnabled());
    const auto count=bar->state()["notes"].toList().size();canvas->setFocus();QTest::keyClick(canvas,Qt::Key_A,Qt::ControlModifier);
    const int copied=bar->state()["selected"].toList().size();QVERIFY(copied>0);QTest::keyClick(canvas,Qt::Key_D,Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(bar->state()["ready"].toBool(),10000);QCOMPARE(bar->state()["notes"].toList().size(),count+copied);QCOMPARE(rollNotationIdentity(reopened,scope),original);
    const auto second=QJsonDocument::fromVariant(bar->state()["notes"]).toJson(QJsonDocument::Compact);
    const int unfinishedUndo=reopened->undoStack()->getCurIdx();QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(canvas,end,30);QTest::keyClick(canvas,Qt::Key_Z,Qt::ControlModifier);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,end);QCOMPARE(reopened->undoStack()->getCurIdx(),unfinishedUndo);QCOMPARE(QJsonDocument::fromVariant(bar->state()["notes"]).toJson(QJsonDocument::Compact),second);
    QFileInfo savedAgain(output.filePath("roll-draft-continued-087.mscz"));QVERIFY(reopened->saveCompressedFile(savedAgain,false,false));
    std::unique_ptr<MasterScore> check(main->readScore(savedAgain.absoluteFilePath()));QVERIFY(check);
    auto metadata=QJsonDocument::fromJson(check->metaTag("kumo.rollWriting").toUtf8()).toVariant().toMap();
    auto record=metadata["drafts"].toMap().value(metadata["activeId"].toString()).toMap();QCOMPARE(QJsonDocument::fromVariant(record["notes"]).toJson(QJsonDocument::Compact),second);
    const int adoption=reopened->undoStack()->getCurIdx();QTest::mouseClick(apply,Qt::LeftButton);QTRY_VERIFY(bar->state()["ready"].toBool());
    QCOMPARE(reopened->undoStack()->getCurIdx(),adoption+1);QVERIFY(rollNotationIdentity(reopened,scope)!=original);
    EditData edit;reopened->undoRedo(true,&edit);QTRY_COMPARE(rollNotationIdentity(reopened,scope),original);QTRY_VERIFY(apply->isEnabled());
    bool discarded=false;QTimer::singleShot(0,menu,[&]{for(auto action:menu->actions())if(action->isEnabled()&&action->text()==QString::fromUtf8("删除当前草稿")){discarded=true;QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());break;}QTimer::singleShot(150,menu,&QMenu::hide);});QTest::mouseClick(savedButton,Qt::LeftButton);QVERIFY(discarded);
    QTRY_VERIFY(bar->state()["ready"].toBool());QVERIFY(!apply->isEnabled());
    canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Z,Qt::ControlModifier);QTRY_VERIFY_WITH_TIMEOUT(bar->state()["ready"].toBool()&&apply->isEnabled(),10000);
    QCOMPARE(QJsonDocument::fromVariant(bar->state()["notes"]).toJson(QJsonDocument::Compact),second);
    QTest::qWait(100);dock->grab().save(output.filePath("roll-draft-reopened-087.png"));dock->resize(560,650);QTest::qWait(100);dock->grab().save(output.filePath("roll-draft-narrow-087.png"));
}

void TestPerformanceEditor::rollDraftAuditionStartsAtScope()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    auto score=expressionScore080(main,_directory.filePath("roll-phrase-087.mscx"));QVERIFY(score);expressionShowMain080(main);
    score->select(score->findCR(Fraction::fromTicks(3840),0));score->select(score->findCR(Fraction::fromTicks(5280),0),SelectType::RANGE);main->showPerformanceEditor(true);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
    std::unique_ptr<Seq,std::function<void(Seq*)>> sequenceGuard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();stopPlaybackPreview(true);s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    auto editor=main->performanceEditor();auto bar=editor->findChild<RollWritingBar*>();auto canvas=editor->findChild<QWidget*>("performanceNoteCanvas");auto toggle=editor->findChild<QToolButton*>("rollWriteToggle");QVERIFY(bar&&canvas&&toggle);
    auto restore=editor->findChild<QAction*>("editorArea/restore/performanceNotesArea");editor->findChild<QAction*>("editorArea/float/performanceNotesArea")->trigger();
    std::unique_ptr<QWidget,std::function<void(QWidget*)>> cleanup(editor,[&](QWidget*){rollWritingAction()->setChecked(false);restore->trigger();});
    auto dock=qobject_cast<QDockWidget*>(canvas->parentWidget()->property("kumoDetachedDock").value<QObject*>());QVERIFY(dock);dock->resize(1100,700);QTest::qWait(80);
    if(!toggle->isChecked())QTest::mouseClick(toggle,Qt::LeftButton);QTRY_VERIFY(bar->state()["ready"].toBool());
    auto mode=bar->findChild<QComboBox*>("rollWriteMode");mode->setCurrentIndex(1);QTRY_VERIFY(bar->state()["ready"].toBool());
    auto scope=bar->state()["scope"].toMap();QCOMPARE(scope["start"].toInt(),3840);
    for(auto button:editor->findChildren<QPushButton*>())if(button->text()==QString::fromUtf8("选区"))QTest::mouseClick(button,Qt::LeftButton);
    QTest::qWait(50);const double x=bar->state()["fromX"].toDouble(),step=bar->state()["quarterPixels"].toDouble();
    const QPoint start(qRound(x+.5*step),canvas->height()/2),end(qRound(x+1.25*step),start.y());qInfo()<<"Roll viewport"<<canvas->size()<<x<<step<<start<<end;QVERIFY(canvas->rect().contains(start)&&canvas->rect().contains(end));
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(canvas,end,30);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,end);
    QTRY_VERIFY_WITH_TIMEOUT(bar->state()["ready"].toBool(),10000);
    auto proposed=bar->findChild<QToolButton*>("rollWriteProposed"),original=bar->findChild<QToolButton*>("rollWriteOriginal"),stop=bar->findChild<QToolButton*>("rollWriteStop");QVERIFY(proposed&&original&&stop);QTRY_VERIFY(proposed->isEnabled());
    QTest::mouseClick(proposed,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);
    QCOMPARE(sequence->getCurTick(),3840);
    for(int i=0;i<15;++i){driver->pulse();QTest::qWait(2);}const int before=sequence->getCurTick();QTest::mouseClick(original,Qt::LeftButton);QTRY_VERIFY(sequence->isPlaying());QCOMPARE(sequence->getCurTick(),before);
    for(int i=0;i<120&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(2);}QTRY_VERIFY(!sequence->isPlaying());QTRY_VERIFY(!stop->isEnabled());
    const auto unchanged=rollNotationIdentity(score,scope);const int undo=score->undoStack()->getCurIdx();rollWritingAction()->setChecked(false);
    score->select(score->findCR(Fraction::fromTicks(3840),0));auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");tabs->setCurrentIndex(3);
    std::unique_ptr<QTabBar,std::function<void(QTabBar*)>> rhythmCleanup(tabs,[](QTabBar* t){t->setCurrentIndex(0);});
    auto pane=editor->findChild<RhythmPane*>();auto table=pane->findChild<QTableWidget*>("rhythmDraftTable");QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),4,10000);
    table->setCurrentCell(0,4);QTest::keyClick(table,Qt::Key_F2);auto line=table->findChild<QLineEdit*>();QVERIFY(line);QTest::keyClick(line,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(line,"110");QTest::keyClick(line,Qt::Key_Return);
    auto draft=pane->findChild<QPushButton*>("rhythm-audition");auto source=pane->findChild<QPushButton*>("rhythm-original");auto finish=pane->findChild<QPushButton*>("rhythm-stop");QTRY_VERIFY(draft->isEnabled());QTest::mouseClick(draft,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);QCOMPARE(sequence->getCurTick(),3840);QVERIFY(draft->isChecked());
    for(int i=0;i<10;++i){driver->pulse();QTest::qWait(2);}const int heard=sequence->getCurTick();QTest::mouseClick(source,Qt::LeftButton);QTRY_VERIFY(sequence->isPlaying());QCOMPARE(sequence->getCurTick(),heard);QVERIFY(source->isChecked());QTest::mouseClick(finish,Qt::LeftButton);QTRY_VERIFY(!sequence->isPlaying());QTRY_VERIFY(!finish->isEnabled());QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(rollNotationIdentity(score,scope),unchanged);
}

void TestPerformanceEditor::rhythmShowsActualLengths()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);
    auto score=expressionScore080(main,_directory.filePath("rhythm-lengths-087.mscx"));QVERIFY(score);expressionShowMain080(main);
    score->select(score->findCR(Fraction(),0));main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(3);
    std::unique_ptr<QTabBar,std::function<void(QTabBar*)>> cleanup(tabs,[](QTabBar* t){t->setCurrentIndex(0);});
    auto pane=editor->findChild<RhythmPane*>();QVERIFY(pane);auto table=pane->findChild<QTableWidget*>("rhythmDraftTable");QVERIFY(table);
    QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),4,10000);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));pane->grab().save(output.filePath("rhythm-lengths-087.png"));
    for(int i=0;i<4;++i)QVERIFY2(table->item(i,3)->text().toDouble()>0,"显示实际音长，避免把自动时值写成0");
}

void TestPerformanceEditor::rhythmEditApplyContinueAndUndo()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);expressionShowMain080(main);
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/pluginhost/rhythm.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    score->select(score->findCR(Fraction(),0));main->showPerformanceEditor(true);auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(3);
    std::unique_ptr<QTabBar,std::function<void(QTabBar*)>> cleanup(tabs,[](QTabBar* t){t->setCurrentIndex(0);});
    auto pane=editor->findChild<RhythmPane*>();QVERIFY(pane);auto table=pane->findChild<QTableWidget*>("rhythmDraftTable");auto apply=pane->findChild<QPushButton*>("rhythmApply");auto canvas=pane->findChild<QWidget*>("rhythmDraftCanvas");QVERIFY(table&&apply&&canvas);
    QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),4,10000);QVERIFY(!apply->isEnabled());
    const auto scope=QVariantMap{{"start",0},{"end",1920},{"firstTrack",0},{"endTrack",1}};const auto original=rollNotationIdentity(score,scope);const int before=score->undoStack()->getCurIdx();
    const double rowStep=qMax(2.,double(canvas->height()-30)/10.);const QPoint press(52+qRound((canvas->width()-64)*.05),qRound(26+7*rowStep+qMax(4.,rowStep-1)/2));QVERIFY(canvas->rect().contains(press));
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,press);QTest::mouseMove(canvas,press+QPoint(90,0),30);QTest::keyClick(canvas,Qt::Key_Escape);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,press+QPoint(90,0));QTRY_COMPARE(table->item(0,2)->text(),QString("1"));QTRY_VERIFY(!apply->isEnabled());QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(rollNotationIdentity(score,scope),original);
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,press);QTest::mouseMove(canvas,press+QPoint(90,0),30);QTest::keyClick(canvas,Qt::Key_Z,Qt::ControlModifier);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,press+QPoint(90,0));QTRY_VERIFY(!apply->isEnabled());QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(rollNotationIdentity(score,scope),original);
    auto edit=[&](int row,int col,QString text){table->scrollToItem(table->item(row,col));table->setCurrentCell(row,col);QTest::keyClick(table,Qt::Key_F2);auto line=table->findChild<QLineEdit*>();QVERIFY(line);QTest::keyClick(line,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(line,text);QTest::keyClick(line,Qt::Key_Return);};
    edit(0,3,"0.5");QTRY_COMPARE(table->item(0,3)->text(),QString("0.5"));QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);QCOMPARE(score->undoStack()->getCurIdx(),before);
    canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Z,Qt::ControlModifier);QTRY_COMPARE(table->item(0,3)->text(),QString("1"));QTRY_VERIFY(!apply->isEnabled());
    QTest::keyClick(canvas,Qt::Key_Y,Qt::ControlModifier);QTRY_COMPARE(table->item(0,3)->text(),QString("0.5"));QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);
    edit(0,4,"90");QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);QCOMPARE(rollNotationIdentity(score,scope),original);
    QSignalSpy clicked(apply,&QPushButton::clicked);QTest::mouseClick(apply,Qt::LeftButton);QCOMPARE(clicked.size(),1);QTRY_VERIFY2_WITH_TIMEOUT(pane->findChild<QLabel*>("rhythmStatus")->text().contains(QString::fromUtf8("已采用")),qUtf8Printable(pane->findChild<QLabel*>("rhythmStatus")->text()),10000);
    QCOMPARE(score->undoStack()->getCurIdx(),before+1);auto first=toChord(score->findCR(Fraction(),0));QCOMPARE(first->actualTicks().ticks(),240);QCOMPARE(first->notes()[0]->veloOffset(),90);QVERIFY(score->findCR(Fraction::fromTicks(240),0)->isRest());QVERIFY(!apply->isEnabled());
    auto second=toChord(score->findCR(Fraction::fromTicks(480),0));const int oldType=int(second->notes()[0]->veloType()),oldRaw=second->notes()[0]->veloOffset(),oldDisplay=table->item(1,4)->text().toInt();
    edit(1,4,"102");QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);QTest::mouseClick(apply,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(pane->findChild<QLabel*>("rhythmStatus")->text().contains(QString::fromUtf8("已采用")),10000);
    QCOMPARE(score->undoStack()->getCurIdx(),before+2);second=toChord(score->findCR(Fraction::fromTicks(480),0));QCOMPARE(second->notes()[0]->veloOffset(),102);QCOMPARE(toChord(score->findCR(Fraction(),0))->notes()[0]->veloOffset(),90);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QFileInfo saved(output.filePath("rhythm-continued-087.mscz"));QVERIFY(score->saveCompressedFile(saved,false,false));std::unique_ptr<MasterScore> reopened(main->readScore(saved.absoluteFilePath()));QVERIFY(reopened);QCOMPARE(toChord(reopened->findCR(Fraction(),0))->actualTicks().ticks(),240);QCOMPARE(toChord(reopened->findCR(Fraction::fromTicks(480),0))->notes()[0]->veloOffset(),102);
    pane->grab().save(output.filePath("rhythm-continued-087.png"));
    canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Z,Qt::ControlModifier);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+1);second=toChord(score->findCR(Fraction::fromTicks(480),0));QCOMPARE(int(second->notes()[0]->veloType()),oldType);QCOMPARE(second->notes()[0]->veloOffset(),oldRaw);
    QTRY_COMPARE_WITH_TIMEOUT(table->item(1,4)->text().toInt(),oldDisplay,10000);
}

void TestPerformanceEditor::rhythmPreservesSourceDurationsAndVelocities()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);expressionShowMain080(main);
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/pluginhost/rhythm.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    auto first=toChord(score->findCR(Fraction(),0)),second=toChord(score->findCR(Fraction::fromTicks(480),0));
    auto head=first->notes()[0],tail=second->notes()[0];tail->setPitch(60);tail->setTpc1(14);tail->setTpc2(14);
    head->setVeloType(Note::ValueType::OFFSET_VAL);head->setVeloOffset(10);
    auto extra=new Note(score);extra->setTrack(0);extra->setParent(first);extra->setPitch(64);extra->setTpc1(18);extra->setTpc2(18);extra->setVeloType(Note::ValueType::USER_VAL);extra->setVeloOffset(52);first->add(extra);
    auto tie=new Tie(score);tie->setStartNote(head);tie->setEndNote(tail);tie->setTrack(0);score->startCmd();score->undoAddElement(tie);score->endCmd();score->doLayout();
    auto events=[&](){auto material=renderPlaybackPreview(score,score);QStringList result;for(const auto& value:material.events){const auto& event=value.second;if(event.type()==ME_NOTEON||event.type()==ME_NOTEOFF)result<<QString("%1:%2:%3:%4:%5").arg(value.first).arg(event.type()).arg(event.channel()).arg(event.pitch()).arg(event.pitch()==65&&event.velo()>0?1:event.velo());}return result;};
    const auto original=events();score->select(first);main->showPerformanceEditor(true);auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");tabs->setCurrentIndex(3);std::unique_ptr<QTabBar,std::function<void(QTabBar*)>> cleanup(tabs,[](QTabBar* t){t->setCurrentIndex(0);});
    auto pane=editor->findChild<RhythmPane*>();auto table=pane->findChild<QTableWidget*>("rhythmDraftTable");auto apply=pane->findChild<QPushButton*>("rhythmApply");QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),3,10000);QCOMPARE(table->item(0,3)->text(),QString("1"));
    table->setCurrentCell(2,4);table->scrollToItem(table->item(2,4));QTest::keyClick(table,Qt::Key_F2);auto line=table->findChild<QLineEdit*>();QVERIFY(line);QTest::keyClick(line,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(line,"105");QTest::keyClick(line,Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);QTest::mouseClick(apply,Qt::LeftButton);QTRY_VERIFY2_WITH_TIMEOUT(pane->findChild<QLabel*>("rhythmStatus")->text().contains(QString::fromUtf8("已采用")),qUtf8Printable(pane->findChild<QLabel*>("rhythmStatus")->text()),10000);
    first=toChord(score->findCR(Fraction(),0));second=toChord(score->findCR(Fraction::fromTicks(480),0));QCOMPARE(first->notes().size(),size_t(2));QCOMPARE(second->notes().size(),size_t(1));
    head=first->notes()[0];QCOMPARE(head->pitch(),60);QCOMPARE(int(head->veloType()),int(Note::ValueType::OFFSET_VAL));QCOMPARE(head->veloOffset(),10);QVERIFY(head->tieFor());QCOMPARE(head->tieFor()->endNote(),second->notes()[0]);QCOMPARE(first->notes()[1]->veloOffset(),52);QCOMPARE(int(first->notes()[1]->veloType()),int(Note::ValueType::USER_VAL));QCOMPARE(toChord(score->findCR(Fraction::fromTicks(1440),0))->notes()[0]->veloOffset(),105);
    QCOMPARE(events(),original);
}
