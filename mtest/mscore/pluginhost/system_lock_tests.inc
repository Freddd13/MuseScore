void systemLocksUndoAndFile() {
    const QString path = _settings.filePath("system-locks.mscx");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray xml("<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><Instrument id=\"piano\"><Channel/></Instrument></Part><Staff id=\"1\">");
    for (int i = 0; i < 8; ++i) {
        xml += "<Measure><voice>";
        if (i == 0) xml += "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
        xml += "<Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice>";
        if (i == 0) xml += "<LayoutBreak><subtype>nobreak</subtype></LayoutBreak>";
        if (i == 3) xml += "<LayoutBreak><subtype>page</subtype></LayoutBreak>";
        xml += "</Measure>";
    }
    xml += "</Staff></Score></museScore>"; QCOMPARE(file.write(xml),qint64(xml.size()));file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->show();score->doLayout();
    QVector<Measure*> bars;for(auto bar=score->firstMeasure();bar;bar=bar->nextMeasure())bars.append(bar);QCOMPARE(bars.size(),8);QVERIFY(bars[0]->noBreak());QVERIFY(bars[3]->pageBreak());QVERIFY(bars[0]->system()!=bars[4]->system());
    score->select(score->findCR(bars[1]->tick(),0));const auto idx=score->undoStack()->getCurIdx();auto action=main->findChild<QAction*>("kumo-system-lock-current");QVERIFY(action);action->trigger();QCOMPARE(score->undoStack()->getCurIdx(),idx+1);QVERIFY(bars[1]->noBreak());QVERIFY(bars[2]->noBreak());QVERIFY(bars[3]->pageBreak());
    auto count=[](Score* s){int count=0;for(auto bar=s->first();bar;bar=bar->next())for(auto element:bar->el())if(element->isLayoutBreak()&&!toLayoutBreak(element)->systemLockData().isEmpty())++count;return count;};QVERIFY(count(score)>0);
    score->select(score->findCR(bars[4]->tick(),0));const auto second=changeSystemLocks(score,true,false);QCOMPARE(second["count"].toInt(),1);QVERIFY(bars[7]->lineBreak());
    const QString savedPath=_settings.filePath("locked-systems.mscz");QFileInfo saved(savedPath);QVERIFY(score->saveCompressedFile(saved,false,false));std::unique_ptr<MasterScore> restored(main->readScore(savedPath));QVERIFY(restored);QCOMPARE(count(restored.get()),count(score));
    QFile savedInput(savedPath);QVERIFY(savedInput.open(QIODevice::ReadOnly));QFile artifact(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("locked-systems-068.mscz"));QVERIFY(artifact.open(QIODevice::WriteOnly));QCOMPARE(artifact.write(savedInput.readAll()),savedInput.size());artifact.close();
    auto restoredFirst=restored->firstMeasure();auto restoredPage=restored->tick2measure(Fraction::fromTicks(5760));restored->startCmd();restored->insertMeasure(ElementType::MEASURE,restoredFirst);restored->endCmd();restored->doLayout();
    const auto cleared=changeSystemLocks(restored.get(),false,true,true);QCOMPARE(cleared["count"].toInt(),2);QVERIFY(restoredFirst->noBreak());QVERIFY(restoredPage->pageBreak());QVERIFY(!restored->lastMeasure()->lineBreak());
    score->select(score->findCR(bars[0]->tick(),0));auto unlocked=changeSystemLocks(score,false,true);QCOMPARE(unlocked["count"].toInt(),1);QVERIFY(bars[0]->noBreak());QVERIFY(!bars[1]->noBreak());QVERIFY(bars[3]->pageBreak());QVERIFY(bars[4]->noBreak());
    EditData edit;score->undoRedo(true,&edit);QVERIFY(bars[1]->noBreak());QVERIFY(bars[4]->noBreak());score->undoRedo(false,&edit);QVERIFY(!bars[1]->noBreak());QVERIFY(bars[4]->noBreak());
    const auto all=changeSystemLocks(score,false,true,true);QCOMPARE(all["count"].toInt(),1);QVERIFY(!bars[7]->lineBreak());QVERIFY(bars[3]->pageBreak());QVERIFY(bars[0]->noBreak());
    auto menu=main->findChild<QMenu*>("kumo-system-lock-menu");QVERIFY(menu);menu->popup(main->mapToGlobal(QPoint(100,100)));QTest::qWait(80);QVERIFY(menu->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("system-lock-menu-068.png")));menu->hide();
}
void upstreamLiveTranslationAndPreferences() {
    QVERIFY(preferences.getBool(PREF_APP_DEBUG_LOG_AUTOSCROLL));
    QTranslator chinese;QVERIFY(chinese.load(QString(TESTROOT)+"/share/locale/mscore_zh_CN.qm"));
    DebugLogDock logger;PianorollEditor piano;auto search=logger.findChild<QLineEdit*>();QVERIFY(search);search->setPlaceholderText("__before_language_change__");const auto original=search->placeholderText();
    qApp->installTranslator(&chinese);QEvent changed(QEvent::LanguageChange);QCoreApplication::sendEvent(&logger,&changed);QCoreApplication::sendEvent(&piano,&changed);
    QVERIFY(search->placeholderText()!=original);bool translated=false;for(auto label:piano.findChildren<QLabel*>())if(label->text()==chinese.translate("Ms::PianorollEditor","Cursor:"))translated=true;QVERIFY(translated);qApp->removeTranslator(&chinese);
}
