void TestP1Native::nativeReferenceDocumentAndMenu(){
 auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));mscore->resize(1250,900);mscore->show();
 const auto scorePath=settings.filePath("portable.mscz");score->fileInfo()->setFile(scorePath);
 PluginAPI::Score wrap(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrap);
 QFile audio(settings.filePath("portable.wav"));QVERIFY(audio.open(QIODevice::WriteOnly));audio.write("temporary location probe");audio.close();
 const auto location=observer.referenceFileLocation("X:/missing/portable.wav","portable.wav");QVERIFY(location["exists"].toBool());QCOMPARE(QFileInfo(location["path"].toString()).absoluteFilePath(),QFileInfo(audio).absoluteFilePath());
 const QVariantMap project{{"schema",2},{"structure",observer.referenceStructureKey()},{"tracks",QVariantList{QVariantMap{{"id","audio1"},{"name","定位文件示例"},{"path","X:/missing/sample.wav"},{"relativePath","sample.wav"},{"associations",QVariantList{QVariantMap{{"start",0},{"end",1920},{"from",0},{"until",2000},{"active",true}}}}}}},{"linked",false}};
 QVERIFY(observer.setWorkflowData("reference",project)["ok"].toBool());QFileInfo info(scorePath);QVERIFY(score->saveCompressedFile(info,false,false));
 std::unique_ptr<MasterScore> reopened(mscore->readScore(scorePath));QVERIFY(reopened);PluginAPI::Score loaded(reopened.get());PluginAPI::ScoreObserver read;read.setScore(&loaded);QCOMPARE(read.workflowData("reference"),project);
 auto action=getAction("reference-player");QVERIFY(action);action->setEnabled(true);if(action->isChecked())action->trigger();action->trigger();QTest::qWait(100);
 QmlPlugin* root=nullptr;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto q=qobject_cast<QmlPlugin*>(view->rootObject()))if(q->menuPath()=="Plugins.Reference Player")root=q;QVERIFY(root);
 QTRY_VERIFY(root->property("documentReady").toBool());QTRY_COMPARE(root->property("tracks").value<QJSValue>().toVariant().toList().size(),1);
 auto dock=mscore->findChild<QDockWidget*>("kumoPanel/ReferencePlayer_MS3");QVERIFY(dock&&dock->isVisible());QVERIFY(root->findChild<QQuickItem*>("referenceTimeScroll"));QVERIFY(!root->property("linkEditor").toBool());
 action->trigger();QVERIFY(!dock->isVisible());action->trigger();QVERIFY(dock->isVisible());
 const auto entry=QString(TESTROOT)+"/share/plugins/ReferencePlayer/ReferencePlayer_MS3.qml";mscore->loadPlugin(entry);mscore->pluginTriggered(entry);QTest::qWait(80);QCOMPARE(mscore->findChildren<QDockWidget*>("kumoPanel/ReferencePlayer_MS3").size(),1);
 auto folder=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!folder.isEmpty())root->window()->grabWindow().save(folder+"/reference-native-056.png");
 root->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
}
