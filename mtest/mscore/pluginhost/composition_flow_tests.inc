// GPL-2.0-or-later. Continue editing the same score across playback and A/B changes.
#include <QTextBrowser>
namespace {
QByteArray compositionContent(Score* score)
{
    EventMap rendered;score->renderMidi(&rendered,false,false,Ms::mscore->synthesizerState());
    auto result=QJsonDocument::fromJson(shareReport145(score)).toVariant().toMap();
    result.remove("groups");result.remove("vst");
    return QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact);
}
}

void TestPluginHost::continuousComposition_data()
{
    QTest::addColumn<QString>("original");QTest::newRow("independent-piano")<<QString();
    if(!qEnvironmentVariable("P2_GUI_SCORE").isEmpty())
        QTest::newRow("actual-piano-copy")<<qEnvironmentVariable("P2_GUI_SCORE");
}

void TestPluginHost::continuousComposition()
{
    QFETCH(QString,original);auto main=Ms::mscore;QVERIFY(main&&!Ms::seq);
    SourceTimerResolution timerResolution;
    const bool actual=!original.isEmpty();const QString label=actual?"actual":"independent";
    const auto path=_settings.filePath("composition-"+label+".mscz");QByteArray originalHash;
    MasterScore* score=nullptr;
    if(actual){
        QFile file(original);QVERIFY(file.open(QIODevice::ReadOnly));
        originalHash=QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);file.close();
        QVERIFY(QFile::copy(original,path));score=main->readScore(path);
    }else{
        QFile fixture(_settings.filePath("composition.mscx"));QVERIFY(fixture.open(QIODevice::WriteOnly));
        fixture.write(systemLockFixture138());fixture.close();score=main->readScore(fixture.fileName());
    }
    QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    const auto close=qScopeGuard([&]{releaseHostPanels();if(score){score->undoStack()->setClean();main->closeScore(score);}});
    main->resize(1280,900);main->show();main->activateWindow();
    auto arrange=main->findChild<QAction*>("kumo-workspace-arrange");QVERIFY(arrange);arrange->trigger();
    main->showPerformanceEditor(true);auto editor=main->performanceEditor();QVERIFY(editor);
    auto editorDock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(editorDock);
    editorDock->setFloating(false);main->resizeDocks({editorDock},{480},Qt::Vertical);QTest::qWait(100);
    auto first=score->firstMeasure()->nextMeasure();QVERIFY(first&&first->nextMeasure()&&first->nextMeasure()->nextMeasure());
    const int start=first->tick().ticks(),end=first->nextMeasure()->nextMeasure()->tick().ticks();
    int tick=-1,track=-1;
    for(int staff=1;staff<score->nstaves()&&tick<0;staff+=2)
        for(auto segment=first->first(SegmentType::ChordRest);segment&&segment->tick().ticks()<end&&tick<0;segment=segment->next1(SegmentType::ChordRest))
            for(int candidate=staff*VOICES;candidate<(staff+1)*VOICES&&tick<0;++candidate)
                if(auto element=segment->element(candidate))if(element->isChord()){
                    tick=segment->tick().ticks();track=candidate;
                }
    QVERIFY(tick>=start&&track>=4);
    const int staffTrack=track/VOICES*VOICES;
    QVariantList writingTracks;for(int voice=0;voice<VOICES;++voice)writingTracks.append(staffTrack+voice);
    auto note=[&]() -> Note* {auto element=score->findCR(Fraction::fromTicks(tick),track);
        return element&&element->isChord()?toChord(element)->downNote():nullptr;};
    QVERIFY(note());
    auto selected=note();score->select(selected);main->endCmd();editor->selectionChanged();
    main->currentScoreView()->adjustCanvasPosition(selected,false);
    const auto created=RegionalVariants::create(score,start,end,writingTracks,"Accompaniment");
    QVERIFY2(created.value("ok").toBool(),qPrintable(created.value("error").toString()));
    const auto group=created.value("group").toString(),a=created.value("variant").toString();
    showRegionalVariantPanel(group);QTest::qWait(80);
    auto setVelocity=[&](int value){
        auto target=note();if(!target)return false;score->select(target);main->endCmd();editor->selectionChanged();
        auto axis=editor->findChild<QComboBox*>("performanceAxis");if(!axis)return false;axis->setCurrentIndex(0);
        auto fixed=editor->findChild<QPushButton*>("performanceConvertAbsolute");
        auto number=editor->findChild<QDoubleSpinBox*>("performanceValue");
        auto apply=editor->findChild<QPushButton*>("performanceWriteValue");
        if(!fixed||!number||!apply||!apply->isEnabled())return false;
        QTest::mouseClick(fixed,Qt::LeftButton);number->setFocus();number->selectAll();
        QTest::keyClicks(number,QString::number(value));QTest::keyClick(number,Qt::Key_Tab);
        QTest::mouseClick(apply,Qt::LeftButton);main->endCmd();
        return target->veloType()==Note::ValueType::USER_VAL&&target->veloOffset()==value;
    };
    QVERIFY(setVelocity(74));
    auto pedal=new Pedal(score);pedal->setTrack(staffTrack);pedal->setTrack2(staffTrack);
    pedal->setTick(Fraction::fromTicks(start));pedal->setTick2(Fraction::fromTicks(end));
    score->startCmd();score->undoAddElement(pedal);score->endCmd();main->endCmd();
    const auto pedals=pedalRanges142(score);
    const auto bResult=RegionalVariants::duplicate(score,group,"B",true);
    QVERIFY2(bResult.value("ok").toBool(),qPrintable(bResult.value("error").toString()));
    const auto b=bResult.value("variant").toString();
    auto choose=[&](const QString& id){
        showRegionalVariantPanel(group);QTest::qWait(20);
        auto button=main->findChild<QPushButton*>("regionalQuick/"+id);
        if(!button||!button->isVisible())return false;
        QTest::mouseClick(button,Qt::LeftButton);main->endCmd();return true;
    };
    QVERIFY(note());score->select(note());main->endCmd();
    const int originalPitch=note()->pitch();getAction("pitch-up")->trigger();main->endCmd();
    QVERIFY(note()->pitch()!=originalPitch);QVERIFY(setVelocity(93));
    const auto bContent=compositionContent(score);const int bPitch=note()->pitch();
    QVERIFY(choose(a));QCOMPARE(note()->pitch(),originalPitch);QCOMPARE(note()->veloOffset(),74);
    const auto aContent=compositionContent(score);
    QVERIFY(choose(b));QCOMPARE(note()->pitch(),bPitch);QCOMPARE(compositionContent(score),bContent);
    QCOMPARE(pedalRanges142(score),pedals);

    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());QScopedValueRollback<bool> sequencing(noSeq,false);
    auto sequence=new Seq;Ms::seq=sequence;
    auto stop=qScopeGuard([&]{workflowContext()->finishPlaybackSession();sequence->stopWait();sequence->synchronizeStoppedGui();
        sequence->waitForStoppedRendering();sequence->setScoreView(nullptr);delete sequence;Ms::seq=nullptr;});
    auto driver=new SourcePlaybackDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());
    QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    score->setPlayPos(Fraction::fromTicks(start));getAction("loop")->setChecked(false);
    auto play=editor->findChild<QToolButton*>("performancePlay");QVERIFY(play);QTest::mouseClick(play,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);QTest::qWait(700);
    QTest::mouseClick(play,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying(),10000);
    sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();
    const auto audio=driver->metrics();QVERIFY(audio.value("peak").toDouble()>.000001);
    QVERIFY(!QWidget::mouseGrabber());QVERIFY(choose(a));QCOMPARE(compositionContent(score),aContent);
    auto daw=main->findChild<QAction*>("kumo-workspace-daw");QVERIFY(daw);daw->trigger();QTest::qWait(80);
    arrange->trigger();QTest::qWait(80);QVERIFY(choose(b));QCOMPARE(compositionContent(score),bContent);
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);
    for(int page:{1,2,3,0}){QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(page).center());QTest::qWait(30);}
    QCOMPARE(compositionContent(score),bContent);
    const auto saved=_settings.filePath("composition-"+label+"-saved.mscz");
    QVERIFY(main->saveAs(score,false,saved,"mscz"));QCOMPARE(compositionContent(score),bContent);
    sequence->setScoreView(nullptr);score->undoStack()->setClean();main->closeScore(score);score=nullptr;
    score=main->readScore(saved);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    QVERIFY(choose(a));QCOMPARE(compositionContent(score),aContent);
    QVERIFY(choose(b));QCOMPARE(compositionContent(score),bContent);QCOMPARE(pedalRanges142(score),pedals);
    const int beforeUndo=score->undoStack()->getCurIdx();QVERIFY(setVelocity(86));
    QCOMPARE(score->undoStack()->getCurIdx(),beforeUndo+1);getAction("undo")->trigger();main->endCmd();
    QCOMPARE(compositionContent(score),bContent);getAction("redo")->trigger();main->endCmd();QCOMPARE(note()->veloOffset(),86);
    QVERIFY(choose(a));QCOMPARE(compositionContent(score),aContent);QVERIFY(choose(b));QCOMPARE(note()->veloOffset(),86);
    const auto finalContent=compositionContent(score);QVERIFY(main->saveFile());
    const auto midi=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("composition-"+label+".mid");
    QVERIFY(main->saveAs(score,true,midi,"mid"));QCOMPARE(compositionContent(score),finalContent);
    QCOMPARE(score->masterScore()->fileInfo()->absoluteFilePath(),QFileInfo(saved).absoluteFilePath());
    const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    const auto archive=output.filePath("composition-"+label+".mscz");
    QFile sourceArchive(saved),retainedArchive(archive);QVERIFY(sourceArchive.open(QIODevice::ReadOnly));
    const auto archiveBytes=sourceArchive.readAll();QVERIFY(retainedArchive.open(QIODevice::WriteOnly));
    QCOMPARE(retainedArchive.write(archiveBytes),qint64(archiveBytes.size()));retainedArchive.close();
    QVERIFY(choose(a));const auto midiA=output.filePath("composition-"+label+"-A.mid");
    QVERIFY(main->saveAs(score,true,midiA,"mid"));QCOMPARE(compositionContent(score),aContent);
    QVERIFY(choose(b));QCOMPARE(compositionContent(score),finalContent);
    auto scoreButton=main->currentScoreView()->findChild<QToolButton*>("regionalScoreSwitch/"+group);
    QTRY_VERIFY_WITH_TIMEOUT(scoreButton&&scoreButton->text().contains("B"),500);
    QTRY_COMPARE_WITH_TIMEOUT(editor->findChild<QDoubleSpinBox*>("performanceValue")->value(),86.,500);
    auto panelDock=main->findChild<QDockWidget*>("kumoPanel/regionalVariantsDock");QVERIFY(panelDock);
    main->resizeDocks({panelDock},{303},Qt::Horizontal);
    QTest::qWait(80);
    QVERIFY(panelDock->width()<=307);
    QVERIFY(main->grab().save(output.filePath("composition-"+label+".png")));
    QVERIFY(panelDock->grab().save(output.filePath("composition-panel-"+label+".png")));
    QVariantMap report{{"label",label},{"sourceSha256",QString::fromLatin1(originalHash.toHex())},
        {"range",QVariantList{start,end}},{"tick",tick},{"track",track},{"originalPitch",originalPitch},{"bPitch",bPitch},
        {"velocityA",74},{"velocityB",86},{"pedals",pedals},{"audio",audio},{"saved",archive},{"midi",midi},{"midiA",midiA}};
    QFile result(output.filePath("composition-"+label+".json"));QVERIFY(result.open(QIODevice::WriteOnly));
    result.write(QJsonDocument::fromVariant(report).toJson());
    if(actual){QFile file(original);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256),originalHash);}
    auto scroll=main->findChild<QScrollArea*>("regionalScrollArea");QVERIFY(scroll);
    qInfo()<<"Writing panel width and horizontal overflow"<<scroll->width()<<scroll->horizontalScrollBar()->maximum();
    QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
    auto quick=main->findChild<QPushButton*>("regionalQuick/"+b);QVERIFY(quick);
    const auto quickBox=QRect(quick->mapTo(scroll->viewport(),QPoint()),quick->size());
    QVERIFY2(scroll->viewport()->rect().contains(quickBox),"The active writing switch must stay visible in the short sidebar");
}

void TestPluginHost::regionalSidebarLifecycle()
{
    auto main=Ms::mscore;const auto path=_settings.filePath("sidebar-lifecycle.mscx");
    QFile fixture(path);QVERIFY(fixture.open(QIODevice::WriteOnly));fixture.write(systemLockFixture138());fixture.close();
    auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    const auto close=qScopeGuard([&]{score->undoStack()->setClean();main->closeScore(score);});
    main->resize(1280,900);main->show();
    const auto a=RegionalVariants::create(score,0,3840,{4},"Sidebar");QVERIFY(a.value("ok").toBool());
    const auto group=a.value("group").toString();QCOMPARE(RegionalVariants::state(score).value("groups").toList().front().toMap().value("variants").toList().front().toMap().value("name").toString(),QString("A"));
    QVERIFY(RegionalVariants::duplicate(score,group,"B",true).value("ok").toBool());
    showRegionalVariantPanel(group);QTest::qWait(80);
    auto more=main->findChild<QToolButton*>("regionalMore");QVERIFY(more&&more->isVisible());
    auto help=main->findChild<QAction*>("regionalHelp");QVERIFY(help);
    bool opened=false;QTimer chooseHelp;chooseHelp.setSingleShot(true);
    QObject::connect(&chooseHelp,&QTimer::timeout,main,[&]{opened=more->menu()->isVisible();
        if(opened)QTest::mouseClick(more->menu(),Qt::LeftButton,Qt::NoModifier,more->menu()->actionGeometry(help).center());});
    chooseHelp.start(30);QTest::mouseClick(more,Qt::LeftButton);QVERIFY(opened);
    auto guide=main->findChild<QDialog*>("kumoUserGuide");QVERIFY(guide&&guide->isVisible());
    const auto closeGuide=qScopeGuard([&]{guide->deleteLater();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);});
    auto book=guide->findChild<QTextBrowser*>();QVERIFY(book);
    QVERIFY(book->source().path().endsWith("VARIANTS_070_GUIDE.md"));QTest::keyClick(guide,Qt::Key_Escape);
    QVERIFY(!guide->isVisible());
    auto tree=main->findChild<QTreeWidget*>("regionalTree");QVERIFY(tree&&tree->topLevelItemCount()==1);
    tree->setCurrentItem(tree->topLevelItem(0));
    auto remove=main->findChild<QPushButton*>("regionalDelete");QVERIFY(remove&&remove->isEnabled());
    bool confirmed=false;
    QTimer confirmDelete;confirmDelete.setSingleShot(true);
    QObject::connect(&confirmDelete,&QTimer::timeout,main,[&]{auto dialog=main->findChild<QDialog*>("regionalDeleteDialog");
        if(!dialog)return;auto button=dialog->findChild<QPushButton*>("regionalConfirmDelete");
        if(!button){dialog->reject();return;}confirmed=true;QTest::mouseClick(button,Qt::LeftButton);});
    confirmDelete.start(30);QTest::mouseClick(remove,Qt::LeftButton);QVERIFY(confirmed);
    QTRY_VERIFY(RegionalVariants::state(score).value("groups").toList().isEmpty());
    QTRY_VERIFY(!main->findChild<QLabel*>("regionalCurrent")->isVisible());
    getAction("undo")->trigger();main->endCmd();
    QTRY_COMPARE(RegionalVariants::state(score).value("groups").toList().size(),1);
    QTRY_VERIFY(main->findChild<QLabel*>("regionalCurrent")->isVisible());
    getAction("redo")->trigger();main->endCmd();QTRY_VERIFY(RegionalVariants::state(score).value("groups").toList().isEmpty());
    getAction("undo")->trigger();main->endCmd();QTRY_COMPARE(RegionalVariants::state(score).value("groups").toList().size(),1);
}
