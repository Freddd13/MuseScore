// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "libmscore/pianofingering.h"
void TestPluginHost::arrangementFingerMatchingCorpus()
{
    QCOMPARE(PianoFingering::choices("2(3)"),6);QCOMPARE(PianoFingering::choices("2(3)(4)"),14);QCOMPARE(PianoFingering::choices("[4]"),8);
    QCOMPARE(PianoFingering::choices(" 2 ( 3 ) "),6);QCOMPARE(PianoFingering::choices("1-3"),0);QCOMPARE(PianoFingering::choices("6"),0);QCOMPARE(PianoFingering::choices("23"),0);
    // Independent exhaustive assignments and kept-key subsets provide the
    // expected minimum number of releases for the small five-finger problem.
    quint32 seed=28719;auto random=[&]{seed=1664525*seed+1013904223;return seed;};
    for(int example=0;example<192;++example){
        const int count=2+example%7;QVector<PianoHands::Key> keys;int mandatory=0;
        for(int i=0;i<count;++i){PianoHands::Key key;key.id=i;key.pitch=60+i;key.hand=key.hint=1;key.attack=random()%4==0;key.releasable=!key.attack;key.fingerMask=1+int((random()>>8)%31);keys.append(key);if(key.attack)mandatory|=1<<i;}
        int best=INT_MAX;
        for(int kept=0;kept<(1<<count);++kept){
            if((kept&mandatory)!=mandatory)continue;QVector<int> masks;for(int i=0;i<count;++i)if(kept&(1<<i))masks.append(keys[i].fingerMask);if(masks.size()>5)continue;
            std::function<bool(int,int)> possible=[&](int index,int used){if(index==masks.size())return true;for(int f=0;f<5;++f)if((masks[index]&(1<<f))&&!(used&(1<<f))&&possible(index+1,used|(1<<f)))return true;return false;};
            if(possible(0,0))best=qMin(best,count-masks.size());
        }
        PianoHands::Limits limits[2];PianoHands::Position positions[2];auto result=PianoHands::plan(keys,limits,positions,0.);
        QCOMPARE(result.possible,best!=INT_MAX);if(result.possible){QCOMPARE(result.released.size(),best);for(const auto index:result.released)QVERIFY(!(mandatory&(1<<index)));}
    }
}

void TestPluginHost::arrangementAlternativeFingers()
{
    auto key=[](int pitch,const QString& text,const QString& id){return QVariantMap{{"id",id},{"analysisId",id},{"pitch",pitch},{"track",0},{"physicalStart",0},{"physicalEnd",960},
        {"physicalOnSeconds",0.},{"physicalEndSeconds",1.},{"analysisHand","right"},{"handKnown",true},{"fingerings",QVariantList{text}}};};
    auto run=[](const QVariantList& notes){PluginAPI::PianoHandAnalysis p;return p.analyse(notes,{},{{"tick",0},{"seconds",0.}});};
    auto side=[](const QVariantMap& result){return result.value("sides").toList()[1].toMap().value("after").toMap();};
    auto a=key(60,"2(3)","c"),b=key(64,"2","e"),c=key(67,"3","g");
    auto result=run({a,b,c});QVERIFY2(!side(result).value("fits").toBool(),"Three keys cannot share the two fingers in 2(3), 2, 3");QCOMPARE(side(result).value("fingerConflicts").toInt(),1);
    c["fingerings"]=QVariantList{QString("4")};result=run({a,b,c});QVERIFY(side(result).value("fits").toBool());QVERIFY(result.value("uncertainFingerings").toList().isEmpty());
    a["fingerings"]=QVariantList{QString("[4]")};b["fingerings"]=QVariantList{QString("4")};result=run({a,b});QVERIFY(!side(result).value("fits").toBool());
    a["fingerings"]=QVariantList{QString(" 2 ( 3 ) ")};b["fingerings"]=QVariantList{QString("2")};result=run({a,b});QVERIFY(side(result).value("fits").toBool());
    a["fingerings"]=QVariantList{QString("2(3)")};auto same=a;same["id"]="same";same["analysisId"]="same";same["fingerings"]=QVariantList{QString("3")};
    result=run({a,same});QCOMPARE(side(result).value("count").toInt(),1);QVERIFY(result.value("uncertainFingerings").toList().isEmpty());
    same["fingerings"]=QVariantList{QString("4")};result=run({a,same});QCOMPARE(result.value("uncertainFingerings").toList().size(),2);
    // The pedal may free a held key, while newly struck keys remain mandatory.
    a=key(60,"2(3)","old");b=key(64,"2","next-e");c=key(67,"3","next-g");b["physicalStart"]=480;b["physicalOnSeconds"]=.5;c["physicalStart"]=480;c["physicalOnSeconds"]=.5;
    const QVariantList pedals{QVariantMap{{"tick",0},{"end",1920},{"seconds",0.},{"firstTrack",0},{"endTrack",8},{"supported",true}}};
    PluginAPI::PianoHandAnalysis p;result=p.analyse({a,b,c},pedals,{{"tick",480},{"seconds",.5}});QVERIFY(side(result).value("fits").toBool());QCOMPARE(result.value("sides").toList()[1].toMap().value("released").toList().size(),1);
}

void TestPluginHost::arrangementArpeggioHolds()
{
    const int pitches[]={48,52,55,60,64,67,72,76};QVariantList notes;
    for(int i=0;i<8;++i)notes.append(QVariant::fromValue(QVariantList{pitches[i],i*240,(i+1)*240,4,14+(pitches[i]%12)*7%12}));
    const auto path=_settings.filePath("physical-continuous-arpeggio.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(arrangementScore115(notes));file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);const auto bundle=tiedTraceBundle(arrangementBundle083());auto config=bundle.value("config").toMap();
    config["harmony"]=false;config["counterpoint"]=false;config["roles"]=QVariantMap{{"4",QVariantMap{{"hand","left"}}}};
    auto run=[&]{return physicalContextRun(observer,bundle,physicalContextInput(observer,1920,8),config);};
    auto has=[](const QVariantMap& result,const QString& code){for(const auto& row:result.value("issues").toList())if(row.toMap().value("code")==code)return true;return false;};
    auto ordinary=run();QVERIFY(!ordinary.contains("error"));QVERIFY(!has(ordinary,"span-left"));QVERIFY(!has(ordinary,"keys-left"));
    score->startCmd();auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction());pedal->setTick2(Fraction::fromTicks(1920));score->undoAddElement(pedal);score->endCmd();main->endCmd();
    auto ringing=run();QVERIFY(!ringing.contains("error"));QVERIFY(!has(ringing,"span-left"));QVERIFY(!has(ringing,"keys-left"));
    for(const auto& value:ringing.value("calls").toList())QVERIFY(value.toMap().value("notes").toList().size()<=1);
    score->startCmd();for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))if(auto element=segment->element(4))if(element->isChord()){
        auto chord=toChord(element);NoteEventList events;events.clear();events.append(NoteEvent(0,0,(1920-segment->tick().ticks())*1000/240));score->undo(new ChangeNoteEventList(chord->upNote(),events));
    }score->setPlaylistDirty();score->endCmd();main->endCmd();
    auto held=run();QVERIFY(!held.contains("error"));QVERIFY(has(held,"release-left"));QVERIFY(!has(held,"span-left"));QVERIFY(!has(held,"keys-left"));
    score->startCmd();score->undoRemoveElement(pedal);score->endCmd();main->endCmd();auto strict=run();QVERIFY(!strict.contains("error"));QVERIFY(has(strict,"span-left")||has(strict,"keys-left"));
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QFile report(out.filePath("arrangement-arpeggio-holds.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(QVariantMap{{"ordinary",ordinary},{"pedalRinging",ringing},{"heldWithPedal",held},{"heldWithoutPedal",strict}}).toJson());
}

void TestPluginHost::arrangementAlternativePanel()
{
    const auto path=_settings.filePath("alternative-fingerings.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(arrangementScore115({QVariantList{60,0,1920,0,14},QVariantList{64,0,1920,0,18},QVariantList{67,0,1920,0,15}}));file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(0));QCOMPARE(chord->notes().size(),3);const QString texts[]={"2(3)","2","3"};Fingering* changed=nullptr;
    score->startCmd();for(int i=0;i<3;++i){auto f=new Fingering(score);f->setParent(chord->notes()[i]);f->setTrack(0);f->setPlainText(texts[i]);score->undoAddElement(f);if(i==2)changed=f;}score->endCmd();main->endCmd();score->select(chord->upNote());
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel=arrangementPanel116();QVERIFY(panel);
    struct Close{QPointer<QmlPlugin> panel;QVariantMap config;~Close(){if(panel){panel->setProperty("config",config);panel->disposePanelForReload();}QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}}close{panel,panel->property("config").value<QJSValue>().toVariant().toMap()};
    panel->setPanelFloating(true);panel->resizePanel(460,760);auto config=close.config;config["harmony"]=false;config["counterpoint"]=false;config["autoRefresh"]=false;config["roles"]=QVariantMap{{"0",QVariantMap{{"hand","right"}}}};panel->setProperty("config",config);QTest::qWait(100);
    auto click=[&](const char* name){auto item=panel->findChild<QQuickItem*>(name);if(!item||!item->isVisible())return false;QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;};
    auto rows=[&]{return panel->property("issues").value<QJSValue>().toVariant().toList();};auto index=[&]{const auto issues=rows();for(int i=0;i<issues.size();++i)if(issues[i].toMap().value("code")=="span-right")return i;return -1;};
    QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),15000);QVERIFY(index()>=0);
    panel->setProperty("currentIssueIndex",index());auto issue=rows()[index()].toMap();QVERIFY(issue.value("basis").toString().contains("2(3)"));QVERIFY(issue.value("basis").toString().contains(QString::fromUtf8("备选指法")));QVERIFY(click("arrangementLocate"));QVERIFY(!score->selection().elements().isEmpty());
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::mouseMove(panel->window(),QPoint(5,5));QTest::qWait(100);QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-alternative-fingers.png")));
    score->startCmd();changed->undoChangeProperty(Pid::TEXT,QString("4"));score->endCmd();main->endCmd();QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(index()<0);
    main->cmd(getAction("undo"));QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(index()>=0);
    QVERIFY(main->saveFile());TemporaryScore reopened(main->readScore(path));QVERIFY(reopened);PluginAPI::Score wrapped(reopened.get());PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto result=physicalContextRun(observer,arrangementBundle083(),physicalContextInput(observer,1920,8),config);QVERIFY(!result.contains("error"));bool found=false;for(const auto& value:result.value("issues").toList())found|=value.toMap().value("basis").toString().contains("2(3)");QVERIFY(found);
    QFile report(out.filePath("arrangement-alternative-fingers.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(result).toJson());
}
