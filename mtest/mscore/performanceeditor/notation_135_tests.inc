static bool notationDetail135(MuseScore* main,ScoreView* view,const QString& path) {
    QVector<QPair<QCheckBox*,bool>> saved;
    for(auto box:main->performanceEditor()->findChildren<QCheckBox*>())if(box->text()==QString::fromUtf8("音旁手柄")||box->objectName()=="performanceScoreBand"){
        saved.append({box,box->isChecked()});box->setChecked(false);
    }
    auto restore=qScopeGuard([&]{for(const auto& state:saved)state.first->setChecked(state.second);});
    view->score()->deselectAll();main->endCmd();QTest::qWait(40);
    const auto rect=view->toPhysical(view->score()->firstMeasure()->canvasBoundingRect()).adjusted(-16,-38,16,20).intersected(view->rect());
    return !rect.isEmpty()&&view->grab(rect).save(path);
}
void TestPerformanceEditor::notationInputActions135_data() {
    QTest::addColumn<bool>("changeQuarter");
    QTest::newRow("dotted-quarter-change-eighth")<<true;
    QTest::newRow("three-three-two")<<false;
}
void TestPerformanceEditor::notationInputActions135() {
    QFETCH(bool,changeQuarter);auto main=Ms::mscore;QVERIFY(main);
    auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/inputrhythm/blank.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->showPerformanceEditor(false);main->resize(1120,780);main->show();
    auto view=main->currentScoreView();view->setBackground(QColor(228,228,228));view->setLogicalZoom(ZoomIndex::ZOOM_FREE,.8);score->setLayoutAll();score->doLayout();QTest::qWait(60);
    const bool oldEnabled=InputRhythm::enabled(),oldBeams=InputNotation::tiedBeams(),oldStems=InputNotation::coherentStems();
    auto restore=qScopeGuard([&]{view->changeState(ViewState::NORMAL);InputRhythm::setEnabled(oldEnabled);InputNotation::setTiedBeams(oldBeams);InputNotation::setCoherentStems(oldStems);});
    InputRhythm::setEnabled(true);InputNotation::setTiedBeams(false);InputNotation::setCoherentStems(true);
    auto chord=[&](Score* target,int tick){auto cr=target->findCR(Fraction::fromTicks(tick),0);return cr&&cr->isChord()?toChord(cr):nullptr;};
    auto action=[&](const char* id){auto a=getAction(id);QVERIFY(a);a->trigger();QApplication::processEvents();};
    auto duration=[&](const char* id,bool dotted){action(id);if(bool(score->inputState().duration().dots())!=dotted)action("pad-dot");};
    const auto name=changeQuarter?"dotted-quarter":"three-three-two";const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path());
    auto rest=score->firstSegment(SegmentType::ChordRest)->element(0);QVERIFY(rest);view->adjustCanvasPosition(rest,false);QTest::qWait(30);
    const auto point=view->toPhysical(rest->canvasBoundingRect()).center();QVERIFY(view->rect().contains(point));QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,point);view->setFocus();
    action("note-input");QVERIFY(score->inputState().noteEntryMode());
    if(changeQuarter) {
        duration("pad-note-4",true);action("note-c");duration("pad-note-8",false);action("note-d");
        QVERIFY(chord(score,720));QCOMPARE(chord(score,720)->actualTicks().ticks(),240);action("note-input");QVERIFY(!score->inputState().noteEntryMode());
        auto selected=chord(score,720)->upNote();view->adjustCanvasPosition(selected,false);QTest::qWait(20);QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,view->toPhysical(selected->canvasBoundingRect()).center());QVERIFY(score->selection().element()==selected);
        const int before=score->undoStack()->getCurIdx();action("pad-note-4");
        QVERIFY(chord(score,960));QCOMPARE(chord(score,720)->actualTicks().ticks(),240);QCOMPARE(chord(score,960)->actualTicks().ticks(),240);
        QVERIFY(chord(score,720)->upNote()->tieFor());QCOMPARE(chord(score,720)->upNote()->tieFor()->endNote(),chord(score,960)->upNote());QCOMPARE(score->undoStack()->getCurIdx(),before+1);
        score->undoRedo(true,&ed);QVERIFY(!chord(score,720)->upNote()->tieFor());score->undoRedo(false,&ed);QVERIFY(chord(score,720)->upNote()->tieFor());
        const auto sound=freeSlurMidi(score);InputNotation::setTiedBeams(true);score->setLayoutAll();score->doLayout();QCOMPARE(chord(score,720)->beam(),chord(score,960)->beam());QVERIFY(chord(score,720)->beam());QCOMPARE(freeSlurMidi(score),sound);
        QTest::qWait(30);QVERIFY(view->grab().save(output+QString("/notation-%1-auto-135.png").arg(name)));
        QVERIFY(notationDetail135(main,view,output+QString("/notation-%1-detail-135.png").arg(name)));
        score->startCmd();chord(score,960)->undoChangeProperty(Pid::BEAM_MODE,int(Beam::Mode::BEGIN));score->endCmd();main->endCmd();QVERIFY(!chord(score,720)->beam()||chord(score,720)->beam()!=chord(score,960)->beam());QCOMPARE(freeSlurMidi(score),sound);
    } else {
        duration("pad-note-8",true);action("note-g");duration("pad-note-8",true);action("note-b");duration("pad-note-8",false);action("note-d");action("note-input");QVERIFY(!score->inputState().noteEntryMode());
        QVERIFY(chord(score,0)&&chord(score,360)&&chord(score,480)&&chord(score,720));QCOMPARE(chord(score,360)->up(),chord(score,480)->up());QVERIFY(chord(score,360)->upNote()->tieFor());QCOMPARE(chord(score,360)->upNote()->tieFor()->endNote(),chord(score,480)->upNote());
        QTest::qWait(30);QVERIFY(view->grab().save(output+QString("/notation-%1-auto-135.png").arg(name)));
        QVERIFY(notationDetail135(main,view,output+QString("/notation-%1-detail-135.png").arg(name)));
        const auto sound=freeSlurMidi(score);score->startCmd();chord(score,360)->undoChangeProperty(Pid::STEM_DIRECTION,int(Direction::UP));score->endCmd();main->endCmd();QVERIFY(chord(score,360)->up());QCOMPARE(freeSlurMidi(score),sound);
    }
    view->changeState(ViewState::NORMAL);score->setLayoutAll();score->doLayout();QTest::qWait(60);
    QVERIFY(view->grab().save(output+QString("/notation-%1-135.png").arg(name)));const auto path=_directory.filePath(QString("notation-%1-135.mscz").arg(name));QFileInfo file(path);QVERIFY(score->saveCompressedFile(file,false,false));
    std::unique_ptr<MasterScore> loaded(main->readScore(path));QVERIFY(loaded);QCOMPARE(freeSlurMidi(loaded.get()),freeSlurMidi(score));
    if(changeQuarter){QVERIFY(chord(loaded.get(),720)->upNote()->tieFor());QCOMPARE(int(chord(loaded.get(),960)->beamMode()),int(Beam::Mode::BEGIN));}
    else {QVERIFY(chord(loaded.get(),360)->upNote()->tieFor());QCOMPARE(int(chord(loaded.get(),360)->stemDirection()),int(Direction::UP));}
}

void TestPerformanceEditor::ritRangeLayout135_data() {
    QTest::addColumn<int>("from");QTest::addColumn<int>("until");QTest::addColumn<int>("breakAfter");QTest::addColumn<bool>("restOnly");
    QTest::newRow("single")<<0<<1920<<0<<false;
    QTest::newRow("line-end")<<0<<3840<<2<<false;
    QTest::newRow("cross-line")<<0<<5760<<1<<false;
    QTest::newRow("middle-to-line-end")<<480<<3840<<2<<false;
    QTest::newRow("last-quarter")<<1440<<1920<<1<<false;
    QTest::newRow("all-rest-score-end")<<0<<5760<<1<<true;
}
void TestPerformanceEditor::ritRangeLayout135() {
    QFETCH(int,from);QFETCH(int,until);QFETCH(int,breakAfter);QFETCH(bool,restOnly);auto main=Ms::mscore;QVERIFY(main);
    auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/inputrhythm/blank.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->showPerformanceEditor(false);main->resize(1120,800);main->show();
    if(!restOnly){score->startCmd();for(auto m=score->firstMeasure();m;m=m->nextMeasure()){auto segment=m->first(SegmentType::ChordRest);for(int i=0;i<4;++i){score->setNoteRest(segment,0,NoteVal(60+i*2),Fraction(1,4));segment=score->tick2segment(m->tick()+Fraction::fromTicks((i+1)*480),false,SegmentType::ChordRest);}}score->endCmd();}
    if(breakAfter){auto m=score->firstMeasure();for(int i=1;i<breakAfter;++i)m=m->nextMeasure();auto br=new LayoutBreak(score);br->setLayoutBreakType(LayoutBreak::Type::LINE);br->setParent(m);score->startCmd();score->undoAddElement(br);score->endCmd();}
    QVERIFY(ParameterEdit::tempoCurve(score,from,until,nullptr,false));auto curves=TempoExpression::curves(score);QCOMPARE(curves.size(),1);auto line=curves.front();score->setLayoutAll();score->doLayout();
    TempoText tempoStyle(score);qInfo()<<"Rit / a tempo font sizes"<<line->getProperty(Pid::BEGIN_FONT_SIZE)<<tempoStyle.getProperty(Pid::FONT_SIZE);
    QCOMPARE(line->getProperty(Pid::BEGIN_FONT_FACE),tempoStyle.getProperty(Pid::FONT_FACE));QCOMPARE(line->getProperty(Pid::BEGIN_FONT_SIZE),tempoStyle.getProperty(Pid::FONT_SIZE));
    line->styleChanged();QCOMPARE(line->getProperty(Pid::BEGIN_FONT_SIZE),tempoStyle.getProperty(Pid::FONT_SIZE));QCOMPARE(line->getProperty(Pid::CONTINUE_FONT_SIZE),tempoStyle.getProperty(Pid::FONT_SIZE));
    auto ending=score->tick2measure(Fraction::fromTicks(until)-Fraction::eps());QVERIFY(ending);
    auto check=[&]{QVERIFY(!line->spannerSegments().empty());auto last=line->backSegment();QVERIFY(last->pos2().x()>0);QVERIFY(std::isfinite(last->canvasPos().x()));
        QCOMPARE(last->system(),ending->system());QVERIFY(qAbs(last->canvasPos().x()+last->pos2().x()-ending->canvasPos().x()-ending->width())<score->spatium());
        if(breakAfter&&ending->system()!=score->firstMeasure()->system())QVERIFY(line->spannerSegments().size()>1);
    };check();QVERIFY(!line->spannerSegments().empty());
    auto view=main->currentScoreView();view->setBackground(QColor(228,228,228));view->setLogicalZoom(ZoomIndex::ZOOM_FREE,.7);view->adjustCanvasPosition(ending,false);QTest::qWait(60);
    view->startEdit(line->backSegment(),Grip::END);view->updateGrips();auto& edit=view->getEditData();QVERIFY(edit.grips>=2);const auto handle=view->matrix().map(edit.grip[1].center()).toPoint();QVERIFY(view->rect().contains(handle));
    const auto before=score->undoStack()->getCurIdx();const QPoint moved=handle+QPoint(-18,-6);QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,handle);
    QMouseEvent motion(QEvent::MouseMove,moved,moved,view->mapToGlobal(moved),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&motion);QApplication::sendEvent(view,&motion);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,moved);QCOMPARE(score->undoStack()->getCurIdx(),before+1);
    view->changeState(ViewState::NORMAL);score->undoRedo(true,&ed);check();score->undoRedo(false,&ed);QVERIFY(!line->spannerSegments().empty());score->undoRedo(true,&ed);
    score->deselectAll();main->endCmd();QTest::qWait(50);const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path());QVERIFY(view->grab().save(output+QString("/rit-%1-135.png").arg(QTest::currentDataTag())));
    const auto path=_directory.filePath(QString("rit-%1-135.mscz").arg(QTest::currentDataTag()));QFileInfo file(path);QVERIFY(score->saveCompressedFile(file,false,false));std::unique_ptr<MasterScore> loaded(main->readScore(path));QVERIFY(loaded);
    auto restored=TempoExpression::curves(loaded.get());QCOMPARE(restored.size(),1);QCOMPARE(restored.front()->tick().ticks(),from);QCOMPARE(restored.front()->tick2().ticks(),until);QVERIFY(!restored.front()->spannerSegments().empty());QCOMPARE(restored.front()->getProperty(Pid::BEGIN_FONT_SIZE),tempoStyle.getProperty(Pid::FONT_SIZE));QCOMPARE(freeSlurMidi(loaded.get()),freeSlurMidi(score));
}
