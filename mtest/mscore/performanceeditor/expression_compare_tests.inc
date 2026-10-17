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

      void expressionVelocityModesAndComparison()
            {
            auto main=Ms::mscore;QVERIFY(main);auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            auto accented=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0));auto accent=new Articulation(score);accent->setSymId(SymId::articAccentAbove);accent->setParent(accented);accent->setTrack(0);score->startCmd();score->undoAddElement(accent);score->endCmd();
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);observer.workflowInfo();auto scope=observer.analysisScope(true);
            auto revision=observer.analysisRevision();auto page=observer.performanceRange(0,scope["end"].toInt(),0,score->ntracks(),0,128,revision);
            QVariantList values;for(auto n:page["notes"].toList()){
                  auto v=n.toMap();if(v["tick"].toInt()!=480||v["track"].toInt()!=0)continue;
                  v["oldType"]=v["velocityType"];v["oldRaw"]=v["velocityRaw"];v["oldBase"]=v["velocityBase"];v["target"]=100;v["writeMode"]="fixed";v["unlock"]=true;values.append(v);
                  }
            QVERIFY(!values.isEmpty());QVariantMap candidate{{"revision",revision},{"scope",scope},{"velocity",values}};
            const int undo=score->undoStack()->getCurIdx();auto applied=observer.applyExpression(candidate);QVERIFY2(applied["ok"].toBool(),qPrintable(applied["error"].toString()));QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
            auto notes=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes();
            for(auto note:notes){QCOMPARE(note->veloType(),Note::ValueType::USER_VAL);QCOMPARE(note->veloOffset(),100);}
            EventMap events;score->renderMidi(&events,false,false,defaultState);int heard=0;for(auto e:events)if(e.second.type()==ME_NOTEON&&e.second.velo()>0&&e.second.note()&&e.second.note()->chord()->tick().ticks()==480&&e.second.note()->track()==0){QCOMPARE(e.second.velo(),100);++heard;}QVERIFY(heard>=int(notes.size()));
            EditData edit;score->undoRedo(true,&edit);revision=observer.analysisRevision();
            for(auto& value:values){auto v=value.toMap();v["writeMode"]="follow";value=v;}candidate["velocity"]=values;candidate["revision"]=revision;
            applied=observer.applyExpression(candidate);QVERIFY2(applied["ok"].toBool(),qPrintable(applied["error"].toString()));for(auto note:notes){QCOMPARE(note->veloType(),Note::ValueType::OFFSET_VAL);QVERIFY(qAbs(NoteVelocity::effective(NoteVelocity::referenceBase(note),note->veloType(),note->veloOffset())-100)<=1);}
            score->undoRedo(true,&edit);revision=observer.analysisRevision();candidate["revision"]=revision;for(auto& value:values){auto v=value.toMap();v["target"]=60+v["index"].toInt()*25;value=v;}candidate["velocity"]=values;
            candidate["tempos"]=QVariantList{QVariantMap{{"tick",0},{"oldBpm",score->tempomap()->tempo(0)*60},{"bpm",score->tempomap()->tempo(0)*30}}};
            QSettings().setValue("performanceEditor/markVelocityLines",true);QSettings().setValue("performanceEditor/comparisonVelocityLines",false);QSettings().setValue("performanceEditor/comparisonView",0);
            PerformanceEditor editor;editor.resize(980,720);editor.setView(main->currentScoreView());editor.show();QTest::qWait(100);editor.fit();
            QVERIFY(observer.setEditorComparison({{"owner","modes-test"},{"revision",revision},{"previewVersion",1},{"candidate",candidate},{"scope",scope}})["ok"].toBool());QTest::qWait(80);
            auto mode=editor.findChild<QComboBox*>("performanceComparisonView");auto guides=editor.findChild<QCheckBox*>("performanceActualVelocity");auto toggle=editor.findChild<QToolButton*>("performanceExpressionCompare");QVERIFY(mode&&mode->isVisible());QCOMPARE(mode->currentIndex(),0);QVERIFY(guides&&!guides->isChecked());
            auto folder=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!folder.isEmpty())editor.grab().save(folder+"/expression-same-note-057.png");
            mode->setCurrentIndex(1);editor.fit();QTest::qWait(80);if(!folder.isEmpty())editor.grab().save(folder+"/expression-time-057.png");
            auto canvas=editor.findChild<QWidget*>("performanceParameterCanvas");QVERIFY(canvas);auto axis=editor.findChild<QComboBox*>("performanceAxis");axis->setCurrentIndex(0);
            // Tempo halved: the proposal at beat two appears twice as far along the seconds axis.
            const int end=scope["end"].toInt();const double seconds=score->tempomap()->tick2time(end)*2;
            const double x=76+2*score->tempomap()->tick2time(480)/seconds*(canvas->width()-90)+4.5;
            const int y=qRound(canvas->height()-6-(100.-1)*(canvas->height()-12)/126.);
            QSignalSpy requests(workflowContext(),&WorkflowContext::requested);QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(qRound(x),y));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(qRound(x),y-12));
            bool changed=false;for(auto request:requests)if(request[0].toString()=="velocityTargets")changed=true;QVERIFY(changed);
            guides->setChecked(true);toggle->setChecked(false);QVERIFY(guides->isChecked());toggle->setChecked(true);QVERIFY(guides->isChecked());
            observer.setEditorComparison({});QVERIFY(guides->isChecked());
            }
