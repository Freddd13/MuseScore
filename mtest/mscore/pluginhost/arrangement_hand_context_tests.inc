// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "libmscore/fingering.h"
#include "libmscore/symbol.h"

// Follow the same paged score and MIDI queries as the panel, yielding between
// calls. Keep every written note reference for independent output checks.
static QVariantMap physicalContextInput(PluginAPI::ScoreObserver& observer,int end,int endTrack)
{
    QVariantMap data;QVariantList notes,rests,hands,harmonies,meters;QSet<int> meterTicks;
    const auto contentRevision=observer.analysisRevision();double worst=0,firstPreparation=0;int pages=0,from=0;
    for(;;){
        QElapsedTimer timer;timer.start();auto page=observer.analysisRange(0,end,0,endTrack,from,16,contentRevision);
        worst=qMax(worst,timer.nsecsElapsed()/1.e6);if(page.contains("error"))return page;
        if(!pages){data=page;firstPreparation=page.value("preparationMilliseconds").toDouble();}
        notes.append(page.value("notes").toList());rests.append(page.value("rests").toList());hands.append(page.value("hands").toList());harmonies.append(page.value("harmonies").toList());
        for(const auto& value:page.value("meters").toList()){const auto tick=value.toMap().value("tick").toInt();if(!meterTicks.contains(tick)){meterTicks.insert(tick);meters.append(value);}}
        ++pages;QTest::qWait(1);if(page.value("done").toBool())break;
        const auto next=page.value("nextTick").toInt();if(next<=from||pages>20000)return {{"error","score-page-did-not-advance"}};from=next;
    }
    const QVariantMap scope{{"start",0},{"end",end},{"firstTrack",0},{"endTrack",endTrack}};
    data["notes"]=notes;data["rests"]=rests;data["hands"]=hands;data["harmonies"]=harmonies;data["meters"]=meters;data["scope"]=scope;
    const auto playbackRevision=observer.analysisPlaybackRevision();QVariantMap playback;int preparations=0;double worstPlayback=0;
    do{
        QElapsedTimer timer;timer.start();playback=observer.analysisPlaybackPrepare(0,end,playbackRevision);worstPlayback=qMax(worstPlayback,timer.nsecsElapsed()/1.e6);
        if(playback.contains("error"))return playback;QTest::qWait(1);if(++preparations>20000)return {{"error","playback-prepare-limit"}};
    }while(!playback.value("ready").toBool());
    QVariantList events;from=INT_MIN;int eventPages=0;
    for(;;){
        QElapsedTimer timer;timer.start();auto page=observer.analysisPlaybackPage(from,0,endTrack,16,playbackRevision);worstPlayback=qMax(worstPlayback,timer.nsecsElapsed()/1.e6);
        if(page.contains("error"))return page;events.append(page.value("events").toList());++eventPages;QTest::qWait(1);
        if(page.value("done").toBool())break;const auto next=page.value("nextTick").toInt();if(next<=from||eventPages>20000)return {{"error","event-page-did-not-advance"}};from=next;
    }
    playback["events"]=events;playback["notes"]=notes;playback["meters"]=meters;playback["scope"]=scope;data["playback"]=playback;
    data["readChecks"]=QVariantMap{{"scorePages",pages},{"eventPages",eventPages},{"preparationCalls",preparations},{"worstScoreCallMs",worst},{"firstVelocityPreparationMs",firstPreparation},{"worstPlaybackCallMs",worstPlayback}};
    return data;
}
static QVariantMap physicalContextRun(PluginAPI::ScoreObserver& observer,const QVariantMap& bundle,const QVariantMap& data,const QVariantMap& config)
{
    QSignalSpy done(&observer,&PluginAPI::ScoreObserver::readOnlyJobFinished);
    observer.startReadOnlyJob(bundle.value("source").toString(),{{"data",data},{"config",config},{"imported",QVariantList{}}});
    QElapsedTimer timer;timer.start();while(done.isEmpty()&&timer.elapsed()<60000)QTest::qWait(10);
    if(done.isEmpty()){observer.cancelReadOnlyJob();return {{"error","analysis-timeout"}};}
    auto result=done.takeFirst()[1].toMap();result["waitMilliseconds"]=timer.elapsed();return result;
}

void TestPluginHost::arrangementPhysicalAnnotations()
{
    auto xml=arrangementScore115({QVariantList{60,0,1920,0,14},QVariantList{84,0,1920,0,14},QVariantList{36,0,1920,4,14}});
    xml.insert(xml.indexOf("<Chord>"),"<Chord><acciaccatura/><durationType>eighth</durationType><Note><pitch>64</pitch><tpc>18</tpc></Note></Chord>");
    const auto path=_settings.filePath("physical-annotations.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    const auto segment=score->firstSegment(SegmentType::ChordRest);auto upper=toChord(segment->element(0)),lower=toChord(segment->element(4));QVERIFY(upper&&lower);QCOMPARE(upper->graceNotes().size(),1);
    auto mark=[&](Note* note,SymId id){auto value=new Symbol(score);value->setSym(id);value->applyHandPreset();value->setParent(note);value->setTrack(note->track());score->undoAddElement(value);return value;};
    auto finger=[&](Note* note,const QString& text){auto value=new Fingering(score);value->setPlainText(text);value->setParent(note);value->setTrack(note->track());score->undoAddElement(value);return value;};
    score->startCmd();mark(upper->downNote(),SymId::keyboardPlayWithRH);mark(upper->upNote(),SymId::keyboardPlayWithRH);mark(lower->upNote(),SymId::keyboardPlayWithLH);
    mark(upper->graceNotes().front()->upNote(),SymId::keyboardPlayWithLH);finger(upper->downNote(),"1");finger(upper->upNote(),"5");finger(lower->upNote(),"1");auto alternate=finger(lower->upNote(),"2");score->endCmd();main->endCmd();
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    const auto bundle=arrangementBundle083();QVERIFY(!bundle.contains("error"));auto config=bundle.value("config").toMap();config["harmony"]=false;config["counterpoint"]=false;
    auto data=physicalContextInput(observer,1920,8);QVERIFY2(!data.contains("error"),qPrintable(data.value("error").toString()));
    QCOMPARE(data.value("notes").toList().size(),4);QCOMPARE(data.value("hands").toList().size(),4);int graceMarks=0,fingers=0;
    for(const auto& value:data.value("hands").toList())if(value.toMap().value("grace").toBool())++graceMarks;
    for(const auto& value:data.value("notes").toList())fingers+=value.toMap().value("fingerings").toList().size();QCOMPARE(graceMarks,1);QCOMPARE(fingers,4);
    const auto state=score->state();const int undo=score->undoStack()->getCurIdx();
    auto result=physicalContextRun(observer,bundle,data,config);QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));bool rightSpan=false,leftFingers=false;
    for(const auto& value:result.value("issues").toList()){
        const auto issue=value.toMap();if(issue.value("code")=="span-right"){rightSpan=true;for(const auto& n:issue.value("notes").toList())QCOMPARE(n.toMap().value("track").toInt(),0);}
        if(issue.value("code")=="fingering-uncertain"){leftFingers=true;for(const auto& n:issue.value("notes").toList())QCOMPARE(n.toMap().value("pitch").toInt(),36);}
        QVERIFY(issue.value("code")!="hand-uncertain");
    }
    QVERIFY(rightSpan&&leftFingers);QVERIFY(score->state()==state);QCOMPARE(score->undoStack()->getCurIdx(),undo);
    score->startCmd();score->undoRemoveElement(alternate);score->endCmd();main->endCmd();auto corrected=physicalContextRun(observer,bundle,physicalContextInput(observer,1920,8),config);QVERIFY(!corrected.contains("error"));
    for(const auto& value:corrected.value("issues").toList())QVERIFY(value.toMap().value("code")!="fingering-uncertain");
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QFile report(output.filePath("physical-hand-annotations.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(QVariantMap{{"input",data},{"original",result},{"corrected",corrected}}).toJson());
}

void TestPluginHost::arrangementNestedHandMarkers()
{
    auto xml=arrangementScore115({QVariantList{60,0,1920,0,14},QVariantList{36,0,1920,4,14}});
    xml.insert(xml.indexOf("<Chord>"),"<Chord><acciaccatura/><durationType>eighth</durationType><Note><pitch>64</pitch><tpc>18</tpc></Note></Chord>");
    const auto path=_settings.filePath("physical-nested-marks.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    auto segment=score->firstSegment(SegmentType::ChordRest);auto upper=toChord(segment->element(0)),lower=toChord(segment->element(4));
    auto mark=[&](Element* parent,SymId id,int track){auto symbol=new Symbol(score);symbol->setSym(id);symbol->setParent(parent);symbol->setTrack(track);score->undoAddElement(symbol);return symbol;};
    score->startCmd();auto left=mark(lower->upNote(),SymId::keyboardPlayWithLH,4);mark(left,SymId::keyboardPlayWithLHEnd,4);
    mark(mark(upper->upNote(),SymId::coda,0),SymId::keyboardPlayWithRHEnd,0);
    mark(mark(upper->graceNotes().front()->upNote(),SymId::coda,0),SymId::keyboardPlayWithLH,0);score->endCmd();main->endCmd();
    auto check=[&](Ms::Score* current){
        PluginAPI::Score wrapped(current);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);const auto state=current->state();const auto undo=current->undoStack()->getCurIdx();
        auto data=observer.analysisRange(0,1920,0,8,0,64,observer.analysisRevision());QVERIFY(!data.contains("error"));const auto hands=data.value("hands").toList();QCOMPARE(hands.size(),4);
        int leftCount=0,rightCount=0,graceCount=0;
        for(const auto& value:hands){const auto row=value.toMap();QCOMPARE(row.value("attached").toString(),QString("note"));QCOMPARE(row.value("index").toInt(),0);
            if(row.value("grace").toBool()){++graceCount;QCOMPARE(row.value("pitch").toInt(),64);QCOMPARE(row.value("graceIndex").toInt(),0);}
            else if(row.value("left").toBool()){++leftCount;QCOMPARE(row.value("track").toInt(),4);QCOMPARE(row.value("pitch").toInt(),36);}
            else{++rightCount;QCOMPARE(row.value("pitch").toInt(),60);QVERIFY(row.value("end").toBool());}
        }
        QCOMPARE(leftCount,2);QCOMPARE(rightCount,1);QCOMPARE(graceCount,1);QVERIFY(current->state()==state);QCOMPARE(current->undoStack()->getCurIdx(),undo);
    };
    check(score);QVERIFY(main->saveFile());main->closeScore(score);score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));check(score);
}

void TestPluginHost::arrangementPhysicalRealScore()
{
    const auto original=qEnvironmentVariable("P2_C3_REAL_SCORE");if(original.isEmpty())QSKIP("Set a read-only real score and independent XML counts");
    QFile source(original);QVERIFY(source.open(QIODevice::ReadOnly));const auto sourceBytes=source.readAll();source.close();const auto hash=QCryptographicHash::hash(sourceBytes,QCryptographicHash::Sha256).toHex();
    QFile expectedFile(qEnvironmentVariable("P2_C3_REAL_EXPECTED"));QVERIFY(expectedFile.open(QIODevice::ReadOnly));const auto expected=QJsonDocument::fromJson(expectedFile.readAll()).toVariant().toMap();QCOMPARE(hash,expected.value("sha256").toByteArray());
    const auto path=_settings.filePath("physical-real-copy.mscz");QFile copied(path);QVERIFY(copied.open(QIODevice::WriteOnly));QCOMPARE(copied.write(sourceBytes),qint64(sourceBytes.size()));copied.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);const auto state=score->state();const int undo=score->undoStack()->getCurIdx();
    SourceTimerResolution resolution;QElapsedTimer clock;clock.start();qint64 previous=clock.nsecsElapsed();QVector<double> gaps;QTimer heartbeat;heartbeat.setTimerType(Qt::PreciseTimer);heartbeat.setInterval(16);
    QObject::connect(&heartbeat,&QTimer::timeout,main,[&]{const auto now=clock.nsecsElapsed();gaps.append((now-previous)/1.e6);previous=now;});heartbeat.start();
    QElapsedTimer readTime;readTime.start();auto data=physicalContextInput(observer,score->lastMeasure()->endTick().ticks(),score->ntracks());const auto readMs=readTime.elapsed();QVERIFY2(!data.contains("error"),qPrintable(data.value("error").toString()));
    const auto notes=data.value("notes").toList();QCOMPARE(notes.size(),expected.value("notes").toInt());QCOMPARE(data.value("hands").toList().size(),expected.value("hands").toInt());
    int fingeringCount=0;QMap<QString,int> fingerTexts;QHash<QString,QVariantMap> references;
    for(const auto& value:notes){const auto row=value.toMap();references.insert(row.value("id").toString(),row);for(const auto& f:row.value("fingerings").toList()){++fingeringCount;++fingerTexts[f.toString()];}}
    QCOMPARE(fingeringCount,expected.value("fingerings").toInt());QVariantMap actualTexts;for(auto it=fingerTexts.cbegin();it!=fingerTexts.cend();++it)actualTexts[it.key()]=it.value();QCOMPARE(actualTexts,expected.value("fingerTexts").toMap());
    const auto bundle=arrangementBundle083();QVERIFY(!bundle.contains("error"));auto config=bundle.value("config").toMap();config["harmony"]=false;config["counterpoint"]=false;
    const auto memory=sourcePrivateBytes();auto result=physicalContextRun(observer,bundle,data,config);QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));
    for(const auto& value:result.value("issues").toList())for(const auto& note:value.toMap().value("notes").toList()){
        const auto row=note.toMap(),reference=references.value(row.value("id").toString());QVERIFY(!reference.isEmpty());for(const auto& name:{"tick","track","index","writtenPitch"})QCOMPARE(row.value(name),reference.value(name));
    }
    heartbeat.stop();QVERIFY(!gaps.isEmpty());QVERIFY(!QWidget::mouseGrabber());QVERIFY(score->state()==state);QCOMPARE(score->undoStack()->getCurIdx(),undo);
    QVariantMap summary{{"readMilliseconds",readMs},{"readChecks",data.value("readChecks")},{"workerMilliseconds",result.value("workerMilliseconds")},{"waitMilliseconds",result.value("waitMilliseconds")},{"notes",notes.size()},{"fingerings",fingeringCount},{"hands",data.value("hands").toList().size()},{"issues",result.value("issues").toList().size()},{"guiMaximumMilliseconds",sourcePercentile(gaps,1.)},{"guiP95Milliseconds",sourcePercentile(gaps,.95)},{"memoryBeforeWorker",memory},{"memoryAfterWorker",sourcePrivateBytes()}};
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QFile report(output.filePath("physical-real-score.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(QVariantMap{{"summary",summary},{"input",data},{"result",result}}).toJson());
    QFile originalAgain(original);QVERIFY(originalAgain.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(originalAgain.readAll(),QCryptographicHash::Sha256).toHex(),hash);qInfo()<<"Physical real score"<<summary;
}

void TestPluginHost::arrangementMovementTiming()
{
    auto key=[](int pitch,int start,int end,double on,double off,const QString& id){return QVariantMap{
        {"id",id},{"analysisId",id},{"pitch",pitch},{"track",4},{"attackTick",start},{"logicalEnd",end},
        {"physicalStart",start},{"physicalEnd",end},{"physicalOnSeconds",on},{"physicalEndSeconds",off},
        {"playbackChannel","0:0"},{"analysisHand","left"},{"handKnown",true}};};
    auto side=[](const QVariantMap& result){return result.value("sides").toList().front().toMap();};
    auto old=key(36,0,470,0.,470/960.,"old"),next=key(72,480,960,.5,1.,"next");
    PluginAPI::PianoHandAnalysis planner;planner.analyse({old},{},{{"tick",0},{"seconds",0.}});
    auto result=side(planner.analyse({next},{},{{"tick",480},{"seconds",.5}}));
    QVERIFY(result.value("travel").toInt()>0);
    QVERIFY2(qAbs(result.value("available").toDouble()-10/960.)<1.e-9,"Movement time starts when the old key is released");
    QCOMPARE(result.value("movementFrom").toList().size(),1);QCOMPARE(result.value("movementFrom").toList().front().toMap().value("id").toString(),QString("old"));
    auto run=[&](const QVariantList& previous,const QVariantMap& following,const QVariantList& pedals=QVariantList{},QVariantMap options=QVariantMap{}){
        PluginAPI::PianoHandAnalysis p;options["tick"]=0;options["seconds"]=0.;p.analyse(previous,pedals,options);
        options["tick"]=following.value("physicalStart");options["seconds"]=following.value("physicalOnSeconds");return side(p.analyse({following},pedals,options));
    };
    old["physicalEnd"]=120;old["physicalEndSeconds"]=.125;result=run({old},next);
    QVERIFY(qAbs(result.value("available").toDouble()-.375)<1.e-9);
    old["physicalEnd"]=470;old["physicalEndSeconds"]=.99;next["physicalOnSeconds"]=1.;next["physicalEndSeconds"]=2.;result=run({old},next);
    QVERIFY(qAbs(result.value("available").toDouble()-.01)<1.e-9);
    // A late release on the higher key leaves a shorter final part of the move.
    old=key(36,0,96,0.,.1,"low");auto later=key(48,0,470,0.,.49,"high");next=key(72,480,960,.5,1.,"next");
    result=run({old,later},next);QCOMPARE(result.value("travelTotal").toInt(),20);QCOMPARE(result.value("travel").toInt(),8);QVERIFY(qAbs(result.value("available").toDouble()-.01)<1.e-9);
    QCOMPARE(result.value("movementFrom").toList().front().toMap().value("id").toString(),QString("high"));
    old=key(36,0,470,0.,.49,"old");QVariantMap pedal{{"tick",20},{"seconds",20/960.},{"end",960},{"endSeconds",1.},{"channelKey","0:0"},{"supported",true}};
    result=run({old},next,{pedal});QVERIFY(qAbs(result.value("available").toDouble()-.01)<1.e-9);QVERIFY(qAbs(result.value("pedalAvailable").toDouble()-.38)<1.e-9);
    QCOMPARE(result.value("pedalReleaseNotes").toList().size(),1);
    pedal["supported"]=false;result=run({old},next,{pedal});QVERIFY(qAbs(result.value("pedalAvailable").toDouble()-.01)<1.e-9);QVERIFY(result.value("pedalReleaseNotes").toList().isEmpty());
    pedal["supported"]=true;pedal["channelKey"]="0:1";result=run({old},next,{pedal});QVERIFY(result.value("pedalReleaseNotes").toList().isEmpty());
    pedal["channelKey"]="0:0";pedal["end"]=460;result=run({old},next,{pedal});QVERIFY(result.value("pedalReleaseNotes").toList().isEmpty());
    pedal["end"]=960;result=run({old},next,{pedal},{{"minimumHoldMs",490}});QVERIFY(result.value("pedalAvailable").toDouble()<.011);
    old.remove("physicalEndSeconds");result=run({old},next);QVERIFY(qAbs(result.value("available").toDouble()-.5)<1.e-9);
    // Parts retain their own preceding hand position; repeats start a fresh sequence.
    old=key(36,0,470,0.,.49,"old");PluginAPI::PianoHandAnalysis parts;
    parts.analyse({old},{},{{"tick",0},{"seconds",0.},{"part",0}});
    parts.analyse({key(96,0,470,0.,.49,"other")},{},{{"tick",0},{"seconds",0.},{"part",8}});
    result=side(parts.analyse({next},{},{{"tick",480},{"seconds",.5},{"part",0}}));QVERIFY(qAbs(result.value("available").toDouble()-.01)<1.e-9);
    result=side(parts.analyse({old},{},{{"tick",0},{"seconds",0.},{"part",0}}));QCOMPARE(result.value("travel").toInt(),0);QVERIFY(result.value("movementFrom").toList().isEmpty());
}

void TestPluginHost::arrangementMovementPanel()
{
    const auto path=_settings.filePath("physical-movement.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(physicalScoreXml(480,480));file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));auto bass=chord->upNote();chord->setPlayEventType(PlayEventType::User);
    NoteEventList almost;almost.clear();almost.append(NoteEvent(0,0,990));bass->setPlayEvents(almost);score->setPlaylistDirty();
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);const auto bundle=arrangementBundle083();
    auto config=bundle.value("config").toMap();config["harmony"]=false;config["counterpoint"]=false;config["autoRefresh"]=false;
    config["roles"]=QVariantMap{{"4",QVariantMap{{"hand","left"}}},{"5",QVariantMap{{"hand","left"}}}};
    auto input=physicalContextInput(observer,1920,8);auto result=physicalContextRun(observer,bundle,input,config);QVERIFY(!result.contains("error"));
    auto transition=[](const QVariantList& rows){for(int i=0;i<rows.size();++i)if(rows[i].toMap().value("code")=="transition-left"&&rows[i].toMap().value("tick").toInt()==480)return i;return -1;};
    auto rows=result.value("issues").toList();int index=transition(rows);QVERIFY(index>=0);QVERIFY(rows[index].toMap().value("basis").toString().contains("C2"));
    double oldOff=-1,nextOn=-1;for(const auto& value:input.value("playback").toMap().value("events").toList()){
        const auto event=value.toMap();if(event.value("pitch").toInt()==36&&event.value("kind")=="off")oldOff=event.value("seconds").toDouble();
        if(event.value("pitch").toInt()==72&&event.value("kind")=="on")nextOn=event.value("seconds").toDouble();
    }
    QVERIFY(oldOff>0&&nextOn>oldOff);const auto gap=QString::number(qRound((nextOn-oldOff)*1000))+" ms";QVERIFY(rows[index].toMap().value("basis").toString().contains(gap));
    auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction());pedal->setTick2(Fraction::fromTicks(1920));
    score->startCmd();score->undoAddElement(pedal);score->endCmd();main->endCmd();
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel=arrangementPanel116();QVERIFY(panel);
    struct Close{QPointer<QmlPlugin> panel;QVariantMap config;~Close(){if(panel){panel->setProperty("config",config);panel->disposePanelForReload();}QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}}close{panel,panel->property("config").value<QJSValue>().toVariant().toMap()};
    panel->setPanelFloating(true);panel->resizePanel(460,760);panel->setProperty("config",config);panel->setProperty("rangeChoice",1);QTest::qWait(100);
    auto update=[&]{auto button=panel->findChild<QQuickItem*>("arrangementUpdate");if(!button||!button->isVisible())return false;QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());return true;};
    auto issues=[&]{return panel->property("issues").value<QJSValue>().toVariant().toList();};
    QVERIFY(update());QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),15000);
    rows=issues();index=transition(rows);QVERIFY(index>=0);const auto selected=rows[index].toMap();QVERIFY(selected.value("title").toString().contains(QString::fromUtf8("借踏板")));QVERIFY(selected.value("basis").toString().contains("380 ms"));
    panel->setProperty("currentIssueIndex",index);auto locate=panel->findChild<QQuickItem*>("arrangementLocate");QVERIFY(locate&&locate->isVisible());
    QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,locate->mapToScene(QPointF(locate->width()/2,locate->height()/2)).toPoint());
    QVERIFY(!score->selection().elements().isEmpty());const auto target=score->selection().elements().front();QVERIFY(target->isNote());QCOMPARE(target->track(),5);QCOMPARE(toNote(target)->chord()->tick().ticks(),480);
    const auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::qWait(100);QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-movement-panel.png")));QVERIFY(main->currentScoreView()->grab().save(out.filePath("arrangement-movement-score.png")));
    NoteEventList shortGate;shortGate.clear();shortGate.append(NoteEvent(0,0,250));score->startCmd();score->undo(new ChangeNoteEventList(bass,shortGate));score->endCmd();main->endCmd();
    QVERIFY(update());QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(transition(issues())<0);
    main->cmd(getAction("undo"));QVERIFY(update());QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(transition(issues())>=0);
    QVERIFY(main->saveFile());TemporaryScore reopened(main->readScore(path));QVERIFY(reopened);PluginAPI::Score restored(reopened.get());observer.setScore(&restored);
    auto restoredResult=physicalContextRun(observer,bundle,physicalContextInput(observer,1920,8),config);QVERIFY(!restoredResult.contains("error"));QVERIFY(transition(restoredResult.value("issues").toList())>=0);
    QFile report(out.filePath("arrangement-movement.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(QVariantMap{{"initialInput",input},{"initialResult",result},{"pedalIssue",selected},{"reopened",restoredResult}}).toJson());
}
