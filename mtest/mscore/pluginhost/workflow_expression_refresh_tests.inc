// GPL-2.0-or-later. Keep comparison in step with actual score undo and redo.
void TestPluginHost::expressionScoreEditRefresh151_data()
{
    QTest::addColumn<QString>("writeMode");
    QTest::newRow("fixed")<<QString("fixed");QTest::newRow("follow")<<QString("follow");
}
void TestPluginHost::expressionScoreEditRefresh151()
{
    QFETCH(QString,writeMode);auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);
    auto score=performanceScore123(_settings.filePath("expression-refresh-"+writeMode+".mscx"),4);QVERIFY(score);
    const auto close=qScopeGuard([&]{releaseHostPanels();score->undoStack()->setClean();main->closeScore(score);});
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();showExpressionPanel();
    auto editor=main->performanceEditor();auto page=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(page);
    auto root=qobject_cast<QmlPlugin*>(page->property("root").value<QObject*>());QVERIFY(root);
    auto map=[&](const char* name){const auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toMap():value.toMap();};
    auto list=[&](const char* name){const auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toList():value.toList();};
    auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);
    const auto midi=_settings.filePath("expression-refresh-"+writeMode+".mid");
    {TemporaryScore copy(score->clone());QMap<Note*,VelocityEdit> values;
    for(auto segment=copy->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int track=0;track<copy->ntracks();++track)
        if(auto element=segment->element(track))if(element->isChord())for(auto note:toChord(element)->notes())values.insert(note,{Note::ValueType::USER_VAL,106});
    QVERIFY(ParameterEdit::velocities(copy.get(),values));QFile file(midi);QVERIFY(file.open(QIODevice::WriteOnly));ExportMidi exporter(copy.get());QVERIFY(exporter.write(&file,true,true,main->synthesizerState()));}
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi).toString())));
    auto ready=[&]{return !root->property("busy").toBool()&&!root->property("stale").toBool()&&!root->property("previewUpdating").toBool()&&!map("candidate").isEmpty();};
    QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);const int total=map("candidate").value("velocity").toList().size();QVERIFY(total>=10);
    root->setProperty("selected",3);QVERIFY(QMetaObject::invokeMethod(root,"confirm"));QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
    QVERIFY(QMetaObject::invokeMethod(root,"anchor"));QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"velocityWriteMode"),Q_ARG(QVariant,writeMode)));QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
    const auto match=map("result").value("matches").toList()[3].toMap();
    const QVariant row=QVariantMap{{"kind","match"},{"index",3},{"match",match}};
    QVERIFY(QMetaObject::invokeMethod(root,"selectResultRow",Q_ARG(QVariant,row)));
    root->setProperty("applicationScope",0);const auto selection=observer->expressionSelection();QCOMPARE(selection.value("notes").toList().size(),1);
    const auto scope=map("scope"),retained=map("retainedScope");const auto links=list("links"),anchors=list("manualAnchors");const auto selected=root->property("selected").toInt();
    const auto original=match.value("original").toMap();auto chord=toChord(score->findCR(Fraction::fromTicks(original.value("tick").toInt()),original.value("track").toInt()));QVERIFY(chord);auto note=chord->notes()[original.value("index").toInt()];
    const auto oldType=note->veloType();const auto oldRaw=note->veloOffset();const auto before=score->state();
    QVERIFY(QMetaObject::invokeMethod(root,"applyPart",Q_ARG(QVariant,"velocity")));QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
    QCOMPARE(map("candidate").value("velocity").toList().size(),total-1);QVERIFY(note->veloType()!=oldType||note->veloOffset()!=oldRaw);
    QSignalSpy jobs(observer,SIGNAL(readOnlyJobFinished(int,QVariantMap)));
    getAction("undo")->trigger();main->endCmd();
    QCOMPARE(note->veloType(),oldType);QCOMPARE(note->veloOffset(),oldRaw);QVERIFY(score->state()==before);
    QTRY_VERIFY_WITH_TIMEOUT(ready()&&map("candidate").value("velocity").toList().size()==total,10000);
    QCOMPARE(map("scope"),scope);QCOMPARE(map("retainedScope"),retained);QCOMPARE(list("links"),links);QCOMPARE(list("manualAnchors"),anchors);QCOMPARE(root->property("selected").toInt(),selected);
    QCOMPARE(observer->expressionSelection().value("notes"),selection.value("notes"));
    QVERIFY(jobs.size()<=2);for(auto row:jobs)QVERIFY2(!row[1].toMap().contains("matches"),"Velocity-only undo must reuse correspondence");jobs.clear();
    getAction("redo")->trigger();main->endCmd();QTRY_VERIFY_WITH_TIMEOUT(ready()&&map("candidate").value("velocity").toList().size()==total-1,10000);
    QCOMPARE(map("scope"),scope);QCOMPARE(list("links"),links);QCOMPARE(list("manualAnchors"),anchors);QCOMPARE(root->property("selected").toInt(),selected);
    // Hidden tools do no work; opening resumes the same scope and correspondence.
    main->showPerformanceEditor(false);QTest::qWait(80);qInfo()<<"151 hidden surface"<<page->isVisible()<<root->isVisible()<<root->window()->isVisible()<<observer->surfaceVisible();jobs.clear();getAction("undo")->trigger();main->endCmd();QTest::qWait(350);QCOMPARE(jobs.size(),0);
    showExpressionPanel();QTRY_VERIFY_WITH_TIMEOUT(ready()&&map("candidate").value("velocity").toList().size()==total,10000);
    QCOMPARE(map("retainedScope"),retained);QCOMPARE(list("links"),links);QCOMPARE(list("manualAnchors"),anchors);
    // Quick edits coalesce, while the original side follows the score's real value.
    jobs.clear();for(int value:{74,75,76}){score->startCmd();note->undoChangeProperty(Pid::VELO_TYPE,int(Note::ValueType::USER_VAL));note->undoChangeProperty(Pid::VELO_OFFSET,value);score->endCmd();main->endCmd();}
    auto currentOriginal=[&]{for(auto row:map("result").value("matches").toList()){auto match=row.toMap();if(match.value("scoreId")==original.value("id"))return match.value("original").toMap();}return QVariantMap();};
    QTRY_VERIFY_WITH_TIMEOUT(ready()&&currentOriginal().value("velocityRaw").toInt()==76,10000);QVERIFY(jobs.size()<=2);
    for(auto row:jobs)QVERIFY(!row[1].toMap().contains("matches"));
    QCOMPARE(list("links"),links);QCOMPARE(list("manualAnchors"),anchors);QCOMPARE(root->property("selected").toInt(),selected);
    // Tempo changes refresh both timing and reference values without moving the pairing.
    auto tempo=new TempoText(score);tempo->setTrack(0);tempo->setParent(score->firstSegment(SegmentType::ChordRest));tempo->setXmlText("<sym>metNoteQuarterUp</sym> = 90");tempo->setTempo(1.5);
    score->startCmd();score->undoAddElement(tempo);score->endCmd();main->endCmd();
    QTRY_VERIFY_WITH_TIMEOUT(ready()&&qAbs(currentOriginal().value("bpm").toDouble()-90)<.01,10000);
    getAction("undo")->trigger();main->endCmd();QTRY_VERIFY_WITH_TIMEOUT(ready()&&qAbs(currentOriginal().value("bpm").toDouble()-120)<.01,10000);
    // Changing the notes does recalculate matching, retaining the user's other connections.
    jobs.clear();const int oldPitch=note->pitch();score->startCmd();score->undoChangePitch(note,oldPitch+12,note->tpc1(),note->tpc2());score->endCmd();main->endCmd();
    QTRY_VERIFY_WITH_TIMEOUT(ready()&&currentOriginal().value("writtenPitch").toInt()==oldPitch+12,10000);
    bool rematched=false;for(auto row:jobs)rematched|=row[1].toMap().contains("matches");QVERIFY(rematched);
    QCOMPARE(list("links"),links);QCOMPARE(list("manualAnchors"),anchors);QCOMPARE(map("retainedScope"),retained);QCOMPARE(root->property("selected").toInt(),selected);
    getAction("undo")->trigger();main->endCmd();QTRY_VERIFY_WITH_TIMEOUT(ready()&&currentOriginal().value("writtenPitch").toInt()==oldPitch,10000);
    const auto workspace=_settings.filePath("refresh-"+writeMode+".json");QVERIFY(QMetaObject::invokeMethod(root,"save",Q_ARG(QVariant,QUrl::fromLocalFile(workspace).toString())));
    QFile project(workspace);QVERIFY(project.open(QIODevice::ReadOnly));const auto saved=QJsonDocument::fromJson(project.readAll()).object().value("expression").toObject();
    QVERIFY(!saved.value("applied").toObject().value("velocity").toObject().isEmpty());
    if(qEnvironmentVariable("QT_QUICK_BACKEND")=="software")QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("expression-refresh151-"+writeMode+".png")));
    qInfo()<<"151 expression refresh"<<writeMode<<"batch maximum ms"<<root->property("worstBatch")<<"pending"<<total;
}
