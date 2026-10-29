// GPL-2.0-or-later. Keep each open score's expression material and unfinished edits together.
void TestPluginHost::velocityBackupKeepsOnlyScoreNotes149()
{
    std::unique_ptr<MasterScore> score(performanceScore123(_settings.filePath("velocity-history-149.mscx"),2));QVERIFY(score);
    auto main=toChord(score->firstSegment(SegmentType::ChordRest)->element(0));QVERIFY(main);auto note=main->upNote();
    score->startCmd();VelocityBackup::remember(score.get(),note);score->endCmd();
    // An undo/temporary grace chord can outlive the main chord it used to belong to.
    alignas(Ms::Chord) unsigned char storage[sizeof(Ms::Chord)]{};
    auto parent=new (storage) Ms::Chord(*main);auto grace=new Ms::Chord(score.get());grace->setNoteType(NoteType::ACCIACCATURA);grace->setTrack(0);grace->setParent(parent);
    auto extra=new Note(*note);grace->add(extra);QCOMPARE(VelocityBackup::original(extra),VelocityBackup::original(note));parent->~Chord();
    const auto dispose=qScopeGuard([&]{grace->setParent(nullptr);delete grace;});
    auto found=VelocityBackup::notes(score.get());QCOMPARE(found.size(),1);QCOMPARE(found.front(),note);
    auto clone=std::unique_ptr<MasterScore>(score->clone());QVERIFY(clone);QCOMPARE(VelocityBackup::notes(clone.get()).size(),1);
    EditData edit;score->undoRedo(true,&edit);QVERIFY(VelocityBackup::notes(score.get()).isEmpty());
    score->undoRedo(false,&edit);QCOMPARE(VelocityBackup::notes(score.get()),QList<Note*>{note});
}

void TestPluginHost::continuousWorkflowScoreSwitching_data()
{
    QTest::addColumn<QString>("source");QTest::addColumn<QString>("performance");QTest::addColumn<QString>("truth");
    QTest::newRow("independent")<<QString()<<QString()<<QString();
    if(!qEnvironmentVariable("P2_WORKFLOW_REAL_SCORE").isEmpty())
        QTest::newRow("real-performance")<<qEnvironmentVariable("P2_WORKFLOW_REAL_SCORE")<<qEnvironmentVariable("P2_WORKFLOW_REAL_MIDI")<<qEnvironmentVariable("P2_WORKFLOW_REAL_TRUTH");
}

void TestPluginHost::continuousWorkflowScoreSwitching()
{
    QFETCH(QString,source);QFETCH(QString,performance);QFETCH(QString,truth);auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);
    const auto label=source.isEmpty()?QString("independent"):QString("real");QVector<MasterScore*> scores;QByteArray sourceHash;
    const auto close=qScopeGuard([&]{releaseHostPanels();for(auto score:scores){score->undoStack()->setClean();main->closeScore(score);}});
    const auto path=_settings.filePath("workflow-switch-"+label+(source.isEmpty()?".mscx":"."+QFileInfo(source).suffix()));
    if(source.isEmpty()){QFile fixture(path);QVERIFY(fixture.open(QIODevice::WriteOnly));fixture.write(systemLockFixture138());}
    else {QFile original(source);QVERIFY(original.open(QIODevice::ReadOnly));sourceHash=QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256);QVERIFY(QFile::copy(source,path));}
    auto first=main->readScore(path);QVERIFY(first);scores.append(first);main->setCurrentScoreView(main->appendScore(first));
    auto second=performanceScore123(_settings.filePath("workflow-switch-other-"+label+".mscx"),4);QVERIFY(second);scores.append(second);main->appendScore(second);
    auto makeMidi=[&](MasterScore* score,const QString& name,int velocity){
        TemporaryScore copy(score->clone());if(!copy)return false;QMap<Note*,VelocityEdit> values;
        for(auto segment=copy->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))
            for(int tr=0;tr<score->ntracks();++tr)if(auto element=segment->element(tr))if(element->isChord())for(auto note:toChord(element)->notes())values.insert(note,{Note::ValueType::USER_VAL,velocity});
        if(!ParameterEdit::velocities(copy.get(),values))return false;
        QFile file(name);if(!file.open(QIODevice::WriteOnly))return false;ExportMidi exporter(copy.get());return exporter.write(&file,true,true,main->synthesizerState());
    };
    if(performance.isEmpty()){performance=_settings.filePath("workflow-first-"+label+".mid");QVERIFY(makeMidi(first,performance,105));}
    const auto otherMidi=_settings.filePath("workflow-other-"+label+".mid");QVERIFY(makeMidi(second,otherMidi,91));
    main->resize(1280,900);main->show();showExpressionPanel();auto editor=main->performanceEditor();QVERIFY(editor);
    auto page=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(page);auto root=qobject_cast<QmlPlugin*>(page->property("root").value<QObject*>());QVERIFY(root);
    auto map=[&](const char* name){const auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toMap():value.toMap();};
    auto list=[&](const char* name){const auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toList():value.toList();};
    auto imported=[&](const QString& file){
        if(!QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,true)))return false;
        return QMetaObject::invokeMethod(root,"importMidi",Q_ARG(QVariant,QUrl::fromLocalFile(file).toString()));
    };
    QVERIFY(imported(performance));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!map("candidate").value("velocity").toList().isEmpty(),30000);
    const auto firstMidi=map("midi").value("sha256").toString();const int firstMatches=map("result").value("matches").toList().size();QVERIFY(firstMatches>=32);
    if(!source.isEmpty()){
        QFile file(truth);QVERIFY(file.open(QIODevice::ReadOnly));const auto reference=QJsonDocument::fromJson(file.readAll()).object();
        QMap<QString,double> seconds;for(auto value:reference.value("performance").toArray()){auto n=value.toObject();seconds[n.value("id").toString()]=n.value("seconds").toDouble();}
        QMultiMap<QString,double> expected;const auto links=reference.value("truth").toObject();
        for(auto value:reference.value("notes").toArray()){auto n=value.toObject();auto id=links.value(n.value("id").toString()).toString();if(!id.isEmpty())expected.insert(QString::number(n.value("tick").toInt())+":"+QString::number(n.value("pitch").toInt()),seconds.value(id));}
        int correct=0,automatic=0;
        for(auto value:map("result").value("matches").toList()){auto n=value.toMap();bool found=false;for(auto t:expected.values(QString::number(n.value("tick").toInt())+":"+QString::number(n.value("pitch").toInt())))if(qAbs(t-n.value("seconds").toDouble())<.002){found=true;break;}correct+=found;automatic+=found&&(n.value("confidence").toDouble()>=.8||n.value("confirmed").toBool());}
        qInfo()<<"real correspondence / correct / directly usable"<<expected.size()<<correct<<automatic;QCOMPARE(correct,expected.size());QCOMPARE(automatic,expected.size());
    }
    const auto workspace=_settings.filePath("expression-"+label+".json");QVERIFY(QMetaObject::invokeMethod(root,"save",Q_ARG(QVariant,QUrl::fromLocalFile(workspace).toString())));QVERIFY(QFileInfo::exists(workspace));
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"velocityStrength"),Q_ARG(QVariant,42)));
    root->setProperty("selected",2);QTRY_VERIFY(!root->property("busy").toBool()&&!root->property("previewUpdating").toBool());
    auto content=[&](){auto value=map("candidate");value.remove("revision");return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);};
    const auto firstCandidate=content();const auto firstScope=map("scope");const auto firstNotes=list("notes");
    main->setCurrentScoreView(main->scores().indexOf(second));QTest::qWait(80);
    QVERIFY2(map("midi").isEmpty(),"A newly opened score must not display the other score's performance material");
    QVERIFY(root->property("workspace").toString().isEmpty());QVERIFY(imported(otherMidi));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!map("candidate").value("velocity").toList().isEmpty(),15000);
    const auto secondMidi=map("midi").value("sha256").toString();QVERIFY(secondMidi!=firstMidi);
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"velocityStrength"),Q_ARG(QVariant,77)));
    QTRY_VERIFY(!root->property("busy").toBool()&&!root->property("previewUpdating").toBool());
    const auto firstState=first->state(),secondState=second->state();const int firstUndo=first->undoStack()->getCurIdx(),secondUndo=second->undoStack()->getCurIdx();
    for(int round=0;round<3;++round){
        main->setCurrentScoreView(main->scores().indexOf(first));QTest::qWait(60);
        QCOMPARE(map("midi").value("sha256").toString(),firstMidi);QCOMPARE(map("result").value("matches").toList().size(),firstMatches);
        QCOMPARE(map("config").value("velocityStrength").toInt(),42);QCOMPARE(root->property("selected").toInt(),2);
        QCOMPARE(map("scope"),firstScope);QCOMPARE(list("notes"),firstNotes);QCOMPARE(root->property("workspace").toString(),QUrl::fromLocalFile(workspace).toString());
        QCOMPARE(content(),firstCandidate);
        auto observer=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observer);QCOMPARE(map("candidate").value("revision").toString(),observer->analysisRevision());
        QVERIFY(root->property("dirtyProject").toBool());QVERIFY(workflowContext()->current(first));QVERIFY(!workflowContext()->current(second));
        if(!round){main->showPerformanceEditor(false);QTest::qWait(60);showExpressionPanel();QTest::qWait(60);QCOMPARE(content(),firstCandidate);}
        main->setCurrentScoreView(main->scores().indexOf(second));QTest::qWait(60);
        QCOMPARE(map("midi").value("sha256").toString(),secondMidi);QCOMPARE(map("config").value("velocityStrength").toInt(),77);
        QVERIFY(workflowContext()->current(second));QVERIFY(!workflowContext()->current(first));
    }
    QVERIFY(first->state()==firstState&&second->state()==secondState);QCOMPARE(first->undoStack()->getCurIdx(),firstUndo);QCOMPARE(second->undoStack()->getCurIdx(),secondUndo);
    // Preserve a completed tempo preview; recompute when its selected application range changes.
    main->setCurrentScoreView(main->scores().indexOf(first));QTest::qWait(60);
    auto tempoMatches=map("result").value("matches").toList();QVERIFY(tempoMatches.size()>20);
    const QVariant tempoRow=QVariantMap{{"kind","match"},{"index",0},{"match",tempoMatches.front()}};
    QVERIFY(QMetaObject::invokeMethod(root,"selectResultRow",Q_ARG(QVariant,tempoRow)));root->setProperty("applicationScope",0);
    QVERIFY(QMetaObject::invokeMethod(root,"editConfig",Q_ARG(QVariant,"rubato"),Q_ARG(QVariant,true)));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!root->property("previewUpdating").toBool()&&!map("candidate").isEmpty(),15000);
    const auto tempoPreview=content();auto observed=qobject_cast<PluginAPI::ScoreObserver*>(root->property("observer").value<QObject*>());QVERIFY(observed);
    QSignalSpy recalculated(observed,SIGNAL(readOnlyJobFinished(int,QVariantMap)));
    main->setCurrentScoreView(main->scores().indexOf(second));QTest::qWait(80);recalculated.clear();
    main->setCurrentScoreView(main->scores().indexOf(first));QTest::qWait(220);QCOMPARE(content(),tempoPreview);QCOMPARE(recalculated.size(),0);
    main->setCurrentScoreView(main->scores().indexOf(second));QTest::qWait(80);recalculated.clear();
    auto other=tempoMatches[20].toMap().value("original").toMap();auto otherChord=first->findCR(Fraction::fromTicks(other.value("tick").toInt()),other.value("track").toInt());QVERIFY(otherChord&&otherChord->isChord());
    const int otherIndex=other.value("index").toInt();QVERIFY(otherIndex>=0&&otherIndex<int(toChord(otherChord)->notes().size()));first->select(toChord(otherChord)->notes()[otherIndex]);
    main->setCurrentScoreView(main->scores().indexOf(first));QTRY_VERIFY_WITH_TIMEOUT(recalculated.size()>0&&!root->property("busy").toBool()&&!root->property("previewUpdating").toBool(),15000);
    QVERIFY(first->state()==firstState);QCOMPARE(first->undoStack()->getCurIdx(),firstUndo);
    // Editing the inactive score must invalidate only its saved preview.
    main->setCurrentScoreView(main->scores().indexOf(second));QTest::qWait(60);
    auto chord=first->firstSegment(SegmentType::ChordRest);while(chord&&!chord->element(0))chord=chord->next1(SegmentType::ChordRest);QVERIFY(chord);
    Note* changed=nullptr;for(int tr=0;tr<first->ntracks()&&!changed;++tr)if(auto element=chord->element(tr))if(element->isChord())changed=toChord(element)->upNote();QVERIFY(changed);
    first->startCmd();first->undoChangePitch(changed,changed->pitch()+12,changed->tpc1(),changed->tpc2());first->endCmd();
    main->setCurrentScoreView(main->scores().indexOf(first));QTest::qWait(60);QVERIFY(root->property("stale").toBool());QVERIFY(map("candidate").isEmpty());
    QCOMPARE(map("midi").value("sha256").toString(),firstMidi);QCOMPARE(map("config").value("velocityStrength").toInt(),42);
    QVERIFY(QMetaObject::invokeMethod(root,"analyze"));QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!root->property("stale").toBool()&&!map("candidate").isEmpty(),30000);
    // Resume a user-requested project read after leaving and returning to its score.
    QVERIFY(QMetaObject::invokeMethod(root,"save",Q_ARG(QVariant,QUrl::fromLocalFile(workspace).toString())));
    QVERIFY(QMetaObject::invokeMethod(root,"load",Q_ARG(QVariant,QUrl::fromLocalFile(workspace).toString())));
    main->setCurrentScoreView(main->scores().indexOf(second));QTest::qWait(100);QCOMPARE(map("midi").value("sha256").toString(),secondMidi);
    main->setCurrentScoreView(main->scores().indexOf(first));QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!map("candidate").isEmpty()&&!root->property("stale").toBool(),30000);
    QCOMPARE(map("config").value("velocityStrength").toInt(),42);QCOMPARE(map("midi").value("sha256").toString(),firstMidi);
    // Closing a score ends its memory session; explicit project loading restores it in a new score session.
    QFileInfo saved(_settings.filePath("workflow-switch-reopened-"+label+".mscz"));QVERIFY(first->saveCompressedFile(saved,false,false));
    scores.removeOne(first);first->undoStack()->setClean();main->closeScore(first);QTest::qWait(60);QCOMPARE(map("midi").value("sha256").toString(),secondMidi);
    first=main->readScore(saved.filePath());QVERIFY(first);scores.append(first);main->setCurrentScoreView(main->appendScore(first));QTest::qWait(60);
    QVERIFY(map("midi").isEmpty());QVERIFY(root->property("workspace").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(root,"load",Q_ARG(QVariant,QUrl::fromLocalFile(workspace).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&!map("candidate").isEmpty()&&!root->property("stale").toBool(),30000);
    QCOMPARE(map("midi").value("sha256").toString(),firstMidi);QCOMPARE(map("config").value("velocityStrength").toInt(),42);
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(main->grab().save(out.filePath("workflow-switch-"+label+".png")));
    // A restored comparison must still select and actually write its notes.
    const auto editable=map("candidate").value("velocity").toList();QVERIFY(!editable.isEmpty());const auto id=editable.front().toMap().value("id").toString();
    const auto matches=map("result").value("matches").toList();int chosen=-1;for(int i=0;i<matches.size();++i)if(matches[i].toMap().value("scoreId").toString()==id){chosen=i;break;}QVERIFY(chosen>=0);
    const QVariant row=QVariantMap{{"kind","match"},{"index",chosen},{"match",matches[chosen]}};
    QVERIFY(QMetaObject::invokeMethod(root,"selectResultRow",Q_ARG(QVariant,row)));QTRY_COMPARE(first->selection().noteList().size(),1);
    auto selected=first->selection().noteList().front();const auto oldType=selected->veloType();const auto oldRaw=selected->veloOffset();
    root->setProperty("applicationScope",0);QTRY_COMPARE(map("scopedCandidate").value("velocity").toList().size(),1);
    const int target=map("scopedCandidate").value("velocity").toList().front().toMap().value("target").toInt(),beforeApply=first->undoStack()->getCurIdx();
    QVERIFY(QMetaObject::invokeMethod(root,"applyPart",Q_ARG(QVariant,"velocity")));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("busy").toBool()&&first->undoStack()->getCurIdx()==beforeApply+1,15000);
    QCOMPARE(selected->veloType(),Note::ValueType::USER_VAL);QCOMPARE(selected->veloOffset(),target);
    EditData undo;first->undoRedo(true,&undo);QCOMPARE(selected->veloType(),oldType);QCOMPARE(selected->veloOffset(),oldRaw);
    qInfo()<<"score-owned expression"<<label<<firstMatches<<"first-note-count"<<firstNotes.size()<<"second"<<map("midi").value("sha256");
    if(!source.isEmpty()){QFile original(source);QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256),sourceHash);}
}
