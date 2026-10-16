 void analysisIdentityAndPositionHint(){
  auto first=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(first);mscore->setCurrentScoreView(mscore->appendScore(first));mscore->resize(1250,900);mscore->show();
  PluginAPI::Score a(first),b(first);PluginAPI::ScoreObserver observer,other;observer.setScore(&a);other.setScore(&b);
  const auto id=observer.analysisScoreId(),revision=observer.analysisContentRevision();QVERIFY(!id.isEmpty());QCOMPARE(other.analysisScoreId(),id);QCOMPARE(other.analysisContentRevision(),revision);
  auto scope=observer.analysisScope(true);auto page=observer.analysisRange(scope["start"].toInt(),scope["end"].toInt(),0,first->ntracks(),0,64,observer.analysisRevision());auto note=page["notes"].toList().front().toMap();
  auto segment=first->tick2segment(Fraction::fromTicks(note["tick"].toInt()),false,SegmentType::ChordRest);QVERIFY(segment);auto element=segment->element(note["track"].toInt());QVERIFY(element&&element->isChord());auto selected=toChord(element)->notes()[note["index"].toInt()];
  first->select(selected,SelectType::SINGLE);QCOMPARE(observer.analysisContentRevision(),revision);
  first->startCmd();selected->undoChangeProperty(Pid::VELO_OFFSET,12);first->endCmd();QVERIFY(observer.analysisContentRevision()!=revision);QCOMPARE(other.analysisContentRevision(),observer.analysisContentRevision());
  const int undo=first->undoStack()->getCurIdx();scope["start"]=note["tick"];scope["firstTrack"]=note["track"];QVERIFY(observer.navigateWorkflow(scope,false)["ok"].toBool());QTest::qWait(80);
  auto view=mscore->currentScoreView();auto hint=view->findChild<QWidget*>("workflowPositionHint");QVERIFY(hint&&hint->isVisible());QCOMPARE(first->undoStack()->getCurIdx(),undo);
  auto location=observer.scoreLocation(note["tick"].toInt(),note["track"].toInt());QVERIFY(location["width"].toInt()>0);QVERIFY(location["screenWidth"].toInt()>0);
  const auto folder=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!folder.isEmpty())view->grab().save(folder+"/workflow-position-hint-050.png");
  QTest::qWait(3100);QVERIFY(!hint->isVisible());QCOMPARE(first->undoStack()->getCurIdx(),undo);
  auto second=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(second);PluginAPI::Score c(second);observer.setScore(&c);QVERIFY(observer.analysisScoreId()!=id);observer.setScore(&b);QCOMPARE(observer.analysisScoreId(),id);
 }
 void arrangementRetainsRangeAndSurfaces(){
  auto first=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(first);mscore->setCurrentScoreView(mscore->appendScore(first));mscore->resize(1250,900);mscore->show();
  const QString path=QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml";PluginDescription description;description.path=path;QVERIFY(collectPluginMetaInformation(&description));mscore->registerPlugin(&description);mscore->pluginTriggered(path);QTest::qWait(100);
  QmlPlugin* root=nullptr;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto candidate=qobject_cast<QmlPlugin*>(view->rootObject()))if(candidate->menuPath().endsWith("Arrangement Check"))root=candidate;QVERIFY(root);
  root->setPanelFloating(true);root->window()->resize(480,800);root->focusPanel();QTest::qWait(100);
  auto update=root->findChild<QQuickItem*>("arrangementUpdate");QVERIFY(update&&update->isEnabled());QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,update->mapToScene(QPointF(update->width()/2,update->height()/2)).toPoint());
  QTRY_VERIFY(root->property("analyzed").toBool());QTRY_VERIFY(!root->property("running").toBool());QVERIFY(root->summaryPanelVisible());
  auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);
  const auto scope=root->property("scope").value<QJSValue>().toVariant().toMap();const auto id=observer->analysisScoreId();
  auto page=observer->analysisRange(scope["start"].toInt(),scope["end"].toInt(),0,first->ntracks(),0,64,observer->analysisRevision());const auto note=page["notes"].toList().front().toMap();
  QVariantMap issue{{"id","keep-result"},{"tick",note["tick"]},{"bar",note["measure"]},{"beat",1.},{"type","harmony"},{"severity","注意"},{"title","核对这个持续音"},{"basis","查看持续音与下一组音的关系。"},{"notes",QVariantList{note}}};auto secondIssue=issue;secondIssue["id"]="keep-second-result";secondIssue["title"]="检查同一位置的另一个声部";
  root->setProperty("issues",QVariantList{issue,secondIssue});root->setProperty("counts",QVariantMap{{"harmony",2},{"counterpoint",0},{"playability",0}});auto config=root->property("config").value<QJSValue>().toVariant().toMap();config["scoreHints"]=true;root->setProperty("config",config);QVERIFY(QMetaObject::invokeMethod(root,"publishMarkers"));
  auto view=mscore->currentScoreView();QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,QPoint(view->width()-20,view->height()-20));QTest::qWait(100);QVERIFY(root->property("analyzed").toBool());QCOMPARE(root->property("scope").value<QJSValue>().toVariant().toMap(),scope);QCOMPARE(root->property("issues").value<QJSValue>().toVariant().toList().size(),2);
  auto boxes=view->notePreviewMarkerBoxes();QVERIFY(!boxes.isEmpty());auto hit=view->matrix().map(boxes.front().center()).toPoint();QVERIFY(view->rect().contains(hit));QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,hit);QTest::mouseDClick(view,Qt::LeftButton,Qt::NoModifier,hit);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,hit);QTest::qWait(80);
  auto popup=root->findChild<QQuickWindow*>("arrangementProblemHint");QVERIFY(popup&&popup->isVisible());
  const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!output.isEmpty())popup->grabWindow().save(output+"/arrangement-nearby-050.png");
  const auto otherPath=settings.filePath("arrangement-other.mscx");QVERIFY(QFile::copy(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx",otherPath));
  auto second=mscore->readScore(otherPath);QVERIFY(second&&second!=first);mscore->setCurrentScoreView(mscore->appendScore(second));QTest::qWait(120);QVERIFY(!root->property("analyzed").toBool());QVERIFY(mscore->scores().contains(first));
  mscore->setCurrentScoreView(mscore->scores().indexOf(first));QTest::qWait(120);QCOMPARE(observer->analysisScoreId(),id);QVERIFY(root->property("analyzed").toBool());QCOMPARE(root->property("issues").value<QJSValue>().toVariant().toList().size(),2);QCOMPARE(root->property("scope").value<QJSValue>().toVariant().toMap(),scope);
  QDockWidget* primary=nullptr;QDockWidget* summary=nullptr;for(auto dock:mscore->findChildren<QDockWidget*>()){if(dock->windowTitle()=="Arrangement Check")primary=dock;if(dock->windowTitle()==QString::fromUtf8("编配检查摘要"))summary=dock;}QVERIFY(primary&&summary);
  QPointer<QmlPlugin> guarded=root;primary->close();QTest::qWait(80);QVERIFY(guarded);QVERIFY(root->summaryPanelVisible());
  auto click=[root](const char* name){auto item=root->findChild<QQuickItem*>(name);if(!item||!item->isEnabled()||!item->isVisible()||!item->window())return false;const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();if(!QRect(QPoint(),item->window()->size()).contains(point))return false;QSignalSpy clicked(item,SIGNAL(clicked()));QTest::mouseMove(item->window(),point);QTest::qWait(100);QTest::mouseClick(item->window(),Qt::LeftButton,Qt::NoModifier,point);QTest::qWait(40);return clicked.count()==1;};
  root->focusPanel();QTest::qWait(80);QVERIFY(primary->isVisible());if(!output.isEmpty())root->summaryPanelHost()->window()->grabWindow().save(output+"/summary-before-close-050.png");QVERIFY(click("arrangementSummaryClose"));QTest::qWait(80);QVERIFY(primary->isVisible());QVERIFY(!root->summaryPanelVisible());
  if(!output.isEmpty())root->window()->grabWindow().save(output+"/arrangement-range-050.png");
  primary->setFloating(false);mscore->addDockWidget(Qt::TopDockWidgetArea,primary);QTest::qWait(100);QVERIFY(QMetaObject::invokeMethod(root,"openDetails"));QTest::qWait(100);QVERIFY(root->detailPanelVisible());
  QVERIFY(click("arrangementSummaryClose"));QTest::qWait(80);QVERIFY(guarded);QVERIFY(!primary->isVisible());QVERIFY(root->detailPanelVisible());root->focusPanel();QTest::qWait(60);
  mscore->pluginTriggered(path);QTest::qWait(80);int instances=0;for(auto window:QGuiApplication::allWindows())if(auto quick=qobject_cast<QQuickView*>(window))if(auto candidate=qobject_cast<QmlPlugin*>(quick->rootObject()))if(candidate->menuPath().endsWith("Arrangement Check"))++instances;QCOMPARE(instances,1);
  if(!output.isEmpty())root->window()->grabWindow().save(output+"/arrangement-summary-050.png");root->setVisible(false);primary->setAttribute(Qt::WA_DeleteOnClose,true);primary->close();QTest::qWait(50);
 }

 void nativeHarmonySummaryGlyphs(){
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);PluginAPI::Score wrapper(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapper);
  QVariantMap style{{"pixels",20.},{"degreeRatio",1.},{"gap",6.},{"order",0},{"color","#343a3f"}};
  const auto label=observer.harmonyLabel("D/F#",QString::fromUtf8("V⁶"),style);QVERIFY(label["width"].toInt()>35);QVERIFY(label["height"].toInt()>16);QVERIFY(label["height"].toInt()<=36);QCOMPARE(observer.harmonyLabel("D/F#",QString::fromUtf8("V⁶"),style),label);
  auto png=QByteArray::fromBase64(label["source"].toString().section(',',1).toLatin1());QImage image;QVERIFY(image.loadFromData(png,"PNG"));QCOMPARE(image.width(),label["width"].toInt()*2);
  style["order"]=2;const auto vertical=observer.harmonyLabel("D/F#",QString::fromUtf8("V⁶"),style);QVERIFY(vertical["height"].toInt()>label["height"].toInt());
  auto unknown=observer.harmonyLabel("?","",style);QVERIFY(unknown["width"].toInt()>5);QVERIFY(unknown["height"].toInt()>10);
 }

 void harmonyScoreRangeAndPositionDrag(){
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));mscore->resize(1250,900);mscore->show();
  const QString path=QString(TESTROOT)+"/share/plugins/HarmonyAssistant/HarmonyAssistant_MS3.qml";PluginDescription description;description.path=path;QVERIFY(collectPluginMetaInformation(&description));mscore->registerPlugin(&description);mscore->pluginTriggered(path);QTest::qWait(80);
  QmlPlugin* root=nullptr;for(auto window:QGuiApplication::allWindows())if(auto quick=qobject_cast<QQuickView*>(window))if(auto item=qobject_cast<QmlPlugin*>(quick->rootObject()))if(item->menuPath().endsWith("Harmony Assistant"))root=item;QVERIFY(root);
  auto config=root->property("configuration").value<QJSValue>().toVariant().toMap();config["chordLabels"]=true;QVERIFY(QMetaObject::invokeMethod(root,"applyPreferences",Q_ARG(QVariant,QVariant(config))));QTRY_VERIFY(!root->property("analysisDirty").toBool());
  auto regions=root->property("analysisRegions").value<QJSValue>().toVariant().toList();QVERIFY(!regions.isEmpty());auto region=regions.front().toMap();const int start=region["start"].toInt(),end=region["end"].toInt(),part=region["part"].toInt();
  auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);QVERIFY(QMetaObject::invokeMethod(root,"openAnnotation",Q_ARG(QVariant,start),Q_ARG(QVariant,part)));QTest::qWait(100);
  auto view=mscore->currentScoreView();auto editor=view->findChild<QWidget*>("harmonyScoreRangeEditor");QVERIFY(editor);QTRY_VERIFY(editor->isVisible());
  const auto original=view->notePreviewMarkerBox(observer,start,part);QVERIFY(!original.isEmpty());const auto point=view->matrix().map(original.center()).toPoint();QVERIFY(view->rect().contains(point));
  const int undo=score->undoStack()->getCurIdx();QTest::mousePress(editor,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(editor,point+QPoint(24,-16));QTest::mouseRelease(editor,Qt::LeftButton,Qt::NoModifier,point+QPoint(24,-16));QTest::qWait(60);
  auto positions=root->property("manualPositions").value<QJSValue>().toVariant().toList();QCOMPARE(positions.size(),1);QVERIFY(positions.front().toMap()["x"].toDouble()>0);QVERIFY(positions.front().toMap()["y"].toDouble()<0);QCOMPARE(score->undoStack()->getCurIdx(),undo);
  QVERIFY(QMetaObject::invokeMethod(root,"editRange",Q_ARG(QVariant,"undo"),Q_ARG(QVariant,0),Q_ARG(QVariant,0),Q_ARG(QVariant,0),Q_ARG(QVariant,0),Q_ARG(QVariant,0)));QTest::qWait(40);QVERIFY(root->property("manualPositions").value<QJSValue>().toVariant().toList().isEmpty());
  const auto handle=editor->property("rangeEndHandle").toPointF().toPoint();QVERIFY(editor->rect().contains(handle));QTest::mousePress(editor,Qt::LeftButton,Qt::NoModifier,handle);QTest::mouseMove(editor,handle-QPoint(35,0));QTest::mouseRelease(editor,Qt::LeftButton,Qt::NoModifier,handle-QPoint(35,0));QTest::qWait(80);
  const auto owned=root->property("ownedRegion").value<QJSValue>().toVariant().toMap();QVERIFY(owned["end"].toInt()<end);QVERIFY(owned["end"].toInt()>start);QCOMPARE(score->undoStack()->getCurIdx(),undo);
  const auto folder=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!folder.isEmpty()){view->grab().save(folder+"/harmony-range-drag-050.png");root->window()->grabWindow().save(folder+"/harmony-range-panel-050.png");}
  config=root->property("configuration").value<QJSValue>().toVariant().toMap();config["dualPanel"]=true;config["summaryOrder"]=2;
  QVERIFY(QMetaObject::invokeMethod(root,"applyPreferences",Q_ARG(QVariant,QVariant(config))));QTRY_VERIFY(root->summaryPanelVisible());QTest::qWait(100);
  QVERIFY(root->summaryPanelHost()->height()>=root->property("desiredSummaryHeight").toInt()-2);
  auto more=root->findChild<QQuickItem*>("harmonyRibbonMore");QVERIFY(more&&more->isVisible());auto summaryWindow=more->window();QVERIFY(summaryWindow);
  QTest::mouseClick(summaryWindow,Qt::LeftButton,Qt::NoModifier,more->mapToScene(QPointF(more->width()/2,more->height()/2)).toPoint());QTest::qWait(80);
  auto menu=root->findChild<QQuickWindow*>("harmonyRibbonMenuWindow");QVERIFY(menu&&menu->isVisible());const auto area=root->panelScreen(more);QVERIFY(menu->x()>=area["x"].toInt());QVERIFY(menu->y()+menu->height()<=area["y"].toInt()+area["height"].toInt());
  auto appearance=root->findChild<QQuickItem*>("harmonyRibbonAppearance");QVERIFY(appearance&&appearance->isVisible());QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,appearance->mapToScene(QPointF(appearance->width()/2,appearance->height()/2)).toPoint());QTest::qWait(80);
  auto settings=root->findChild<QQuickWindow*>("harmonySummarySettings");QVERIFY(settings&&settings->isVisible());QVERIFY(settings->height()>=350);
  if(!folder.isEmpty()){summaryWindow->grabWindow().save(folder+"/harmony-vertical-summary-050.png");settings->grabWindow().save(folder+"/harmony-summary-style-050.png");}settings->hide();
  config["summaryOrder"]=0;QVERIFY(QMetaObject::invokeMethod(root,"applyPreferences",Q_ARG(QVariant,QVariant(config))));QTest::qWait(80);QVERIFY(root->summaryPanelHost()->height()<60);
  root->setVisible(false);root->deleteLater();QTest::qWait(50);
 }
