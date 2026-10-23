// The reference button, panel Space and global shortcut control one audio session.
void TestPerformanceEditor::referenceOneTransportFromEveryEntry()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);
    auto score=expressionScore080(main,_directory.filePath("reference-transport-098.mscx"));QVERIFY(score);expressionShowMain080(main);
    const auto wav=referenceWave081(_directory.filePath("reference-transport-098.wav"));QVERIFY(!wav.isEmpty());
    ReferencePanel081 panel{referenceOpen081(wav)};auto root=panel.root.data();QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>3900,10000);
    auto media=referenceMedia081(root),project=referenceProject();QVERIFY(project);auto action=getAction("reference-audio-play");QVERIFY(action);
    auto shortcut=Shortcut::getShortcut("reference-audio-play");QVERIFY(shortcut);const auto oldKeys=shortcut->keys();
    struct RestoreKeys {Shortcut* sc;QList<QKeySequence> keys;~RestoreKeys(){sc->setKeys(keys);}} restoreKeys{shortcut,oldKeys};shortcut->setKeys({QKeySequence(Qt::CTRL|Qt::Key_Space)});
    media->setProperty("volume",0.);root->setProperty("transportTarget",0);
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY(project->property("playing").toBool());QVERIFY(!media->property("playing").toBool());
    action->trigger();QTest::qWait(80);
    qInfo()<<"shortcut after panel play: single / project"<<media->property("playing")<<project->property("playing");
    QTRY_VERIFY2_WITH_TIMEOUT(!media->property("playing").toBool()&&!project->property("playing").toBool(),"原曲按钮播放后，原曲快捷键应控制同一次播放",500);
    QVERIFY(QMetaObject::invokeMethod(root,"setTrackFlag",Q_ARG(QVariant,0),Q_ARG(QVariant,"mute"),Q_ARG(QVariant,true)));
    action->trigger();QTest::qWait(60);QVERIFY(!media->property("playing").toBool());QVERIFY(!project->property("playing").toBool());QVERIFY(root->property("status").toString().contains("静音"));
    QVERIFY(QMetaObject::invokeMethod(root,"setTrackFlag",Q_ARG(QVariant,0),Q_ARG(QVariant,"mute"),Q_ARG(QVariant,false)));
    root->setProperty("transportTarget",2);action->trigger();QTRY_VERIFY(project->property("playing").toBool());QCOMPARE(root->property("transportTarget").toInt(),0);QVERIFY(!media->property("playing").toBool());
    QMetaObject::invokeMethod(project,"toggleIndependent");QTRY_VERIFY(!project->property("playing").toBool());QVERIFY(!media->property("playing").toBool());
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY(project->property("playing").toBool());root->focusPanel();root->window()->requestActivate();root->forceActiveFocus();QTest::qWait(80);
    QTest::keyPress(root->window(),Qt::Key_Space);qInfo()<<"space pressed"<<root->property("spaceDown")<<root->window()->activeFocusItem()<<root->window()->isActive();QVERIFY(root->property("spaceDown").toBool());QTest::keyRelease(root->window(),Qt::Key_Space);QTRY_VERIFY_WITH_TIMEOUT(!project->property("playing").toBool(),500);QVERIFY(!media->property("playing").toBool());
    QTest::keyClick(root->window(),Qt::Key_Space);QTRY_VERIFY(project->property("playing").toBool());action->trigger();QTRY_VERIFY(!project->property("playing").toBool());
    QVERIFY(QMetaObject::invokeMethod(root,"seekAudio",Q_ARG(QVariant,1200.)));
    action->trigger();QTRY_VERIFY(project->property("playing").toBool());QTRY_VERIFY(root->property("shownPosition").toDouble()>=1200);
    QTest::qWait(150);const double beforePause=root->property("shownPosition").toDouble();QVERIFY(beforePause<2000);
    action->trigger();QTRY_VERIFY(!project->property("playing").toBool());QVERIFY(media->property("position").toDouble()>=beforePause-10);
    action->trigger();QTRY_VERIFY(project->property("playing").toBool());QVERIFY(root->property("shownPosition").toDouble()>=beforePause-10);action->trigger();QTRY_VERIFY(!project->property("playing").toBool());
    QTest::keyClick(root->window(),Qt::Key_Space,Qt::ControlModifier);QTRY_VERIFY_WITH_TIMEOUT(project->property("playing").toBool(),1000);QTest::keyClick(root->window(),Qt::Key_Space,Qt::ControlModifier);QTRY_VERIFY_WITH_TIMEOUT(!project->property("playing").toBool(),500);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->grabWindow().save(output.filePath("reference-one-transport-098.png")));
}

void TestPerformanceEditor::referenceSourcePositionAndLoop()
{
    expressionHost080(_directory.path());
    auto project=referenceProject();QVERIFY(project&&project->property("available").toBool());
    const auto path=referenceWave081(_directory.filePath("reference-offset-098.wav"));QVERIFY(!path.isEmpty());
    auto position=[&]{double value=-1;if(!QMetaObject::invokeMethod(project,"sourcePosition",Q_RETURN_ARG(double,value),Q_ARG(QString,path)))return -1.;return value;};
    const QVariantList pieces{QVariantMap{{"from",500.},{"until",1500.},{"at",0.},{"duration",800.},{"fade",false}}};
    QVariantMap clip{{"id","offset"},{"path",path},{"volume",1.},{"pieces",pieces}},plan{{"owner","offset-check"},{"joint",false},{"volume",0.},{"offset",560.},{"loop",true},{"clips",QVariantList{clip}}};
    QVariantMap reply;QVERIFY(QMetaObject::invokeMethod(project,"start",Q_RETURN_ARG(QVariantMap,reply),Q_ARG(QVariantMap,plan)));QVERIFY2(reply.value("ok").toBool(),qPrintable(reply.value("error").toString()));
    QVERIFY(qAbs(position()-1200)<.1);
    QTRY_VERIFY_WITH_TIMEOUT(position()>1205,3000);QTRY_VERIFY_WITH_TIMEOUT(position()<1000,3000);
    const double before=position();QVERIFY(before>=500&&before<1500);QMetaObject::invokeMethod(project,"stop");QVERIFY(qAbs(position()-before)<30);
    plan["loop"]=false;plan["offset"]=760.;QVERIFY(QMetaObject::invokeMethod(project,"start",Q_RETURN_ARG(QVariantMap,reply),Q_ARG(QVariantMap,plan)));QVERIFY(reply.value("ok").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!project->property("playing").toBool(),3000);QVERIFY(project->property("completed").toBool());QVERIFY(qAbs(position()-1500)<.1);
}

void TestPerformanceEditor::referenceIndependentPlaybackPolicy()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);
    auto score=expressionScore080(main,_directory.filePath("reference-policy-098.mscx"));QVERIFY(score);expressionShowMain080(main);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    auto sequence=new PerformanceAuditionSeq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    const auto wav=referenceWave081(_directory.filePath("reference-policy-098.wav"));QVERIFY(!wav.isEmpty());ReferencePanel081 panel{referenceOpen081(wav)};auto root=panel.root.data();QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>3900,10000);referenceMedia081(root)->setProperty("volume",0.);
    auto project=referenceProject();const bool oldAudio=project->property("audioStopsScore").toBool(),oldScore=project->property("scoreStopsAudio").toBool();
    struct RestorePolicy {QObject* project;bool audio,score;~RestorePolicy(){project->setProperty("audioStopsScore",audio);project->setProperty("scoreStopsAudio",score);}} restore{project,oldAudio,oldScore};
    project->setProperty("audioStopsScore",false);project->setProperty("scoreStopsAudio",false);root->setProperty("transportTarget",0);
    sequence->startAtUTick(0,0);QTRY_VERIFY(sequence->isPlaying());
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY(project->property("playing").toBool());QVERIFY(sequence->isPlaying());
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY(!project->property("playing").toBool());QVERIFY(sequence->isPlaying());
    QVERIFY(referenceClick081(root,"referenceProjectMenuButton"));QTest::qWait(60);QVERIFY(referenceClick081(root,"referenceAudioStopsScore"));QVERIFY(project->property("audioStopsScore").toBool());
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY(project->property("playing").toBool());QTRY_VERIFY(!sequence->isPlaying());
    QVERIFY(referenceClick081(root,"referenceProjectMenuButton"));QTest::qWait(60);QVERIFY(referenceClick081(root,"referenceScoreStopsAudio"));QVERIFY(project->property("scoreStopsAudio").toBool());
    QVERIFY(referenceClick081(root,"referenceProjectMenuButton"));QTest::qWait(60);
    const auto menuOutput=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->grabWindow().save(menuOutput.filePath("reference-playback-options-098.png")));QTest::keyClick(root->window(),Qt::Key_Escape);
    sequence->startAtUTick(0,0);QTRY_VERIFY(sequence->isPlaying());QTRY_VERIFY(!project->property("playing").toBool());
    sequence->stopWait();sequence->waitForStoppedRendering();
    score->selection().setRangeTicks(Fraction(),score->lastMeasure()->endTick(),0,1);score->selection().updateSelectedElements();root->setProperty("a",0.);root->setProperty("b",4000.);root->setProperty("jointVolume",0.);
    QVERIFY(referenceClick081(root,"referenceAssociate"));root->setProperty("transportTarget",2);QVERIFY(referenceClick081(root,"referenceJointPlay"));
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&project->property("playing").toBool(),10000);for(int n=0;n<8;++n){driver->pulse();QTest::qWait(5);}QVERIFY(sequence->isPlaying());QVERIFY(project->property("playing").toBool());
    const double jointPosition=root->property("shownPosition").toDouble();QVERIFY(jointPosition>100);
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY(!sequence->isPlaying());QVERIFY(!project->property("playing").toBool());QVERIFY(referenceMedia081(root)->property("position").toDouble()>=jointPosition-30);
    root->setProperty("transportTarget",0);root->setProperty("pauseWhenHidden",true);QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY(project->property("playing").toBool());
    auto selected=toChord(score->findCR(Fraction::fromTicks(480),0))->upNote();score->select(selected,SelectType::SINGLE);QTest::qWait(40);QVERIFY(referenceClick081(root,"referenceSeekScore"));
    QTRY_VERIFY(root->property("shownPosition").toDouble()>=250);QVERIFY(root->property("shownPosition").toDouble()<500);root->hidePanel();QTRY_VERIFY(!project->property("playing").toBool());showReferencePanel(true);root->setProperty("pauseWhenHidden",false);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->grabWindow().save(output.filePath("reference-playback-policy-098.png")));
}

void TestPerformanceEditor::referenceShortcutPreservesUserChoice()
{
    expressionHost080(_directory.path());
    struct Saved {Shortcut* sc;QList<QKeySequence> keys;QKeySequence::StandardKey standard;};QVector<Saved> saved;
    for(auto sc:Shortcut::shortcuts())saved.append({sc,sc->keys(),sc->standardKey()});
    const QString previousPath=dataPath;const bool dirty=Shortcut::dirty;
    struct Restore {QVector<Saved> saved;QString path;bool dirty;~Restore(){dataPath=path;Shortcut::load();for(const auto& item:saved){item.sc->setKeys(item.keys);item.sc->setStandardKey(item.standard);}Shortcut::dirty=dirty;}} restore{saved,previousPath,dirty};
    dataPath=_directory.filePath("reference-shortcuts-098");QVERIFY(QDir().mkpath(dataPath));
    auto target=Shortcut::getShortcut("reference-audio-play"),other=Shortcut::getShortcut("help");QVERIFY(target&&other);
    auto load=[&](const QByteArray& text){QFile file(dataPath+"/shortcuts.xml");if(!file.open(QIODevice::WriteOnly))return false;file.write(text);file.close();Shortcut::load();return true;};
    QVERIFY(load("<Shortcuts><SC><key>help</key></SC></Shortcuts>"));QVERIFY(target->keys().contains(QKeySequence(Qt::CTRL|Qt::Key_Space)));
    QVERIFY(load("<Shortcuts><SC><key>reference-audio-play</key></SC></Shortcuts>"));QVERIFY(target->keys().isEmpty());
    QVERIFY(load("<Shortcuts><SC><key>reference-audio-play</key><seq>F8</seq></SC></Shortcuts>"));QCOMPARE(target->keys(),QList<QKeySequence>{QKeySequence(Qt::Key_F8)});
    QVERIFY(load("<Shortcuts><SC><key>help</key><seq>Ctrl+Space</seq></SC></Shortcuts>"));QVERIFY(target->keys().isEmpty());QVERIFY(other->keys().contains(QKeySequence(Qt::CTRL|Qt::Key_Space)));
    QVERIFY(load("<Shortcuts><SC><key>help</key><seq>Ctrl+Space, F8</seq></SC></Shortcuts>"));QVERIFY(target->keys().isEmpty());
}
