// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "mscore/plugin/api/pianohandanalysis.h"
#include "mscore/workflow/temporaryscore.h"

static QByteArray physicalScoreXml(int oldEnd=1920,int nextTick=480)
{
    QVariantList notes;notes.append(QVariant(QVariantList{36,0,oldEnd,4,14}));
    auto xml=arrangementScore115(notes);
    const auto voice="<voice>"+arrangementRests115(nextTick)+"<Chord><durationType>quarter</durationType><Note><pitch>72</pitch><tpc>14</tpc></Note></Chord>"+arrangementRests115(1920-nextTick-480)+"</voice>";
    xml.insert(xml.lastIndexOf("</Measure></Staff>"),voice);return xml;
}

void TestPluginHost::arrangementPhysicalKeys()
{
    PluginAPI::PianoHandAnalysis planner;
    QCOMPARE(planner.property("playbackFieldsVersion").toInt(),1);
    auto note=[](int pitch,int writtenEnd,int start,int end,QString id){return QVariantMap{
        {"id",id},{"analysisId",id+"@0:0:"+QString::number(start)},{"pitch",pitch},{"track",4},
        {"tick",0},{"attackTick",0},{"logicalEnd",writtenEnd},{"physicalStart",start},{"physicalEnd",end},
        {"physicalOnSeconds",start/960.},{"playbackChannel","0:0"},{"analysisHand","left"},{"handKnown",true}};};
    auto old=note(36,1920,0,120,"old"),next=note(72,960,480,960,"next");
    QVariantMap options{{"tick",480},{"seconds",.5}};
    auto side=[](QVariantMap result){return result.value("sides").toList().front().toMap();};
    auto result=side(planner.analyse({old,next},{},options));
    QCOMPARE(result.value("before").toMap().value("count").toInt(),1);
    old["logicalEnd"]=240;old["physicalEnd"]=1440;
    result=side(planner.analyse({old,next},{},options));
    QCOMPARE(result.value("before").toMap().value("count").toInt(),2);QVERIFY(!result.value("after").toMap().value("fits").toBool());QCOMPARE(result.value("travel").toInt(),0);
    QVariantMap pedal{{"tick",10},{"end",1440},{"channelKey","0:0"},{"supported",true}};
    result=side(planner.analyse({old,next},{pedal},options));QCOMPARE(result.value("released").toList().size(),1);QVERIFY(result.value("after").toMap().value("fits").toBool());
    pedal["channelKey"]="0:1";
    result=side(planner.analyse({old,next},{pedal},options));QVERIFY(result.value("released").toList().isEmpty());
    pedal["channelKey"]="0:0";pedal["supported"]=false;
    result=side(planner.analyse({old,next},{pedal},options));QVERIFY(result.value("released").toList().isEmpty());
    old["play"]=false;result=side(planner.analyse({old,next},{},options));QCOMPARE(result.value("before").toMap().value("count").toInt(),1);
    old["play"]=true;old["physicalStart"]=800;result=side(planner.analyse({old,next},{},options));QCOMPARE(result.value("before").toMap().value("count").toInt(),1);
    old=note(36,240,0,1440,"old");pedal["supported"]=true;
    result=side(planner.analyse({old,next},{pedal},options));QCOMPARE(result.value("released").toList().size(),1);
    // The next pass refers to the same score note and a fresh physical attack.
    old=note(36,240,1920,2880,"old");next=note(72,960,1920,2880,"next");
    options["tick"]=1920;options["seconds"]=2.;
    result=side(planner.analyse({old,next},{},options));QCOMPARE(result.value("before").toMap().value("count").toInt(),2);
    auto doubling=old;doubling["id"]="voice2";doubling["analysisId"]="voice2@0:0:1920";
    result=side(planner.analyse({old,doubling},{},options));QCOMPARE(result.value("before").toMap().value("count").toInt(),1);
    PluginAPI::PianoHandAnalysis positions;old=note(36,960,0,960,"old");next=note(72,960,480,960,"next");
    options["tick"]=0;options["seconds"]=0.;positions.analyse({old},{},options);
    options["tick"]=480;options["seconds"]=.5;result=side(positions.analyse({old,next},{},options));QVERIFY(!result.value("after").toMap().value("fits").toBool());
    options["tick"]=960;options["seconds"]=1.;result=side(positions.analyse({note(76,1440,960,1440,"later")},{},options));QCOMPARE(result.value("travel").toInt(),0);
}

void TestPluginHost::arrangementPhysicalPlayback()
{
    const auto bundle=arrangementBundle083();QVERIFY2(!bundle.contains("error"),qPrintable(bundle.value("error").toString()));
    auto xml=physicalScoreXml();QVERIFY(!xml.isEmpty());
    const auto path=_settings.filePath("physical-keys.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));QVERIFY(chord);chord->setPlayEventType(PlayEventType::User);
    auto bass=chord->upNote();QVERIFY(bass);
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    auto config=bundle.value("config").toMap();config["harmony"]=false;config["counterpoint"]=false;
    config["roles"]=QVariantMap{{"4",QVariantMap{{"hand","left"}}},{"5",QVariantMap{{"hand","left"}}}};
    auto analyze=[&]() -> QVariantMap {
        auto data=observer.analysisRange(0,1920,0,8,0,64,observer.analysisRevision());if(data.contains("error"))return data;
        const QVariantMap scope{{"start",0},{"end",1920},{"firstTrack",0},{"endTrack",8}};data["scope"]=scope;
        auto playback=playbackRead122(observer,1920);if(playback.contains("error"))return playback;
        playback["scope"]=scope;playback["meters"]=data["meters"];playback["notes"]=data["notes"];data["playback"]=playback;
        const auto state=score->state();const auto undo=score->undoStack()->getCurIdx();
        QSignalSpy done(&observer,&PluginAPI::ScoreObserver::readOnlyJobFinished);
        observer.startReadOnlyJob(bundle.value("source").toString(),{{"data",data},{"config",config},{"imported",QVariantList{}}});
        QElapsedTimer timer;timer.start();while(done.isEmpty()&&timer.elapsed()<15000)QTest::qWait(10);
        if(done.isEmpty())return {{"error","analysis-timeout"}};
        if(score->state()!=state || score->undoStack()->getCurIdx()!=undo)return {{"error","analysis-edited-score"}};
        auto result=done.takeFirst()[1].toMap();result["input"]=data;return result;
    };
    auto has=[](const QVariantMap& result,const QString& code,int tick=480){for(const auto& value:result.value("issues").toList())if(value.toMap().value("tick").toInt()==tick&&value.toMap().value("code")==code)return true;return false;};
    auto full=analyze();QVERIFY2(!full.contains("error"),qPrintable(full.value("error").toString()));QVERIFY(has(full,"span-left"));QVERIFY(!has(full,"travel-left"));
    NoteEventList shorter;shorter.clear();shorter.append(NoteEvent(0,0,50));
    score->startCmd();score->undo(new ChangeNoteEventList(bass,shorter));score->endCmd();main->endCmd();
    auto shortened=analyze();QVERIFY2(!shortened.contains("error"),qPrintable(shortened.value("error").toString()));QVERIFY(!has(shortened,"span-left"));QVERIFY(!has(shortened,"release-left"));
    EditData edit;score->undoRedo(true,&edit);main->endCmd();auto undone=analyze();QVERIFY(has(undone,"span-left"));
    score->undoRedo(false,&edit);main->endCmd();auto redone=analyze();QVERIFY(!has(redone,"span-left"));
    auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction());pedal->setTick2(Fraction::fromTicks(1920));
    score->startCmd();score->undoAddElement(pedal);score->endCmd();main->endCmd();
    auto sustained=analyze();QVERIFY2(!sustained.contains("error"),qPrintable(sustained.value("error").toString()));QVERIFY(!has(sustained,"span-left"));QVERIFY(!has(sustained,"release-left"));
    NoteEventList longer;longer.clear();longer.append(NoteEvent(0,0,1000));
    score->startCmd();score->undo(new ChangeNoteEventList(bass,longer));score->endCmd();main->endCmd();
    auto release=analyze();QVERIFY2(!release.contains("error"),qPrintable(release.value("error").toString()));QVERIFY(has(release,"release-left"));QVERIFY(!has(release,"span-left"));
    score->startCmd();bass->undoChangeProperty(Pid::PLAY,false);score->endCmd();main->endCmd();auto muted=analyze();QVERIFY(!has(muted,"span-left"));QVERIFY(!has(muted,"release-left"));
    // A short written bass can have a manually extended performance event.
    xml=physicalScoreXml(480,960);
    const auto extendedPath=_settings.filePath("physical-extended.mscx");QFile extendedFile(extendedPath);QVERIFY(extendedFile.open(QIODevice::WriteOnly));extendedFile.write(xml);extendedFile.close();
    score=main->readScore(extendedPath);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    auto extendedChord=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));QVERIFY(extendedChord);extendedChord->setPlayEventType(PlayEventType::User);
    NoteEventList extendedEvents;extendedEvents.clear();extendedEvents.append(NoteEvent(0,0,3000));extendedChord->upNote()->setPlayEvents(extendedEvents);score->setPlaylistDirty();
    PluginAPI::Score extendedWrapped(score);observer.setScore(&extendedWrapped);
    auto extended=analyze();QVERIFY2(!extended.contains("error"),qPrintable(extended.value("error").toString()));QVERIFY(has(extended,"span-left",960));
    const auto saved=_settings.filePath("physical-extended-saved.mscz");QVERIFY(main->saveAs(score,false,saved,"mscz"));
    TemporaryScore restored(main->readScore(saved));QVERIFY(restored);score=restored.get();PluginAPI::Score restoredWrapped(score);observer.setScore(&restoredWrapped);
    auto reopened=analyze();QVERIFY2(!reopened.contains("error"),qPrintable(reopened.value("error").toString()));QVERIFY(has(reopened,"span-left",960));
    score->startCmd();score->firstMeasure()->undoChangeProperty(Pid::REPEAT_START,true);score->firstMeasure()->undoChangeProperty(Pid::REPEAT_END,true);score->firstMeasure()->undoChangeProperty(Pid::REPEAT_COUNT,2);score->setExpandRepeats(true);score->endCmd();score->setPlaylistDirty();
    auto repeated=analyze();QVERIFY2(!repeated.contains("error"),qPrintable(repeated.value("error").toString()));int appearances=0;
    for(const auto& value:repeated.value("issues").toList()){auto issue=value.toMap();if(issue.value("code")=="span-left"&&issue.value("tick").toInt()==960){++appearances;QVERIFY(issue.value("occurrence").toInt()>0);QCOMPARE(issue.value("utick").toInt(),960+(issue.value("occurrence").toInt()-1)*1920);}}
    qInfo()<<"Physical repeated ranges"<<repeated.value("input").toMap().value("playback").toMap().value("ranges")<<"span appearances"<<appearances;QCOMPARE(appearances,2);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QFile report(output.filePath("arrangement-physical-keys.json"));QVERIFY(report.open(QIODevice::WriteOnly));
    report.write(QJsonDocument::fromVariant(QVariantMap{{"full",full},{"shortened",shortened},{"sustained",sustained},{"release",release},{"muted",muted},{"extended",extended},{"reopened",reopened},{"repeated",repeated}}).toJson());
}

void TestPluginHost::arrangementPhysicalPanel()
{
    const auto path=_settings.filePath("physical-panel.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(physicalScoreXml());file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));QVERIFY(chord);chord->setPlayEventType(PlayEventType::User);auto bass=chord->upNote();score->select(bass);
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel=arrangementPanel116();QVERIFY(panel);panel->setPanelFloating(true);panel->resizePanel(440,760);QTest::qWait(80);
    struct Close {QPointer<QmlPlugin> panel;~Close(){if(panel)panel->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}} close{panel};
    for(int track:{4,5})QVERIFY(QMetaObject::invokeMethod(panel,"roleChange",Q_ARG(QVariant,QVariant(track)),Q_ARG(QVariant,QVariant("hand")),Q_ARG(QVariant,QVariant("left"))));
    auto issues=[&]{return panel->property("issues").value<QJSValue>().toVariant().toList();};
    auto spanIndex=[&]{const auto rows=issues();for(int i=0;i<rows.size();++i)if(rows[i].toMap().value("code")=="span-left"&&rows[i].toMap().value("tick").toInt()==480)return i;return -1;};
    auto update=[&]{auto button=panel->findChild<QQuickItem*>("arrangementUpdate");if(!button||!button->isVisible())return false;QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());return true;};
    QVERIFY(update());QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),15000);QVERIFY(spanIndex()>=0);
    panel->setProperty("currentIssueIndex",spanIndex());QVERIFY(QMetaObject::invokeMethod(panel,"locate",Q_ARG(QVariant,QVariant(true))));
    QVERIFY(!score->selection().elements().isEmpty());auto selected=score->selection().elements().front();QVERIFY(selected->isNote());QCOMPARE(selected->track(),5);QCOMPARE(toNote(selected)->chord()->tick().ticks(),480);
    const auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::qWait(80);QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-physical-panel.png")));
    NoteEventList shorter;shorter.clear();shorter.append(NoteEvent(0,0,50));score->startCmd();score->undo(new ChangeNoteEventList(bass,shorter));score->endCmd();main->endCmd();
    QVERIFY(update());QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(spanIndex()<0);
    main->cmd(getAction("undo"));QVERIFY(update());QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(spanIndex()>=0);
}
