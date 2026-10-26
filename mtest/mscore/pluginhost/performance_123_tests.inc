// GPL-2.0-or-later. Repeated real-window actions for the reported slowdown.
#include "mscore/zoombox.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
#include "mscore/performanceeditor/performanceselection.h"
static MasterScore* performanceScore123(const QString& path,int bars)
{
    const auto base=arrangementScore115({QVariantList{72,0,480,0,14},QVariantList{76,480,960,0,18},QVariantList{79,960,1440,0,15},QVariantList{72,1440,1920,0,14},QVariantList{48,0,1920,4,14}});
    auto xml=base;
    xml.replace("<Instrument id=\"piano\">","<Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId>");
    for(int staff:{1,2}){
        const auto begin=xml.indexOf("<Measure>",xml.indexOf(QByteArray("<Staff id=\"")+QByteArray::number(staff)+"\"><Measure>"));
        if(begin<0)return nullptr;const auto end=xml.indexOf("</Measure>",begin)+10;const auto measure=xml.mid(begin,end-begin);QByteArray extra;
        for(int bar=1;bar<bars;++bar)extra+=measure;xml.insert(end,extra);
    }
    QFile file(path);if(!file.open(QIODevice::WriteOnly))return nullptr;file.write(xml);file.close();return Ms::mscore->readScore(path);
}
void TestPluginHost::harmonyCloseAll123()
{
    auto main=Ms::mscore;auto score=performanceScore123(_settings.filePath("harmony-close-123.mscx"),4);QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,920);main->show();
    showHarmonyPanel(true);QPointer<QmlPlugin> panel=harmonyPanel077();QVERIFY(panel);
    struct Cleanup {~Cleanup(){releaseHostPanels();}} cleanup;
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["chordLabels"]=true;config["dualPanel"]=true;
    QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),15000);
    QVERIFY(!main->currentScoreView()->notePreviewMarkerBoxes().isEmpty());
    showHarmonyPanel(false);QTest::qWait(150);
    auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);
    QVERIFY2(!panel->property("surfaceActive").toBool(),"整体关闭后和弦分析必须停止");
    QVERIFY(!observer->enabled());QVERIFY(main->currentScoreView()->notePreviewMarkerBoxes().isEmpty());
    const auto index=observer->indexBuildCount();score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->upNote());main->endCmd();QTest::qWait(200);QCOMPARE(observer->indexBuildCount(),index);
    showHarmonyPanel(true);QTRY_VERIFY_WITH_TIMEOUT(panel->property("surfaceActive").toBool()&&!panel->property("analysisDirty").toBool(),15000);
    QVERIFY(!main->currentScoreView()->notePreviewMarkerBoxes().isEmpty());
    config=panel->property("configuration").value<QJSValue>().toVariant().toMap();QVERIFY(config.value("chordLabels").toBool());
    auto dock=main->findChild<QDockWidget*>("kumoPanel/HarmonyAssistant_MS3");QVERIFY(dock);dock->hide();QVERIFY(QMetaObject::invokeMethod(panel,"closeHarmonyTool"));QVERIFY(!getAction("harmony-assistant")->isChecked());showHarmonyPanel(true);QTRY_VERIFY(observer->enabled());dock->close();QTest::qWait(80);QVERIFY(!observer->enabled());QVERIFY(main->currentScoreView()->notePreviewMarkerBoxes().isEmpty());
}
void TestPluginHost::harmonyVelocityReuse123()
{
    auto main=Ms::mscore;auto score=performanceScore123(_settings.filePath("harmony-velocity-123.mscx"),8);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->show();showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    struct Cleanup {~Cleanup(){releaseHostPanels();}} cleanup;
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["chordLabels"]=true;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),15000);auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);const int builds=observer->indexBuildCount(),completed=panel->property("completedAnalysisCount").toInt(),reused=panel->property("reusedAnalysisCount").toInt();
    auto note=toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->upNote();const auto original=note->veloOffset();const auto fingerprint=observer->contextSnapshot(0,0,8,true,480).value("fingerprint");
    score->startCmd();note->undoChangeProperty(Pid::VELO_OFFSET,25);score->endCmd();main->endCmd();
    QTRY_VERIFY(panel->property("reusedAnalysisCount").toInt()>reused);QCOMPARE(observer->indexBuildCount(),builds);QCOMPARE(panel->property("completedAnalysisCount").toInt(),completed);QCOMPARE(note->veloOffset(),25);
    EditData edit(main->currentScoreView());score->undoRedo(true,&edit);main->endCmd(true);QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool()&&observer->indexBuildCount()>builds,15000);QCOMPARE(note->veloOffset(),original);
    const int beforePitch=panel->property("completedAnalysisCount").toInt();score->startCmd();note->undoChangeProperty(Pid::PITCH,76);note->undoChangeProperty(Pid::TPC1,18);score->endCmd();main->endCmd();QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool()&&panel->property("completedAnalysisCount").toInt()>beforePitch,15000);
    QVERIFY(observer->contextSnapshot(0,0,8,true,480).value("fingerprint")!=fingerprint);
}
void TestPluginHost::harmonyMultiScorePencil123()
{
    auto main=Ms::mscore;main->resize(1280,920);main->show();QVector<ScoreView*> views;
    for(int i=0;i<4;++i){auto score=performanceScore123(_settings.filePath(QString("performance-multiple-123-%1.mscx").arg(i)),qMax(128,qEnvironmentVariableIntValue("PERFORMANCE_TEST_BARS")));QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));auto view=main->currentScoreView();QVERIFY(view);views.append(view);}
    main->setCurrentScoreView(main->scores().indexOf(views[0]->score()->masterScore()));showHarmonyPanel(true);QPointer<QmlPlugin> panel=harmonyPanel077();QVERIFY(panel);
    struct Cleanup {~Cleanup(){releaseHostPanels();}} cleanup;
    panel->setProperty("analysisReuseEnabled",!qEnvironmentVariableIsSet("PERFORMANCE_DISABLE_REUSE"));
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["chordLabels"]=true;config["allColor"]=true;config["coloring"]=true;config["dualPanel"]=false;
    QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));
    main->showPerformanceEditor(true);auto editor=main->performanceEditor();QVERIFY(editor);
    auto parameter=editor->findChild<QComboBox*>("performanceParameter"),tool=editor->findChild<QComboBox*>("performanceTool"),scope=editor->findChild<QComboBox*>("performanceScope");QVERIFY(parameter&&tool&&scope);
    parameter->setCurrentIndex(0);tool->setCurrentIndex(1);scope->setCurrentIndex(2);
    auto canvas=editor->findChild<QWidget*>("performanceParameterCanvas");QVERIFY(canvas);QVariantList samples;
    for(int round=0;round<12;++round){
        QElapsedTimer timer;timer.start();main->setCurrentScoreView(main->scores().indexOf(views[round%views.size()]->score()->masterScore()));QVERIFY(views[round%views.size()]->isVisible());views[round%views.size()]->score()->deselectAll();main->endCmd();
        QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),20000);QTest::qWait(60);editor->fit(false);QTest::qWait(100);
        tool->setCurrentIndex(1);
        {QList<Note*> notes;auto score=views[round%views.size()]->score();for(auto s=score->firstSegment(SegmentType::ChordRest);s;s=s->next1(SegmentType::ChordRest))for(int t:{0,4})if(auto e=s->element(t))if(e->isChord())for(auto n:toChord(e)->notes())notes.append(n);QVERIFY(PerformanceSelection::apply(score,notes,notes.front()));editor->selectionChanged();}


        if(!round)main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("performance-123-before.png"));
        const auto switchMs=timer.elapsed();const int before=views[round%views.size()]->score()->undoStack()->getCurIdx();
        const bool reverse=(round/4)%2;
        const QPoint from(100,canvas->height()*(reverse?2:1)/3),until(canvas->width()-12,canvas->height()*(reverse?1:2)/3);
        main->activateWindow();QTest::qWait(60);
        timer.restart();QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,from);
        QCOMPARE(QWidget::mouseGrabber(),canvas);
        for(int step=1;step<=16;++step){const auto point=from+(until-from)*step/16;QMouseEvent event(QEvent::MouseMove,point,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&event);QTest::qWait(17);}
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,until);QTRY_VERIFY_WITH_TIMEOUT(!editor->hasPending(),10000);
        const auto pencilMs=timer.elapsed();QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),20000);const auto totalEditMs=timer.elapsed();
        const auto after=views[round%views.size()]->score()->undoStack()->getCurIdx();
        QVERIFY2(after>before,"铅笔必须实际修改力度，才能记录连续编辑耗时");
        timer.restart();for(int zoom=0;zoom<4;++zoom){auto view=views[round%views.size()];QPointF point(view->width()/2.,view->height()/2.);QWheelEvent wheel(point,view->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,zoom%2?-120:120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(view,&wheel);QApplication::processEvents();}
        const auto zoomMs=timer.elapsed();QTest::qWait(160);timer.restart();
        for(int zoom=0;zoom<4;++zoom){auto view=views[round%views.size()];QPointF point(view->width()/2.,view->height()/2.);QWheelEvent wheel(point,view->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,zoom%2?-120:120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(view,&wheel);QApplication::processEvents();}
        const auto settledZoomMs=timer.elapsed();auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);
        qulonglong memory=0;
#ifdef Q_OS_WIN
        {PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);using ReadMemory=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);auto read=reinterpret_cast<ReadMemory>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));if(read&&read(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),sizeof(counters)))memory=counters.PrivateUsage;}
#endif
        samples.append(QVariantMap{{"indexMilliseconds",observer->indexBuildMilliseconds()},{"privateBytes",memory},{"totalEditMilliseconds",totalEditMs},{"analysisCount",panel->property("completedAnalysisCount")},{"reusedCount",panel->property("reusedAnalysisCount")},{"round",round},{"switchMilliseconds",switchMs},{"pencilMilliseconds",pencilMs},{"zoomMilliseconds",zoomMs},{"settledZoomMilliseconds",settledZoomMs},{"undoAdded",after-before},{"indexBuilds",observer->indexBuildCount()},{"markers",main->currentScoreView()->notePreviewMarkerBoxes().size()}});
        qInfo()<<"Multiple123"<<samples.back();
    }
    const auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QFile report(out.filePath("multiscore-pencil-123.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(samples).toJson());
}
