      void expressionParameterDragAndApply()
            {
            auto main=Ms::mscore;QVERIFY(main);auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            PluginAPI::Score wrap(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrap);observer.workflowInfo();auto scope=observer.analysisScope(true);auto revision=observer.analysisRevision();
            auto page=observer.performanceRange(0,scope["end"].toInt(),0,8,0,64,revision);QVariantList targets;
            for(auto value:page["notes"].toList()){auto n=value.toMap();if(n["track"].toInt()!=0||n["tick"].toInt()!=480)continue;n["target"]=60+n["index"].toInt()*25;n["oldType"]=n["velocityType"];n["oldRaw"]=n["velocityRaw"];n["oldBase"]=n["velocityBase"];targets.append(n);}
            QCOMPARE(targets.size(),3);QVariantMap candidate{{"revision",revision},{"scope",scope},{"velocity",targets}};
            PerformanceEditor editor;editor.resize(980,720);editor.setView(main->currentScoreView());editor.show();QTest::qWait(60);editor.fit();
            auto chord=noteAt(score)->chord()->notes();PerformanceSelection::apply(score,{chord[0]},chord[0]);editor.selectionChanged();
            QVERIFY(observer.setEditorComparison({{"owner","comparison-test"},{"revision",revision},{"previewVersion",1},{"previewCanUndo",true},{"candidate",candidate},{"scope",scope}})["ok"].toBool());
            auto toggle=editor.findChild<QToolButton*>("performanceExpressionCompare");QVERIFY(toggle&&toggle->isVisible()&&toggle->isChecked());
            auto parameter=editor.findChild<QComboBox*>("performanceParameter");QVERIFY(parameter&&parameter->isVisible());
            qInfo()<<"comparison layout"<<parameter->geometry()<<toggle->geometry();for(auto grid:editor.findChildren<QGridLayout*>())qInfo()<<"grid"<<grid<<grid->indexOf(parameter)<<grid->indexOf(toggle)<<grid->count();
            QVERIFY(!QRect(parameter->mapTo(&editor,QPoint()),parameter->size()).intersects(QRect(toggle->mapTo(&editor,QPoint()),toggle->size())));
            auto canvas=editor.findChild<QWidget*>("performanceParameterCanvas");QVERIFY(canvas);auto axis=editor.findChild<QComboBox*>("performanceAxis");axis->setCurrentIndex(0);QTest::qWait(30);
            QSignalSpy requests(workflowContext(),&WorkflowContext::requested);const int undo=score->undoStack()->getCurIdx();const int raw=chord[0]->veloOffset();
            QPoint hit(76+qRound((canvas->width()-90)/4.),qRound(canvas->height()-6-(85.-1)*(canvas->height()-12)/126.));
            QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,hit);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,hit-QPoint(0,18));QTest::qWait(30);
            QCOMPARE(chord[0]->veloOffset(),raw);QCOMPARE(score->undoStack()->getCurIdx(),undo);
            QVariantList edits;for(const auto& request:requests)if(request[0].toString()=="velocityTargets")edits=QJsonDocument::fromJson(request[2].toString().toUtf8()).array().toVariantList();
            QCOMPARE(edits.size(),1);QCOMPARE(edits.front().toMap()["id"].toString(),targets.front().toMap()["id"].toString());QVERIFY(edits.front().toMap()["target"].toInt()>60);
            QTest::keyClick(canvas,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(requests.back()[0].toString(),QString("undoPreview"));QCOMPARE(score->undoStack()->getCurIdx(),undo);
            auto chosen=targets.front().toMap();chosen["target"]=edits.front().toMap()["target"];candidate["velocity"]=QVariantList{chosen};auto applied=observer.applyExpression(candidate);QVERIFY2(applied["ok"].toBool(),qPrintable(applied["error"].toString()));QCOMPARE(score->undoStack()->getCurIdx(),undo+1);QVERIFY(chord[0]->veloOffset()!=raw);
            score->undoRedo(true,&ed);QCOMPARE(chord[0]->veloOffset(),raw);observer.setEditorComparison({});
            }
