// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "libmscore/playbacktiming.h"

static QVariantMap tiedTraceBundle(const QVariantMap& bundle)
{
    auto result=bundle;auto source=bundle.value("source").toString();
    source.replace("return {issues:result.issues,roles:result.roles}","return {issues:result.issues,roles:result.roles,calls:readCalls}");
    source="(function(nativeHands){var readCalls=[];var PianoHands={playbackFieldsVersion:nativeHands.playbackFieldsVersion,analyse:function(notes,pedals,options){var value=nativeHands.analyse(notes,pedals,options);readCalls.push({tick:options.tick,seconds:options.seconds,notes:notes,result:value});return value;}};return "+source+";})(PianoHands)";
    result["source"]=source;return result;
}
void TestPluginHost::arrangementTiedContext()
{
    auto xml=arrangementScore115({QVariantList{55,0,480,0,15},QVariantList{60,0,480,0,14},QVariantList{60,480,960,0,14},QVariantList{84,960,1440,0,14}});
    xml.insert(xml.indexOf("</Measure></Staff>"),"<voice>"+arrangementRests115(480)+"<Chord><durationType>quarter</durationType><Note><pitch>64</pitch><tpc>18</tpc></Note></Chord>"+arrangementRests115(960)+"</voice>");
    const auto path=_settings.filePath("tied-hand-context.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    auto first=toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->upNote();auto segment=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);
    auto second=toChord(segment->element(0))->upNote(),other=toChord(segment->element(1))->upNote();
    score->startCmd();auto tie=new Tie(score);tie->setStartNote(first);tie->setEndNote(second);score->undoAddElement(tie);
    auto finger=[&](Note* note,const QString& text){auto value=new Fingering(score);value->setParent(note);value->setTrack(note->track());value->setPlainText(text);score->undoAddElement(value);return value;};
    finger(first,"1");auto changedFinger=finger(second,"2");finger(other,"1");auto tempo=new TempoText(score);tempo->setParent(segment);tempo->setTrack(0);tempo->setFollowText(false);tempo->setTempo(1.);tempo->setXmlText("quarter = 60");score->undoAddElement(tempo);score->endCmd();main->endCmd();QVERIFY(first->tieFor());QVERIFY(second->tieBack());
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto bundle=tiedTraceBundle(arrangementBundle083());auto config=bundle.value("config").toMap();
    config["harmony"]=false;config["counterpoint"]=false;config["roles"]=QVariantMap{{"0",QVariantMap{{"hand","right"}}},{"1",QVariantMap{{"hand","right"}}}};
    auto run=[&]{return physicalContextRun(observer,bundle,physicalContextInput(observer,1920,8),config);};
    auto result=run();QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));bool seen=false;
    for(const auto& value:result.value("calls").toList())if(value.toMap().value("tick").toInt()==480){const auto call=value.toMap();for(const auto& n:call.value("notes").toList())if(n.toMap().value("pitch").toInt()==60){const auto row=n.toMap();QCOMPARE(row.value("tick").toInt(),480);QCOMPARE(row.value("index").toInt(),0);QCOMPARE(row.value("fingerings").toList(),QVariantList{QString("2")});QCOMPARE(row.value("physicalStart").toInt(),0);seen=true;}}
    QVERIFY(seen);for(const auto& value:result.value("issues").toList())QVERIFY(!(value.toMap().value("tick").toInt()==480&&value.toMap().value("code")=="span-right"));
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel=arrangementPanel116();QVERIFY(panel);
    struct Close{QPointer<QmlPlugin> panel;QVariantMap config;~Close(){if(panel){panel->setProperty("config",config);panel->disposePanelForReload();}QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}}close{panel,panel->property("config").value<QJSValue>().toVariant().toMap()};
    panel->setPanelFloating(true);panel->resizePanel(460,760);panel->setProperty("config",config);panel->setProperty("rangeChoice",1);QTest::qWait(100);
    auto click=[&](const char* name){auto item=panel->findChild<QQuickItem*>(name);if(!item||!item->isVisible())return false;QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;};
    auto index=[&]{const auto issues=panel->property("issues").value<QJSValue>().toVariant().toList();for(int i=0;i<issues.size();++i)if(issues[i].toMap().value("tick").toInt()==480&&issues[i].toMap().value("code")=="span-right")return i;return -1;};
    QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),15000);QVERIFY(index()<0);
    score->startCmd();changedFinger->undoChangeProperty(Pid::TEXT,QString("1"));score->endCmd();main->endCmd();QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(index()>=0);
    panel->setProperty("currentIssueIndex",index());QVERIFY(click("arrangementLocate"));QVERIFY(!score->selection().elements().isEmpty());QCOMPARE(score->selection().elements().front(),static_cast<Element*>(second));
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::mouseMove(panel->window(),QPoint(5,5));QTest::qWait(100);QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-tied-context.png")));
    main->cmd(getAction("undo"));QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(index()<0);
    score->startCmd();score->firstMeasure()->undoChangeProperty(Pid::REPEAT_START,true);score->firstMeasure()->undoChangeProperty(Pid::REPEAT_END,true);score->firstMeasure()->undoChangeProperty(Pid::REPEAT_COUNT,2);score->setExpandRepeats(true);score->endCmd();main->endCmd();
    auto repeated=run();QVERIFY(!repeated.contains("error"));bool secondPass=false;for(const auto& value:repeated.value("calls").toList())if(value.toMap().value("tick").toInt()==2400){secondPass=true;QVERIFY(qAbs(value.toMap().value("seconds").toDouble()-PlaybackTiming::time(score,2400))<1.e-9);}QVERIFY(secondPass);
    auto partialInput=physicalContextInput(observer,1920,8);auto playback=partialInput.value("playback").toMap();auto scope=partialInput.value("scope").toMap();scope["start"]=480;partialInput["scope"]=scope;playback["scope"]=scope;
    playback["ranges"]=observer.analysisPlaybackPrepare(480,1920,observer.analysisPlaybackRevision()).value("ranges");partialInput["playback"]=playback;
    auto partial=physicalContextRun(observer,bundle,partialInput,config);QVERIFY(!partial.contains("error"));bool selectedStart=false;
    for(const auto& value:partial.value("calls").toList())if(value.toMap().value("tick").toInt()==480){selectedStart=true;QVERIFY(qAbs(value.toMap().value("seconds").toDouble()-PlaybackTiming::time(score,480))<1.e-9);}QVERIFY(selectedStart);
    QVERIFY(main->saveFile());TemporaryScore reopened(main->readScore(path));QVERIFY(reopened);PluginAPI::Score restored(reopened.get());observer.setScore(&restored);auto saved=run();QVERIFY(!saved.contains("error"));
    QFile report(out.filePath("arrangement-tied-context.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(QVariantMap{{"initial",result},{"repeated",repeated},{"partial",partial},{"saved",saved}}).toJson());
}

void TestPluginHost::arrangementTiedHandTransfer()
{
    PluginAPI::PianoHandAnalysis planner;QVariantMap held{{"id","first"},{"analysisId","held@0:0:0"},{"pitch",60},{"track",0},{"physicalStart",0},{"physicalEnd",1440},{"physicalOnSeconds",0.},{"physicalEndSeconds",1.5},{"analysisHand","left"},{"handKnown",true},{"fingerings",QVariantList{QString("1")}}};
    planner.analyse({held},{},{{"tick",0},{"seconds",0.}});held["id"]="continuation";held["analysisHand"]="right";held["fingerings"]=QVariantList{QString("2")};
    auto transfer=planner.analyse({held},{},{{"tick",480},{"seconds",.5}});QCOMPARE(transfer.value("sides").toList()[1].toMap().value("attacks").toList().size(),0);
    auto next=held;next["id"]="new-left";next["analysisId"]="new-left@0:0:960";next["pitch"]=84;next["analysisHand"]="left";next["physicalStart"]=960;next["physicalEnd"]=1440;next["physicalOnSeconds"]=1.;
    auto after=planner.analyse({held,next},{},{{"tick",960},{"seconds",1.}});QCOMPARE(after.value("sides").toList()[0].toMap().value("travel").toInt(),0);
    held["id"]="third";held["fingerings"]=QVariantList{QString("3")};auto same=planner.analyse({held},{},{{"tick",1200},{"seconds",1.25}});QCOMPARE(same.value("sides").toList()[1].toMap().value("attacks").toList().size(),0);
}
