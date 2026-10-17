      void gatePlaybackKeepsScoreAndBeat()
            {
            auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            auto pedal=new Pedal(score);pedal->setTick(Fraction::fromTicks(0));pedal->setTicks(Fraction::fromTicks(1920));pedal->setTrack(4);pedal->setTrack2(4);score->addSpanner(pedal);
            ExportMidi exporter(score);exporter.collectGateData=true;QBuffer bytes;bytes.open(QIODevice::WriteOnly);QVERIFY(exporter.write(&bytes,true,true,main->synthesizerState()));MidiGateProcessor processor;MidiGateOptions options;options.jitter=0;auto rows=processor.process(exporter.gateData,options);int pitch=-1;for(const auto& note:rows)if(note.newOff<note.off){pitch=note.pitch;break;}QVERIFY(pitch>=0);
            auto material=prepareGateAudition(score,exporter.gateData,rows);QCOMPARE(material.unmatched,0);QVERIFY(material.changed>0);
            class Recording:public PerformanceRecordingSynth {public:QVector<QPair<int,unsigned>> off;void play(const PlayEvent& event)override{PerformanceRecordingSynth::play(event);if(event.type()==ME_NOTEOFF||(event.type()==ME_NOTEON&&event.velo()==0))off.append({event.pitch(),frames});}};
            MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new Recording;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
            std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){stopPlaybackPreview(true);s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
            auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
            const int tabs=main->scores().size(),undo=score->undoStack()->getCurIdx();const auto state=score->state();preparePlaybackPreview(score,"gate-test",std::move(material.original),std::move(material.proposed));QVariantMap range{{"start",0},{"end",score->lastMeasure()->endTick().ticks()},{"occurrence",1}};
            QVector<unsigned> onTimes,offTimes;
            for(bool proposed:{false,true}){
                  recording->onFrames.clear();recording->onPitches.clear();recording->off.clear();const unsigned start=recording->frames;
                  const auto result=startPlaybackPreview(score,proposed,range);QVERIFY2(result.value("ok").toBool(),qPrintable(result.value("error").toString()));QApplication::processEvents();
                  for(int i=0;i<180&&sequence->isPlaying();++i){driver->pulse();QTest::qWait(2);}QTRY_VERIFY(!sequence->isPlaying());
                  unsigned on=UINT_MAX,off=UINT_MAX;for(int i=0;i<recording->onFrames.size();++i)if(recording->onPitches[i]==pitch){on=recording->onFrames[i]-start;break;}
                  for(const auto& value:recording->off)if(value.first==pitch&&value.second>start+on){off=value.second-start;break;}
                  QVERIFY(on!=UINT_MAX&&off!=UINT_MAX);onTimes.append(on);offTimes.append(off);
                  }
            QCOMPARE(onTimes[0],onTimes[1]);QVERIFY(offTimes[1]<offTimes[0]);
            QVERIFY(startPlaybackPreview(score,false,range).value("ok").toBool());QApplication::processEvents();for(int i=0;i<5;++i)driver->pulse();const int before=sequence->getCurTick();QVERIFY(before>0);const auto switched=startPlaybackPreview(score,true,range);QVERIFY(switched.value("ok").toBool());QCOMPARE(switched.value("fromUtick").toInt(),before);
            QCOMPARE(main->scores().size(),tabs);QCOMPARE(main->currentScore(),static_cast<Score*>(score));QCOMPARE(score->undoStack()->getCurIdx(),undo);QVERIFY(score->state()==state);stopPlaybackPreview(true);
            }

      void regionalPlaybackSwitchesSamePhrase(){
            auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/p0native/longroot.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));auto view=main->currentScoreView();
            auto group=RegionalVariants::create(score,0,score->firstMeasure()->endTick().ticks(),{4},"左手");QVERIFY(group["ok"].toBool());auto b=RegionalVariants::duplicate(score,group["group"].toString(),"B",true);QVERIFY(b["ok"].toBool());
            auto note=toChord(score->findCR(Fraction::fromTicks(0),4))->upNote();score->startCmd();note->undoChangeProperty(Pid::VELO_TYPE,int(Note::ValueType::USER_VAL));note->undoChangeProperty(Pid::VELO_OFFSET,112);score->endCmd();
            MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
            std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){stopPlaybackPreview(true);s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(view);
            const int tabs=main->scores().size();setRegionalAuditionEnabled(true);
            for(const auto& variant:{group["variant"].toString(),b["variant"].toString()}){
                  recording->onVelocities.clear();auto result=switchRegionalWriting(view,group["group"].toString(),variant);QVERIFY2(!result.contains("error")&&!result.contains("playbackError"),qPrintable(result.value("error",result.value("playbackError")).toString()));
                  QTRY_VERIFY(sequence->isPlaying());QVERIFY(sequence->getCurTick()<480);for(int i=0;i<8;++i){driver->pulse();QTest::qWait(2);}QVERIFY(sequence->getCurTick()>0);QVERIFY(!recording->onVelocities.isEmpty());if(variant==b["variant"].toString())QVERIFY(recording->onVelocities.contains(112));QCOMPARE(main->scores().size(),tabs);QCOMPARE(main->currentScoreView(),view);
                  }
            setRegionalAuditionEnabled(false);sequence->stopWait();sequence->waitForStoppedRendering();QApplication::processEvents();
            }
