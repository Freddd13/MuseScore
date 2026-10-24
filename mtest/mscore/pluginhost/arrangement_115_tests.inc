// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
static QByteArray arrangementDuration115(int ticks)
{
    switch(ticks) {
    case 120:return "<durationType>16th</durationType>";
    case 240:return "<durationType>eighth</durationType>";
    case 480:return "<durationType>quarter</durationType>";
    case 960:return "<durationType>half</durationType>";
    case 1440:return "<durationType>half</durationType><dots>1</dots>";
    case 1920:return "<durationType>whole</durationType>";
    default:return {};
    }
}

static QByteArray arrangementRests115(int ticks)
{
    QByteArray result;
    for(int length:{1920,960,480,240,120})while(ticks>=length){result+="<Rest>"+arrangementDuration115(length)+"</Rest>";ticks-=length;}
    return ticks?QByteArray():result;
}

static QByteArray arrangementScore115(const QVariantList& notes,bool writtenHarmony=false)
{
    QByteArray xml="<museScore version=\"3.02\"><Score><Division>480</Division><Part>";
    for(int staff=1;staff<=2;++staff)xml+="<Staff id=\""+QByteArray::number(staff)+"\"><StaffType group=\"pitched\"/></Staff>";
    xml+="<trackName>Piano</trackName><Instrument id=\"piano\"><Channel/></Instrument></Part>";
    for(int track:{0,4}) {
        xml+="<Staff id=\""+QByteArray::number(track/4+1)+"\"><Measure><voice><Clef><concertClefType>"+(track?QByteArray("F"):QByteArray("G"))+"</concertClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
        QMap<int,QVariantList> groups;
        for(const auto& value:notes){auto row=value.toList();if(row[3].toInt()==track)groups[row[1].toInt()].append(value);}
        int current=0;
        for(auto it=groups.cbegin();it!=groups.cend();++it) {
            if(it.key()<current)return {};
            if(it.key()>current){const auto rests=arrangementRests115(it.key()-current);if(rests.isEmpty())return {};xml+=rests;}
            const auto first=it.value().front().toList();const int until=first[2].toInt();
            const auto duration=arrangementDuration115(until-it.key());if(duration.isEmpty())return {};
            if(writtenHarmony && track==0 && it.key()==960)xml+="<Harmony><root>14</root><name></name></Harmony>";
            xml+="<Chord>"+duration;
            for(const auto& value:it.value()){const auto row=value.toList();if(row[2].toInt()!=until)return {};xml+="<Note><pitch>"+QByteArray::number(row[0].toInt())+"</pitch><tpc>"+QByteArray::number(row[4].toInt())+"</tpc></Note>";}
            xml+="</Chord>";current=until;
        }
        if(current<1920){const auto rests=arrangementRests115(1920-current);if(rests.isEmpty())return {};xml+=rests;}
        xml+="</voice></Measure></Staff>";
    }
    return xml+"</Score></museScore>";
}

void TestPluginHost::arrangementContextCorpus115()
{
    for(const auto& name:{"Harmony.js","Timeline.js","Analysis.js"}) {
        QFile publicModule(QString(TESTROOT)+"/share/plugins/HarmonyAssistant/"+name),privateModule(QString(TESTROOT)+"/share/workflow/panels/HarmonyAssistant/"+name);
        QVERIFY(publicModule.open(QIODevice::ReadOnly));QVERIFY(privateModule.open(QIODevice::ReadOnly));QCOMPARE(publicModule.readAll(),privateModule.readAll());
    }
    QFile cases(QString(TESTROOT)+"/mtest/mscore/pluginhost/arrangement_115_cases.json");QVERIFY(cases.open(QIODevice::ReadOnly));
    const auto corpus=QJsonDocument::fromJson(cases.readAll()).toVariant().toMap().value("cases").toList();QCOMPARE(corpus.size(),32);
    const auto bundle=arrangementBundle083();QVERIFY(!bundle.contains("error"));auto main=Ms::mscore;main->resize(1200,900);main->show();QVariantList report;
    for(const auto& value:corpus) {
        const auto item=value.toMap();const auto xml=arrangementScore115(item.value("notes").toList());QVERIFY2(!xml.isEmpty(),qPrintable(item.value("id").toString()));
        const auto path=_settings.filePath(item.value("id").toString()+"-115.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
        auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
        auto scope=observer.analysisScope(true);QCOMPARE(scope.value("end").toInt(),1920);if(item.contains("scopeStart"))scope["start"]=item["scopeStart"];auto data=observer.analysisRange(0,1920,0,8,0,64,observer.analysisRevision());QVERIFY(!data.contains("error"));QVERIFY(data.value("done").toBool());data["scope"]=scope;data["harmonyRegions"]=item.value("harmonyRegions",QVariantList());QCOMPARE(data.value("notes").toList().size(),item.value("notes").toList().size());
        auto config=bundle.value("config").toMap();config["playability"]=false;const auto changes=item.value("config").toMap();for(auto it=changes.cbegin();it!=changes.cend();++it)config[it.key()]=it.value();
        if(item.value("independent").toBool())config["roles"]=QVariantMap{{"0",QVariantMap{{"role","melody"},{"independent",true}}},{"4",QVariantMap{{"role","bass"},{"independent",true}}}};
        const auto before=QJsonDocument::fromVariant(data).toJson();const auto state=score->state();const int undo=score->undoStack()->getCurIdx();QSignalSpy done(&observer,&PluginAPI::ScoreObserver::readOnlyJobFinished);
        observer.startReadOnlyJob(bundle.value("source").toString(),{{"data",data},{"config",config},{"imported",QVariantList()}});QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);
        const auto result=done.takeFirst()[1].toMap();QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));const auto issues=result.value("issues").toList();
        for(const auto& expected:item.value("expect").toList()) {
            const auto condition=expected.toList();bool found=false;for(const auto& issue:issues){const auto row=issue.toMap();found|=row.value("code")==condition[0]&&row.value("tick")==condition[1];}
            QVERIFY2(found,qPrintable(item.value("id").toString()+" missing "+condition[0].toString()));
        }
        for(const auto& absent:item.value("absent").toList())for(const auto& issue:issues)QVERIFY2(issue.toMap().value("code")!=absent,qPrintable(item.value("id").toString()+" unexpected "+absent.toString()));
        const auto evidence=item.value("minimumNotes").toMap();for(auto it=evidence.cbegin();it!=evidence.cend();++it){bool found=false;for(const auto& issue:issues){auto row=issue.toMap();found|=row.value("code").toString()==it.key()&&row.value("notes").toList().size()>=it.value().toInt();}QVERIFY(found);}
        QCOMPARE(QJsonDocument::fromVariant(data).toJson(),before);QVERIFY(score->state()==state);QCOMPARE(score->undoStack()->getCurIdx(),undo);
        report.append(QVariantMap{{"id",item.value("id")},{"issues",issues}});main->closeScore(score);
    }
    QFile output(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("arrangement-context-115.json"));QVERIFY(output.open(QIODevice::WriteOnly));output.write(QJsonDocument::fromVariant(report).toJson());
}

void TestPluginHost::arrangementHarmonyAndCounterpointUI115()
{
    const QVariantList notes{QVariantList{67,0,480,0,15},QVariantList{69,480,960,0,17},QVariantList{73,960,1920,0,21},QVariantList{60,0,480,4,14},QVariantList{62,480,960,4,16},QVariantList{60,960,1920,4,14}};
    const auto path=_settings.filePath("arrangement-ui-115.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(arrangementScore115(notes,true));file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,920);main->show();score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front());
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel;
    for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))if(root->menuPath()=="Plugins.Arrangement Check")panel=root;
    QVERIFY(panel);struct Close{QPointer<QmlPlugin> panel;QVariantMap config;~Close(){if(panel){panel->setProperty("config",config);panel->disposePanelForReload();}QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}}close{panel,panel->property("config").value<QJSValue>().toVariant().toMap()};
    panel->setPanelFloating(true);panel->resizePanel(430,800);QTest::qWait(120);auto config=panel->property("config").value<QJSValue>().toVariant().toMap();config["playability"]=false;config["scoreHints"]=true;config["harmony"]=true;config["counterpoint"]=true;config["roles"]=QVariantMap{{"0",QVariantMap{{"role","melody"},{"independent",true}}},{"4",QVariantMap{{"role","bass"},{"independent",true}}}};panel->setProperty("config",config);
    auto find=[&](const QString& name){QQuickItem* result=nullptr;std::function<void(QQuickItem*)> visit=[&](QQuickItem* item){if(item->isVisible()&&item->objectName()==name)result=item;for(auto child:item->childItems())visit(child);};visit(panel->window()->contentItem());return result;};
    auto click=[&](const QString& name){auto item=find(name);if(!item)return false;const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();if(!QRect(QPoint(),item->window()->size()).contains(point))return false;QTest::mouseClick(item->window(),Qt::LeftButton,Qt::NoModifier,point);return true;};
    const auto state=score->state();const int undo=score->undoStack()->getCurIdx();QVERIFY(click("arrangementUpdate"));QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),10000);
    auto issues=[&]{return panel->property("issues").value<QJSValue>().toVariant().toList();};int parallel=-1,harmony=-1;for(int i=0;i<issues().size();++i){auto row=issues()[i].toMap();if(row.value("code")=="parallel-5")parallel=i;if(row.value("code")=="chord-color")harmony=i;}
    QVERIFY(parallel>=0);QVERIFY(harmony>=0);const auto original=issues();
    auto list=find("arrangementIssues");QVERIFY(list);QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,list->mapToScene(QPointF(70,parallel*59+25)).toPoint());
    QTRY_COMPARE(panel->property("current").value<QJSValue>().toVariant().toMap().value("code").toString(),QString("parallel-5"));QCOMPARE(panel->property("current").value<QJSValue>().toVariant().toMap().value("notes").toList().size(),4);
    auto hint=panel->findChild<QQuickWindow*>("arrangementProblemHint");QTRY_VERIFY(hint&&hint->isVisible());auto hintText=hint->findChild<QQuickItem*>("arrangementProblemText");QVERIFY(hintText);QTRY_VERIFY(hintText->height()>20);QVERIFY(hintText->property("text").toString().contains("G4"));QVERIFY(hintText->property("text").toString().contains("A4"));
    auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);
    auto notesClear=[&]{const auto current=panel->property("current").value<QJSValue>().toVariant().toMap();for(const auto& value:current.value("notes").toList()){const auto note=value.toMap(),where=observer->scoreLocation(note.value("tick").toInt(),note.value("track").toInt());const QRectF box(where.value("x").toDouble(),where.value("y").toDouble(),where.value("width").toDouble(),where.value("height").toDouble());if(box.isEmpty()||QRectF(hint->geometry()).intersects(box))return false;}return true;};
    QVERIFY(notesClear());QVERIFY(main->currentScoreView()->notePreviewMarkerBoxes().size()>=2);
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(hint->grabWindow().save(out.filePath("arrangement-counterpoint-hint-115.png")));auto closeHint=hint->findChild<QQuickItem*>("arrangementProblemClose");QVERIFY(closeHint);QTest::mouseClick(hint,Qt::LeftButton,Qt::NoModifier,closeHint->mapToScene(QPointF(closeHint->width()/2,closeHint->height()/2)).toPoint());QTRY_VERIFY(!hint->isVisible());
    QVERIFY(click("arrangementCategoryFilter"));QTest::qWait(80);QVERIFY(click("arrangementCategory_counterpoint"));QTRY_COMPARE(panel->property("categoryMask").toInt(),5);QTest::keyClick(panel->window(),Qt::Key_Escape);
    const auto filtered=panel->property("filteredIssues").value<QJSValue>().toVariant().toList();QVERIFY(!filtered.isEmpty());for(const auto& value:filtered)QCOMPARE(value.toMap().value("type").toString(),QString("harmony"));
    list=find("arrangementIssues");QVERIFY(list);QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,list->mapToScene(QPointF(70,25)).toPoint());QTRY_COMPARE(panel->property("current").value<QJSValue>().toVariant().toMap().value("code").toString(),QString("chord-color"));QTRY_VERIFY(hint->isVisible());QVERIFY(hintText->property("text").toString().contains(QString::fromUtf8(QByteArray::fromHex("43e299af35"))));QVERIFY(notesClear());
    QVERIFY(hint->grabWindow().save(out.filePath("arrangement-harmony-hint-115.png")));QTest::mouseClick(hint,Qt::LeftButton,Qt::NoModifier,closeHint->mapToScene(QPointF(closeHint->width()/2,closeHint->height()/2)).toPoint());QTRY_VERIFY(!hint->isVisible());QTest::mouseMove(panel->window(),QPoint(5,5));QTest::qWait(120);QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-harmony-panel-115.png")));
    score->deselectAll();main->endCmd();QTest::qWait(100);QCOMPARE(issues(),original);QVERIFY(score->state()==state);QCOMPARE(score->undoStack()->getCurIdx(),undo);QVERIFY(main->saveFile());
}

void TestPluginHost::arrangementUsesSharedHarmony115()
{
    const QVariantList notes{QVariantList{73,0,1920,0,21},QVariantList{60,0,1920,4,14},QVariantList{64,0,1920,4,18},QVariantList{67,0,1920,4,15}};
    const auto path=_settings.filePath("arrangement-shared-115.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(arrangementScore115(notes));file.close();auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front());
    showHarmonyPanel(true);QPointer<QmlPlugin> harmony=harmonyPanel077();QVERIFY(harmony);auto configuration=harmony->property("configuration").value<QJSValue>().toVariant().toMap();configuration["auto"]=true;configuration["scope"]=0;configuration["resultMode"]=1;configuration["chordLabels"]=true;QVERIFY(QMetaObject::invokeMethod(harmony,"applyPreferences",Q_ARG(QVariant,configuration)));QTRY_VERIFY_WITH_TIMEOUT(!harmony->property("analysisDirty").toBool(),10000);
    auto assign=[&](int definition){return QMetaObject::invokeMethod(harmony,"editRange",Q_ARG(QVariant,"assign"),Q_ARG(QVariant,0),Q_ARG(QVariant,1920),Q_ARG(QVariant,0),Q_ARG(QVariant,definition),Q_ARG(QVariant,0));};QVERIFY(assign(0));QTRY_VERIFY_WITH_TIMEOUT(!harmony->property("analysisDirty").toBool(),10000);
    auto harmonyObserver=harmony->findChild<PluginAPI::ScoreObserver*>();QVERIFY(harmonyObserver);auto shared=[&]{return harmonyObserver->arrangementState().value("harmonyAnalysis").toMap();};QTRY_COMPARE(shared().value("freshness").toString(),QString("current"));
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel;
    for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))if(root->menuPath()=="Plugins.Arrangement Check")panel=root;
    QVERIFY(panel);struct Close{QPointer<QmlPlugin> panel;QVariantMap config;~Close(){if(panel){panel->setProperty("config",config);panel->disposePanelForReload();}showHarmonyPanel(false);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}}close{panel,panel->property("config").value<QJSValue>().toVariant().toMap()};panel->setPanelFloating(true);panel->resizePanel(430,760);
    auto config=close.config;config["playability"]=false;config["counterpoint"]=false;config["harmony"]=true;config["chords"]=QVariantList();panel->setProperty("config",config);QVERIFY(QMetaObject::invokeMethod(panel,"start",Q_ARG(QVariant,false)));QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),10000);
    auto definition=[&]{const auto data=panel->property("cachedData").value<QJSValue>().toVariant().toMap();const auto regions=data.value("harmonyRegions").toList();return regions.isEmpty()?-1:regions.front().toMap().value("definition").toInt();};
    auto hasColour=[&]{for(const auto& value:panel->property("issues").value<QJSValue>().toVariant().toList())if(value.toMap().value("code")=="chord-color")return true;return false;};QCOMPARE(definition(),0);QVERIFY(hasColour());const auto range=panel->property("scope").value<QJSValue>().toVariant();
    QVERIFY(assign(25));QTRY_VERIFY_WITH_TIMEOUT(!harmony->property("analysisDirty").toBool(),10000);QTRY_COMPARE(shared().value("freshness").toString(),QString("current"));QTRY_COMPARE_WITH_TIMEOUT(definition(),25,10000);QTRY_VERIFY(!panel->property("running").toBool());QVERIFY(!hasColour());QCOMPARE(panel->property("scope").value<QJSValue>().toVariant(),range);
    main->cmd(getAction("undo"));QTRY_COMPARE_WITH_TIMEOUT(definition(),0,10000);QTRY_VERIFY(!panel->property("running").toBool());QVERIFY(hasColour());QVERIFY(main->saveFile());
}
