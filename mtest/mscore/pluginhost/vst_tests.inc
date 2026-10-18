void vstNativeSelectionUndoAndSave() {
#ifdef KUMO_VST_HOST
    const auto piano=qEnvironmentVariable("P2_TEST_PIANOTEQ","C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3");
    if(!QFileInfo::exists(piano))QSKIP("Pianoteq is not installed on this machine");
    auto main=Ms::mscore;
    const auto input=_settings.filePath("vst-score.mscx");QFile f(input);QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel><program value=\"0\"/><synti>Fluid</synti></Channel></Instrument></Part><Staff id=\"1\"><Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>quarter</durationType><Note><pitch>60</pitch><tpc>14</tpc></Note></Chord><Rest><durationType>quarter</durationType></Rest><Rest><durationType>quarter</durationType></Rest><Rest><durationType>quarter</durationType></Rest></voice></Measure></Staff></Score></museScore>");f.close();
    auto score=main->readScore(input);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));const auto screen=QGuiApplication::primaryScreen()->availableGeometry();main->resize(qMin(1100,screen.width()),qMin(800,screen.height()));main->show();
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->setEffect(0,0);synth->setEffect(1,0);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));QVERIFY(engine);
    QString error;auto descriptors=VstHost::scanVst3(piano,&error);QVERIFY2(!descriptors.isEmpty(),qPrintable(error));auto descriptor=descriptors[0].toMap();
    QSettings().setValue("kumo/vst/catalog",QJsonDocument::fromVariant(QVariantList{descriptor}).toJson(QJsonDocument::Compact));
    auto channel=const_cast<Channel*>(score->midiMapping().front().articulation());QVERIFY(channel);const auto originalBank=channel->bank();const auto before=score->undoStack()->getCurIdx();
    const auto originalMidi=_settings.filePath("vst-before.mid");QVERIFY(main->saveMidi(score,originalMidi));
    showVstPanel(channel);auto panel=main->findChild<QDockWidget*>("kumoVstPanel");QVERIFY(panel);QTRY_VERIFY(panel->isVisible());auto choose=panel->findChild<QPushButton*>("kumoVstChoose");QVERIFY(choose);QVERIFY(choose->isEnabled());QTest::mouseClick(choose,Qt::LeftButton);
    QTRY_COMPARE(channel->synti(),QString("KumoVST"));QCOMPARE(score->undoStack()->getCurIdx(),before+1);QVERIFY(channel->bank()>0);QVERIFY(engine->problems().isEmpty());QVERIFY(!engine->configuration(false)["slots"].toList().isEmpty());
    const auto vstMidi=_settings.filePath("vst-after.mid");QVERIFY(main->saveMidi(score,vstMidi));QFile a(originalMidi),b(vstMidi);QVERIFY(a.open(QIODevice::ReadOnly)&&b.open(QIODevice::ReadOnly));QCOMPARE(b.readAll(),a.readAll());
    routeVstChannels(synth.get(),score);NPlayEvent on(ME_NOTEON,channel->channel(),60,100);synth->play(on,synth->index("KumoVST"));float audio[512]{};synth->setPlaybackContext(0,0,120,true);synth->process(256,audio);double peak=0;for(auto v:audio)peak=qMax(peak,qAbs(double(v)));QVERIFY(peak>0);
    synth->allSoundsOff(-1);synth->process(256,audio);
    QBuffer rendered;QVERIFY(main->saveAudio(score,&rendered));QVERIFY(rendered.data().size()>44100*8);double exportPeak=0;for(int i=0;i+4<=rendered.data().size();i+=4){float value=0;std::memcpy(&value,rendered.data().constData()+i,4);QVERIFY(std::isfinite(value));exportPeak=qMax(exportPeak,qAbs(double(value)));}QVERIFY(exportPeak>.001);
    QFileInfo saved(_settings.filePath("vst-score.mscz"));QVERIFY(score->saveCompressedFile(saved,false,false));std::unique_ptr<MasterScore> read(main->readScore(saved.absoluteFilePath()));QVERIFY(read);QVERIFY(!read->synthesizerState().group("KumoVST").empty());QCOMPARE(read->midiMapping().front().articulation()->synti(),QString("KumoVST"));
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QFile copy(saved.absoluteFilePath());QVERIFY(copy.open(QIODevice::ReadOnly));QFile artifact(output.filePath("vst-score-069.mscz"));QVERIFY(artifact.open(QIODevice::WriteOnly));artifact.write(copy.readAll());artifact.close();
    QVERIFY(panel->grab().save(output.filePath("vst-panel-069.png")));panel->setFloating(true);panel->resize(430,660);QTest::qWait(100);QVERIFY(panel->grab().save(output.filePath("vst-floating-069.png")));panel->resize(320,500);QTest::qWait(50);auto scroll=panel->findChild<QScrollArea*>();QVERIFY(scroll);QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);QVERIFY(panel->grab().save(output.filePath("vst-narrow-069.png")));panel->setFloating(false);
    EditData edit;score->undoRedo(true,&edit);QCOMPARE(channel->synti(),QString("Fluid"));QCOMPARE(channel->bank(),originalBank);score->undoRedo(false,&edit);QCOMPARE(channel->synti(),QString("KumoVST"));QVERIFY(!engine->configuration(false)["slots"].toList().isEmpty());
    std::unique_ptr<MasterScore> blank(main->readScore(input));QVERIFY(blank);QVERIFY(blank->synthesizerState().group("KumoVST").empty());
    restoreVstForScore(nullptr);QVERIFY(engine->configuration(false).isEmpty());restoreVstForScore(score);QVERIFY(!engine->configuration(false).isEmpty());
    if(qEnvironmentVariableIsSet("P2_TEST_CFX")){
        const auto cfx=qEnvironmentVariable("P2_TEST_CFX_DLL","E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll");
        const auto preset=qEnvironmentVariable("P2_TEST_CFX_PRESET","E:/music_fonts/CFX Concert Grand/Presets/com.Plogue.Aria/02- Compact/Player/00_Default.ariax");
        QVERIFY(QFileInfo::exists(cfx)&&QFileInfo::exists(preset));auto list=VstHost::scanVst2(cfx,&error);QVERIFY2(!list.isEmpty(),qPrintable(error));auto result=assignVstInstrument(score,channel,list[0].toMap());QVERIFY2(!result.contains("error"),qPrintable(result["error"].toString()));auto plugin=engine->rack().plugin(channel->bank());QVERIFY(plugin);engine->rack().suspend();bool imported=plugin->importPreset(preset,&error);engine->rack().resume();QVERIFY2(imported,qPrintable(error));QVERIFY(!applyVstConfiguration(score,engine->configuration()).contains("error"));
        QBuffer cfxAudio;QVERIFY(main->saveAudio(score,&cfxAudio));double cfxPeak=0;for(int i=0;i+4<=cfxAudio.data().size();i+=4){float v=0;std::memcpy(&v,cfxAudio.data().constData()+i,4);QVERIFY(std::isfinite(v));cfxPeak=qMax(cfxPeak,qAbs(double(v)));}QVERIFY(cfxPeak>.001);qInfo()<<"CFX export peak"<<cfxPeak;
    }
    auto remove=panel->findChild<QPushButton*>("kumoVstRemoveSource");QVERIFY(remove);const int removeBefore=score->undoStack()->getCurIdx();QTest::mouseClick(remove,Qt::LeftButton);QCOMPARE(channel->synti(),QString("Fluid"));QCOMPARE(score->undoStack()->getCurIdx(),removeBefore+1);QVERIFY(engine->configuration(false).isEmpty());
    const auto sf=qEnvironmentVariable("P0_TEST_SOUNDFONT");QVERIFY(QFileInfo::exists(sf));QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({sf}));
    const auto effectFile=qEnvironmentVariable("P2_TEST_EFFECT_DLL");if(!effectFile.isEmpty()){
        auto effectList=VstHost::scanVst2(effectFile,&error);QVERIFY2(!effectList.isEmpty(),qPrintable(error));auto effect=effectList[0].toMap();
        const QVariantMap internal{{"path",sf},{"format","SF2"},{"uid",QString::number(channel->bank())+":"+QString::number(channel->program())},{"name","SF2 Piano"},{"instrument",true}};
        const int beforeEffect=score->undoStack()->getCurIdx();auto adopted=assignVstInstrument(score,channel,internal,false,QVariantMap{{"plugin",effect}});QVERIFY(!adopted.contains("error"));QVERIFY2(engine->problems().isEmpty(),qPrintable(engine->problems().join('\n')));QCOMPARE(score->undoStack()->getCurIdx(),beforeEffect+1);QCOMPARE(engine->rack().effects(channel->bank()).size(),1);
        QBuffer withFx;QVERIFY(main->saveAudio(score,&withFx));double peakFx=0;for(int i=0;i+4<=withFx.data().size();i+=4){float value=0;std::memcpy(&value,withFx.data().constData()+i,4);QVERIFY(std::isfinite(value));peakFx=qMax(peakFx,qAbs(double(value)));}QVERIFY(peakFx>.001);qInfo()<<"SF2 part with VST effect export peak"<<peakFx;
        score->undoRedo(true,&edit);QCOMPARE(channel->synti(),QString("Fluid"));QVERIFY(engine->configuration(false).isEmpty());
    }
    auto recover=panel->findChild<QPushButton*>("kumoVstRestoreSource");QVERIFY(recover);QTest::mouseClick(recover,Qt::LeftButton);QCOMPARE(channel->synti(),QString("KumoVST"));QVERIFY(!engine->configuration(false).isEmpty());
    SynthControl originalSynth(main);originalSynth.setScore(score);originalSynth.show();QTest::qWait(50);QVERIFY(originalSynth.isVisible());originalSynth.close();
    panel->hide();
#else
    QSKIP("Windows x64 VST host");
#endif
}
void vstDisabledRetainsStateAndUsesSF2() {
#ifdef KUMO_VST_HOST
    class Recording final:public Synthesizer {
    public:
        QList<PlayEvent> events;QList<MidiPatch*> patches;
        const char* name()const override{return "test";}bool loadSoundFonts(const QStringList&)override{return true;}
        std::vector<SoundFontInfo> soundFontsInfo()const override{return {};}
        void process(unsigned,float*,float*,float*)override{}
        void play(const PlayEvent& e)override{events.append(e);}
        const QList<MidiPatch*>& getPatchInfo()const override{return patches;}
        SynthesizerGroup state()const override{return {};}
        bool setState(const SynthesizerGroup&)override{return true;}
    } fallback;
    VstSynth unused;unused.init(44100);std::array<float,512> wave{},copy{};for(int i=0;i<512;++i)wave[i]=std::sin(float(i)*.1f);copy=wave;unused.mixMaster(256,wave.data());unused.processMaster(256,wave.data());QCOMPARE(std::memcmp(wave.data(),copy.data(),sizeof(wave)),0);QElapsedTimer cost;cost.start();for(int i=0;i<10000;++i){unused.mixMaster(256,wave.data());unused.processMaster(256,wave.data());}qInfo()<<"Unused VST 10k blocks ms"<<cost.nsecsElapsed()/1e6;
    VstSynth disabled(false,&fallback,false);disabled.init(44100);
    const QVariantMap data{{"schema",1},{"slots",QVariantList{QVariantMap{{"id",1},{"label","Piano"},{"source",QVariantMap{{"plugin",QVariantMap{{"path","does-not-load.dll"}}},{"state","saved-state"}}},{"originalPatch",QVariantMap{{"bank",128},{"program",4}}}}}}};
    QVERIFY(disabled.setConfiguration(data));QVERIFY(disabled.rack().instruments().isEmpty());QCOMPARE(disabled.configuration(),data);
    disabled.play(PlayEvent(ME_CONTROLLER,0,CTRL_HBANK,0));disabled.play(PlayEvent(ME_CONTROLLER,0,CTRL_LBANK,1));disabled.play(PlayEvent(ME_CONTROLLER,0,CTRL_PROGRAM,0));disabled.play(PlayEvent(ME_NOTEON,0,60,90));QCOMPARE(fallback.events.size(),4);QCOMPARE(fallback.events[0].dataB(),1);QCOMPARE(fallback.events[1].dataB(),0);QCOMPARE(fallback.events[2].dataB(),4);QCOMPARE(fallback.events[3].velo(),90);QVERIFY(fallback.active());QVERIFY(!disabled.state().empty());
#else
    QSKIP("Windows x64 VST host");
#endif
}
