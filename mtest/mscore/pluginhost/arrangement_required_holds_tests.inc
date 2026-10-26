// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
void TestPluginHost::arrangementRequiredKey()
{
    auto key=[](int pitch,int from,int end,const QString& id){return QVariantMap{{"id",id},{"analysisId",id},{"pitch",pitch},{"track",4},{"physicalStart",from},{"physicalEnd",end},
        {"physicalOnSeconds",from/960.},{"physicalEndSeconds",end/960.},{"playbackChannel","0:0"},{"analysisHand","left"},{"handKnown",true}};};
    auto old=key(60,0,960,"held"),next=key(36,240,480,"new");old["requiredUntil"]=480;old["requiredSeconds"]=.5;
    const QVariantList pedals{QVariantMap{{"tick",0},{"seconds",0.},{"end",1920},{"endSeconds",2.},{"channelKey","0:0"},{"supported",true}}};
    auto run=[&](QVariantMap a,QVariantMap b,int tick){PluginAPI::PianoHandAnalysis p;return p.analyse({a,b},pedals,{{"tick",tick},{"seconds",tick/960.}}).value("sides").toList().front().toMap();};
    auto result=run(old,next,240);QVERIFY2(result.value("released").toList().isEmpty(),"A later written fingering requires the same key to remain held");QVERIFY(!result.value("after").toMap().value("fits").toBool());
    next=key(36,480,720,"at-change");result=run(old,next,480);QVERIFY(result.value("released").toList().isEmpty());
    next=key(36,600,840,"after-change");result=run(old,next,600);QCOMPARE(result.value("released").toList().size(),1);
    PluginAPI::PianoHandAnalysis p;p.analyse({old},pedals,{{"tick",0},{"seconds",0.}});result=p.analyse({old,next},pedals,{{"tick",600},{"seconds",.625}}).value("sides").toList().front().toMap();
    QVERIFY(qAbs(result.value("pedalAvailable").toDouble()-.125)<1.e-8);
    old["requiredUntil"]=720;old["requiredSeconds"]=.75;result=run(old,next,600);QVERIFY(result.value("released").toList().isEmpty());
    old.remove("requiredUntil");old.remove("requiredSeconds");result=run(old,next,600);QCOMPARE(result.value("released").toList().size(),1);
}

void TestPluginHost::arrangementRequiredRange()
{
    QVariantList notes;notes.append(QVariant::fromValue(QVariantList{60,0,1920,0,14}));
    auto xml=arrangementScore115(notes);QVERIFY(!xml.isEmpty());
    xml.insert(xml.indexOf("</Measure></Staff>"),"<voice>"+arrangementRests115(480)+"<Chord><durationType>quarter</durationType><Note><pitch>36</pitch><tpc>14</tpc></Note></Chord>"+arrangementRests115(960)+"</voice>");
    xml.insert(xml.indexOf("</Measure></Staff>")+int(QByteArray("</Measure>").size()),"<Measure><voice><Chord><durationType>whole</durationType><Note><pitch>60</pitch><tpc>14</tpc></Note></Chord></voice></Measure>");
    xml.insert(xml.lastIndexOf("</Staff>"),"<Measure><voice>"+arrangementRests115(1920)+"</voice></Measure>");
    const auto path=_settings.filePath("required-outside-range.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    QVERIFY(score->firstMeasure());QVERIFY(score->firstMeasure()->nextMeasure());
    auto first=toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->upNote();auto second=toChord(score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest)->element(0))->upNote();
    score->startCmd();auto tie=new Tie(score);tie->setStartNote(first);tie->setEndNote(second);score->undoAddElement(tie);
    auto finger=[&](Note* note,const QString& text){auto value=new Fingering(score);value->setParent(note);value->setTrack(note->track());value->setPlainText(text);score->undoAddElement(value);return value;};
    finger(first,"1");auto future=finger(second,"2");auto pedal=new Pedal(score);pedal->setTrack(0);pedal->setTrack2(0);pedal->setTick(Fraction());pedal->setTick2(Fraction::fromTicks(3840));score->undoAddElement(pedal);score->endCmd();main->endCmd();
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);const auto bundle=tiedTraceBundle(arrangementBundle083());auto config=bundle.value("config").toMap();
    config["harmony"]=false;config["counterpoint"]=false;config["autoRefresh"]=false;config["roles"]=QVariantMap{{"0",QVariantMap{{"hand","left"}}},{"1",QVariantMap{{"hand","left"}}}};
    auto run=[&]{auto input=physicalContextInput(observer,1920,8);for(const auto& value:input.value("notes").toList())if(value.toMap().value("tick").toInt()>=1920)return QVariantMap{{"error","snapshot-included-outside-note"}};return physicalContextRun(observer,bundle,input,config);};
    auto has=[](const QVariantList& issues,const QString& code){for(const auto& value:issues)if(value.toMap().value("code")==code)return true;return false;};
    auto protectedResult=run();QVERIFY2(!protectedResult.contains("error"),qPrintable(protectedResult.value("error").toString()));QVERIFY(has(protectedResult.value("issues").toList(),"span-left"));QVERIFY(!has(protectedResult.value("issues").toList(),"release-left"));
    score->select(first);main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel=arrangementPanel116();QVERIFY(panel);
    struct Close{QPointer<QmlPlugin> panel;QVariantMap config;~Close(){if(panel){panel->setProperty("config",config);panel->disposePanelForReload();}QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}}close{panel,panel->property("config").value<QJSValue>().toVariant().toMap()};
    panel->setPanelFloating(true);panel->resizePanel(460,760);panel->setProperty("config",config);QTest::qWait(100);
    auto click=[&](const char* name){auto item=panel->findChild<QQuickItem*>(name);if(!item||!item->isVisible())return false;QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;};
    auto issues=[&]{return panel->property("issues").value<QJSValue>().toVariant().toList();};
    auto span=[&]{const auto rows=issues();for(int i=0;i<rows.size();++i)if(rows[i].toMap().value("code")=="span-left")return i;return -1;};
    QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),15000);QCOMPARE(panel->property("scope").value<QJSValue>().toVariant().toMap().value("end").toInt(),1920);QVERIFY(span()>=0);QVERIFY(!has(issues(),"release-left"));
    panel->setProperty("currentIssueIndex",span());const auto current=issues()[span()].toMap();QVERIFY(current.value("basis").toString().contains(QString::fromUtf8("第2小节第1拍")));QVERIFY(current.value("basis").toString().contains(QString::fromUtf8("2指")));QVERIFY(click("arrangementLocate"));
    const auto selected=score->selection().elements();QVERIFY(!selected.isEmpty());QVERIFY(selected.front()->isNote());QCOMPARE(toNote(selected.front())->chord()->tick().ticks(),480);
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::mouseMove(panel->window(),QPoint(5,5));QTest::qWait(100);QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-required-range.png")));
    score->startCmd();score->undoRemoveElement(future);score->endCmd();main->endCmd();QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(has(issues(),"release-left"));
    main->cmd(getAction("undo"));QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(!has(issues(),"release-left"));QVERIFY(span()>=0);
    score->startCmd();score->undoRemoveElement(future);auto hand=new Symbol(score);hand->setSym(SymId::keyboardPlayWithLH);hand->applyHandPreset();hand->setParent(second);hand->setTrack(second->track());score->undoAddElement(hand);score->endCmd();main->endCmd();
    auto handResult=run();QVERIFY(!handResult.contains("error"));QVERIFY(has(handResult.value("issues").toList(),"span-left"));QVERIFY(!has(handResult.value("issues").toList(),"release-left"));
    bool handReason=false;for(const auto& value:handResult.value("issues").toList())handReason|=value.toMap().value("basis").toString().contains(QString::fromUtf8("第2小节第1拍标有分手记号"));QVERIFY(handReason);
    main->cmd(getAction("undo"));
    score->startCmd();score->firstMeasure()->undoChangeProperty(Pid::REPEAT_START,true);score->lastMeasure()->undoChangeProperty(Pid::REPEAT_END,true);score->lastMeasure()->undoChangeProperty(Pid::REPEAT_COUNT,2);score->setExpandRepeats(true);score->endCmd();main->endCmd();auto repeated=run();QVERIFY(!repeated.contains("error"));
    QSet<int> positions;for(const auto& value:repeated.value("issues").toList())if(value.toMap().value("code")=="span-left")positions.insert(value.toMap().value("utick").toInt());QVERIFY(positions.contains(480)&&positions.contains(4320));QVERIFY(!has(repeated.value("issues").toList(),"release-left"));
    QVERIFY(main->saveFile());TemporaryScore reopened(main->readScore(path));QVERIFY(reopened);PluginAPI::Score restored(reopened.get());observer.setScore(&restored);auto saved=run();QVERIFY(!saved.contains("error"));QVERIFY(has(saved.value("issues").toList(),"span-left"));QVERIFY(!has(saved.value("issues").toList(),"release-left"));
    QFile report(out.filePath("arrangement-required-range.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(QVariantMap{{"protected",protectedResult},{"hand",handResult},{"repeated",repeated},{"saved",saved}}).toJson());
}
