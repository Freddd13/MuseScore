// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
void TestPluginHost::harmonyPartKeys112()
{
    const auto path=_settings.filePath("harmony-part-keys-112.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray xml="<museScore version=\"3.02\"><Score><Division>480</Division>";
    for(int staff=1;staff<=2;++staff)xml+="<Part><Staff id=\""+QByteArray::number(staff)+"\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel><program value=\"0\"/><synti>Fluid</synti></Channel></Instrument></Part>";
    for(int staff=1;staff<=2;++staff)xml+="<Staff id=\""+QByteArray::number(staff)+"\"><Measure><voice><KeySig><accidental>"+QByteArray::number(staff-1)+"</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>whole</durationType><Note><pitch>48</pitch><tpc>14</tpc></Note><Note><pitch>52</pitch><tpc>18</tpc></Note><Note><pitch>55</pitch><tpc>15</tpc></Note></Chord></voice></Measure></Staff>";
    file.write(xml+"</Score></museScore>");file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1200,900);main->show();
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    const auto frame=observer.contextSnapshot(0,0,8,false,0,false);const auto notes=frame.value("analysisNotes").toList();
    QCOMPARE(notes.size(),6);
    const auto parts=frame.value("parts").toList();QCOMPARE(parts.size(),2);QCOMPARE(parts[0].toMap().value("keySignature").toInt(),0);QCOMPARE(parts[1].toMap().value("keySignature").toInt(),1);
    auto first=toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front();score->select(first);showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();QCOMPARE(config.value("schema").toInt(),1);config["auto"]=true;config["scope"]=1;config["manualKey"]=false;config["keyMode"]=0;config["pedal"]=false;config["window"]=0;config["chordLabels"]=true;config["resultMode"]=1;
    QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    auto owned=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(owned);const auto regions=owned->arrangementState().value("harmonyAnalysis").toMap().value("regions").toList();QCOMPARE(regions.size(),2);
    for(int part=0;part<2;++part){const auto region=regions[part].toMap();QCOMPARE(region.value("part").toInt(),part*4);QCOMPARE(region.value("chord").toString(),QString("C"));QCOMPARE(region.value("degree").toString(),QString(part?"IV":"I"));}
    score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->notes().front());QVERIFY(QMetaObject::invokeMethod(panel,"refreshSelection"));QTRY_COMPARE(panel->property("summaryDegreeText").toString(),QString("IV"));
    const auto artifacts=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!artifacts.isEmpty())QVERIFY(main->currentScoreView()->grab().copy(80,60,400,240).save(QDir(artifacts).filePath("harmony-part-keys-112.png")));
    QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);
}

void TestPluginHost::harmonyWindow112()
{
    const auto path=_settings.filePath("harmony-window-112.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel/></Instrument></Part><Staff id=\"1\"><Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>eighth</durationType><Note><pitch>48</pitch><tpc>14</tpc></Note></Chord><Chord><durationType>eighth</durationType><Note><pitch>52</pitch><tpc>18</tpc></Note></Chord><Chord><durationType>quarter</durationType><Note><pitch>55</pitch><tpc>15</tpc></Note></Chord><Rest><durationType>half</durationType></Rest></voice></Measure></Staff></Score></museScore>");file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    for(int window:{0,240,480}) {
        const auto notes=observer.contextSnapshot(480,0,4,false,window,false).value("analysisNotes").toList();
        QCOMPARE(notes.size(),window==0?1:window==240?2:3);
        QCOMPARE(observer.contextSnapshot(960,0,4,false,window,false).value("analysisNotes").toList().size(),0);
        const auto batch=observer.analysisFrames(0,32,0,4,false,window).value("frames").toList();
        bool found=false;for(const auto& value:batch)if(value.toMap().value("tick").toInt()==480){QCOMPARE(value.toMap().value("analysisNotes").toList(),notes);found=true;}QVERIFY(found);
    }
    score->select(toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes().front());showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();QCOMPARE(config.value("schema").toInt(),1);config["auto"]=true;config["scope"]=0;config["manualKey"]=false;config["keyMode"]=0;config["pedal"]=false;config["window"]=2;config["chordLabels"]=true;config["resultMode"]=0;
    QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QTRY_COMPARE(panel->property("currentTick").toInt(),480);
    QCOMPARE(panel->property("configuration").value<QJSValue>().property("window").toInt(),2);
    QTRY_COMPARE(panel->property("summaryChordText").toString(),QString("C/G"));
    config["resultMode"]=1;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QTRY_COMPARE(panel->property("summaryChordText").toString(),QString("C/G"));
    QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);
}
