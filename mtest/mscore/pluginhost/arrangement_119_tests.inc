// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
static MasterScore* arrangementRepeated119(MuseScore* main,const QString& path)
{
    QVariantList notes{QVariant(QVariantList{72,0,1920,0,14})};
    for(int tick:{0,480,960,1440})for(int pitch:{36,37})notes.append(QVariant(QVariantList{pitch,tick,tick+480,4,pitch==36?14:21}));
    QFile file(path);if(!file.open(QIODevice::WriteOnly))return nullptr;file.write(arrangementScore115(notes));file.close();
    auto score=main->readScore(path);if(!score)return nullptr;main->setCurrentScoreView(main->appendScore(score));
    auto segment=score->tick2segment(Fraction::fromTicks(960),false,SegmentType::ChordRest);
    auto tempo=new TempoText(score);tempo->setParent(segment);tempo->setTrack(0);tempo->setTempo(1);tempo->setXmlText("<sym>metNoteQuarterUp</sym> = 60");
    score->startCmd();score->undoAddElement(tempo);score->endCmd();main->endCmd();return score;
}
void TestPluginHost::arrangementTiming119()
{
    auto main=Ms::mscore;auto score=arrangementRepeated119(main,_settings.filePath("arrangement-clock-119.mscx"));QVERIFY(score);
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    const auto state=score->state();const int undo=score->undoStack()->getCurIdx();
    const auto page=observer.analysisRange(480,1440,0,8,480,64,observer.analysisRevision());QVERIFY(!page.contains("error"));
    const auto points=page.value("tempos").toList();QCOMPARE(points.size(),3);
    QCOMPARE(points[0].toMap().value("tick").toInt(),480);QCOMPARE(points[0].toMap().value("bpm").toDouble(),120.);
    QCOMPARE(points[1].toMap().value("tick").toInt(),960);QCOMPARE(points[1].toMap().value("bpm").toDouble(),60.);
    QCOMPARE(points[2].toMap().value("tick").toInt(),1440);
    for(const auto& value:points){auto row=value.toMap();QCOMPARE(row.value("seconds").toDouble(),score->tempomap()->tick2time(row.value("tick").toInt()));}
    QCOMPARE(points[2].toMap().value("seconds").toDouble()-points[0].toMap().value("seconds").toDouble(),1.5);
    QVERIFY(score->state()==state);QCOMPARE(score->undoStack()->getCurIdx(),undo);
    auto later=observer.analysisRange(480,1440,0,8,960,64,observer.analysisRevision());QVERIFY(later.value("tempos").toList().isEmpty());
}
void TestPluginHost::arrangementRepeatedHints119()
{
    auto main=Ms::mscore;auto score=arrangementRepeated119(main,_settings.filePath("arrangement-repeated-119.mscx"));QVERIFY(score);
    main->resize(1280,920);main->show();score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->upNote());
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel=arrangementPanel116();QVERIFY(panel);
    panel->setPanelFloating(true);panel->resizePanel(430,740);QTest::qWait(100);
    struct Close{QPointer<QmlPlugin> panel;~Close(){if(panel)panel->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}} close{panel};
    auto value=[&](const char* name){return panel->property(name).value<QJSValue>().toVariant();};
    auto click=[&](const char* name){auto item=panel->findChild<QQuickItem*>(name);if(!item||!item->isVisible()||!item->isEnabled())return false;QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;};
    auto low=[&]{QVariantList found;for(const auto& issue:value("issues").toList())if(issue.toMap().value("code")=="low-cluster")found.append(issue);return found;};
    QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),10000);
    QCOMPARE(low().size(),4);QCOMPARE(value("issueRows").toList().size(),1);QCOMPARE(value("counts").toMap().value("harmony").toInt(),1);
    QCOMPARE(value("currentRow").toMap().value("occurrences").toList().size(),4);
    QCOMPARE(low()[0].toMap().value("lowDetails").toList()[0].toMap().value("seconds").toDouble(),.5);
    QCOMPARE(low()[2].toMap().value("lowDetails").toList()[0].toMap().value("seconds").toDouble(),1.);
    auto config=value("config").toMap();config["scoreHints"]=true;panel->setProperty("config",config);QVERIFY(QMetaObject::invokeMethod(panel,"publishMarkers"));QTRY_COMPARE(main->currentScoreView()->notePreviewMarkerBoxes().size(),4);
    QVERIFY(click("arrangementNextOccurrence"));QCOMPARE(value("current").toMap().value("tick").toInt(),480);
    QVERIFY(click("arrangementNextOccurrence"));QCOMPARE(value("current").toMap().value("tick").toInt(),960);QCOMPARE(panel->property("occurrenceIndex").toInt(),2);
    auto label=panel->findChild<QQuickItem*>("arrangementOccurrenceLabel");QVERIFY(label);QCOMPARE(label->property("text").toString(),QString("3 / 4"));
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-repeated-119.png")));
    QVERIFY(click("arrangementIgnoreIssue"));QTRY_COMPARE_WITH_TIMEOUT(low().size(),3,10000);QTRY_VERIFY(!panel->property("running").toBool());
    for(const auto& issue:low())QVERIFY(issue.toMap().value("tick").toInt()!=960);
    QCOMPARE(value("issueRows").toList().size(),1);QTRY_COMPARE(main->currentScoreView()->notePreviewMarkerBoxes().size(),3);
    panel->setProperty("categoryMask",0);QCOMPARE(value("issueRows").toList().size(),0);panel->setProperty("categoryMask",7);QCOMPARE(value("issueRows").toList().size(),1);
    auto note=toChord(score->tick2segment(Fraction::fromTicks(1440),false,SegmentType::ChordRest)->element(4))->upNote();
    score->startCmd();note->undoChangeProperty(Pid::PLAY,false);score->endCmd();main->endCmd();
    QTRY_COMPARE_WITH_TIMEOUT(low().size(),2,10000);QTRY_VERIFY(!panel->property("running").toBool());QCOMPARE(value("currentRow").toMap().value("occurrences").toList().size(),2);
    main->cmd(getAction("undo"));QTRY_COMPARE_WITH_TIMEOUT(low().size(),3,10000);QTRY_VERIFY(!panel->property("running").toBool());
    auto segment=score->tick2segment(Fraction::fromTicks(960),false,SegmentType::ChordRest);TempoText* tempo=nullptr;
    for(auto element:segment->annotations())if(element->isTempoText())tempo=toTempoText(element);QVERIFY(tempo);
    auto lastDuration=[&]{for(const auto& value:low()){const auto issue=value.toMap();if(issue.value("tick").toInt()==1440)return issue.value("lowDetails").toList()[0].toMap().value("seconds").toDouble();}return -1.;};
    score->startCmd();static_cast<Element*>(tempo)->undoChangeProperty(Pid::TEMPO,2.);score->endCmd();main->endCmd();QTRY_COMPARE_WITH_TIMEOUT(lastDuration(),.5,10000);
    main->cmd(getAction("undo"));QTRY_COMPARE_WITH_TIMEOUT(lastDuration(),1.,10000);QTRY_VERIFY(!panel->property("running").toBool());
    QVERIFY(main->saveFile());auto reopened=main->readScore(score->fileInfo()->absoluteFilePath());QVERIFY(reopened);QCOMPARE(reopened->tempomap()->tempo(960),1.);delete reopened;
}
