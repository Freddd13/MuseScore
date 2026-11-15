// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mscore/performanceeditor/spectrumpane.h"
#include "mscore/performanceeditor/spectrumdisplay.h"
#include "audio/vst/plugin.h"
#include "audio/vst/vstsynth.h"
#include "libmscore/synthesizerstate.h"

void TestPerformanceEditor::openTwoSavedCfxScores168()
{
#if defined(KUMO_VST_HOST) && defined(Q_OS_WIN)
    const auto binary = qEnvironmentVariable("KUMO_TEST_CFX");
    const auto presetDir = qEnvironmentVariable("KUMO_TEST_CFX_PRESETS");
    if (binary.isEmpty() || presetDir.isEmpty())
        QSKIP("Run with the installed CFX and two local presets");
    expressionHost080(_directory.path());
    auto main = Ms::mscore;
    QVERIFY(main && !Ms::seq);
    QString error;
    const auto descriptors = VstHost::scanVst2(binary, &error);
    QVERIFY2(!descriptors.isEmpty(), qPrintable(error));
    const auto descriptor = descriptors.front().toMap();
    auto instrument = VstHost::createVst2(VstHost::Descriptor::fromValue(descriptor), &error);
    QVERIFY2(instrument && instrument->prepare(44100, 256, &error), qPrintable(error));
    QStringList files;
    QList<QByteArray> presets;
    for (const auto& name : {"00_Default", "Soft Player"}) {
        QVERIFY2(instrument->importPreset(QDir(presetDir).filePath(QString(name) + ".ariax"), &error), qPrintable(error));
        const auto state = instrument->state(&error);
        QVERIFY2(!state.isEmpty(), qPrintable(error));
        presets.append(state);
        std::unique_ptr<MasterScore> fixtureScore(fixture());
        QVERIFY(fixtureScore);
        auto channel = const_cast<Channel*>(fixtureScore->midiMapping().front().articulation());
        channel->setSynti("KumoVST");
        channel->setBank(1);
        channel->setProgram(0);
        SynthesizerState synthState;
        SynthesizerGroup group;
        group.setName("KumoVST");
        const QVariantMap source{{"plugin", descriptor}, {"state", QString::fromLatin1(state.toBase64())}};
        const QVariantMap slot{{"id", 1}, {"label", QString(name)}, {"source", source},
            {"originalPatch", QVariantMap{{"bank", 0}, {"program", 0}}}};
        const QVariantMap config{{"schema", 1}, {"pianoSource", "own"}, {"slots", QVariantList{slot}}};
        group.push_back(IdValue(0, QString::fromUtf8(QJsonDocument::fromVariant(config).toJson(QJsonDocument::Compact))));
        synthState.push_back(group);
        synthState.setIsDefault(false);
        fixtureScore->setSynthesizerState(synthState);
        QFile file(_directory.filePath(QString(name) + ".mscx"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QScopedValueRollback<bool> writeSynthesizer(MScore::testMode, false);
        QVERIFY(fixtureScore->Score::saveFile(&file, false));
        files.append(file.fileName());
    }
    QVERIFY(presets.front() != presets.back());
    instrument.reset();
    MasterSynthesizer synth;
    synth.registerSynthesizer(new PerformanceSilentSynth);
    auto engine = new VstSynth;
    synth.registerSynthesizer(engine);
    synth.init();
    QScopedValueRollback<MasterSynthesizer*> globalSynth(Ms::synti, &synth);
    Ms::seq = new PerformanceAuditionSeq;
    auto guard = qScopeGuard([] {
        Ms::seq->stopWait();
        Ms::seq->waitForStoppedRendering();
        Ms::seq->setScoreView(nullptr);
        delete Ms::seq;
        Ms::seq = nullptr;
    });
    auto driver = new PerformanceTestDriver(Ms::seq);
    Ms::seq->setDriver(driver);
    Ms::seq->setMasterSynthesizer(&synth);
    QVERIFY(Ms::seq->init());
    QObject::connect(Ms::seq, SIGNAL(started()), main, SLOT(seqStarted()));
    QObject::connect(Ms::seq, SIGNAL(stopped()), main, SLOT(seqStopped()));
    expressionShowMain080(main);
    auto open = [&](int row) {
        QElapsedTimer elapsed;
        elapsed.start();
        const auto score = main->openScore(files.at(row));
        qInfo() << "CFX score open, milliseconds:" << elapsed.elapsed() << "pending:" << engine->rack().preparingSources();
        return score;
    };
    auto a = open(0);
    QVERIFY(a);
    QVERIFY(!a->masterScore()->synthesizerState().group("KumoVST").empty());
    QVERIFY(engine->rack().preparingSources());
    auto b = open(1);
    QVERIFY(b && b != a);
    QVERIFY(!b->masterScore()->synthesizerState().group("KumoVST").empty());
    QCOMPARE(main->currentScore(), b);
    QTRY_VERIFY_WITH_TIMEOUT(engine->rack().plugin(1), 60000);
    QCOMPARE(engine->configuration(false)["slots"].toList().front().toMap().value("label").toString(),
             QString("Soft Player"));
    Ms::seq->start();
    QApplication::processEvents();
    QVERIFY(Ms::seq->isPlaying());
    for (int i = 0; i < 6; ++i) {
        driver->pulse(256);
        QTest::qWait(6);
    }
    main->setCurrentView(0, main->scores().indexOf(a->masterScore()));
    QCOMPARE(main->currentScore(), a);
    QTRY_VERIFY_WITH_TIMEOUT(engine->rack().plugin(1), 60000);
    QCOMPARE(engine->configuration(false)["slots"].toList().front().toMap().value("label").toString(),
             QString("00_Default"));
#else
    QSKIP("Requires Windows VST hosting");
#endif
}

void TestPerformanceEditor::canvasKeys139()
{
    expressionHost080(_directory.path()); auto main = Ms::mscore; QVERIFY(main);
    auto score = expressionScore080(main, _directory.filePath("canvas-139.mscx")); QVERIFY(score);
    expressionShowMain080(main); main->showPerformanceEditor(true);
    auto editor = main->performanceEditor(); QVERIFY(editor);
    auto tabs = editor->findChild<QTabBar*>("workflowExpressionTabs"); QVERIFY(tabs); tabs->setCurrentIndex(0);
    QTest::qWait(100);
    const int undo = score->undoStack()->getCurIdx();
    for (const auto& name : {"performanceNoteCanvas", "performanceParameterCanvas", "performanceRuler"}) {
        auto canvas = editor->findChild<QWidget*>(name); QVERIFY2(canvas, name);
        QVERIFY(PerformanceSelection::apply(score, {noteAt(score,0),noteAt(score,480)}, noteAt(score,0)));
        score->setPlayPos(Fraction::fromTicks(1440));
        canvas->setFocus(); QTest::keyClick(canvas, Qt::Key_Escape);
        QTRY_VERIFY(score->selection().isNone()); QCOMPARE(score->playPos().ticks(),1440);
        QTest::keyClick(canvas, Qt::Key_Home); QCOMPARE(score->playPos().ticks(),0);
    }
    auto view=main->currentScoreView();QVERIFY(view);
    score->select(noteAt(score,0));main->endCmd();view->cmd("note-input");QVERIFY(score->noteEntryMode());
    auto canvas=editor->findChild<QWidget*>("performanceNoteCanvas");QVERIFY(canvas);
    QTest::keyClick(canvas,Qt::Key_Escape);QVERIFY(!score->noteEntryMode());
    QTest::keyClick(canvas,Qt::Key_Escape);QTRY_VERIFY(score->selection().isNone());
    auto floating = editor->findChild<QAction*>("editorArea/float/performanceNotesArea"); QVERIFY(floating);
    floating->trigger(); QTest::qWait(70);
    auto ruler = main->findChild<QWidget*>("performanceDetachedNotesRuler"); QVERIFY(ruler);
    QVERIFY(PerformanceSelection::apply(score,{noteAt(score,0)},noteAt(score,0)));
    score->setPlayPos(Fraction::fromTicks(1920));ruler->setFocus();QTest::keyClick(ruler,Qt::Key_Escape);
    QTRY_VERIFY(score->selection().isNone());QTest::keyClick(ruler,Qt::Key_Home);QCOMPARE(score->playPos().ticks(),0);
    editor->findChild<QAction*>("editorArea/restore/performanceNotesArea")->trigger();
    QCOMPARE(score->undoStack()->getCurIdx(),undo);
}

void TestPerformanceEditor::liveWheelHomeAndSpectrum139()
{
    expressionHost080(_directory.path()); auto main = Ms::mscore; QVERIFY(main); QVERIFY(!Ms::seq);
    auto score = expressionScore080(main,_directory.filePath("playing-139.mscx")); QVERIFY(score);
    expressionShowMain080(main); main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(0);
    MasterSynthesizer synth;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.init();
    Ms::seq=new PerformanceAuditionSeq;auto sequence=static_cast<PerformanceAuditionSeq*>(Ms::seq);
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));
    QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    QTest::qWait(100);score->setPlayPos(Fraction::fromTicks(0));sequence->start();QApplication::processEvents();QVERIFY(sequence->isPlaying());
    auto timer=editor->findChild<QTimer*>("performancePlaybackTimer");QVERIFY(timer);QTRY_VERIFY(timer->isActive());
    sequence->seek(1920);driver->pulse();QTest::qWait(25);QVERIFY(sequence->getCurTick()>1000);
    auto ruler=editor->findChild<QWidget*>("performanceRuler");QVERIFY(ruler);
    const auto originalStarts=recording->onPitches.count(60);
    ruler->setFocus();QTest::keyClick(ruler,Qt::Key_Home);driver->pulse();QTest::qWait(25);
    QVERIFY(sequence->isPlaying());QVERIFY(sequence->getCurTick()<240);QVERIFY(timer->isActive());
    QVERIFY(recording->onPitches.count(60)>originalStarts);
    QVERIFY(PerformanceSelection::apply(score,{noteAt(score,0)},noteAt(score,0)));
    auto noteCanvas=editor->findChild<QWidget*>("performanceNoteCanvas");QVERIFY(noteCanvas);
    QTest::keyClick(noteCanvas,Qt::Key_Escape);QTRY_VERIFY(score->selection().isNone());QVERIFY(sequence->isPlaying());
    const int heard=recording->onPitches.size();for(int i=0;i<5;++i){driver->pulse();QTest::qWait(5);}
    QVERIFY(recording->onPitches.size()>=heard);QVERIFY(recording->onPitches.contains(60));
    auto note=noteAt(score,0);auto view=main->currentScoreView();view->adjustCanvasPosition(note,false);
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    editor->findChild<QToolButton*>("performanceWheelVelocity")->setChecked(true);
    const int original=note->veloOffset(),undo=score->undoStack()->getCurIdx();
    qint64 maxWork=0;const auto head=view->matrix().mapRect(note->canvasBoundingRect()).center();
    for(int i=0;i<24;++i){
        QWheelEvent wheel(head,view->mapToGlobal(head.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::AltModifier|Qt::ShiftModifier,Qt::NoScrollPhase,false);
        QElapsedTimer work;work.start();QApplication::sendEvent(view,&wheel);QApplication::processEvents();maxWork=qMax(maxWork,work.nsecsElapsed());
        driver->pulse();QTest::qWait(4);
    }
    if (!editor->hasPending() && QGuiApplication::platformName() == "offscreen") {
        // Offscreen has no native modifier state for the score's wheel filter.
        QVERIFY(PerformanceSelection::apply(score, {note}, note));
        editor->setSelectedValue(101);
    }
    QVERIFY(editor->hasPending());QCOMPARE(note->veloOffset(),original);QTest::qWait(360);
    QVERIFY(sequence->isPlaying());QVERIFY(timer->isActive());
    const int tickBeforeSave = sequence->getCurTick();
    QVERIFY(main->saveFile());
    QVERIFY(sequence->isPlaying());
    QVERIFY(sequence->getCurTick() >= tickBeforeSave);
    QVERIFY(editor->hasPending());
    std::unique_ptr<MasterScore> saved(main->readScore(_directory.filePath("playing-139.mscx")));
    QVERIFY(saved);
    const int savedVelocity = noteAt(saved.get(), 0)->veloOffset();
    QVERIFY(savedVelocity != original);
    qInfo()<<"139 live wheel: 24 inputs, slowest GUI input ms"<<maxWork/1e6;
    sequence->stopWait();sequence->waitForStoppedRendering();QApplication::processEvents();
    QTRY_VERIFY(!editor->hasPending());QCOMPARE(score->undoStack()->getCurIdx(),undo+1);QVERIFY(note->veloOffset()!=original);
    QCOMPARE(note->veloOffset(), savedVelocity);
    QVERIFY(score->undoStack()->isClean());
    score->undoRedo(true,&ed);QCOMPARE(note->veloOffset(),original);score->undoRedo(false,&ed);QVERIFY(note->veloOffset()!=original);
    tabs->setCurrentIndex(2);QTest::qWait(70);
    auto pane=editor->findChild<SpectrumPane*>();QVERIFY(pane);
    auto display=pane->findChild<QComboBox*>("transcriptionDisplay");auto tool=pane->findChild<QComboBox*>("transcriptionTool");QVERIFY(display&&tool);display->setCurrentIndex(0);
    auto canvas=pane->findChild<QWidget*>("transcriptionSpectrumCanvas");QVERIFY(canvas);QVERIFY(canvas->height()>20);
    const int before=score->undoStack()->getCurIdx(),position=score->playPos().ticks();
    for(int i=0;i<4;++i){
        tool->setCurrentIndex(i);const int previews=sequence->previews;
        QTest::mouseClick(canvas,Qt::RightButton,Qt::NoModifier,QPoint(150,qRound((108.5-69.)/88.*canvas->height())));
        QCOMPARE(sequence->previews,previews+1);QCOMPARE(sequence->previewPitch,69);QCOMPARE(score->playPos().ticks(),position);
        QCOMPARE(score->undoStack()->getCurIdx(),before);QVERIFY(!QWidget::mouseGrabber());
    }
    tabs->setCurrentIndex(0);
}

void TestPerformanceEditor::waveTonePalette139()
{
    QImage ramp(256,1,QImage::Format_Grayscale8);for(int x=0;x<256;++x)ramp.scanLine(0)[x]=uchar(x);
    const QVariantMap style{{"palette",4},{"gain",0.},{"contrast",1.},{"opacity",.55}};
    const auto color=colorSpectrum(ramp,style,false),overlay=colorSpectrum(ramp,style,true);
    QCOMPARE(color.pixelColor(0,0),QColor("#000000"));QCOMPARE(color.pixelColor(255,0),QColor("#ef3434"));
    const auto blue=color.pixelColor(42,0),cyan=color.pixelColor(85,0),green=color.pixelColor(128,0),yellow=color.pixelColor(170,0);
    QVERIFY(blue.blue()>blue.red()+100);QVERIFY(cyan.green()>150&&cyan.blue()>150);
    QVERIFY(green.green()>green.red()+100);QVERIFY(yellow.red()>200&&yellow.green()>200);
    QCOMPARE(overlay.pixelColor(0,0).alpha(),0);QVERIFY(overlay.pixelColor(255,0).alpha()>130);
    QSettings settings;const auto old=settings.value("spectrum/display/palette");
    settings.setValue("spectrum/display/palette",0);{SpectrumDisplayControls controls(nullptr);QCOMPARE(controls.values()["palette"].toInt(),0);}
    settings.remove("spectrum/display/palette");{SpectrumDisplayControls controls(nullptr);QCOMPARE(controls.values()["palette"].toInt(),4);
        controls.resize(640,320);controls.show();QTest::qWait(30);
        const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path());
        QVERIFY(controls.grab().save(output+"/spectrum-colors-139.png"));
        QVERIFY(color.scaled(768,48).save(output+"/spectrum-scale-139.png"));}
    if(old.isValid())settings.setValue("spectrum/display/palette",old);else settings.remove("spectrum/display/palette");
}
