static QmlPlugin* harmonyPanel077()
{
    for (auto window : QGuiApplication::allWindows())
        if (auto view=qobject_cast<QQuickView*>(window))
            if (auto panel=qobject_cast<QmlPlugin*>(view->rootObject()))
                if (panel->menuPath()=="Plugins.Harmony Assistant")return panel;
    return nullptr;
}

void TestPluginHost::harmonyRangeWithoutSettings()
{
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1200,900);main->show();
    showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    panel->setProperty("settingsExpanded",false);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    auto range=panel->findChild<QQuickItem*>("harmonyRangeEditor");QVERIFY(range);
    QVERIFY2(range->isVisible(),"修改和弦与范围应在关闭详细设置时仍可打开");
    showHarmonyPanel(false);
}

void TestPluginHost::harmonyManualSurvivesPitchEdit()
{
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1200,900);main->show();
    showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY(QMetaObject::invokeMethod(panel,"editRange",Q_ARG(QVariant,"assign"),Q_ARG(QVariant,480),Q_ARG(QVariant,960),Q_ARG(QVariant,0),Q_ARG(QVariant,0),Q_ARG(QVariant,0)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);
    QVERIFY(!observer->workflowData("harmony").value("overrides").toList().isEmpty());
    auto note=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes().front();
    score->startCmd();score->undoChangePitch(note,60,14,14);score->endCmd();
    QVERIFY(QMetaObject::invokeMethod(panel,"requestRefresh",Q_ARG(QVariant,true)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY2(!panel->property("manualNeedsReview").toBool(),"改音高后人工和弦范围应保留");
    auto regions=panel->property("analysisRegions").value<QJSValue>().toVariant().toList();
    auto hasCurrentNote=[&regions] {
        for(auto value:regions){auto r=value.toMap();if(r.value("source")=="manual")
            for(auto n:r.value("notes").toList())if(n.toMap().value("track").toInt()==0&&n.toMap().value("tick").toInt()==480&&n.toMap().value("pitch").toInt()==60)return true;}
        return false;
    };
    QVERIFY(hasCurrentNote());
    auto data=observer->workflowData("harmony");QVERIFY(!data.value("structure").toString().isEmpty());
    const auto path=_settings.filePath("harmony-manual-077.mscz");QFileInfo info(path);
    QVERIFY(score->saveCompressedFile(info,false,false));
    std::unique_ptr<MasterScore> restored(main->readScore(path));QVERIFY(restored);
    QCOMPARE(restored->metaTag("kumo.harmony"),score->metaTag("kumo.harmony"));
    score->startCmd();score->insertMeasure(ElementType::MEASURE,score->firstMeasure());score->endCmd();score->doLayout();
    QVERIFY(QMetaObject::invokeMethod(panel,"requestRefresh",Q_ARG(QVariant,true)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY(panel->property("manualNeedsReview").toBool());
    main->cmd(getAction("undo"));
    QVERIFY(QMetaObject::invokeMethod(panel,"requestRefresh",Q_ARG(QVariant,true)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY(!panel->property("manualNeedsReview").toBool());
    main->cmd(getAction("undo"));
    QVERIFY(QMetaObject::invokeMethod(panel,"requestRefresh",Q_ARG(QVariant,true)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QCOMPARE(note->pitch(),59);QVERIFY(!panel->property("manualNeedsReview").toBool());
    showHarmonyPanel(false);releaseHostPanels();
    main->setCurrentScoreView(main->appendScore(restored.release()));
    showHarmonyPanel(true);panel=harmonyPanel077();QVERIFY(panel);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY(!panel->property("manualNeedsReview").toBool());
    regions=panel->property("analysisRegions").value<QJSValue>().toVariant().toList();
    QVERIFY(hasCurrentNote());
    showHarmonyPanel(false);
}

void TestPluginHost::harmonyStyleSurfacesAndRestore()
{
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1200,900);main->show();
    showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    panel->setPanelFloating(true);panel->resizePanel(380,840);QTest::qWait(120);
    auto detail=panel->findChild<QQuickItem*>("harmonyDetailLabel");QVERIFY(detail);
    auto summary=panel->findChild<QQuickItem*>("harmonyRibbonChord");QVERIFY(summary);
    auto current=panel->findChild<QQuickItem*>("harmonySummary");QVERIFY(current);
    const auto config=[&panel]{return panel->property("configuration").value<QJSValue>().toVariant().toMap();};
    const auto before=config();
    const QDir artifacts(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QTest::mouseMove(panel->window(),QPoint(370,810));QTest::qWait(100);
    QVERIFY(panel->window()->grabWindow().save(artifacts.filePath("harmony-details-default-077.png")));
    const auto click=[](QQuickItem* item,QPointF at) {
        QTest::mouseClick(item->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(at).toPoint());
    };
    auto settings=panel->findChild<QQuickItem*>("harmonySettingsButton");QVERIFY(settings);
    if(!panel->property("settingsExpanded").toBool())click(settings,QPointF(settings->width()/2,settings->height()/2));
    QTRY_VERIFY(panel->property("settingsExpanded").toBool());
    auto flick=panel->findChild<QQuickItem*>("harmonyDetailScroll");QVERIFY(flick);
    auto content=qobject_cast<QQuickItem*>(flick->property("contentItem").value<QObject*>());QVERIFY(content);
    auto styleCard=panel->findChild<QQuickItem*>("harmonyDetailStyleCard");QVERIFY(styleCard);
    const auto reveal=[flick,content](QQuickItem* item){
        QTest::qWait(160);
        const qreal y=item->mapToItem(content,QPointF()).y()-6;
        flick->setProperty("contentY",y);QTest::qWait(100);
    };
    reveal(styleCard);
    if(styleCard->property("collapsed").toBool())click(styleCard,QPointF(styleCard->width()/2,15));
    QTRY_VERIFY(!styleCard->property("collapsed").toBool());
    auto style=panel->findChild<QQuickItem*>("detailStyleEditor");QVERIFY(style);
    const auto option=[style](const char* key,const QVariant& value){return QMetaObject::invokeMethod(style,"option",Q_ARG(QVariant,QString::fromLatin1(key)),Q_ARG(QVariant,value));};
    QVERIFY(option("Pixels",20));QVERIFY(option("Order",2));QVERIFY(option("Gap",3));QVERIFY(option("DegreeRatio",90));
    QTRY_COMPARE(detail->property("pixels").toInt(),20);
    QCOMPARE(detail->property("order").toInt(),2);QCOMPARE(detail->property("gap").toInt(),3);
    QCOMPARE(summary->property("pixels").toInt(),before.value("summaryPixels").toInt());
    QCOMPARE(config().value("chordScale"),before.value("chordScale"));
    QCOMPARE(config().value("chordOrder"),before.value("chordOrder"));
    QVERIFY(option("Content",1));QTRY_VERIFY(detail->property("chord").toString().isEmpty());
    QVERIFY(!detail->property("degree").toString().isEmpty());
    QVERIFY(option("Content",2));
    const auto stableHeight=current->height();
    panel->setProperty("chordText","Cmaj13(#11)/F#");QTest::qWait(30);QCOMPARE(current->height(),stableHeight);
    panel->setProperty("chordText","C/E");QTest::qWait(30);QCOMPARE(current->height(),stableHeight);
    QTest::mouseMove(panel->window(),QPoint(370,810));QTest::qWait(100);
    flick->setProperty("contentY",0);QTest::qWait(40);
    QVERIFY(panel->window()->grabWindow().save(artifacts.filePath("harmony-details-077.png")));
    reveal(styleCard);QTest::qWait(40);
    QVERIFY(panel->window()->grabWindow().save(artifacts.filePath("harmony-style-077.png")));
    panel->resizePanel(300,520);reveal(styleCard);
    QVERIFY(panel->window()->grabWindow().save(artifacts.filePath("harmony-style-narrow-077.png")));
    QVERIFY(style->width()>=230);
    auto theme=panel->findChild<QObject*>("workflowUiTheme");QVERIFY(theme);
    const auto palette=qApp->palette();auto dark=palette;
    dark.setColor(QPalette::Window,QColor("#303236"));dark.setColor(QPalette::WindowText,QColor("#eeeeee"));
    dark.setColor(QPalette::Base,QColor("#25272a"));dark.setColor(QPalette::Text,QColor("#eeeeee"));
    dark.setColor(QPalette::Button,QColor("#3a3d41"));dark.setColor(QPalette::ButtonText,QColor("#eeeeee"));
    qApp->setPalette(dark);QTest::qWait(160);
    const bool isDark=theme->property("dark").toBool();
    const bool captured=panel->window()->grabWindow().save(artifacts.filePath("harmony-style-dark-077.png"));
    qApp->setPalette(palette);QTest::qWait(80);QVERIFY(isDark);QVERIFY(captured);
    const auto expected=config();QVERIFY(QMetaObject::invokeMethod(panel,"savePreferences"));
    showHarmonyPanel(false);releaseHostPanels();
    showHarmonyPanel(true);panel=harmonyPanel077();QVERIFY(panel);
    QTRY_COMPARE(config().value("detailOrder").toInt(),2);
    QCOMPARE(config().value("detailPixels"),expected.value("detailPixels"));
    QCOMPARE(config().value("summaryPixels"),expected.value("summaryPixels"));
    panel->setPanelFloating(false);QTest::qWait(80);
    showHarmonyPanel(false);
}
