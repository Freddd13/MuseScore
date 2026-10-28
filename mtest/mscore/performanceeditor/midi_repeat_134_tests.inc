void TestPerformanceEditor::midiRepeatedExportAudition134() {
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));expressionShowMain080(main);
    score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);score->setExpandRepeats(false);
    auto pedal=new Pedal(score);pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(score->lastMeasure()->endTick());pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);score->setPlaylistDirty();
    const bool ordinary=MScore::playRepeats;MScore::playRepeats=false;auto restore=qScopeGuard([&]{MScore::playRepeats=ordinary;});
    preferences.setPreference("export/midi/cropProtectMelody",false);preferences.setPreference("export/midi/cropEnabled",true);preferences.setPreference("export/midi/cropFromMeasure",0);preferences.setPreference("export/midi/cropUntilMeasure",0);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){stopPlaybackPreview(true);s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    const auto state=score->state();const auto undo=score->undoStack()->getCurIdx();const auto dirty=score->dirty();
    MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score};},[]{return MidiRenderSettings{true,true};});panel.show();
    QStringList issues;bool completed=false;int tries=0;QTimer timer;timer.setInterval(40);
    connect(&timer,&QTimer::timeout,[&]{
        auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog||dialog->objectName()!="midiCropComparison")return;
        auto table=dialog->findChild<QTableView*>("midiCropNotes");auto roll=dialog->findChild<QAbstractScrollArea*>("midiCropRoll");
        if(++tries>150){issues<<"repeat comparison timeout";timer.stop();dialog->reject();return;}if(!table||!roll||!roll->isEnabled()||table->model()->rowCount()!=10)return;
        timer.stop();auto close=qScopeGuard([&]{stopPlaybackPreview(true);dialog->accept();});
        auto check=[&](bool value,const char* message){if(!value)issues<<message;return value;};auto wait=[&](std::function<bool()> condition){for(int i=0;i<120&&!condition();++i)QTest::qWait(10);return condition();};
        auto original=dialog->findChild<QPushButton*>("midiCropOriginalPlay"),proposed=dialog->findChild<QPushButton*>("midiCropProposedPlay"),stop=dialog->findChild<QPushButton*>("midiCropStop");if(!check(original&&proposed&&stop,"missing transport"))return;
        int second=-1;for(int i=0;i<table->model()->rowCount();++i)if(table->model()->index(i,0).data().toString()=="C2"&&table->model()->index(i,1).data().toString().section(' ',0,0).toDouble()>1.5)second=i;
        if(!check(second>=0,"second pass root not visible"))return;table->selectRow(second);QTest::qWait(30);QTest::mouseClick(proposed,Qt::LeftButton);
        if(!check(wait([&]{return sequence->isPlaying();}),qPrintable(dialog->findChild<QLabel*>("midiCropPlaybackStatus")->text())))return;
        if(!check(sequence->getCurTick()==1920&&score->expandRepeats()&&!MScore::playRepeats,"second pass start or temporary policy is wrong"))return;
        for(int i=0;i<8;++i){driver->pulse();QTest::qWait(2);}const int at=sequence->getCurTick();QTest::mouseClick(original,Qt::LeftButton);
        if(!check(wait([&]{return sequence->isPlaying();})&&sequence->getCurTick()==at,"repeat source switch lost position"))return;
        QTest::mouseClick(stop,Qt::LeftButton);if(!check(wait([&]{return !sequence->isPlaying();})&&!score->expandRepeats(),"pause did not restore ordinary repeat choice"))return;
        QTest::mouseClick(proposed,Qt::LeftButton);if(!check(wait([&]{return sequence->isPlaying();})&&sequence->getCurTick()==at,"resume lost second-pass position"))return;
        auto recompute=dialog->findChild<QPushButton*>("midiCropRecompute");if(!check(recompute,"recompute missing"))return;QTest::mouseClick(recompute,Qt::LeftButton);
        if(!check(wait([&]{return roll->isEnabled()&&!sequence->isPlaying();})&&!score->expandRepeats(),"recompute did not pause before preparing export"))return;
        QTest::mouseClick(proposed,Qt::LeftButton);if(!check(wait([&]{return sequence->isPlaying();})&&sequence->getCurTick()==at,"recompute lost resume position"))return;
        QTest::mouseClick(stop,Qt::LeftButton);if(!check(wait([&]{return !sequence->isPlaying();}),"resume could not stop"))return;
        auto fit=dialog->findChild<QPushButton*>("midiCropFit");QTest::mouseClick(fit,Qt::LeftButton);QTest::qWait(30);
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(46,12));recording->onPitches.clear();QTest::mouseClick(proposed,Qt::LeftButton);
        if(!check(wait([&]{return sequence->isPlaying();})&&sequence->getCurTick()==0,"ruler did not return to first pass"))return;
        for(int i=0;i<200&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(2);}
        if(!check(wait([&]{return !sequence->isPlaying();})&&recording->onPitches.count(36)==2,"whole audition did not include both passes"))return;
        if(!check(!score->expandRepeats()&&!MScore::playRepeats,"natural end changed normal playback"))return;
        QTest::qWait(80);if(!check(!dialog->findChild<QLabel*>("midiCropPlaybackStatus")->text().contains(QString::fromUtf8("正在听")),"playback label stayed active after ending"))return;
        QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));dialog->grab().save(out.filePath("midi-repeated-audition-134.png"));completed=true;
    });
    timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiCropCompare"),Qt::LeftButton);timer.stop();
    QVERIFY2(completed&&issues.isEmpty(),qPrintable(issues.join("; ")));QCOMPARE(score->state(),state);QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(score->dirty(),dirty);QVERIFY(!score->expandRepeats());
}

void TestPerformanceEditor::midiAnticipatedExportAudition134() {
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));expressionShowMain080(main);
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));QVERIFY(chord);chord->downNote()->setOnTimeOffset(-100);score->setPlaylistDirty();
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){stopPlaybackPreview(true);s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    auto material=renderPlaybackPreview(score,score,true,0);preparePlaybackPreview(score,"anticipated-134",material,material);
    QVariantMap range{{"start",0},{"end",score->lastMeasure()->endTick().ticks()},{"occurrence",1},{"exactStart",true},{"fromEventTick",-96}};
    auto result=startPlaybackPreview(score,false,range);QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));
    QTRY_VERIFY(sequence->isPlaying());QCOMPARE(sequence->getCurEventTick(),-96);QCOMPARE(sequence->getCurTick(),0);
    const auto restored=restoredPlaybackEvents(material.events,-96);int held=0;for(const auto& e:restored)if(e.second.type()==ME_NOTEON&&e.second.velo()>0&&e.second.pitch()==36)++held;QCOMPARE(held,1);
    const auto firstFrame=recording->frames;driver->pulse(1024);qInfo()<<"Anticipation first output"<<firstFrame<<recording->frames<<recording->onPitches<<sequence->getCurEventTick();QCOMPARE(recording->onPitches.count(36),1);QVERIFY(!recording->onFrames.isEmpty());QCOMPARE(recording->onFrames.last(),firstFrame);
    const int paused=sequence->getCurEventTick();QVERIFY(paused<0);stopPlaybackPreview();QTRY_VERIFY(!sequence->isPlaying());
    range.remove("fromEventTick");result=startPlaybackPreview(score,true,range);QVERIFY(!result.contains("error"));QTRY_VERIFY(sequence->isPlaying());QCOMPARE(sequence->getCurEventTick(),paused);
    driver->pulse(1024);const int switched=sequence->getCurEventTick();result=startPlaybackPreview(score,false,range);QVERIFY(!result.contains("error"));QTRY_VERIFY(sequence->isPlaying());QCOMPARE(sequence->getCurEventTick(),switched);
    stopPlaybackPreview(true);QTRY_VERIFY(!sequence->isPlaying());
    recording->onPitches.clear();result=startPlaybackPreview(score,false,range);QVERIFY(!result.contains("error"));QTRY_VERIFY(sequence->isPlaying());QCOMPARE(sequence->getCurEventTick(),-192);
    for(int i=0;i<160&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(2);}QTRY_VERIFY(!sequence->isPlaying());QCOMPARE(recording->onPitches.count(36),1);
    stopPlaybackPreview(true);recording->onPitches.clear();range["fromEventTick"]=0;result=startPlaybackPreview(score,false,range);QVERIFY(!result.contains("error"));QTRY_VERIFY(sequence->isPlaying());QCOMPARE(sequence->getCurEventTick(),0);
    driver->pulse(1024);QCOMPARE(recording->onPitches.count(36),1);stopPlaybackPreview(true);QTRY_VERIFY(!sequence->isPlaying());
    preferences.setPreference("export/midi/cropEnabled",false);MidiCropPanel panel(nullptr,[&]{return QList<Score*>{score};},[]{return MidiRenderSettings{false,true};});panel.show();
    QStringList issues;bool completed=false;int tries=0;QTimer timer;timer.setInterval(40);connect(&timer,&QTimer::timeout,[&]{
        auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog||dialog->objectName()!="midiCropComparison")return;
        auto roll=dialog->findChild<QAbstractScrollArea*>("midiCropRoll");auto slider=dialog->findChild<QSlider*>("midiCropZoom");
        if(++tries>150){issues<<"anticipated preview timeout";timer.stop();dialog->reject();return;}if(!roll||!roll->isEnabled())return;
        timer.stop();auto close=qScopeGuard([&]{stopPlaybackPreview(true);dialog->accept();});
        if(!slider){issues<<"time scale not found";return;}slider->setValue(800);QTest::qWait(30);
        const double scale=roll->property("timeScale").toDouble();if(scale<=0){issues<<"time scale missing";return;}
        roll->horizontalScrollBar()->setValue(0);const int x=46+qRound(100.*scale);
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(x,12));
        QTest::mouseClick(dialog->findChild<QPushButton*>("midiCropOriginalPlay"),Qt::LeftButton);for(int i=0;i<100&&!sequence->isPlaying();++i)QTest::qWait(10);
        if(!sequence->isPlaying()){issues<<dialog->findChild<QLabel*>("midiCropPlaybackStatus")->text();return;}
        const int actual=sequence->getCurEventTick();qInfo()<<"Anticipated file ruler 100ms / native tick"<<actual;
        if(qAbs(actual+96)>8){issues<<QString("file 100ms started at native %1, expected -96").arg(actual);return;}completed=true;
    });timer.start();QTest::mouseClick(panel.findChild<QPushButton*>("midiCropCompare"),Qt::LeftButton);timer.stop();QVERIFY2(completed&&issues.isEmpty(),qPrintable(issues.join("; ")));
}
