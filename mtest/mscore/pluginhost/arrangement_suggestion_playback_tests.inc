// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
void TestPluginHost::traditionalSuggestionsReadActualKeys()
{
    QVariantList rows{QVariant::fromValue(QVariantList{60,0,960,0,14}),QVariant::fromValue(QVariantList{67,960,1440,0,15})};
    auto xml=arrangementScore115(rows);
    xml.insert(xml.indexOf("</Measure></Staff>"),"<voice>"+arrangementRests115(480)+"<Chord><durationType>quarter</durationType><Note><pitch>64</pitch><tpc>18</tpc></Note></Chord>"+arrangementRests115(960)+"</voice>");
    const auto path=_settings.filePath("suggestion-actual-keys.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,920);main->show();
    auto held=toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->upNote();
    auto other=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(1))->upNote();
    auto next=toChord(score->tick2segment(Fraction::fromTicks(960),false,SegmentType::ChordRest)->element(0))->upNote();
    auto addFinger=[&](Note* note){auto f=new Fingering(score);f->setParent(note);f->setTrack(note->track());f->setPlainText("1");score->undoAddElement(f);};
    NoteEventList shortKey;shortKey.clear();shortKey.append(NoteEvent(0,0,200));
    score->startCmd();addFinger(held);addFinger(other);score->undo(new ChangeNoteEventList(held,shortKey));score->endCmd();main->endCmd();score->deselectAll();
    const int before=score->undoStack()->getCurIdx();
    showSuggestionPanel(true);auto panel=main->findChild<QWidget*>("arrangementSuggestionPanel");QVERIFY(panel);
    struct Close{~Close(){showSuggestionPanel(false);}}close;
    auto task=panel->findChild<LocalSuggestionTask*>();QVERIFY(task);
    QSignalSpy completed(task,&LocalSuggestionTask::completed),progress(task,&LocalSuggestionTask::progress);
    auto kind=panel->findChild<QComboBox*>("localSuggestionKind");QVERIFY(kind);kind->setCurrentIndex(0);
    QComboBox* hand=nullptr;for(auto box:panel->findChildren<QComboBox*>())if(box->findData("auto")>=0&&box->findData("right")>=0)hand=box;QVERIFY(hand);hand->setCurrentIndex(hand->findData("right"));
    auto generate=panel->findChild<QPushButton*>("localSuggestionStart");QVERIFY(generate);QTest::mouseClick(generate,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!task->running(),15000);
    QVERIFY2(completed.size()==1,qUtf8Printable(progress.isEmpty()?QString("No status"):progress.last()[0].toString()));
    const auto result=completed.front()[0].toMap();QVERIFY2(result.value("messages").toStringList().isEmpty(),qUtf8Printable(result.value("messages").toStringList().join(";")));
    const auto options=result.value("options").toList();QVERIFY(!options.isEmpty());
    QVERIFY(options.front().toMap().value("summary").toString().contains("G4"));
    for(const auto& value:options){const auto changes=value.toMap().value("changes").toList();QCOMPARE(changes.size(),1);QCOMPARE(changes.front().toMap().value("ref").toMap().value("id").toString(),QString("960:0:0"));}
    bool readActual=false;for(const auto& status:progress)readActual|=status[0].toString().contains(QString::fromUtf8("实际演奏"));QVERIFY(readActual);
    QCOMPARE(score->undoStack()->getCurIdx(),before);
    QTRY_VERIFY(main->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer")->isVisible());
    bool fingerLabel=false;for(auto tree:panel->findChildren<QTreeWidget*>())if(tree->topLevelItemCount())fingerLabel|=tree->topLevelItem(0)->text(2).contains(QString::fromUtf8("指法"));QVERIFY(fingerLabel);
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::qWait(100);QVERIFY(main->grab().save(out.filePath("arrangement-suggestion-actual-keys.png")));
    auto fingers=[](Note* n){QStringList result;for(auto e:n->el())if(e->isFingering())result.append(toFingering(e)->plainText());return result;};
    auto apply=panel->findChild<QPushButton*>("arrangement-apply");QVERIFY(apply&&apply->isEnabled());QTest::mouseClick(apply,Qt::LeftButton);
    QTRY_COMPARE(score->undoStack()->getCurIdx(),before+1);QVERIFY(!fingers(next).isEmpty());QCOMPARE(fingers(held),QStringList{"1"});QCOMPARE(fingers(other),QStringList{"1"});
    const auto output=out.filePath("suggestion-actual-keys-applied.mscz");QFileInfo info(output);QVERIFY(score->saveCompressedFile(info,false,false));
    std::unique_ptr<MasterScore> reopened(main->readScore(output));QVERIFY(reopened);auto saved=toChord(reopened->tick2segment(Fraction::fromTicks(960),false,SegmentType::ChordRest)->element(0))->upNote();QCOMPARE(fingers(saved),fingers(next));
    main->cmd(getAction("undo"));QCOMPARE(score->undoStack()->getCurIdx(),before);QVERIFY(fingers(next).isEmpty());
    completed.clear();NoteEventList fullKey;fullKey.clear();fullKey.append(NoteEvent(0,0,1000));score->startCmd();score->undo(new ChangeNoteEventList(held,fullKey));score->endCmd();main->endCmd();score->deselectAll();
    QTest::mouseClick(generate,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!task->running(),15000);QCOMPARE(completed.size(),1);QVERIFY(!completed.front()[0].toMap().value("messages").toStringList().isEmpty());
    main->cmd(getAction("undo"));
    for(const auto& phase:{QString::fromUtf8("正在准备这段"),QString::fromUtf8("准备实际演奏"),QString::fromUtf8("已读取实际演奏")}) {
        completed.clear();bool cancelled=false;auto connection=connect(task,&LocalSuggestionTask::progress,task,[&](const QString& message){if(message.contains(phase)){cancelled=true;task->cancel();}});
        task->start(score,"fingering",{{"hand","right"}});QTRY_VERIFY_WITH_TIMEOUT(cancelled,3000);QTest::qWait(100);QVERIFY(!task->running());QCOMPARE(completed.size(),0);disconnect(connection);
    }
}
