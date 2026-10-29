// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "mscore/workflow/arrangementcontext.h"
namespace {
QByteArray contextFixture143() {
    const auto piano=QByteArray::fromHex("e4b8bbe992a2e790b4");
    QByteArray xml("<?xml version=\"1.0\" encoding=\"UTF-8\"?><museScore version=\"3.02\"><Score><Division>480</Division><Part>"
        "<Staff id=\"1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>"
        "<Staff id=\"2\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff><trackName>");
    xml+=piano+"</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><longName>"+piano+"</longName><Channel/></Instrument></Part>";
    xml+="<Part><Staff id=\"3\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff><trackName>Solo clarinet</trackName>"
        "<Instrument id=\"clarinet\"><instrumentId>wind.reed.clarinet</instrumentId><longName>Solo clarinet</longName>"
        "<transposeDiatonic>-1</transposeDiatonic><transposeChromatic>-2</transposeChromatic><Channel><program value=\"71\"/></Channel></Instrument></Part>";
    for(int staff=0;staff<3;++staff) {
        xml+="<Staff id=\""+QByteArray::number(staff+1)+"\">";
        for(int bar=0;bar<2;++bar) {
            xml+=bar?"<Measure>":"<Measure len=\"1/4\">";
            if(!staff&&!bar)xml+="<startRepeat/>";
            xml+="<voice>";
            if(!bar)xml+="<Clef><concertClefType>"+QByteArray(staff==1?"F":"G")+"</concertClefType><transposingClefType>"+QByteArray(staff==1?"F":"G")+"</transposingClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
            if(!staff&&!bar)xml+="<Chord><acciaccatura/><durationType>eighth</durationType><Note><pitch>60</pitch><tpc>14</tpc></Note></Chord>";
            const int offsets[]={0,2,4,5},tpcs[]={14,16,18,13};
            for(int beat=0;beat<(bar?4:1);++beat) {
                const int pitch=(staff==0?60:staff==1?48:58)+offsets[beat];
                xml+="<Chord><durationType>quarter</durationType>";
                if(!staff&&!bar)xml+="<staffMove>1</staffMove>";
                xml+="<Note><pitch>"+QByteArray::number(pitch)+"</pitch><tpc>"+QByteArray::number(tpcs[beat]-(staff==2?2:0))+"</tpc>";
                if(staff==2)xml+="<tpc2>"+QByteArray::number(tpcs[beat])+"</tpc2>";
                xml+="</Note></Chord>";
            }
            xml+="</voice>";
            if(!staff&&!bar)xml+="<noOffset>10</noOffset>";
            if(!staff&&bar)xml+="<endRepeat>2</endRepeat>";
            xml+="</Measure>";
        }
        xml+="</Staff>";
    }
    return xml+"</Score></museScore>";
}
MasterScore *contextScore143(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::WriteOnly))return nullptr;
    file.write(contextFixture143());file.close();auto score=Ms::mscore->readScore(path);
    if(score){score->style().set(Sid::concertPitch,false);score->doLayout();Ms::mscore->setCurrentScoreView(Ms::mscore->appendScore(score));}
    return score;
}
}

void TestPluginHost::context143Notation() {
    auto score=contextScore143(_settings.filePath("context-143.mscx"));QVERIFY(score);
    PluginAPI::Score wrapper(score);PluginAPI::ScoreObserver reader;reader.setScore(&wrapper);
    score->select(toChord(score->findCR(Fraction::fromTicks(480),0))->upNote());
    score->setPlayPos(Fraction::fromTicks(960));
    const auto before=score->state();const int undo=score->undoStack()->getCurIdx();const auto state=reader.arrangementState({{"whole",true}});
    QCOMPARE(state["parts"].toList().size(),2);QCOMPARE(state["staves"].toList().size(),3);
    QCOMPARE(state["parts"].toList().first().toMap()["name"].toString().toUtf8(),QByteArray::fromHex("e4b8bbe992a2e790b4"));
    QCOMPARE(state["parts"].toList().last().toMap()["transpose"].toMap()["chromatic"].toInt(),-2);
    const auto args=QVariantMap{{"snapshotId",state["snapshotId"]},{"scope",state["scope"]},{"fields",QStringList{"geometry"}},{"limit",64},{"fromTick",0}};
    QVariantList notes,meters;auto request=args;
    for(int i=0;i<20;++i){const auto page=reader.arrangementRange(request);QVERIFY2(!page.contains("error"),qUtf8Printable(page["error"].toString()));notes+=page["notes"].toList();meters+=page["meters"].toList();if(page["done"].toBool())break;request["fromTick"]=page["nextTick"];}
    QSet<QString> ids;int grace=0;bool transposed=false,crossed=false;
    for(auto value:notes) {
        const auto row=value.toMap();const auto ref=row["ref"].toMap();
        QVERIFY(!ids.contains(ref["id"].toString()));ids.insert(ref["id"].toString());
        grace+=row["grace"].toBool();
        if(row["track"].toInt()==8&&row["tick"].toInt()==0){QCOMPARE(row["concertPitch"].toInt(),58);QCOMPARE(row["notatedPitch"].toInt(),60);QCOMPARE(row["soundingPitch"].toInt(),58);QCOMPARE(row["noteName"].toString(),QString("C4"));transposed=true;}
        if(row["track"].toInt()==0&&!row["grace"].toBool()&&row["tick"].toInt()==0){QCOMPARE(row["displayStaff"].toInt(),1);crossed=true;}
    }
    QCOMPARE(grace,1);QCOMPARE(notes.size(),16);QVERIFY(transposed&&crossed);
    bool ordinal=false;for(auto value:meters){const auto row=value.toMap();if(row["tick"].toInt()==480){QCOMPARE(row["measure"].toInt(),2);ordinal=true;}}
    QVERIFY(ordinal);QCOMPARE(state["measureCount"].toInt(),2);
    const auto image=reader.arrangementImage({{"snapshotId",state["snapshotId"]},{"scope",state["scope"]}});
    QVERIFY(!image.contains("error"));for(auto value:image["notes"].toList())QVERIFY(ids.contains(value.toMap()["ref"].toMap()["id"].toString()));
    const auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QFile metadata(out.filePath("context-143-native.json"));QVERIFY(metadata.open(QIODevice::WriteOnly));
    auto evidence=state;evidence["notes"]=notes;evidence["meters"]=meters;metadata.write(QJsonDocument::fromVariant(evidence).toJson());metadata.close();
    QImage png;QVERIFY(png.loadFromData(QByteArray::fromBase64(image["pngBase64"].toString().toLatin1()),"PNG"));QVERIFY(png.save(out.filePath("context-143-native.png")));
    QVERIFY(score->state()==before);QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(score->playPos().ticks(),960);
}

void TestPluginHost::context143Performance() {
    auto score=contextScore143(_settings.filePath("context-performance-143.mscx"));QVERIFY(score);score->setExpandRepeats(true);
    auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction());pedal->setTick2(score->lastMeasure()->endTick());
    score->startCmd();score->undoAddElement(pedal);score->endCmd();Ms::mscore->endCmd();
    PluginAPI::Score wrapper(score);PluginAPI::ScoreObserver reader;reader.setScore(&wrapper);
    const QVariantMap scope{{"start",480},{"end",1440},{"firstTrack",0},{"endTrack",8}};
    auto state=reader.arrangementState({{"scope",scope}});QVariantMap args{{"snapshotId",state["snapshotId"]},{"scope",scope}};
    QVariantMap prepared;for(int i=0;i<1000;++i){prepared=reader.arrangementRequest("arrangementPlaybackPrepare",args);QVERIFY2(!prepared.contains("error"),qUtf8Printable(prepared["error"].toString()));args["playbackRevision"]=prepared["playbackRevision"];if(prepared["ready"].toBool())break;}
    QVERIFY(prepared["ready"].toBool());QVERIFY(prepared["ranges"].toList().size()>=2);
    QStringList actual,expected;int cursor=INT_MIN;int pedalCount=0;
    for(int i=0;i<1000;++i){args["fromTick"]=cursor;const auto page=reader.arrangementRequest("arrangementPlaybackPage",args);QVERIFY(!page.contains("error"));
        for(auto value:page["events"].toList()) {const auto row=value.toMap();const bool control=row["kind"]=="pedal";pedalCount+=control;
            if(!control)QCOMPARE(row["ref"].toMap()["snapshotId"],state["snapshotId"]);
            actual.append(QString("%1:%2:%3:%4").arg(row["utick"].toInt()).arg(row["channel"].toInt()).arg(control?-1:row["pitch"].toInt()).arg(control?row["value"].toInt():row["velocity"].toInt()));}
        if(page["done"].toBool())break;QVERIFY(page["nextTick"].toInt()>cursor);cursor=page["nextTick"].toInt();}
    EventMap events;score->renderMidi(&events,false,true,Ms::mscore->synthesizerState());
    for(const auto &entry:events){const auto &event=entry.second;if(event.getOriginatingStaff()<0||event.getOriginatingStaff()>1)continue;
        const bool control=event.type()==ME_CONTROLLER&&event.controller()==CTRL_SUSTAIN;
        if(!control&&event.type()!=ME_NOTEON)continue;
        if(!control&&!event.note())continue;
        expected.append(QString("%1:%2:%3:%4").arg(entry.first).arg(event.channel()).arg(control?-1:event.pitch()).arg(control?event.value():event.velo()));}
    actual.sort();expected.sort();QCOMPARE(actual,expected);QVERIFY(pedalCount>=2);
    const auto repeat=prepared["ranges"].toList().last().toMap();QVERIFY(repeat["start"].toInt()>480);QVERIFY(repeat["startSeconds"].toDouble()>0.);
    const auto revision=args["playbackRevision"].toString();score->staff(0)->setPlaybackVoice(0,false);
    QVERIFY(reader.analysisPlaybackRevision()!=revision);QVERIFY(reader.arrangementRequest("arrangementPlaybackPage",args).contains("error"));
}

void TestPluginHost::context143LayoutRefresh() {
    auto score=contextScore143(_settings.filePath("context-layout-143.mscx"));QVERIFY(score);
    PluginAPI::Score wrapper(score);PluginAPI::ScoreObserver reader;reader.setScore(&wrapper);
    const auto state=reader.arrangementState({{"whole",true}});const auto original=reader.arrangementImage({{"snapshotId",state["snapshotId"]},{"scope",state["scope"]}});
    auto note=toChord(score->findCR(Fraction::fromTicks(480),0))->upNote();const int pitch=note->pitch();
    score->startCmd();note->undoChangeProperty(Pid::OFFSET,QPointF(6,2));score->endCmd();score->doLayout();
    QVERIFY(reader.arrangementRange({{"snapshotId",state["snapshotId"]},{"scope",state["scope"]}}).contains("error"));
    const auto next=reader.arrangementState({{"scope",state["scope"]}});
    const auto moved=reader.arrangementImage({{"snapshotId",next["snapshotId"]},{"scope",next["scope"]}});
    QVERIFY(original["pngBase64"]!=moved["pngBase64"]);QCOMPARE(note->pitch(),pitch);
    QCOMPARE(next["parts"],state["parts"]);QCOMPARE(next["staves"],state["staves"]);
}

void TestPluginHost::context143Transport() {
    const auto folder=qEnvironmentVariable("P2_CONTEXT_143_CONTROL");
    if(folder.isEmpty())QSKIP("Run with the frozen context client.");
    auto score=contextScore143(folder+"/source.mscx");QVERIFY(score);
    score->setExpandRepeats(true);
    auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);
    pedal->setTick(Fraction());pedal->setTick2(score->lastMeasure()->endTick());
    score->startCmd();score->undoAddElement(pedal);score->endCmd();
    PluginAPI::Score wrapper(score);PluginAPI::ScoreObserver reader;reader.setScore(&wrapper);
    const QVariantMap nearbyAssociation{{"id","near"},{"start",480},{"end",1440},{"from",20000},{"until",30000},
        {"anchors",QVariantList{QVariantMap{{"tick",960},{"ms",24500}}}}};
    const QVariantMap remoteAssociation{{"id","away"},{"start",1440},{"end",2400},{"from",30000},{"until",40000}};
    const QVariantMap track{{"id","recording"},{"name","Reference recording"},{"path","E:/audio/reference.wav"},
        {"associations",QVariantList{nearbyAssociation,remoteAssociation}}};
    QVERIFY(!reader.setWorkflowData("reference",{{"schema",2},{"tracks",QVariantList{track}}}).contains("error"));
    auto note=toChord(score->findCR(Fraction::fromTicks(480),4))->upNote();score->select(note);
    mscore->endCmd();QVERIFY(mscore->saveAs(score,false,folder+"/original.mscz","mscz"));
    guiScaling=1.0;MuseScore::updateUiStyleAndTheme();mscore->resize(1200,820);mscore->show();
    initializeBuiltinAI();
    auto write=[&](const QString &name,const QJsonObject &value){QSaveFile file(folder+"/"+name);
        if(!file.open(QIODevice::WriteOnly))return false;file.write(QJsonDocument(value).toJson());return file.commit();};
    QVERIFY(write("ready.json",QJsonObject::fromVariantMap(reader.scoreIdentity())));
    int handled=0;QElapsedTimer timeout;timeout.start();
    while(timeout.elapsed()<180000) {
        QFile file(folder+"/command.json");
        if(file.open(QIODevice::ReadOnly)) {
            const auto job=QJsonDocument::fromJson(file.readAll()).object();file.close();const int id=job["id"].toInt();
            if(id>handled) {
                handled=id;const auto action=job["action"].toString();
                if(action=="finish"){QVERIFY(write("reply.json",{{"id",id},{"ok",true}}));break;}
                if(action=="browse")score->select(toChord(score->findCR(Fraction::fromTicks(1920),8))->upNote());
                else if(action=="move") {score->startCmd();note->undoChangeProperty(Pid::OFFSET,QPointF(5,1));score->endCmd();score->doLayout();}
                else if(action=="saveAs")QVERIFY(mscore->saveAs(score,false,folder+"/saved.mscz","mscz"));
                else if(action=="screenshot"){QTest::qWait(150);QVERIFY(mscore->grab().save(folder+"/suggestion.png"));}
                auto value=reader.scoreIdentity();value["id"]=id;value["ok"]=true;value["undoIndex"]=score->undoStack()->getCurIdx();
                value["selectedPitch"]=score->selection().element()&&score->selection().element()->isNote()?toNote(score->selection().element())->pitch():-1;
                QVERIFY(write("reply.json",QJsonObject::fromVariantMap(value)));
            }
        }
        QTest::qWait(20);
    }
    QVERIFY(timeout.elapsed()<180000);shutdownBuiltinAI();
}
