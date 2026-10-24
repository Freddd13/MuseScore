// GPL-2.0-or-later. Optional read-only corpus inspection; source stays unchanged.
void TestPluginHost::harmonyReferenceCorpus111()
{
    const auto path=qEnvironmentVariable("P2_HARMONY_REFERENCE");
    if(path.isEmpty())QSKIP("Reference corpus is supplied separately");
    QFile input(path);QVERIFY(input.open(QIODevice::ReadOnly));
    const auto before=QCryptographicHash::hash(input.readAll(),QCryptographicHash::Sha256);input.close();
    auto main=Ms::mscore;QVERIFY(main);main->resize(1200,900);main->show();
    std::unique_ptr<MasterScore> score(main->readScore(path));QVERIFY(score);
    PluginAPI::Score wrapped(score.get());PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    QVariantList variants;QElapsedTimer timer;
    for(int window:{0,240,480}) {
        timer.start();QVariantList frames;int next=0,pages=0;
        do {
            const auto batch=observer.analysisFrames(next,32,0,score->nstaves()*VOICES,true,window);
            frames.append(batch["frames"].toList());next=batch["nextTick"].toInt();++pages;
            QVERIFY(pages<10000);
        }while(next>=0);
        QVERIFY(!frames.isEmpty());variants.append(QVariantMap{{"window",window},{"frames",frames},{"milliseconds",timer.nsecsElapsed()/1000000.}});
    }
    QVariantMap report{{"source",path},{"sha256",QString::fromLatin1(before.toHex())},{"title",score->metaTag("workTitle")},{"variants",variants}};
    QFile output(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")).filePath("harmony-reference-111.json"));
    QVERIFY(output.open(QIODevice::WriteOnly));output.write(QJsonDocument::fromVariant(report).toJson());output.close();
    QVERIFY(input.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(input.readAll(),QCryptographicHash::Sha256),before);
}

void TestPluginHost::harmonySegmentCorpus111()
{
    QFile data(QString(TESTROOT)+"/mtest/mscore/pluginhost/harmony_segments_111.json");QVERIFY(data.open(QIODevice::ReadOnly));
    const auto cases=QJsonDocument::fromJson(data.readAll()).object()["cases"].toArray();
    auto duration=[](int ticks) {
        static const QMap<int,QByteArray> names{{60,"32nd"},{120,"16th"},{240,"eighth"},{480,"quarter"},{960,"half"},{1920,"whole"}};
        if(names.contains(ticks))return QByteArray("<durationType>")+names[ticks]+"</durationType>";
        return QByteArray("<dots>1</dots><durationType>")+names.value(ticks*2/3)+"</durationType>";
    };
    auto rests=[&](int ticks) {
        QByteArray text;for(int length:{1920,1440,960,720,480,360,240,180,120,60})while(ticks>=length){text+="<Rest>"+duration(length)+"</Rest>";ticks-=length;}
        return text;
    };
    int checked=0;auto main=Ms::mscore;main->resize(1200,900);main->show();
    for(const auto& value:cases) {
        const auto row=value.toObject().toVariantMap();const auto signature=row["signature"].toList();
        const auto notes=row["notes"].toList();const int end=row["end"].toInt();
        QByteArray xml("<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><Staff id=\"2\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel/></Instrument></Part>");
        for(int staff=0;staff<2;++staff) {
            xml+="<Staff id=\""+QByteArray::number(staff+1)+"\"><Measure>";
            int lastVoice=0;for(const auto& n:notes)if(n.toMap()["track"].toInt()/4==staff)lastVoice=qMax(lastVoice,n.toMap()["track"].toInt()%4);
            for(int voice=0;voice<=lastVoice;++voice) {
                xml+="<voice>";
                if(!voice)xml+="<Clef><concertClefType>"+QByteArray(staff?"F":"G")+"</concertClefType><transposingClefType>"+QByteArray(staff?"F":"G")+"</transposingClefType></Clef><KeySig><accidental>"+QByteArray::number(row["key"].toInt())+"</accidental></KeySig><TimeSig><sigN>"+QByteArray::number(signature[0].toInt())+"</sigN><sigD>"+QByteArray::number(signature[1].toInt())+"</sigD></TimeSig>";
                QMap<int,QVariantList> onsets;for(const auto& n:notes)if(n.toMap()["track"].toInt()==staff*4+voice)onsets[n.toMap()["tick"].toInt()].append(n);
                int cursor=0;for(auto it=onsets.cbegin();it!=onsets.cend();++it) {
                    QVERIFY(it.key()>=cursor);xml+=rests(it.key()-cursor);const int until=it.value().front().toMap()["end"].toInt();
                    xml+="<Chord>"+duration(until-it.key());
                    for(const auto& n:it.value()) {const auto note=n.toMap();QCOMPARE(note["end"].toInt(),until);xml+="<Note><pitch>"+QByteArray::number(note["pitch"].toInt())+"</pitch><tpc>"+QByteArray::number(note["tpc"].toInt())+"</tpc></Note>";}
                    xml+="</Chord>";cursor=until;
                }
                xml+=rests(end-cursor);xml+="</voice>";
            }
            xml+="</Measure></Staff>";
        }
        xml+="</Score></museScore>";
        const auto path=_settings.filePath(QString("harmony-segment-%1-111.mscx").arg(checked));QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
        auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
        QCOMPARE(score->lastMeasure()->endTick().ticks(),end);
        score->startCmd();for(const auto& p:row["pedals"].toList()){const auto window=p.toMap();auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction::fromTicks(window["start"].toInt()));pedal->setTick2(Fraction::fromTicks(window["end"].toInt()));score->undoAddElement(pedal);}score->endCmd();score->doLayout();
        Note* first=nullptr;for(auto segment=score->firstSegment(SegmentType::ChordRest);segment&&!first;segment=segment->next1(SegmentType::ChordRest))for(int track=0;track<8&&!first;++track)if(auto e=segment->element(track))if(e->isChord())first=toChord(e)->notes().front();
        QVERIFY(first);score->select(first);showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
        auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();QCOMPARE(config.value("schema").toInt(),1);config["auto"]=true;config["manualKey"]=false;config["keyMode"]=0;config["pedal"]=true;config["pedalMode"]=1;config["window"]=0;config["coloring"]=false;config["chordLabels"]=true;config["chordContent"]=2;config["resultMode"]=1;
        QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));
        QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
        auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);
        const auto regions=observer->arrangementState().value("harmonyAnalysis").toMap().value("regions").toList(),expected=row["expected"].toList();
        QCOMPARE(regions.size(),expected.size());
        for(int j=0;j<expected.size();++j)for(const auto& field:{"start","end","chord","root","definition","degree"})QVERIFY2(regions[j].toMap()[field]==expected[j].toMap()[field],qUtf8Printable(row["name"].toString()+" "+field+": "+regions[j].toMap()[field].toString()+" / "+expected[j].toMap()[field].toString()));
        const auto artifacts=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");
        if(!artifacts.isEmpty()&&(checked==0||checked==12)){QTest::qWait(80);auto view=main->currentScoreView();QVERIFY(view->grab(QRect(80,35,qMin(680,view->width()-80),qMin(390,view->height()-35))).save(QDir(artifacts).filePath(QString("harmony-segment-%1-111.png").arg(checked))));}
        QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);++checked;
    }
    QCOMPARE(checked,cases.size());
}

void TestPluginHost::harmonyCancelAndSwitch111()
{
    const auto path=qEnvironmentVariable("P2_HARMONY_REFERENCE");if(path.isEmpty())QSKIP("Reference corpus is supplied separately");
    auto main=Ms::mscore;auto reference=main->readScore(path);QVERIFY(reference);main->setCurrentScoreView(main->appendScore(reference));main->show();
    Note* first=nullptr;for(auto segment=reference->firstSegment(SegmentType::ChordRest);segment&&!first;segment=segment->next1(SegmentType::ChordRest))for(int track=0;track<reference->nstaves()*VOICES&&!first;++track)if(auto e=segment->element(track))if(e->isChord())first=toChord(e)->notes().front();
    QVERIFY(first);reference->select(first);showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();QCOMPARE(config.value("schema").toInt(),1);config["auto"]=true;config["manualKey"]=false;config["keyMode"]=0;config["pedal"]=true;config["pedalMode"]=1;config["window"]=0;config["chordLabels"]=true;config["resultMode"]=1;
    QElapsedTimer analysisClock;analysisClock.start();qint64 lastPulse=0;QVector<double> gaps;QVariantList slowPulses;QTimer pulse;pulse.setInterval(10);
    QObject::connect(&pulse,&QTimer::timeout,[&]{const auto now=analysisClock.elapsed();const auto gap=now-lastPulse;gaps.append(double(gap));if(gap>25)slowPulses.append(QVariantMap{{"at",now},{"gap",gap},{"nextTick",panel->property("analysisNextTick")},{"worker",panel->property("regionJobToken")},{"dirty",panel->property("analysisDirty")}});lastPulse=now;});pulse.start();
    QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);pulse.stop();QVERIFY(!gaps.isEmpty());std::sort(gaps.begin(),gaps.end());
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS"));QFile performance(output.filePath("harmony-native-performance-111.json"));QVERIFY(performance.open(QIODevice::WriteOnly));
    performance.write(QJsonDocument::fromVariant(QVariantMap{{"analysisMilliseconds",analysisClock.nsecsElapsed()/1000000.},{"pulses",gaps.size()},{"pulseInterval",10},{"p95GapMilliseconds",gaps[int(gaps.size()*.95)]},{"maximumGapMilliseconds",gaps.back()},{"slowPulses",slowPulses}}).toJson());performance.close();
    QVERIFY(QMetaObject::invokeMethod(panel,"optionsChanged",Q_ARG(QVariant,true)));
    QTRY_VERIFY_WITH_TIMEOUT(panel->property("regionJobToken").toInt()>0,10000);
    auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);QSignalSpy finished(observer,SIGNAL(readOnlyJobFinished(int,QVariantMap)));QVERIFY(finished.isValid());
    const int oldToken=panel->property("regionJobToken").toInt();QElapsedTimer timer;timer.start();
    const auto nextPath=_settings.filePath("harmony-cancel-next-111.mscx");QFile file(nextPath);QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel/></Instrument></Part><Staff id=\"1\"><Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>whole</durationType><Note><pitch>60</pitch><tpc>14</tpc></Note><Note><pitch>64</pitch><tpc>18</tpc></Note><Note><pitch>67</pitch><tpc>15</tpc></Note></Chord></voice></Measure></Staff></Score></museScore>");file.close();
    auto next=main->readScore(nextPath);QVERIFY(next);main->setCurrentScoreView(main->appendScore(next));next->select(toChord(next->firstSegment(SegmentType::ChordRest)->element(0))->notes().front());
    QVERIFY(QMetaObject::invokeMethod(panel,"refreshSelection"));
    QTRY_VERIFY(observer->wrappedScore() && observer->wrappedScore()->score()==next);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QTRY_COMPARE(panel->property("summaryChordText").toString(),QString("C"));
    const double ready=timer.nsecsElapsed()/1000000.;QTest::qWait(300);QCOMPARE(panel->property("summaryChordText").toString(),QString("C"));
    for(const auto& emission:finished)QVERIFY(emission[0].toInt()!=oldToken);
    QFile report(output.filePath("harmony-cancel-111.json"));QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument::fromVariant(QVariantMap{{"cancelledToken",oldToken},{"newScoreReadyMilliseconds",ready},{"newChord",panel->property("summaryChordText")}}).toJson());
    showHarmonyPanel(false);QVERIFY(main->saveFile());main->closeScore(next);QVERIFY(!reference->dirty());main->closeScore(reference);
}
