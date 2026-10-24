// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
void TestPluginHost::harmonySpellingAcrossViews109()
{
    QFile data(QString(TESTROOT)+"/mtest/mscore/pluginhost/harmony_theory_109.json");
    QVERIFY(data.open(QIODevice::ReadOnly));
    const auto cases=QJsonDocument::fromJson(data.readAll()).object().value("cases").toArray();
    auto main=Ms::mscore;QVERIFY(main);main->resize(1200,900);main->show();
    int checked=0;
    for(const auto& value:cases) {
        const auto row=value.toObject().toVariantMap();const auto pitches=row["pitches"].toList(),tpcs=row["tpc"].toList();
        QByteArray xml("<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division>"
            "<Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel><program value=\"0\"/><synti>Fluid</synti></Channel></Instrument></Part>"
            "<Staff id=\"1\"><Measure><voice><KeySig><accidental>");
        xml+=QByteArray::number(row["key"].toInt())+"</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>whole</durationType>";
        for(int i=0;i<pitches.size();++i)xml+="<Note><pitch>"+QByteArray::number(pitches[i].toInt())+"</pitch><tpc>"+QByteArray::number(tpcs[i].toInt())+"</tpc></Note>";
        xml+="</Chord></voice></Measure></Staff></Score></museScore>";
        const auto path=_settings.filePath(QString("harmony-theory-%1-109.mscx").arg(checked));QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
        auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
        auto chord=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));QVERIFY(chord);score->select(chord->notes().front());
        showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
        // Opening the next score must work without pressing Analyse or refreshing.
        auto observed=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observed);
        QTRY_VERIFY(observed->wrappedScore() && observed->wrappedScore()->score()==score);
        if(!row["specified"].toBool())
            QTRY_COMPARE_WITH_TIMEOUT(panel->property("summaryChordText").toString(),row["chord"].toString(),10000);
        auto configuration=panel->property("configuration").toMap();
        configuration["schema"]=1;configuration["auto"]=true;configuration["manualKey"]=false;configuration["keyMode"]=row["minor"].toBool()?1:0;
        configuration["degreeMode"]=row.value("degreeMode",1);configuration["pedalMode"]=1;configuration["window"]=0;
        configuration["coloring"]=false;configuration["chordLabels"]=true;configuration["chordContent"]=2;configuration["holdLastHarmony"]=false;
        QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,configuration)));
        QVERIFY(QMetaObject::invokeMethod(panel,"refreshSelection"));
        QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
        QTest::qWait(100);
        QTRY_VERIFY(observed->wrappedScore() && observed->wrappedScore()->score()==score);
        if(row["specified"].toBool()) {
            // These cases deliberately choose one interpretation of an ambiguous set.
            QVERIFY(QMetaObject::invokeMethod(panel,"editRange",Q_ARG(QVariant,"assign"),Q_ARG(QVariant,0),Q_ARG(QVariant,1920),Q_ARG(QVariant,0),Q_ARG(QVariant,row["definition"]),Q_ARG(QVariant,0)));
        }
        auto mode=[&](int target) {return QMetaObject::invokeMethod(panel,"changeSummary",Q_ARG(QVariant,"resultMode"),Q_ARG(QVariant,target));};
        if(!row["specified"].toBool()) {
            QVERIFY(mode(0));QVERIFY(QMetaObject::invokeMethod(panel,"updateTexts"));
            QTRY_COMPARE(panel->property("chordText").toString(),row["chord"].toString());
            QCOMPARE(panel->property("degreeText").toString(),row["degree"].toString());
        }
        QVERIFY(mode(1));QVERIFY(QMetaObject::invokeMethod(panel,"updateTexts"));
        QTRY_COMPARE(panel->property("summaryChordText").toString(),row["chord"].toString());
        QCOMPARE(panel->property("summaryDegreeText").toString(),row["degree"].toString());
        QCOMPARE(panel->property("chordText").toString(),row["chord"].toString());
        QCOMPARE(panel->property("degreeText").toString(),row["degree"].toString());
        auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);
        QVariant direct;QVERIFY(QMetaObject::invokeMethod(panel,"frameResult",Q_RETURN_ARG(QVariant,direct),Q_ARG(QVariant,observer->contextSnapshot(0,0,4,true,0,false))));
        const auto records=panel->property("analysisRecords").value<QJSValue>().toVariant().toList();QVERIFY(!records.isEmpty());
        for(const auto& field:{"root","definition","chord","degree","rootSpelling","tonicSpelling","chromatic"})QCOMPARE(records.front().toMap()[field],direct.toMap()[field]);
        const auto published=observer->arrangementState().value("harmonyAnalysis").toMap().value("regions").toList();
        QVERIFY(!published.isEmpty());QCOMPARE(published.front().toMap()["chord"].toString(),row["chord"].toString());
        QCOMPARE(published.front().toMap()["degree"].toString(),row["degree"].toString());
        const auto region=published.front().toMap();const auto notes=region["notes"].toList();QVERIFY(!notes.isEmpty());
        QVariant descriptor;
        QVERIFY(QMetaObject::invokeMethod(panel,"chordDescriptor",Q_RETURN_ARG(QVariant,descriptor),Q_ARG(QVariant,notes.front()),Q_ARG(QVariant,region)));
        QCOMPARE(descriptor.toMap()["chord"].toString(),row["chord"].toString());
        QCOMPARE(descriptor.toMap()["degree"].toString(),row["degree"].toString());
        const auto artifacts=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");
        if(!artifacts.isEmpty() && (checked==1 || checked==6)) {
            QTest::qWait(70);QVERIFY(main->currentScoreView()->grab().save(QDir(artifacts).filePath(QString("harmony-theory-%1-109-score.png").arg(checked))));
            QVERIFY(QMetaObject::invokeMethod(panel,"setPanelFloating",Q_ARG(bool,true)));
            QVERIFY(QMetaObject::invokeMethod(panel,"resizePanel",Q_ARG(int,380),Q_ARG(int,850)));QTest::qWait(80);
            QVERIFY(panel->window()->grabWindow().save(QDir(artifacts).filePath(QString("harmony-theory-%1-109-panel.png").arg(checked))));
        }
        qInfo()<<"Harmony theory"<<row["name"].toString()<<row["chord"].toString()<<row["degree"].toString();
        QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);++checked;
    }
    QCOMPARE(checked,23);
}
