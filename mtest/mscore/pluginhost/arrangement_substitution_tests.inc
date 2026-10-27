// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
void TestPluginHost::arrangementSubstitutionTiming()
{
    QCOMPARE(PianoFingering::substitution(QString::fromUtf8(" 1 – 3 ")),(QVector<int>{1,3}));QVERIFY(PianoFingering::substitution("2(3)").isEmpty());
    auto key=[](int pitch,const QString& text,const QString& id,int on,int off){return QVariantMap{{"id",id},{"analysisId",id},{"pitch",pitch},{"track",0},{"physicalStart",on},{"physicalEnd",off},
        {"physicalOnSeconds",on/960.},{"physicalEndSeconds",off/960.},{"analysisHand","right"},{"handKnown",true},{"fingerings",QVariantList{text}}};};
    auto side=[](const QVariantMap& result){return result.value("sides").toList()[1].toMap().value("after").toMap();};
    auto held=key(60,"1-3","held",0,1440),other=key(64,"1","other",0,480);
    PluginAPI::PianoHandAnalysis attack;auto result=attack.analyse({held,other},{},{{"tick",0},{"seconds",0.}});QVERIFY2(!side(result).value("fits").toBool(),"The first finger in 1-3 must be available for the attack");
    other=key(64,"3","other",0,240);PluginAPI::PianoHandAnalysis delayed;
    result=delayed.analyse({held,other},{},{{"tick",0},{"seconds",0.},{"nextChangeSeconds",.5}});QVERIFY(side(result).value("fits").toBool());
    const auto changes=result.value("fingeringChanges").toList();QCOMPARE(changes.size(),1);auto change=changes.front().toMap();QCOMPARE(change.value("from").toInt(),1);QCOMPARE(change.value("to").toInt(),3);QVERIFY(change.value("seconds").toDouble()>.25);QVERIFY(change.value("seconds").toDouble()<.5);QCOMPARE(change.value("after").toList().size(),1);
    auto next=key(67,"1","next",480,960);result=delayed.analyse({held,next},{},{{"tick",480},{"seconds",.5}});QVERIFY(side(result).value("fits").toBool());QCOMPARE(result.value("effectiveFingers").toMap().value("0").toInt(),3);
    other=key(64,"3","other",0,960);PluginAPI::PianoHandAnalysis blocked;
    blocked.analyse({held,other},{},{{"tick",0},{"seconds",0.},{"nextChangeSeconds",.5}});result=blocked.analyse({held,other,next},{},{{"tick",480},{"seconds",.5}});QVERIFY(!side(result).value("fits").toBool());
    // A still pending substitution cannot be bypassed by releasing its key.
    const QVariantList pedals{QVariantMap{{"tick",0},{"seconds",0.},{"end",1920},{"firstTrack",0},{"endTrack",8},{"supported",true}}};
    other["requiredUntil"]=960;other["requiredSeconds"]=1.;
    PluginAPI::PianoHandAnalysis pedal;result=pedal.analyse({held,other},{},{{"tick",0},{"seconds",0.},{"nextChangeSeconds",.2}});
    next=key(67,"1","next",192,960);result=pedal.analyse({held,other,next},pedals,{{"tick",192},{"seconds",.2}});QVERIFY(!side(result).value("fits").toBool());QVERIFY(result.value("sides").toList()[1].toMap().value("released").toList().isEmpty());
    // If the other key may be released under the pedal, schedule that release
    // first; a control point before it keeps both keys in the current frame.
    other.remove("requiredUntil");other.remove("requiredSeconds");
    PluginAPI::PianoHandAnalysis early;result=early.analyse({held,other},pedals,{{"tick",0},{"seconds",0.},{"nextChangeSeconds",.2}});QCOMPARE(result.value("fingeringChanges").toList().size(),1);
    change=result.value("fingeringChanges").toList().front().toMap();QCOMPARE(change.value("released").toList().size(),1);QVERIFY(change.value("seconds").toDouble()>=.12);QVERIFY(change.value("seconds").toDouble()<.2);
    result=early.analyse({held,other},pedals,{{"tick",48},{"seconds",.05},{"nextChangeSeconds",.2}});QCOMPARE(side(result).value("count").toInt(),2);QCOMPARE(result.value("effectiveFingers").toMap().value("0").toInt(),1);
    result=early.analyse({held,other,next},pedals,{{"tick",192},{"seconds",.2}});QCOMPARE(side(result).value("count").toInt(),2);QVERIFY(side(result).value("fits").toBool());
    PluginAPI::PianoHandAnalysis wait;other=key(64,"3","other",0,240);auto third=key(67,"3","next-three",480,960);
    result=wait.analyse({held,other},{},{{"tick",0},{"seconds",0.},{"nextChangeSeconds",.5},{"lookahead",QVariantList{QVariantMap{{"tick",480},{"seconds",.5},{"notes",QVariantList{held,third}}}}}});QVERIFY(result.value("fingeringChanges").toList().isEmpty());
    result=wait.analyse({held,third},{},{{"tick",480},{"seconds",.5}});QVERIFY(side(result).value("fits").toBool());QCOMPARE(result.value("fingeringChanges").toList().size(),1);QVERIFY(result.value("fingeringChanges").toList().front().toMap().value("seconds").toDouble()>1.);
    PluginAPI::PianoHandAnalysis ending;auto shortHeld=key(60,"1-3","short",0,240);other=key(64,"3","other",0,960);result=ending.analyse({shortHeld,other},{},{{"tick",0},{"seconds",0.}});QCOMPARE(result.value("blockedSubstitutions").toList().size(),1);QCOMPARE(result.value("blockedSubstitutions").toList().front().toMap().value("blockers").toList().size(),1);
    PluginAPI::PianoHandAnalysis sequence;auto chain=key(60,"1-2-3","chain",0,1440);result=sequence.analyse({chain},{},{{"tick",0},{"seconds",0.}});QCOMPARE(result.value("fingeringChanges").toList().size(),2);
    PluginAPI::PianoHandAnalysis doubled;auto same=key(60,"1","same",0,1440);result=doubled.analyse({held,same},{},{{"tick",0},{"seconds",0.}});QCOMPARE(side(result).value("count").toInt(),1);QCOMPARE(result.value("fingeringChanges").toList().size(),1);
    auto finish=key(67,"1","next-one",480,960);result=doubled.analyse({held,same,finish},{},{{"tick",480},{"seconds",.5}});QVERIFY(side(result).value("fits").toBool());QCOMPARE(result.value("effectiveFingers").toMap().value("0").toInt(),3);QCOMPARE(result.value("effectiveFingers").toMap().value("1").toInt(),3);
    PluginAPI::PianoHandAnalysis duplicateChanges;same["fingerings"]=QVariantList{QString("1-3")};result=duplicateChanges.analyse({held,same},{},{{"tick",0},{"seconds",0.}});QCOMPARE(result.value("fingeringChanges").toList().size(),1);result=duplicateChanges.analyse({held,same,finish},{},{{"tick",480},{"seconds",.5}});QVERIFY(side(result).value("fits").toBool());
    PluginAPI::PianoHandAnalysis automatic;auto suggested=held;suggested["handKnown"]=false;other=key(64,"3","short-three",0,240);next=key(67,"1","next-thumb",480,960);
    result=automatic.analyse({suggested,other},{},{{"tick",0},{"seconds",0.},{"nextChangeSeconds",.5},{"lookahead",QVariantList{QVariantMap{{"tick",480},{"seconds",.5},{"notes",QVariantList{suggested,next}}}}}});
    QCOMPARE(result.value("assignments").toList().front().toMap().value("hand").toString(),QString("right"));QCOMPARE(result.value("fingeringChanges").toList().front().toMap().value("hand").toString(),QString("right"));
    QVariantList dense;for(int i=0;i<25;++i)dense.append(key(21+i,i==0?"1-3":"",QString::number(i),0,1440));PluginAPI::PianoHandAnalysis limited;result=limited.analyse(dense,{},{{"tick",0},{"seconds",0.}});QVERIFY(result.value("limited").toBool());
    PluginAPI::PianoHandAnalysis uncertain;auto left=key(64,"3","left",0,1440),right=left;left["analysisHand"]="left";right["id"]=right["analysisId"]="right";result=uncertain.analyse({held,left,right,finish},{},{{"tick",480},{"seconds",.5}});QCOMPARE(result.value("uncertainHands").toList().size(),2);QVERIFY(result.value("substitutionUncertain").toBool());
}

void TestPluginHost::arrangementSubstitutionPanel()
{
    QVariantList notes;notes.append(QVariant::fromValue(QVariantList{60,0,960,0,14}));auto xml=arrangementScore115(notes);
    xml.insert(xml.indexOf("</Measure></Staff>"),"<voice><Chord><durationType>eighth</durationType><Note><pitch>64</pitch><tpc>18</tpc></Note></Chord>"+arrangementRests115(240)+"<Chord><durationType>quarter</durationType><Note><pitch>67</pitch><tpc>15</tpc></Note></Chord>"+arrangementRests115(960)+"</voice>");
    const auto path=_settings.filePath("substitution-panel.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    auto segment=score->firstSegment(SegmentType::ChordRest);auto held=toChord(segment->element(0))->upNote(),other=toChord(segment->element(1))->upNote();auto next=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(1))->upNote();
    score->startCmd();auto finger=[&](Note* note,const QString& text){auto value=new Fingering(score);value->setParent(note);value->setTrack(note->track());value->setPlainText(text);score->undoAddElement(value);return value;};auto change=finger(held,"1-3");finger(other,"3");finger(next,"1");score->endCmd();main->endCmd();score->select(held);
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel=arrangementPanel116();QVERIFY(panel);
    struct Close{QPointer<QmlPlugin> panel;QVariantMap config;~Close(){if(panel){panel->setProperty("config",config);panel->disposePanelForReload();}QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}}close{panel,panel->property("config").value<QJSValue>().toVariant().toMap()};
    panel->setPanelFloating(true);panel->resizePanel(460,760);auto config=close.config;config["harmony"]=false;config["counterpoint"]=false;config["autoRefresh"]=false;config["roles"]=QVariantMap{{"0",QVariantMap{{"hand","right"}}},{"1",QVariantMap{{"hand","right"}}}};panel->setProperty("config",config);QTest::qWait(100);
    auto click=[&](const char* name){auto item=panel->findChild<QQuickItem*>(name);if(!item||!item->isVisible())return false;QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;};
    auto rows=[&]{return panel->property("issues").value<QJSValue>().toVariant().toList();};auto index=[&](const QString& code){const auto issues=rows();for(int i=0;i<issues.size();++i)if(issues[i].toMap().value("code")==code)return i;return -1;};
    QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),15000);QVERIFY(index("fingering-change-right")>=0);QVERIFY(index("span-right")<0);
    panel->setProperty("currentIssueIndex",index("fingering-change-right"));auto issue=rows()[index("fingering-change-right")].toMap();QVERIFY(issue.value("basis").toString().contains("E4"));QVERIFY(issue.value("basis").toString().contains(QString::fromUtf8("松键后")));QVERIFY(click("arrangementLocate"));QVERIFY(!score->selection().elements().isEmpty());
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::mouseMove(panel->window(),QPoint(5,5));QTest::qWait(100);QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-substitution.png")));
    score->startCmd();change->undoChangeProperty(Pid::TEXT,QString("1"));score->endCmd();main->endCmd();QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(index("span-right")>=0);
    main->cmd(getAction("undo"));QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(index("fingering-change-right")>=0);QVERIFY(index("span-right")<0);
    NoteEventList shortKey;shortKey.clear();shortKey.append(NoteEvent(0,0,200));score->startCmd();score->undo(new ChangeNoteEventList(held,shortKey));score->endCmd();main->endCmd();QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(index("fingering-substitution")>=0);
    panel->setProperty("currentIssueIndex",index("fingering-substitution"));issue=rows()[index("fingering-substitution")].toMap();QVERIFY(issue.value("basis").toString().contains(QString::fromUtf8("3指还需要按住 E4")));QVERIFY(click("arrangementLocate"));QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-substitution-blocked.png")));
    main->cmd(getAction("undo"));QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QVERIFY(index("fingering-substitution")<0);QVERIFY(index("fingering-change-right")>=0);
    score->startCmd();score->firstMeasure()->undoChangeProperty(Pid::REPEAT_START,true);score->firstMeasure()->undoChangeProperty(Pid::REPEAT_END,true);score->firstMeasure()->undoChangeProperty(Pid::REPEAT_COUNT,2);score->setExpandRepeats(true);score->endCmd();main->endCmd();
    QVERIFY(main->saveFile());TemporaryScore reopened(main->readScore(path));QVERIFY(reopened);PluginAPI::Score wrapped(reopened.get());PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto result=physicalContextRun(observer,tiedTraceBundle(arrangementBundle083()),physicalContextInput(observer,1920,8),config);QVERIFY(!result.contains("error"));QSet<int> instances;
    for(const auto& value:result.value("issues").toList()){const auto row=value.toMap();if(row.value("code")=="fingering-change-right")instances.insert(row.value("utick").toInt());QVERIFY(row.value("code")!="span-right");}QVERIFY(instances.contains(0)&&instances.contains(1920));
    QFile report(out.filePath("arrangement-substitution.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(result).toJson());
}
