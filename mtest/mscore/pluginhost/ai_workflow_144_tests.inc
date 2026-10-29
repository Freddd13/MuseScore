// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
void TestPluginHost::aiSuggestionState144() {
    auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    mscore->setCurrentScoreView(mscore->appendScore(score));guiScaling=1.0;MuseScore::updateUiStyleAndTheme();
    mscore->resize(1200,820);mscore->show();
    PluginAPI::Score wrapper(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapper);
    auto bass=toChord(score->findCR(Fraction(),4))->notes().front();
    auto submit=[&]{
        const auto state=observer.arrangementState({{"whole",true}});
        QVariantMap ref{{"id","0:4:0"},{"snapshotId",state["snapshotId"]}};
        QVariantMap option{{"title",QString::fromUtf8(QByteArray::fromHex("e5b7a6e6898be8bdbbe4b880e4ba9b"))},{"summary",QString::fromUtf8(QByteArray::fromHex("e4bf9de79599e6978be5be8befbc8ce4bd8ee99fb3e98787e794a8e58a9be5baa63533e38082"))},{"scope",state["scope"]},
            {"changes",QVariantList{QVariantMap{{"ref",ref},{"velocity",53}}}}};
        return observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",state["snapshotId"]},{"options",QVariantList{option}}});
    };
    auto result=submit();QVERIFY2(!result.contains("error"),qUtf8Printable(result["error"].toString()));
    auto panel=mscore->findChild<QWidget*>("arrangementSuggestionPanel");QVERIFY(panel);
    auto choices=panel->findChild<QTreeWidget*>("arrangementChoices");QVERIFY(choices);
    auto apply=panel->findChild<QPushButton*>("arrangement-apply");QVERIFY(apply);
    auto cancel=panel->findChild<QPushButton*>("localSuggestionCancel");QVERIFY(cancel);
    QTRY_VERIFY(apply->isEnabled());QVERIFY(!cancel->isEnabled());
    score->startCmd();bass->undoChangeProperty(Pid::VELO_OFFSET,3);score->endCmd();mscore->endCmd();
    QTRY_VERIFY(!apply->isEnabled());QCOMPARE(choices->topLevelItem(0)->text(1),QString::fromUtf8(QByteArray::fromHex("e8b0b1e5ad90e5b7b2e4bfaee694b9")));
    auto layer=mscore->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer");
    QVERIFY(layer);QVERIFY(!layer->isVisible());
    const auto folder=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QTest::qWait(80);QVERIFY(mscore->grab().save(folder.filePath("ai-stale-144.png")));
    result=submit();QVERIFY(!result.contains("error"));QTRY_VERIFY(apply->isEnabled());
    const int undo=score->undoStack()->getCurIdx();QTest::mouseClick(apply,Qt::LeftButton);
    QTRY_COMPARE(score->undoStack()->getCurIdx(),undo+1);QCOMPARE(bass->veloOffset(),53);
    QVERIFY(!apply->isEnabled());QCOMPARE(choices->topLevelItem(0)->text(1),QString::fromUtf8(QByteArray::fromHex("e5b7b2e98787e794a8")));
    const auto path=folder.filePath("ai-applied-144.mscz");QFileInfo info(path);QVERIFY(score->saveCompressedFile(info,false,false));
    std::unique_ptr<MasterScore> saved(mscore->readScore(path));QVERIFY(saved);
    QCOMPARE(toChord(saved->findCR(Fraction(),4))->notes().front()->veloOffset(),53);
    mscore->cmd(getAction("undo"));QCOMPARE(bass->veloOffset(),3);
    QTRY_COMPARE(choices->topLevelItem(0)->text(1),QString::fromUtf8(QByteArray::fromHex("e8b0b1e5ad90e5b7b2e4bfaee694b9")));
    QVERIFY(!arrangementSuggestions(score)["options"].toList().first().toMap()["used"].toBool());
    mscore->cmd(getAction("redo"));QCOMPARE(bass->veloOffset(),53);
    QTRY_COMPARE(choices->topLevelItem(0)->text(1),QString::fromUtf8(QByteArray::fromHex("e5b7b2e98787e794a8")));
    QTest::qWait(80);QVERIFY(mscore->grab().save(folder.filePath("ai-adopt-144.png")));
    showSuggestionPanel(false);
}
