// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mscore/workflow/miditracks.h"
#include "mscore/workflow/midiinput.h"
#include "mscore/workflow/miditransport.h"
#include <QDialogButtonBox>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include "audiodrivers/driver.h"
#include "mscore/workflow/midisources.h"
#include "mscore/workflow/referencefactory.h"
#include <QSlider>
namespace {
// In the UI check the visible rectangle comes from the same cached rows used
// by drawing and hit testing; actual mouse events still perform every edit.
QByteArray midiTrackFixture160() {
    // Track 0 changes tempo after one quarter; track 1 has a pedal and three
    // notes. Different MIDI channels and equal-time retriggers are retained.
    QByteArray header=QByteArray::fromHex("4d546864000000060001000201e0");
    const auto clock=QByteArray::fromHex("00ff510307a12000c00000b05b288360ff51030f424000ff2f00");
    const auto notes=QByteArray::fromHex("00ff03055069616e6f00c00000b0407f00903c508360803c000090405f836080400000914364836081430000b0400000ff2f00");
    auto chunk=[](const QByteArray& body){QByteArray value("MTrk",4);for(int i=3;i>=0;--i)value.append(char(body.size()>>(i*8)));return value+body;};
    return header+chunk(clock)+chunk(notes);
}
}
void TestPluginHost::midiTracksEditor160() {
    auto main=Ms::mscore;QVERIFY(main);auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1240,850);main->show();main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();QVERIFY(editor);editor->showMidiTracks(true);
    auto pane=editor->findChild<QWidget*>("midiTrackPane");QVERIFY(pane);
    auto state=[pane]{QVariantMap value;QMetaObject::invokeMethod(pane,"state",Q_RETURN_ARG(QVariantMap,value));return value;};
    const auto out=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");const QString file=out+"/editor-midi-160.mid";
    QFile source(file);QVERIFY(source.open(QIODevice::WriteOnly));source.write(midiTrackFixture160());source.close();
    QVERIFY(QMetaObject::invokeMethod(pane,"importFile",Q_ARG(QString,file)));
    QTRY_COMPARE_WITH_TIMEOUT(state().value("tracks").toList().size(),1,5000);
    auto tree=editor->findChild<QTreeWidget*>("midiTrackList");QVERIFY(tree);QCOMPARE(tree->topLevelItemCount(),2);QVERIFY(!state().value("activeId").toString().isEmpty());
    const auto id=state().value("activeId").toString();MidiTrackDocument document;document.setScore(score);QVERIFY(document.track(id).value("mute").toBool());
    auto notes=editor->findChild<QWidget*>("performanceNoteCanvas");QVERIFY(notes);
    if(auto split=editor->findChild<QSplitter*>("performanceSplitter"))split->setSizes({220,180,0});
    auto pitch=notes->findChild<QScrollBar*>();QVERIFY(pitch);pitch->setValue(6000);QTest::qWait(80);
    auto visible=state().value("visibleNotes").toList();QVERIFY2(!visible.isEmpty(),qPrintable(state().value("message").toString()));
    const auto row=visible.first().toMap();const QPoint point=row.value("rect").toRectF().center().toPoint();
    QTest::mouseClick(notes,Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(state().value("selectedNotes").toInt(),1);
    auto number=editor->findChild<QSpinBox*>("midiTrackVelocity");auto use=editor->findChild<QToolButton*>("midiTrackSetVelocity");QVERIFY(number&&use);number->setValue(112);QTest::mouseClick(use,Qt::LeftButton);document.reload();
    QString selectedId=row.value("id").toString();auto chosen=[&](){document.reload();for(auto n:document.track(id).value("notes").toList())if(n.toMap().value("id").toString()==selectedId)return n.toMap();return QVariantMap();};QCOMPARE(chosen().value("velocity").toInt(),112);
    QTest::keyClick(notes,Qt::Key_Z,Qt::ControlModifier);QTRY_COMPARE(chosen().value("velocity").toInt(),row.value("velocity").toInt());
    QTest::keyClick(notes,Qt::Key_Y,Qt::ControlModifier);QTRY_COMPARE(chosen().value("velocity").toInt(),112);
    const int initialPitch=chosen().value("pitch").toInt();QTest::keyClick(notes,Qt::Key_Up);QTRY_COMPARE(chosen().value("pitch").toInt(),initialPitch+1);QTest::keyClick(notes,Qt::Key_Z,Qt::ControlModifier);QTRY_COMPARE(chosen().value("pitch").toInt(),initialPitch);
    auto volume=editor->findChild<QSpinBox*>("midiTrackVolume");auto slider=editor->findChild<QSlider*>("midiTrackVolumeSlider");QVERIFY(volume&&slider);volume->setValue(70);QTest::keyClick(volume,Qt::Key_Return);document.reload();QCOMPARE(document.track(id).value("volume").toDouble(),.7);QCOMPARE(slider->value(),70);
    auto original=chosen();const int undo=score->undoStack()->getCurIdx();QPoint dragPoint;
    for(const auto& item:state().value("visibleNotes").toList())if(item.toMap().value("id").toString()==selectedId)dragPoint=item.toMap().value("rect").toRectF().center().toPoint();QVERIFY(!dragPoint.isNull());
    QTest::mousePress(notes,Qt::LeftButton,Qt::NoModifier,dragPoint);
    QMouseEvent move(QEvent::MouseMove,dragPoint+QPoint(100,0),notes->mapToGlobal(dragPoint+QPoint(100,0)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(notes,&move);
    QTest::mouseRelease(notes,Qt::LeftButton,Qt::NoModifier,dragPoint+QPoint(100,0));
    if(chosen().value("start").toDouble()<=original.value("start").toDouble()){qInfo()<<"MIDI drag"<<point<<dragPoint<<original<<chosen()<<state();main->grab().save(out+"/midi-tracks-drag-160.png");}
    QVERIFY(chosen().value("start").toDouble()>original.value("start").toDouble());QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
    QTest::keyClick(notes,Qt::Key_Z,Qt::ControlModifier);QTRY_COMPARE(chosen(),original);
    auto align=editor->findChild<QToolButton*>("midiTrackAlign");QVERIFY(align);QTest::mouseClick(align,Qt::LeftButton);QTRY_VERIFY(editor->findChild<QDialog*>("midiTrackAlignment"));
    auto dialog=editor->findChild<QDialog*>("midiTrackAlignment");auto offset=dialog->findChild<QDoubleSpinBox*>("midiTrackOffset");QVERIFY(offset);offset->setValue(.25);auto buttons=dialog->findChild<QDialogButtonBox*>();QVERIFY(buttons);QTest::mouseClick(buttons->button(QDialogButtonBox::Apply),Qt::LeftButton);document.reload();QCOMPARE(document.track(id).value("offsetMs").toDouble(),250.);dialog->close();
    auto remove=editor->findChild<QToolButton*>("midiTrackDelete");auto restore=editor->findChild<QToolButton*>("midiTrackRestore");QVERIFY(remove&&restore);QTest::mouseClick(remove,Qt::LeftButton);QTRY_COMPARE(state().value("tracks").toList().size(),0);QTest::mouseClick(restore,Qt::LeftButton);QTRY_COMPARE(state().value("tracks").toList().size(),1);
    auto add=editor->findChild<QToolButton*>("midiTrackAdd");QVERIFY(add);QTest::mouseClick(add,Qt::LeftButton);QTRY_COMPARE(state().value("tracks").toList().size(),2);
    const auto scratchId=state().value("activeId").toString();QSettings().setValue("workflow/recordingDirectory",out+"/录音160");
    auto record=editor->findChild<QToolButton*>("midiTrackRecord");QVERIFY(record);QTest::mouseClick(record,Qt::LeftButton);auto capture=pane->findChild<MidiCaptureSession*>();QVERIFY(capture);
    QVERIFY(!capture->begin({{"endMode","manual"},{"monitor",false}},false).contains("error"));
    QVERIFY(captureMidiInput(NPlayEvent(ME_NOTEON,0,72,87),midiInputTime()));QTest::qWait(180);QVERIFY(captureMidiInput(NPlayEvent(ME_NOTEOFF,0,72,0),midiInputTime()));capture->stop();
    QTRY_VERIFY_WITH_TIMEOUT(!capture->completed().value("path").toString().isEmpty(),4000);
    QTest::qWait(200);qInfo()<<"MIDI recording result"<<state();
    QTRY_COMPARE_WITH_TIMEOUT((document.reload(),document.track(scratchId).value("notes").toList().size()),1,4000);
    QCOMPARE(document.track(scratchId).value("notes").toList().first().toMap().value("velocity").toInt(),87);
    QTest::qWait(100);for(int i=0;i<editor->layout()->count();++i){auto item=editor->layout()->itemAt(i);qInfo()<<"MIDI layout"<<i<<item->isEmpty()<<item->geometry()<<(item->widget()?item->widget()->objectName():QString("layout"));}
    for(const auto& name:{"performanceControlScroll","performanceNotesArea","rollWriteBar","midiTrackControls","midiTrackListSplitter"})if(auto widget=editor->findChild<QWidget*>(name))qInfo()<<"MIDI widget"<<name<<widget->isVisible()<<widget->geometry();
    main->grab().save(out+"/midi-tracks-editor-160.png");
    auto close=editor->findChild<QToolButton*>("midiTrackClose");QVERIFY(close);QTest::mouseClick(close,Qt::LeftButton);QVERIFY(!state().value("enabled").toBool());
    editor->showMidiTracks(true);QVERIFY(state().value("enabled").toBool());QCOMPARE(state().value("tracks").toList().size(),2);
    auto adopt=editor->findChild<QToolButton*>("midiTrackAdopt");QVERIFY(adopt);QTest::mouseClick(adopt,Qt::LeftButton);QTRY_VERIFY(editor->findChild<QDialog*>("midiTrackAdoptDialog"));auto adoption=editor->findChild<QDialog*>("midiTrackAdoptDialog");adoption->findChild<QComboBox*>("midiTrackAdoptScope")->setCurrentIndex(1);QTest::mouseClick(adoption->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),Qt::LeftButton);
    auto writing=editor->findChild<RollWritingBar*>();QVERIFY(writing);QTRY_VERIFY_WITH_TIMEOUT(writing->state().value("ready").toBool(),5000);QVERIFY2(writing->state().value("status").toString().contains(QString::fromUtf8("草稿")),qPrintable(writing->state().value("status").toString()));
    auto apply=editor->findChild<QToolButton*>("rollWriteApply");QVERIFY(apply);QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),5000);for(auto parent=apply->parentWidget();parent;parent=parent->parentWidget())if(auto scroll=qobject_cast<QScrollArea*>(parent))scroll->ensureWidgetVisible(apply);QTest::qWait(60);qInfo()<<"MIDI adoption before"<<writing->state();main->grab().save(out+"/midi-track-draft-before-160.png");QSignalSpy applied(apply,&QToolButton::clicked);QTest::mouseClick(apply,Qt::LeftButton);QCOMPARE(applied.count(),1);QTRY_VERIFY_WITH_TIMEOUT(writing->state().value("ready").toBool(),5000);qInfo()<<"MIDI adoption after"<<writing->state();main->grab().save(out+"/midi-track-draft-after-160.png");
    bool found=false;for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int track=0;track<4;++track)if(auto element=segment->element(track))if(element->isChord())for(auto note:toChord(element)->notes()){if(track==0&&note->pitch()==72){found=true;QCOMPARE(note->veloType(),Note::ValueType::USER_VAL);QCOMPARE(note->veloOffset(),87);}}QVERIFY(found);
    main->cmd(getAction("undo"));rollWritingAction()->setChecked(false);
    editor->showMidiTracks(false);
}
void TestPluginHost::midiTracksAlignment160() {
    auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1240,850);main->show();main->showPerformanceEditor(true);
    MidiTrackDocument doc;doc.setScore(score);std::atomic_bool cancel{false};QVERIFY(doc.addPrepared(prepareMidiTracks(midiTrackFixture160(),"对齐",cancel)));const auto track=doc.tracks().first().toMap();const auto id=track.value("id").toString(),noteId=track.value("notes").toList().first().toMap().value("id").toString();
    auto project=referenceProject();QVERIFY(project);const auto saved=project->property("tracks").toList();const auto restore=qScopeGuard([&]{QVariantMap result;QMetaObject::invokeMethod(project,"setTracks",Q_RETURN_ARG(QVariantMap,result),Q_ARG(QVariantList,saved));});
    QVariantMap association{{"id","clip"},{"from",1000.},{"until",3000.},{"start",0},{"end",1920},{"occurrence",1}};
    auto publish=[&](){QVariantMap result;const QVariantList tracks{QVariantMap{{"id","audio"},{"name","录音"},{"associations",QVariantList{association}}}};QMetaObject::invokeMethod(project,"setTracks",Q_RETURN_ARG(QVariantMap,result),Q_ARG(QVariantList,tracks));return result.value("ok").toBool();};QVERIFY(publish());QVERIFY(doc.setAlignment(id,"audio",1000,{},"clip"));
    auto editor=main->performanceEditor();editor->showMidiTracks(true);auto tree=editor->findChild<QTreeWidget*>("midiTrackList");QTRY_COMPARE(tree->topLevelItemCount(),2);tree->scrollToItem(tree->topLevelItem(1));QTest::qWait(30);QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,tree->visualItemRect(tree->topLevelItem(1)).center());auto time=editor->findChild<QComboBox*>("midiTrackTimeView");QVERIFY(time);time->setCurrentIndex(1);auto pane=editor->findChild<QWidget*>("midiTrackPane");
    auto first=[&](){QVariantMap result;QMetaObject::invokeMethod(pane,"state",Q_RETURN_ARG(QVariantMap,result));for(const auto& value:result.value("visibleNotes").toList())if(value.toMap().value("id").toString()==noteId)return value.toMap();return QVariantMap();};
    QTRY_COMPARE(first().value("tick").toInt(),0);QTRY_COMPARE(first().value("until").toInt(),480);
    association["end"]=960;QVERIFY(publish());QTRY_COMPARE(first().value("until").toInt(),240);QCOMPARE(doc.track(id).value("notes").toList().first().toMap().value("end").toDouble(),.5);
    editor->showMidiTracks(false);
}
namespace {
class MidiClockProbe160 final:public Synthesizer {
    QList<MidiPatch*> patches;
public:
    struct Message {qint64 frame;PlayEvent event;};
    QVector<Message> messages;qint64 frame=0;
    const char* name()const override{return "MidiClockProbe160";}
    bool loadSoundFonts(const QStringList&)override{return true;}
    std::vector<SoundFontInfo> soundFontsInfo()const override{return {};}
    void process(unsigned count,float*,float*,float*)override{frame+=count;}
    void play(const PlayEvent& event)override{messages.append({frame,event});}
    const QList<MidiPatch*>&getPatchInfo()const override{return patches;}
    SynthesizerGroup state()const override{return {};}
    bool setState(const SynthesizerGroup&)override{return true;}
};
}
void TestPluginHost::midiTracksClock160() {
    std::atomic_bool canceled{false};const auto prepared=prepareMidiTracks(midiTrackFixture160(),"演奏",canceled);
    auto tracks=prepared.value("tracks").toList();auto track=tracks[0].toMap();track.insert("mute",false);tracks[0]=track;
    MidiTrackSound sound;sound.id=track.value("id").toString();sound.synthesizer=0;
    auto plan=buildMidiTrackPlayback(tracks,{sound},2,44100,0,false);QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
    MasterSynthesizer synth;auto probe=new MidiClockProbe160;synth.registerSynthesizer(probe);synth.setSampleRate(44100);
    const auto cleanup=qScopeGuard([&]{stopMidiTrackPlayback();float block[512]{};processMidiTrackPlayback(&synth,256,block,0,false);collectMidiTrackPlayback();});
    QVERIFY(startMidiTrackPlayback(std::move(plan))>0);
    for(int frame=0;frame<44100*3;frame+=257){float block[514]{};processMidiTrackPlayback(&synth,257,block,0,false);}
    QVector<qint64> onsets;for(const auto& message:probe->messages)if(message.event.type()==ME_NOTEON&&message.event.velo()>0){QVERIFY(message.event.channel()>=2);onsets.append(message.frame);}
    QCOMPARE(onsets,QVector<qint64>({0,22050,66150}));QVERIFY(!midiTrackPlaybackState().value("playing").toBool());
    stopMidiTrackPlayback();float block[2000]{};processMidiTrackPlayback(&synth,1000,block,0,false);collectMidiTrackPlayback();
    const int before=probe->messages.size();
    auto follow=buildMidiTrackPlayback(tracks,{sound},2,44100,0,true);QVERIFY(follow.error.isEmpty());startMidiTrackPlayback(std::move(follow));
    processMidiTrackPlayback(&synth,1000,block,0,false);QCOMPARE(probe->messages.size(),before);
    processMidiTrackPlayback(&synth,1000,block,0,true);QVERIFY(midiTrackPlaybackState().value("playing").toBool());
    processMidiTrackPlayback(&synth,1000,block,22050,true);
    processMidiTrackPlayback(&synth,1000,block,0,true);
    int firstNotes=0;for(int i=before;i<probe->messages.size();++i){const auto& e=probe->messages[i].event;if(e.type()==ME_NOTEON&&e.velo()>0&&e.pitch()==60)++firstNotes;}
    QVERIFY(firstNotes>=2);QVERIFY(midiTrackPlaybackState().value("seconds").toDouble()<.025);
    processMidiTrackPlayback(&synth,1000,block,44100*4,true);QVERIFY(!midiTrackPlaybackState().value("playing").toBool());QVERIFY(midiTrackPlaybackState().value("requested").toBool());
    const int atEnd=probe->messages.size();processMidiTrackPlayback(&synth,1000,block,0,true);QVERIFY(midiTrackPlaybackState().value("playing").toBool());bool restarted=false;for(int i=atEnd;i<probe->messages.size();++i)restarted|=probe->messages[i].event.type()==ME_NOTEON&&probe->messages[i].event.velo()>0;QVERIFY(restarted);
    processMidiTrackPlayback(&synth,1000,block,1000,false);QVERIFY(!midiTrackPlaybackState().value("playing").toBool());
    const auto monitor=startMidiTrackMonitor(sound,6,44100);QVERIFY(monitor);QVERIFY(sendMidiTrackMonitor(monitor,NPlayEvent(ME_NOTEON,0,72,91)));processMidiTrackPlayback(&synth,1000,block,0,false);QVERIFY(midiTrackPlaybackState().value("playing").toBool());bool monitored=false;for(const auto& value:probe->messages)monitored|=value.event.channel()==6&&value.event.type()==ME_NOTEON&&value.event.pitch()==72&&value.event.velo()==91;QVERIFY(monitored);
    QVERIFY(sendMidiTrackMonitor(monitor,NPlayEvent(ME_NOTEON,0,72,0)));processMidiTrackPlayback(&synth,1000,block,0,false);stopMidiTrackPlayback();processMidiTrackPlayback(&synth,1000,block,0,false);QVERIFY(!sendMidiTrackMonitor(monitor,NPlayEvent(ME_NOTEON,0,72,91)));
    qInfo()<<"160 external MIDI onsets"<<onsets<<"followed seeks and native channels preserved";
}
namespace {
class MidiTrackDriver160 final:public Driver {
    std::atomic<Transport> transport{Transport::STOP};std::atomic_bool quit{false};std::thread worker;
public:
    std::atomic<double> energy{0};
    std::atomic<int> starts{0};
    explicit MidiTrackDriver160(Seq* sequence):Driver(sequence){}
    ~MidiTrackDriver160()override{stop();}
    bool init(bool=false)override{return true;}
    bool start(bool=false)override{worker=std::thread([this]{float buffer[1024]{};while(!quit){seq->process(512,buffer);double e=0;for(auto sample:buffer)e+=sample*sample;energy.store(energy.load()+e);std::this_thread::sleep_for(std::chrono::milliseconds(11));}});return true;}
    bool stop()override{quit=true;if(worker.joinable())worker.join();return true;}
    void startTransport()override{++starts;transport=Transport::PLAY;}
    void stopTransport()override{transport=Transport::STOP;}
    Transport getState()override{return transport.load();}
    int sampleRate()const override{return 44100;}
};
}
void TestPluginHost::midiTracksPlayback160() {
    auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    std::atomic_bool canceled{false};MidiTrackDocument doc;doc.setScore(score);auto prepared=prepareMidiTracks(midiTrackFixture160(),"钢琴",canceled);QVERIFY(doc.addPrepared(prepared));const auto id=doc.tracks().first().toMap().value("id").toString();QVERIFY(doc.update(id,{{"mute",false}}));
    auto synth=std::unique_ptr<MasterSynthesizer>(synthesizerFactory());auto fluid=dynamic_cast<FluidS::Fluid*>(synth->synthesizer("Fluid"));QVERIFY(fluid);QVERIFY(fluid->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));synth->setSampleRate(44100);
    auto sequence=new Seq;Ms::seq=sequence;auto driver=new MidiTrackDriver160(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    const auto cleanup=qScopeGuard([&]{main->performanceEditor()->showMidiTracks(false);stopPlaybackPreview(true);stopMidiTrackPlayback();sequence->stopWait();sequence->waitForStoppedRendering();sequence->setScoreView(nullptr);delete sequence;Ms::seq=nullptr;collectMidiTrackPlayback();});
    main->resize(1240,850);main->show();main->showPerformanceEditor(true);auto editor=main->performanceEditor();editor->showMidiTracks(true);QTest::qWait(60);auto tree=editor->findChild<QTreeWidget*>("midiTrackList");QVERIFY(tree);QTRY_COMPARE(tree->topLevelItemCount(),2);QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,tree->visualItemRect(tree->topLevelItem(1)).center());
    auto play=editor->findChild<QToolButton*>("midiTrackPlay");
    if(!play||!play->isVisible()){
        qInfo()<<"MIDI playback tree"<<tree->isVisible()<<tree->geometry()<<tree->visualItemRect(tree->topLevelItem(1))<<tree->currentItem()->text(0);
        if(play)for(auto parent=play->parentWidget();parent;parent=parent->parentWidget())qInfo()<<"play ancestor"<<parent->objectName()<<parent->isVisible()<<parent->geometry();
        main->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/midi-playback-open-160.png");
    }
    QVERIFY(play&&play->isVisible());QTest::mouseClick(play,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(midiTrackPlaybackState().value("playing").toBool(),1000);QTRY_VERIFY_WITH_TIMEOUT(driver->energy.load()>.001,1500);
    QTest::keyClick(tree,Qt::Key_Space);QTRY_VERIFY(!midiTrackPlaybackState().value("requested").toBool());QTest::qWait(60);
    QTest::mouseClick(play,Qt::LeftButton);QTRY_VERIFY(midiTrackPlaybackState().value("playing").toBool());QTRY_VERIFY_WITH_TIMEOUT(!midiTrackPlaybackState().value("requested").toBool(),4000);
    QTest::qWait(60);const double energy=driver->energy.load();QTest::mouseClick(play,Qt::LeftButton);QTRY_VERIFY(midiTrackPlaybackState().value("playing").toBool());QTRY_VERIFY(driver->energy.load()>energy+.001);
    QTest::mouseClick(play,Qt::LeftButton);QTRY_VERIFY(!midiTrackPlaybackState().value("requested").toBool());
    auto withScore=editor->findChild<QCheckBox*>("midiTrackWithScore");QVERIFY(withScore);QTest::mouseClick(withScore,Qt::LeftButton);QVERIFY(withScore->isChecked());QSignalSpy clicked(play,&QToolButton::clicked);QTest::mouseClick(play,Qt::LeftButton);qInfo()<<"MIDI joint click"<<clicked.count()<<play->isEnabled()<<play->geometry()<<editor->findChild<QLabel*>("midiTrackMessage")->text()<<sequence->isPlaying()<<midiTrackPlaybackState();QCOMPARE(clicked.count(),1);
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),3000);QTRY_VERIFY_WITH_TIMEOUT(midiTrackPlaybackState().value("playing").toBool(),1000);QTest::qWait(500);
    auto canvas=editor->findChild<QWidget*>("performanceNoteCanvas");QVERIFY(canvas);QTest::keyClick(canvas,Qt::Key_Home);QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&sequence->getCurTick()<240,1500);QTRY_VERIFY(midiTrackPlaybackState().value("playing").toBool());
    setPlaybackPreviewLoop(score,true);const int starts=driver->starts.load();QTRY_VERIFY_WITH_TIMEOUT(driver->starts.load()>starts,4500);QTRY_VERIFY(sequence->isPlaying());QTRY_VERIFY(midiTrackPlaybackState().value("playing").toBool());
    QTest::mouseClick(play,Qt::LeftButton);QTRY_VERIFY(!sequence->isPlaying());QTRY_VERIFY(!midiTrackPlaybackState().value("requested").toBool());
    qInfo()<<"160 standalone UI playback through native Seq/Fluid energy"<<driver->energy.load();
}
void TestPluginHost::midiTracksUndoState160() {
    auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    MidiTrackDocument doc;doc.setScore(score);const auto initial=score->state();const auto id=doc.create("临时");QVERIFY(!id.isEmpty());QVERIFY(score->state()==initial);
    Note* note=nullptr;for(auto segment=score->firstSegment(SegmentType::ChordRest);segment&&!note;segment=segment->next1(SegmentType::ChordRest))if(auto element=segment->element(0))if(element->isChord())note=toChord(element)->notes().front();QVERIFY(note);const int pitch=note->pitch();
    score->startCmd();note->undoChangeProperty(Pid::PITCH,pitch+1);score->endCmd();const auto changed=score->state();QVERIFY(changed!=initial);
    QVERIFY(doc.update(id,{{"name","更名"}}));QVERIFY(score->state()==changed);main->cmd(getAction("undo"));QVERIFY(score->state()==changed);main->cmd(getAction("undo"));QVERIFY(score->state()==initial);
    main->cmd(getAction("redo"));QVERIFY(score->state()==changed);QVERIFY(doc.update(id,{{"mute",true}}));QVERIFY(score->state()==changed);QVERIFY(!score->undoStack()->canRedo());
    const int first=score->undoStack()->getCurIdx();score->startCmd();note->undoChangeProperty(Pid::PITCH,pitch+2);score->endCmd();QVERIFY(doc.update(id,{{"name","合并"}}));score->undoStack()->mergeCommands(first);const auto merged=score->state();QVERIFY(merged!=changed);main->cmd(getAction("undo"));QVERIFY(score->state()==changed);QCOMPARE(note->pitch(),pitch+1);
    score->undoStack()->clearAfterLoad();const auto reset=score->state();QVERIFY(score->undoStack()->isClean());score->startCmd();note->undoChangeProperty(Pid::PITCH,pitch+3);score->endCmd();QVERIFY(score->state()!=reset);QVERIFY(!score->undoStack()->isClean());
}
void TestPluginHost::midiTracksIndependentSounds160_data(){QTest::addColumn<QString>("kind");QTest::newRow("SF2-rack")<<QString("SF2");QTest::newRow("Pianoteq")<<QString("Pianoteq");QTest::newRow("CFX")<<QString("CFX");}
void TestPluginHost::midiTracksIndependentSounds160() {
#ifdef KUMO_VST_HOST
    QFETCH(QString,kind);auto main=Ms::mscore;auto score=std::unique_ptr<MasterScore>(main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(score);
    MasterSynthesizer synth;auto vst=new VstSynth(true,nullptr);synth.registerSynthesizer(vst);synth.setSampleRate(44100);
    VstHost::Descriptor descriptor;QString error;
    if(kind=="SF2"){descriptor.format="SF2";descriptor.path=qEnvironmentVariable("KUMO_KAWAI_SF2");descriptor.uid="0:0";descriptor.name="Kawai";descriptor.instrument=true;}
    else {const auto found=kind=="CFX"?VstHost::scanVst2("E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll",&error):VstHost::scanVst3("C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3",&error);QVERIFY2(!found.isEmpty(),qPrintable(error));descriptor=VstHost::Descriptor::fromValue(found.first().toMap());}
    const QVariantMap native{{"id",1},{"label",kind},{"source",QVariantMap{{"plugin",descriptor.value()}}},{"originalPatch",QVariantMap{{"bank",0},{"program",0}}}};
    QVERIFY(vst->setConfiguration({{"slots",QVariantList{native}}}));QVERIFY2(vst->problems().isEmpty(),qPrintable(vst->problems().join(";")));auto processor=vst->rack().plugin(1);QVERIFY(processor);
    if(kind=="CFX")QVERIFY2(processor->importPreset("E:/music_fonts/CFX Concert Grand/Presets/com.Plogue.Aria/02- Compact/Player/00_Default.ariax",&error),qPrintable(error));
    auto channel=score->playbackChannel(score->staff(0)->part()->instrument()->channel(0));channel->setSynti("KumoVST");channel->setBank(1);channel->setProgram(0);
    std::atomic_bool canceled{false};auto tracks=prepareMidiTracks(midiTrackFixture160(),kind,canceled).value("tracks").toList();auto track=tracks.first().toMap();track["mute"]=false;track["source"]=QVariantMap{{"kind","score-piano"},{"staff",0}};tracks[0]=track;
    const auto before=score->state();QObject owner;const auto prepared=prepareMidiTrackSounds(&synth,score.get(),tracks,&owner);QVERIFY2(prepared.error.isEmpty(),qPrintable(prepared.error));QCOMPARE(prepared.sounds.size(),1);QVERIFY(prepared.sounds.first().bank!=1);
    QCOMPARE(vst->rack().instruments().size(),1);QCOMPARE(vst->rack().instruments(true).size(),2);QCOMPARE(vst->configuration(false).value("slots").toList().size(),1);QCOMPARE(vst->getPatchInfo().size(),1);QVERIFY(score->state()==before);QCOMPARE(vst->rack().plugin(1),processor);
    synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_HBANK,0),0);synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_LBANK,1),0);synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_VOLUME,100),0);synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_SUSTAIN,127),0);synth.play(NPlayEvent(ME_NOTEON,0,60,100),0);
    for(int i=0;i<50;++i){float b[512]{};synth.process(256,b);}synth.play(NPlayEvent(ME_NOTEON,0,60,0),0);
    auto plan=buildMidiTrackPlayback(tracks,prepared.sounds,4,44100,0,false);QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));startMidiTrackPlayback(std::move(plan));for(int i=0;i<90;++i){float b[512]{};processMidiTrackPlayback(&synth,256,b,0,false);}stopMidiTrackPlayback();float release[512]{};processMidiTrackPlayback(&synth,256,release,0,false);collectMidiTrackPlayback();
    clearMidiTrackSounds(&synth,&owner);QCOMPARE(vst->rack().instruments(true).size(),1);QCOMPARE(vst->rack().plugin(1),processor);
    double energy=0;for(int i=0;i<300;++i){float b[512]{};synth.process(256,b);if(i>160)for(float v:b)energy+=v*v;}QVERIFY2(energy>1.e-7,qPrintable(kind+" native held pedal sound lost"));
    qInfo()<<"160 independent"<<kind<<"native pedal tail energy"<<energy;synth.allSoundsOff(-1);
#else
    QSKIP("This build has no VST host");
#endif
}
void TestPluginHost::midiTracksPersistence160() {
    std::atomic_bool cancel{false};const auto bytes=midiTrackFixture160();
    const auto parsed=readPerformanceMidiData(bytes,cancel);QVERIFY2(!parsed.contains("error"),qPrintable(parsed.value("error").toString()));
    const auto original=parsed.value("notes").toList();QCOMPARE(original.size(),3);
    QCOMPARE(original[1].toMap().value("seconds").toDouble(),.5);
    QCOMPARE(original[2].toMap().value("seconds").toDouble(),1.5);
    auto prepared=prepareMidiTracks(bytes,"完整演奏.mid",cancel);QVERIFY(!prepared.contains("error"));
    auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));MidiTrackDocument document;document.setScore(score);
    score->undoStack()->setClean();const auto musicState=score->state();
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);const auto revision=observer.analysisRevision();
    workflowContext()->setExpression(score,{{"owner","midi-state-160"},{"revision",revision}});
    const int before=score->undoStack()->getCurIdx();QVERIFY(document.addPrepared(prepared));
    QVERIFY(score->state()==musicState);QVERIFY(!score->undoStack()->isClean());
    QCOMPARE(observer.analysisRevision(),revision);QVERIFY(workflowContext()->current(score));
    QCOMPARE(score->undoStack()->getCurIdx(),before+1);QCOMPARE(document.tracks().size(),1);
    const auto id=document.tracks().first().toMap().value("id").toString();QVERIFY(document.track(id).value("mute").toBool());
    QVERIFY(document.update(id,{{"mute",false},{"volume",.7},{"name","钢琴参考"}}));
    QVERIFY(document.setAlignment(id,"reference-audio-1",25,{
        QVariantMap{{"sourceMs",0.},{"targetMs",1000.}},QVariantMap{{"sourceMs",1000.},{"targetMs",2200.}}}));
    const auto track=document.track(id);QVERIFY(qAbs(midiTrackTime(track,.5)-1.625)<1.e-9);
    QVERIFY(qAbs(midiTrackTime(track,1.625,true)-.5)<1.e-9);
    auto notes=track.value("notes").toList();auto first=notes[0].toMap();first.insert("velocity",103);first.insert("start",.125);first.insert("end",.375);notes[0]=first;
    QVERIFY(document.replaceNotes(id,notes));
    const auto copy=document.duplicate(id);QVERIFY(!copy.isEmpty());QCOMPARE(document.tracks().size(),2);
    QCOMPARE(document.state().value("assets").toMap().size(),1);
    QVERIFY(document.remove(id));QVERIFY(document.remove(copy));QVERIFY(document.tracks().isEmpty());
    QVERIFY(document.restore(id));QCOMPARE(document.tracks().size(),1);
    main->cmd(getAction("undo"));document.reload();QVERIFY(document.tracks().isEmpty());
    main->cmd(getAction("redo"));document.reload();QCOMPARE(document.tracks().size(),1);
    const int stable=score->undoStack()->getCurIdx();
    QVERIFY(!document.setAlignment(id,"audio",0,{QVariantMap{{"sourceMs",0.},{"targetMs",50.}},QVariantMap{{"sourceMs",100.},{"targetMs",25.}}}));
    auto bad=notes;auto n=bad[0].toMap();n.insert("end",-.1);bad[0]=n;QVERIFY(!document.replaceNotes(id,bad));QCOMPARE(score->undoStack()->getCurIdx(),stable);
    const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QFileInfo saved(output.filePath("midi-tracks-160.mscz"));QVERIFY(score->saveCompressedFile(saved,false,false));
    std::unique_ptr<MasterScore> reopened(main->readScore(saved.filePath()));QVERIFY(reopened);
    MidiTrackDocument loaded;loaded.setScore(reopened.get());QCOMPARE(loaded.state(),document.state());
    const auto asset=loaded.state().value("assets").toMap().first().toMap();
    QCOMPARE(QByteArray::fromBase64(asset.value("data").toString().toLatin1()),bytes);
    const auto result=exportMidiTrack(loaded.state(),id,output.filePath("edited-track-160.mid"));QVERIFY2(result.value("ok").toBool(),qPrintable(result.value("error").toString()));
    const auto readBack=readPerformanceMidi(output.filePath("edited-track-160.mid"),cancel);QVERIFY(!readBack.contains("error"));
    const auto exported=readBack.value("notes").toList();QCOMPARE(exported.size(),3);
    QCOMPARE(exported.first().toMap().value("velocity").toInt(),103);QCOMPARE(exported.first().toMap().value("seconds").toDouble(),.125);
    QCOMPARE(exported.first().toMap().value("endSeconds").toDouble(),.375);
    for(const auto& value:exported)QVERIFY(!value.toMap().value("unreleased").toBool());
    bool reverb=false;for(const auto& value:readBack.value("events").toList()) {
        const auto e=value.toMap();reverb|=e.value("type").toInt()==176&&e.value("a").toInt()==91&&e.value("b").toInt()==40;
    }
    QVERIFY(reverb); // A controller on the separate conductor track still applies.
    const auto aligned=exportMidiTrack(loaded.state(),id,output.filePath("aligned-track-160.mid"),true);QVERIFY(aligned.value("ok").toBool());
    const auto alignedRead=readPerformanceMidi(output.filePath("aligned-track-160.mid"),cancel);
    QVERIFY(qAbs(alignedRead.value("notes").toList().first().toMap().value("seconds").toDouble()-1.175)<.00001);
    QVERIFY(loaded.restore(copy));QCOMPARE(loaded.tracks().size(),2);
    const auto scratch=loaded.create("临时写作");QVERIFY(!scratch.isEmpty());QVERIFY(loaded.remove(scratch));QVERIFY(loaded.restore(scratch));
    QVERIFY(loaded.appendRecording(scratch,prepared,2.));QCOMPARE(loaded.track(scratch).value("notes").toList().size(),3);
    QCOMPARE(loaded.track(scratch).value("notes").toList().first().toMap().value("start").toDouble(),2.);
    QVERIFY(loaded.setScoreOptions({{"visible",false},{"mute",true}}));
    QString encodedError;QCOMPARE(decodeMidiTracks(encodeMidiTracks(loaded.state(),&encodedError)),loaded.state());
    QVERIFY(score->state()==musicState);QVERIFY(workflowContext()->current(score));
    cancel=true;QVERIFY(prepareMidiTracks(bytes,"取消.mid",cancel).contains("error"));
    workflowContext()->setExpression(nullptr,{});
}

void TestPluginHost::midiTracksLongData160() {
    QByteArray body=QByteArray::fromHex("00ff510307a120");
    for(int i=0;i<5000;++i){body.append(char(i?15:0));body.append('\x90');body.append(char(48+i%36));body.append(char(64+i%40));body.append(char(15));body.append('\x80');body.append(char(48+i%36));body.append(char(0));}
    body.append("\0\xff\x2f\0",4);
    QByteArray bytes=QByteArray::fromHex("4d546864000000060000000101e04d54726b");for(int i=3;i>=0;--i)bytes.append(char(body.size()>>(8*i)));bytes+=body;
    std::atomic_bool canceled{false};const auto prepared=prepareMidiTracks(bytes,"完整5000音.mid",canceled);QVERIFY(!prepared.contains("error"));
    auto track=prepared.value("tracks").toList().first().toMap();auto notes=track.value("notes").toList();QCOMPARE(notes.size(),5000);
    auto first=notes.first().toMap();first.insert("velocity",103);first.insert("start",.125);first.insert("end",.25);notes[0]=first;
    notes.removeAt(10);auto added=first;added.insert("id","temporary-note");added.insert("pitch",72);notes.insert(1,added);
    track.insert("notes",notes);track.insert("mute",false);
    const QVariantMap document{{"schema",1},{"tracks",QVariantList{track}},{"deleted",QVariantList()},
        {"assets",QVariantMap{{prepared.value("assetId").toString(),prepared.value("asset")}}}};
    QElapsedTimer watch;watch.start();QString message;const auto text=encodeMidiTracks(document,&message);const auto elapsed=watch.nsecsElapsed()/1.e6;
    QVERIFY2(!text.isEmpty(),qPrintable(message));const auto saved=QJsonDocument::fromJson(qUncompress(QByteArray::fromBase64(text.toLatin1()))).toVariant().toMap();
    const auto compactTrack=saved.value("tracks").toList().first().toMap();QVERIFY(!compactTrack.contains("notes"));
    QCOMPARE(compactTrack.value("overrides").toMap().size(),1);QCOMPARE(compactTrack.value("removed").toList().size(),1);QCOMPARE(compactTrack.value("added").toList().size(),1);
    QCOMPARE(decodeMidiTracks(text),document);
    const auto legacy=QString::fromLatin1(qCompress(QJsonDocument::fromVariant(document).toJson(QJsonDocument::Compact)).toBase64());QCOMPARE(decodeMidiTracks(legacy),document);
    auto corrupt=QByteArray::fromBase64(text.toLatin1());corrupt[0]=0;corrupt[1]=0;corrupt[2]=0;corrupt[3]=64;
    QVERIFY(decodeMidiTracks(QString::fromLatin1(corrupt.toBase64())).contains("error"));
    MidiTrackSound sound;sound.id=track.value("id").toString();auto plan=buildMidiTrackPlayback({track},{sound},2,44100,60,true);
    QVERIFY(plan.error.isEmpty());QVERIFY(plan.checkpoints.size()>10);
    qInfo()<<"160 5000-note save: milliseconds"<<elapsed<<"encoded bytes"<<text.size()<<"checkpoints"<<plan.checkpoints.size();
}
