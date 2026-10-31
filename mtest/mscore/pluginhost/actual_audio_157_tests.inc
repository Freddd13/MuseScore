// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in sound-card test. Uses the shipping PortAudio stream parameters and
// native sequencer callback, adding counters without recording other apps.
#if defined(USE_PORTAUDIO) && defined(KUMO_VST_HOST)
#include "portaudio.h"
namespace {
class ActualOutput157 final : public Driver {
    PaStream* stream = nullptr;
    bool initialized = false, started = false;
    int rate = 44100;
    std::atomic<Transport> transport{Transport::STOP};
    std::atomic<qulonglong> blocks{0}, frames{0}, underflows{0}, late{0};
    std::atomic<unsigned long> largestBlock{0};
    std::atomic<double> peak{0}, maximumMs{0};
    std::unique_ptr<float[]> capture;
    unsigned captureCapacity = 0;
    std::atomic<unsigned> capturePosition{0};
    std::atomic_bool capturing{false};
    static int callback(const void*, void* output, unsigned long count,
                        const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags flags, void* owner) {
        auto driver = static_cast<ActualOutput157*>(owner);
        const bool playing = driver->transport.load() == Transport::PLAY;
        const auto before = std::chrono::steady_clock::now();
        driver->seq->setInitialMillisecondTimestampWithLatency();
        driver->seq->process(unsigned(count), static_cast<float*>(output));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-before).count();
        if (playing) {
            driver->blocks.fetch_add(1); driver->frames.fetch_add(count);
            if (flags & paOutputUnderflow) driver->underflows.fetch_add(1);
            if (ms > count*1000./driver->rate) driver->late.fetch_add(1);
            driver->maximumMs.store(qMax(driver->maximumMs.load(), ms));
            driver->largestBlock.store(qMax(driver->largestBlock.load(), count));
            double maximum = driver->peak.load();
            const auto samples = static_cast<const float*>(output);
            for (unsigned long i=0; i<count*2; ++i)
                maximum = !std::isfinite(samples[i]) ? INFINITY : qMax(maximum, qAbs(double(samples[i])));
            driver->peak.store(maximum);
            if (driver->capturing.load(std::memory_order_acquire)) {
                const auto offset = driver->capturePosition.load(std::memory_order_relaxed);
                const auto amount = qMin(count, static_cast<unsigned long>(driver->captureCapacity-offset));
                for (unsigned i=0; i<amount; ++i)
                    driver->capture[offset+i] = samples[i*2];
                driver->capturePosition.store(offset+unsigned(amount), std::memory_order_release);
                if (offset+amount == driver->captureCapacity)
                    driver->capturing.store(false, std::memory_order_release);
            }
        }
        return paContinue;
    }
public:
    QString error, device, api;
    double latency = 0;
    explicit ActualOutput157(Seq* sequence) : Driver(sequence) {}
    ~ActualOutput157() override {
        stop(); if(stream) Pa_CloseStream(stream); if(initialized) Pa_Terminate();
    }
    bool init(bool=false) override {
        auto code = Pa_Initialize();
        if (code != paNoError) { error=QString::fromUtf8(Pa_GetErrorText(code)); return false; }
        initialized=true;
        const int index=qEnvironmentVariableIntValue("P2_ACTUAL_AUDIO_DEVICE");
        const auto info=Pa_GetDeviceInfo(index);
        if(!info || info->maxOutputChannels<2) {error="No stereo output at requested device";return false;}
        device=QString::fromUtf8(info->name);api=QString::fromUtf8(Pa_GetHostApiInfo(info->hostApi)->name);
        rate=int(info->defaultSampleRate);
        PaStreamParameters output{};output.device=index;output.channelCount=2;
        output.sampleFormat=paFloat32;output.suggestedLatency=info->defaultLowOutputLatency;
        code=Pa_OpenStream(&stream,nullptr,&output,rate,0,0,callback,this);
        if(code!=paNoError) {error=QString::fromUtf8(Pa_GetErrorText(code));return false;}
        const auto actual=Pa_GetStreamInfo(stream);rate=int(actual->sampleRate);latency=actual->outputLatency;
        captureCapacity = unsigned(rate/2);
        capture.reset(new float[captureCapacity]);
        return true;
    }
    bool start(bool=false) override {
        if(started)return true;const auto code=Pa_StartStream(stream);started=code==paNoError;
        if(!started)error=QString::fromUtf8(Pa_GetErrorText(code));return started;
    }
    bool stop() override { if(started){Pa_StopStream(stream);started=false;}return true; }
    void startTransport() override {transport=Transport::PLAY;}
    void stopTransport() override {transport=Transport::STOP;}
    Transport getState() override {return transport.load();}
    int sampleRate() const override {return rate;}
    bool beginCapture() {
        if (capturing.load(std::memory_order_acquire)) return false;
        capturePosition.store(0, std::memory_order_relaxed);
        capturing.store(true, std::memory_order_release);
        return true;
    }
    bool captureReady() const {
        return !capturing.load(std::memory_order_acquire)
            && capturePosition.load(std::memory_order_acquire) == captureCapacity;
    }
    QVector<float> capturedAudio() const {
        return captureReady() ? QVector<float>(capture.get(), capture.get()+captureCapacity) : QVector<float>();
    }
    void resetMetrics() {blocks=0;frames=0;underflows=0;late=0;largestBlock=0;peak=0;maximumMs=0;}
    QVariantMap metrics() const {
        return {{"device",device},{"api",api},{"sampleRate",rate},{"outputLatencyMilliseconds",latency*1000.},
                {"blocks",qulonglong(blocks.load())},{"frames",qulonglong(frames.load())},
                {"largestBlock",uint(largestBlock.load())},{"peak",peak.load()},
                {"processMaximumMilliseconds",maximumMs.load()},{"lateBlocks",qulonglong(late.load())},
                {"outputUnderflows",qulonglong(underflows.load())}};
    }
};
}
#endif

void TestPluginHost::actualAudioOutput157_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("SF3") << QString("SF3");
    QTest::newRow("CFX") << QString("CFX");
    QTest::newRow("Pianoteq") << QString("Pianoteq");
}
void TestPluginHost::actualAudioOutput157() {
#if defined(USE_PORTAUDIO) && defined(KUMO_VST_HOST)
    if(!qEnvironmentVariableIsSet("P2_ACTUAL_AUDIO_DEVICE")) QSKIP("Explicit local sound-card selection required");
    QFETCH(QString,kind);auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);SourceTimerResolution timer;
    const auto previousCommon=QSettings().value("kumo/vst/commonPiano");
    const auto restoreCommon=qScopeGuard([&]{QSettings().setValue("kumo/vst/commonPiano",previousCommon);});
    QSettings().remove("kumo/vst/commonPiano");
    auto score=main->readScore(qEnvironmentVariable("P2_ACTUAL_AUDIO_SCORE"));QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1100,800);main->show();
    const auto closeScore=qScopeGuard([&]{releaseHostPanels();score->undoStack()->setClean();main->closeScore(score);});
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    QScopedValueRollback<bool> sequencing(noSeq,false);
    auto sequence=new Seq;sequence->setMasterSynthesizer(synth.get());
    const auto stop=qScopeGuard([&]{
        workflowContext()->finishPlaybackSession();sequence->stopWait();sequence->synchronizeStoppedGui();
        sequence->waitForStoppedRendering();sequence->setScoreView(nullptr);delete sequence;Ms::seq=nullptr;
    });
    auto driver=new ActualOutput157(sequence);sequence->setDriver(driver);
    QVERIFY2(driver->init(),qPrintable(driver->error));
    synth->setSampleRate(driver->sampleRate());synth->init();synth->setGain(.15f);
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    if(kind!="SF3") {
        QString error;
        const auto found = kind=="CFX"
            ? VstHost::scanVst2("E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll",&error)
            : VstHost::scanVst3("C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3",&error);
        QVERIFY2(!found.isEmpty(),qPrintable(error));auto channel=const_cast<Channel*>(score->midiMapping().front().articulation());
        QVERIFY(!assignVstInstrument(score,channel,found.front().toMap()).contains("error"));
        auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));auto plugin=engine->rack().plugin(channel->bank());QVERIFY(plugin);
        if(kind=="CFX")
            QVERIFY2(plugin->importPreset("E:/music_fonts/CFX Concert Grand/Presets/com.Plogue.Aria/02- Compact/Player/00_Default.ariax",&error),qPrintable(error));
        saveVstEdits(score);
    }
    Ms::seq=sequence;QScopedValueRollback<Sequencer*> events(MScore::seq,sequence);
    QVERIFY2(sequence->init(),qPrintable(driver->error));sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    QVariantList rounds;
    for(int round=0;round<2;++round) {
        main->setWindowTitle("Sound-card check - "+kind+QString(" - %1/2[*]").arg(round+1));
        score->setPlayPos(Fraction());driver->resetMetrics();getAction("play")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);
        if (round == 0) {
            QTest::qWait(7200);QVERIFY(sequence->getCurTick()>1920);
            getAction("play")->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying(),3000);
        }
        else {
            QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying(),35000);
        }
        sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();
        QVERIFY(!getAction("play")->isChecked());QVERIFY(!score->isPlaying());QVERIFY(!QWidget::mouseGrabber());
        auto result=driver->metrics();QVERIFY(result["frames"].toULongLong()>qulonglong(driver->sampleRate()));
        QVERIFY(std::isfinite(result["peak"].toDouble()));QVERIFY(result["peak"].toDouble()>.00001);
        result["round"]=round+1;result["ending"]=round==0 ? "manual-stop" : "natural-end";
        rounds.append(result);qInfo()<<kind<<"actual sound card"<<result;
        QTest::qWait(250);
    }
    const auto folder=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path());
    QFile report(folder+"/actual-audio-157-"+kind+".json");QVERIFY(report.open(QIODevice::WriteOnly));
    report.write(QJsonDocument::fromVariant(rounds).toJson());
#else
    QSKIP("Windows audio and VST host");
#endif
}
