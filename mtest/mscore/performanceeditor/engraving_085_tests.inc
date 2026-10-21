// Keep the visible point while locking or smoothing mixed-side slurs. GPL v2.
#include "mscore/shortcut.h"
static Slur* mixedSlur085(MasterScore* score, bool crossStaff, bool automatic, int side=-1)
{
    auto start=toChord(score->firstSegment(SegmentType::ChordRest)->element(crossStaff?4:0));
    auto end=toChord(score->tick2segment(Fraction::fromTicks(1440),false,SegmentType::ChordRest)->element(0));
    auto line=new Slur(score);
    line->setTrack(start->track());line->setTrack2(end->track());
    line->setTick(start->tick());line->setTicks(end->tick()-start->tick());
    line->setStartElement(start);line->setEndElement(end);
    line->setProperty(Pid::FREE_SLUR_MODE,true);
    line->setProperty(Pid::FREE_SLUR_AUTO,automatic);
    line->setProperty(Pid::FREE_SLUR_START_SIDE,side);
    line->setProperty(Pid::FREE_SLUR_END_SIDE,-side);
    line->setProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode({{.5,{}, {},false,side,-side}}));
    score->startCmd();score->undoAddElement(line);score->endCmd();score->doLayout();
    return line;
}

void TestPerformanceEditor::freeSlurNodeFreezeKeepsShape()
{
    for(bool crossStaff:{false,true})for(bool automatic:{false,true})for(int side:{-1,1}) {
        std::unique_ptr<MasterScore> score(readScore(crossStaff?
            "mscore/performanceeditor/repair054-crossstaff.mscx":"mscore/performanceeditor/freeslur.mscx"));
        QVERIFY(score);auto line=mixedSlur085(score.get(),crossStaff,automatic,side);
        auto segment=line->frontSegment();QVERIFY(segment);QVERIFY(segment->gripsPositions().size()>6);
        const auto before=segment->gripsPositions()[6]-segment->pagePos();
        FreeSlurNode shown;QVERIFY(segment->displayedFreeNode(0,&shown));shown.locked=true;
        score->startCmd();line->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode({shown}));score->endCmd();
        const auto after=segment->gripsPositions()[6]-segment->pagePos();
        const double shift=QLineF(before,after).length()/score->spatium();
        qInfo("lock visible node: cross-staff=%d auto=%d side=%d moved=%.6f spaces",crossStaff,automatic,side,shift);
        QVERIFY2(shift<.001,"Locking the displayed point must keep its visible position");
        score->undoRedo(true,&ed);
        QVERIFY(QLineF(before,segment->gripsPositions()[6]-segment->pagePos()).length()<.001*score->spatium());
    }
}

void TestPerformanceEditor::freeSlurSmoothingKeepsVisiblePoints()
{
    for(bool crossStaff:{false,true})for(int side:{-1,1}) {
        std::unique_ptr<MasterScore> score(readScore(crossStaff?
            "mscore/performanceeditor/repair054-crossstaff.mscx":"mscore/performanceeditor/freeslur.mscx"));
        QVERIFY(score);auto line=mixedSlur085(score.get(),crossStaff,true,side);
        auto segment=line->frontSegment();QVERIFY(segment);
        const auto before=segment->gripsPositions()[6]-segment->pagePos();
        FreeSlurNode shown;QVERIFY(FreeSlur::smoothNode(line,0,&shown));
        score->startCmd();line->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode({shown}));score->endCmd();
        const auto after=segment->gripsPositions()[6]-segment->pagePos();
        const double shift=QLineF(before,after).length()/score->spatium();
        qInfo("smooth visible node: cross-staff=%d moved=%.6f spaces",crossStaff,shift);
        QVERIFY2(shift<.001,"Smoothing the tangent must preserve the displayed point");
    }
}

void TestPerformanceEditor::freeSlurSmoothingKeepsHandlesLocal()
{
    std::unique_ptr<MasterScore> score(readScore("mscore/performanceeditor/freeslur.mscx"));QVERIFY(score);
    auto line=mixedSlur085(score.get(),false,false);
    score->startCmd();line->undoChangeProperty(Pid::FREE_SLUR_START_SIDE,0);
    line->undoChangeProperty(Pid::FREE_SLUR_END_SIDE,0);line->undoChangeProperty(Pid::FREE_SLUR_NODES,QString("[]"));score->endCmd();
    auto segment=line->frontSegment();const auto reference=segment->gripsPositions();
    auto base=[&](double t){const double r=1-t;
        return r*r*r*reference[int(Grip::START)]+3*r*r*t*reference[int(Grip::BEZIER1)]+
            3*r*t*t*reference[int(Grip::BEZIER2)]+t*t*t*reference[int(Grip::END)];};
    const auto center=base(.5);const double sp=score->spatium(),gap=.15*sp;
    QVector<FreeSlurNode> nodes;
    for(int i=0;i<3;++i) {
        const double t=.45+i*.05;const auto position=center+QPointF((i-1)*gap,0);
        nodes.append({t,(position-base(t))/sp,{},true});
    }
    score->startCmd();line->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(nodes));score->endCmd();
    const auto before=segment->gripsPositions()[9];FreeSlurNode shown;QVERIFY(FreeSlur::smoothNode(line,1,&shown));
    nodes[1]=shown;score->startCmd();line->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(nodes));score->endCmd();
    const auto grips=segment->gripsPositions();
    QVERIFY(QLineF(before,grips[9]).length()<.001*sp);
    QVERIFY2(grips[10].x()>=grips[6].x()-.001*sp,"The left handle must remain between its adjacent points");
    QVERIFY2(grips[11].x()<=grips[12].x()+.001*sp,"The right handle must remain between its adjacent points");
    QVERIFY(QLineF(grips[10],grips[9]).length()<gap);
    QVERIFY(QLineF(grips[9],grips[11]).length()<gap);
}

void TestPerformanceEditor::freeSlurMixedSidesMouseAndReopen()
{
    if(!Ms::mscore) {
        QCoreApplication::setOrganizationName("PerformanceRegression");QCoreApplication::setApplicationName("Engraving085");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,_directory.path());
        dataPath=_directory.path();MScore::noGui=true;noSeq=true;converterMode=true;
        initMuseScoreResources();QStringList args;MuseScore::init(args);
    }
    auto main=Ms::mscore;QVERIFY(main);main->resize(1150,760);main->show();
    Shortcut::load();
    for(const char* key:{"undo","redo"}) {auto shortcut=Shortcut::getShortcut(key);shortcut->setStandardKey(shortcut->standardKey());}
    QVERIFY(!getAction("undo")->shortcut().isEmpty());main->activateWindow();
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    for(bool crossStaff:{false,true}) {
        auto score=main->readScore(QString(TESTROOT)+(crossStaff?
            "/mtest/mscore/performanceeditor/repair054-crossstaff.mscx":"/mtest/mscore/performanceeditor/freeslur.mscx"));
        QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
        auto line=mixedSlur085(score,crossStaff,true);auto segment=line->frontSegment();
        const auto midi=freeSlurMidi(score);auto view=main->currentScoreView();
        auto select=[&]{view->changeState(ViewState::NORMAL);score->select(segment,SelectType::SINGLE);
            getAction("inspector")->setChecked(true);main->cmd(getAction("inspector"));
            main->resizeDocks({main->findChild<Inspector*>()},{340},Qt::Horizontal);QTest::qWait(100);
            auto dock=main->findChild<Inspector*>();
            qInfo("Inspector after selection: cross=%d width=%d min=%d hint=%d mainMin=%d floating=%d",crossStaff,dock->width(),dock->minimumWidth(),dock->sizeHint().width(),main->minimumWidth(),dock->isFloating());};
        auto panel=[&]()->InspectorSlurTie*{auto dock=main->findChild<Inspector*>();
            auto stack=dock?dock->findChild<QStackedWidget*>():nullptr;
            return stack?qobject_cast<InspectorSlurTie*>(stack->currentWidget()):nullptr;};
        auto reveal=[&](QWidget* control){
            for(auto parent=control->parentWidget();parent;parent=parent->parentWidget())
                if(auto scroll=qobject_cast<QScrollArea*>(parent))scroll->ensureWidgetVisible(control);
            QTest::qWait(30);
        };
        select();QVERIFY(panel());
        auto nodeChoice=panel()->findChild<QComboBox*>("freeSlurNode");QVERIFY(nodeChoice);nodeChoice->setCurrentIndex(0);
        auto lock=panel()->findChild<QCheckBox*>("freeSlurLock");QVERIFY(lock);
        QVERIFY(!lock->isChecked());reveal(lock);
        const auto before=segment->gripsPositions()[6]-segment->pagePos();const int lockUndo=score->undoStack()->getCurIdx();
        QTest::mouseClick(lock,Qt::LeftButton);QTRY_VERIFY(line->freeNodes()[0].locked);
        QCOMPARE(score->undoStack()->getCurIdx(),lockUndo+1);
        QVERIFY(QLineF(before,segment->gripsPositions()[6]-segment->pagePos()).length()<.001*score->spatium());
        score->undoRedo(true,&ed);select();QVERIFY(panel());
        const auto smoothPoint=segment->gripsPositions()[6]-segment->pagePos();
        auto smooth=panel()->findChild<QToolButton*>("freeSlurSmoothSpan");QVERIFY(smooth);reveal(smooth);
        QTest::mouseClick(smooth,Qt::LeftButton);QTRY_VERIFY(line->freeNodes()[0].locked);
        QVERIFY(QLineF(smoothPoint,segment->gripsPositions()[6]-segment->pagePos()).length()<.001*score->spatium());
        QCOMPARE(segment->freeCollisions(),0);
        int turns=0,previous=0;
        for(int i=0;i<=200;++i) {
            const double dy=FreeSlur::curvePoint(segment->freeCurvePoints(),i/200.).d.y()/score->spatium();
            if(std::abs(dy)<.02)continue;
            const int sign=dy>0?1:-1;
            if(previous && sign!=previous)++turns;
            previous=sign;
        }
        qInfo("Visible slur vertical turns: cross-staff=%d turns=%d",crossStaff,turns);
        QVERIFY2(turns<=(crossStaff?0:2),"This phrase must retain a simple rising path or a single smooth S");
        const auto original=line->getProperty(Pid::FREE_SLUR_NODES);const int dragUndo=score->undoStack()->getCurIdx();
        view->setFocus();view->startEdit(segment,Grip(6));view->adjustCanvasPosition(line->startCR(),false);
        QTest::qWait(50);view->updateGrips();
        const auto point=view->matrix().map(view->getEditData().grip[6].center()).toPoint(),target=point+QPoint(12,8);
        QVERIFY(view->rect().contains(point));QVERIFY(view->rect().contains(target));
        const auto oldPosition=segment->gripsPositions()[6];
        const auto expected=view->toLogical(target)-view->toLogical(point);
        QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(int(view->getEditData().curGrip),6);
        QMouseEvent move(QEvent::MouseMove,target,target,view->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&move);QApplication::sendEvent(view,&move);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,target);
        QCOMPARE(score->undoStack()->getCurIdx(),dragUndo+1);
        const auto actual=segment->gripsPositions()[6]-oldPosition;
        QVERIFY(QLineF(actual,expected).length()<.05*score->spatium());
        main->activateWindow();view->setFocus();QTest::qWait(50);
        QTest::keyClick(view,Qt::Key_Z,Qt::ControlModifier);QTRY_COMPARE(line->getProperty(Pid::FREE_SLUR_NODES),original);
        view->changeState(ViewState::NORMAL);select();QVERIFY(panel());
        view->startEdit(segment,Grip(6));QTest::qWait(100);view->updateGrips();
        auto inspector=main->findChild<Inspector*>();
        qInfo("Inspector after edit: cross=%d width=%d min=%d hint=%d",crossStaff,inspector->width(),inspector->minimumWidth(),inspector->sizeHint().width());
        QVERIFY2(inspector->width()>=280,"The chosen inspector width must remain usable after entering curve edit mode");
        QVERIFY(view->grab().save(output.filePath(crossStaff?"slur-crossstaff-edit-085.png":"slur-mixed-sides-edit-085.png")));
        QVERIFY(panel()->grab().save(output.filePath(crossStaff?"slur-crossstaff-inspector-085.png":"slur-mixed-sides-inspector-085.png")));
        QVERIFY(main->grab().save(output.filePath(crossStaff?"slur-crossstaff-workspace-085.png":"slur-mixed-sides-workspace-085.png")));
        const auto detail=view->matrix().mapRect(segment->canvasBoundingRect().united(line->startCR()->measure()->canvasBoundingRect()))
            .adjusted(-16,-16,16,16).toAlignedRect().intersected(view->rect());
        QVERIFY(!detail.isEmpty());
        QVERIFY(view->grab(detail).save(output.filePath(crossStaff?"slur-crossstaff-detail-085.png":"slur-mixed-sides-detail-085.png")));
        view->changeState(ViewState::NORMAL);
        const auto path=output.filePath(crossStaff?"slur-crossstaff-085.mscz":"slur-mixed-sides-085.mscz");QFileInfo info(path);
        QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> restored(readCreatedScore(path));QVERIFY(restored);
        Slur* found=nullptr;for(auto pair:restored->spannerMap().map())if(pair.second->isSlur())found=toSlur(pair.second);
        QVERIFY(found);QCOMPARE(found->getProperty(Pid::FREE_SLUR_NODES),line->getProperty(Pid::FREE_SLUR_NODES));
        QCOMPARE(found->freeStartSide(),-1);QCOMPARE(found->freeEndSide(),1);
        QCOMPARE(freeSlurMidi(restored.get()),midi);QCOMPARE(freeSlurMidi(score),midi);
    }
}
