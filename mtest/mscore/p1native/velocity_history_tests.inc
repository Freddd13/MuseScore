 void expressionVelocitySavedHistory(){
  auto s=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(s);mscore->setCurrentScoreView(mscore->appendScore(s));
  PluginAPI::Score wrap(s);PluginAPI::ScoreObserver observer;observer.setScore(&wrap);observer.workflowInfo();
  auto scope=observer.analysisScope(true);auto page=observer.performanceRange(0,scope["end"].toInt(),0,8,0,64,observer.analysisRevision());
  QVariantList changes;QList<Note*> notes;QList<QPair<int,int>> original;
  for(auto value:page["notes"].toList()){
   auto row=value.toMap();if(row["grace"].toBool()||row["tieBack"].toBool())continue;
   auto segment=s->tick2segment(Fraction::fromTicks(row["tick"].toInt()),false,SegmentType::ChordRest);
   auto note=toChord(segment->element(row["track"].toInt()))->notes()[row["index"].toInt()];
   if(notes.size()==1){note->setVeloType(Note::ValueType::USER_VAL);note->setVeloOffset(55);row["velocityType"]=int(note->veloType());row["velocityRaw"]=note->veloOffset();}
   original.append({int(note->veloType()),note->veloOffset()});notes.append(note);
   row["oldType"]=row["velocityType"];row["oldRaw"]=row["velocityRaw"];row["oldBase"]=row["velocityBase"];row["target"]=101;row["unlock"]=true;changes.append(row);
   if(changes.size()==2)break;
  }
  QCOMPARE(changes.size(),2);QVERIFY(VelocityBackup::notes(s).isEmpty());
  const int undo=s->undoStack()->getCurIdx();
  QVariantMap candidate{{"revision",observer.analysisRevision()},{"scope",scope},{"velocity",changes}};
  auto applied=observer.applyExpression(candidate);QVERIFY2(applied["ok"].toBool(),qPrintable(applied["error"].toString()));
  QCOMPARE(s->undoStack()->getCurIdx(),undo+1);QCOMPARE(VelocityBackup::notes(s).size(),2);
  QCOMPARE(applied["appliedVelocity"].toList().size(),2);
  for(int i=0;i<2;++i)QCOMPARE(VelocityBackup::original(notes[i]),original[i]);
  EditData edit;s->undoRedo(true,&edit);QVERIFY(VelocityBackup::notes(s).isEmpty());
  for(int i=0;i<2;++i){QCOMPARE(int(notes[i]->veloType()),original[i].first);QCOMPARE(notes[i]->veloOffset(),original[i].second);}
  s->undoRedo(false,&edit);QCOMPARE(VelocityBackup::notes(s).size(),2);
  auto all=scope;all["revision"]=observer.analysisRevision();QCOMPARE(observer.expressionVelocityHistory(all)["count"].toInt(),2);
  s->select(notes.front(),SelectType::SINGLE);auto selected=all;selected["selectedOnly"]=true;
  QCOMPARE(observer.expressionVelocityHistory(selected)["count"].toInt(),1);
  selected["revision"]=observer.analysisRevision();auto restored=observer.restoreExpressionVelocity(selected);
  QVERIFY2(restored["ok"].toBool(),qPrintable(restored["error"].toString()));QCOMPARE(restored["changed"].toInt(),1);
  QCOMPARE(notes[0]->veloOffset(),original[0].second);QVERIFY(notes[1]->veloOffset()!=original[1].second);
  s->undoRedo(true,&edit);QVERIFY(notes[0]->veloOffset()!=original[0].second);
  // A new comparison starting point and its undo retain the original first application.
  all["revision"]=observer.analysisRevision();QVERIFY(observer.saveCurrentExpressionVelocity(all)["ok"].toBool());
  QCOMPARE(VelocityBackup::original(notes[0]).second,notes[0]->veloOffset());
  s->undoRedo(true,&edit);QCOMPARE(VelocityBackup::original(notes[0]),original[0]);
  QString path=settings.filePath("expression-history.mscz");
  auto saved=observer.copyWorkflowScore(path,observer.analysisRevision(),false);QVERIFY2(saved["ok"].toBool(),qPrintable(saved["error"].toString()));
  auto reopened=mscore->readScore(path);QVERIFY(reopened);mscore->setCurrentScoreView(mscore->appendScore(reopened));
  PluginAPI::Score reopenedWrap(reopened);PluginAPI::ScoreObserver reopenedObserver;reopenedObserver.setScore(&reopenedWrap);
  auto range=reopenedObserver.analysisScope(true);range["revision"]=reopenedObserver.analysisRevision();
  QCOMPARE(reopenedObserver.expressionVelocityHistory(range)["count"].toInt(),2);
  auto result=reopenedObserver.restoreExpressionVelocity(range);QVERIFY2(result["ok"].toBool(),qPrintable(result["error"].toString()));QCOMPARE(result["changed"].toInt(),2);
  for(auto note:VelocityBackup::notes(reopened)){auto old=VelocityBackup::original(note);QCOMPARE(int(note->veloType()),old.first);QCOMPARE(note->veloOffset(),old.second);}
  range["revision"]="expired";QVERIFY(reopenedObserver.restoreExpressionVelocity(range).contains("error"));
 }
