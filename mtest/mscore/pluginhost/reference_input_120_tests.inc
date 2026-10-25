// GPL-2.0-or-later. Follow reference playback with actual palette and player clicks.
#include <thread>
#include <atomic>
#include <chrono>
class ReferenceCallbackDriver120 : public Driver {
    std::atomic<Transport> transport{Transport::STOP};
    std::atomic_bool quit{false};
    std::thread worker;
public:
    explicit ReferenceCallbackDriver120(Seq* sequence):Driver(sequence){}
    ~ReferenceCallbackDriver120() override {stop();}
    bool init(bool=false) override {return true;}
    bool start(bool=false) override {worker=std::thread([this]{float buffer[4096]={};while(!quit){seq->process(2048,buffer);std::this_thread::sleep_for(std::chrono::milliseconds(46));}});return true;}
    bool stop() override {quit=true;if(worker.joinable())worker.join();return true;}
    void startTransport() override {transport=Transport::PLAY;}
    void stopTransport() override {transport=Transport::STOP;}
    Transport getState() override {return transport.load();}
    int sampleRate() const override {return 44100;}
    void pulse(){QTest::qWait(15);}
};
void TestPluginHost::referencePanelsAfterPlayback120()
{
    auto main=Ms::mscore;
    guiScaling=1.;MuseScore::updateUiStyleAndTheme();
    if(!WorkspacesManager::currentWorkspace())WorkspacesManager::initCurrentWorkspace();
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());QScopedValueRollback<bool> sequencing(noSeq,false);
    QVERIFY(!Ms::seq);auto sequence=new Seq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->synchronizeStoppedGui();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new ReferenceCallbackDriver120(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();main->resize(1360,920);main->show();
    if(!main->getPaletteWidget()||main->getPaletteWidget()->isHidden())getAction("toggle-palette")->trigger();
    auto palette=main->getPaletteWidget();QVERIFY(palette);main->resizeDocks({palette},{240},Qt::Horizontal);
    auto paletteWindow=const_cast<QQuickView*>(palette->view());QVERIFY(paletteWindow&&paletteWindow->rootObject());
    {QList<QQuickItem*> pending{paletteWindow->rootObject()};while(!pending.isEmpty()){auto item=pending.takeFirst();pending.append(item->childItems());if(!item->property("placeholderText").toString().isEmpty())item->setProperty("text",QString());}}
    main->showPerformanceEditor(true);showReferencePanel(true);QTest::qWait(160);
    QmlPlugin* root=nullptr;
    for(auto w:QGuiApplication::allWindows())if(auto v=qobject_cast<QQuickView*>(w))if(auto p=qobject_cast<QmlPlugin*>(v->rootObject()))if(p->menuPath()=="Plugins.Reference Player")root=p;
    QVERIFY(root);
    QVERIFY(QMetaObject::invokeMethod(root,"setPanelFloating",Q_ARG(bool,true)));
    QVERIFY(QMetaObject::invokeMethod(root,"resizePanel",Q_ARG(int,920),Q_ARG(int,680)));
    QTemporaryDir audio(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("input120-XXXXXX"));QVERIFY(audio.isValid());
    const auto path=audio.filePath("reference.wav");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
    const int frames=22050*4;QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);data.writeRawData("RIFF",4);data<<quint32(36+frames*2);data.writeRawData("WAVEfmt ",8);data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(22050)<<quint32(44100)<<quint16(2)<<quint16(16);data.writeRawData("data",4);data<<quint32(frames*2);for(int i=0;i<frames;++i)data<<qint16(0);file.close();
    QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(path).toString())));
    auto object=[&](const char* property){auto v=root->property(property);return v.canConvert<QJSValue>()?v.value<QJSValue>().toQObject():v.value<QObject*>();};
    QTRY_VERIFY_WITH_TIMEOUT(object("media")&&object("media")->property("duration").toDouble()>3900,10000);
    object("media")->setProperty("volume",0.);root->setProperty("jointVolume",0.);auto project=referenceProject();QVERIFY(project->property("available").toBool());
    auto click=[&](const char* name){auto item=root->findChild<QQuickItem*>(name);if(!item&&QByteArray(name)=="referenceLoop")for(auto candidate:root->findChildren<QQuickItem*>())if(candidate->property("text").toString()==QString::fromUtf8("循环")&&candidate->isVisible()){item=candidate;break;}if(!item||!item->isVisible()||!item->isEnabled())return false;auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,point);return true;};
    auto clickHeader=[&]{QList<QQuickItem*> queue{paletteWindow->rootObject()};while(!queue.isEmpty()){auto item=queue.takeFirst();queue.append(item->childItems());if(!QByteArray(item->metaObject()->className()).startsWith("TreePaletteHeader")||!item->isVisible())continue;auto point=item->mapToScene(QPointF(10,item->height()/2)).toPoint();if(!paletteWindow->rootObject()->contains(point))continue;bool before=item->property("expanded").toBool();QTest::mouseClick(paletteWindow,Qt::LeftButton,Qt::NoModifier,point);QTest::qWait(50);return item->property("expanded").toBool()!=before;}return false;};
    auto dump=[&]{qInfo()<<"input120"<<main->state()<<main->currentScoreView()->mscoreState()<<"palette"<<palette->isEnabled()<<"grab"<<QWidget::mouseGrabber()<<"quick"<<root->window()->mouseGrabberItem()<<paletteWindow->mouseGrabberItem()<<"modal"<<QGuiApplication::modalWindow()<<"audio"<<project->property("playing")<<"music"<<sequence->isPlaying();};
    for(int round=0;round<8;++round){
        root->setProperty("looping",false);root->setProperty("transportTarget",round%2?2:0);
        if(round%2){score->selection().setRangeTicks(Fraction(),score->lastMeasure()->endTick(),0,score->nstaves());score->selection().updateSelectedElements();root->setProperty("a",0.);root->setProperty("b",4000.);QVERIFY(QMetaObject::invokeMethod(root,"associate"));}
        QVERIFY(click("referenceJointPlay"));QTRY_VERIFY_WITH_TIMEOUT(project->property("playing").toBool(),10000);
        if(round%2)QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);
        for(int pulse=0;pulse<8;++pulse){driver->pulse();QTest::qWait(10);}
        if(round==0||round==2||round==4||round==5){
            auto editor=main->performanceEditor();QVERIFY(editor);auto target=editor->findChild<QWidget*>(round==0?"performanceRuler":round==4?"performanceNoteCanvas":"performanceCanvas");
            if(!target&&round!=0&&round!=4)target=editor->findChild<QWidget*>("performanceParameterCanvas");
            QVERIFY(target&&target->isVisible());
            if(round==0){target->window()->activateWindow();QVERIFY(QTest::qWaitForWindowActive(target->window(),2000));}
            const QPoint point=round==0?QPoint(110,target->height()/2):round==5?QPoint(20,target->height()/2):QPoint(target->width()-25,target->height()-10);
            QTest::mousePress(target,Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(QWidget::mouseGrabber(),target);
            if(round==0){root->focusPanel();QTRY_VERIFY_WITH_TIMEOUT(QWidget::mouseGrabber()==nullptr,2000);}
            else {QEvent leftWindow(QEvent::WindowDeactivate);QCoreApplication::sendEvent(target->window(),&leftWindow);}
            dump();QVERIFY2(QWidget::mouseGrabber()==nullptr,"原曲播放中移至其它工具窗口，编辑器归还鼠标");
            QEvent returned(QEvent::WindowActivate);QCoreApplication::sendEvent(target->window(),&returned);
            QTest::mouseRelease(target,Qt::LeftButton,Qt::NoModifier,point);
        }
        dump();
        if(round>=6)QTRY_VERIFY_WITH_TIMEOUT(!project->property("playing").toBool(),10000);
        else QVERIFY(click("referenceJointPlay"));
        QTRY_VERIFY_WITH_TIMEOUT(!project->property("playing").toBool(),10000);QTRY_VERIFY(!sequence->isPlaying());
        sequence->synchronizeStoppedGui();QTest::qWait(80);dump();
        QVERIFY2(QWidget::mouseGrabber()==nullptr,"停播后鼠标可交给其它面板");QVERIFY(!QGuiApplication::modalWindow());QVERIFY(palette->isEnabled());QVERIFY2(clickHeader(),"原曲或一起播放后，符号组仍响应实际点击");
        bool loop=root->property("looping").toBool();QVERIFY(click("referenceLoop"));QTRY_COMPARE(root->property("looping").toBool(),!loop);
    }
    const auto artifacts=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QVERIFY(root->window()->grabWindow().save(artifacts.filePath("reference-after-playback-120.png")));
    QVERIFY(paletteWindow->grabWindow().save(artifacts.filePath("palette-after-reference-120.png")));
    showReferencePanel(false);
}
