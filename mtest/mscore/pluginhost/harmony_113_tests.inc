// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
void TestPluginHost::harmonyImportRoundTrip113()
{
    const auto source=_settings.filePath("harmony-import-source-113.mscx");QVERIFY(QFile::copy(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx",source));
    auto main=Ms::mscore;auto score=main->readScore(source);QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1200,900);main->show();
    score->select(toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes().front());
    showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();QCOMPARE(config.value("schema").toInt(),1);
    config["auto"]=true;config["scope"]=0;config["manualKey"]=false;config["keyMode"]=0;config["window"]=0;config["pedal"]=false;config["chordLabels"]=true;config["resultMode"]=1;
    QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);const auto context=observer->contextSnapshot(0,0,8,false,0,false);
    QVERIFY(QMetaObject::invokeMethod(panel,"editRange",Q_ARG(QVariant,"assign"),Q_ARG(QVariant,0),Q_ARG(QVariant,480),Q_ARG(QVariant,0),Q_ARG(QVariant,0),Q_ARG(QVariant,0)));
    const auto notes=observer->contextSnapshot(480,0,8,false,0,false).value("analysisNotes").toList();QVERIFY(!notes.isEmpty());
    const auto chord=QStringLiteral("D\u266Dmaj7/F");
    QVariantMap chosen{{"start",480},{"end",960},{"part",0},{"root",1},{"definition",3},{"bass",5},{"bassName","F"},{"tonic",0},{"minor",false},{"rootSpelling",QStringLiteral("D\u266D")},{"tonicSpelling","C"},{"chord",chord},{"degree",QStringLiteral("\u266DIImaj\u2076\u2085")},{"notes",notes}};
    QVariantMap hidden{{"start",960},{"end",1440},{"part",0},{"root",-1},{"definition",-1},{"suppressed",true},{"notes",QVariantList()}};
    QVariantMap document{{"format","HarmonyAssistant.analysis"},{"schema",2},{"ticksPerQuarter",480},{"fingerprint",context.value("fingerprint")},{"frames",QVariantList()},
        {"regions",QVariantList{chosen}},{"overrides",QVariantList{hidden}},{"positions",QVariantList{QVariantMap{{"tick",480},{"part",0},{"x",1.25},{"y",-2.}}}}};
    const auto importPath=_settings.filePath("harmony-import-113.json");
    auto write=[&](const QVariantMap& value){QFile file(importPath);if(!file.open(QIODevice::WriteOnly))return false;file.write(QJsonDocument::fromVariant(value).toJson());return true;};QVERIFY(write(document));
    auto exchange=[&](const QString& action,const QString& path){return panel->setProperty("pendingFileAction",action)&&QMetaObject::invokeMethod(panel,"exchangeFile",Q_ARG(QVariant,QUrl::fromLocalFile(path).toString()));};
    const auto before=score->metaTag("kumo.harmony");QVERIFY(exchange("import-analysis",importPath));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    auto saved=observer->workflowData("harmony");bool stored=false;
    for(const auto& value:saved.value("overrides").toList())stored|=value.toMap().value("chord").toString()==chord;
    QVERIFY2(stored,"Imported region must be stored in the score together with manual corrections");
    QCOMPARE(saved.value("positions").toList().front().toMap().value("x").toDouble(),1.25);
    QTRY_COMPARE(panel->property("summaryChordText").toString(),chord);
    QVERIFY(panel->property("usingImportedAnalysis").toBool());
    const auto imported=score->metaTag("kumo.harmony");QVERIFY(imported!=before);
    QVERIFY(exchange("import-analysis",importPath));QCOMPARE(score->metaTag("kumo.harmony"),imported);
    main->cmd(getAction("undo"));QTRY_COMPARE(score->metaTag("kumo.harmony"),before);QTRY_VERIFY(panel->property("summaryChordText").toString()!=chord);
    main->cmd(getAction("redo"));QTRY_COMPARE(score->metaTag("kumo.harmony"),imported);QTRY_COMPARE(panel->property("summaryChordText").toString(),chord);
    panel->setPanelFloating(true);panel->resizePanel(380,840);panel->setProperty("settingsExpanded",true);QTest::qWait(150);
    auto card=panel->findChild<QQuickItem*>("harmonyExchangeCard");auto flick=panel->findChild<QQuickItem*>("harmonyDetailScroll");QVERIFY(card&&flick);
    if(card->property("collapsed").toBool())QVERIFY(QMetaObject::invokeMethod(panel,"toggleSection",Q_ARG(QVariant,"exchange")));
    QTest::qWait(120);auto content=qobject_cast<QQuickItem*>(flick->property("contentItem").value<QObject*>());QVERIFY(content);
    flick->setProperty("contentY",qBound(0.,card->mapToItem(content,QPointF()).y()-6.,qMax(0.,flick->property("contentHeight").toDouble()-flick->height())));QTest::qWait(100);
    const QDir artifacts(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(panel->window()->grabWindow().save(artifacts.filePath("harmony-import-panel-113.png")));
    auto reanalyse=panel->findChild<QQuickItem*>("harmonyReanalyseImported");QVERIFY(reanalyse&&reanalyse->isVisible());
    QTest::mouseClick(reanalyse->window(),Qt::LeftButton,Qt::NoModifier,reanalyse->mapToScene(QPointF(reanalyse->width()/2,reanalyse->height()/2)).toPoint());QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY(!panel->property("usingImportedAnalysis").toBool());
    QCOMPARE(observer->workflowData("harmony").value("overrides").toList().size(),1);QVERIFY(observer->workflowData("harmony").value("overrides").toList().front().toMap().value("suppressed").toBool());
    main->cmd(getAction("undo"));QTRY_COMPARE(score->metaTag("kumo.harmony"),imported);QTRY_COMPARE(panel->property("summaryChordText").toString(),chord);
    QVERIFY(panel->property("usingImportedAnalysis").toBool());
    const auto exported=_settings.filePath("harmony-export-113.json");QVERIFY(exchange("export-analysis",exported));QFile exportedFile(exported);QVERIFY(exportedFile.open(QIODevice::ReadOnly));
    const auto roundTrip=QJsonDocument::fromJson(exportedFile.readAll()).toVariant().toMap();QCOMPARE(roundTrip.value("fingerprint"),document.value("fingerprint"));QVERIFY(!roundTrip.value("regions").toList().isEmpty());
    auto invalid=document;invalid["positions"]=QVariantList{QVariantMap{{"tick",480},{"part",0},{"x",1000.},{"y",0.}}};QVERIFY(write(invalid));QVERIFY(exchange("import-analysis",importPath));QCOMPARE(score->metaTag("kumo.harmony"),imported);QCOMPARE(panel->property("summaryChordText").toString(),chord);
    const auto compressed=_settings.filePath("harmony-import-113.mscz");QFileInfo compressedInfo(compressed);QVERIFY(score->saveCompressedFile(compressedInfo,false,false));QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);
    auto reopened=main->readScore(compressed);QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));reopened->select(toChord(reopened->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes().front());showHarmonyPanel(true);panel=harmonyPanel077();QVERIFY(panel);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QTRY_COMPARE(panel->property("summaryChordText").toString(),chord);QCOMPARE(reopened->metaTag("kumo.harmony"),imported);
    QVERIFY(panel->property("usingImportedAnalysis").toBool());panel->setPanelFloating(false);QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(reopened);
}

void TestPluginHost::harmonyLegacyAndFailure113()
{
    const auto source=_settings.filePath("harmony-legacy-source-113.mscx");QFile sourceFile(source);QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel/></Instrument></Part><Staff id=\"1\"><Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>whole</durationType><Note><pitch>52</pitch><tpc>18</tpc></Note><Note><pitch>56</pitch><tpc>22</tpc></Note><Note><pitch>59</pitch><tpc>19</tpc></Note><Note><pitch>62</pitch><tpc>16</tpc></Note></Chord></voice></Measure></Staff></Score></museScore>");sourceFile.close();
    auto main=Ms::mscore;auto score=main->readScore(source);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front());showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["auto"]=true;config["scope"]=0;config["keyMode"]=0;config["manualKey"]=false;config["degreeMode"]=1;config["resultMode"]=1;config["chordLabels"]=true;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);
    const auto context=observer->contextSnapshot(0,0,4,false,0,false);
    QVariantMap frame{{"tick",0},{"root",4},{"definition",2},{"chord","E7"},{"degree","V7"},{"notes",context.value("analysisNotes")}};
    QVariantMap end{{"tick",1920},{"root",-1},{"definition",-1},{"chord",""},{"degree",""},{"notes",QVariantList()}};
    QVariantMap document{{"format","HarmonyAssistant.analysis"},{"schema",1},{"ticksPerQuarter",480},{"fingerprint",context.value("fingerprint")},{"frames",QVariantList{frame,end}},{"configuration",QVariantMap{{"schema",1},{"keyMode",1},{"manualKey",false},{"keyTonic",0}}}};
    const auto fileName=_settings.filePath("harmony-legacy-113.json");auto write=[&](const QVariantMap& value){QFile file(fileName);if(!file.open(QIODevice::WriteOnly))return false;file.write(QJsonDocument::fromVariant(value).toJson());return true;};
    auto import=[&]{return panel->setProperty("pendingFileAction","import-analysis")&&QMetaObject::invokeMethod(panel,"exchangeFile",Q_ARG(QVariant,QUrl::fromLocalFile(fileName).toString()));};
    QVERIFY(write(document));QVERIFY(import());QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    auto rows=observer->workflowData("harmony").value("overrides").toList();QCOMPARE(rows.size(),1);QCOMPARE(rows.front().toMap().value("source").toString(),QString("imported"));QCOMPARE(rows.front().toMap().value("end").toInt(),1920);
    QCOMPARE(rows.front().toMap().value("tonic").toInt(),9);QVERIFY(rows.front().toMap().value("minor").toBool());QTRY_COMPARE(panel->property("summaryDegreeText").toString(),QString("V7"));QCOMPARE(panel->property("configuration").value<QJSValue>().property("keyMode").toInt(),0);QVERIFY(!panel->property("chromaticChord").toBool());
    const auto saved=score->metaTag("kumo.harmony");const auto original=panel->property("summaryChordText").toString();QCOMPARE(original,QString("E7"));
    auto shown=panel->property("analysisRegions").value<QJSValue>().toVariant().toList();QVERIFY(!shown.isEmpty());QVERIFY(!shown.front().toMap().value("chromatic").toBool());
    config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["manualKey"]=true;config["keyTonic"]=0;config["keyMode"]=0;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QTRY_VERIFY(panel->property("chromaticChord").toBool());
    const auto exported=_settings.filePath("harmony-legacy-effective-key-113.json");QVERIFY(panel->setProperty("pendingFileAction","export-analysis"));QVERIFY(QMetaObject::invokeMethod(panel,"exchangeFile",Q_ARG(QVariant,QUrl::fromLocalFile(exported).toString())));QFile exportedFile(exported);QVERIFY(exportedFile.open(QIODevice::ReadOnly));const auto exportedData=QJsonDocument::fromJson(exportedFile.readAll()).toVariant().toMap();
    for(const auto& field:{QString("regions"),QString("overrides")}){auto values=exportedData.value(field).toList();QVERIFY(!values.isEmpty());auto value=values.front().toMap();QCOMPARE(value.value("tonic").toInt(),0);QVERIFY(!value.value("minor").toBool());QVERIFY(value.value("chromatic").toBool());}
    config["manualKey"]=false;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QTRY_COMPARE(panel->property("summaryDegreeText").toString(),QString("V7"));QVERIFY(!panel->property("chromaticChord").toBool());QCOMPARE(score->metaTag("kumo.harmony"),saved);
    score->setReadOnly(true);frame["root"]=7;frame["chord"]="G/E";document["frames"]=QVariantList{frame,end};QVERIFY(write(document));QVERIFY(import());QCOMPARE(score->metaTag("kumo.harmony"),saved);QCOMPARE(panel->property("summaryChordText").toString(),original);score->setReadOnly(false);
    document["fingerprint"]=QString(64,'a');QVERIFY(write(document));QVERIFY(import());QCOMPARE(score->metaTag("kumo.harmony"),saved);QCOMPARE(panel->property("summaryChordText").toString(),original);
    QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);
}

void TestPluginHost::harmonyColorAndPdf113()
{
    const auto source=_settings.filePath("harmony-color-source-113.mscx");QVERIFY(QFile::copy(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx",source));
    auto main=Ms::mscore;auto score=main->readScore(source);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1200,900);main->show();
    auto note=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes().front();note->setColor(QColor("#b57422"));const auto originalColor=note->color();score->deselectAll();
    showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY(panel->property("manualOverrides").value<QJSValue>().toVariant().toList().isEmpty());
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["coloring"]=false;config["allColor"]=false;config["noteFunctions"]=false;config["chordLabels"]=false;
    QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTest::qWait(150);auto view=main->currentScoreView();const auto plain=view->grab().toImage();
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(main->savePdf(score,out.filePath("harmony-clean-113.pdf")));
    const auto metadata=score->metaTag("kumo.harmony");config["coloring"]=true;config["allColor"]=true;config["noteFunctions"]=true;config["chordLabels"]=true;
    QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QTest::qWait(80);
    QVERIFY(view->grab().toImage()!=plain);QCOMPARE(note->color(),originalColor);QCOMPARE(score->metaTag("kumo.harmony"),metadata);QVERIFY(view->grab().copy(80,50,500,260).save(out.filePath("harmony-color-and-functions-113.png")));
    QVERIFY(main->savePdf(score,out.filePath("harmony-preview-113.pdf")));QCOMPARE(note->color(),originalColor);
    config["coloring"]=false;config["allColor"]=false;config["noteFunctions"]=false;config["chordLabels"]=false;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTest::qWait(150);
    QCOMPARE(note->color(),originalColor);QCOMPARE(view->grab().toImage(),plain);QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);
}

void TestPluginHost::harmonyScopedImport113()
{
    const auto source=_settings.filePath("harmony-scoped-113.mscx");QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray xml="<museScore version=\"3.02\"><Score><Division>480</Division>";
    for(int staff=1;staff<=2;++staff)xml+="<Part><Staff id=\""+QByteArray::number(staff)+"\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel/></Instrument></Part>";
    for(int staff=1;staff<=2;++staff)xml+="<Staff id=\""+QByteArray::number(staff)+"\"><Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>whole</durationType><Note><pitch>48</pitch><tpc>14</tpc></Note><Note><pitch>52</pitch><tpc>18</tpc></Note><Note><pitch>55</pitch><tpc>15</tpc></Note></Chord></voice></Measure></Staff>";
    file.write(xml+"</Score></museScore>");file.close();auto main=Ms::mscore;auto score=main->readScore(source);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    auto first=toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front();auto second=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->notes().front();score->select(first);showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["scope"]=1;config["resultMode"]=1;config["chordLabels"]=true;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY(QMetaObject::invokeMethod(panel,"editRange",Q_ARG(QVariant,"assign"),Q_ARG(QVariant,0),Q_ARG(QVariant,1920),Q_ARG(QVariant,0),Q_ARG(QVariant,3),Q_ARG(QVariant,0)));
    score->select(second);QVERIFY(QMetaObject::invokeMethod(panel,"refreshSelection"));QVERIFY(QMetaObject::invokeMethod(panel,"editRange",Q_ARG(QVariant,"assign"),Q_ARG(QVariant,0),Q_ARG(QVariant,1920),Q_ARG(QVariant,0),Q_ARG(QVariant,2),Q_ARG(QVariant,0)));
    QVERIFY(QMetaObject::invokeMethod(panel,"moveAnnotation",Q_ARG(QVariant,0),Q_ARG(QVariant,4),Q_ARG(QVariant,1.5),Q_ARG(QVariant,-1.)));
    auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);
    auto selectPart=[&](Note* note){score->select(note);auto c=panel->property("configuration").value<QJSValue>().toVariant().toMap();c["scope"]=0;return QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,c))&&QMetaObject::invokeMethod(panel,"refreshSelection");};
    auto exchange=[&](const QString& action,const QString& path){return panel->setProperty("pendingFileAction",action)&&QMetaObject::invokeMethod(panel,"exchangeFile",Q_ARG(QVariant,QUrl::fromLocalFile(path).toString()));};
    auto exportPart=[&](int part){const auto path=_settings.filePath(QString("harmony-scoped-%1-113.json").arg(part));if(!exchange("export-analysis",path))return QVariantMap();QFile f(path);if(!f.open(QIODevice::ReadOnly))return QVariantMap();return QJsonDocument::fromJson(f.readAll()).toVariant().toMap();};
    auto importPart=[&](const QVariantMap& value){const auto path=_settings.filePath("harmony-scoped-import-113.json");QFile f(path);if(!f.open(QIODevice::WriteOnly))return false;f.write(QJsonDocument::fromVariant(value).toJson());f.close();return exchange("import-analysis",path);};
    auto has=[&](int part,int definition,const QString& sourceName){for(const auto& value:observer->workflowData("harmony").value("overrides").toList()){const auto row=value.toMap();if(row.value("part").toInt()==part&&row.value("definition").toInt()==definition&&row.value("source").toString()==sourceName)return true;}return false;};
    QVERIFY(selectPart(second));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QCOMPARE(panel->property("firstTrack").toInt(),4);auto secondDoc=exportPart(4);QVERIFY(!secondDoc.isEmpty());
    for(const auto& value:secondDoc.value("regions").toList())QCOMPARE(value.toMap().value("part").toInt(),4);
    secondDoc["overrides"]=QVariantList();QVERIFY(importPart(secondDoc));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QVERIFY(has(0,3,"manual"));QVERIFY(has(4,2,"imported"));
    QVERIFY(selectPart(first));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);auto firstDoc=exportPart(0);QVERIFY(!firstDoc.isEmpty());
    for(const auto& value:firstDoc.value("regions").toList())QCOMPARE(value.toMap().value("part").toInt(),0);
    for(const auto& value:firstDoc.value("overrides").toList())QCOMPARE(value.toMap().value("part").toInt(),0);
    QVERIFY(firstDoc.value("positions").toList().isEmpty());auto changed=firstDoc.value("regions").toList();QVERIFY(!changed.isEmpty());auto row=changed.front().toMap();row["definition"]=4;row["chord"]="Cm7";changed[0]=row;firstDoc["regions"]=changed;firstDoc["overrides"]=QVariantList();
    QVERIFY(importPart(firstDoc));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QVERIFY(has(0,4,"imported"));QVERIFY(has(4,2,"imported"));QCOMPARE(observer->workflowData("harmony").value("positions").toList().front().toMap().value("part").toInt(),4);
    QVERIFY(QMetaObject::invokeMethod(panel,"reanalyseImported"));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QVERIFY(!panel->property("usingImportedAnalysis").toBool());QVERIFY(has(4,2,"imported"));QVERIFY(!has(0,4,"imported"));
    main->cmd(getAction("undo"));QTRY_VERIFY(panel->property("usingImportedAnalysis").toBool());QVERIFY(has(0,4,"imported"));QVERIFY(has(4,2,"imported"));QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);
    auto reopened=main->readScore(source);QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));showHarmonyPanel(true);panel=harmonyPanel077();QVERIFY(panel);QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);QVERIFY(has(0,4,"imported"));QVERIFY(has(4,2,"imported"));showHarmonyPanel(false);main->closeScore(reopened);
}
