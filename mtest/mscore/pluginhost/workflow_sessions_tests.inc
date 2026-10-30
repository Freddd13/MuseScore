// GPL-2.0-or-later. Use A/B, expression, reference audio, spectrum and harmony on one score.
#include "audio/exports/exportmidi.h"
#include "mscore/workflow/playbackpreview.h"
#include <QSaveFile>
void TestPluginHost::continuousWorkflowSessions()
{
    auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);SourceTimerResolution timerResolution;
    struct SavedKeys {Shortcut* shortcut;QList<QKeySequence> keys;QKeySequence::StandardKey standard;};
    QVector<SavedKeys> savedKeys;for(auto shortcut:Shortcut::shortcuts())savedKeys.append({shortcut,shortcut->keys(),shortcut->standardKey()});
    const bool shortcutsDirty=Shortcut::dirty;
    const auto restoreKeys=qScopeGuard([&]{for(const auto& saved:savedKeys){saved.shortcut->setKeys(saved.keys);saved.shortcut->setStandardKey(saved.standard);}Shortcut::dirty=shortcutsDirty;});
    Shortcut::load();
    for(auto shortcut:Shortcut::shortcuts()){
        const auto keys=shortcut->keys();const auto standard=shortcut->standardKey();
        shortcut->setKeys(keys);shortcut->setStandardKey(standard);
    }
    QVERIFY(getAction("play")->shortcuts().contains(QKeySequence(Qt::Key_Space)));
    QFile fixture(_settings.filePath("workflow-sessions.mscx"));QVERIFY(fixture.open(QIODevice::WriteOnly));
    fixture.write(systemLockFixture138());fixture.close();auto score=main->readScore(fixture.fileName());QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    const auto close=qScopeGuard([&]{releaseHostPanels();score->undoStack()->setClean();main->closeScore(score);});
    const int start=1920,end=5760;
    auto created=RegionalVariants::create(score,start,end,{4,5,6,7},"Left hand");QVERIFY(!created.contains("error"));
    const auto group=created.value("group").toString(),a=created.value("variant").toString();
    auto added=RegionalVariants::duplicate(score,group,"B",true);QVERIFY(!added.contains("error"));
    const auto b=added.value("variant").toString();
    auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction::fromTicks(start));pedal->setTick2(Fraction::fromTicks(end));
    score->startCmd();score->undoAddElement(pedal);score->endCmd();main->endCmd();const auto pedals=pedalRanges142(score);
    auto note=[&](){return toChord(score->findCR(Fraction::fromTicks(start),4))->downNote();};
    score->select(note());getAction("pitch-up")->trigger();main->endCmd();const int bPitch=note()->pitch();

    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());QScopedValueRollback<bool> sequencing(noSeq,false);
    auto sequence=new Seq;Ms::seq=sequence;
    const auto stop=qScopeGuard([&]{QMetaObject::invokeMethod(referenceProject(),"stop");workflowContext()->finishPlaybackSession();stopPlaybackPreview(true);sequence->stopWait();sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();sequence->setScoreView(nullptr);delete sequence;Ms::seq=nullptr;});
    auto driver=new SourcePlaybackDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    showRegionalVariantPanel(group);QTest::qWait(80);
    auto listen=main->findChild<QPushButton*>("regionalAudition");QVERIFY(listen);QTest::mouseClick(listen,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);QTRY_VERIFY(sequence->getCurTick()>=start);
    QVERIFY2(getAction("play")->isChecked(),"试听开始后，主播放按钮必须处于可停止的状态");
    QElapsedTimer firstStop;firstStop.start();getAction("play")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying(),2000);QTRY_VERIFY(sequence->backgroundRenderingIdle());
    qInfo()<<"initial SF3 stop milliseconds"<<firstStop.elapsed();
    qInfo()<<"workflow: audition stopped, switching A/B";
    auto quick=main->findChild<QPushButton*>("regionalQuick/"+a);QVERIFY(quick);QTest::mouseClick(quick,Qt::LeftButton);QCOMPARE(pedalRanges142(score),pedals);
    quick=main->findChild<QPushButton*>("regionalQuick/"+b);QVERIFY(quick);QTest::mouseClick(quick,Qt::LeftButton);QCOMPARE(note()->pitch(),bPitch);QCOMPARE(pedalRanges142(score),pedals);

    qInfo()<<"workflow: opening reference audio";
    const auto audioPath=spectrumAudio078(_settings.filePath("workflow-sessions.wav"));QVERIFY(!audioPath.isEmpty());
    showReferencePanel(true);QPointer<QmlPlugin> reference;
    for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))if(root->menuPath()=="Plugins.Reference Player")reference=root;
    QVERIFY(reference);auto media=referenceProject();QVERIFY(media->property("available").toBool());
    const QVariantMap association{{"id","workflow-clip"},{"name","Whole passage"},{"start",0},{"end",15360},{"from",0.},{"until",16000.},{"active",true},{"occurrence",1}};
    const QVariantMap track{{"id","workflow-audio"},{"name","Reference"},{"path",audioPath},{"duration",24000.},{"volume",0.},{"participate",true},{"associations",QVariantList{association}},{"selectedAssociation",0}};
    const QVariant project=QVariantMap{{"tracks",QVariantList{track}},{"selectedTrack",0}};
    QVERIFY(QMetaObject::invokeMethod(reference,"restoreTracks",Q_ARG(QVariant,project),Q_ARG(QVariant,QVariant(QString()))));
    QTRY_VERIFY_WITH_TIMEOUT(reference->property("audioReady").toBool(),15000);reference->setProperty("jointVolume",0.);
    QVERIFY(QMetaObject::invokeMethod(reference,"storeTrack"));QVERIFY(QMetaObject::invokeMethod(reference,"saveScoreProject"));
    qInfo()<<"workflow: reference ready, opening harmony";
    showHarmonyPanel(true);QPointer<QmlPlugin> harmony=harmonyPanel077();QVERIFY(harmony);
    QTRY_VERIFY_WITH_TIMEOUT(!harmony->property("analysisDirty").toBool(),15000);

    // Export a complete performance, then import it through the expression page.
    qInfo()<<"workflow: exporting performance material";
    const auto midiPath=_settings.filePath("workflow-performance.mid");
    TemporaryScore played(score->clone());QVERIFY(played);QMap<Note*,VelocityEdit> velocities;
    for(auto segment=played->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))
        for(int tr:{0,4})if(auto element=segment->element(tr))if(element->isChord())for(auto n:toChord(element)->notes())velocities.insert(n,{Note::ValueType::USER_VAL,105});
    QVERIFY(ParameterEdit::velocities(played.get(),velocities));QFile midi(midiPath);QVERIFY(midi.open(QIODevice::WriteOnly));
    ExportMidi exporter(played.get());QVERIFY(exporter.write(&midi,true,true,main->synthesizerState()));midi.close();
    showExpressionPanel();auto editor=main->performanceEditor();QVERIFY(editor);
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);
    auto page=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(page);
    auto expression=qobject_cast<QmlPlugin*>(page->property("root").value<QObject*>());QVERIFY(expression);
    auto map=[&](const char* name){const auto value=expression->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toMap():value.toMap();};
    QVERIFY(QMetaObject::invokeMethod(expression,"chooseTarget",Q_ARG(QVariant,true)));
    QVERIFY(QMetaObject::invokeMethod(expression,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midiPath).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!expression->property("busy").toBool()&&!map("candidate").value("velocity").toList().isEmpty(),20000);
    qInfo()<<"continuous expression"<<expression->property("status")<<map("candidate").value("velocity").toList().size();
    QCOMPARE(map("candidate").value("velocity").toList().size(),64);
    QCOMPARE(main->currentScore(),static_cast<Score*>(score));const int scoreTabs=main->scores().size();
    expression->setProperty("auditionAudio",true);
    QVERIFY(QMetaObject::invokeMethod(expression,"audition",Q_ARG(QVariant,true)));
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&media->property("playing").toBool(),15000);
    QTRY_VERIFY(sequence->getCurTick()>480);const int beforeSwitch=sequence->getCurTick();
    QVERIFY(QMetaObject::invokeMethod(expression,"audition",Q_ARG(QVariant,false)));
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&media->property("playing").toBool(),15000);
    QVERIFY(sequence->getCurTick()>=beforeSwitch-120);QVERIFY(sequence->getCurTick()<beforeSwitch+960);
    QCOMPARE(workflowContext()->audition().value("mode").toString(),QString("score"));
    QVERIFY(QMetaObject::invokeMethod(expression,"stopAudition"));QTRY_VERIFY(!sequence->isPlaying()&&!media->property("playing").toBool());
    QCOMPARE(main->scores().size(),scoreTabs);QCOMPARE(main->currentScore(),static_cast<Score*>(score));
    for(int round=0;round<2;++round){
        QVERIFY(QMetaObject::invokeMethod(reference,"startJoint"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&media->property("playing").toBool(),15000);
        if(!round){main->raise();main->activateWindow();QApplication::setActiveWindow(main);main->currentScoreView()->setFocus();QTest::qWait(60);QSignalSpy playKey(getAction("play"),SIGNAL(triggered(bool)));QTest::keyClick(main->currentScoreView(),Qt::Key_Space);QTRY_COMPARE(playKey.size(),1);}
        else QVERIFY(QMetaObject::invokeMethod(reference,"stopJoint"));
        QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying()&&!media->property("playing").toBool(),2000);
    }
    tabs->setCurrentIndex(2);auto spectrum=editor->findChild<QWidget*>("transcriptionSpectrumPane");QVERIFY(spectrum);
    auto clips=spectrum->findChild<QComboBox*>("transcriptionClips");QVERIFY(clips);QTRY_VERIFY(clips->count()>0);clips->setCurrentIndex(0);
    auto session=spectrum->findChild<QObject*>("transcriptionSpectrumSession");QVERIFY(session);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool()&&!qvariant_cast<QImage>(session->property("image")).isNull(),20000);
    QVERIFY(!expression->property("stale").toBool());QCOMPARE(map("candidate").value("velocity").toList().size(),64);
    tabs->setCurrentIndex(1);expression->setProperty("applicationScope",2);
    QVERIFY(QMetaObject::invokeMethod(expression,"applyPart",Q_ARG(QVariant,"velocity")));
    QTRY_VERIFY_WITH_TIMEOUT(!expression->property("busy").toBool()&&map("candidate").value("velocity").toList().isEmpty(),15000);
    QCOMPARE(note()->veloType(),Note::ValueType::USER_VAL);QCOMPARE(note()->veloOffset(),105);QCOMPARE(pedalRanges142(score),pedals);
    QVERIFY(RegionalVariants::select(score,group,a).value("ok").toBool());main->endCmd();
    QVERIFY(RegionalVariants::select(score,group,b).value("ok").toBool());main->endCmd();QCOMPARE(note()->veloOffset(),105);QCOMPARE(pedalRanges142(score),pedals);
    QVERIFY(QMetaObject::invokeMethod(reference,"saveScoreProject"));
    QFileInfo saved(_settings.filePath("workflow-sessions.mscz"));QVERIFY(score->saveCompressedFile(saved,false,false));
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QFile currentMidi(out.filePath("workflow-sessions-current.mid"));QVERIFY(currentMidi.open(QIODevice::WriteOnly));
    ExportMidi currentExport(score);QVERIFY(currentExport.write(&currentMidi,true,true,main->synthesizerState(),MidiExportOptions()));currentMidi.close();
    QVERIFY(main->grab().save(out.filePath("workflow-sessions.png")));
    QFile source(saved.filePath());QVERIFY(source.open(QIODevice::ReadOnly));QSaveFile archive(out.filePath("workflow-sessions.mscz"));
    QVERIFY(archive.open(QIODevice::WriteOnly));QVERIFY(archive.write(source.readAll())>0);QVERIFY(archive.commit());source.close();
    std::unique_ptr<MasterScore> reopened(main->readScore(saved.filePath()));QVERIFY(reopened);QCOMPARE(pedalRanges142(reopened.get()),pedals);
    QCOMPARE(toChord(reopened->findCR(Fraction::fromTicks(start),4))->downNote()->veloOffset(),105);
    qInfo()<<"continuous output"<<driver->metrics();
}
