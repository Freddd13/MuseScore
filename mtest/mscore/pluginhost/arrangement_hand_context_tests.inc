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
