// GPL-2.0-or-later. Eight distinct signals prove the actual joint output after switching scores.
void TestPerformanceEditor::referenceEightTrackSessions153()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);
    auto first=expressionScore080(main,_directory.filePath("joint-first-153.mscx"));QVERIFY(first);expressionShowMain080(main);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;
    synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    auto sequence=new PerformanceAuditionSeq;Ms::seq=sequence;
    const auto stop=qScopeGuard([&]{workflowContext()->finishPlaybackSession();QMetaObject::invokeMethod(referenceProject(),"stop");sequence->stopWait();sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();sequence->setScoreView(nullptr);delete sequence;Ms::seq=nullptr;showHarmonyPanel(false);});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    const QVector<double> frequencies{330.,550.,770.,990.,1210.,1430.,1650.,1870.};
    const auto path=referenceTone099(_directory.filePath("joint-track-0-153.wav"),frequencies.front());QVERIFY(!path.isEmpty());
    ReferencePanel081 panel{referenceOpen081(path)};auto root=panel.root.data();QVERIFY(root);
    auto tracks=[&]{auto v=root->property("tracks");return v.canConvert<QJSValue>()?v.value<QJSValue>().toVariant().toList():v.toList();};
    QTRY_VERIFY_WITH_TIMEOUT(root->property("audioReady").toBool(),10000);
    for(int index=0;index<8;++index){
        if(index){const auto audio=referenceTone099(_directory.filePath(QString("joint-track-%1-153.wav").arg(index)),frequencies[index]);QVERIFY(!audio.isEmpty());root->setProperty("addingAudio",true);
            QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(audio).toString())));
            QTRY_VERIFY_WITH_TIMEOUT(tracks().size()==index+1&&root->property("readingAudioCount").toInt()==0,10000);}
        first->selection().setRangeTicks(Fraction::fromTicks(480),Fraction::fromTicks(3360),0,1);first->selection().updateSelectedElements();
        root->setProperty("a",500.);root->setProperty("b",3500.);referenceMedia081(root)->setProperty("volume",.25);
        QVERIFY(QMetaObject::invokeMethod(root,"associate"));
    }
    root->setProperty("transportTarget",2);root->setProperty("jointVolume",1.);root->setProperty("fade",false);
    QVERIFY(QMetaObject::invokeMethod(root,"saveScoreProject"));const auto originalTracks=tracks();QCOMPARE(originalTracks.size(),8);
    auto project=referenceProject();QSignalSpy starts(sequence,&Seq::started);
    auto levels=[&]{
        QVector<float> samples;for(int n=0;n<10;++n){driver->pulse();for(int i=0;i<2048;++i)samples.append(driver->output()[2*i]);QTest::qWait(4);}
        QVector<double> values;for(const auto frequency:frequencies){double real=0,imaginary=0;
            for(int i=2048;i<samples.size();++i){const double angle=2*3.14159265358979323846*frequency*i/44100.;real+=samples[i]*std::cos(angle);imaginary+=samples[i]*std::sin(angle);}
            values.append(2*std::hypot(real,imaginary)/(samples.size()-2048));}
        qInfo()<<"153 actual mixed amplitudes"<<values;return values;
    };
    auto allAudible=[&](const QVector<double>& values){for(const auto value:values)if(value<.027||value>.034)return false;return true;};
    showHarmonyPanel(true);QPointer<QmlPlugin> harmony;
    for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto plugin=qobject_cast<QmlPlugin*>(view->rootObject()))if(plugin->menuPath().contains("Harmony"))harmony=plugin;
    QVERIFY(harmony);QTRY_VERIFY_WITH_TIMEOUT(!harmony->property("analysisDirty").toBool(),15000);
    auto harmonyObserver=qobject_cast<PluginAPI::ScoreObserver*>(harmony->property("observer").value<QObject*>());QVERIFY(harmonyObserver);
    for(int round=0;round<3;++round){
        const int beforeOnsets=recording->onPitches.size();
        QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&project->property("playing").toBool(),15000);
        QCOMPARE(project->property("diagnostics").toMap().value("audioTracks").toInt(),8);QVERIFY(allAudible(levels()));
        QVERIFY(recording->onPitches.size()>beforeOnsets);QCOMPARE(recording->onPitches[beforeOnsets],62);
        QCOMPARE(project->property("diagnostics").toMap().value("underruns").toInt(),0);
        if(!round){
            const int began=starts.count();QVERIFY(referenceClick099(root,"referenceTrackMute"));auto muted=levels();QVERIFY(muted.front()<.001&&muted.back()>.027);
            QVERIFY(referenceClick099(root,"referenceTrackMute"));QVERIFY(allAudible(levels()));
            QVERIFY(referenceClick099(root,"referenceTrackSolo"));auto solo=levels();QVERIFY(solo.front()>.027);for(int i=1;i<8;++i)QVERIFY(solo[i]<.001);
            QVERIFY(referenceClick099(root,"referenceTrackSolo"));QVERIFY(allAudible(levels()));QCOMPARE(starts.count(),began);
            showReferencePanel(false);showHarmonyPanel(false);showReferencePanel(true);showHarmonyPanel(true);QVERIFY(sequence->isPlaying());
        }
        QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying()&&!project->property("playing").toBool(),2000);
        QTRY_VERIFY(sequence->backgroundRenderingIdle());QVERIFY(!getAction("play")->isChecked());
    }
    QVERIFY(QMetaObject::invokeMethod(root,"startJoint"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&project->property("playing").toBool(),15000);QVERIFY(allAudible(levels()));
    auto second=expressionScore080(main,_directory.filePath("joint-second-153.mscx"));QVERIFY(second);
    QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying()&&!project->property("playing").toBool(),2000);QTRY_COMPARE(tracks().size(),0);
    QTRY_VERIFY(harmonyObserver->wrappedScore()&&harmonyObserver->wrappedScore()->score()==second);
    const auto other=referenceTone099(_directory.filePath("joint-other-153.wav"),frequencies[3]);
    QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(other).toString())));QTRY_VERIFY_WITH_TIMEOUT(root->property("audioReady").toBool(),10000);
    second->selection().setRangeTicks(Fraction::fromTicks(480),Fraction::fromTicks(3360),0,1);second->selection().updateSelectedElements();
    root->setProperty("a",500.);root->setProperty("b",3500.);referenceMedia081(root)->setProperty("volume",.25);
    QVERIFY(QMetaObject::invokeMethod(root,"associate"));root->setProperty("jointVolume",1.);root->setProperty("fade",false);
    QVERIFY(QMetaObject::invokeMethod(root,"startJoint"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),15000);
    auto onlySecond=levels();for(int i=0;i<8;++i)if(i==3)QVERIFY(onlySecond[i]>.027);else QVERIFY(onlySecond[i]<.001);
    QVERIFY(QMetaObject::invokeMethod(root,"stopJoint"));QTRY_VERIFY(!sequence->isPlaying());
    main->setCurrentScoreView(main->scores().indexOf(first));
    QTRY_VERIFY_WITH_TIMEOUT(tracks().size()==8&&root->property("readingAudioCount").toInt()==0,15000);
    QTRY_VERIFY(harmonyObserver->wrappedScore()&&harmonyObserver->wrappedScore()->score()==first);
    QTRY_VERIFY_WITH_TIMEOUT(!harmony->property("analysisDirty").toBool(),15000);
    for(int i=0;i<8;++i)QCOMPARE(tracks()[i].toMap().value("id"),originalTracks[i].toMap().value("id"));
    QVERIFY(QMetaObject::invokeMethod(root,"startJoint"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),15000);QVERIFY(allAudible(levels()));
    // Let the full passage finish through the actual output, then start it again.
    for(int n=0;n<120&&(sequence->isPlaying()||project->property("playing").toBool());++n){driver->pulse();QTest::qWait(4);}
    QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying()&&!project->property("playing").toBool(),3000);
    QVERIFY(QMetaObject::invokeMethod(root,"startJoint"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),15000);QVERIFY(allAudible(levels()));
    QVERIFY(QMetaObject::invokeMethod(root,"stopJoint"));QTRY_VERIFY(!sequence->isPlaying());
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QTest::qWait(100);
    QVERIFY(main->grab().save(output.filePath("joint-tracks-153.png")));
    QVERIFY(root->window()->isVisible());QVERIFY(root->window()->grabWindow().save(output.filePath("joint-player-153.png")));
    QVERIFY(harmony->window()->isVisible());QVERIFY(harmony->window()->grabWindow().save(output.filePath("joint-harmony-153.png")));
    qInfo()<<"153 harmony status"<<harmony->property("analysisStatus")<<"chord"<<harmony->property("chordText")<<"degree"<<harmony->property("degreeText")<<"size"<<harmony->size();
    qInfo()<<"153 eight-track sessions, actual starts"<<starts.count()<<"MIDI onsets"<<recording->onPitches.size();
}
