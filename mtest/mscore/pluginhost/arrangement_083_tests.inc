// Test the same JS bundle and native hand analysis used by the actual panel.
static QVariantMap arrangementBundle083()
{
    QVariantMap sources;
    for(const auto& item:QList<QPair<QString,QString>>{{"harmony","HarmonyAssistant/Harmony.js"},
        {"timeline","HarmonyAssistant/Timeline.js"},{"rules","ArrangementAssistant/Rules.js"}}) {
        QFile file(QString(TESTROOT)+"/share/plugins/"+item.second);
        if(!file.open(QIODevice::ReadOnly))return {{"error",file.fileName()}};
        sources[item.first]=QString::fromUtf8(file.readAll());
    }
    QJSEngine engine;
    QString rules=sources.value("rules").toString();
    rules.remove(QRegularExpression("^\\.(pragma|import).*?$",QRegularExpression::MultilineOption));
    auto value=engine.evaluate(rules);
    if(value.isError())return {{"error",value.toString()}};
    auto bundle=engine.globalObject().property("workerBundle").call({engine.toScriptValue(sources)});
    auto defaults=engine.globalObject().property("defaults").call();
    return {{"source",bundle.toString()},{"config",defaults.toVariant()}};
}

void TestPluginHost::arrangementKnownHandWithOtherFingerConflict()
{
    const auto bundle=arrangementBundle083();QVERIFY2(!bundle.contains("error"),qPrintable(bundle.value("error").toString()));
    auto config=bundle.value("config").toMap();
    config["roles"]=QVariantMap{{"0",QVariantMap{{"hand","right"}}},
        {"1",QVariantMap{{"hand","right"}}},{"2",QVariantMap{{"hand","right"}}},{"4",QVariantMap{{"hand","left"}}}};
    auto note=[](int pitch,int tick,int end,int track){return QVariantMap{{"id",QString("%1:%2").arg(track).arg(tick)},
        {"pitch",pitch},{"writtenPitch",pitch},{"tpc",14},{"tick",tick},{"attackTick",tick},
        {"end",end},{"logicalEnd",end},{"track",track},{"index",0},{"partId",0},{"piano",true},
        {"measure",1},{"beat",1.+tick/480.},{"onSeconds",tick/960.},{"endSeconds",end/960.}};};
    for(bool conflictingHands:{false,true}) {
    auto left=note(36,0,960,4);if(!conflictingHands)left["fingerings"]=QStringList{"1","2"};
    QVariantList notes{note(60,0,1920,0),note(84,480,960,1),left};
    if(conflictingHands)notes.append(note(36,0,960,2));
    for(bool pedal:{false,true}) {
        const QVariantList pedals=pedal?QVariantList{QVariantMap{{"tick",0},{"end",1920},{"firstTrack",0},{"endTrack",8}}}:QVariantList{};
        const QVariantMap data{{"notes",notes},{"pedals",pedals},{"hands",QVariantList{}},{"harmonies",QVariantList{}},
            {"meters",QVariantList{QVariantMap{{"tick",0},{"end",1920},{"measure",1},{"denominator",4}}}},
            {"scope",QVariantMap{{"start",0},{"end",1920},{"firstTrack",0},{"endTrack",8}}}};
        const auto before=QJsonDocument::fromVariant(data).toJson();
        PluginAPI::ScoreObserver observer;
        QSignalSpy done(&observer,&PluginAPI::ScoreObserver::readOnlyJobFinished);
        observer.startReadOnlyJob(bundle.value("source").toString(),{{"data",data},{"config",config},{"imported",QVariantList{}}});
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);
        const auto result=done.takeFirst()[1].toMap();QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));
        bool span=false,release=false,uncertainty=false;
        for(const auto& value:result.value("issues").toList()) {
            const auto issue=value.toMap();const auto code=issue.value("code").toString();
            span|=issue.value("tick").toInt()==480 && code=="span-right";
            release|=issue.value("tick").toInt()==480 && code=="release-right";
            uncertainty|=code==(conflictingHands?"hand-uncertain":"fingering-uncertain");
        }
        QVERIFY2(pedal?release:span,pedal?"踏板下应说明右手C4可松开":"左手指法冲突时仍应报告右手保持音的两八度跨度");
        if(pedal)QVERIFY(!span);
        QVERIFY(uncertainty);
        QCOMPARE(QJsonDocument::fromVariant(data).toJson(),before);
    }
    }
}

void TestPluginHost::arrangementCompletePanelFlow()
{
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/pluginhost/arrangement_083.mscx");QVERIFY(score);
    const int undoBefore=score->undoStack()->getCurIdx();
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,920);main->show();
    score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->upNote());
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");
    QPointer<QmlPlugin> panel;
    for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))
        if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))if(root->menuPath()=="Plugins.Arrangement Check")panel=root;
    QVERIFY(panel);panel->setPanelFloating(true);panel->resizePanel(430,740);QTest::qWait(100);
    struct Close {QPointer<QmlPlugin> panel;~Close(){if(panel)panel->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}} close{panel};
    for(int track:{0,1,4})QVERIFY(QMetaObject::invokeMethod(panel,"roleChange",Q_ARG(QVariant,QVariant(track)),
        Q_ARG(QVariant,QVariant("hand")),Q_ARG(QVariant,QVariant(track==4?"left":"right"))));
    auto click=[&](const char* name){auto item=panel->findChild<QQuickItem*>(name);if(!item)return false;
        QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;};
    QVERIFY(click("arrangementUpdate"));
    QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),10000);
    auto issues=[&]{return panel->property("issues").value<QJSValue>().toVariant().toList();};
    int spanIndex=-1;for(int i=0;i<issues().size();++i)if(issues()[i].toMap().value("code")=="span-right")spanIndex=i;
    QVERIFY2(spanIndex>=0,"实际谱面读取和后台检查应保留右手跨度提示");
    const auto retained=issues();const auto range=panel->property("scope").value<QJSValue>().toVariant().toMap();
    score->select(score->lastMeasure()->first(SegmentType::ChordRest)->element(0));main->endCmd();QTest::qWait(150);
    QCOMPARE(issues(),retained);QCOMPARE(panel->property("scope").value<QJSValue>().toVariant().toMap(),range);
    panel->setProperty("categoryMask",4);
    auto filtered=panel->property("filteredIssues").value<QJSValue>().toVariant().toList();
    QVERIFY(!filtered.isEmpty());for(auto value:filtered)QCOMPARE(value.toMap().value("type").toString(),QString("playability"));
    panel->setProperty("currentIssueIndex",spanIndex);
    auto config=panel->property("config").value<QJSValue>().toVariant().toMap();config["scoreHints"]=true;panel->setProperty("config",config);
    QVERIFY(QMetaObject::invokeMethod(panel,"locate",Q_ARG(QVariant,QVariant(false))));
    QTRY_VERIFY(!main->currentScoreView()->notePreviewMarkerBoxes().isEmpty());
    const auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-held-span-083.png")));
    auto view=main->currentScoreView();
    auto bounds=score->firstMeasure()->canvasBoundingRect();
    for(const auto& box:view->notePreviewMarkerBoxes())bounds=bounds.united(box);
    QVERIFY(view->grab(view->toPhysical(bounds).adjusted(-65,-45,65,70).intersected(view->rect())).save(out.filePath("arrangement-score-hints-083.png")));
    QVERIFY(QMetaObject::invokeMethod(panel,"openProblemHint",Q_ARG(QVariant,QVariant(0))));
    auto nearby=panel->findChild<QQuickWindow*>("arrangementProblemHint");QTRY_VERIFY(nearby&&nearby->isVisible());
    auto explanation=nearby->findChild<QQuickItem*>("arrangementProblemExplanation");QVERIFY(explanation);
    auto text=nearby->findChild<QQuickItem*>("arrangementProblemText");QVERIFY(text);
    QTRY_VERIFY(explanation->height()>80 && text->height()>20);
    QVERIFY(text->property("text").toString().contains(QString::fromUtf8("指法")));
    QCOMPARE(panel->property("current").value<QJSValue>().toVariant().toMap().value("code").toString(),QString("fingering-uncertain"));
    auto object=panel->property("observer");
    auto observer=qobject_cast<PluginAPI::ScoreObserver*>(object.canConvert<QJSValue>()?object.value<QJSValue>().toQObject():object.value<QObject*>());QVERIFY(observer);
    const auto where=observer->scoreLocation(0,4);
    const QRectF noteBox(where.value("x").toDouble(),where.value("y").toDouble(),where.value("width").toDouble(),where.value("height").toDouble());
    QVERIFY(!noteBox.isEmpty());QVERIFY(!QRectF(nearby->geometry()).intersects(noteBox));
    QVERIFY(nearby->grabWindow().save(out.filePath("arrangement-nearby-083.png")));
    auto closeHint=nearby->findChild<QQuickItem*>("arrangementProblemClose");QVERIFY(closeHint);
    QTest::mouseClick(nearby,Qt::LeftButton,Qt::NoModifier,closeHint->mapToScene(QPointF(closeHint->width()/2,closeHint->height()/2)).toPoint());
    QTRY_VERIFY(!nearby->isVisible());panel->setProperty("currentIssueIndex",spanIndex);
    QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),10000);
    QCOMPARE(panel->property("current").value<QJSValue>().toVariant().toMap().value("code").toString(),QString("span-right"));
    panel->setProperty("rangeChoice",1);QVERIFY(click("arrangementUpdate"));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),10000);
    QCOMPARE(panel->property("scope").value<QJSValue>().toVariant().toMap().value("end").toInt(),3840);
    panel->setProperty("categoryMask",7);QCOMPARE(issues().size(),panel->property("filteredIssues").value<QJSValue>().toVariant().toList().size());
    QCOMPARE(score->undoStack()->getCurIdx(),undoBefore);
    auto segment=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);QVERIFY(segment);
    auto next=toChord(segment->element(1))->upNote();QCOMPARE(next->pitch(),84);
    auto hasSpan=[&]{for(const auto& value:issues())if(value.toMap().value("code")=="span-right")return true;return false;};
    score->startCmd();next->undoChangeProperty(Pid::PITCH,72);score->endCmd();main->endCmd();
    QTRY_VERIFY_WITH_TIMEOUT(!hasSpan(),7000);
    QCOMPARE(panel->property("scope").value<QJSValue>().toVariant().toMap().value("end").toInt(),3840);
    main->cmd(getAction("undo"));QTRY_VERIFY_WITH_TIMEOUT(hasSpan(),7000);
    QCOMPARE(next->pitch(),84);QCOMPARE(score->undoStack()->getCurIdx(),undoBefore);
    const auto original=preferences.preferredGlobalStyle();
    struct RestoreTheme {MuseScorePreferredStyleType style;~RestoreTheme(){preferences.setTemporaryPreference(PREF_UI_APP_GLOBALSTYLE,QVariant::fromValue(style));MuseScore::updateUiStyleAndTheme();}} restoreTheme{original};
    preferences.setTemporaryPreference(PREF_UI_APP_GLOBALSTYLE,QVariant::fromValue(MuseScorePreferredStyleType::DARK_FUSION));
    MuseScore::updateUiStyleAndTheme();
    auto theme=panel->findChild<QObject*>("workflowUiTheme");QVERIFY(theme);
    QTRY_VERIFY(theme->property("dark").toBool());
    const auto background=theme->property("background").value<QColor>(),foreground=theme->property("text").value<QColor>();
    QVERIFY(qAbs(background.lightnessF()-foreground.lightnessF())>.4);
    panel->setProperty("currentIssueIndex",spanIndex);QTest::qWait(100);
    QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-held-span-dark-083.png")));
    panel->resizePanel(320,500);QTest::qWait(150);
    auto details=panel->findChild<QQuickItem*>("arrangementDetailsScroll");QVERIFY(details);
    QTRY_VERIFY(details->property("contentHeight").toDouble()>details->height());
    QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-compact-top-083.png")));
    details->setProperty("contentY",details->property("contentHeight").toDouble()-details->height());QTest::qWait(80);
    auto roles=panel->findChild<QQuickItem*>("arrangementRoles");QVERIFY(roles);
    const auto position=roles->mapToScene(QPointF(roles->width()/2,roles->height()/2));
    QVERIFY(position.y()>0&&position.y()<panel->window()->height());
    QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-compact-bottom-083.png")));
    panel->focusPanel();QTest::qWait(80);
    QVERIFY(click("arrangementRoles"));auto popup=panel->findChild<QObject*>("arrangementRolesPopup");QTRY_VERIFY(popup&&popup->property("visible").toBool());
    QTest::qWait(80);QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-role-settings-083.png")));
    QVERIFY(click("arrangementRolesClose"));QTRY_VERIFY(!popup->property("visible").toBool());
    QVERIFY(click("arrangementRoles"));QTRY_VERIFY(popup->property("visible").toBool());QTest::qWait(80);
    QTest::keyClick(panel->window(),Qt::Key_Escape);QTRY_VERIFY(!popup->property("visible").toBool());
}
