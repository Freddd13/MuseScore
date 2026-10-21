      void referencePanelRepeatedTransport()
            {
            if(!Ms::mscore){
                  QCoreApplication::setOrganizationName("PerformanceRegression");QCoreApplication::setApplicationName("ReferencePanel074");
                  QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,_directory.path());
                  dataPath=_directory.path();MScore::noGui=true;noSeq=true;converterMode=true;initMuseScoreResources();QStringList args;MuseScore::init(args);
                  }
            auto main=Ms::mscore;QVERIFY(!Ms::seq);
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));main->resize(1200,900);main->show();score->select(score->firstSegment(SegmentType::ChordRest)->element(0));
            score->selection().setRangeTicks(Fraction(),score->lastMeasure()->endTick(),0,score->nstaves());score->selection().updateSelectedElements();QVERIFY(score->selection().isRange());
            MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;
            synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
            Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
            std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
            auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
            const auto path=_directory.filePath("reference-panel-074.wav");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
            const int samples=44100*4;QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);
            data.writeRawData("RIFF",4);data<<quint32(36+samples*2);data.writeRawData("WAVEfmt ",8);data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(44100)<<quint32(88200)<<quint16(2)<<quint16(16);
            data.writeRawData("data",4);data<<quint32(samples*2);for(int i=0;i<samples;++i)data<<qint16(8000*std::sin(2*3.141592653589793*440*i/44100)*(.2+.8*std::pow(std::sin(3.141592653589793*i/22050),2)));file.close();
            showReferencePanel(true);QmlPlugin* root=nullptr;
            for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto q=qobject_cast<QmlPlugin*>(view->rootObject()))if(q->menuPath()=="Plugins.Reference Player")root=q;
            QVERIFY(root);QVERIFY(QMetaObject::invokeMethod(root,"setPanelFloating",Q_ARG(bool,true)));QVERIFY(QMetaObject::invokeMethod(root,"resizePanel",Q_ARG(int,920),Q_ARG(int,680)));QTest::qWait(100);
            struct CloseReference {QPointer<QmlPlugin> root;~CloseReference(){if(root)root->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}} closeReference{root};
            QVariant url=QUrl::fromLocalFile(path).toString();QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,url)));
            auto object=[&](const char* name){auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toQObject():value.value<QObject*>();};
            QTRY_VERIFY_WITH_TIMEOUT(object("media")&&object("media")->property("duration").toDouble()>3500,10000);
            auto project=referenceProject();root->setProperty("a",250.);root->setProperty("b",1750.);root->setProperty("jointVolume",0.);
            QVERIFY(QMetaObject::invokeMethod(root,"associate"));QCOMPARE(root->property("selectedAssociation").toInt(),0);root->setProperty("transportTarget",2);
            auto play=root->findChild<QQuickItem*>("referenceJointPlay");QVERIFY(play);
            auto click=[&]{QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,play->mapToScene(QPointF(play->width()/2,play->height()/2)).toPoint());};
            auto why=[&]{return root->property("status").toString()+" / "+project->property("status").toString();};
            for(int mode=0;mode<3;++mode)for(int round=0;round<3;++round){
                  root->setProperty("playbackMode",mode);
                  click();QTRY_VERIFY2_WITH_TIMEOUT(sequence->isPlaying(),qPrintable(why()),10000);QVERIFY(project->property("playing").toBool());
                  QCOMPARE(play->property("text").toString(),QString::fromUtf8("停止"));
                  for(int i=0;i<8;++i){driver->pulse();QTest::qWait(5);}QVERIFY(sequence->getCurTick()>0);QVERIFY(project->property("position").toDouble()>0);
                  QVERIFY(root->property("shownPosition").toDouble()>=250.);QVERIFY(root->property("shownPosition").toDouble()<1750.);
                  click();QTRY_VERIFY(!sequence->isPlaying());QTRY_VERIFY(!project->property("playing").toBool());QCOMPARE(play->property("text").toString(),QString::fromUtf8("播放"));
                  }
            click();QVERIFY(project->property("playing").toBool());click();QTRY_VERIFY(!project->property("playing").toBool());QVERIFY(!sequence->isPlaying());
            click();QTRY_VERIFY2_WITH_TIMEOUT(sequence->isPlaying(),qPrintable(why()),10000);
            for(int i=0;i<200&&(project->property("playing").toBool()||sequence->isPlaying());++i){driver->pulse();QTest::qWait(5);}
            QTRY_VERIFY(!sequence->isPlaying());QTRY_VERIFY(!project->property("playing").toBool());
            click();QTRY_VERIFY2_WITH_TIMEOUT(sequence->isPlaying(),qPrintable(why()),10000);click();QTRY_VERIFY(!sequence->isPlaying());QVERIFY(!project->property("playing").toBool());
            QVERIFY(!recording->onPitches.isEmpty());
            root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path())).filePath("reference-repeat-074.png"));
            showReferencePanel(false);
            }
