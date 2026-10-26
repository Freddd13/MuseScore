// SPDX-License-Identifier: GPL-2.0-or-later
// Repeat actual piano output while changing the four score-owned sessions.
#include <algorithm>
#include <mutex>
namespace {
struct SourceTimerResolution {
#ifdef Q_OS_WIN
    using Resolution=UINT(WINAPI*)(UINT);
    HMODULE library=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    Resolution end=library?reinterpret_cast<Resolution>(GetProcAddress(library,"timeEndPeriod")):nullptr;
    bool active=false;
    SourceTimerResolution() {const auto begin=library?reinterpret_cast<Resolution>(GetProcAddress(library,"timeBeginPeriod")):nullptr;active=begin && begin(1)==0;}
    ~SourceTimerResolution() {if(active && end)end(1);if(library)FreeLibrary(library);}
#endif
};
qulonglong sourcePrivateBytes() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb=sizeof(counters);
    using ReadMemory=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);
    const auto read=reinterpret_cast<ReadMemory>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));
    if(read && read(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),sizeof(counters))) return counters.PrivateUsage;
#endif
    return 0;
}

double sourceCpuMilliseconds() {
#ifdef Q_OS_WIN
    FILETIME created{},ended{},kernel{},user{};
    if(GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user)) {
        ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return (k.QuadPart+u.QuadPart)/10000.;
    }
#endif
    return 0.;
}
double sourcePercentile(QVector<double> values,double fraction) {
    if(values.isEmpty()) return 0.;
    std::sort(values.begin(),values.end());
    return values[qBound(0,int((values.size()-1)*fraction),values.size()-1)];
}
class SourcePlaybackDriver final : public Driver {
    std::atomic<Transport> transport{Transport::STOP};
    std::atomic_bool quit{false};std::thread worker;std::mutex lock;
    QVector<double> times;double peak=0.;bool finite=true;
public:
    explicit SourcePlaybackDriver(Seq* sequence):Driver(sequence){}
    ~SourcePlaybackDriver() override {stop();}
    bool init(bool=false) override {return true;}
    bool start(bool=false) override {
        worker=std::thread([this]{auto next=std::chrono::steady_clock::now();while(!quit){
            float audio[512]{};const bool playing=transport.load()==Transport::PLAY;
            QElapsedTimer timer;timer.start();seq->process(256,audio);const double ms=timer.nsecsElapsed()/1.e6;
            if(playing) {std::lock_guard<std::mutex> guard(lock);times.append(ms);for(float value:audio){finite&=std::isfinite(value);peak=qMax(peak,qAbs(double(value)));}}
            next+=std::chrono::microseconds(5805);if(next<std::chrono::steady_clock::now())next=std::chrono::steady_clock::now();std::this_thread::sleep_until(next);
        }});return true;
    }
    bool stop() override {quit=true;if(worker.joinable())worker.join();return true;}
    void startTransport() override {transport=Transport::PLAY;}
    void stopTransport() override {transport=Transport::STOP;}
    Transport getState() override {return transport.load();}
    int sampleRate() const override {return 44100;}
    void resetMetrics(){std::lock_guard<std::mutex> guard(lock);times.clear();peak=0.;finite=true;}
    QVariantMap metrics(){std::lock_guard<std::mutex> guard(lock);int late=0;for(double value:times)late+=value>256000./44100.;return {{"blocks",times.size()},{"blockMedianMilliseconds",sourcePercentile(times,.5)},{"blockP95Milliseconds",sourcePercentile(times,.95)},{"blockMaximumMilliseconds",sourcePercentile(times,1.)},{"blocksOverDeadline",late},{"peak",peak},{"finite",finite}};}
};
}
void TestPluginHost::sourceScorePlayback_data(){QTest::addColumn<QString>("kind");for(const char* name:{"SF2","Pianoteq","CFX"})QTest::newRow(name)<<QString(name);}
void TestPluginHost::sourceScorePlayback(){
#ifdef KUMO_VST_HOST
    if(!qEnvironmentVariableIsSet("P2_SOURCE_SESSIONS_AUDIO"))QSKIP("Enable local score output checks");
    QFETCH(QString,kind);SourceTimerResolution timerResolution;auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    const auto oldCommon=QSettings().value("kumo/vst/commonPiano"),oldRetain=QSettings().value("kumo/vst/retainOpenScores");
    struct Restore {QVariant common,retain;~Restore(){QSettings().setValue("kumo/vst/commonPiano",common);QSettings().setValue("kumo/vst/retainOpenScores",retain);}} restore{oldCommon,oldRetain};
    QSettings().remove("kumo/vst/commonPiano");QSettings().setValue("kumo/vst/retainOpenScores",false);
    QVector<MasterScore*> scores;
    struct CloseScores {QVector<MasterScore*>& scores;~CloseScores(){for(auto score:scores){score->undoStack()->setClean();Ms::mscore->closeScore(score);}}} closeScores{scores};
    for(int i=0;i<4;++i){auto score=performanceScore123(_settings.filePath(QString("source-score-%1-%2.mscx").arg(kind).arg(i)),qMax(128,qEnvironmentVariableIntValue("P2_SOURCE_SCORE_BARS")));QVERIFY(score);scores.append(score);main->setCurrentScoreView(main->appendScore(score));}
    main->setCurrentScoreView(main->scores().indexOf(scores.front()));main->resize(1280,920);main->show();
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();synth->setEffect(0,0);synth->setEffect(1,0);
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());QScopedValueRollback<bool> sequencing(noSeq,false);
    restoreVstForScore(scores.front());auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));QVERIFY(engine);
    if(kind!="SF2"){
        QString error;const auto descriptors=kind=="CFX"?VstHost::scanVst2("E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll",&error):VstHost::scanVst3("C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3",&error);
        QVERIFY2(!descriptors.isEmpty(),qPrintable(error));auto channel=const_cast<Channel*>(scores.front()->midiMapping().front().articulation());
        QVERIFY(!assignVstInstrument(scores.front(),channel,descriptors.front().toMap()).contains("error"));auto plugin=engine->rack().plugin(channel->bank());QVERIFY(plugin);
        if(kind=="CFX")QVERIFY2(plugin->importPreset("E:/music_fonts/CFX Concert Grand/Presets/com.Plogue.Aria/02- Compact/Player/00_Default.ariax",&error),qPrintable(error));
        saveVstEdits(scores.front());auto settings=commonPianoSettings();settings["enabled"]=true;settings["instance"]=commonPianoFromCurrent(scores.front(),channel);settings["name"]="Performance piano";
        QVERIFY(!applyCommonPianoSettings(scores.front(),settings).contains("error"));
    }
    auto sequence=new Seq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->synchronizeStoppedGui();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new SourcePlaybackDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    showHarmonyPanel(true);QPointer<QmlPlugin> panel=harmonyPanel077();QVERIFY(panel);struct Panels {~Panels(){releaseHostPanels();}} panels;
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["chordLabels"]=true;config["coloring"]=true;config["allColor"]=true;config["dualPanel"]=false;
    QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));
    main->showPerformanceEditor(true);auto editor=main->performanceEditor();QVERIFY(editor);
    auto tool=editor->findChild<QComboBox*>("performanceTool");QVERIFY(tool);editor->findChild<QComboBox*>("performanceParameter")->setCurrentIndex(0);editor->findChild<QComboBox*>("performanceScope")->setCurrentIndex(2);
    auto canvas=editor->findChild<QWidget*>("performanceParameterCanvas");QVERIFY(canvas);QVariantList samples;VstHost::Plugin* commonInstance=nullptr;
    for(int round=0;round<qMax(12,qEnvironmentVariableIntValue("P2_SOURCE_SCORE_ROUNDS"));++round){
        QElapsedTimer timer;timer.start();auto score=scores[round%4];main->setCurrentScoreView(main->scores().indexOf(score));QVERIFY(main->currentScoreView()->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),20000);QTest::qWait(80);const auto switchMs=timer.elapsed();
        if(kind!="SF2"){auto channel=score->midiMapping().front().articulation();const int slot=engine->pianoSlot(channel->channel());QVERIFY(slot);auto instance=engine->rack().plugin(slot);QVERIFY(instance);if(commonInstance)QCOMPARE(instance,commonInstance);else commonInstance=instance;}
        QVERIFY(engine->rack().retainedSessions().isEmpty());tool->setCurrentIndex(1);editor->fit(false);QTest::qWait(60);
        QList<Note*> selected;for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int track:{0,4})if(auto element=segment->element(track))if(element->isChord())for(auto note:toChord(element)->notes())selected.append(note);
        QVERIFY(PerformanceSelection::apply(score,selected,selected.front()));editor->selectionChanged();main->activateWindow();QTest::qWait(60);QVERIFY(!score->isPlaying());const int before=score->undoStack()->getCurIdx();
        const bool reverse=(round/4)%2;QPoint from(90,canvas->height()*(reverse?2:1)/3),until(canvas->width()-12,canvas->height()*(reverse?1:2)/3);
        timer.restart();QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,from);QCOMPARE(QWidget::mouseGrabber(),canvas);for(int step=1;step<=8;++step){const auto point=from+(until-from)*step/8;QMouseEvent event(QEvent::MouseMove,point,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&event);QTest::qWait(17);}QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,until);
        QVERIFY(!QWidget::mouseGrabber());QTest::qWait(100);if(editor->hasPending()){qInfo()<<"Pending context"<<round<<"score playing"<<score->isPlaying()<<"sequence playing"<<sequence->isPlaying()<<"background idle"<<sequence->backgroundRenderingIdle()<<"active"<<QApplication::activeWindow()<<"mouse"<<QWidget::mouseGrabber()<<"buttons"<<QApplication::mouseButtons();for(auto label:editor->findChildren<QLabel*>())if(label->isVisible()&&!label->text().isEmpty())qInfo()<<label->objectName()<<label->text();}QTRY_VERIFY_WITH_TIMEOUT(!editor->hasPending(),10000);QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),20000);QVERIFY(score->undoStack()->getCurIdx()>before);const auto editMs=timer.elapsed();
        score->setPlayPos(Fraction());driver->resetMetrics();QVERIFY(getAction("play")->isEnabled());QVERIFY(!getAction("play")->isChecked());timer.restart();getAction("play")->trigger();QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);const auto startMs=timer.elapsed();QTest::qWait(200);QVERIFY(score->isPlaying());
        timer.restart();for(int step=0;step<8;++step){auto view=main->currentScoreView();QPointF point(view->width()/2.,view->height()/2.);QWheelEvent event(point,view->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,step%2?-120:120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(view,&event);QApplication::processEvents();}const auto zoomMs=timer.elapsed();QTest::qWait(250);
        sequence->stopWait();QVERIFY(!score->isPlaying());QVERIFY(!getAction("play")->isChecked());sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();QTRY_VERIFY(!sequence->isPlaying());QVERIFY(!QWidget::mouseGrabber());
        auto row=driver->metrics();QVERIFY(row["finite"].toBool());QVERIFY(row["peak"].toDouble()>.00001);row["round"]=round;row["privateBytes"]=sourcePrivateBytes();row["switchMilliseconds"]=switchMs;row["editMilliseconds"]=editMs;row["startMilliseconds"]=startMs;row["eightZoomMilliseconds"]=zoomMs;row["retainedInstances"]=engine->rack().retainedSessions().size();samples.append(row);qInfo()<<kind<<"score playback"<<row;
        QFile report(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("source-score-playback-"+kind+".json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(samples).toJson());
    }
    showHarmonyPanel(false);QTRY_VERIFY(main->currentScoreView()->notePreviewMarkerBoxes().isEmpty());
#endif
}
void TestPluginHost::sourcePerformance_data() {
    QTest::addColumn<QString>("kind");QTest::addColumn<QString>("mode");
    for(const QString kind:{QString("SF2"),QString("Pianoteq"),QString("CFX")})
        for(const QString mode:{QString("common"),QString("retain"),QString("release")})
            QTest::newRow(qPrintable(kind+"-"+mode)) << kind << mode;
}
void TestPluginHost::sourcePerformance() {
#ifdef KUMO_VST_HOST
    if(!qEnvironmentVariableIsSet("P2_SOURCE_SESSIONS_AUDIO")) QSKIP("Enable local piano output checks");
    QFETCH(QString,kind);QFETCH(QString,mode);
    SourceTimerResolution timerResolution;
    QString error;QVariantMap descriptor;
    if(kind=="SF2") descriptor={{"path",qEnvironmentVariable("P0_TEST_SOUNDFONT")},{"format","SF2"},{"uid","0:0"},{"name","Piano"},{"instrument",true}};
    else {
        const auto found=kind=="Pianoteq"?VstHost::scanVst3("C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3",&error)
            :VstHost::scanVst2("E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll",&error);
        QVERIFY2(!found.isEmpty(),qPrintable(error));descriptor=found.front().toMap();
    }
    const bool common=mode=="common",retain=mode=="retain";
    auto config=sessionConfiguration107(descriptor,"Performance piano");
    if(common) {auto rows=config["slots"].toList();auto row=rows.front().toMap();row["commonPiano"]="performance-piano";rows[0]=row;config["slots"]=rows;}
    const int rounds=qMax(12,qEnvironmentVariableIntValue("P2_SOURCE_PERFORMANCE_ROUNDS"));
    const auto path=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("source-performance-"+kind+"-"+mode+".json");
    QVariantList samples;QVector<VstHost::Plugin*> instances(4,nullptr);QVariantMap report{{"kind",kind},{"mode",mode},{"rate",44100},{"framesPerBlock",256},{"blocksPerRound",192},{"baselinePrivateBytes",sourcePrivateBytes()}};
    auto writeReport=[&] {report["rounds"]=samples;QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;file.write(QJsonDocument::fromVariant(report).toJson());return true;};
    {
        VstHost::Rack rack(false,VstHost::createFluidSource);
        for(int round=0;round<rounds;++round) {
            const int score=round%4;QElapsedTimer watch;watch.start();
            rack.selectSession(QString::number(score),QString("谱%1").arg(score+1),retain);
            QVERIFY(rack.load(config,44100));auto plugin=rack.plugin(1);QVERIFY2(plugin,qPrintable(rack.problems().join('\n')));
            if(kind=="CFX" && round==0) {
                QVERIFY2(plugin->importPreset("E:/music_fonts/CFX Concert Grand/Presets/com.Plogue.Aria/02- Compact/Player/00_Default.ariax",&error),qPrintable(error));
                config=rack.state();
            }
            const auto loadMs=watch.elapsed();
            if(common && round) QCOMPARE(plugin,instances[0]);
            if(retain && instances[score]) QCOMPARE(plugin,instances[score]);
            instances[score]=plugin;
            QCOMPARE(rack.instruments().size(),1);
            QCOMPARE(rack.retainedSessions().size(),retain?qMin(round,3):0);
            rack.bindChannels({1});
            VstHost::Clock clock;clock.rate=44100;clock.playing=true;
            // The same phrases reach the same sample pages on every visit.
            double peak=0.;bool finite=true;QVector<double> blockTimes;int overBlock=0;
            const double cpuBefore=sourceCpuMilliseconds();watch.restart();
            for(int block=0;block<192;++block) {
                if(block%48==0) {rack.midi(0,0xb0,64,0);rack.midi(0,0xb0,123,0);rack.midi(0,0xb0,64,127);
                    for(int pitch:{48,60,64,67,72})rack.midi(0,0x90,pitch,90);}
                if(block%48==40) for(int pitch:{48,60,64,67,72})rack.midi(0,0x80,pitch,0);
                float audio[512]{};QElapsedTimer processing;processing.start();rack.mix(256,audio,clock);rack.master(256,audio,clock);
                const double milliseconds=processing.nsecsElapsed()/1.e6;
                if(block>=16) {blockTimes.append(milliseconds);if(milliseconds>256000./44100.)++overBlock;}
                for(float value:audio) {finite&=std::isfinite(value);peak=qMax(peak,qAbs(double(value)));}
                clock.sample+=256;clock.quarter=clock.sample/clock.rate*clock.tempo/60.;
                // Pace outside the measured callback; CFX streams its samples.
                QTest::qWait(5);
            }
            QVERIFY(finite);QVERIFY2(peak>.00001,"Every visit must produce real piano output");
            samples.append(QVariantMap{{"round",round},{"score",score},{"loadMilliseconds",loadMs},{"privateBytes",sourcePrivateBytes()},
                {"retainedInstances",rack.retainedSessions().size()},{"peak",peak},{"renderWallMilliseconds",watch.elapsed()},
                {"processCpuMilliseconds",sourceCpuMilliseconds()-cpuBefore},{"blockMedianMilliseconds",sourcePercentile(blockTimes,.5)},
                {"blockP95Milliseconds",sourcePercentile(blockTimes,.95)},{"blockMaximumMilliseconds",sourcePercentile(blockTimes,1.)},{"blocksOverDeadline",overBlock}});
            QVERIFY(writeReport());qInfo()<<kind<<mode<<"round"<<round<<samples.back();
        }
        rack.silence(-1,true);rack.releaseInactiveSessions();QVERIFY(rack.retainedSessions().isEmpty());
        report["afterReleaseInactiveBytes"]=sourcePrivateBytes();
        rack.releaseCommonPianos();rack.releaseSession(QString::number((rounds-1)%4));rack.selectSession("closed","",false);
        QVERIFY(rack.load({},44100));QVERIFY(!rack.plugin(1));QVERIFY(rack.retainedSessions().isEmpty());
    }
    QTest::qWait(150);report["afterCloseBytes"]=sourcePrivateBytes();QVERIFY(writeReport());
#endif
}
