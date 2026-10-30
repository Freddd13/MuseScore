// GPL-2.0-or-later. A restored scroll position must not move a button during a press.
void TestPerformanceEditor::expressionFirstClickAfterScoreSwitch152()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);
    auto first=expressionScore080(main,_directory.filePath("first-click-152.mscx"));QVERIFY(first);expressionShowMain080(main);
    auto root=expressionRoot080(main);QVERIFY(root);QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    const auto midi=_directory.filePath("first-click-152.mid");QFile file(midi);QVERIFY(file.open(QIODevice::WriteOnly));ExportMidi exporter(first);QVERIFY(exporter.write(&file,true,true,main->synthesizerState()));file.close();
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!expressionMap080(root,"candidate").isEmpty(),15000);
    auto editor=main->performanceEditor();auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);main->resizeDocks({dock},{330},Qt::Vertical);
    auto splitter=editor->findChild<QSplitter*>("performanceMainSplitter");QVERIFY(splitter);auto host=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(host);auto sizes=splitter->sizes();sizes[splitter->indexOf(host)]=180;splitter->setSizes(sizes);QTest::qWait(100);
    QQuickItem* flick=nullptr;for(auto item:root->childItems())if(item->isVisible()&&QByteArray(item->metaObject()->className()).contains("ScrollView")){flick=qobject_cast<QQuickItem*>(item->property("contentItem").value<QObject*>());break;}QVERIFY(flick);
    qInfo()<<"152 scroll canvas"<<flick->size()<<flick->property("contentHeight");
    flick->setProperty("contentY",150.);QTest::qWait(60);const auto saved=flick->property("contentY").toDouble();QVERIFY(saved>30);
    auto second=expressionScore080(main,_directory.filePath("other-click-152.mscx"));QVERIFY(second);QTest::qWait(100);QVERIFY(flick->property("contentY").toDouble()<1);
    main->setCurrentScoreView(main->scores().indexOf(first));
    auto record=root->findChild<QQuickItem*>("expressionRecord");QVERIFY(record&&record->isVisible()&&record->isEnabled());
    const auto immediate=flick->property("contentY").toDouble();
    qInfo()<<"152 returning scroll"<<immediate<<"saved"<<saved;
    // The user scrolls back to the controls before a queued restore runs.
    flick->setProperty("contentY",0.);
    QSignalSpy clicked(record,SIGNAL(clicked()));const auto point=record->mapToScene(QPointF(record->width()/2,record->height()/2)).toPoint();
    QCoreApplication::processEvents();QTest::qWait(20);
    qInfo()<<"152 controls before press"<<point<<"after restore"<<record->mapToScene(QPointF(record->width()/2,record->height()/2))<<"scroll"<<flick->property("contentY");
    QCOMPARE(flick->property("contentY").toDouble(),0.);
    QTest::mouseClick(record->window(),Qt::LeftButton,Qt::NoModifier,point);QTRY_COMPARE_WITH_TIMEOUT(clicked.size(),1,1000);
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&root->property("notes").value<QJSValue>().toVariant().toList().size()==16,10000);
    QVERIFY(root->property("readForCapture").toBool()==false);
    QVERIFY2(!root->property("status").toString().contains(QChar(0xfffd)),"The recording result must contain readable text");
}
