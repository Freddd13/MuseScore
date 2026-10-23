// Import the complete performance, edit a middle passage, save and read it back.
static QList<int> expressionPitches095(int attack)
{
    const int bar=attack/8, degree=(bar*3+attack*2+attack/5)%12;
    return {36+(bar*5)%12,55+degree%7,72+degree};
}
static bool expressionMidi095(const QString& path, int attacks, bool repeat=false)
{
    struct Event { int tick; QByteArray bytes; };
    QVector<Event> events; double seconds=0;
    for(int i=0;i<attacks;++i) {
        auto pitches=expressionPitches095(i);
        for(int k=0;k<3;++k) {
            const int on=qRound((seconds+k*.035)*960), velocity=55+(i+k*11)%54;
            QByteArray start=QByteArray::fromHex("90");start.append(char(pitches[k]));start.append(char(velocity));
            QByteArray end=QByteArray::fromHex("80");end.append(char(pitches[k]));end.append(char(0));
            events.append({on,start});events.append({on+120,end});
            if(repeat){events.append({on+24000,start});events.append({on+24120,end});}
        }
        seconds+=.24+.025*qSin(i*.17);
    }
    std::stable_sort(events.begin(),events.end(),[](const Event& a,const Event& b){return a.tick<b.tick;});
    QByteArray track;int previous=0;
    auto vlq=[&](int number){QByteArray bytes;bytes.prepend(char(number&0x7f));while((number>>=7))bytes.prepend(char((number&0x7f)|0x80));track+=bytes;};
    track.append(QByteArray::fromHex("00ff510307a120"));
    for(const auto& event:events){vlq(event.tick-previous);track+=event.bytes;previous=event.tick;}
    track.append(QByteArray::fromHex("00ff2f00"));
    QByteArray data=QByteArray::fromHex("4d546864000000060000000101e04d54726b");
    const quint32 length=track.size();for(int shift=24;shift>=0;shift-=8)data.append(char(length>>shift));data+=track;
    QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size();
}
static MasterScore* expressionScore095(MuseScore* main,const QString& path,int bars=20)
{
    QString xml="<museScore version=\"3.02\"><Score><Division>480</Division><Part>";
    for(int staff=1;staff<=2;++staff)xml+=QString("<Staff id=\"%1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>").arg(staff);
    xml+="<trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part>";
    const int spelling[]{14,21,16,11,18,13,20,15,22,17,12,19};
    for(int staff=1;staff<=2;++staff){
        xml+=QString("<Staff id=\"%1\">").arg(staff);
        for(int bar=0;bar<bars;++bar){
            xml+="<Measure><voice>";
            if(!bar)xml+="<Clef><concertClefType>G</concertClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
            for(int i=0;i<8;++i){
                const auto pitches=expressionPitches095(bar*8+i);xml+="<Chord><durationType>eighth</durationType>";
                for(int k=staff==1?1:0;k<(staff==1?3:1);++k)xml+=QString("<Note><pitch>%1</pitch><tpc>%2</tpc></Note>").arg(pitches[k]).arg(spelling[pitches[k]%12]);
                xml+="</Chord>";
            }
            xml+="</voice></Measure>";
        }
        xml+="</Staff>";
    }
    xml+="</Score></museScore>";
    QFile file(path);if(!file.open(QIODevice::WriteOnly))return nullptr;file.write(xml.toUtf8());file.close();
    auto score=main->readScore(path);if(score)main->setCurrentScoreView(main->appendScore(score));return score;
}
void TestPerformanceEditor::expressionWholeMidiMiddlePassage()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;
    auto score=expressionScore095(main,_directory.filePath("expression-full-095.mscx"));QVERIFY(score);
    expressionShowMain080(main);auto root=expressionRoot080(main);QVERIFY(root);
    const int begin=8*1920,end=12*1920;
    score->selection().setRangeTicks(Fraction::fromTicks(begin),Fraction::fromTicks(end),0,2);score->selection().updateSelectedElements();
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,false)));
    QCOMPARE(expressionMap080(root,"scope").value("start").toInt(),begin);
    QCOMPARE(expressionMap080(root,"scope").value("end").toInt(),end);
    const auto midi=_directory.filePath("expression-full-095.mid");QVERIFY(expressionMidi095(midi,160));
    // A previous file's time bounds must not crop the newly imported recording.
    root->setProperty("fromSeconds",1.);root->setProperty("untilSeconds",2.);
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==96,15000);
    QCOMPARE(root->property("fromSeconds").toDouble(),0.);
    const auto result=expressionMap080(root,"result");QCOMPARE(result.value("pending").toInt(),0);
    QCOMPARE(result.value("missing").toList().size(),0);QCOMPARE(result.value("extra").toList().size(),0);
    for(auto value:result.value("matches").toList()){
        auto match=value.toMap();const int attack=match.value("tick").toInt()/240;
        const int voice=expressionPitches095(attack).indexOf(match.value("pitch").toInt());QVERIFY(voice>=0);
        QCOMPARE(match.value("velocity").toInt(),55+(attack+voice*11)%54);
    }
    auto first=toChord(score->findCR(Fraction::fromTicks(begin),0))->upNote();score->select(first);
    root->setProperty("applicationScope",0);
    QTRY_COMPARE(expressionMap080(root,"scopedCandidate").value("velocity").toList().size(),1);
    const int undo=score->undoStack()->getCurIdx();
    QTRY_VERIFY(root->findChild<QQuickItem*>("expressionApplyVelocity")->isEnabled());QTest::qWait(100);
    QVERIFY(expressionClick080(root,"expressionApplyVelocity"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==95,10000);
    QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
    score->selection().setRangeTicks(Fraction::fromTicks(begin),Fraction::fromTicks(begin+3840),0,2);score->selection().updateSelectedElements();
    root->setProperty("applicationScope",1);QVERIFY(QMetaObject::invokeMethod(root,"refreshLiveSelection"));
    QTRY_COMPARE(expressionMap080(root,"scopedCandidate").value("velocity").toList().size(),47);
    QVERIFY(expressionClick080(root,"expressionApplyVelocity"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==48,10000);
    root->setProperty("applicationScope",2);QTRY_VERIFY(root->findChild<QQuickItem*>("expressionApplyVelocity")->isEnabled());
    QVERIFY(expressionClick080(root,"expressionApplyVelocity"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().isEmpty(),10000);
    QCOMPARE(score->undoStack()->getCurIdx(),undo+3);
    for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int track:{0,4})
        if(auto chord=segment->element(track))if(chord->isChord())for(auto note:toChord(chord)->notes()){
            const int tick=segment->tick().ticks();
            if(tick>=begin&&tick<end){const int attack=tick/240,k=expressionPitches095(attack).indexOf(note->pitch());QCOMPARE(note->veloType(),Note::ValueType::USER_VAL);QCOMPARE(note->veloOffset(),55+(attack+k*11)%54);}
            else {QCOMPARE(note->veloType(),Note::ValueType::OFFSET_VAL);QCOMPARE(note->veloOffset(),0);}
        }
    auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    const auto saved=output.filePath("expression-adopted-middle-095.mscz");
    QVERIFY(observer->copyWorkflowScore(saved,observer->analysisRevision(),true).value("ok").toBool());
    std::unique_ptr<MasterScore> reopened(main->readScore(saved));QVERIFY(reopened);
    PluginAPI::Score restoredScore(reopened.get());PluginAPI::ScoreObserver restored;restored.setScore(&restoredScore);
    QCOMPARE(restored.expressionVelocityHistory({}).value("count").toInt(),96);
    int readBack=0;
    for(auto segment=reopened->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int track:{0,4})
        if(auto chord=segment->element(track))if(chord->isChord())for(auto note:toChord(chord)->notes()){
            const int tick=segment->tick().ticks();
            if(tick>=begin&&tick<end){const int attack=tick/240,k=expressionPitches095(attack).indexOf(note->pitch());QCOMPARE(note->veloType(),Note::ValueType::USER_VAL);QCOMPARE(note->veloOffset(),55+(attack+k*11)%54);++readBack;}
            else {QCOMPARE(note->veloType(),Note::ValueType::OFFSET_VAL);QCOMPARE(note->veloOffset(),0);}
        }
    QCOMPARE(readBack,96);
    EditData edit;for(int i=0;i<3;++i)score->undoRedo(true,&edit);QCOMPARE(score->undoStack()->getCurIdx(),undo);
    QCOMPARE(first->veloType(),Note::ValueType::OFFSET_VAL);
    for(int i=0;i<3;++i)score->undoRedo(false,&edit);QCOMPARE(score->undoStack()->getCurIdx(),undo+3);
    root->window()->grabWindow().save(output.filePath("expression-middle-used-095.png"));
}
void TestPerformanceEditor::expressionRepeatedPassageChoice()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;
    auto score=expressionScore095(main,_directory.filePath("expression-repeat-095.mscx"),2);QVERIFY(score);
    expressionShowMain080(main);auto root=expressionRoot080(main);QVERIFY(root);
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    const auto midi=_directory.filePath("expression-repeat-095.mid");QVERIFY(expressionMidi095(midi,16,true));
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"result").value("matches").toList().size()==48,15000);
    auto choice=root->findChild<QQuickItem*>("expressionPassageChoice");QVERIFY(choice);
    QTRY_VERIFY(choice->isVisible());QCOMPARE(choice->property("currentIndex").toInt(),-1);
    QCOMPARE(expressionMap080(root,"result").value("pending").toInt(),48);
    QVERIFY(expressionMap080(root,"candidate").value("velocity").toList().isEmpty());
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    QVERIFY(root->window()->grabWindow().save(output.filePath("expression-repeated-choice-095.png")));
    const auto candidates=expressionMap080(root,"result").value("passage").toMap().value("candidates").toList();
    int target=-1;for(int i=0;i<candidates.size();++i)if(candidates[i].toMap().value("from").toDouble()>20)target=i;
    QVERIFY(target>=0);QVERIFY(expressionClick080(root,"expressionPassageChoice"));
    auto popup=choice->property("popup").value<QObject*>();QVERIFY(popup);QTRY_VERIFY(popup->property("visible").toBool());
    auto content=popup->property("contentItem").value<QQuickItem*>();QVERIFY(content);
    QTest::qWait(120);QList<QQuickItem*> pending=content->childItems();QQuickItem* option=nullptr;
    while(!pending.isEmpty()){
        auto item=pending.takeFirst();pending.append(item->childItems());
        if(item->isVisible()&&item->property("text").toString().startsWith(QString::number(target+1)+" ·")&&item->property("highlighted").isValid()){option=item;break;}
    }
    QVERIFY(option);const auto point=option->mapToScene(QPointF(option->width()/2,option->height()/2));
    QTest::mouseClick(choice->window(),Qt::LeftButton,Qt::NoModifier,point.toPoint());
    QTest::qWait(500);qInfo()<<"chosen passage"<<root->property("passageChoice")<<root->property("status")<<expressionMap080(root,"candidate").value("velocity").toList().size();
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==48,15000);
    QCOMPARE(root->property("passageChoice").toInt(),target);
    QCOMPARE(expressionMap080(root,"result").value("pending").toInt(),0);
    for(auto value:expressionMap080(root,"result").value("matches").toList())QVERIFY(value.toMap().value("seconds").toDouble()>=25);
    const auto project=output.filePath("expression-choice-095.json");
    QVERIFY(QMetaObject::invokeMethod(root,"save",Q_ARG(QVariant,QUrl::fromLocalFile(project).toString())));
    QFile file(project);QVERIFY(file.open(QIODevice::ReadOnly));auto json=QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(json.value("expression").toObject().value("passageChoice").toInt(-1),target);
    QCOMPARE(json.value("expression").toObject().value("passageFrom").toDouble(-1),25.);
    QVERIFY(QMetaObject::invokeMethod(root,"load",Q_ARG(QVariant,QUrl::fromLocalFile(project).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==48,15000);
    QCOMPARE(root->property("passageFrom").toDouble(),25.);
    for(auto value:expressionMap080(root,"result").value("matches").toList())QVERIFY(value.toMap().value("seconds").toDouble()>=25);
    QVERIFY(root->window()->grabWindow().save(output.filePath("expression-repeated-selected-095.png")));
}
void TestPerformanceEditor::expressionRealPerformanceFiles()
{
    const auto source=qEnvironmentVariable("P2_EXPRESSION_REAL_SCORE"),midi=qEnvironmentVariable("P2_EXPRESSION_REAL_MIDI");
    const auto reference=qEnvironmentVariable("P2_EXPRESSION_REAL_TRUTH");
    if(source.isEmpty()||midi.isEmpty()||reference.isEmpty())QSKIP("Provide separately licensed MusicXML, MIDI and expert reference files.");
    expressionHost080(_directory.path());auto main=Ms::mscore;
    auto score=main->readScore(source);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));expressionShowMain080(main);
    auto root=expressionRoot080(main);QVERIFY(root);QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!expressionMap080(root,"candidate").isEmpty(),30000);
    QFile file(reference);QVERIFY(file.open(QIODevice::ReadOnly));const auto json=QJsonDocument::fromJson(file.readAll()).object();
    const auto truth=json.value("truth").toObject();QMap<QString,double> seconds;
    for(const auto& value:json.value("performance").toArray()){auto note=value.toObject();seconds[note["id"].toString()]=note["seconds"].toDouble();}
    QMultiMap<QString,double> expected;
    for(const auto& value:json.value("notes").toArray()){
        auto note=value.toObject();auto id=truth[note["id"].toString()].toString();
        if(!id.isEmpty())expected.insert(QString::number(note["tick"].toInt())+":"+QString::number(note["pitch"].toInt()),seconds[id]);
    }
    const auto result=expressionMap080(root,"result");int correct=0,automatic=0,wrongAutomatic=0;
    for(auto value:result.value("matches").toList()){
        auto note=value.toMap();const auto key=QString::number(note["tick"].toInt())+":"+QString::number(note["pitch"].toInt());
        bool found=false;for(double time:expected.values(key))if(qAbs(time-note["seconds"].toDouble())<.002){found=true;break;}
        if(found)++correct;
        if(note["confidence"].toDouble()>=.8||note["confirmed"].toBool()){++automatic;if(!found)++wrongAutomatic;}
    }
    qInfo()<<"real file matching: reference / correct / automatic / wrong automatic"<<expected.size()<<correct<<automatic<<wrongAutomatic;
    QCOMPARE(wrongAutomatic,0);QCOMPARE(correct,expected.size());QCOMPARE(automatic,expected.size());
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    QVERIFY(root->window()->grabWindow().save(output.filePath("expression-real-bach-095.png")));
}
