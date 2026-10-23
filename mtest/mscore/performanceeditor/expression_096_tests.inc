static QVariantMap expressionMatch096(QmlPlugin* root,const QString& scoreId)
{
    for(auto value:expressionMap080(root,"result").value("matches").toList())
        if(value.toMap().value("scoreId").toString()==scoreId)return value.toMap();
    return {};
}
void TestPerformanceEditor::expressionCorrectionKeepsPickedNotes()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;
    auto source=expressionScore080(main,_directory.filePath("correction-096.mscx"));QVERIFY(source);
    QMap<Note*,VelocityEdit> played;int number=0;
    for(auto segment=source->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))
        if(auto element=segment->element(0))if(element->isChord())for(auto note:toChord(element)->notes())played[note]={Note::ValueType::USER_VAL,60+2*number++};
    QCOMPARE(number,16);QVERIFY(ParameterEdit::velocities(source,played));
    QFile midi(_directory.filePath("correction-096.mid"));QVERIFY(midi.open(QIODevice::WriteOnly));
    ExportMidi exporter(source);QVERIFY(exporter.write(&midi,true,true,main->synthesizerState()));midi.close();
    EditData edit;source->undoRedo(true,&edit);const int undo=source->undoStack()->getCurIdx();
    expressionShowMain080(main);auto root=expressionRoot080(main);QVERIFY(root);
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)));
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi.fileName()).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==16,15000);
    QVariantMap a,b;
    for(auto value:expressionMap080(root,"result").value("matches").toList()){
        auto match=value.toMap();if(match.value("tick").toInt()==480)a=match;if(match.value("tick").toInt()==960)b=match;
    }
    QVERIFY(!a.isEmpty()&&!b.isEmpty());
    const auto scoreA=a.value("scoreId").toString(),scoreB=b.value("scoreId").toString();
    const auto midiA=a.value("midiId").toString(),midiB=b.value("midiId").toString();
    auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);
    QVERIFY(observer->showEditorCorrespondence().value("ok").toBool());
    auto editor=main->performanceEditor();auto lane=editor->findChild<QWidget*>("performanceWorkflowLanes");QVERIFY(lane);
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(0);
    main->resizeDocks({main->findChild<QDockWidget*>("performanceEditorDock")},{700},Qt::Vertical);
    editor->fit();QTest::qWait(120);QVERIFY(lane->isVisible());
    auto canvas=editor->findChild<QWidget*>("performanceParameterCanvas");QVERIFY(canvas);
    auto x=[&](int tick){return qRound(PerformanceViewport::gutter+tick/7680.*qMax(100,canvas->width()-PerformanceViewport::gutter-PerformanceViewport::rightMargin));};
    QSignalSpy requested(workflowContext(),&WorkflowContext::requested);
    QTest::mouseClick(lane,Qt::LeftButton,Qt::NoModifier,QPoint(x(480),13));
    QVERIFY(!requested.isEmpty());QCOMPARE(requested.back().at(1).toString(),scoreA);
    QTest::mouseClick(lane,Qt::LeftButton,Qt::NoModifier,QPoint(x(960),40));
    QCOMPARE(requested.back().at(1).toString(),scoreA);
    QCOMPARE(requested.back().at(2).toString(),midiB);
    QTRY_COMPARE(workflowContext()->expression().value("selectedScoreId").toString(),scoreA);
    QTRY_COMPARE(workflowContext()->expression().value("selectedMidiId").toString(),midiB);
    auto connect=editor->findChild<QToolButton*>("workflowConnectNotes");QVERIFY(connect);QVERIFY(connect->isEnabled());
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    QVERIFY(editor->grab().save(output.filePath("expression-picked-two-notes-096.png")));
    QTest::mouseClick(connect,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMatch096(root,scoreA).value("midiId").toString()==midiB,10000);
    const auto sourceHash=expressionMap080(root,"midi").value("sha256").toString();
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(_directory.filePath("missing-performance.mid")).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool(),10000);
    QCOMPARE(expressionMap080(root,"midi").value("sha256").toString(),sourceHash);
    QCOMPARE(expressionMatch096(root,scoreA).value("midiId").toString(),midiB);
    QVERIFY(expressionMatch096(root,scoreA).value("confirmed").toBool());
    QVERIFY(expressionMatch096(root,scoreB).isEmpty());
    QCOMPARE(source->undoStack()->getCurIdx(),undo);
    QTest::keyClick(lane,Qt::Key_Z,Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMatch096(root,scoreA).value("midiId").toString()==midiA,10000);
    QCOMPARE(expressionMatch096(root,scoreB).value("midiId").toString(),midiB);
    QTest::keyClick(lane,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMatch096(root,scoreA).value("midiId").toString()==midiB,10000);
    auto more=editor->findChild<QToolButton*>("workflowmore");QVERIFY(more);
    auto menu=editor->findChild<QMenu*>("workflowCorrespondenceMenu");QVERIFY(menu);
    auto unlink=menu->findChild<QAction*>("workflowunlink");QVERIFY(unlink);QTRY_VERIFY(unlink->isEnabled());
    bool menuClicked=false;QTimer::singleShot(60,menu,[&]{menuClicked=menu->isVisible();if(menuClicked)QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(unlink).center());menu->close();});
    QTest::mouseClick(more,Qt::LeftButton);QTRY_VERIFY(menuClicked);
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMatch096(root,scoreA).isEmpty(),10000);
    QCOMPARE(expressionMatch096(root,scoreB).value("midiId").toString(),midiB);
    QCOMPARE(workflowContext()->expression().value("selectedScoreId").toString(),scoreA);
    QTest::keyClick(lane,Qt::Key_Z,Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMatch096(root,scoreA).value("midiId").toString()==midiB,10000);
    root->setProperty("applicationScope",0);QVERIFY(QMetaObject::invokeMethod(root,"refreshLiveSelection"));
    QTRY_COMPARE(expressionMap080(root,"scopedCandidate").value("velocity").toList().size(),1);
    auto apply=editor->findChild<QToolButton*>("workflowapply");QVERIFY(apply);QTRY_VERIFY(apply->isEnabled());
    QTest::mouseClick(apply,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&source->undoStack()->getCurIdx()==undo+1,10000);
    auto target=toChord(source->findCR(Fraction::fromTicks(480),0))->upNote();
    QCOMPARE(target->veloType(),Note::ValueType::USER_VAL);QCOMPARE(target->veloOffset(),b.value("velocity").toInt());
    const auto project=output.filePath("expression-corrected-096.json");
    QVERIFY(QMetaObject::invokeMethod(root,"save",Q_ARG(QVariant,QUrl::fromLocalFile(project).toString())));
    const auto saved=output.filePath("expression-corrected-096.mscz");
    QVERIFY(observer->copyWorkflowScore(saved,observer->analysisRevision(),true).value("ok").toBool());
    auto reopened=main->readScore(saved);QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));
    tabs->setCurrentIndex(1);QTest::qWait(80);
    QVERIFY(QMetaObject::invokeMethod(root,"load",Q_ARG(QVariant,QUrl::fromLocalFile(project).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMatch096(root,scoreA).value("midiId").toString()==midiB,15000);
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!expressionMap080(root,"candidate").isEmpty(),10000);
    QCOMPARE(expressionMap080(root,"candidate").value("velocity").toList().size(),14);
    auto readBack=toChord(reopened->findCR(Fraction::fromTicks(480),0))->upNote();
    QCOMPARE(readBack->veloType(),Note::ValueType::USER_VAL);QCOMPARE(readBack->veloOffset(),b.value("velocity").toInt());
    QTest::qWait(120);QVERIFY(root->window()->grabWindow().save(output.filePath("expression-corrected-results-096.png")));
    QFile projectFile(project);QVERIFY(projectFile.open(QIODevice::ReadOnly));
    auto broken=QJsonDocument::fromJson(projectFile.readAll()).object();projectFile.close();
    auto expression=broken.value("expression").toObject();expression["source"]="missing-original.mid";broken["expression"]=expression;
    const auto missing=output.filePath("expression-missing-source-096.json");QFile missingFile(missing);QVERIFY(missingFile.open(QIODevice::WriteOnly));
    missingFile.write(QJsonDocument(broken).toJson());missingFile.close();
    QVERIFY(QMetaObject::invokeMethod(root,"load",Q_ARG(QVariant,QUrl::fromLocalFile(missing).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool(),10000);
    QCOMPARE(root->property("workspace").toString(),QUrl::fromLocalFile(project).toString());
    QCOMPARE(expressionMap080(root,"midi").value("sha256").toString(),sourceHash);
    QCOMPARE(expressionMatch096(root,scoreA).value("midiId").toString(),midiB);
}

void TestPerformanceEditor::expressionRolledPassageRoles()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;
    auto source=expressionScore095(main,_directory.filePath("rolled-096.mscx"));QVERIFY(source);
    expressionShowMain080(main);auto root=expressionRoot080(main);QVERIFY(root);
    source->selection().setRangeTicks(Fraction::fromTicks(8*1920),Fraction::fromTicks(12*1920),0,2);source->selection().updateSelectedElements();
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,false)));
    const auto midi=_directory.filePath("rolled-096.mid");QVERIFY(expressionMidi095(midi,160,false,.105));
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi).toString())));
    const int roles[]{0,1,2,2},tracks[]{0,0,0,4},counts[]{96,32,64,32};
    for(int i=0;i<4;++i){
        if(i){root->setProperty("role",roles[i]);root->setProperty("selectedTrack",tracks[i]);QVERIFY(QMetaObject::invokeMethod(root,"analyze"));}
        QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&expressionMap080(root,"candidate").value("velocity").toList().size()==counts[i],15000);
        const auto result=expressionMap080(root,"result");QCOMPARE(result.value("pending").toInt(),0);QCOMPARE(result.value("missing").toList().size(),0);
        for(auto value:result.value("matches").toList()){
            auto match=value.toMap();const int attack=match.value("tick").toInt()/240;
            const int key=expressionPitches095(attack).indexOf(match.value("pitch").toInt());QVERIFY(key>=0);
            QCOMPARE(match.value("velocity").toInt(),55+(attack+key*11)%54);
        }
    }
    root->setProperty("role",0);root->setProperty("selectedTrack",0);
}

void TestPerformanceEditor::expressionRealDecoratedPassage()
{
    const auto sourceFile=qEnvironmentVariable("P2_EXPRESSION_REAL_SCORE"),midi=qEnvironmentVariable("P2_EXPRESSION_REAL_MIDI");
    const auto reference=qEnvironmentVariable("P2_EXPRESSION_REAL_TRUTH");
    if(sourceFile.isEmpty()||midi.isEmpty()||reference.isEmpty())QSKIP("Provide separately licensed Chopin MusicXML, MIDI and expert reference files.");
    QFile file(reference);QVERIFY(file.open(QIODevice::ReadOnly));const auto json=QJsonDocument::fromJson(file.readAll()).object();
    const auto truth=json.value("truth").toObject();QMap<QString,double> seconds;QMultiMap<QString,double> expected;
    for(const auto& value:json.value("performance").toArray()){auto n=value.toObject();seconds[n["id"].toString()]=n["seconds"].toDouble();}
    int begin=INT_MAX,end=0;
    for(const auto& value:json.value("notes").toArray()){
        auto n=value.toObject();const int bar=n["measure"].toInt();if(bar<17||bar>20)continue;
        begin=qMin(begin,n["tick"].toInt());end=qMax(end,n["end"].toInt());
        auto id=truth[n["id"].toString()].toString();if(!id.isEmpty())expected.insert(QString::number(n["tick"].toInt())+":"+QString::number(n["pitch"].toInt()),seconds[id]);
    }
    QVERIFY(begin<end);QCOMPARE(expected.size(),115);
    expressionHost080(_directory.path());auto main=Ms::mscore;auto source=main->readScore(sourceFile);QVERIFY(source);
    main->setCurrentScoreView(main->appendScore(source));expressionShowMain080(main);auto root=expressionRoot080(main);QVERIFY(root);root->setProperty("role",0);
    source->selection().setRangeTicks(Fraction::fromTicks(begin),Fraction::fromTicks(end),0,source->nstaves());source->selection().updateSelectedElements();
    QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,false)));
    QVERIFY(QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(midi).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!expressionMap080(root,"candidate").isEmpty(),30000);
    const auto result=expressionMap080(root,"result");int correct=0,automatic=0,wrongAutomatic=0;
    for(auto value:result.value("matches").toList()){
        auto n=value.toMap();const auto key=QString::number(n["tick"].toInt())+":"+QString::number(n["pitch"].toInt());
        bool found=false;for(double time:expected.values(key))if(qAbs(time-n["seconds"].toDouble())<.002){found=true;break;}
        if(found)++correct;if(n["confidence"].toDouble()>=.8||n["confirmed"].toBool()){++automatic;if(!found)++wrongAutomatic;}
    }
    qInfo()<<"decorated passage: reference / correct / automatic / wrong automatic / pending / missing"<<expected.size()<<correct<<automatic<<wrongAutomatic<<result.value("pending")<<result.value("missing").toList().size();
    QCOMPARE(wrongAutomatic,0);QVERIFY(correct>=100);QVERIFY(automatic>=60);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    QVERIFY(root->window()->grabWindow().save(output.filePath("expression-real-chopin-096.png")));
}
