// GPL-2.0-or-later. Recording belongs to its score through stopping and writing.
#include "mscore/workflow/midiinput.h"
void TestPluginHost::expressionRecordingAcrossScores_data()
{
    QTest::addColumn<QString>("phase");
    for(const auto& phase:{"recording","stopping","saving","reload"})QTest::newRow(phase)<<QString::fromLatin1(phase);
}
void TestPluginHost::expressionRecordingAcrossScores()
{
    QFETCH(QString,phase);auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);
    QSettings().setValue("workflow/recordingDirectory",_settings.filePath("recording-switch-"+phase));
    auto first=performanceScore123(_settings.filePath("recording-first-"+phase+".mscx"),4);
    auto second=performanceScore123(_settings.filePath("recording-second-"+phase+".mscx"),4);QVERIFY(first&&second);
    const auto close=qScopeGuard([&]{releaseHostPanels();for(auto score:{first,second}){score->undoStack()->setClean();main->closeScore(score);}});
    main->setCurrentScoreView(main->appendScore(first));main->appendScore(second);main->resize(1280,900);main->show();showExpressionPanel();
    auto page=main->performanceEditor()->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(page);
    auto root=qobject_cast<QmlPlugin*>(page->property("root").value<QObject*>());QVERIFY(root);
    auto map=[&](const char* name){const auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toMap():value.toMap();};
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    auto capture=qobject_cast<MidiCaptureSession*>(root->property("captureSession").value<QObject*>());QVERIFY(capture);
    auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);
    const auto target=observer->performanceRange(0,first->lastMeasure()->endTick().ticks(),0,first->ntracks(),0,32,observer->analysisRevision()).value("notes").toList();QVERIFY(target.size()>=8);
    const auto aState=first->state(),bState=second->state();const int aUndo=first->undoStack()->getCurIdx(),bUndo=second->undoStack()->getCurIdx();
    QSignalSpy finished(capture,&MidiCaptureSession::finished),failed(capture,&MidiCaptureSession::failed);
    QVERIFY(!capture->begin({{"monitor",false},{"notes",target}},false).contains("error"));root->setProperty("busy",true);
    int previousTick=-1,count=0;
    for(const auto& value:target){auto note=value.toMap();if(count>=8)break;const int tick=note.value("tick").toInt();if(tick!=previousTick){QTest::qWait(150);previousTick=tick;}
        const int pitch=note.value("pitch").toInt();captureMidiInput(NPlayEvent(ME_NOTEON,0,pitch,103),midiInputTime());captureMidiInput(NPlayEvent(ME_NOTEOFF,0,pitch,0),midiInputTime());++count;}
    bool switched=false;
    QMetaObject::Connection switching;const auto disconnect=qScopeGuard([&]{QObject::disconnect(switching);});
    if(phase=="saving")switching=connect(capture,&MidiCaptureSession::changed,this,[&]{if(!switched&&capture->state().value("saving").toBool()){switched=true;main->setCurrentScoreView(main->scores().indexOf(second));}});
    if(phase=="reload"){
        QPointer<QmlPlugin> old(root);auto container=page->layout()->itemAt(0)->widget();QVERIFY(container);delete container;QTRY_VERIFY(old.isNull());showExpressionPanel();
        root=qobject_cast<QmlPlugin*>(page->property("root").value<QObject*>());QVERIFY(root);QCOMPARE(root->property("captureSession").value<QObject*>(),static_cast<QObject*>(capture));switched=true;
    }
    else {if(phase!="recording")capture->stop();if(phase!="saving"){switched=true;main->setCurrentScoreView(main->scores().indexOf(second));}}
    QTRY_VERIFY_WITH_TIMEOUT(switched&&finished.size()==1,5000);QCOMPARE(failed.size(),0);if(phase!="reload")QVERIFY(map("midi").isEmpty());
    std::atomic_bool cancel(false);const auto recorded=readPerformanceMidi(finished.front().front().toString(),cancel);QVERIFY(!recorded.contains("error"));QCOMPARE(recorded.value("notes").toList().size(),count);
    main->setCurrentScoreView(main->scores().indexOf(first));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!map("candidate").value("velocity").toList().isEmpty(),15000);
    QCOMPARE(map("midi").value("sha256").toString(),recorded.value("sha256").toString());QVERIFY(map("result").value("matches").toList().size()>=6);
    QVERIFY(first->state()==aState&&second->state()==bState);QCOMPARE(first->undoStack()->getCurIdx(),aUndo);QCOMPARE(second->undoStack()->getCurIdx(),bUndo);
    const auto candidate=map("candidate").value("velocity").toList();root->setProperty("applicationScope",2);
    QVERIFY(QMetaObject::invokeMethod(root,"applyPart",Q_ARG(QVariant,"velocity")));QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&first->undoStack()->getCurIdx()==aUndo+1,15000);
    QVERIFY(!candidate.isEmpty());EditData undo;first->undoRedo(true,&undo);QVERIFY(first->state()==aState);
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(main->grab().save(out.filePath("recording-switch-"+phase+".png")));
}

void TestPluginHost::expressionRecordingCloseAndRecover()
{
    auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);QSettings().setValue("workflow/recordingDirectory",_settings.filePath("closed-recordings"));
    auto first=performanceScore123(_settings.filePath("recorded-original.mscx"),2);auto second=performanceScore123(_settings.filePath("recorded-recovery.mscx"),2);QVERIFY(first&&second);
    QVector<MasterScore*> scores{first,second};const auto close=qScopeGuard([&]{releaseHostPanels();for(auto score:scores){score->undoStack()->setClean();main->closeScore(score);}});
    main->setCurrentScoreView(main->appendScore(first));main->appendScore(second);main->resize(1280,900);main->show();showExpressionPanel();
    auto editor=main->performanceEditor();auto page=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(page);
    auto root=qobject_cast<QmlPlugin*>(page->property("root").value<QObject*>());QVERIFY(root);
    auto map=[&](const char* name){const auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toMap():value.toMap();};
    auto capture=qobject_cast<MidiCaptureSession*>(root->property("captureSession").value<QObject*>());QVERIFY(capture);QSignalSpy finished(capture,&MidiCaptureSession::finished);
    auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);
    const auto notes=observer->performanceRange(0,3840,0,first->ntracks(),0,32,observer->analysisRevision()).value("notes").toList();QCOMPARE(notes.size(),10);
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    QVERIFY(!capture->begin({{"monitor",false},{"notes",notes}},false).contains("error"));root->setProperty("busy",true);int previous=-1;
    for(const auto& value:notes){auto n=value.toMap();if(previous!=n.value("tick").toInt()){QTest::qWait(120);previous=n.value("tick").toInt();}int pitch=n.value("pitch").toInt();captureMidiInput(NPlayEvent(ME_NOTEON,0,pitch,106),midiInputTime());QTest::qWait(10);captureMidiInput(NPlayEvent(ME_NOTEOFF,0,pitch,0),midiInputTime());}
    QPointer<MidiCaptureSession> original(capture);QPointer<MasterScore> closed(first);scores.removeOne(first);first->undoStack()->setClean();main->closeScore(first);QTRY_VERIFY(closed.isNull());
    QTRY_COMPARE(finished.size(),1);QTRY_VERIFY(original.isNull());QVERIFY(map("midi").isEmpty());
    const auto recent=workflowContext()->lastRecording();QCOMPARE(recent.value("count").toInt(),10);QCOMPARE(recent.value("path").toString(),finished.front().front().toString());
    QVERIFY(QFileInfo(QUrl(recent.value("path").toString()).toLocalFile()).exists());QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);main->resizeDocks({dock},{610},Qt::Vertical);
    auto splitter=editor->findChild<QSplitter*>("performanceMainSplitter");QVERIFY(splitter);auto sizes=splitter->sizes();sizes[splitter->indexOf(page)]=400;splitter->setSizes(sizes);QTest::qWait(120);
    auto more=root->findChild<QQuickItem*>("expressionApplicationMenuButton");QVERIFY(more&&more->isVisible());
    QTest::mouseClick(more->window(),Qt::LeftButton,Qt::NoModifier,more->mapToScene(QPointF(more->width()/2,more->height()/2)).toPoint());
    auto recentItem=root->findChild<QQuickItem*>("expressionLastRecording");QVERIFY(recentItem);QTRY_VERIFY(recentItem->isVisible()&&recentItem->isEnabled());
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));const bool software=qEnvironmentVariable("QT_QUICK_BACKEND")=="software";
    if(software)QVERIFY(recentItem->window()->grabWindow().save(out.filePath("recording150-recent-menu.png")));
    QTest::mouseClick(recentItem->window(),Qt::LeftButton,Qt::NoModifier,recentItem->mapToScene(QPointF(recentItem->width()/2,recentItem->height()/2)).toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&map("candidate").value("velocity").toList().size()==10,15000);
    const auto before=second->state();const int undo=second->undoStack()->getCurIdx();root->setProperty("applicationScope",2);
    QVERIFY(QMetaObject::invokeMethod(root,"revealResults"));QTest::qWait(100);
    if(software)QVERIFY(root->window()->grabWindow().save(out.filePath("recording150-ready.png")));
    auto use=root->findChild<QQuickItem*>("expressionApplyVelocity");QVERIFY(use&&use->isVisible()&&use->isEnabled());
    QTest::mouseClick(use->window(),Qt::LeftButton,Qt::NoModifier,use->mapToScene(QPointF(use->width()/2,use->height()/2)).toPoint());
    QTRY_COMPARE(second->undoStack()->getCurIdx(),undo+1);QTRY_VERIFY(!root->property("busy").toBool());
    int changed=0;for(auto segment=second->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int tr=0;tr<second->ntracks();++tr)if(auto e=segment->element(tr))if(e->isChord())for(auto n:toChord(e)->notes())changed+=n->veloType()==Note::ValueType::USER_VAL&&n->veloOffset()==106;
    QCOMPARE(changed,10);getAction("undo")->trigger();main->endCmd();QVERIFY(second->state()==before);QTest::qWait(200);
    qInfo()<<"recording recovery undo"<<root->property("stale")<<root->property("status")<<map("candidate").value("velocity").toList().size();
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!root->property("stale").toBool()&&map("candidate").value("velocity").toList().size()==10,15000);
    if(software)QVERIFY(root->window()->grabWindow().save(out.filePath("recording150-recovered.png")));
}
