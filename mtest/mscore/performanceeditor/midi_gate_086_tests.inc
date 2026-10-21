void TestPerformanceEditor::midiGateHistoryAndLayout()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);expressionShowMain080(main);
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    auto pedal=new Pedal(score);pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1920));
    pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);score->setPlaylistDirty();
    auto first=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->notes().front();
    score->startCmd();first->undoChangeProperty(Pid::COLOR,QColor(Qt::darkRed));score->endCmd();
    for(const char* key:{"export/midi/cropFromMeasure","export/midi/cropUntilMeasure"})preferences.setPreference(key,0);
    preferences.setPreference("export/midi/cropProtectMelody",false);preferences.setPreference("export/midi/cropEnabled",true);
    preferences.setPreference("export/midi/cropVoices",QString());preferences.setPreference("export/midi/cropProtectedVoices",QString());
    main->showPerformanceEditor(true);auto editor=main->performanceEditor();QVERIFY(editor);
    const auto parent=editor->parentWidget();const bool hidden=editor->isHidden();
    const int undo=score->undoStack()->getCurIdx();const auto state=score->state();const int tabs=main->scores().size();
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score};});panel.show();
    QTimer timer,modeTimer;timer.setInterval(50);modeTimer.setInterval(50);
    bool busy=false,done=false,modeOpened=false,modeReturned=false;int tries=0;QStringList issues;
    QMap<QString,int> edited;
    connect(&modeTimer,&QTimer::timeout,[&]{
        auto window=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!window||window->objectName()!="midiCropEditorWindow")return;
        modeTimer.stop();modeOpened=window->findChild<PerformanceEditor*>()==editor;
        auto roll=window->findChild<QAbstractScrollArea*>("midiCropRoll");
        if(roll) {
            if(roll->palette().color(QPalette::Base)!=editor->palette().color(QPalette::Base))issues<<"editor graph uses another theme";
            roll->setFocus();QTest::keyClick(roll,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(80);
            if(!panel.options(score).gate.releaseOverrides.isEmpty())issues<<"editor mode undo did not restore the release";
            for(int i=0;i<100&&!roll->isEnabled();++i)QTest::qWait(10);
            QTest::keyClick(roll,Qt::Key_Y,Qt::ControlModifier);QTest::qWait(80);
            if(panel.options(score).gate.releaseOverrides!=edited)issues<<"editor mode redo did not restore the release";
        }
        window->grab().save(output.filePath("midi-editor-086.png"));
        auto back=window->findChild<QPushButton*>("midiCropEditorReturn");
        if(!back){issues<<"missing editor return";window->reject();return;}
        QTest::mouseClick(back,Qt::LeftButton);
    });
    connect(&timer,&QTimer::timeout,[&]{
        if(busy)return;QScopedValueRollback<bool> running(busy,true);
        auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog||dialog->objectName()!="midiCropComparison")return;
        if(++tries>180){issues<<"comparison never became ready";timer.stop();dialog->reject();return;}
        auto table=dialog->findChild<QTableView*>();auto roll=dialog->findChild<QAbstractScrollArea*>("midiCropRoll");
        if(!table||!roll||!roll->isEnabled()||!table->model()->rowCount())return;
        timer.stop();dialog->resize(QSize(680,620).boundedTo(dialog->screen()->availableGeometry().size()-QSize(16,48)));QTest::qWait(100);
        qInfo("MIDI comparison requested width 680, actual %d, minimum hint %d",dialog->width(),dialog->minimumSizeHint().width());
        if(dialog->width()>700)issues<<"comparison cannot shrink to a normal side-by-side window";
        int row=-1;
        for(int i=0;i<table->model()->rowCount();++i)
            if(table->model()->index(i,3).data().toInt()<table->model()->index(i,2).data().toInt()){row=i;break;}
        if(row<0){issues<<"no proposed shortening";dialog->reject();return;}
        const auto cell=table->model()->index(row,0);table->scrollTo(cell);
        QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(cell).center());
        auto fit=dialog->findChild<QPushButton*>("midiCropFit");
        if(!fit){issues<<"missing fit button";dialog->reject();return;}QTest::mouseClick(fit,Qt::LeftButton);QTest::qWait(80);
        auto point=roll->property("selectedNewOffHandle").toPoint();
        if(!roll->viewport()->rect().contains(point)){issues<<"new release is outside the visible canvas";dialog->reject();return;}
        const auto previous=panel.options(score).gate.releaseOverrides;
        QTest::mousePress(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point);
        const auto target=point-QPoint(18,0);
        QMouseEvent move(QEvent::MouseMove,target,target,roll->viewport()->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(roll->viewport(),&move);QApplication::sendEvent(roll->viewport(),&move);
        QTest::mouseRelease(roll->viewport(),Qt::LeftButton,Qt::NoModifier,target);
        for(int i=0;i<100&&(!roll->isEnabled()||panel.options(score).gate.releaseOverrides==previous);++i)QTest::qWait(10);
        edited=panel.options(score).gate.releaseOverrides;
        if(edited==previous){issues<<"mouse edit did not reach the export";dialog->reject();return;}
        auto fold=dialog->findChild<QCheckBox*>("midiCropFoldPitches");
        auto listToggle=dialog->findChild<QToolButton*>("midiCropListToggle");
        auto splitter=dialog->findChild<QSplitter*>("midiCropWorkspace");
        if(!fold||!listToggle||!splitter)issues<<"view controls are missing";
        else {
            if(!fold->isChecked())QTest::mouseClick(fold,Qt::LeftButton);QTest::qWait(40);
            if(roll->verticalScrollBar()->maximum()!=0)issues<<"folded fit hides the bass or upper notes";
            QTest::mouseClick(listToggle,Qt::LeftButton);QTest::qWait(20);
            if(!table->isHidden())issues<<"note list cannot close";
            QTest::mouseClick(listToggle,Qt::LeftButton);QTest::qWait(20);
            if(table->isHidden())issues<<"note list cannot reopen";
            const auto before=splitter->sizes();auto handle=splitter->handle(1);const auto start=handle->rect().center();
            QTest::mousePress(handle,Qt::LeftButton,Qt::NoModifier,start);
            const auto end=start+QPoint(0,30);QMouseEvent slide(QEvent::MouseMove,end,end,handle->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(handle,&slide);QTest::mouseRelease(handle,Qt::LeftButton,Qt::NoModifier,end);
            if(splitter->sizes()==before)issues<<"graph and list divider cannot drag";
        }
        dialog->grab().save(output.filePath("midi-comparison-086.png"));
        auto cancelDrag=[&](int key,Qt::KeyboardModifiers modifiers) {
            const auto at=roll->property("selectedNewOffHandle").toPoint();
            QTest::mousePress(roll->viewport(),Qt::LeftButton,Qt::NoModifier,at);
            const auto to=at+QPoint(12,0);QMouseEvent motion(QEvent::MouseMove,to,to,roll->viewport()->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(roll->viewport(),&motion);roll->setFocus();QTest::keyClick(roll,Qt::Key(key),modifiers);
            QTest::mouseRelease(roll->viewport(),Qt::LeftButton,Qt::NoModifier,to);
            if(panel.options(score).gate.releaseOverrides!=edited)issues<<"canceling a drag changed earlier export edits";
        };
        cancelDrag(Qt::Key_Escape,Qt::NoModifier);cancelDrag(Qt::Key_Z,Qt::ControlModifier);
        roll->setFocus();QTest::keyClick(roll,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(80);
        if(panel.options(score).gate.releaseOverrides!=previous)issues<<"Ctrl+Z does not undo the exported release";
        else {
            for(int i=0;i<100&&!roll->isEnabled();++i)QTest::qWait(10);
            QTest::keyClick(roll,Qt::Key_Y,Qt::ControlModifier);QTest::qWait(80);
            if(panel.options(score).gate.releaseOverrides!=edited)issues<<"Ctrl+Y does not restore the exported release";
        }
        if(score->undoStack()->getCurIdx()!=undo||score->state()!=state)issues<<"export undo changed the score";
        for(int i=0;i<100&&!roll->isEnabled();++i)QTest::qWait(10);
        auto exclude=dialog->findChild<QPushButton*>("midiCropExclude");
        if(!exclude||!exclude->isEnabled())issues<<"selected note cannot be excluded";
        else {
            QTest::mouseClick(exclude,Qt::LeftButton);QTest::qWait(70);
            if(panel.options(score).gate.excluded.isEmpty())issues<<"exclude did not change this export";
            for(int i=0;i<100&&!roll->isEnabled();++i)QTest::qWait(10);
            roll->setFocus();QTest::keyClick(roll,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(70);
            if(!panel.options(score).gate.excluded.isEmpty()||panel.options(score).gate.releaseOverrides!=edited)issues<<"undo exclusion lost the release edit";
            for(int i=0;i<100&&!roll->isEnabled();++i)QTest::qWait(10);
        }
        auto open=dialog->findChild<QPushButton*>("midiCropEditorMode");
        if(!open||!open->isEnabled()){issues<<"editor mode is unavailable";dialog->reject();return;}
        modeTimer.start();QTest::mouseClick(open,Qt::LeftButton);
        modeReturned=editor->parentWidget()==parent&&editor->isHidden()==hidden&&roll->window()==dialog;
        done=true;dialog->accept();
    });
    timer.start();auto compare=panel.findChild<QPushButton*>("midiCropCompare");QVERIFY(compare);
    QTest::mouseClick(compare,Qt::LeftButton);timer.stop();modeTimer.stop();
    const auto json=QJsonDocument(QJsonObject{{"issues",QJsonArray::fromStringList(issues)},
        {"editorOpened",modeOpened},{"editorReturned",modeReturned},{"manualCount",edited.size()}}).toJson();
    QFile report(output.filePath("midi-comparison-086.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(json);report.close();
    QVERIFY2(done,qPrintable(issues.join("; ")));QVERIFY(modeOpened&&modeReturned);
    const auto proposed=output.filePath("midi-proposed-086.mid"),original=output.filePath("midi-original-086.mid");
    QVERIFY(panel.write(score,proposed,original));
    ExportMidi expected(score);QBuffer proposedBytes;proposedBytes.open(QIODevice::WriteOnly);
    QVERIFY(expected.write(&proposedBytes,preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),preferences.getBool(PREF_IO_MIDI_EXPORTRPNS),main->synthesizerState(),panel.options(score)));
    ExportMidi plain(score);QBuffer plainBytes;plainBytes.open(QIODevice::WriteOnly);
    QVERIFY(plain.write(&plainBytes,preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),preferences.getBool(PREF_IO_MIDI_EXPORTRPNS),main->synthesizerState()));
    QFile proposedFile(proposed),originalFile(original);QVERIFY(proposedFile.open(QIODevice::ReadOnly));QVERIFY(originalFile.open(QIODevice::ReadOnly));
    QCOMPARE(proposedFile.readAll(),proposedBytes.data());QCOMPARE(originalFile.readAll(),plainBytes.data());
    QCOMPARE(main->scores().size(),tabs);QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(score->state(),state);
    QVERIFY2(issues.isEmpty(),qPrintable(issues.join("; ")));
}
