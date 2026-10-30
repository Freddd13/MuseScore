// GPL-2.0-or-later. Reference materials and the spectrum keep their score identity.
void TestPerformanceEditor::referenceSpectrumAcrossScores152()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);
    auto first=expressionScore080(main,_directory.filePath("media-first-152.mscx"));QVERIFY(first);expressionShowMain080(main);
    const auto tone=referenceTone099(_directory.filePath("media-tone-152.wav"),330.);QVERIFY(!tone.isEmpty());
    ReferencePanel081 panel{referenceOpen081(tone)};auto root=panel.root.data();QVERIFY(root);
    auto list=[&](const char* name){auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toList():value.toList();};
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)->property("duration").toDouble()>4900,10000);
    for(int index=1;index<8;++index){
        const auto path=_directory.filePath(QString("media-track-%1-152.wav").arg(index));QVERIFY(QFile::copy(tone,path));root->setProperty("addingAudio",true);
        QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(path).toString())));
        QTRY_VERIFY_WITH_TIMEOUT(list("tracks").size()==index+1&&root->property("readingAudioCount").toInt()==0,10000);
    }
    first->selection().setRangeTicks(Fraction::fromTicks(480),Fraction::fromTicks(3360),0,1);first->selection().updateSelectedElements();root->setProperty("a",500.);root->setProperty("b",3500.);
    QVERIFY(QMetaObject::invokeMethod(root,"associate"));QVERIFY(QMetaObject::invokeMethod(root,"saveScoreProject"));
    const auto allTracks=list("tracks");QCOMPARE(allTracks.size(),8);QCOMPARE(allTracks.back().toMap().value("associations").toList().size(),1);
    QVERIFY(QMetaObject::invokeMethod(root,"removeTrack",Q_ARG(QVariant,7)));QCOMPARE(list("tracks").size(),7);
    QVERIFY(QMetaObject::invokeMethod(root,"restoreDeletedTrack"));QTRY_VERIFY_WITH_TIMEOUT(list("tracks").size()==8&&root->property("readingAudioCount").toInt()==0,10000);
    QVERIFY(QMetaObject::invokeMethod(root,"saveScoreProject"));const auto savedTracks=list("tracks");
    auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);const auto firstData=observer->workflowData("reference");QCOMPARE(firstData.value("tracks").toList().size(),8);
    QFileInfo firstFile(_directory.filePath("media-first-152.mscz"));QVERIFY(first->saveCompressedFile(firstFile,false,false));

    auto second=expressionScore080(main,_directory.filePath("media-second-152.mscx"));QVERIFY(second);QTRY_COMPARE(list("tracks").size(),0);
    const auto other=referenceTone099(_directory.filePath("media-other-152.wav"),660.);QVERIFY(!other.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(other).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(list("tracks").size()==1&&root->property("readingAudioCount").toInt()==0,10000);
    QVERIFY(QMetaObject::invokeMethod(root,"saveScoreProject"));const auto otherTrack=list("tracks").front().toMap();
    showExpressionPanel();auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(2);
    auto pane=editor->findChild<QWidget*>("transcriptionSpectrumPane");QVERIFY(pane);auto choices=pane->findChild<QComboBox*>("transcriptionTracks");QVERIFY(choices);
    QTRY_VERIFY(choices->count()==1&&choices->itemData(0).toString()==otherTrack.value("id").toString());
    for(int round=0;round<2;++round){
        main->setCurrentScoreView(main->scores().indexOf(first));
        QTRY_VERIFY_WITH_TIMEOUT(list("tracks").size()==8&&root->property("readingAudioCount").toInt()==0,15000);
        QTRY_COMPARE(choices->count(),8);
        for(int i=0;i<8;++i)QCOMPARE(list("tracks")[i].toMap().value("id"),savedTracks[i].toMap().value("id"));
        choices->setCurrentIndex(7);QTRY_VERIFY(choices->currentData().toString()==savedTracks[7].toMap().value("id").toString());
        auto session=pane->findChild<QObject*>("transcriptionSpectrumSession");QVERIFY(session);
        QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool()&&!qvariant_cast<QImage>(session->property("image")).isNull(),15000);
        main->setCurrentScoreView(main->scores().indexOf(second));
        QTRY_VERIFY_WITH_TIMEOUT(list("tracks").size()==1&&root->property("readingAudioCount").toInt()==0,15000);
        QTRY_VERIFY(choices->count()==1&&choices->currentData().toString()==otherTrack.value("id").toString());
    }
    // Read the actual saved file, then display its materials in a new score session.
    auto reopened=main->readScore(firstFile.filePath());QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));
    QTRY_VERIFY_WITH_TIMEOUT(list("tracks").size()==8&&root->property("readingAudioCount").toInt()==0,15000);QTRY_COMPARE(choices->count(),8);
    QCOMPARE(list("tracks").back().toMap().value("associations"),savedTracks.back().toMap().value("associations"));
    const auto source=referenceMedia081(root)->property("path").toString();QVERIFY(source.contains("media-track-7-152"));
    showReferencePanel(false);tabs->setCurrentIndex(0);main->showPerformanceEditor(false);QTest::qWait(120);
    showReferencePanel(true);main->showPerformanceEditor(true);tabs->setCurrentIndex(2);QTRY_COMPARE(choices->count(),8);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(main->grab().save(output.filePath("media-scores-152.png")));
    qInfo()<<"152 restored media tracks"<<list("tracks").size()<<"source"<<source;
}
