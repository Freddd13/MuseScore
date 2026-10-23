      void expressionEmbeddedRecordingResults()
            {
            expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
            QSettings().setValue("workflow/recordingDirectory",_directory.filePath("recordings"));
            auto score=expressionScore080(main,_directory.filePath("expression-recording-080.mscx"));QVERIFY(score);
            expressionShowMain080(main);auto root=expressionRoot080(main);QVERIFY(root);
            auto editor=main->performanceEditor();auto splitter=editor->findChild<QSplitter*>("performanceMainSplitter");QVERIFY(splitter);
            auto pane=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(pane);auto sizes=splitter->sizes();sizes[splitter->indexOf(pane)]=180;splitter->setSizes(sizes);
            QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
            QVERIFY(expressionClick080(root,"expressionRecord"));QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool(),10000);
            const auto notes=root->property("notes").value<QJSValue>().toVariant().toList();QCOMPARE(notes.size(),16);
            auto session=qobject_cast<MidiCaptureSession*>(root->property("captureSession").value<QObject*>());QVERIFY(session);
            QVERIFY(!session->begin({{"notes",notes},{"monitor",false}},false).contains("error"));root->setProperty("busy",true);
            const qint64 origin=midiInputTime();
            for(int i=0;i<notes.size();++i){
                const int pitch=notes[i].toMap().value("pitch").toInt();const qint64 when=origin+i*100000000LL;
                const int wait=int((when-midiInputTime())/1000000);if(wait>0)QTest::qWait(wait);
                captureMidiInput(NPlayEvent(ME_NOTEON,0,pitch,105),midiInputTime());QTest::qWait(20);captureMidiInput(NPlayEvent(ME_NOTEOFF,0,pitch,0),midiInputTime());
            }
            QVERIFY(expressionClick080(root,"expressionRecord"));
            const bool ready=QTest::qWaitFor([&]{return !root->property("busy").toBool()&&!expressionMap080(root,"candidate").isEmpty();},15000);
            qInfo()<<"recorded result"<<root->property("status")<<"busy / stale"<<root->property("busy")<<root->property("stale")
                <<"MIDI / matches"<<expressionMap080(root,"midi").value("notes").toList().size()<<expressionMap080(root,"result").value("matches").toList().size();
            QVERIFY(ready);
            QCOMPARE(expressionMap080(root,"result").value("matches").toList().size(),16);
            QTest::qWait(100);auto list=root->findChild<QQuickItem*>("embeddedExpressionResults");QVERIFY(list);
            const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
            root->window()->grabWindow().save(output.filePath("expression-embedded-recorded-080.png"));
            const QPointF firstRow=list->mapToItem(root,QPointF(20,25));
            qInfo("recorded results first row y %.1f, panel height %.1f",firstRow.y(),root->height());
            QVERIFY2(QRectF(0,0,root->width(),root->height()).contains(firstRow),"录完即可看见对应结果中的音名、拍位与力度");
            const int before=score->undoStack()->getCurIdx();
            const auto rowPoint=list->mapToScene(QPointF(80,25));QTest::mouseClick(list->window(),Qt::LeftButton,Qt::NoModifier,rowPoint.toPoint());QTRY_COMPARE(score->selection().noteList().size(),1);
            root->setProperty("applicationScope",0);QTRY_COMPARE(expressionMap080(root,"scopedCandidate").value("velocity").toList().size(),1);
            QVERIFY(expressionClick080(root,"expressionApplyVelocity"));QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==15,10000);QCOMPARE(score->undoStack()->getCurIdx(),before+1);
            root->setProperty("applicationScope",2);QTRY_COMPARE(expressionMap080(root,"scopedCandidate").value("velocity").toList().size(),15);
            QVERIFY(expressionClick080(root,"expressionApplyVelocity"));QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().isEmpty(),10000);QCOMPARE(score->undoStack()->getCurIdx(),before+2);
            for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))if(auto e=segment->element(0))if(e->isChord())for(auto note:toChord(e)->notes()){QCOMPARE(note->veloType(),Note::ValueType::USER_VAL);QCOMPARE(note->veloOffset(),105);}
            auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);
            const auto saved=observer->copyWorkflowScore(output.filePath("expression-adopted-080.mscz"),observer->analysisRevision(),true);QVERIFY2(saved.value("ok").toBool(),qPrintable(saved.value("error").toString()));
            std::unique_ptr<MasterScore> reopened(main->readScore(output.filePath("expression-adopted-080.mscz")));QVERIFY(reopened);
            PluginAPI::Score restoredScore(reopened.get());PluginAPI::ScoreObserver restored;restored.setScore(&restoredScore);QCOMPARE(restored.expressionVelocityHistory({}).value("count").toInt(),16);
            for(auto segment=reopened->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))if(auto e=segment->element(0))if(e->isChord())for(auto note:toChord(e)->notes()){QCOMPARE(note->veloType(),Note::ValueType::USER_VAL);QCOMPARE(note->veloOffset(),105);}
            EditData edit;score->undoRedo(true,&edit);QCOMPARE(score->undoStack()->getCurIdx(),before+1);
            int retained=0;for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))if(auto e=segment->element(0))if(e->isChord())for(auto note:toChord(e)->notes())if(note->veloType()==Note::ValueType::USER_VAL&&note->veloOffset()==105)++retained;
            QCOMPARE(retained,1);score->undoRedo(true,&edit);QCOMPARE(score->undoStack()->getCurIdx(),before);
            for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))if(auto e=segment->element(0))if(e->isChord())for(auto note:toChord(e)->notes()){QCOMPARE(note->veloType(),Note::ValueType::OFFSET_VAL);QCOMPARE(note->veloOffset(),0);}
            }
      void expressionFullPhraseTransport()
            {
            expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
            auto score=expressionScore080(main,_directory.filePath("expression-playback-080.mscx"));QVERIFY(score);
            expressionShowMain080(main);
            MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
            Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
            std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();stopPlaybackPreview(true);s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
            auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);observer.workflowInfo();
            const auto revision=observer.analysisRevision();auto range=observer.analysisScope(true);QCOMPARE(range.value("end").toInt(),7680);
            auto page=observer.performanceRange(0,7680,0,4,0,100,revision);QVariantList values;
            for(auto value:page.value("notes").toList()){auto n=value.toMap();n["oldType"]=n["velocityType"];n["oldRaw"]=n["velocityRaw"];n["oldBase"]=n["velocityBase"];n["target"]=105;n["unlock"]=true;n["writeMode"]="fixed";values.append(n);}
            QCOMPARE(values.size(),16);QVariantMap candidate{{"revision",revision},{"scope",range},{"velocity",values},{"tempos",QVariantList{}}};
            const auto originalState=score->state();const int tabs=main->scores().size();
            for(int round=0;round<2;++round){
                recording->onPitches.clear();recording->onVelocities.clear();auto started=observer.auditionExpression(QString(),candidate,range,true);QVERIFY2(started.value("ok").toBool(),qPrintable(started.value("error").toString()));
                QTRY_VERIFY(sequence->isPlaying());QCOMPARE(started.value("untilUtick").toInt(),7680);
                for(int i=0;i<70&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(2);}const int before=sequence->getCurTick();QVERIFY(before>1920);
                auto switched=observer.auditionExpression(QString(),candidate,range,false);QVERIFY(switched.value("ok").toBool());QCOMPARE(switched.value("fromUtick").toInt(),before);
                QTRY_VERIFY(sequence->isPlaying());
                for(int i=0;i<180&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(2);}QTRY_VERIFY(!sequence->isPlaying());
                QVERIFY2(recording->onPitches.contains(74),"试听输出到第三小节，涵盖第一小节之后的音");QVERIFY(recording->onPitches.size()>=16);
                QVERIFY(observer.finishExpressionAudition().value("ok").toBool());QCOMPARE(main->scores().size(),tabs);QVERIFY(score->state()==originalState);
            }
            std::unique_ptr<MasterScore> played(main->readScore(_directory.filePath("expression-playback-080.mscx")));QVERIFY(played);
            QMap<Note*,VelocityEdit> changed;
            for(auto segment=played->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))if(auto e=segment->element(0))if(e->isChord())for(auto note:toChord(e)->notes())changed.insert(note,{Note::ValueType::USER_VAL,105});
            QVERIFY(ParameterEdit::velocities(played.get(),changed));QFile midi(_directory.filePath("expression-performed-080.mid"));QVERIFY(midi.open(QIODevice::WriteOnly));ExportMidi exporter(played.get());QVERIFY(exporter.write(&midi,true,true,main->synthesizerState()));midi.close();
            auto root=expressionRoot080(main);QVERIFY(root);QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));root->setProperty("applicationScope",2);
            QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi.fileName()).toString())));
            QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==16,15000);
            QTest::qWait(100);recording->onPitches.clear();QVERIFY(expressionClick080(root,"expressionCandidateAudition"));QTRY_VERIFY(sequence->isPlaying());
            auto proposedButton=root->findChild<QQuickItem*>("expressionCandidateAudition");auto sourceButton=root->findChild<QQuickItem*>("expressionOriginalAudition");QVERIFY(proposedButton&&sourceButton);QTRY_VERIFY(proposedButton->property("checked").toBool());
            for(int i=0;i<70;++i){driver->pulse();QTest::qWait(2);}const int beforeSwitch=sequence->getCurTick();QVERIFY(beforeSwitch>1920);
            QVERIFY(expressionClick080(root,"expressionOriginalAudition"));QTRY_VERIFY(sequence->isPlaying());QCOMPARE(sequence->getCurTick(),beforeSwitch);
            const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->isVisible());QVERIFY(root->window()->grabWindow().save(output.filePath("expression-source-playback-080.png")));
            qInfo("source playback mode %s, source button checked %d",qPrintable(expressionMap080(root,"playbackState").value("mode").toString()),sourceButton->property("checked").toBool());
            QTRY_VERIFY2(sourceButton->property("checked").toBool(),"正在听谱中效果时，对应按钮保持选中");
            for(int i=0;i<180&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(2);}QTRY_VERIFY(!sequence->isPlaying());QVERIFY(recording->onPitches.contains(74));QVERIFY(recording->onPitches.size()>=16);
            QVERIFY(expressionClick080(root,"expressionCandidateAudition"));QTRY_VERIFY(sequence->isPlaying());for(int i=0;i<5;++i)driver->pulse();
            auto list=root->findChild<QQuickItem*>("embeddedExpressionResults");QVERIFY(list);const auto rowPoint=list->mapToScene(QPointF(80,25));const int beforeSelection=sequence->getCurTick();
            QTest::mouseClick(list->window(),Qt::LeftButton,Qt::NoModifier,rowPoint.toPoint());QVERIFY(sequence->isPlaying());QCOMPARE(sequence->getCurTick(),beforeSelection);QTRY_COMPARE(score->selection().noteList().size(),1);
            root->setProperty("applicationScope",0);QTRY_COMPARE(expressionMap080(root,"scopedCandidate").value("velocity").toList().size(),1);
            const int undo=score->undoStack()->getCurIdx();QVERIFY(expressionClick080(root,"expressionApplyVelocity"));
            QTRY_VERIFY_WITH_TIMEOUT(root->property("status").toString().contains("已使用演奏力度"),10000);QVERIFY(!sequence->isPlaying());QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
            EditData edit;score->undoRedo(true,&edit);
            }
