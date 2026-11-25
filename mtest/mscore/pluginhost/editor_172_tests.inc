// GPL-2.0-or-later. Continuous selection, real audio measurement and score saving.
#include "mscore/workflow/audiocalibration.h"
#include "mscore/workflow/scoremixstate.h"
#include "mscore/workflow/documentsettings.h"
#include "mscore/workflow/editorareas.h"
#include "effects/effect.h"
#include <QProgressBar>
#include <QLabel>

#ifdef Q_OS_WIN
static void rightBox172(QWidget* widget,bool add=false,bool subtract=false)
{
    const QPoint from(PerformanceViewport::timelineStart+2,PerformanceViewport::parameterPadding+2),to(widget->width()-PerformanceViewport::rightMargin-2,widget->height()-PerformanceViewport::parameterPadding-2);
    QVERIFY2(widget->isVisible()&&!widget->visibleRegion().isEmpty(),"Box selection starts inside a visible editor area");
    NativeModifiers171 keyboard(add);BYTE keys[256];GetKeyboardState(keys);
    if(subtract){keys[VK_MENU]=keys[VK_LMENU]=0x80;SetKeyboardState(keys);}
    POINT client;const auto window=inputWindow171(widget,from,&client);
    DWORD process=0;GetWindowThreadProcessId(window,&process);QVERIFY(process==GetCurrentProcessId());
    const WPARAM modifier=add?MK_CONTROL:0;
    SendMessageW(window,WM_RBUTTONDOWN,MK_RBUTTON|modifier,MAKELPARAM(client.x,client.y));QTest::qWait(5);
    auto global=widget->mapToGlobal(to);POINT last{global.x(),global.y()};ScreenToClient(window,&last);
    SendMessageW(window,WM_MOUSEMOVE,MK_RBUTTON|modifier,MAKELPARAM(last.x,last.y));QTest::qWait(5);
    SendMessageW(window,WM_RBUTTONUP,modifier,MAKELPARAM(last.x,last.y));QTest::qWait(5);
    QVERIFY(!QWidget::mouseGrabber());QVERIFY(!implicitOwner171());
}
#endif
void TestPluginHost::sharedEditorSelection172()
{
#ifndef Q_OS_WIN
    QSKIP("Native Windows gesture check");
#else
    auto main=Ms::mscore;guiScaling=1.;MuseScore::updateUiStyleAndTheme();
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());
    if(!WorkspacesManager::currentWorkspace())WorkspacesManager::initCurrentWorkspace();
    auto score=performanceScore123(_settings.filePath("shared-selection172.mscx"),2);QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1360,940);main->show();
    main->showPerformanceEditor(true);auto editor=main->performanceEditor();QTest::qWait(100);
    main->resizeDocks({main->findChild<QDockWidget*>("performanceEditorDock")},{600},Qt::Vertical);
    DocumentSettings(score).setValue("performanceEditor/pageArea/2/performanceParametersArea",true);
    editor->findChild<QComboBox*>("performanceScope")->setCurrentIndex(2);
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    editor->fit();QTest::qWait(50);
    auto parameter=editor->findChild<QWidget*>("performanceParameterCanvas");QVERIFY(parameter);QVERIFY(parameter->isVisible());
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);
    const auto content=score->state();
    for(int page:{0,1,2,0}) {
        tabs->setCurrentIndex(page);QTest::qWait(50);
        QVERIFY(setEditorAreaVisible(editor,"performanceParametersArea",true));QTest::qWait(30);
        qInfo()<<"Selecting on page"<<page<<parameter->size()<<parameter->isVisible();
        rightBox172(parameter);QCOMPARE(score->selection().noteList().size(),size_t(10));
        rightBox172(parameter,true);QCOMPARE(score->selection().noteList().size(),size_t(10));
        rightBox172(parameter,false,true);QCOMPARE(score->selection().noteList().size(),size_t(0));
        QCOMPARE(score->state(),content);
        auto ruler=editor->findChild<QWidget*>("performanceRuler");QVERIFY(ruler);
        nativeClick171(ruler,QPoint(ruler->width()/2,ruler->height()/2));QVERIFY(score->playPos().ticks()>0);
    }
    workflowContext()->setExpression(score,{{"revision","selection172"},{"candidate",QVariantMap{{"velocity",QVariantList{QVariantMap{{"tick",0},{"track",0},{"index",0},{"id","first"},{"target",96}}}}}}});
    QTest::qWait(50);rightBox172(parameter);QCOMPARE(score->selection().noteList().size(),size_t(10));
    QCOMPARE(workflowContext()->expression().value("candidate").toMap().value("velocity").toList().front().toMap().value("target").toInt(),96);
    workflowContext()->setExpression(nullptr,{});
    editor->showMidiTracks(true);auto pane=editor->findChild<QWidget*>("midiTrackPane");QVERIFY(pane);
    const auto state=[pane]{QVariantMap value;QMetaObject::invokeMethod(pane,"state",Q_RETURN_ARG(QVariantMap,value));return value;};
    const auto midiFile=_settings.filePath("parameter-box172.mid");
    {QFile file(midiFile);QVERIFY(file.open(QIODevice::WriteOnly));file.write(midiTrackFixture160());}
    QVERIFY(QMetaObject::invokeMethod(pane,"importFile",Q_ARG(QString,midiFile)));
    QTRY_COMPARE_WITH_TIMEOUT(state().value("tracks").toList().size(),1,5000);
    QTest::qWait(80);rightBox172(parameter);QCOMPARE(state().value("selectedNotes").toInt(),3);
    auto velocity=editor->findChild<QSpinBox*>("midiTrackVelocity");QVERIFY(velocity);velocity->setValue(104);
    editor->findChild<QToolButton*>("midiTrackSetVelocity")->click();
    MidiTrackDocument midi;midi.setScore(score);
    for(const auto& note:midi.track(state().value("activeId").toString()).value("notes").toList())QCOMPARE(note.toMap().value("velocity").toInt(),104);
    editor->showMidiTracks(false);
    main->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/selection172.png");
#endif
}
void TestPluginHost::scoreMixCalibration172()
{
    auto main=Ms::mscore;
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());
    QScopedValueRollback<bool> sequencing(noSeq,false);QVERIFY(!Ms::seq);
    auto sequence=new Seq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* value){value->stopWait();value->synchronizeStoppedGui();value->waitForStoppedRendering();value->setScoreView(nullptr);delete value;Ms::seq=nullptr;});
    auto driver=new PalettePlaybackDriver093(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());QVERIFY(sequence->init());
    auto score=performanceScore123(_settings.filePath("mix-a172.mscx"),2);auto other=performanceScore123(_settings.filePath("mix-b172.mscx"),2);QVERIFY(score);QVERIFY(other);
    const auto firstView=main->appendScore(score);main->setCurrentScoreView(firstView);
    auto channel=score->masterScore()->midiMapping(0)->articulation();QVERIFY(channel);
    channel->setVolume(57);channel->setPan(23);channel->setReverb(49);channel->setChorus(21);channel->setSolo(true);
    synth->setGain(.04f);
    if(auto effect=synth->effect(0)){auto state=effect->state();if(!state.empty()){state.front().data=QString::number(state.front().data.toDouble()*.9);effect->setState(state);}}
    const auto effectA=synth->effect(0)?synth->effect(0)->state():SynthesizerGroup();
    const auto file=_settings.filePath("mix-a172.mscz");QVERIFY(main->saveAs(score,false,file,"mscz"));
    const auto secondView=main->appendScore(other);main->setCurrentScoreView(secondView);synth->setGain(.12f);
    QVERIFY(main->saveAs(other,false,_settings.filePath("mix-b172.mscz"),"mscz"));
    main->setCurrentScoreView(firstView);QTest::qWait(60);QVERIFY(qAbs(synth->gain()-.04f)<1e-6);
    auto restored=main->readScore(file);QVERIFY(restored);main->setCurrentScoreView(main->appendScore(restored));QTest::qWait(60);
    QVERIFY(qAbs(synth->gain()-.04f)<1e-6);
    auto loaded=restored->masterScore()->midiMapping(0)->articulation();
    QCOMPARE(int(loaded->volume()),57);QCOMPARE(int(loaded->pan()),23);QCOMPARE(int(loaded->reverb()),49);QCOMPARE(int(loaded->chorus()),21);QVERIFY(loaded->solo());
    if(!effectA.empty()){auto actual=synth->effect(0)->state();QCOMPARE(actual.front().data,effectA.front().data);}
    auto window=new QWidget;window->resize(400,380);auto layout=new QVBoxLayout(window);
    auto calibration=new AudioCalibrationPanel(window,qEnvironmentVariable("KUMO_TEST_CALIBRATION_PROGRAM"));layout->addWidget(calibration);
    std::unique_ptr<QWidget> panel(window);window->show();
    calibration->findChild<QToolButton*>("audioCalibrationToggle")->setChecked(true);
    auto measure=calibration->findChild<QToolButton*>("audioCalibrationMeasure");auto cancel=calibration->findChild<QToolButton*>("audioCalibrationCancel");auto apply=calibration->findChild<QToolButton*>("audioCalibrationApply");
    measure->click();QTRY_VERIFY_WITH_TIMEOUT(!cancel->isVisible(),90000);
    auto result=DocumentSettings(restored).value("audioCalibration/result").toMap();
    window->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/calibration172-measured.png");
    QVERIFY2(result.contains("peak"),qPrintable(calibration->findChild<QLabel*>("audioCalibrationStatus")->text()));
    QVERIFY(result.value("peak").toDouble()>0);QVERIFY(apply->isEnabled());
    const float before=synth->gain();
    auto preview=calibration->findChild<QToolButton*>("audioCalibrationPreview");
    preview->click();QTRY_VERIFY(sequence->isPlaying());QVERIFY(qAbs(synth->outputGain()-before)>1e-6);QVERIFY(qAbs(synth->gain()-before)<1e-6);
    driver->pulse();
    QVERIFY(main->saveAs(restored,false,_settings.filePath("preview-save172.mscz"),"mscz"));
    QTRY_VERIFY(sequence->isPlaying());
    for(const auto& value:restored->synthesizerState().group("master"))if(value.id==2)QVERIFY(qAbs(value.data.toFloat()-before)<1e-6);
    preview->click();QTRY_VERIFY(sequence->isPlaying());QVERIFY(qAbs(synth->gain()-before)<1e-6);
    apply->click();
    const auto after=synth->gain();QVERIFY(qAbs(after-before)>1e-6);
    QTRY_VERIFY(sequence->isPlaying());calibration->findChild<QToolButton*>("audioCalibrationStop")->click();
    QTRY_VERIFY(!sequence->isPlaying());QTRY_VERIFY(!(main->currentScoreView()->mscoreState()&STATE_PLAY));
    const double estimated=result.value("peakDb").toDouble()+20*std::log10(after/before);QVERIFY(qAbs(estimated+6)<.02);
    getAction("undo")->trigger();QVERIFY(qAbs(synth->gain()-before)<1e-6);
    getAction("redo")->trigger();QVERIFY(qAbs(synth->gain()-after)<1e-6);
    QVERIFY(main->saveAs(restored,false,_settings.filePath("calibrated172.mscz"),"mscz"));
    measure->click();QTRY_VERIFY_WITH_TIMEOUT(!cancel->isVisible(),90000);
    const auto checked=DocumentSettings(restored).value("audioCalibration/result").toMap();
    QVERIFY2(checked.contains("peakDb"),qPrintable(calibration->findChild<QLabel*>("audioCalibrationStatus")->text()));
    QVERIFY(qAbs(checked.value("peakDb").toDouble()+6)<.25);
    getAction("file-save")->trigger();
    auto reopened=main->readScore(_settings.filePath("calibrated172.mscz"));QVERIFY(reopened);
    main->setCurrentScoreView(main->appendScore(reopened));QTest::qWait(80);
    QVERIFY(qAbs(synth->gain()-after)<1e-6);
    auto recover=calibration->findChild<QToolButton*>("audioCalibrationRestore");QVERIFY(recover->isEnabled());recover->click();QVERIFY(qAbs(synth->gain()-before)<1e-6);
    window->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/calibration172.png");
    qInfo()<<"Measured real SF2 peak"<<result.value("peakDb")<<"dBFS; measured again after applying"<<checked.value("peakDb")<<"dBFS";
}
