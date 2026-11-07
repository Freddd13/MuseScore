// GPL-2.0-or-later. Home rebuilds the selected preview, including earlier notes.
void TestPerformanceEditor::homePreview159()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    auto score=expressionScore080(main,_directory.filePath("home-preview-159.mscx"));QVERIFY(score);
    expressionShowMain080(main);main->showPerformanceEditor(true);
    MasterSynthesizer synth;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.init();
    auto sequence=new PerformanceAuditionSeq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* value){stopPlaybackPreview(true);value->stopWait();value->waitForStoppedRendering();value->setScoreView(nullptr);delete value;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    const auto material=renderPlaybackPreview(score,score);QVERIFY(!material.events.empty());
    preparePlaybackPreview(score,"home-preview-159",material,material);
    const auto started=startPlaybackPreview(score,true,{{"start",960},{"end",2880},{"exactStart",true}});QVERIFY2(started.value("ok").toBool(),qPrintable(started.value("error").toString()));
    driver->pulse();QApplication::processEvents();QVERIFY(sequence->isPlaying());
    sequence->seek(1920);driver->pulse();QTest::qWait(25);QVERIFY(sequence->getCurTick()>=1920);
    auto canvas=main->performanceEditor()->findChild<QWidget*>("performanceNoteCanvas");QVERIFY(canvas);canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Home);
    driver->pulse();QTest::qWait(25);QVERIFY(sequence->isPlaying());QVERIFY(sequence->getCurTick()>=960);QVERIFY(sequence->getCurTick()<1200);
    QVERIFY(playbackPreviewActive(score,true));
    const auto events=activePlaybackPreviewEvents(score);QVERIFY(events);bool hasFirst=false;for(auto e=events->lower_bound(960);e!=events->upper_bound(960);++e)hasFirst=hasFirst||(e->second.type()==ME_NOTEON&&e->second.velo()>0);QVERIFY(hasFirst);
}
