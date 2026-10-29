// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "libmscore/tempo.h"
void TestPluginHost::visiblePlaybackFollow142()
{
    guiScaling=1.; MuseScore::updateUiStyleAndTheme();
    const auto path = _settings.filePath("visible-follow-142.mscx");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(systemLockFixture138()); file.close();
    auto main = Ms::mscore; auto score = main->readScore(path); QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1280, 900); main->show();
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->showPerformanceEditor(true);
    auto dock = main->findChild<QDockWidget*>("performanceEditorDock"); QVERIFY(dock);
    dock->setFloating(false); main->resizeDocks({dock}, {500}, Qt::Vertical);
    QTest::qWait(150);
    auto view = main->currentScoreView();
    auto button = main->findChild<QToolButton*>("kumoVisibleScoreFollow"); QVERIFY(button);
    auto toolbar=main->findChild<QToolBar*>("transport-tools");QVERIFY(toolbar);toolbar->show();toolbar->setEnabled(true);QTest::qWait(60);
    qInfo()<<"142 follow button"<<button->isVisible()<<button->geometry()<<toolbar->geometry()<<toolbar->isVisible();
    main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("visible-follow-142-before.png"));
    QVERIFY(button->isVisible()); QCOMPARE(button->defaultAction(), getAction("pan"));
    if (!button->isChecked()) QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY(main->panDuringPlayback());
    const double zoom = view->physicalZoomLevel();
    QVector<Measure*> bars; for (auto m = score->firstMeasure(); m; m = m->nextMeasure()) bars.append(m);
    QVERIFY(bars.size() == 8);
    auto checkRow = [&](Measure* measure) {
        view->adjustCanvasPosition(measure, true);
        auto visible = playbackScoreViewport(view);
        const auto system = measure->system();
        const auto y = view->toPhysical(system->canvasPos()).y();
        const auto bottom = view->toPhysical(system->canvasPos()+QPointF(0,system->height())).y();
        qInfo()<<"142 visible row"<<measure->tick().ticks()<<"view"<<visible<<"system"<<y<<bottom;
        return !visible.isEmpty() && y >= visible.top()-1 && y < visible.bottom()
            && (bottom <= visible.bottom()+1 || (bottom-y) > visible.height());
    };
    for (auto measure : bars) QVERIFY(checkRow(measure));
    QCOMPARE(view->physicalZoomLevel(), zoom);
    // Exercise the clipping which occurs while a dock changes the parent's size.
    const auto geometry = view->geometry();
    view->setGeometry(17, 19, geometry.width()+180, geometry.height()+240);
    const auto clipped = playbackScoreViewport(view);
    QVERIFY(clipped.height() < view->height()); QVERIFY(clipped.width() < view->width());
    QVERIFY(checkRow(bars.front())); QVERIFY(checkRow(bars.back()));
    view->setGeometry(geometry);
    QTest::mouseClick(button, Qt::LeftButton); QVERIFY(!main->panDuringPlayback());
    const QPointF offset(view->xoffset(), view->yoffset());
    view->adjustCanvasPosition(bars.front(), true);
    QCOMPARE(QPointF(view->xoffset(), view->yoffset()), offset);
    QTest::mouseClick(button, Qt::LeftButton);
    dock->setFloating(true); QTest::qWait(100);
    QVERIFY(checkRow(bars.front())); QVERIFY(checkRow(bars.back()));
    dock->hide(); QTest::qWait(80);
    QVERIFY(checkRow(bars.front())); QVERIFY(checkRow(bars.back()));
    dock->setFloating(false); dock->show(); QTest::qWait(100);
    main->resizeDocks({dock}, {500}, Qt::Vertical); QTest::qWait(80);
    QVERIFY(checkRow(bars.back()));
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QVERIFY(main->grab().save(out.filePath("visible-follow-142.png")));
    QElapsedTimer watch; watch.start();
    for (int i=0; i<1000; ++i) view->adjustCanvasPosition(bars.back(), true);
    qInfo()<<"142 repeated follow ms"<<watch.nsecsElapsed()/1.e6;
    QCOMPARE(view->physicalZoomLevel(), zoom);
}

void TestPluginHost::visiblePlaybackAudio142()
{
    guiScaling=1.; MuseScore::updateUiStyleAndTheme();
    const auto path = _settings.filePath("audible-follow-142.mscx");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));file.write(systemLockFixture138());file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    main->showPerformanceEditor(true);auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);
    dock->setFloating(false);dock->show();main->resizeDocks({dock},{460},Qt::Vertical);QTest::qWait(100);
    getAction("pan")->setChecked(true);getAction("loop")->setChecked(false);
    auto tempo=new TempoText(score);tempo->setTrack(0);tempo->setParent(score->firstSegment(SegmentType::ChordRest));
    tempo->setXmlText("<sym>metNoteQuarterUp</sym> = 480");tempo->setTempo(8.);
    score->startCmd();score->undoAddElement(tempo);score->endCmd();main->endCmd();
    score->setPlaylistDirty();score->setPlayPos(Fraction());
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());QScopedValueRollback<bool> sequencing(noSeq,false);
    QVERIFY(!Ms::seq);auto sequence=new Seq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->synchronizeStoppedGui();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new SourcePlaybackDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());
    QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    QVERIFY(getAction("play")->isEnabled());getAction("play")->trigger();QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),5000);
    const auto zoom=main->currentScoreView()->physicalZoomLevel();
    QSet<System*> seen;int samples=0;QElapsedTimer watch;watch.start();System* previous=nullptr;
    while(sequence->isPlaying() && watch.elapsed()<10000) {
        QTest::qWait(60);auto measure=score->tick2measure(main->currentScoreView()->cursorTick());
        if(!measure || !measure->system())continue;
        auto system=measure->system();seen.insert(system);
        if(system!=previous){previous=system;continue;}
        auto view=main->currentScoreView();const auto visible=playbackScoreViewport(view);
        const auto y=view->toPhysical(system->canvasPos()).y();
        if(y<visible.top()-1 || y>=visible.bottom())qInfo()<<"142 playback mismatch"<<view->cursorTick().ticks()<<sequence->getCurTick()<<visible<<view->geometry()<<system->canvasPos()<<y<<view->isVisible()<<main->panDuringPlayback();
        QVERIFY2(y>=visible.top()-1 && y<visible.bottom(),"Actual playback must move each new row above the dock");
        ++samples;
    }
    QVERIFY2(!sequence->isPlaying(),"Playback must finish the full eight measures");
    QVERIFY(seen.size()>=2);QVERIFY(samples>10);QCOMPARE(main->currentScoreView()->physicalZoomLevel(),zoom);
    auto metrics=driver->metrics();QVERIFY(metrics.value("peak").toDouble()>0.000001);
    qInfo()<<"142 actual playback rows"<<seen.size()<<"samples"<<samples<<"audio"<<metrics;
    sequence->stopWait();sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();
    sequence->seek(0);driver->resetMetrics();getAction("play")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),5000);
    QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying(),10000);
    const auto warm=driver->metrics();QVERIFY(warm.value("blocks").toInt()>100);
    QVERIFY(warm.value("finite").toBool());QVERIFY(warm.value("peak").toDouble()>0.000001);
    qInfo()<<"142 repeated playback audio"<<warm;
}
