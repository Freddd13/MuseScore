// SPDX-License-Identifier: GPL-2.0-or-later
void TestPluginHost::sourceModeSwitch129_data() {
    QTest::addColumn<QString>("kind");QTest::addColumn<bool>("common");
    for(const QString kind:{QString("Pianoteq"),QString("CFX")})
        for(bool common:{false,true})QTest::newRow(qPrintable(kind+(common?"-common":"-score")))<<kind<<common;
}
void TestPluginHost::sourceModeSwitch129() {
#ifdef KUMO_VST_HOST
    if(!qEnvironmentVariableIsSet("P2_COMMON_PIANO_AUDIO"))QSKIP("Enable installed piano checks");
    QFETCH(QString,kind);QFETCH(bool,common);
    auto main=Ms::mscore;QVERIFY(main);
    if(auto old=main->findChild<QDockWidget*>("kumoVstPanel"))delete old;
    const auto oldCommon=QSettings().value("kumo/vst/commonPiano");
    struct Restore {QVariant value;~Restore(){QSettings().setValue("kumo/vst/commonPiano",value);}} restore{oldCommon};
    QSettings().remove("kumo/vst/commonPiano");
    auto score=vstScore088(_settings.filePath(kind+"-mode-129.mscx"));QVERIFY(score);
    struct Close {MasterScore* score;~Close(){score->undoStack()->setClean();Ms::mscore->closeScore(score);}} close{score};
    main->resize(1280,920);main->show();
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();synth->setEffect(0,0);synth->setEffect(1,0);
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    restoreVstForScore(score);
    QString error;
    const auto descriptors=kind=="CFX"?VstHost::scanVst2("E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll",&error)
        :VstHost::scanVst3("C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3",&error);
    QVERIFY2(!descriptors.isEmpty(),qPrintable(error));
    const auto effects=VstHost::scanVst2(effectFile088(),&error);QVERIFY2(!effects.isEmpty(),qPrintable(error));
    auto channel=const_cast<Channel*>(score->midiMapping().front().articulation());
    QVERIFY(!assignVstInstrument(score,channel,descriptors.front().toMap(),false,{{"plugin",effects.front()}}).contains("error"));
    auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));QVERIFY(engine);
    auto slot=[&]{const int shared=engine->pianoSlot(channel->channel());return shared?shared:channel->bank();};
    auto plugin=engine->rack().plugin(slot());QVERIFY(plugin);
    if(kind=="CFX")QVERIFY2(plugin->importPreset("E:/music_fonts/CFX Concert Grand/Presets/com.Plogue.Aria/02- Compact/Player/00_Default.ariax",&error),qPrintable(error));
    saveVstEdits(score);
    if(common){auto settings=commonPianoSettings();settings["enabled"]=true;settings["instance"]=commonPianoFromCurrent(score,channel);settings["name"]="Mode test piano";QVERIFY(!applyCommonPianoSettings(score,settings).contains("error"));}
    auto storedUnit=[&]{for(const auto& row:engine->runtimeConfiguration()["slots"].toList())if(row.toMap()["id"].toInt()==slot())return row.toMap();return QVariantMap();};
    auto sound=[&]{synth->play(NPlayEvent(ME_NOTEON,channel->channel(),60,95),synth->index("KumoVST"));double peak=0.;
        for(int block=0;block<80;++block){float audio[512]{};synth->process(256,audio);for(float value:audio){if(!std::isfinite(value))return -1.;peak=qMax(peak,qAbs(double(value)));}QTest::qWait(4);}
        synth->allSoundsOff(channel->channel());return peak;};
    QVERIFY(sound()>.00001);
    showVstPanel(channel);auto panel=main->findChild<QDockWidget*>("kumoVstPanel");QVERIFY(panel);VstPanelCleanup088 cleanup{panel};
    const auto available=QGuiApplication::primaryScreen()->availableGeometry();
    panel->setFloating(true);panel->resize(qMin(490,available.width()-20),qMin(870,available.height()-25));
    panel->move(available.right()-panel->width()-10,available.top()+10);panel->show();
    auto mode=panel->findChild<QComboBox*>("kumoVstSourceMode");QVERIFY(mode);
    auto scroll=panel->findChild<QScrollArea*>();QVERIFY(scroll);scroll->ensureWidgetVisible(mode);
    QTRY_VERIFY(mode->isVisible()&&!mode->visibleRegion().isEmpty());QVERIFY(mode->isEnabled());QVERIFY(!mode->currentData().toBool());
    QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstOpenSource"),Qt::LeftButton);
    QTRY_VERIFY(engine->rack().plugin(slot())->editorVisible());QTest::qWait(100);
    if(auto window=panel->findChild<QWidget*>("kumoVstPluginEditor"))window->move(available.left()+5,available.top()+10);
    saveVstEdits(score);
    const auto before=storedUnit();const auto beforeEffects=before["effects"].toList();QCOMPARE(beforeEffects.size(),1);
    const auto preset=engine->rack().plugin(slot())->presetName();
    auto chooseMode=[&](bool independent){
        QSignalSpy activated(mode,SIGNAL(activated(int)));
        scroll->ensureWidgetVisible(mode);panel->raise();panel->activateWindow();QTest::qWait(100);
        const auto panelHandle=reinterpret_cast<HWND>(panel->winId());
        const auto foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
        const auto thisThread=GetCurrentThreadId();
        const bool attached=foregroundThread&&foregroundThread!=thisThread&&AttachThreadInput(thisThread,foregroundThread,TRUE);
        BringWindowToTop(panelHandle);SetForegroundWindow(panelHandle);
        if(attached)AttachThreadInput(thisThread,foregroundThread,FALSE);
        QTest::qWait(100);
        QTest::mouseClick(mode,Qt::LeftButton);QTest::qWait(50);
        if(!mode->view()->isVisible()){qInfo()<<"Mode popup not visible"<<mode->geometry()<<mode->isVisible()<<QApplication::activeWindow();return false;}
        const auto index=mode->model()->index(independent?1:0,0);
        const auto visible=mode->view()->visualRect(index).intersected(mode->view()->viewport()->rect());
        if(visible.isEmpty())return false;
        QTest::mouseMove(mode->view()->viewport(),visible.center());QTest::qWait(250);
        if(!qEnvironmentVariableIsSet("P2_SOURCE_MODE_NATIVE")) {
            QTest::keyClick(mode->view(),independent?Qt::Key_End:Qt::Key_Home);
            QTest::keyClick(mode->view(),Qt::Key_Return);
        } else {
        auto popup=mode->view()->window();
        const auto local=mode->view()->viewport()->mapTo(popup,visible.center());
        POINT point{qRound(local.x()*popup->devicePixelRatioF()),qRound(local.y()*popup->devicePixelRatioF())};
        const auto popupHandle=reinterpret_cast<HWND>(popup->winId());
        if(!ClientToScreen(popupHandle,&point))return false;
        const auto hit=WindowFromPoint(point);DWORD process=0;GetWindowThreadProcessId(hit,&process);
        if(process!=GetCurrentProcessId() || (hit!=popupHandle&&!IsChild(popupHandle,hit))) {
            wchar_t name[256]{};GetClassNameW(hit,name,256);
            qInfo()<<"Popup not at desktop target"<<popupHandle<<hit<<process<<GetCurrentProcessId()<<QString::fromWCharArray(name);return false;
        }
        POINT previous{};GetCursorPos(&previous);SetCursorPos(point.x,point.y);QTest::qWait(100);
        INPUT click[2]{};click[0].type=click[1].type=INPUT_MOUSE;
        click[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;click[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
        const auto sent=SendInput(2,click,sizeof(INPUT));QTest::qWait(150);SetCursorPos(previous.x,previous.y);
        if(sent!=2)return false;
        }
        if(mode->currentData().toBool()!=independent) {
            qInfo()<<"Mode choice failed"<<mode->currentIndex()<<"activated"<<activated.count()<<"at"<<mode->view()->indexAt(visible.center()).row()<<"isolated"<<storedUnit()["source"].toMap()["isolated"];
            QVariantList labels;for(auto label:panel->findChildren<QLabel*>())if(label->isVisible())labels.append(QVariantMap{{"name",label->objectName()},{"text",label->text()}});
            QFile state(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("mode-129-choice-state.json"));
            if(state.open(QIODevice::WriteOnly))state.write(QJsonDocument::fromVariant(labels).toJson());
            panel->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("mode-129-choice-failed.png"));
        }
        return mode->currentData().toBool()==independent;
    };
    QVERIFY(chooseMode(true));QTRY_VERIFY(engine->rack().plugin(slot())->editorVisible());
    QCOMPARE(engine->rack().plugin(slot())->presetName(),preset);QVERIFY(storedUnit()["source"].toMap()["isolated"].toBool());
    QCOMPARE(storedUnit()["effects"].toList(),beforeEffects);QVERIFY(sound()>.00001);
    auto after=storedUnit();auto source=after["source"].toMap();source["isolated"]=false;after["source"]=source;
    QVERIFY(VstHost::sameConfiguration({{"slots",QVariantList{before}}},{{"slots",QVariantList{after}}}));
    const auto saved=_settings.filePath(kind+(common?"-common":"-own")+"-mode-129.mscz");
    QVERIFY(main->saveAs(score,false,saved,"mscz"));
    TemporaryScore reread(main->readScore(saved));QVERIFY(reread);
    const auto group=reread->synthesizerState().group("KumoVST");QVERIFY(!group.empty());
    const auto value=QJsonDocument::fromJson(group.front().data.toUtf8()).toVariant().toMap();
    const auto savedSource=common?value["commonPianoSnapshot"].toMap()["instance"].toMap()["source"].toMap()
        :value["slots"].toList().front().toMap()["source"].toMap();
    QVERIFY(savedSource["isolated"].toBool());
    EditData edit;score->undoRedo(true,&edit);QTRY_VERIFY(!mode->currentData().toBool());QVERIFY(!storedUnit()["source"].toMap()["isolated"].toBool());QVERIFY(sound()>.00001);
    score->undoRedo(false,&edit);QTRY_VERIFY(mode->currentData().toBool());QVERIFY(storedUnit()["source"].toMap()["isolated"].toBool());QVERIFY(sound()>.00001);
    QVERIFY(chooseMode(false));QVERIFY(!storedUnit()["source"].toMap()["isolated"].toBool());QVERIFY(sound()>.00001);
    QTest::qWait(100);
    QVERIFY(panel->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath(kind+(common?"-common":"-own")+"-mode-129.png")));
    QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstRemoveSource"),Qt::LeftButton);QTRY_VERIFY(!mode->isVisible());QVERIFY(sound()>.00001);
    qInfo()<<"Mode switch preserved preset, effects, audio and saved setting"<<kind<<common;
#endif
}

void TestPluginHost::sourceModeDuringPlayback129() {
#ifdef KUMO_VST_HOST
    if(!qEnvironmentVariableIsSet("P2_COMMON_PIANO_AUDIO"))QSKIP("Enable installed piano checks");
    auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);
    if(auto old=main->findChild<QDockWidget*>("kumoVstPanel"))delete old;
    const auto common=QSettings().value("kumo/vst/commonPiano");
    struct Restore {QVariant value;~Restore(){QSettings().setValue("kumo/vst/commonPiano",value);}} restore{common};
    QSettings().remove("kumo/vst/commonPiano");
    auto score=performanceScore123(_settings.filePath("mode-playing-129.mscx"),12);QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,920);main->show();
    struct Close {MasterScore* score;~Close(){score->undoStack()->setClean();Ms::mscore->closeScore(score);}} close{score};
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());QScopedValueRollback<bool> sequencing(noSeq,false);
    restoreVstForScore(score);QString error;
    auto descriptors=VstHost::scanVst3("C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3",&error);QVERIFY2(!descriptors.isEmpty(),qPrintable(error));
    auto channel=const_cast<Channel*>(score->midiMapping().front().articulation());QVERIFY(!assignVstInstrument(score,channel,descriptors.front().toMap()).contains("error"));
    auto sequence=new Seq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->synchronizeStoppedGui();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new SourcePlaybackDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    showVstPanel(channel);auto panel=main->findChild<QDockWidget*>("kumoVstPanel");QVERIFY(panel);VstPanelCleanup088 cleanup{panel};
    auto mode=panel->findChild<QComboBox*>("kumoVstSourceMode");QVERIFY(mode);
    score->setPlayPos(Fraction::fromTicks(1920));getAction("play")->trigger();QTRY_VERIFY(sequence->isPlaying());QTest::qWait(250);
    QTest::mouseClick(mode,Qt::LeftButton);QTRY_VERIFY(mode->view()->isVisible());QTest::keyClick(mode->view(),Qt::Key_End);
    const int before=sequence->getCurTick();QTest::keyClick(mode->view(),Qt::Key_Return);
    QTRY_VERIFY(!sequence->isPlaying());QVERIFY(!score->isPlaying());QVERIFY(!getAction("play")->isChecked());QVERIFY(!QWidget::mouseGrabber());
    QVERIFY(mode->currentData().toBool());
    const int stopped=score->playPos().ticks();qInfo()<<"Mode stopped beat"<<before<<stopped;
    QVERIFY(qAbs(stopped-before)<120);
    driver->resetMetrics();getAction("play")->trigger();QTRY_VERIFY(sequence->isPlaying());QTest::qWait(1000);
    sequence->stopWait();sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();
    const auto result=driver->metrics();QVERIFY(result["finite"].toBool());QVERIFY(result["peak"].toDouble()>.00001);
    qInfo()<<"Mode playback continued"<<result;
#endif
}
