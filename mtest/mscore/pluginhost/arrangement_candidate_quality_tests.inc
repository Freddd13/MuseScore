// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "mscore/workflow/temporaryscore.h"
#include "mscore/workflow/regionalcomparison.h"
static MasterScore* candidateScoreC3(const QByteArray& xml,const QString& path)
{
    QFile file(path);if(!file.open(QIODevice::WriteOnly))return nullptr;file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);if(!score)return nullptr;
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,920);main->show();score->deselectAll();return score;
}
static QWidget* candidatePanelC3()
{
    showSuggestionPanel(true);auto panel=Ms::mscore->findChild<QWidget*>("arrangementSuggestionPanel");
    if(panel)if(auto hand=panel->findChild<QComboBox*>("localSuggestionHand"))hand->setCurrentIndex(hand->findData("auto"));
    return panel;
}
static bool candidateHasResultC3(QSignalSpy& completed,QSignalSpy& progress)
{
    if(completed.size()==1)return true;
    if(!progress.isEmpty())qWarning().noquote()<<progress.last()[0].toString();return false;
}
void TestPluginHost::regionalDifferencePreservesMaterial()
{
    QVariantList rows{QVariant::fromValue(QVariantList{60,0,240,0,14}),QVariant::fromValue(QVariantList{62,240,480,0,16}),QVariant::fromValue(QVariantList{64,480,720,0,18})};
    auto score=candidateScoreC3(arrangementScore115(rows),_settings.filePath("candidate-paint-state.mscx"));QVERIFY(score);auto view=Ms::mscore->currentScoreView();
    std::shared_ptr<MasterScore> other(score->clone(),TemporaryScoreDeleter{});QVERIFY(other);auto note=toChord(other->firstSegment(SegmentType::ChordRest)->element(0))->upNote();
    auto symbol=new Symbol(other.get());symbol->setSym(SymId::keyboardPlayWithLH);symbol->applyHandPreset();symbol->setParent(note);symbol->setTrack(0);
    auto finger=new Fingering(other.get());finger->setParent(note);finger->setTrack(0);finger->setPlainText("2");
    other->startCmd();other->undoAddElement(symbol);other->undoAddElement(finger);other->endCmd();other->doLayout();symbol->setColor(QColor("#b85442"));symbol->setSelected(true);finger->setColor(QColor("#547e45"));note->setSelected(true);
    struct Value{Element* element;QColor color;bool selected;};QVector<Value> values;QSet<Element*> known;
    for(auto segment=other->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int track=0;track<other->ntracks();++track)if(auto e=segment->element(track)) {
        QList<Element*> elements;e->scanElements(&elements,[](void* out,Element* item){static_cast<QList<Element*>*>(out)->append(item);},false);if(e->isChord()&&toChord(e)->beam())elements.append(toChord(e)->beam());
        for(auto item:elements)if(!known.contains(item)){known.insert(item);values.append({item,item->color(),item->selected()});}
    }
    const QVariantMap scope{{"start",0},{"end",1920},{"firstTrack",0},{"endTrack",8}};const auto before=rollNotationIdentity(other.get(),scope);const int undo=score->undoStack()->getCurIdx();
    for(int i=0;i<4;++i) {
        showRegionalDifference(view,other.get(),{QVariantMap{{"tick",0},{"track",0}},QVariantMap{{"tick",240},{"track",0}}});QTest::qWait(30);QVERIFY(!view->grab().isNull());
        QCOMPARE(rollNotationIdentity(other.get(),scope),before);for(const auto& value:values){QCOMPARE(value.element->color(),value.color);QCOMPARE(value.element->selected(),value.selected);}
        showRegionalDifference(view,nullptr,{});
    }
    QCOMPARE(score->undoStack()->getCurIdx(),undo);
}
void TestPluginHost::traditionalVoicingCandidateFlow()
{
    QVariantList rows{QVariant::fromValue(QVariantList{72,0,480,0,14}),QVariant::fromValue(QVariantList{36,0,960,4,14}),QVariant::fromValue(QVariantList{60,0,960,4,14})};
    auto score=candidateScoreC3(arrangementScore115(rows),_settings.filePath("candidate-voicing.mscx"));QVERIFY(score);auto main=Ms::mscore;
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));auto low=chord->notes().front();NoteEventList shorter;shorter.clear();shorter.append(NoteEvent(0,0,500));
    score->startCmd();score->undo(new ChangeNoteEventList(low,shorter));low->undoChangeProperty(Pid::VELO_OFFSET,17);score->endCmd();main->endCmd();
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    QVariantMap config{{"harmony",false},{"counterpoint",false},{"roles",QVariantMap{{"0",QVariantMap{{"role","melody"},{"hand","right"}}},{"4",QVariantMap{{"role","accompaniment"},{"hand","left"}}}}}};observer.publishPianoSettings(config);
    auto panel=candidatePanelC3();QVERIFY(panel);struct Close{~Close(){showSuggestionPanel(false);}}close;
    auto task=panel->findChild<LocalSuggestionTask*>();QVERIFY(task);QSignalSpy completed(task,&LocalSuggestionTask::completed),progress(task,&LocalSuggestionTask::progress);
    panel->findChild<QComboBox*>("localSuggestionKind")->setCurrentIndex(2);auto start=panel->findChild<QPushButton*>("localSuggestionStart");QVERIFY(start);QTest::mouseClick(start,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!task->running(),15000);QVERIFY(candidateHasResultC3(completed,progress));const auto result=completed.front()[0].toMap();QVERIFY(result.value("preserveTracks").toList().contains(0));
    const auto options=result.value("options").toList();QVERIFY(!options.isEmpty());for(auto value:options)for(auto change:value.toMap().value("changes").toList())QVERIFY(change.toMap().value("ref").toMap().value("id").toString().contains(":4:"));
    const int before=score->undoStack()->getCurIdx();auto selected=arrangementSuggestions(score).value("options").toList().front().toMap();auto performance=observer.arrangementRequest("arrangementOption",{{"optionId",selected["id"]},{"action","performance"}});QVERIFY(!performance.contains("error"));
    auto apply=panel->findChild<QPushButton*>("arrangement-apply");QVERIFY(apply&&apply->isEnabled());auto keep=panel->findChild<QCheckBox*>("localSuggestionPreserveMelody");QVERIFY(keep&&keep->isChecked());QTest::mouseClick(keep,Qt::LeftButton);QVERIFY(!apply->isEnabled());QTest::mouseClick(keep,Qt::LeftButton);QVERIFY(apply->isEnabled());
    auto changedSettings=config;changedSettings["roles"]=QVariantMap{{"0",QVariantMap{{"role","melody"},{"hand","right"}}},{"4",QVariantMap{{"role","melody"},{"hand","left"}}}};observer.publishPianoSettings(changedSettings);QVERIFY(!apply->isEnabled());auto rejected=observer.arrangementRequest("arrangementOption",{{"optionId",selected["id"]},{"action","apply"}});QVERIFY(rejected.contains("error"));QCOMPARE(score->undoStack()->getCurIdx(),before);observer.publishPianoSettings(config);QVERIFY(apply->isEnabled());
    QCOMPARE(performance.value("original").toList().size(),performance.value("proposed").toList().size());QVERIFY(performance.value("original")!=performance.value("proposed"));
    QTRY_VERIFY(main->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer")->isVisible());const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::qWait(100);QVERIFY(main->grab().save(out.filePath("candidate-voicing.png")));
    QTest::mouseClick(apply,Qt::LeftButton);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+1);
    QCOMPARE(toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->upNote()->pitch(),72);QCOMPARE(low->veloOffset(),17);QCOMPARE(low->playEvents(),shorter);
    auto changed=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));QVERIFY(changed->upNote()->pitch()-changed->downNote()->pitch()<=16);
    const auto output=out.filePath("candidate-voicing-applied.mscz");QFileInfo info(output);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> reopened(main->readScore(output));QVERIFY(reopened);QCOMPARE(toChord(reopened->firstSegment(SegmentType::ChordRest)->element(4))->downNote()->pitch(),changed->downNote()->pitch());
    main->cmd(getAction("undo"));QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(low->pitch(),36);QCOMPARE(low->playEvents(),shorter);
    config["roles"]=QVariantMap{{"0",QVariantMap{{"role","melody"},{"hand","right"}}},{"4",QVariantMap{{"role","melody"},{"hand","left"}}}};observer.publishPianoSettings(config);score->deselectAll();completed.clear();QTest::mouseClick(start,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!task->running(),15000);QCOMPARE(completed.size(),0);
}
void TestPluginHost::traditionalHandoffCandidateFlow()
{
    QVariantList rows{QVariant::fromValue(QVariantList{60,0,480,0,14}),QVariant::fromValue(QVariantList{84,0,480,0,14}),QVariant::fromValue(QVariantList{48,0,480,4,14})};
    auto score=candidateScoreC3(arrangementScore115(rows),_settings.filePath("candidate-handoff.mscx"));QVERIFY(score);auto main=Ms::mscore;
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);observer.publishPianoSettings({{"harmony",false},{"counterpoint",false},{"roles",QVariantMap{}}});
    auto panel=candidatePanelC3();QVERIFY(panel);struct Close{~Close(){showSuggestionPanel(false);}}close;
    auto task=panel->findChild<LocalSuggestionTask*>();QVERIFY(task);QSignalSpy completed(task,&LocalSuggestionTask::completed),progress(task,&LocalSuggestionTask::progress);panel->findChild<QComboBox*>("localSuggestionKind")->setCurrentIndex(1);
    QTest::mouseClick(panel->findChild<QPushButton*>("localSuggestionStart"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!task->running(),15000);QVERIFY(candidateHasResultC3(completed,progress));
    const auto options=completed.front()[0].toMap().value("options").toList();QVERIFY(!options.isEmpty());QVERIFY(options.front().toMap().value("summary").toString().contains("C4"));
    auto selected=arrangementSuggestions(score).value("options").toList().front().toMap();auto performance=observer.arrangementRequest("arrangementOption",{{"optionId",selected["id"]},{"action","performance"}});QVERIFY(!performance.contains("error"));QCOMPARE(performance.value("original"),performance.value("proposed"));
    auto changes=panel->findChild<QTreeWidget*>("arrangementChanges");QVERIFY(changes&&changes->topLevelItemCount());auto row=changes->topLevelItem(0);QVERIFY(row->text(2).contains(QString::fromUtf8("分手")));
    QTRY_VERIFY(main->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer")->isVisible());
    QVERIFY(changes->columnWidth(2)>=changes->fontMetrics().horizontalAdvance(row->text(2))+8);
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::qWait(100);QVERIFY(main->grab().save(out.filePath("candidate-handoff.png")));
    const int before=score->undoStack()->getCurIdx();auto apply=panel->findChild<QPushButton*>("arrangement-apply");QVERIFY(apply&&apply->isEnabled());QTest::mouseClick(apply,Qt::LeftButton);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+1);
    auto note=toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->downNote();bool marked=false;for(auto e:note->el())if(e->isSymbol())marked|=toSymbol(e)->handEligible()&&toSymbol(e)->handLeft();QVERIFY(marked);
    const auto output=out.filePath("candidate-handoff-applied.mscz");QFileInfo info(output);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> reopened(main->readScore(output));QVERIFY(reopened);
    auto saved=toChord(reopened->firstSegment(SegmentType::ChordRest)->element(0))->downNote();bool persisted=false;for(auto e:saved->el())if(e->isSymbol())persisted|=toSymbol(e)->handEligible()&&toSymbol(e)->handLeft();QVERIFY(persisted);
    auto data=observer.analysisRange(0,1920,0,8,0,32,observer.analysisRevision());QVERIFY(!data.value("hands").toList().isEmpty());main->cmd(getAction("undo"));QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(note->pitch(),60);
}
void TestPluginHost::traditionalCounterpointHeldToneFlow()
{
    QVariantList rows{QVariant::fromValue(QVariantList{67,0,480,0,15}),QVariant::fromValue(QVariantList{69,480,960,0,17}),QVariant::fromValue(QVariantList{60,0,480,4,14}),QVariant::fromValue(QVariantList{62,480,960,4,16})};
    auto xml=arrangementScore115(rows);xml.insert(xml.lastIndexOf("</Measure></Staff>"),"<voice><Chord><durationType>half</durationType><Note><pitch>65</pitch><tpc>13</tpc></Note></Chord>"+arrangementRests115(960)+"</voice>");
    auto score=candidateScoreC3(xml,_settings.filePath("candidate-counterpoint.mscx"));QVERIFY(score);auto main=Ms::mscore;
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);observer.publishPianoSettings({{"style",0},{"harmony",true},{"counterpoint",true},{"roles",QVariantMap{{"0",QVariantMap{{"role","melody"},{"line","single"},{"independent",true},{"hand","right"}}},{"4",QVariantMap{{"role","bass"},{"line","single"},{"independent",true},{"hand","left"}}},{"5",QVariantMap{{"independent",false},{"hand","left"}}}}}});
    auto panel=candidatePanelC3();QVERIFY(panel);struct Close{~Close(){showSuggestionPanel(false);}}close;auto task=panel->findChild<LocalSuggestionTask*>();QVERIFY(task);QSignalSpy completed(task,&LocalSuggestionTask::completed),progress(task,&LocalSuggestionTask::progress);panel->findChild<QComboBox*>("localSuggestionKind")->setCurrentIndex(3);
    QTest::mouseClick(panel->findChild<QPushButton*>("localSuggestionStart"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!task->running(),15000);QVERIFY(candidateHasResultC3(completed,progress));
    const auto options=completed.front()[0].toMap().value("options").toList();QVERIFY(!options.isEmpty());const auto change=options.front().toMap().value("changes").toList().front().toMap();QCOMPARE(change.value("pitch").toInt(),65);QCOMPARE(change.value("ref").toMap().value("id").toString(),QString("480:4:0"));
    auto selected=arrangementSuggestions(score).value("options").toList().front().toMap();auto performance=observer.arrangementRequest("arrangementOption",{{"optionId",selected["id"]},{"action","performance"}});QVERIFY(!performance.contains("error"));bool midi=false;for(auto v:performance.value("proposed").toList()){auto n=v.toMap();midi|=n.value("tick").toInt()==480&&n.value("track").toInt()==4&&n.value("pitch").toInt()==65;}QVERIFY(midi);
    QTRY_VERIFY(main->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer")->isVisible());
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::qWait(100);QVERIFY(main->grab().save(out.filePath("candidate-counterpoint.png")));
    const int before=score->undoStack()->getCurIdx();auto target=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(4))->upNote();QCOMPARE(target->pitch(),62);QTest::mouseClick(panel->findChild<QPushButton*>("arrangement-apply"),Qt::LeftButton);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+1);QCOMPARE(target->pitch(),65);
    const auto output=out.filePath("candidate-counterpoint-applied.mscz");QFileInfo info(output);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> reopened(main->readScore(output));QVERIFY(reopened);QCOMPARE(toChord(reopened->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(4))->upNote()->pitch(),65);main->cmd(getAction("undo"));QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(target->pitch(),62);
}
