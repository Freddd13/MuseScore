// GPL-2.0-or-later. Measure the GUI while the sequencer is actually producing sound.
namespace {
class PlaybackGuiProbe125 : public QObject {
    bool nested=false;
    QMap<QString,QVector<double>> timings;
    bool eventFilter(QObject* object,QEvent* event) override {
        if(nested || (event->type()!=QEvent::Paint && event->type()!=QEvent::Timer && event->type()!=QEvent::MetaCall && event->type()!=QEvent::UpdateRequest && event->type()!=QEvent::Wheel && event->type()!=QEvent::MouseMove))return false;
        const QString name=QString::fromLatin1(object->metaObject()->className())+":"+object->objectName()+":"+QString::number(event->type());
        QElapsedTimer watch;watch.start();nested=true;QCoreApplication::sendEvent(object,event);nested=false;
        const double ms=watch.nsecsElapsed()/1.e6;timings[name].append(ms);if(ms>100)qInfo()<<"Long GUI event"<<name<<ms;return true;
    }
public:
    PlaybackGuiProbe125(){qApp->installEventFilter(this);}
    ~PlaybackGuiProbe125(){qApp->removeEventFilter(this);}
    QVariantList report() const {
        QVariantList rows;
        for(auto it=timings.cbegin();it!=timings.cend();++it){double total=0;for(double value:it.value())total+=value;
            rows.append(QVariantMap{{"name",it.key()},{"count",it.value().size()},{"totalMilliseconds",total},{"p95Milliseconds",sourcePercentile(it.value(),.95)},{"maxMilliseconds",sourcePercentile(it.value(),1)}});}
        std::sort(rows.begin(),rows.end(),[](const QVariant& a,const QVariant& b){return a.toMap()["totalMilliseconds"].toDouble()>b.toMap()["totalMilliseconds"].toDouble();});return rows;
    }
};
}
void TestPluginHost::playbackGuiLatency125(){
    SourceTimerResolution timerResolution;auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    QVector<MasterScore*> scores;
    struct Close {QVector<MasterScore*>& scores;~Close(){releaseHostPanels();for(auto score:scores){score->undoStack()->setClean();Ms::mscore->closeScore(score);}}} close{scores};
    const auto original=qEnvironmentVariable("P2_GUI_SCORE");
    QByteArray originalHash;
    if(!original.isEmpty()){QFile file(original);QVERIFY(file.open(QIODevice::ReadOnly));originalHash=QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);}
    for(int i=0;i<4;++i){
        MasterScore* score=nullptr;
        if(original.isEmpty())score=performanceScore123(_settings.filePath(QString("playback-gui-125-%1.mscx").arg(i)),600);
        else {const auto copy=_settings.filePath(QString("playback-real-125-%1.mscz").arg(i));QVERIFY(QFile::copy(original,copy));score=main->readScore(copy);}
        QVERIFY(score);scores.append(score);main->setCurrentScoreView(main->appendScore(score));
    }
    main->resize(1280,920);main->show();main->activateWindow();
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();synth->setEffect(0,0);synth->setEffect(1,0);
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());QScopedValueRollback<bool> sequencing(noSeq,false);
    auto sequence=new Seq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){s->stopWait();s->synchronizeStoppedGui();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new SourcePlaybackDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    main->showPerformanceEditor(true);auto editor=main->performanceEditor();QVERIFY(editor);
    auto wheel=editor->findChild<QToolButton*>("performanceWheelVelocity");QVERIFY(wheel);
    auto canvas=editor->findChild<QWidget*>("performanceParameterCanvas");QVERIFY(canvas);
    QVariantList results;
    const int seconds=qMax(8,qEnvironmentVariableIntValue("P2_GUI_PLAY_SECONDS"));
    for(int mode=0;mode<4;++mode){
        if(qEnvironmentVariableIsSet("P2_GUI_MODE")&&mode!=qEnvironmentVariableIntValue("P2_GUI_MODE"))continue;
        main->setCurrentScoreView(main->scores().indexOf(scores[mode]));auto score=scores[mode];auto view=main->currentScoreView();
        main->showPerformanceEditor(true);editor->setScoreBandEnabled(mode==3);wheel->setChecked(mode>=2);
        editor->findChild<QComboBox*>("performanceParameter")->setCurrentIndex(0);editor->findChild<QComboBox*>("performanceScope")->setCurrentIndex(2);
        editor->fit(false);QTest::qWait(100);
        if(mode>=1){
            editor->findChild<QComboBox*>("performanceTool")->setCurrentIndex(1);
            QList<Note*> selected;for(auto s=score->firstSegment(SegmentType::ChordRest);s;s=s->next1(SegmentType::ChordRest))for(int track:{0,4})if(auto e=s->element(track))if(e->isChord())for(auto n:toChord(e)->notes())selected.append(n);
            QVERIFY(PerformanceSelection::apply(score,selected,selected.front()));editor->selectionChanged();
            QPoint from(95,canvas->height()/3),until(canvas->width()-15,canvas->height()*2/3);
            QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,from);for(int step=1;step<=8;++step){QMouseEvent move(QEvent::MouseMove,from+(until-from)*step/8,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&move);QTest::qWait(17);}QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,until);QTRY_VERIFY_WITH_TIMEOUT(!editor->hasPending(),10000);
            if(!qEnvironmentVariableIsSet("P2_GUI_KEEP_SELECTION")){score->deselectAll();main->endCmd();editor->selectionChanged();}
        }
        if(mode==3){showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["chordLabels"]=true;config["coloring"]=true;config["allColor"]=true;config["dualPanel"]=false;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),20000);}
        if(mode==0)main->showPerformanceEditor(false);
        score->setPlayPos(Fraction());driver->resetMetrics();getAction("play")->trigger();QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);QTest::qWait(150);
        QVector<double> heartbeat,zooms;QElapsedTimer clock;clock.start();qint64 previous=clock.nsecsElapsed();QTimer ticker;ticker.setTimerType(Qt::PreciseTimer);ticker.setInterval(16);
        QObject::connect(&ticker,&QTimer::timeout,main,[&]{const auto now=clock.nsecsElapsed();heartbeat.append((now-previous)/1.e6);previous=now;});
        QVariantList events;
        {PlaybackGuiProbe125 probe;ticker.start();
            for(int second=0;second<seconds;++second){
                QTest::qWait(1000);
                if(second%3==2){QElapsedTimer watch;watch.start();for(int step=0;step<4;++step){QPointF p(view->width()/2.,view->height()/2.);QWheelEvent event(p,view->mapToGlobal(p.toPoint()),QPoint(),QPoint(0,step%2?-120:120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(view,&event);QApplication::processEvents();}zooms.append(watch.nsecsElapsed()/1.e6);}
                if(mode>=2 && second%3==1){
                    Note* target=nullptr;
                    for(auto segment=score->firstSegment(SegmentType::ChordRest);segment&&!target;segment=segment->next1(SegmentType::ChordRest))for(int track=0;track<score->nstaves()*VOICES&&!target;++track)if(auto e=segment->element(track))if(e->isChord())for(auto n:toChord(e)->notes())if(view->rect().contains(view->matrix().map(n->canvasBoundingRect().center()).toPoint())){target=n;break;}
                    QVERIFY(target);auto p=view->matrix().map(target->canvasBoundingRect().center());QWheelEvent event(p,view->mapToGlobal(p.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(view,&event);
                }
            }
            ticker.stop();events=probe.report();
        }
        sequence->stopWait();QVERIFY(!score->isPlaying());sequence->waitForStoppedRendering();QTRY_VERIFY_WITH_TIMEOUT(!editor->hasPending(),10000);
        main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath(QString("playback-gui-125-%1.png").arg(mode)));
        QVariantList zoomValues;for(double value:zooms)zoomValues.append(value);
        auto row=driver->metrics();row["mode"]=mode;row["seconds"]=clock.elapsed()/1000.;row["guiMedianMilliseconds"]=sourcePercentile(heartbeat,.5);row["guiP95Milliseconds"]=sourcePercentile(heartbeat,.95);row["guiMaximumMilliseconds"]=sourcePercentile(heartbeat,1);row["fourZoomMilliseconds"]=zoomValues;row["events"]=events;row["privateBytes"]=sourcePrivateBytes();results.append(row);
        qInfo()<<"Playback GUI"<<mode<<"p95"<<row["guiP95Milliseconds"]<<"maximum"<<row["guiMaximumMilliseconds"]<<"zooms"<<zooms;
        QFile report(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("playback-gui-125.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(results).toJson());
        if(mode==3)showHarmonyPanel(false);
    }
    if(!original.isEmpty()){QFile file(original);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256),originalHash);}
}
