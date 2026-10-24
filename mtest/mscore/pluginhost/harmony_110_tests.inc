// GPL-2.0-or-later. Pedal regions retain explicit interpretation and key changes.
void TestPluginHost::harmonyPedalContext110()
{
    auto main=Ms::mscore;QVERIFY(main);main->resize(1200,900);main->show();
    auto chordXml=[](const QByteArray& duration,const QList<int>& pitches,const QList<int>& tpcs) {
        QByteArray out="<Chord>";
        if(duration=="half")out+="<dots>1</dots>";
        out+="<durationType>"+duration+"</durationType>";
        for(int n=0;n<pitches.size();++n)out+="<Note><pitch>"+QByteArray::number(pitches[n])+"</pitch><tpc>"+QByteArray::number(tpcs[n])+"</tpc></Note>";
        return out+"</Chord>";
    };
    for(int scenario=0;scenario<2;++scenario) {
        QByteArray xml("<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division>"
            "<Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel><program value=\"0\"/><synti>Fluid</synti></Channel></Instrument></Part>"
            "<Staff id=\"1\"><Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>");
        xml+=chordXml(scenario?"quarter":"whole",{48,52,55},{14,18,15});
        if(scenario)xml+=chordXml("half",{43,59,62},{15,19,16});
        xml+="</voice></Measure>";
        if(!scenario)xml+="<Measure><voice><KeySig><accidental>1</accidental></KeySig>"+chordXml("whole",{48,52,55},{14,18,15})+"</voice></Measure>";
        xml+="</Staff></Score></museScore>";
        const auto path=_settings.filePath(QString("harmony-pedal-context-%1-110.mscx").arg(scenario));
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
        auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
        auto pedal=new Pedal(score);pedal->setTrack(0);pedal->setTrack2(0);pedal->setTick(Fraction());pedal->setTick2(score->lastMeasure()->endTick());
        score->startCmd();score->undoAddElement(pedal);score->endCmd();score->doLayout();
        score->select(toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0))->notes().front());
        showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
        auto config=panel->property("configuration").toMap();config["auto"]=true;config["manualKey"]=false;config["keyMode"]=0;config["pedal"]=true;config["pedalMode"]=0;
        config["window"]=0;config["coloring"]=false;config["chordLabels"]=true;config["chordContent"]=2;config["resultMode"]=1;
        QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));
        QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
        auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);
        QVERIFY(!observer->contextSnapshot(0,0,4,true,0,false).value("pedalWindows").toList().isEmpty());
        if(scenario) {
            auto nextChord=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0));QVERIFY(nextChord);
            QCOMPARE(nextChord->endTick().ticks(),1920);
            QVariantList records;
            const auto frames=observer->analysisFrames(0,32,0,4,true,0).value("frames").toList();
            for(const auto& frame:frames) {
                if(frame.toMap()["tick"].toInt()>=score->lastMeasure()->endTick().ticks())continue;
                QVariant described;QVERIFY(QMetaObject::invokeMethod(panel,"frameResult",Q_RETURN_ARG(QVariant,described),Q_ARG(QVariant,frame)));
                auto row=described.toMap();const bool first=row["tick"].toInt()==0;row["root"]=first?0:7;row["definition"]=0;row["chord"]=first?"C":"G";row["degree"]=first?"I":"V";
                records.append(row);
            }
            QCOMPARE(records.size(),2);QVERIFY(panel->setProperty("importedRecords",records));
            QVERIFY(QMetaObject::invokeMethod(panel,"optionsChanged",Q_ARG(QVariant,true)));
            QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
        }
        const auto regions=observer->arrangementState().value("harmonyAnalysis").toMap().value("regions").toList();
        QCOMPARE(regions.size(),2);const auto a=regions[0].toMap(),b=regions[1].toMap();
        QCOMPARE(a["chord"].toString(),QString("C"));QCOMPARE(a["degree"].toString(),QString("I"));
        QCOMPARE(a["end"].toInt(),scenario?480:1920);QCOMPARE(b["start"].toInt(),scenario?480:1920);
        QCOMPARE(b["chord"].toString(),QString(scenario?"G":"C"));QCOMPARE(b["degree"].toString(),QString(scenario?"V":"IV"));
        if(scenario)QCOMPARE(b["source"].toString(),QString("imported"));
        const auto artifacts=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");
        if(!artifacts.isEmpty()){QTest::qWait(80);QVERIFY(main->currentScoreView()->grab().save(QDir(artifacts).filePath(QString("harmony-pedal-context-%1-110-score.png").arg(scenario))));}
        QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);
    }
}
