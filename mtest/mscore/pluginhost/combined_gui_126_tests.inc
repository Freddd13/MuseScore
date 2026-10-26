// GPL-2.0-or-later. Keep the actual media, spectrum, editor and score paths active together.
#include <QScreen>
namespace {
class CombinedPaintCounter126 : public QObject {
      QMap<QObject*,int> counts;
      bool eventFilter(QObject* object,QEvent* event) override {
            if(event->type()==QEvent::Paint)++counts[object];return false;
            }
public:
      explicit CombinedPaintCounter126(const QList<QWidget*>& widgets) {
            for(auto widget:widgets)widget->installEventFilter(this);
            }
      int count(QObject* object)const{return counts.value(object);}
      };
}
void TestPluginHost::combinedPlaybackGui126()
{
    SourceTimerResolution timerResolution;
    auto main = Ms::mscore;
    QVERIFY(main && !Ms::seq);
    const bool pencil=!qEnvironmentVariableIsSet("P2_COMBINED_NO_PENCIL");
    const bool useHarmony=!qEnvironmentVariableIsSet("P2_COMBINED_NO_HARMONY");
    const auto entryBytes=sourcePrivateBytes();
    const auto audioPath = qEnvironmentVariable("P2_COMBINED_AUDIO");
    QVERIFY(QFileInfo::exists(audioPath));
    const auto original = qEnvironmentVariable("P2_GUI_SCORE");
    QByteArray originalHash;
    if (!original.isEmpty()) { QFile file(original); QVERIFY(file.open(QIODevice::ReadOnly)); originalHash=QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256); }
    QVector<MasterScore*> scores;
    struct CloseScores { QVector<MasterScore*>& scores; ~CloseScores() { releaseHostPanels(); for(auto score:scores) { score->undoStack()->setClean(); Ms::mscore->closeScore(score); } } } close{scores};
    for (int index=0;index<4;++index) {
        MasterScore* score=nullptr;
        if (original.isEmpty()) score=performanceScore123(_settings.filePath(QString("combined-126-%1.mscx").arg(index)),600);
        else { auto copy=_settings.filePath(QString("combined-126-%1.mscz").arg(index)); QVERIFY(QFile::copy(original,copy)); score=main->readScore(copy); }
        QVERIFY(score); scores.append(score); main->setCurrentScoreView(main->appendScore(score));
    }
    main->resize(1280,920); main->show(); main->activateWindow();
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
    synth->setSampleRate(44100); synth->init(); synth->setEffect(0,0); synth->setEffect(1,0);
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());
    QScopedValueRollback<bool> sequencing(noSeq,false);
    auto sequence=new Seq; Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s) { workflowContext()->finishPlaybackSession(); s->stopWait(); s->synchronizeStoppedGui(); s->waitForStoppedRendering(); s->setScoreView(nullptr); delete s; Ms::seq=nullptr; });
    auto driver=new SourcePlaybackDriver(sequence);
    sequence->setDriver(driver); sequence->setMasterSynthesizer(synth.get()); QVERIFY(sequence->init()); sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted())); QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    main->showPerformanceEditor(true);
    auto editor=main->performanceEditor(); QVERIFY(editor);
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs"); QVERIFY(tabs);
    auto pane=editor->findChild<QWidget*>("transcriptionSpectrumPane"); QVERIFY(pane);
    auto media=referenceProject(); QVERIFY(media->property("available").toBool());
    media->setProperty("audioStopsScore",false); media->setProperty("scoreStopsAudio",false);
    showReferencePanel(true); QPointer<QmlPlugin> reference;
    for(auto window:QGuiApplication::allWindows()) if(auto view=qobject_cast<QQuickView*>(window)) if(auto root=qobject_cast<QmlPlugin*>(view->rootObject())) if(root->menuPath()=="Plugins.Reference Player") reference=root;
    QVERIFY(reference);
    auto referenceDock=main->findChild<QDockWidget*>("kumoPanel/ReferencePlayer_MS3"); QVERIFY(referenceDock);
    auto editorDock=main->findChild<QDockWidget*>("performanceEditorDock"); QVERIFY(editorDock);
    const auto available=QGuiApplication::primaryScreen()->availableGeometry();
    referenceDock->setFloating(true);referenceDock->resize(560,680);
    referenceDock->move(available.right()-referenceDock->width(),available.top()+30);referenceDock->show();
    editorDock->raise();main->resizeDocks({editorDock},{590},Qt::Vertical);
    QPointer<QmlPlugin> harmony;
    if(useHarmony){
        showHarmonyPanel(true);harmony=harmonyPanel077();QVERIFY(harmony);
        auto config=harmony->property("configuration").value<QJSValue>().toVariant().toMap();
        config["chordLabels"]=true;config["coloring"]=true;config["allColor"]=true;config["dualPanel"]=false;
        QVERIFY(QMetaObject::invokeMethod(harmony,"applyPreferences",Q_ARG(QVariant,config)));
    }
    auto canvas=editor->findChild<QWidget*>("performanceParameterCanvas"); QVERIFY(canvas);
    auto wheel=editor->findChild<QToolButton*>("performanceWheelVelocity"); QVERIFY(wheel); wheel->setChecked(true);
    auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QVariantList rows;
    const int rounds=qMax(4,qEnvironmentVariableIntValue("P2_COMBINED_ROUNDS"));
    const int seconds=qMax(6,qEnvironmentVariableIntValue("P2_COMBINED_SECONDS"));
    for(int round=0;round<rounds;++round) {
        QElapsedTimer preparation; preparation.start();
        auto score=scores[round%4]; main->setCurrentScoreView(main->scores().indexOf(score));
        main->showPerformanceEditor(true);editorDock->show();editorDock->raise();
        tabs->setCurrentIndex(0);
        editor->findChild<QComboBox*>("performanceScope")->setCurrentIndex(2);
        editor->findChild<QComboBox*>("performanceParameter")->setCurrentIndex(0);
        editor->findChild<QComboBox*>("performanceTool")->setCurrentIndex(1);
        editor->fit(false); QTest::qWait(100);
        QList<Note*> selected;
        for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))
            for(int track=0;track<score->nstaves()*VOICES;++track) if(auto element=segment->element(track)) if(element->isChord()) for(auto note:toChord(element)->notes()) selected.append(note);
        QVERIFY(!selected.isEmpty()); QVERIFY(PerformanceSelection::apply(score,selected,selected.front())); editor->selectionChanged();
        const bool reverse=(round/4)%2;
        QPoint from(95,canvas->height()*(reverse?2:1)/3),until(canvas->width()-15,canvas->height()*(reverse?1:2)/3);
        main->raise(); main->activateWindow(); QTest::qWait(40);
        if(pencil){
            QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,from);
            for(int step=1;step<=8;++step) { QMouseEvent move(QEvent::MouseMove,from+(until-from)*step/8,Qt::NoButton,Qt::LeftButton,Qt::NoModifier); QCoreApplication::sendEvent(canvas,&move); QTest::qWait(17); }
            QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,until);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!editor->hasPending(),10000);
        const int pencilChildren=pencil&&score->undoStack()->last()?score->undoStack()->last()->childCount():0;
        if(round<4) {
            QVariantList tracks;
            for(int track=0;track<8;++track) {
                const QString id=QString("combined-%1-%2").arg(round%4).arg(track);
                QVariantMap association{{"id",id+"-clip"},{"name","Playback range"},{"start",0},{"end",19200},{"from",0.},{"until",score->tempomap()->tick2time(19200)*1000.},{"active",true}};
                tracks.append(QVariantMap{{"id",id},{"name",QString("Audio %1").arg(track+1)},{"path",audioPath},{"duration",240000.},{"speed",1.},{"volume",0.},{"participate",true},{"onlyRange",false},{"selectedAssociation",0},{"associations",QVariantList{association}}});
            }
            const QVariant project=QVariantMap{{"tracks",tracks},{"selectedTrack",0}};
            QVERIFY(QMetaObject::invokeMethod(reference,"restoreTracks",Q_ARG(QVariant,project),Q_ARG(QVariant,QVariant(QString()))));
        }
        QTRY_VERIFY_WITH_TIMEOUT(reference->property("audioReady").toBool(),20000);
        reference->setProperty("jointVolume",0.);
        tabs->setCurrentIndex(2); QTRY_VERIFY(pane->isVisible());
        auto display=pane->findChild<QComboBox*>("transcriptionDisplay"); QVERIFY(display); display->setCurrentIndex(round%2);
        auto clips=pane->findChild<QComboBox*>("transcriptionClips"); QVERIFY(clips); QTRY_VERIFY(clips->count()>0); clips->setCurrentIndex(0);
        auto parameterAction=editor->findChild<QAction*>("editorArea/show/performanceParametersArea");QVERIFY(parameterAction);
        if(!parameterAction->isChecked())parameterAction->trigger();
        QTRY_VERIFY(canvas->isVisible());
        if(!round){
            tabs->setCurrentIndex(0);QVERIFY(canvas->isVisible());tabs->setCurrentIndex(2);QVERIFY(canvas->isVisible());
            parameterAction->trigger();QVERIFY(!canvas->isVisible());tabs->setCurrentIndex(0);QVERIFY(canvas->isVisible());
            tabs->setCurrentIndex(2);QVERIFY(!canvas->isVisible());parameterAction->trigger();QVERIFY(canvas->isVisible());
        }
        auto spectrumDock=main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");
        if(round%4==0){
            editor->findChild<QAction*>("editorArea/float/transcriptionSpectrumPane")->trigger();
            spectrumDock=main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");QVERIFY(spectrumDock);
            spectrumDock->resize(760,760);spectrumDock->move(available.left()+10,available.top()+10);spectrumDock->show();
        }else if(spectrumDock&&spectrumDock->isVisible()){
            editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();tabs->setCurrentIndex(2);
        }
        auto session=pane->findChild<QObject*>("transcriptionSpectrumSession"); QVERIFY(session);
        auto notes=editor->findChild<QWidget*>("performanceNoteCanvas"); QVERIFY(notes);
        auto spectrumCanvas=pane->findChild<QWidget*>("transcriptionSpectrumCanvas"); QVERIFY(spectrumCanvas);
        QSplitter* splitter=nullptr;for(auto parent=canvas->parentWidget();parent&&!splitter;parent=parent->parentWidget())splitter=qobject_cast<QSplitter*>(parent);
        QVERIFY(splitter);QList<int> sizes;
        for(int index=0;index<splitter->count();++index){auto widget=splitter->widget(index);sizes.append(widget==pane?210:widget->isAncestorOf(canvas)?120:widget->isAncestorOf(notes)?150:0);}
        splitter->setSizes(sizes);editorDock->show();editorDock->raise();QTest::qWait(60);
        qInfo()<<"Combined visible"<<splitter->sizes()<<canvas->geometry()<<notes->geometry()<<pane->geometry();
        QTRY_VERIFY(!canvas->visibleRegion().isEmpty()&&!notes->visibleRegion().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool()&&!qvariant_cast<QImage>(session->property("image")).isNull(),20000);
        QTRY_VERIFY_WITH_TIMEOUT(!useHarmony||!harmony->property("analysisDirty").toBool(),20000);
        const auto preparedMs=preparation.elapsed();
        QSignalSpy spectrumChanges(session,SIGNAL(changed())); QSignalSpy mediaChanges(media,SIGNAL(changed()));
        const int mode=round%3;
        score->setPlayPos(Fraction()); driver->resetMetrics();
        if(mode==0) { getAction("play")->trigger(); QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000); }
        else if(mode==1) { reference->setProperty("transportTarget",0); QVERIFY(QMetaObject::invokeMethod(reference,"toggleReferencePlayback")); QTRY_VERIFY_WITH_TIMEOUT(media->property("playing").toBool(),10000); }
        else { QVERIFY(QMetaObject::invokeMethod(reference,"startJoint")); QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&media->property("playing").toBool(),10000); }
        QVector<double> gaps,zooms; QElapsedTimer clock; clock.start(); qint64 previous=clock.nsecsElapsed(); QTimer ticker; ticker.setTimerType(Qt::PreciseTimer); ticker.setInterval(16);
        QObject::connect(&ticker,&QTimer::timeout,main,[&] { const auto now=clock.nsecsElapsed(); gaps.append((now-previous)/1.e6); previous=now; });
        QVariantList events;
        int notePaints=0,parameterPaints=0,spectrumPaints=0;
        { CombinedPaintCounter126 painted({notes,canvas,spectrumCanvas});PlaybackGuiProbe125 probe; ticker.start();
            for(int second=0;second<seconds;++second) {
                QTest::qWait(1000);
                if(second%3==2) {
                    QElapsedTimer timer; timer.start();
                    for(int step=0;step<4;++step) { auto view=main->currentScoreView(); QPointF point(view->width()/2.,view->height()/2.); QWheelEvent event(point,view->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,step%2?-120:120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false); QCoreApplication::sendEvent(view,&event); QApplication::processEvents(); }
                    zooms.append(timer.nsecsElapsed()/1.e6);
                    auto scroll=pane->findChild<QScrollBar*>("spectrumTimeScroll"); QVERIFY(scroll); scroll->setValue(qMin(scroll->maximum(),scroll->value()+7000));
                }
            }
            ticker.stop(); events=probe.report();notePaints=painted.count(notes);parameterPaints=painted.count(canvas);spectrumPaints=painted.count(spectrumCanvas);
        }
        QVERIFY(notePaints>0&&parameterPaints>0);if(display->currentIndex()==0)QVERIFY(spectrumPaints>0);
        const auto mediaDiagnostics=media->property("diagnostics").toMap();
        if(mode!=0) { QVERIFY2(mediaDiagnostics.value("outputFrames").toLongLong()>4096,qPrintable(media->property("status").toString())); if(mode==2)QCOMPARE(mediaDiagnostics.value("audioTracks").toInt(),8); }
        QVERIFY(QMetaObject::invokeMethod(reference,"stopJoint")); sequence->stopWait(); sequence->waitForStoppedRendering();
        QTRY_VERIFY(!sequence->isPlaying()&&!media->property("playing").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!editor->hasPending()&&!session->property("busy").toBool(),15000);
        QVariantList zoomValues; for(double value:zooms)zoomValues.append(value);
        auto row=driver->metrics(); row["round"]=round; row["mode"]=mode; row["overlay"]=display->currentIndex()==1; row["preparationMilliseconds"]=preparedMs; row["guiP95Milliseconds"]=sourcePercentile(gaps,.95); row["guiMaximumMilliseconds"]=sourcePercentile(gaps,1.); row["fourZoomMilliseconds"]=zoomValues; row["events"]=events; row["spectrumChanges"]=spectrumChanges.count(); row["mediaChanges"]=mediaChanges.count(); row["spectrumInfo"]=session->property("info"); row["cache"]=session->property("cacheOptions"); row["mediaDiagnostics"]=mediaDiagnostics; row["privateBytes"]=sourcePrivateBytes(); row["windows"]=QGuiApplication::allWindows().size();row["notePaints"]=notePaints;row["parameterPaints"]=parameterPaints;row["spectrumPaints"]=spectrumPaints; rows.append(row);
        int playEvents=0;for(auto note:selected)playEvents+=note->playEvents().size();row["playEvents"]=playEvents;row["notes"]=selected.size();row["undoMacros"]=score->undoStack()->getCurIdx();row["pencilUndoChildren"]=pencilChildren;row["pencil"]=pencil;row["harmony"]=useHarmony;rows.last()=row;
        qInfo()<<"Combined GUI"<<round<<"mode"<<mode<<"p95"<<row["guiP95Milliseconds"]<<"maximum"<<row["guiMaximumMilliseconds"]<<"zooms"<<zooms;
        QFile report(output.filePath("combined-gui-126.json")); QVERIFY(report.open(QIODevice::WriteOnly)); report.write(QJsonDocument::fromVariant(rows).toJson());
        if(!round||round==rounds-1) {QVERIFY(main->grab().save(output.filePath(QString("combined-gui-126-%1.png").arg(round))));QVERIFY(reference->window()->grabWindow().save(output.filePath(QString("combined-reference-126-%1.png").arg(round))));}
    }
    showHarmonyPanel(false); QTRY_VERIFY(main->currentScoreView()->notePreviewMarkerBoxes().isEmpty());
    if(pane->property("kumoDetachedArea").toBool())editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();
    tabs->setCurrentIndex(0); QTest::qWait(80); QVERIFY(!pane->isVisible()); QVERIFY(!media->property("playing").toBool());
    if(!original.isEmpty()) { QFile file(original); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256),originalHash); }
    QVariantMap teardown{{"entryPrivateBytes",entryBytes},{"beforeGcPrivateBytes",sourcePrivateBytes()}};
    if(reference)if(auto engine=qmlEngine(reference))engine->collectGarbage();
    if(harmony)if(auto engine=qmlEngine(harmony))engine->collectGarbage();
    QTest::qWait(80);teardown["afterGcPrivateBytes"]=sourcePrivateBytes();
    releaseHostPanels();sequence->setScoreView(nullptr);
    for(auto score:scores){score->undoStack()->setClean();main->closeScore(score);}scores.clear();
    QTest::qWait(150);teardown["afterClosePrivateBytes"]=sourcePrivateBytes();
    QFile report(output.filePath("combined-teardown-126.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(teardown).toJson());
}

#include "mscore/referenceaudio/projectfactory.h"
#include <QPluginLoader>
#include "mscore/workflow/temporaryscore.h"
void TestPluginHost::workflowCopyLifetime126()
{
    auto main=Ms::mscore;QVERIFY(main);
    TemporaryScore original(performanceScore123(_settings.filePath("workflow-copy-126.mscx"),4));QVERIFY(original);
    const auto notes=toChord(original->firstSegment(SegmentType::ChordRest)->element(0))->notes().size();
    QVariantList memory;
    for(int round=0;round<96;++round) {
        TemporaryScore copy(original->clone());QVERIFY(copy);
        auto chord=toChord(copy->firstSegment(SegmentType::ChordRest)->element(0));
        auto added=new Note(copy.get());added->setParent(chord);added->setTrack(chord->track());added->setPitch(79);added->setTpcFromPitch();
        copy->startCmd();copy->undoAddElement(added);copy->endCmd();
        if(round%2)copy->undoStack()->undo(nullptr);
        else {copy->startCmd();copy->undoRemoveElement(chord->notes().front());copy->endCmd();}
        copy.reset();
        QCOMPARE(toChord(original->firstSegment(SegmentType::ChordRest)->element(0))->notes().size(),notes);
        QTest::qWait(2);
        if(round%16==15)memory.append(QVariantMap{{"copies",round+1},{"privateBytes",sourcePrivateBytes()}});
    }
    QVERIFY(memory.last().toMap().value("privateBytes").toULongLong()<memory.front().toMap().value("privateBytes").toULongLong()+2*1024*1024);
    qInfo()<<"Workflow copy lifetime"<<memory;
    QFile report(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("workflow-copy-memory-126.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(memory).toJson());
}
void TestPluginHost::harmonyTemporaryMemory126()
{
    auto main=Ms::mscore;QVERIFY(main);
    auto score=performanceScore123(_settings.filePath("harmony-memory-126.mscx"),4);QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->show();
    struct Close {MasterScore* score;~Close(){score->undoStack()->setClean();Ms::mscore->closeScore(score);}} close{score};
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    auto note=toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->upNote();
    auto render=[&](int index){
        auto image=observer.harmonyLabel(index%2?"C/E":"G/B",QString("I%1").arg(index%19),{{"gap",index%25},{"pixels",18+index%5}});
        return !image.value("source").toString().isEmpty();
    };
    for(int index=0;index<64;++index)QVERIFY(render(index));
    const auto before=sourcePrivateBytes();
    for(int index=64;index<640;++index)QVERIFY(render(index));
    const auto afterLabels=sourcePrivateBytes();
    QVariantMap marker{{"tick",0},{"track",0},{"index",0},{"pitch",note->pitch()},{"tpc",note->tpc1()},
        {"chordTick",0},{"chordUntil",480},{"chord","C/E"},{"degree","I⁶"},{"onTop",true},{"avoidScoreContent",false}};
    for(int index=0;index<256;++index)observer.setScorePreview({marker});
    observer.setScorePreview({});
    const auto afterPreview=sourcePrivateBytes();
    QVariantMap result{{"before",before},{"afterLabels",afterLabels},{"afterPreview",afterPreview},{"movementsBytes",int(sizeof(Movements))},{"labels",576},{"previews",256}};
    QFile report(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("harmony-memory-126.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(result).toJson());
    qInfo()<<"Harmony temporary memory"<<result;
    QVERIFY2(afterLabels<before+8*1024*1024,"反复画和弦后有临时乐谱资料未释放");
    QVERIFY2(afterPreview<afterLabels+4*1024*1024,"反复建立谱面和弦后有临时乐谱资料未释放");
}
void TestPluginHost::referenceOutputCancel126()
{
    SourceTimerResolution timerResolution;
    QPluginLoader module(QDir(QCoreApplication::applicationDirPath()).filePath("kumo_referenceaudio"));
    module.setLoadHints(QLibrary::PreventUnloadHint);
    auto factory=qobject_cast<KumoMediaProjectFactory*>(module.instance()); QVERIFY2(factory,qPrintable(module.errorString()));
    const auto path=qEnvironmentVariable("P2_COMBINED_AUDIO"); QVERIFY(QFileInfo::exists(path));
    std::unique_ptr<QObject> project(factory->createProject(nullptr,KumoReferenceHost{}));
    const QVariantMap piece{{"from",0.},{"until",6000.},{"at",0.},{"duration",6000.}};
    const QVariantMap clip{{"id","cancel-audio"},{"path",path},{"pieces",QVariantList{piece}}};
    const QVariantMap plan{{"clips",QVariantList{clip}},{"joint",false},{"volume",0.}};
    auto start=[&]{QVariantMap result;return QMetaObject::invokeMethod(project.get(),"start",Q_RETURN_ARG(QVariantMap,result),Q_ARG(QVariantMap,plan))&&!result.contains("error");};
    auto stop=[&]{return QMetaObject::invokeMethod(project.get(),"stop");};
    QVariantList rows;
    for(int round=0;round<6;++round) {
        QVERIFY(start()); QTest::qWait(45);
        QElapsedTimer timer;timer.start(); QVERIFY(stop()); const double stopped=timer.nsecsElapsed()/1.e6;
        QVERIFY(!project->property("playing").toBool()); QVERIFY2(stopped<80,"停止等待了声音设备初始化");
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(project->property("diagnostics").toMap().value("outputStarted").toBool(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(project->property("diagnostics").toMap().value("outputFrames").toLongLong()>4096,5000);
        auto row=project->property("diagnostics").toMap(); row["round"]=round; row["stopMilliseconds"]=stopped; rows.append(row);
        QVERIFY(stop()); QTest::qWait(80); QVERIFY(!project->property("playing").toBool());
        QCOMPARE(project->property("diagnostics").toMap().size(),0);
    }
    QVERIFY(start()); QTest::qWait(35); project.reset();
    QTest::qWait(80);
    QFile report(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("reference-output-cancel-126.json")); QVERIFY(report.open(QIODevice::WriteOnly)); report.write(QJsonDocument::fromVariant(rows).toJson());
}
