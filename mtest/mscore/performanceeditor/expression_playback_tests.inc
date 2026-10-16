      void expressionPlaybackKeepsScoreAndBeat()
            {
            auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            std::unique_ptr<MasterScore> material(main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx"));QVERIFY(material);
            auto original=renderPlaybackPreview(score,material.get());
            auto element=material->firstMeasure()->first(SegmentType::ChordRest)->element(4);QVERIFY(element&&element->isChord());auto chord=toChord(element);QMap<Note*,VelocityEdit> changes;for(auto note:chord->notes())changes.insert(note,{Note::ValueType::USER_VAL,110});QVERIFY(ParameterEdit::velocities(material.get(),changes));
            auto proposed=renderPlaybackPreview(score,material.get());
            QVariantMap piece{{"from",0.},{"until",2000.},{"at",0.},{"duration",2000.}};
            QVariantMap clip{{"pieces",QVariantList{piece}}};
            original.reference={{"startFrame",0},{"clock",QVariantList{}},{"clips",QVariantList{clip}}};alignPreviewReference(original);
            proposed.reference=original.reference;alignPreviewReference(proposed);
            for(const auto& item:proposed.events){if(item.second.note())QCOMPARE(item.second.note()->masterScore(),score);if(item.second.noteEventOwner())QCOMPARE(item.second.noteEventOwner()->masterScore(),score);}
            material.reset(); // All playback note pointers now refer to the displayed score.
            MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
            Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
            std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){stopPlaybackPreview(true);s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
            auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
            const int tabs=main->scores().size(),undo=score->undoStack()->getCurIdx();const auto state=score->state();
            preparePlaybackPreview(score,"test",std::move(original),std::move(proposed));QVERIFY(playbackPreviewPrepared(score,"test"));
            auto audioPlan=playbackPreviewReferencePlan(score,true,480);auto audioPiece=audioPlan["clips"].toList().front().toMap()["pieces"].toList().front().toMap();QVERIFY(qAbs(audioPiece["from"].toDouble()-500.)<.01);QVERIFY(qAbs(audioPiece["duration"].toDouble()-1500.)<.01);
            QVariantMap range{{"start",0},{"end",score->lastMeasure()->endTick().ticks()},{"occurrence",1}};
            auto started=startPlaybackPreview(score,true,range);QVERIFY2(started["ok"].toBool(),qPrintable(started["error"].toString()));QApplication::processEvents();QVERIFY(sequence->isPlaying());
            for(int i=0;i<5;++i)driver->pulse();QVERIFY(recording->onVelocities.contains(110));int before=sequence->getCurTick();QVERIFY(before>0);
            auto switched=startPlaybackPreview(score,false,range);QVERIFY2(switched["ok"].toBool(),qPrintable(switched["error"].toString()));QCOMPARE(switched["fromUtick"].toInt(),before);QApplication::processEvents();QVERIFY(sequence->isPlaying());
            QCOMPARE(main->scores().size(),tabs);QCOMPARE(main->currentScore(),static_cast<Score*>(score));QCOMPARE(score->undoStack()->getCurIdx(),undo);QVERIFY(score->state()==state);
            QCOMPARE(workflowContext()->audition()["mode"].toString(),QString("score"));
            for(int i=0;i<5;++i)driver->pulse();QVERIFY(sequence->getCurTick()>before);
            stopPlaybackPreview(true);QApplication::processEvents();QVERIFY(!sequence->isPlaying());QVERIFY(!activePlaybackPreviewEvents(score));QCOMPARE(referenceEndTick(),-1);QVERIFY(!workflowContext()->audition()["playing"].toBool());
            // The actual plugin API prepares, swaps and frees its temporary material on the source tab.
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);observer.workflowInfo();
            auto revision=observer.analysisRevision();auto scope=observer.analysisScope(true);scope["occurrence"]=1;
            auto page=observer.performanceRange(0,scope["end"].toInt(),0,score->ntracks(),0,100,revision);QVariantList targets;
            for(auto value:page["notes"].toList()){auto n=value.toMap();n["oldType"]=n["velocityType"];n["oldRaw"]=n["velocityRaw"];n["oldBase"]=n["velocityBase"];n["target"]=105;n["unlock"]=true;targets.append(n);}
            QVERIFY(!targets.isEmpty());QVariantMap candidate{{"revision",revision},{"scope",scope},{"velocity",targets},{"tempos",QVariantList{}}};
            auto prepared=observer.auditionExpression(QString(),candidate,scope,true);QVERIFY2(prepared["ok"].toBool(),qPrintable(prepared["error"].toString()));QApplication::processEvents();QVERIFY(sequence->isPlaying());
            for(int i=0;i<5;++i)driver->pulse();before=sequence->getCurTick();
            auto continued=observer.auditionExpression(QString(),candidate,scope,true);QVERIFY(continued["continuing"].toBool());QCOMPARE(sequence->getCurTick(),before);
            auto source=observer.auditionExpression(QString(),candidate,scope,false);QVERIFY(source["ok"].toBool());QCOMPARE(source["fromUtick"].toInt(),before);QApplication::processEvents();QVERIFY(sequence->isPlaying());
            QCOMPARE(main->scores().size(),tabs);QCOMPARE(main->currentScore(),static_cast<Score*>(score));QCOMPARE(score->undoStack()->getCurIdx(),undo);QVERIFY(score->state()==state);
            QVERIFY(observer.finishExpressionAudition()["ok"].toBool());QApplication::processEvents();QVERIFY(!sequence->isPlaying());

            // A comparison loop reuses its prepared material and preserves toolbar choices.
            auto countAction=getAction("countin"),loopAction=getAction("loop");QVERIFY(countAction&&loopAction);const bool oldCount=countAction->isChecked(),oldLoop=loopAction->isChecked();countAction->setChecked(true);loopAction->setChecked(true);
            QSignalSpy playbackErrors(&observer,&PluginAPI::ScoreObserver::workflowPlaybackError);scope["loop"]=true;QSignalSpy loops(sequence,&Seq::started);prepared=observer.auditionExpression(QString(),candidate,scope,true);QVERIFY2(prepared["ok"].toBool(),qPrintable(prepared["error"].toString()));QVERIFY(!countAction->isChecked());QVERIFY(!loopAction->isChecked());
            for(int i=0;i<180&&loops.count()<2;++i){driver->pulse();QTest::qWait(4);}QVERIFY2(loops.count()>=2,"comparison did not repeat");QVERIFY(observer.stopWorkflowPlayback()["ok"].toBool());QTest::qWait(80);QVERIFY(!sequence->isPlaying());QVERIFY(countAction->isChecked());QVERIFY(loopAction->isChecked());
            // Joint playback keeps the longer side playing, then repeats the selected phrase.
            auto project=referenceProject();QVERIFY2(project->property("available").toBool(),qPrintable(project->property("status").toString()));
            const QString audioPath=_directory.filePath("expression-reference-050.wav");QFile audio(audioPath);QVERIFY(audio.open(QIODevice::WriteOnly));QDataStream stream(&audio);stream.setByteOrder(QDataStream::LittleEndian);const int sampleCount=44100*4;stream.writeRawData("RIFF",4);stream<<quint32(36+sampleCount*2);stream.writeRawData("WAVEfmt ",8);stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(44100)<<quint32(88200)<<quint16(2)<<quint16(16);stream.writeRawData("data",4);stream<<quint32(sampleCount*2);QByteArray silence(sampleCount*2,0);stream.writeRawData(silence.constData(),silence.size());audio.close();
            auto requestFor=[&](int milliseconds,int mode){QVariantMap association{{"start",0},{"end",scope["end"]},{"from",0},{"until",milliseconds},{"active",true},{"occurrence",1}};QVariantMap track{{"id","transport-test"},{"path",audioPath},{"name","reference"},{"volume",0.},{"associations",QVariantList{association}}};return QVariantMap{{"revision",observer.analysisRevision()},{"start",0},{"end",scope["end"]},{"mode",mode},{"speed",1.},{"leader",association},{"tracks",QVariantList{track}},{"volume",0.}};};
            auto jointPlan=observer.referencePlan(requestFor(1000,0));QVERIFY2(jointPlan["ok"].toBool(),qPrintable(jointPlan["error"].toString()));auto joint=observer.startReferencePlayback(jointPlan);QVERIFY2(joint["ok"].toBool(),qPrintable(joint["error"].toString()));QTRY_VERIFY(project->property("prepared").toBool());QTest::qWait(50);
            for(int i=0;i<32;++i){driver->pulse();QTest::qWait(4);}QTRY_VERIFY(project->property("completed").toBool());QVERIFY2(sequence->isPlaying(),"the shorter audio stopped the score");
            for(int i=0;i<50&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(4);}QTRY_VERIFY(!sequence->isPlaying());QTest::qWait(60);QVERIFY(countAction->isChecked());QVERIFY(loopAction->isChecked());
            jointPlan=observer.referencePlan(requestFor(3000,0));QVERIFY(jointPlan["ok"].toBool());joint=observer.startReferencePlayback(jointPlan);QVERIFY(joint["ok"].toBool());QTRY_VERIFY(project->property("prepared").toBool());QTest::qWait(50);
            for(int i=0;i<48;++i){driver->pulse();QTest::qWait(4);}QVERIFY(!sequence->isPlaying());QVERIFY2(project->property("playing").toBool(),"the shorter score stopped the audio");
            for(int i=0;i<40&&project->property("playing").toBool();++i){driver->pulse();QTest::qWait(4);}QTRY_VERIFY(!project->property("playing").toBool());QTest::qWait(60);
            jointPlan=observer.referencePlan(requestFor(2000,1));jointPlan["loop"]=true;QSignalSpy jointLoops(sequence,&Seq::started);joint=observer.startReferencePlayback(jointPlan);QVERIFY(joint["ok"].toBool());QTRY_VERIFY(project->property("prepared").toBool());
            for(int i=0;i<220&&jointLoops.count()<2;++i){driver->pulse();QTest::qWait(4);}QVERIFY2(jointLoops.count()>=2,"joint playback did not repeat");QVERIFY(observer.stopReferencePlayback()["ok"].toBool());QTest::qWait(100);QVERIFY(!sequence->isPlaying());QVERIFY(!project->property("playing").toBool());QVERIFY(countAction->isChecked());QVERIFY(loopAction->isChecked());
            countAction->setChecked(oldCount);loopAction->setChecked(oldLoop);

            }
