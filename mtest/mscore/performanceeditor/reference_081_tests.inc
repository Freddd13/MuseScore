// GPL-2.0-or-later. Capture decoded/mixed PCM through the real sequencer callback.
void referenceEightTracksAndMidPhrase()
{
    expressionHost080(_directory.path());
    auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    auto score=expressionScore080(main,_directory.filePath("reference-midphrase-081.mscx"));QVERIFY(score);
    expressionShowMain080(main);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;
    synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* value){
        workflowContext()->finishPlaybackSession();value->stopWait();value->waitForStoppedRendering();value->setScoreView(nullptr);delete value;Ms::seq=nullptr;
    });
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);
    QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);observer.workflowInfo();
    const int begin=2400,end=6240;
    const double frequencies[]{220.,277.182631,329.627557,391.995436,466.163762,554.365262,659.255114,783.990872};
    QVariantList tracks;
    for(int track=0;track<8;++track){
        const QString path=_directory.filePath(QString("reference-lane-081-%1.wav").arg(track));
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
        const int rate=22050,frames=5*rate;QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);
        data.writeRawData("RIFF",4);data<<quint32(36+frames*2);data.writeRawData("WAVEfmt ",8);
        data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(rate)<<quint32(2*rate)<<quint16(2)<<quint16(16);
        data.writeRawData("data",4);data<<quint32(frames*2);
        for(int i=0;i<frames;++i)data<<qint16(1200*std::sin(2*3.14159265358979323846*frequencies[track]*i/rate));
        file.close();
        const QVariantMap association{{"id",QString("link-%1").arg(track)},{"start",begin},{"end",end},
            {"from",500.},{"until",4500.},{"active",true},
            {"anchors",QVariantList{QVariantMap{{"tick",4320},{"ms",3000.}}}}};
        tracks.append(QVariantMap{{"id",QString("track-%1").arg(track)},{"name",QString("Reference %1").arg(track+1)},
            {"path",path},{"volume",1.},{"participate",true},{"associations",QVariantList{association}}});
    }
    QVERIFY(observer.setReferenceTracks(tracks).value("ok").toBool());
    auto project=referenceProject();QVERIFY(project->property("available").toBool());
    QVariantMap request{{"revision",observer.analysisRevision()},{"start",begin},{"end",end},{"tracks",tracks},
        {"volume",1.},{"fade",false},{"leader",tracks.front().toMap().value("associations").toList().front()}};
    const auto originalState=score->state();
    for(int mode=0;mode<3;++mode)for(int round=0;round<2;++round){
        request["mode"]=mode;auto plan=observer.referencePlan(request);
        QVERIFY2(!plan.contains("error"),qPrintable(plan.value("error").toString()));
        recording->onPitches.clear();recording->onFrames.clear();QVector<float> pcm;
        auto result=observer.startReferencePlayback(plan);QVERIFY2(result.value("ok").toBool(),qPrintable(result.value("error").toString()));
        QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),15000);
        QCOMPARE(project->property("diagnostics").toMap().value("audioTracks").toInt(),8);
        const int firstTick=sequence->getCurTick();
        for(int period=0;period<400 && (sequence->isPlaying() || project->property("playing").toBool());++period){
            driver->pulse();for(int i=0;i<2048;++i)pcm.append(driver->output()[i*2]);QTest::qWait(4);
        }
        QTRY_VERIFY(!sequence->isPlaying());QTRY_VERIFY(!project->property("playing").toBool());
        int firstAudio=0;while(firstAudio<pcm.size() && std::abs(pcm[firstAudio])<.0001f)++firstAudio;
        qInfo("mode %d round %d: first tick %d, first note %d, first audio %.3f seconds, frames %d",mode,round,firstTick,
            recording->onPitches.isEmpty()?-1:recording->onPitches.front(),firstAudio/44100.,pcm.size());
        QVERIFY(!recording->onPitches.isEmpty());QVERIFY(firstAudio<pcm.size());
        QVERIFY2(firstTick>=begin,"从音符拍位开始关联播放时，光标直接停在所选起点");
        QCOMPARE(recording->onPitches.front(),69); // A4 is the selected onset, rather than C4 in the preceding bar.
        QVERIFY2(firstAudio<4410,"原曲与所选起点一起开始，首音不等待前一小节");
        QVERIFY(recording->onPitches.contains(74));QVERIFY(score->state()==originalState);
        const int offset=firstAudio+11025,count=qMin(22050,pcm.size()-offset);QVERIFY(count>=22050);
        for(double frequency:frequencies){
            double real=0.,imag=0.;
            for(int i=0;i<count;++i){const double angle=2*3.14159265358979323846*frequency*i/44100.;real+=pcm[offset+i]*std::cos(angle);imag+=pcm[offset+i]*std::sin(angle);}
            const double amplitude=2*std::sqrt(real*real+imag*imag)/count;
            qInfo("track frequency %.2f Hz, mixed amplitude %.5f",frequency,amplitude);
            QVERIFY2(amplitude>.012,"每条音轨都应在实际混音数据中可辨认");
        }
    }
    auto shortTrack=tracks.front().toMap();auto shortLink=shortTrack.value("associations").toList().front().toMap();
    shortLink["end"]=begin+480;shortLink["from"]=500.;shortLink["until"]=2000.;shortLink["anchors"]=QVariantList{};shortTrack["associations"]=QVariantList{shortLink};
    auto impossible=request;impossible["mode"]=1;impossible["end"]=begin+480;impossible["tracks"]=QVariantList{shortTrack};
    const auto explained=observer.referencePlan(impossible);QVERIFY(explained.contains("error"));QVERIFY(explained.value("error").toString().contains("300.0%"));QCOMPARE(explained.value("requiredSpeed").toDouble(),3.);
    request["mode"]=1;auto loopPlan=observer.referencePlan(request);loopPlan["loop"]=true;
    QSignalSpy restarts(sequence,&Seq::started);
    QVERIFY(observer.startReferencePlayback(loopPlan).value("ok").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),15000);
    for(int period=0;period<600 && restarts.count()<2;++period){driver->pulse();QTest::qWait(4);}
    QVERIFY2(restarts.count()>=2,"播放到选段结束后，联合循环应从同一段重新开始");
    observer.stopReferencePlayback();QTRY_VERIFY(!sequence->isPlaying());QTRY_VERIFY(!project->property("playing").toBool());
    auto quick=observer.referencePlan(request);QVERIFY(observer.startReferencePlayback(quick).value("ok").toBool());
    observer.stopReferencePlayback();QTest::qWait(100);QVERIFY(!sequence->isPlaying());QVERIFY(!project->property("playing").toBool());
}

void referenceRestoreEventSemantics()
{
    EventMap events;
    events.insert({0,NPlayEvent(ME_CONTROLLER,0,64,100)});
    events.insert({0,NPlayEvent(ME_NOTEON,0,60,80)});
    events.insert({20,NPlayEvent(ME_NOTEON,0,64,90)});
    events.insert({100,NPlayEvent(ME_NOTEON,0,60,0)});
    auto notes=[](const EventMap& values){QList<int> result;for(const auto& pair:values)if(pair.second.type()==ME_NOTEON&&pair.second.velo())result.append(pair.second.pitch());std::sort(result.begin(),result.end());return result;};
    auto restored=restoredPlaybackEvents(events,200);
    QCOMPARE(notes(restored),QList<int>({60,64}));QCOMPARE(restored.size(),size_t(4));
    QCOMPARE(restored.begin()->second.type(),ME_CONTROLLER);QCOMPARE(restored.begin()->second.value(),100);
    auto lifted=events;lifted.insert({200,NPlayEvent(ME_CONTROLLER,0,64,0)});
    QCOMPARE(notes(restoredPlaybackEvents(lifted,200)),QList<int>({64}));
    auto reset=events;reset.insert({150,NPlayEvent(ME_CONTROLLER,0,121,0)});
    QCOMPARE(notes(restoredPlaybackEvents(reset,200)),QList<int>({64}));
    auto released=events;released.insert({150,NPlayEvent(ME_CONTROLLER,0,123,0)});
    QCOMPARE(notes(restoredPlaybackEvents(released,200)),QList<int>({60,64}));
    auto silenced=events;silenced.insert({150,NPlayEvent(ME_CONTROLLER,0,120,0)});
    QVERIFY(notes(restoredPlaybackEvents(silenced,200)).isEmpty());
    auto repeated=events;repeated.insert({200,NPlayEvent(ME_NOTEON,0,60,105)});
    QCOMPARE(notes(restoredPlaybackEvents(repeated,200)),QList<int>({64}));
    QCOMPARE(events.size(),size_t(4)); // Reading the start state preserves the source MIDI events.
}

void referenceHeldNotesAtRangeStart()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    const QString path=_directory.filePath("reference-held-081.mscx");
    QString xml="<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><Staff id=\"2\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part>";
    for(int staff=1;staff<=2;++staff){xml+=QString("<Staff id=\"%1\">").arg(staff);for(int bar=0;bar<4;++bar){
        xml+="<Measure><voice>";if(!bar)xml+="<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
        if(staff==1)xml+="<Chord><durationType>whole</durationType><Note><pitch>60</pitch><tpc>14</tpc></Note></Chord>";
        else for(int i=0;i<4;++i){const int pitch=36+i*2;xml+=QString("<Chord><durationType>quarter</durationType><Note><pitch>%1</pitch><tpc>%2</tpc></Note></Chord>").arg(pitch).arg(i==0?14:i==1?16:i==2?18:20);}
        xml+="</voice></Measure>";
    }xml+="</Staff>";}xml+="</Score></museScore>";
    QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml.toUtf8());file.close();
    auto source=main->readScore(path);QVERIFY(source);main->setCurrentScoreView(main->appendScore(source));expressionShowMain080(main);
    source->startCmd();
    for(int bar=0;bar<3;++bar){
        auto a=toChord(source->findCR(Fraction::fromTicks(bar*1920),0))->upNote();
        auto b=toChord(source->findCR(Fraction::fromTicks((bar+1)*1920),0))->upNote();
        auto tie=new Tie(source);tie->setStartNote(a);tie->setEndNote(b);tie->setTrack(0);tie->setTick(a->tick());tie->setTick2(b->tick());tie->setParent(a);source->undoAddElement(tie);
    }
    source->endCmd();source->doLayout();
    const QString audio=_directory.filePath("reference-held-081.wav");QFile wave(audio);QVERIFY(wave.open(QIODevice::WriteOnly));
    QDataStream data(&wave);data.setByteOrder(QDataStream::LittleEndian);const int frames=22050*2;
    data.writeRawData("RIFF",4);data<<quint32(36+frames*2);data.writeRawData("WAVEfmt ",8);data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(22050)<<quint32(44100)<<quint16(2)<<quint16(16);data.writeRawData("data",4);data<<quint32(frames*2);wave.write(QByteArray(frames*2,0));wave.close();
    class CapturedSynth final:public PerformanceRecordingSynth {
    public:QList<PlayEvent> played;void play(const PlayEvent& event)override{PerformanceRecordingSynth::play(event);played.append(event);}
    };
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new CapturedSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* value){workflowContext()->finishPlaybackSession();stopPlaybackPreview(true);value->stopWait();value->waitForStoppedRendering();value->setScoreView(nullptr);delete value;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    PluginAPI::Score wrapped(source);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);observer.workflowInfo();
    const int begin=6240,end=7200;
    const QVariantMap association{{"start",begin},{"end",end},{"from",0.},{"until",1000.},{"active",true}};
    const QVariantList tracks{QVariantMap{{"id","held"},{"name","Reference"},{"path",audio},{"associations",QVariantList{association}}}};
    QVERIFY(observer.setReferenceTracks(tracks).value("ok").toBool());
    auto run=[&](){recording->onPitches.clear();recording->played.clear();QVariantMap request{{"revision",observer.analysisRevision()},{"start",begin},{"end",end},{"tracks",tracks},{"mode",1},{"volume",0.}};auto plan=observer.referencePlan(request);if(plan.contains("error"))return plan;plan["foundContext"]=plan.value("contextUtick");auto result=observer.startReferencePlayback(plan);result["foundContext"]=plan.value("contextUtick");return result;};
    auto started=run();QVERIFY2(started.value("ok").toBool(),qPrintable(started.value("error").toString()));QTRY_VERIFY(sequence->isPlaying());
    for(int i=0;i<8;++i){driver->pulse();QTest::qWait(4);}
    qInfo("held tie start context %d, selected tick %d",started.value("foundContext").toInt(),sequence->getCurTick());
    QVERIFY(recording->onPitches.contains(60));QVERIFY(recording->onPitches.contains(38));QVERIFY(!recording->onPitches.contains(36));
    QCOMPARE(started.value("foundContext").toInt(),0);observer.stopReferencePlayback();QTRY_VERIFY(!sequence->isPlaying());
    // The next ordinary playback has one C4 attack across the full tie chain.
    recording->onPitches.clear();sequence->startAtUTick(0);QTRY_VERIFY(sequence->isPlaying());
    for(int i=0;i<260&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(2);}QTRY_VERIFY(!sequence->isPlaying());QCOMPARE(recording->onPitches.count(60),1);
    auto pedal=new Pedal(source);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction());pedal->setTicks(Fraction::fromTicks(7680));
    source->startCmd();source->undoAddElement(pedal);source->endCmd();sequence->invalidatePlaybackEvents();
    started=run();QVERIFY(started.value("ok").toBool());QTRY_VERIFY(sequence->isPlaying());for(int i=0;i<8;++i){driver->pulse();QTest::qWait(4);}
    QVERIFY(recording->onPitches.contains(36));QVERIFY(recording->onPitches.contains(40));QVERIFY(recording->onPitches.contains(42));
    int damper=-1,releasedAttack=-1;
    for(int i=0;i<recording->played.size();++i){auto event=recording->played[i];if(event.type()==ME_CONTROLLER&&event.controller()==64&&event.value()>=64&&damper<0)damper=i;if(event.type()==ME_NOTEON&&event.pitch()==36&&event.velo()&&releasedAttack<0)releasedAttack=i;}
    QVERIFY(damper>=0&&releasedAttack>damper);observer.stopReferencePlayback();QTRY_VERIFY(!sequence->isPlaying());
    source->startCmd();pedal->undoChangeProperty(Pid::SPANNER_TICKS,QVariant::fromValue(Fraction::fromTicks(end)));source->endCmd();QCOMPARE(pedal->ticks().ticks(),end);
    EventMap pedalEvents;source->renderMidi(&pedalEvents,true,MScore::playRepeats,main->synthesizerState());
    int releaseTick=-1;for(const auto& pair:pedalEvents)if(pair.second.type()==ME_CONTROLLER&&pair.second.controller()==64&&pair.second.value()<64&&pair.first>0)releaseTick=pair.first;
    QVERIFY(releaseTick>0);const int visualRelease=pedal->ticks().ticks()+begin-releaseTick;
    source->startCmd();pedal->undoChangeProperty(Pid::SPANNER_TICKS,QVariant::fromValue(Fraction::fromTicks(visualRelease)));source->endCmd();sequence->invalidatePlaybackEvents();
    pedalEvents.clear();source->renderMidi(&pedalEvents,true,MScore::playRepeats,main->synthesizerState());
    bool releaseAtStart=false;for(const auto& pair:pedalEvents)if(pair.first==begin&&pair.second.type()==ME_CONTROLLER&&pair.second.controller()==64&&pair.second.value()<64)releaseAtStart=true;
    QVERIFY(releaseAtStart);
    started=run();QVERIFY(started.value("ok").toBool());QTRY_VERIFY(sequence->isPlaying());for(int i=0;i<8;++i){driver->pulse();QTest::qWait(4);}
    QVERIFY(recording->onPitches.contains(60));QVERIFY(!recording->onPitches.contains(36));QVERIFY(!recording->onPitches.contains(40));QVERIFY(!recording->onPitches.contains(42));observer.stopReferencePlayback();QTRY_VERIFY(!sequence->isPlaying());
    auto revision=observer.analysisRevision();auto scope=observer.analysisScope(true);scope["start"]=begin;scope["end"]=end;
    const auto page=observer.performanceRange(begin,end,0,source->ntracks(),0,100,revision);QVariantList velocities;
    for(auto value:page.value("notes").toList()){auto note=value.toMap();if(note.value("tick").toInt()<begin)continue;note["oldType"]=note["velocityType"];note["oldRaw"]=note["velocityRaw"];note["oldBase"]=note["velocityBase"];note["target"]=100;note["unlock"]=true;velocities.append(note);}
    QVERIFY(!velocities.isEmpty());const QVariantMap candidate{{"revision",revision},{"scope",scope},{"velocity",velocities},{"tempos",QVariantList{}}};
    recording->onPitches.clear();auto heard=observer.auditionExpression(QString(),candidate,scope,false);QVERIFY2(heard.value("ok").toBool(),qPrintable(heard.value("error").toString()));QCOMPARE(heard.value("fromUtick").toInt(),begin);QTRY_VERIFY(sequence->isPlaying());for(int i=0;i<8;++i){driver->pulse();QTest::qWait(4);}
    QVERIFY(recording->onPitches.contains(60));QVERIFY(recording->onPitches.contains(38));QVERIFY(!recording->onPitches.contains(36));observer.finishExpressionAudition();
}

void referencePanelExactSelection()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);
    auto source=expressionScore080(main,_directory.filePath("reference-panel-scope-081.mscx"));QVERIFY(source);expressionShowMain080(main);
    const auto wave=referenceWave081(_directory.filePath("reference-panel-scope-081.wav"));QVERIFY(!wave.isEmpty());
    auto root=referenceOpen081(wave);QVERIFY(root);ReferencePanel081 view{root};
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>3900,10000);QTest::qWait(80);
    source->select(toChord(source->findCR(Fraction::fromTicks(2400),0))->upNote(),SelectType::SINGLE);
    source->select(toChord(source->findCR(Fraction::fromTicks(3360),0))->upNote(),SelectType::RANGE);
    QVERIFY(source->selection().isRange());QCOMPARE(source->selection().tickStart().ticks(),2400);QCOMPARE(source->selection().tickEnd().ticks(),3840);
    root->setProperty("a",500.);root->setProperty("b",2000.);QVERIFY(referenceClick081(root,"referenceAssociate"));
    QTRY_COMPARE(referenceAssociations081(root).size(),1);auto associated=referenceAssociations081(root).front().toMap();
    qInfo("created panel association %d..%d for selected 2400..3840",associated.value("start").toInt(),associated.value("end").toInt());
    QCOMPARE(associated.value("start").toInt(),2400);QCOMPARE(associated.value("end").toInt(),3840);
    source->select(toChord(source->findCR(Fraction::fromTicks(2880),0))->upNote(),SelectType::SINGLE);
    QVERIFY(referenceClick081(root,"referenceUpdateAssociationScore"));associated=referenceAssociations081(root).front().toMap();
    QCOMPARE(associated.value("start").toInt(),2880);QCOMPARE(associated.value("end").toInt(),3360);QVERIFY(associated.value("name").toString().contains(QString::fromUtf8("第3")));
    // The same precise selection covers a rest without changing expression's default note-range behaviour.
    source->startCmd();auto chord=source->findCR(Fraction::fromTicks(2880),0);source->cmdDeleteSelection();source->endCmd();
    auto rest=source->findCR(Fraction::fromTicks(2880),0);QVERIFY(rest&&rest->isRest());source->select(rest,SelectType::SINGLE);
    QVERIFY(referenceClick081(root,"referenceUpdateAssociationScore"));associated=referenceAssociations081(root).front().toMap();
    QCOMPARE(associated.value("start").toInt(),2880);QCOMPARE(associated.value("end").toInt(),3360);
    QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path())).filePath("reference-exact-scope-081.png")));
}

void referencePanelSavesAfterTransport()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);
    auto source=expressionScore080(main,_directory.filePath("reference-panel-save-081.mscx"));QVERIFY(source);expressionShowMain080(main);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* value){workflowContext()->finishPlaybackSession();value->stopWait();value->waitForStoppedRendering();value->setScoreView(nullptr);delete value;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    const auto wave=referenceWave081(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path())).filePath("reference-panel-save-081.wav"));QVERIFY(!wave.isEmpty());
    auto root=referenceOpen081(wave);QVERIFY(root);ReferencePanel081 view{root};
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>3900,10000);QTest::qWait(80);
    source->select(toChord(source->findCR(Fraction(),0))->upNote(),SelectType::SINGLE);source->select(toChord(source->findCR(Fraction::fromTicks(1440),0))->upNote(),SelectType::RANGE);
    root->setProperty("a",500.);root->setProperty("b",2500.);root->setProperty("jointVolume",.5);
    QVERIFY(referenceClick081(root,"referenceAssociate"));root->setProperty("transportTarget",2);root->setProperty("playbackMode",1);
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);
    root->setProperty("jointVolume",.37);QTest::qWait(350);QVERIFY(sequence->isPlaying());
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY(!sequence->isPlaying());
    auto saved=[&]{return QJsonDocument::fromJson(source->metaTag("kumo.reference").toUtf8()).toVariant().toMap();};
    QTest::qWait(450);qInfo("saved reference volume %.3f after stopping",saved().value("volume").toDouble());
    QTRY_COMPARE_WITH_TIMEOUT(saved().value("volume").toDouble(),.37,1500);
    const auto tracks=saved().value("tracks").toList();QCOMPARE(tracks.size(),1);QCOMPARE(tracks.front().toMap().value("associations").toList().size(),1);
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);
    root->setProperty("jointVolume",.42);QTest::qWait(80);root->disposePanelForReload();root=nullptr;QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    QTRY_VERIFY(!sequence->isPlaying());QTRY_COMPARE(saved().value("volume").toDouble(),.42);
    PluginAPI::Score wrapped(source);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    const QString savedPath=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path())).filePath("reference-saved-081.mscz");
    const auto written=observer.copyWorkflowScore(savedPath,observer.analysisRevision(),true);QVERIFY2(written.value("ok").toBool(),qPrintable(written.value("error").toString()));
    std::unique_ptr<MasterScore> reopened(main->readScore(savedPath));QVERIFY(reopened);
    const auto restored=QJsonDocument::fromJson(reopened->metaTag("kumo.reference").toUtf8()).toVariant().toMap();
    QCOMPARE(restored.value("volume").toDouble(),.42);QCOMPARE(restored.value("tracks").toList().size(),1);
}
