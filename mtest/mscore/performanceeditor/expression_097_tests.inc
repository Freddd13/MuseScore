static bool expressionMidi097(const QString& path,const QVariantList& notes)
{
    struct Event {int tick;QByteArray bytes;};QVector<Event> events;
    for(auto value:notes){auto note=value.toMap();const double original=note.value("scoreSeconds").toDouble();const double seconds=original*(1.+.08*qSin(original/2.));
        const int tick=qRound(seconds*960),pitch=note.value("pitch").toInt();
        QByteArray on=QByteArray::fromHex("90"),off=QByteArray::fromHex("80");on.append(char(pitch));on.append(char(96));off.append(char(pitch));off.append(char(0));events.append({tick,on});events.append({tick+120,off});}
    std::stable_sort(events.begin(),events.end(),[](const Event&a,const Event&b){return a.tick<b.tick;});
    QByteArray track=QByteArray::fromHex("00ff510307a120");int previous=0;
    auto delta=[&](int value){QByteArray data;data.prepend(char(value&127));while((value>>=7))data.prepend(char((value&127)|128));track+=data;};
    for(auto event:events){delta(event.tick-previous);track+=event.bytes;previous=event.tick;}track+=QByteArray::fromHex("00ff2f00");
    QByteArray data=QByteArray::fromHex("4d546864000000060000000101e04d54726b");for(int shift=24;shift>=0;shift-=8)data.append(char(track.size()>>shift));data+=track;
    QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size();
}
void TestPerformanceEditor::expressionTempoWithRestsAndCurves()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);
    const auto raw=_directory.filePath("tempo-source-097.mscx");QVERIFY(expressionScore080(main,raw));
    QFile xmlFile(raw);QVERIFY(xmlFile.open(QIODevice::ReadOnly));QString xml=QString::fromUtf8(xmlFile.readAll());xmlFile.close();
    const QString chord="<Chord><durationType>quarter</durationType><Note><pitch>67</pitch><tpc>15</tpc></Note></Chord>";const int at=xml.indexOf(chord);QVERIFY(at>=0);xml.replace(at,chord.size(),"<Rest><durationType>quarter</durationType></Rest>");
    const auto fixture=_directory.filePath("tempo-rest-097.mscx");QFile file(fixture);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml.toUtf8());file.close();
    auto source=main->readScore(fixture);QVERIFY(source);main->setCurrentScoreView(main->appendScore(source));
    QString error;QVERIFY2(ParameterEdit::writeTapTempo(source,0,120,false,&error),qPrintable(error));QVERIFY2(ParameterEdit::writeTapTempo(source,1920,80,false,&error),qPrintable(error));
    QVERIFY2(ParameterEdit::tempoCurve(source,4320,5760,nullptr,false,&error),qPrintable(error));QVERIFY(ParameterEdit::restoreTempo(source,5760,&error));
    source->tempomap()->setRelTempo(1.5);TempoMap original=*source->tempomap();original.setRelTempo(1.0);
    expressionShowMain080(main);auto root=expressionRoot080(main);QVERIFY(root);root->setProperty("role",0);root->setProperty("applicationScope",2);
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);observer->workflowInfo();
    const auto page=observer->performanceRange(0,7680,0,4,0,100,observer->analysisRevision());const auto notes=page.value("notes").toList();QCOMPARE(notes.size(),15);
    const auto midi=_directory.filePath("tempo-played-097.mid");QVERIFY(expressionMidi097(midi,notes));
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==15,15000);
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"keepDuration"),Q_ARG(QVariant,true)));
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"rubatoStrength"),Q_ARG(QVariant,80)));
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"rubato"),Q_ARG(QVariant,true)));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!root->property("previewUpdating").toBool()&&!expressionMap080(root,"candidate").value("tempos").toList().isEmpty(),15000);
    const int previewVersion=root->property("previewVersion").toInt();
    workflowContext()->expressionAction("tempoTargets",QString(),"[{\"tick\":480,\"bpm\":130}]");
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("previewUpdating").toBool()&&root->property("previewVersion").toInt()>previewVersion,10000);
    bool adjusted=false;for(auto value:expressionMap080(root,"candidate").value("tempos").toList())if(value.toMap().value("tick").toInt()==480){adjusted=true;QVERIFY(qAbs(value.toMap().value("bpm").toDouble()-130)<.00001);}QVERIFY(adjusted);
    auto candidate=expressionMap080(root,"candidate");const int initial=source->undoStack()->getCurIdx();const auto state=source->state();
    const auto summary=candidate.value("tempoSummary").toMap();QVERIFY(qAbs(summary.value("originalDuration").toDouble()-(original.tick2time(7680)-original.tick2time(0)))<.0001);
    auto snapshot=expressionMap080(root,"tempoContext");QVERIFY(!snapshot.value("protectedRanges").toList().isEmpty());
    bool restMark=false;for(auto item:snapshot.value("protectedTicks").toList())if(item.toMap().value("tick").toInt()==1920)restMark=true;QVERIFY(restMark);
    for(auto item:candidate.value("tempos").toList()){const int tick=item.toMap().value("tick").toInt();QVERIFY(tick!=0&&tick!=1920&&!(tick>=4320&&tick<5760)&&tick!=5760);}
    {
        MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
        auto sequence=new PerformanceAuditionSeq;Ms::seq=sequence;
        std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();stopPlaybackPreview(true);s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
        auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
        auto response=observer->auditionExpression(QString(),candidate,expressionMap080(root,"scope"),true);QVERIFY2(response.value("ok").toBool(),qPrintable(response.value("error").toString()));QTRY_VERIFY(sequence->isPlaying());
        for(int n=0;n<500&&sequence->isPlaying();++n){driver->pulse();QTest::qWait(1);}QTRY_VERIFY(!sequence->isPlaying());QVERIFY(recording->onPitches.size()>=15);QCOMPARE(source->state(),state);QCOMPARE(source->undoStack()->getCurIdx(),initial);
        QVERIFY(observer->finishExpressionAudition().value("ok").toBool());
    }
    QVERIFY(QMetaObject::invokeMethod(root,"revealResults"));QTest::qWait(80);
    const auto artifacts=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->grabWindow().save(artifacts.filePath("expression-tempo-preview-097.png")));
    qInfo("tempo context: native page %.3f ms, largest GUI read %.3f ms",page.value("milliseconds").toDouble(),root->property("worstBatch").toDouble());
    QVERIFY(expressionClick080(root,"expressionApplyTempo"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!root->property("previewUpdating").toBool()&&source->undoStack()->getCurIdx()==initial+1,15000);
    QCOMPARE(source->tempomap()->relTempo(),1.5);TempoMap applied=*source->tempomap();applied.setRelTempo(1.0);
    QVERIFY(qAbs(applied.tick2time(7680)-applied.tick2time(0)-summary.value("originalDuration").toDouble())<.001);
    for(const auto& event:original)if(event.first>=4320&&event.first<=5760)QVERIFY(qAbs(applied.tempo(event.first)-event.second.tempo)<.00001);
    QCOMPARE(applied.tempo(1920),80./60);QCOMPARE(expressionMap080(root,"candidate").value("tempos").toList().size(),0);
    QVERIFY(!ExpressionTempo::read(source).isEmpty());
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));const auto saved=output.filePath("expression-tempo-adopted-097.mscz");
    QVERIFY(observer->copyWorkflowScore(saved,observer->analysisRevision(),true).value("ok").toBool());
    std::unique_ptr<MasterScore> reopened(main->readScore(saved));QVERIFY(reopened);QCOMPARE(ExpressionTempo::read(reopened.get()),ExpressionTempo::read(source));
    for(const auto& event:applied)QVERIFY(qAbs(reopened->tempomap()->tempo(event.first)-event.second.tempo)<.00001);
    EditData edit;source->undoRedo(true,&edit);QCOMPARE(source->undoStack()->getCurIdx(),initial);QVERIFY(ExpressionTempo::read(source).isEmpty());
    for(const auto& event:original)QVERIFY(qAbs(source->tempomap()->tempo(event.first)-event.second.tempo)<.00001);
    source->undoRedo(false,&edit);QCOMPARE(source->undoStack()->getCurIdx(),initial+1);QVERIFY(!ExpressionTempo::read(source).isEmpty());
    QVERIFY(root->window()->grabWindow().save(output.filePath("expression-tempo-used-097.png")));
}

void TestPerformanceEditor::expressionTempoSelectedRanges()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);
    auto source=expressionScore080(main,_directory.filePath("tempo-selected-097.mscx"));QVERIFY(source);
    QString issue;QVERIFY(ParameterEdit::writeTapTempo(source,0,120,false,&issue));
    expressionShowMain080(main);auto root=expressionRoot080(main);QVERIFY(root);root->setProperty("role",0);root->setProperty("applicationScope",2);
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);observer->workflowInfo();
    auto notes=observer->performanceRange(0,7680,0,4,0,100,observer->analysisRevision()).value("notes").toList();QCOMPARE(notes.size(),16);
    auto midi=_directory.filePath("tempo-selected-097.mid");QVERIFY(expressionMidi097(midi,notes));
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"previewTempo"),Q_ARG(QVariant,QVariantMap({{"480",130.}}))));
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"previewVelocity"),Q_ARG(QVariant,QVariantMap({{"previous-material",20}}))));
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==16,15000);
    QVERIFY(expressionMap080(root,"config").value("previewTempo").toMap().isEmpty());QVERIFY(expressionMap080(root,"config").value("previewVelocity").toMap().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"rubato"),Q_ARG(QVariant,true)));
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"keepDuration"),Q_ARG(QVariant,false)));
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"rubatoStrength"),Q_ARG(QVariant,100)));
    source->select(toChord(source->findCR(Fraction::fromTicks(480),0))->upNote(),SelectType::SINGLE);
    source->select(toChord(source->findCR(Fraction::fromTicks(2400),0))->upNote(),SelectType::ADD);
    root->setProperty("applicationScope",0);QVERIFY(QMetaObject::invokeMethod(root,"refreshLiveSelection"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!root->property("previewUpdating").toBool()&&expressionMap080(root,"candidate").value("tempoSummary").toMap().value("ranges").toInt()==2,15000);
    const auto selected=expressionMap080(root,"candidate");QCOMPARE(selected.value("tempoScoped").toBool(),true);QVERIFY(!selected.value("tempos").toList().isEmpty());
    for(auto value:selected.value("tempos").toList()){int tick=value.toMap().value("tick").toInt();QVERIFY(tick==480||tick==960||tick==2400||tick==2880);}
    const int initial=source->undoStack()->getCurIdx();QVERIFY(QMetaObject::invokeMethod(root,"revealResults"));QTest::qWait(80);QVERIFY(expressionClick080(root,"expressionApplyTempo"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!root->property("previewUpdating").toBool()&&source->undoStack()->getCurIdx()==initial+1,15000);
    for(int tick:{0,960,1920,2880,3360,7200})QVERIFY(qAbs(source->tempomap()->tempo(tick)-2)<.00001);
    QVERIFY(qAbs(source->tempomap()->tempo(480)-2)>.01);QVERIFY(qAbs(source->tempomap()->tempo(2400)-2)>.01);
    QTRY_COMPARE(expressionMap080(root,"candidate").value("tempos").toList().size(),0);
    const double previousFirst=source->tempomap()->tempo(480),previousSecond=source->tempomap()->tempo(2400);
    source->selection().setRangeTicks(Fraction::fromTicks(3840),Fraction::fromTicks(5760),0,1);source->selection().updateSelectedElements();
    root->setProperty("applicationScope",1);QVERIFY(QMetaObject::invokeMethod(root,"refreshLiveSelection"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("previewUpdating").toBool()&&!expressionMap080(root,"candidate").value("tempos").toList().isEmpty(),15000);
    const auto range=expressionMap080(root,"candidate");for(auto value:range.value("tempos").toList()){int tick=value.toMap().value("tick").toInt();QVERIFY(tick>=3840&&tick<=5760);}
    QVERIFY(QMetaObject::invokeMethod(root,"revealResults"));QTest::qWait(80);QVERIFY(expressionClick080(root,"expressionApplyTempo"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!root->property("previewUpdating").toBool()&&source->undoStack()->getCurIdx()==initial+2,15000);
    QCOMPARE(source->tempomap()->tempo(480),previousFirst);QCOMPARE(source->tempomap()->tempo(2400),previousSecond);QVERIFY(qAbs(source->tempomap()->tempo(5760)-2)<.00001);
    QCOMPARE(expressionMap080(root,"candidate").value("tempos").toList().size(),0);
    EditData edit;source->undoRedo(true,&edit);QVERIFY(qAbs(source->tempomap()->tempo(3840)-2)<.00001);QCOMPARE(source->tempomap()->tempo(480),previousFirst);
    source->undoRedo(true,&edit);QVERIFY(ExpressionTempo::read(source).isEmpty());for(int tick:{480,2400,3840,5760})QVERIFY(qAbs(source->tempomap()->tempo(tick)-2)<.00001);
    source->undoRedo(false,&edit);source->undoRedo(false,&edit);QCOMPARE(source->undoStack()->getCurIdx(),initial+2);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->grabWindow().save(output.filePath("expression-tempo-selected-097.png")));
}

void TestPerformanceEditor::expressionRecordingAcrossLongRest()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);
    QSettings().setValue("workflow/recordingDirectory",_directory.filePath("recordings-097"));
    const auto raw=_directory.filePath("capture-source-097.mscx");QVERIFY(expressionScore080(main,raw));
    QFile file(raw);QVERIFY(file.open(QIODevice::ReadOnly));QString xml=QString::fromUtf8(file.readAll());file.close();
    const int boundary=xml.indexOf("</Measure><Measure>");QVERIFY(boundary>=0);
    xml.insert(boundary+10,"<Measure><voice><Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure>");
    const auto fixture=_directory.filePath("capture-rest-097.mscx");file.setFileName(fixture);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml.toUtf8());file.close();
    auto score=main->readScore(fixture);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));expressionShowMain080(main);
    auto root=expressionRoot080(main);QVERIFY(root);root->setProperty("role",0);root->setProperty("applicationScope",2);
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"rubato"),Q_ARG(QVariant,false)));
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    QVERIFY(expressionClick080(root,"expressionRecord"));QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool(),10000);
    const auto notes=root->property("notes").value<QJSValue>().toVariant().toList();QCOMPARE(notes.size(),16);
    auto capture=qobject_cast<MidiCaptureSession*>(root->property("captureSession").value<QObject*>());QVERIFY(capture);
    QSignalSpy finished(capture,&MidiCaptureSession::finished),failed(capture,&MidiCaptureSession::failed);
    QVERIFY(!capture->begin({{"notes",notes},{"monitor",false},{"endMode","passage"}},false).contains("error"));root->setProperty("busy",true);
    for(int i=0;i<notes.size();++i){
        if(i==4){QTest::qWait(1100);QVERIFY(capture->state().value("recording").toBool());QCOMPARE(finished.size(),0);}
        const int pitch=notes[i].toMap().value("pitch").toInt();
        captureMidiInput(NPlayEvent(ME_NOTEON,0,pitch,101),midiInputTime());QTest::qWait(25);captureMidiInput(NPlayEvent(ME_NOTEOFF,0,pitch,0),midiInputTime());QTest::qWait(90);
        QTRY_VERIFY(capture->state().value("matchedTick").toInt()>=notes[i].toMap().value("tick").toInt());
    }
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);QCOMPARE(failed.size(),0);
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!root->property("previewUpdating").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==16,15000);
    QCOMPARE(expressionMap080(root,"result").value("missing").toList().size(),0);
    for(auto value:expressionMap080(root,"result").value("matches").toList())QCOMPARE(value.toMap().value("velocity").toInt(),101);
    const int before=score->undoStack()->getCurIdx();QVERIFY(QMetaObject::invokeMethod(root,"revealResults"));QTest::qWait(80);QVERIFY(expressionClick080(root,"expressionApplyVelocity"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&score->undoStack()->getCurIdx()==before+1,10000);
    for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))if(auto e=segment->element(0))if(e->isChord())QCOMPARE(toChord(e)->upNote()->veloOffset(),101);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->grabWindow().save(output.filePath("expression-recorded-rest-097.png")));
}
