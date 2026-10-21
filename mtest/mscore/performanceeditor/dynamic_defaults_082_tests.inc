// GPL-2.0-or-later. Actual mark values, MIDI output, settings controls and persistence.
static Dynamic* addDynamic082(MasterScore* score, int tick, int track, Dynamic::Type type, int velocity=-1) {
    auto segment=score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);
    if(!segment)return nullptr;
    auto mark=new Dynamic(score);mark->setParent(segment);mark->setTrack(track);
    mark->setDynamicType(QString::fromLatin1(dynList[int(type)].tag));mark->setDynRange(Dynamic::Range::STAFF);mark->setVelocity(velocity);
    score->undoAddElement(mark);return mark;
}
static int renderedVelocity082(MasterScore* score, Note* note) {
    EventMap events;SynthesizerState state;score->renderMidi(&events,false,false,state);
    for(auto entry:events)if(entry.second.type()==ME_NOTEON&&entry.second.velo()>0&&entry.second.note()==note)return entry.second.velo();
    return -1;
}
void dynamicDefaultsRenderingAndRoundTrip() {
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);
    auto score=fixture();QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    score->startCmd();
    auto mp=addDynamic082(score,0,0,Dynamic::Type::MP),p=addDynamic082(score,0,4,Dynamic::Type::P);
    auto manual=addDynamic082(score,960,0,Dynamic::Type::MP,43);QVERIFY(mp&&p&&manual);
    auto upper=noteAt(score),lower=noteAt(score,0,4);
    const auto notes=upper->chord()->notes();QCOMPARE(notes.size(),3);
    upper->undoChangeProperty(Pid::VELO_TYPE,int(Note::ValueType::OFFSET_VAL));upper->undoChangeProperty(Pid::VELO_OFFSET,10);
    notes[1]->undoChangeProperty(Pid::VELO_TYPE,int(Note::ValueType::USER_VAL));notes[1]->undoChangeProperty(Pid::VELO_OFFSET,99);
    score->endCmd();
    QCOMPARE(mp->velocity(),64);QCOMPARE(p->velocity(),49);
    QCOMPARE(renderedVelocity082(score,upper),NoteVelocity::effective(64,Note::ValueType::OFFSET_VAL,10));
    QCOMPARE(renderedVelocity082(score,lower),49);
    const int before=score->undoStack()->getCurIdx();
    score->startCmd();score->undoChangeStyleVal(Sid::dynamicsPlaybackVelocities,QString("{\"mp\":58,\"p\":37}"));score->setPlaylistDirty();score->endCmd();
    QCOMPARE(score->undoStack()->getCurIdx(),before+1);
    QCOMPARE(mp->velocity(),58);QCOMPARE(p->velocity(),37);QCOMPARE(manual->velocity(),43);
    QCOMPARE(renderedVelocity082(score,upper),NoteVelocity::effective(58,Note::ValueType::OFFSET_VAL,10));
    QCOMPARE(renderedVelocity082(score,notes[1]),99);QCOMPARE(renderedVelocity082(score,lower),37);
    QCOMPARE(NoteVelocity::referenceBase(upper),58);QCOMPARE(NoteVelocity::referenceBase(lower),37);
    EditData edit;score->undoRedo(true,&edit);QCOMPARE(mp->velocity(),64);QCOMPARE(renderedVelocity082(score,lower),49);
    score->undoRedo(false,&edit);QCOMPARE(mp->velocity(),58);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    const auto path=output.filePath("dynamics-defaults-082.mscz");
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);observer.workflowInfo();
    const auto saved=observer.copyWorkflowScore(path,observer.analysisRevision(),true);QVERIFY2(!saved.contains("error"),qPrintable(saved.value("error").toString()));
    QFile midi(output.filePath("dynamics-defaults-082.mid"));QVERIFY(midi.open(QIODevice::WriteOnly));
    ExportMidi exporter(score);QVERIFY(exporter.write(&midi,true,true,main->synthesizerState()));midi.close();
    std::unique_ptr<MasterScore> reopened(main->readScore(path));QVERIFY(reopened);
    QCOMPARE(renderedVelocity082(reopened.get(),noteAt(reopened.get())),NoteVelocity::effective(58,Note::ValueType::OFFSET_VAL,10));
    QCOMPARE(renderedVelocity082(reopened.get(),noteAt(reopened.get(),0,4)),37);
    Dynamic* restored=nullptr;
    for(auto element:reopened->firstSegment(SegmentType::ChordRest)->annotations())if(element->isDynamic()&&element->track()==0)restored=toDynamic(element);
    QVERIFY(restored);QCOMPARE(restored->velocity(),58);
    reopened->setStyleValue(Sid::dynamicsPlaybackVelocities,QString("{\"mp\":72}"));QCOMPARE(restored->velocity(),72);
    // A pasted mark keeps its actual value when the destination uses different defaults.
    std::unique_ptr<Element> standalone(writeReadElement(mp));QVERIFY(standalone&&standalone->isDynamic());QCOMPARE(toDynamic(standalone.get())->velocity(),58);
    MStyle invalid;invalid.set(Sid::dynamicsPlaybackVelocities,QString("{\"mp\":128,\"p\":0,\"mf\":\"loud\"}"));
    QCOMPARE(invalid.dynamicsVelocity(7,64),64);QCOMPARE(invalid.dynamicsVelocity(6,49),49);QCOMPARE(invalid.dynamicsVelocity(8,80),80);
}
void dynamicDefaultsSettingsMouseAndUndo() {
    expressionHost080(_directory.path());auto main=Ms::mscore;auto score=fixture();QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));score->startCmd();auto mp=addDynamic082(score,0,0,Dynamic::Type::MP,64);score->endCmd();QVERIFY(mp);
    expressionShowMain080(main);main->showPerformanceEditor(true);auto editor=main->performanceEditor();QVERIFY(editor);
    auto button=editor->findChild<QToolButton*>("performanceOperationSettings");QVERIFY(button&&button->isVisible());
    QTest::mouseClick(button,Qt::LeftButton);
    auto dialog=main->findChild<QDialog*>("performanceSettings");QTRY_VERIFY(dialog&&dialog->isVisible());
    QVERIFY(!dialog->isModal());
    auto description=dialog->findChild<QLabel*>("performanceSettingsDescription");QVERIFY(description);
    QVERIFY(qAbs(description->palette().color(QPalette::WindowText).lightnessF()-dialog->palette().color(QPalette::Window).lightnessF())>.4);
    auto tabs=dialog->findChild<QTabWidget*>("performanceSettingsTabs");QVERIFY(tabs);tabs->setCurrentIndex(1);
    auto value=dialog->findChild<QSpinBox*>("dynamicDefault_mp");QVERIFY(value&&value->isVisible());QVERIFY(!value->visibleRegion().isEmpty());
    value->setFocus();QTest::keyClick(value,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(value,"57");
    const int before=score->undoStack()->getCurIdx();
    auto buttons=dialog->findChild<QDialogButtonBox*>();QVERIFY(buttons);QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);
    QTRY_COMPARE(mp->velocity(),57);QCOMPARE(score->undoStack()->getCurIdx(),before+1);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    QTest::mouseClick(button,Qt::LeftButton);dialog=main->findChild<QDialog*>("performanceSettings");QTRY_VERIFY(dialog&&dialog->isVisible());
    tabs=dialog->findChild<QTabWidget*>("performanceSettingsTabs");tabs->setCurrentIndex(1);value=dialog->findChild<QSpinBox*>("dynamicDefault_mp");QCOMPARE(value->value(),57);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(dialog->grab().save(output.filePath("dynamics-settings-082.png")));
    dialog->resize(380,360);QTest::qWait(100);
    auto grid=dialog->findChild<QTableWidget*>("performanceDynamicsDefaults");QVERIFY(grid);
    QVERIFY(!grid->verticalScrollBar()->isVisible());
    QVERIFY(dialog->grab().save(output.filePath("dynamics-settings-narrow-082.png")));
    auto reset=dialog->findChild<QPushButton*>("performanceDynamicsReset");QVERIFY(reset);auto form=dialog->findChild<QScrollArea*>("performanceDynamicsScroll");QVERIFY(form);form->ensureWidgetVisible(reset);QVERIFY(!reset->visibleRegion().isEmpty());QTest::mouseClick(reset,Qt::LeftButton);QCOMPARE(value->value(),64);
    buttons=dialog->findChild<QDialogButtonBox*>();QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel),Qt::LeftButton);QCOMPARE(mp->velocity(),57);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    EditData edit;score->undoRedo(true,&edit);QCOMPARE(mp->velocity(),64);score->undoRedo(false,&edit);QCOMPARE(mp->velocity(),57);
    QTest::mouseClick(button,Qt::LeftButton);dialog=main->findChild<QDialog*>("performanceSettings");QTRY_VERIFY(dialog&&dialog->isVisible());
    tabs=dialog->findChild<QTabWidget*>("performanceSettingsTabs");tabs->setCurrentIndex(1);reset=dialog->findChild<QPushButton*>("performanceDynamicsReset");QTest::mouseClick(reset,Qt::LeftButton);
    buttons=dialog->findChild<QDialogButtonBox*>();QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);QTRY_COMPARE(mp->velocity(),64);
    QVERIFY(score->styleSt(Sid::dynamicsPlaybackVelocities).isEmpty());
}

void dynamicDefaultsRepeatedPlaybackAndUndo() {
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    auto score=expressionScore080(main,_directory.filePath("dynamic-playback-082.mscx"));QVERIFY(score);
    score->startCmd();auto mark=addDynamic082(score,0,0,Dynamic::Type::MP);score->endCmd();QVERIFY(mark);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;
    synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    Ms::seq=new PerformanceAuditionSeq;auto sequence=Ms::seq;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* value){value->stopWait();value->waitForStoppedRendering();value->setScoreView(nullptr);delete value;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);
    QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    const auto firstVelocity=[&](){
        recording->onVelocities.clear();score->setPlayPos(Fraction());sequence->start();QApplication::processEvents();
        for(int i=0;i<12&&recording->onVelocities.isEmpty();++i){driver->pulse();QTest::qWait(5);}
        const int value=recording->onVelocities.isEmpty()?-1:recording->onVelocities.front();
        sequence->stopWait();sequence->waitForStoppedRendering();sequence->synchronizeStoppedGui();
        return value;
    };
    QCOMPARE(firstVelocity(),64);
    score->startCmd();score->undoChangeStyleVal(Sid::dynamicsPlaybackVelocities,QString("{\"mp\":58}"));score->setPlaylistDirty();score->endCmd();
    QCOMPARE(firstVelocity(),58);
    EditData edit;score->undoRedo(true,&edit);QCOMPARE(mark->velocity(),64);QCOMPARE(firstVelocity(),64);
    score->undoRedo(false,&edit);QCOMPARE(mark->velocity(),58);QCOMPARE(firstVelocity(),58);
}

void dynamicDefaultsExistingChoiceAndFutureMarks() {
    expressionHost080(_directory.path());auto main=Ms::mscore;
    QFile original(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(original.open(QIODevice::ReadOnly));
    QString xml=QString::fromUtf8(original.readAll());
    const auto mark=[](int value){return QString("<Dynamic><subtype>mp</subtype><velocity>%1</velocity><dynType>0</dynType></Dynamic>").arg(value);};
    int first=xml.indexOf("<Rest><durationType>quarter");QVERIFY(first>=0);xml.insert(first,mark(64));
    int later=xml.indexOf("<Rest><durationType>half");QVERIFY(later>=0);xml.insert(later,mark(43));
    QFile legacyFile(_directory.filePath("legacy-dynamics-082.mscx"));QVERIFY(legacyFile.open(QIODevice::WriteOnly));legacyFile.write(xml.toUtf8());legacyFile.close();
    auto score=main->readScore(legacyFile.fileName());QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    Dynamic *legacy=nullptr,*manual=nullptr;
    for(auto element:score->firstSegment(SegmentType::ChordRest)->annotations())if(element->isDynamic())legacy=toDynamic(element);
    for(auto element:score->tick2segment(Fraction::fromTicks(960),false,SegmentType::ChordRest)->annotations())if(element->isDynamic())manual=toDynamic(element);
    QVERIFY(legacy&&manual);QCOMPARE(legacy->velocity(),64);QCOMPARE(manual->velocity(),43);
    expressionShowMain080(main);main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto button=editor->findChild<QToolButton*>("performanceOperationSettings");QVERIFY(button);
    const auto open=[&](int tab){
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTest::mouseClick(button,Qt::LeftButton);
        auto dialog=main->findChild<QDialog*>("performanceSettings");
        if(dialog)dialog->findChild<QTabWidget*>("performanceSettingsTabs")->setCurrentIndex(tab);
        return dialog;
    };
    auto dialog=open(1);QVERIFY(dialog&&dialog->isVisible());
    auto existing=dialog->findChild<QCheckBox*>("performanceDynamicsIncludeExisting");QVERIFY(existing);existing->setChecked(false);
    auto value=dialog->findChild<QSpinBox*>("dynamicDefault_mp");QVERIFY(value);value->setValue(57);
    auto buttons=dialog->findChild<QDialogButtonBox*>();QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);
    QCOMPARE(legacy->velocity(),64);QCOMPARE(manual->velocity(),43);
    score->startCmd();auto future=addDynamic082(score,480,0,Dynamic::Type::MP);score->endCmd();QVERIFY(future);QCOMPARE(future->velocity(),57);
    dialog=open(0);QVERIFY(dialog&&dialog->isVisible());existing=dialog->findChild<QCheckBox*>("performanceDynamicsIncludeExisting");QVERIFY(!existing->isChecked());
    buttons=dialog->findChild<QDialogButtonBox*>();QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);QCOMPARE(legacy->velocity(),64);
    dialog=open(1);QVERIFY(dialog&&dialog->isVisible());existing=dialog->findChild<QCheckBox*>("performanceDynamicsIncludeExisting");existing->setChecked(true);
    const int before=score->undoStack()->getCurIdx();buttons=dialog->findChild<QDialogButtonBox*>();QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);
    QCOMPARE(legacy->velocity(),57);QCOMPARE(manual->velocity(),43);QCOMPARE(future->velocity(),57);QCOMPARE(score->undoStack()->getCurIdx(),before+1);
    EditData edit;score->undoRedo(true,&edit);QCOMPARE(legacy->velocity(),64);QCOMPARE(manual->velocity(),43);QCOMPARE(future->velocity(),57);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
}

void dynamicDefaultsThemeAndFloating() {
    expressionHost080(_directory.path());auto main=Ms::mscore;auto score=fixture();QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));expressionShowMain080(main);main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();QVERIFY(editor);
    QDockWidget* dock=nullptr;for(auto owner=editor->parentWidget();owner;owner=owner->parentWidget())if(auto candidate=qobject_cast<QDockWidget*>(owner)){dock=candidate;break;}
    QVERIFY(dock);dock->setFloating(true);dock->resize(1000,650);dock->show();QTest::qWait(120);
    const auto original=preferences.preferredGlobalStyle();
    std::unique_ptr<int,std::function<void(int*)>> restore(new int,[=](int* data){preferences.setTemporaryPreference(PREF_UI_APP_GLOBALSTYLE,QVariant::fromValue(original));MuseScore::updateUiStyleAndTheme();dock->setFloating(false);delete data;});
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    for(auto theme:{MuseScorePreferredStyleType::LIGHT_FUSION,MuseScorePreferredStyleType::DARK_FUSION}){
        preferences.setTemporaryPreference(PREF_UI_APP_GLOBALSTYLE,QVariant::fromValue(theme));MuseScore::updateUiStyleAndTheme();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        auto button=editor->findChild<QToolButton*>("performanceOperationSettings");QVERIFY(button&&button->isVisible());
        QTest::mouseClick(button,Qt::LeftButton);auto dialog=main->findChild<QDialog*>("performanceSettings");QTRY_VERIFY(dialog&&dialog->isVisible());
        auto tabs=dialog->findChild<QTabWidget*>("performanceSettingsTabs");QVERIFY(tabs);tabs->setCurrentIndex(1);QTest::qWait(80);
        auto description=dialog->findChild<QLabel*>("performanceSettingsDescription");QVERIFY(description);
        const auto background=dialog->palette().color(QPalette::Window),text=description->palette().color(QPalette::WindowText);
        QVERIFY(qAbs(text.lightnessF()-background.lightnessF())>.4);
        QCOMPARE(background.lightnessF()<.5,theme==MuseScorePreferredStyleType::DARK_FUSION);
        QTRY_COMPARE(QGuiApplication::focusWindow(),dialog->windowHandle());
        auto value=dialog->findChild<QSpinBox*>("dynamicDefault_mp");QVERIFY(value&&!value->visibleRegion().isEmpty());
        QTest::mouseClick(value,Qt::LeftButton);QTest::keyClick(value,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(value,"55");QCOMPARE(value->value(),55);
        auto extra=dialog->findChild<QCheckBox*>("performanceDynamicsShowAll");QVERIFY(extra);
        auto form=dialog->findChild<QScrollArea*>("performanceDynamicsScroll");QVERIFY(form);form->ensureWidgetVisible(extra);
        QTest::mouseClick(extra,Qt::LeftButton,Qt::NoModifier,QPoint(8,extra->height()/2));
        auto table=dialog->findChild<QTableWidget*>("performanceDynamicsDefaults");QVERIFY(table);QVERIFY(!table->isRowHidden(13));
        QTest::mouseClick(extra,Qt::LeftButton,Qt::NoModifier,QPoint(8,extra->height()/2));QVERIFY(table->isRowHidden(13));
        value->setValue(58);dialog->findChild<QSpinBox*>("dynamicDefault_p")->setValue(37);
        form->ensureWidgetVisible(description);
        QVERIFY(dialog->grab().save(output.filePath(theme==MuseScorePreferredStyleType::DARK_FUSION?"dynamics-settings-dark-082.png":"dynamics-settings-light-082.png")));
        auto buttons=dialog->findChild<QDialogButtonBox*>();QVERIFY(buttons);QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel),Qt::LeftButton);
        QVERIFY(score->styleSt(Sid::dynamicsPlaybackVelocities).isEmpty());
    }
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
}

void dynamicDefaultsOldPaletteAndDrag() {
    expressionHost080(_directory.path());auto main=Ms::mscore;auto score=fixture();QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));score->setStyleValue(Sid::dynamicsPlaybackVelocities,QString("{\"mp\":58}"));
    std::unique_ptr<MasterScore> paletteScore(fixture());QVERIFY(paletteScore);paletteScore->setPaletteMode(true);
    Dynamic oldPalette(paletteScore.get());
    XmlReader saved(QByteArray("<Dynamic><subtype>mp</subtype><velocity>64</velocity></Dynamic>"));QVERIFY(saved.readNextStartElement());oldPalette.read(saved);
    auto note=noteAt(score);score->select(note);main->selectionChanged(score->selection().state());
    QVERIFY(Palette::applyPaletteElement(&oldPalette));
    Dynamic* added=nullptr;for(auto element:note->chord()->segment()->annotations())if(element->isDynamic())added=toDynamic(element);
    QVERIFY(added);QCOMPARE(added->velocity(),58);
    const auto bytes=oldPalette.mimeData(QPointF());XmlReader drag(bytes);
    while(!drag.atEnd() && !(drag.isStartElement() && drag.name()=="Dynamic"))drag.readNext();
    QVERIFY(drag.isStartElement());Dynamic dragged(score);dragged.read(drag);QCOMPARE(dragged.velocity(),58);
    Dynamic custom(paletteScore.get());XmlReader fixed(QByteArray("<Dynamic><subtype>mp</subtype><velocity>43</velocity></Dynamic>"));QVERIFY(fixed.readNextStartElement());custom.read(fixed);
    score->select(note);main->selectionChanged(score->selection().state());QVERIFY(Palette::applyPaletteElement(&custom));
    bool found=false;for(auto element:note->chord()->segment()->annotations())if(element->isDynamic()&&toDynamic(element)->velocity()==43)found=true;QVERIFY(found);
    const auto copiedBytes=added->mimeData(QPointF());XmlReader copied(copiedBytes);
    while(!copied.atEnd() && !(copied.isStartElement() && copied.name()=="Dynamic"))copied.readNext();
    QVERIFY(copied.isStartElement());paletteScore->setPaletteMode(false);Dynamic pasted(paletteScore.get());pasted.read(copied);QCOMPARE(pasted.velocity(),58);
}
