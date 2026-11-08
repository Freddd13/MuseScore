// GPL-2.0-or-later. Held keys, pointer release and real per-channel reverb output.
#include "effects/zita1/zita.h"
#include <QKeySequenceEdit>
#include <QDialogButtonBox>

void TestPluginHost::velocityPeekHold159()
{
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();QVERIFY(editor);
    auto editingTabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(editingTabs);
    QTest::mouseClick(editingTabs,Qt::LeftButton,Qt::NoModifier,editingTabs->tabRect(0).center());
    main->raise();main->activateWindow();QVERIFY(QTest::qWaitForWindowActive(main,2000));QTest::qWait(80);
    auto options=editor->findChild<QToolButton*>("performanceOperationSettings");QVERIFY(options);
    QTest::mouseClick(options,Qt::LeftButton);QTest::qWait(30);
    auto dialog=main->findChild<QDialog*>("performanceSettings");QVERIFY(dialog);
    auto keys=dialog->findChild<QKeySequenceEdit*>("performancePeekKey");QVERIFY(keys);keys->setKeySequence(QKeySequence("Alt+Q"));
    auto delay=dialog->findChild<QSpinBox*>("performancePeekReleaseMs");QVERIFY(delay);delay->setValue(250);
    dialog->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("peek-settings-159.png"));
    auto buttons=dialog->findChild<QDialogButtonBox*>();QVERIFY(buttons);QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);
    // Let Windows finish the modal-to-main-window activation before sending
    // held-key events directly to the score widget.
    QTest::qWait(80);
    main->raise();main->activateWindow();QVERIFY(QTest::qWaitForWindowActive(main,2000));
    auto view=main->currentScoreView();view->setFocus();QTest::keyPress(view,Qt::Key_Q,Qt::AltModifier);QVERIFY(editor->temporaryVelocityVisible());
    for(int i=0;i<12;++i){
        QKeyEvent release(QEvent::KeyRelease,Qt::Key_Q,Qt::AltModifier,QString(),true);QApplication::sendEvent(view,&release);
        QKeyEvent press(QEvent::KeyPress,Qt::Key_Q,Qt::AltModifier,QString(),true);QApplication::sendEvent(view,&press);
        QTest::qWait(30);QVERIFY(editor->temporaryVelocityVisible());
    }
    QTest::keyRelease(view,Qt::Key_Control,Qt::AltModifier);QVERIFY(editor->temporaryVelocityVisible());
    QTest::keyRelease(view,Qt::Key_Q,Qt::AltModifier);QVERIFY(editor->temporaryVelocityVisible());
    QTest::qWait(120);QVERIFY(editor->temporaryVelocityVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!editor->temporaryVelocityVisible(),1000);
    QTest::keyPress(view,Qt::Key_Q,Qt::AltModifier);QVERIFY(editor->temporaryVelocityVisible());
    QEvent deactivate(QEvent::ApplicationDeactivate);QApplication::sendEvent(qApp,&deactivate);QVERIFY(!editor->temporaryVelocityVisible());
    QTest::keyRelease(view,Qt::Key_Q,Qt::AltModifier);
    main->showPerformanceEditor(false);
}

void TestPluginHost::keyboardPointer159()
{
    HPiano piano;piano.resize(900,140);piano.show();QTest::qWait(30);
    QSignalSpy pressed(&piano,&HPiano::keyPressed),released(&piano,&HPiano::keyReleased);
    auto target=piano.scene()->items().front();QVERIFY(target);
    const QPoint point=piano.mapFromScene(target->sceneBoundingRect().center());
    QTest::mousePress(piano.viewport(),Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(pressed.count(),1);
    QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(&piano,&deactivate);QVERIFY(released.count()>=1);
    QTest::mouseRelease(piano.viewport(),Qt::LeftButton,Qt::NoModifier,point);
    QTest::mouseClick(piano.viewport(),Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(pressed.count(),2);
    QVERIFY(!piano.scene()->mouseGrabberItem());
    auto before=piano.grab().toImage();piano.setPlaybackActive(true);piano.setPressedPlaybackPitches({60});
    QVERIFY(piano.grab().toImage()!=before);piano.setPressedPlaybackPitches({});piano.setPlaybackActive(false);
    QCOMPARE(piano.grab().toImage(),before);
}

void TestPluginHost::graceSpacing159()
{
    auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));auto rest=score->findCR(Fraction(),0);QVERIFY(rest);
    score->startCmd();if(rest->isRest())score->setNoteRest(rest->segment(),0,NoteVal(60),Fraction(1,4));
    auto chord=toChord(score->findCR(Fraction(),0));score->setGraceNote(chord,62,NoteType::APPOGGIATURA,DIVISION/2);score->setGraceNote(chord,64,NoteType::APPOGGIATURA,DIVISION/2);score->endCmd();score->doLayout();
    const auto graces=chord->graceNotesBefore();QCOMPARE(graces.size(),2);
    const qreal original=graces.front()->pos().x();
    score->startCmd();graces.front()->undoChangeProperty(Pid::GRACE_SPACING,.8);graces.front()->undoChangeProperty(Pid::GRACE_MAIN_SPACING,1.2);score->endCmd();score->doLayout();
    QVERIFY(graces.front()->pos().x()<original);QCOMPARE(chord->getProperty(Pid::GRACE_SPACING).toDouble(),.8);
    main->cmd(getAction("undo"));QCOMPARE(chord->getProperty(Pid::GRACE_SPACING).toDouble(),0.0);
    main->cmd(getAction("redo"));QCOMPARE(graces.front()->getProperty(Pid::GRACE_MAIN_SPACING).toDouble(),1.2);
    QTemporaryDir folder;QFileInfo saved(folder.filePath("grace-spacing.mscz"));QVERIFY(score->saveCompressedFile(saved,false,false));
    std::unique_ptr<MasterScore> copy(main->readScore(saved.filePath()));QVERIFY(copy);
    auto restored=toChord(copy->findCR(Fraction(),0));QCOMPARE(restored->getProperty(Pid::GRACE_SPACING).toDouble(),.8);
    QCOMPARE(restored->getProperty(Pid::GRACE_MAIN_SPACING).toDouble(),1.2);
    score->startCmd();score->setGraceNote(chord,65,NoteType::GRACE8_AFTER,DIVISION/2);score->setGraceNote(chord,67,NoteType::GRACE8_AFTER,DIVISION/2);score->endCmd();score->doLayout();
    const auto after=chord->graceNotesAfter();QCOMPARE(after.size(),2);
    const qreal gap=after[1]->pos().x()-after[0]->pos().x();
    score->startCmd();after[0]->undoChangeProperty(Pid::GRACE_SPACING,1.6);after[0]->undoChangeProperty(Pid::GRACE_MAIN_SPACING,2.0);score->endCmd();score->doLayout();
    QVERIFY(after[1]->pos().x()-after[0]->pos().x()>gap);
    for(auto grace:after){QVERIFY(std::isfinite(grace->pos().x()));QVERIFY(grace->pos().x()>chord->upNote()->headWidth());}
    const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QFileInfo example(output.filePath("grace-spacing-159.mscz"));QVERIFY(score->saveCompressedFile(example,false,false));
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    auto image=observer.arrangementImage(imageRequest141(observer,{{"whole",true},{"firstMeasure",1},{"lastMeasure",1}}));
    QVERIFY2(!image.contains("error"),qPrintable(image.value("error").toString()));
    QImage rendered;QVERIFY(rendered.loadFromData(QByteArray::fromBase64(image.value("pngBase64").toString().toLatin1()),"PNG"));
    QVERIFY(rendered.save(output.filePath("grace-spacing-159.png")));
    std::unique_ptr<MasterScore> both(main->readScore(example.filePath()));QVERIFY(both);auto final=toChord(both->findCR(Fraction(),0));
    QCOMPARE(final->graceNotesAfter().size(),2);QCOMPARE(final->getProperty(Pid::GRACE_SPACING).toDouble(),1.6);
}

namespace {
class RecordingReverb159 final:public ZitaReverb {
public:
    double dryEnergy=0,sendEnergy=0;
    void processWithSend(int n,float* dry,const float* send,float* out)override{
        for(int i=0;i<n*2;++i){dryEnergy+=double(dry[i])*dry[i];if(send)sendEnergy+=double(send[i])*send[i];}
        ZitaReverb::processWithSend(n,dry,send,out);
    }
};
void saveAudio159(const QString& name,const QVector<float>& samples){
    QFile file(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",QDir::tempPath())).filePath(name));
    if(!file.open(QIODevice::WriteOnly))return;
    QDataStream output(&file);output.setByteOrder(QDataStream::LittleEndian);
    output.writeRawData("RIFF",4);output<<quint32(36+samples.size()*2);output.writeRawData("WAVEfmt ",8);
    output<<quint32(16)<<quint16(1)<<quint16(2)<<quint32(44100)<<quint32(44100*4)<<quint16(4)<<quint16(16);
    output.writeRawData("data",4);output<<quint32(samples.size()*2);
    for(float value:samples)output<<qint16(qBound(-32768,qRound(value*32767),32767));
}
class SendProbe159 final:public Synthesizer {
    QList<MidiPatch*> patches;bool first=true;
public:
    float send=0;
    const char* name()const override{return "SendProbe159";}
    bool loadSoundFonts(const QStringList&)override{return true;}
    std::vector<SoundFontInfo> soundFontsInfo()const override{return {};}
    void process(unsigned n,float* dry,float* reverb,float*)override{if(first&&n){dry[0]+=.3f;dry[1]+=.3f;reverb[0]+=.3f*send;reverb[1]+=.3f*send;first=false;}}
    void play(const PlayEvent&)override{first=true;}
    const QList<MidiPatch*>&getPatchInfo()const override{return patches;}
    SynthesizerGroup state()const override{return {};}
    bool setState(const SynthesizerGroup&)override{return true;}
};
double sendTail159(int slot,float level){
    MasterSynthesizer synth;auto source=new SendProbe159;source->send=level;synth.registerSynthesizer(source);
    synth.registerEffect(slot,new ZitaReverb);synth.setSampleRate(44100);synth.setEffect(slot,0);source->setActive();
    double energy=0;for(int at=0;at<44100*2;at+=256){float samples[512]{};synth.process(256,samples);for(int i=0;i<512;++i){if(!std::isfinite(samples[i]))return -1;if(at>4410)energy+=double(samples[i])*samples[i];}}
    return energy;
}
}
void TestPluginHost::nativeReverbSend159_data(){QTest::addColumn<int>("slot");QTest::newRow("effect-A")<<0;QTest::newRow("effect-B")<<1;}
void TestPluginHost::nativeReverbSend159(){QFETCH(int,slot);const double dry=sendTail159(slot,0),wet=sendTail159(slot,1);qInfo()<<"159 send tail"<<slot<<dry<<wet;QVERIFY(dry>=0);QVERIFY(dry<1.e-10);QVERIFY(wet>1.e-7);}

void TestPluginHost::kawaiReverbSend159()
{
    const QString font=qEnvironmentVariable("KUMO_KAWAI_SF2");if(font.isEmpty())QSKIP("Set KUMO_KAWAI_SF2 to check the actual Kawai SF2");
    QVERIFY(QFileInfo::exists(font));
    MasterSynthesizer synth;auto source=new FluidS::Fluid;synth.registerSynthesizer(source);
    QVector<RecordingReverb159*> reverbs;for(int i=0;i<3;++i){auto reverb=new RecordingReverb159;reverbs.append(reverb);synth.registerEffect(0,reverb);}synth.setSampleRate(44100);QVERIFY(source->loadSoundFonts({font}));
    QVector<QVector<float>> audio;const int levels[]={0,40,127};
    for(int test=0;test<3;++test){
        synth.allSoundsOff(-1);synth.setEffect(0,test);synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_PROGRAM,0),0);synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_VOLUME,100),0);synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_REVERB_SEND,levels[test]),0);synth.play(NPlayEvent(ME_NOTEON,0,60,100),0);
        QVector<float> samples;for(int at=0;at<44100*3;at+=256){if(at==256*35)synth.play(NPlayEvent(ME_NOTEON,0,60,0),0);float block[512]{};synth.process(256,block);for(float v:block){QVERIFY(std::isfinite(v));samples.append(v);}}audio.append(samples);
    }
    for(int i=0;i<3;++i){QVERIFY(reverbs[i]->dryEnergy>1.e-6);const double ratio=reverbs[i]->sendEnergy/reverbs[i]->dryEnergy,expected=std::pow(levels[i]/128.0,2);qInfo()<<"159 native send ratio"<<levels[i]<<ratio<<expected;QVERIFY(qAbs(ratio-expected)<1.e-5);}
    double difference=0,middle=0;for(int i=44100;i<audio[0].size();++i){const double d=audio[2][i]-audio[0][i],m=audio[1][i]-audio[0][i];difference+=d*d;middle+=m*m;}
    for(int i=0;i<3;++i)saveAudio159(QString("kawai-cc91-%1.wav").arg(levels[i]),audio[i]);
    qInfo()<<"159 actual Kawai CC91 tail difference"<<middle<<difference;QVERIFY(middle>1.e-7);QVERIFY(difference>middle);
}

void TestPluginHost::privateSf2Reverb159()
{
#ifdef KUMO_VST_HOST
    const QString font=qEnvironmentVariable("KUMO_KAWAI_SF2");if(font.isEmpty())QSKIP("Set KUMO_KAWAI_SF2");
    MasterSynthesizer synth;auto source=new VstSynth(true,nullptr);synth.registerSynthesizer(source);
    QVector<RecordingReverb159*> reverbs;for(int i=0;i<3;++i){auto reverb=new RecordingReverb159;reverbs.append(reverb);synth.registerEffect(0,reverb);}synth.setSampleRate(44100);
    VstHost::Descriptor descriptor;descriptor.format="SF2";descriptor.path=font;descriptor.uid="0:0";descriptor.name="Kawai";descriptor.instrument=true;
    QVariantMap slot{{"id",1},{"label","Kawai"},{"source",QVariantMap{{"plugin",descriptor.value()}}},{"originalPatch",QVariantMap{{"bank",0},{"program",0}}}};
    QVERIFY(source->setConfiguration({{"slots",QVariantList{slot}}}));QVERIFY(source->problems().isEmpty());
    QVector<QVector<float>> audio;const int levels[]={0,40,127};
    for(int test=0;test<3;++test){
        synth.allSoundsOff(-1);synth.setEffect(0,test);
        synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_HBANK,0),0);synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_LBANK,1),0);
        synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_PROGRAM,0),0);synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_VOLUME,100),0);
        synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_REVERB_SEND,levels[test]),0);synth.play(NPlayEvent(ME_NOTEON,0,60,100),0);
        QVector<float> samples;for(int at=0;at<44100*3;at+=256){if(at==256*35)synth.play(NPlayEvent(ME_NOTEON,0,60,0),0);float block[512]{};synth.process(256,block);for(float v:block){QVERIFY(std::isfinite(v));samples.append(v);}}audio.append(samples);
    }
    for(int i=0;i<3;++i){QVERIFY(reverbs[i]->dryEnergy>1.e-6);const double ratio=reverbs[i]->sendEnergy/reverbs[i]->dryEnergy,expected=std::pow(levels[i]/128.0,2);qInfo()<<"159 private send ratio"<<levels[i]<<ratio<<expected;QVERIFY(qAbs(ratio-expected)<1.e-5);}
    double low=0,high=0;for(int i=44100;i<audio[0].size();++i){double a=audio[1][i]-audio[0][i],b=audio[2][i]-audio[0][i];low+=a*a;high+=b*b;}
    for(int i=0;i<3;++i)saveAudio159(QString("kawai-rack-cc91-%1.wav").arg(levels[i]),audio[i]);
    qInfo()<<"159 private SF2 CC91 tail difference"<<low<<high;QVERIFY(low>1.e-7);QVERIFY(high>low);
#else
    QSKIP("VST rack is disabled");
#endif
}
