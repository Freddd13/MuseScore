void TestPerformanceEditor::midiGateAuditionTransport()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    auto score=expressionScore080(main,_directory.filePath("midi-transport-086.mscx"));QVERIFY(score);expressionShowMain080(main);
    auto pedal=new Pedal(score);pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(7680));
    pedal->setTrack(0);pedal->setTrack2(0);score->addSpanner(pedal);score->setPlaylistDirty();
    for(const char* key:{"export/midi/cropFromMeasure","export/midi/cropUntilMeasure"})preferences.setPreference(key,0);
    preferences.setPreference("export/midi/cropProtectMelody",false);preferences.setPreference("export/midi/cropEnabled",true);
    preferences.setPreference("export/midi/cropVoices",QString());preferences.setPreference("export/midi/cropProtectedVoices",QString());
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;
    synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();stopPlaybackPreview(true);s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    const auto state=score->state();const int undo=score->undoStack()->getCurIdx(),tabs=main->scores().size();
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score};});panel.show();
    QStringList issues;bool completed=false;int tries=0;QTimer timer;timer.setInterval(50);
    connect(&timer,&QTimer::timeout,[&]{
        auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog||dialog->objectName()!="midiCropComparison")return;
        auto table=dialog->findChild<QTableView*>("midiCropNotes");auto roll=dialog->findChild<QAbstractScrollArea*>("midiCropRoll");
        if(++tries>160){issues<<"comparison timed out";timer.stop();dialog->reject();return;}
        if(!table||!roll||!roll->isEnabled()||table->model()->rowCount()!=16)return;
        timer.stop();
        std::unique_ptr<QDialog,std::function<void(QDialog*)>> close(dialog,[&](QDialog* w){stopPlaybackPreview(true);w->accept();});
        auto check=[&](bool value,const char* message){if(!value)issues<<message;return value;};
        auto waitFor=[&](std::function<bool()> condition){for(int i=0;i<120&&!condition();++i)QTest::qWait(10);return condition();};
        auto original=dialog->findChild<QPushButton*>("midiCropOriginalPlay"),proposed=dialog->findChild<QPushButton*>("midiCropProposedPlay"),stop=dialog->findChild<QPushButton*>("midiCropStop");
        if(!check(original&&proposed&&stop,"transport controls missing"))return;
        table->selectRow(8);QTest::qWait(60);QTest::mouseClick(proposed,Qt::LeftButton);
        if(!check(waitFor([&]{return sequence->isPlaying();}),qPrintable(dialog->findChild<QLabel*>("midiCropPlaybackStatus")->text())))return;
        if(!check(sequence->getCurTick()==3840,"selected note audition started in preceding music"))return;
        if(!check(proposed->isChecked()&&!original->isChecked()&&stop->isEnabled(),"playing source is unclear"))return;
        recording->onPitches.clear();
        for(int i=0;i<50&&sequence->isPlaying()&&sequence->getCurTick()<5000;++i){driver->pulse();QTest::qWait(2);}
        if(!check(recording->onPitches.contains(74),"selected phrase did not reach MIDI output"))return;
        const int before=sequence->getCurTick();QTest::mouseClick(original,Qt::LeftButton);
        if(!check(waitFor([&]{return sequence->isPlaying();})&&sequence->getCurTick()==before,"switching original/new lost current beat"))return;
        if(!check(original->isChecked()&&!proposed->isChecked(),"original source button did not follow playback"))return;
        table->selectRow(1);QTest::qWait(20);
        if(!check(sequence->isPlaying()&&sequence->getCurTick()==before,"selecting another note interrupted audition"))return;
        QTest::mouseClick(stop,Qt::LeftButton);
        if(!check(waitFor([&]{return !sequence->isPlaying();})&&!stop->isEnabled()&&!original->isChecked()&&!proposed->isChecked(),"stop did not reset the controls"))return;
        auto fit=dialog->findChild<QPushButton*>("midiCropFit");QTest::mouseClick(fit,Qt::LeftButton);QTest::qWait(40);
        const double scale=roll->property("timeScale").toDouble();
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(46+qRound(1250*scale),12));
        roll->setFocus();QTest::keyClick(roll,Qt::Key_Space);
        if(!check(waitFor([&]{return sequence->isPlaying();})&&qAbs(sequence->getCurTick()-1200)<=5,"ruler and Space did not start at the chosen time"))return;
        QTest::keyClick(roll,Qt::Key_Space);
        if(!check(waitFor([&]{return !sequence->isPlaying();}),"Space did not pause audition"))return;
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(46,12));QTest::mouseClick(proposed,Qt::LeftButton);
        if(!check(waitFor([&]{return sequence->isPlaying();}),"audition did not restart from beginning"))return;
        recording->onPitches.clear();
        for(int i=0;i<250&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(2);}
        if(!check(waitFor([&]{return !sequence->isPlaying();})&&recording->onPitches.contains(74),"audition did not play through later measures"))return;
        QTest::qWait(60);
        if(!check(!stop->isEnabled()&&!proposed->isChecked(),"natural end left playback controls active"))return;
        QTest::mouseClick(original,Qt::LeftButton);
        if(!check(waitFor([&]{return sequence->isPlaying();}),"original length did not play after natural end"))return;
        for(int i=0;i<6;++i){driver->pulse();QTest::qWait(2);}
        dialog->grab().save(output.filePath("midi-audition-086.png"));QTest::mouseClick(stop,Qt::LeftButton);completed=true;
    });
    timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiCropCompare"),Qt::LeftButton);timer.stop();
    QFile report(output.filePath("midi-audition-086.json"));QVERIFY(report.open(QIODevice::WriteOnly));
    report.write(QJsonDocument(QJsonObject{{"issues",QJsonArray::fromStringList(issues)},{"completed",completed},{"emittedAttacks",recording->onPitches.size()}}).toJson());report.close();
    QVERIFY2(completed&&issues.isEmpty(),qPrintable(issues.join("; ")));
    QCOMPARE(score->state(),state);QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(main->scores().size(),tabs);
}
