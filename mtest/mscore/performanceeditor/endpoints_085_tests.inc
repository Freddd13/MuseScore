void TestPerformanceEditor::freeSlurEndpointEditsAcrossSystems()
{
    std::unique_ptr<MasterScore> score(readScore("mscore/performanceeditor/freeslur.mscx"));QVERIFY(score);
    score->startCmd();score->insertMeasure(ElementType::VBOX,score->first());score->endCmd();
    auto part=new Score(score.get());auto excerpt=new Excerpt(score.get());excerpt->setPartScore(part);
    excerpt->setParts({score->parts().front()});excerpt->setTitle("Piano");Excerpt::createExcerpt(excerpt);score->excerpts().append(excerpt);part->doLayout();
    auto line=addFreeSlur(score.get());
    auto lineBreak=new LayoutBreak(score.get());lineBreak->setLayoutBreakType(LayoutBreak::Type::LINE);
    lineBreak->setParent(score->firstMeasure());
    score->startCmd();score->undoAddElement(lineBreak);
    line->undoChangeProperty(Pid::FREE_SLUR_MODE,true);line->undoChangeProperty(Pid::FREE_SLUR_AUTO,true);
    line->undoChangeProperty(Pid::FREE_SLUR_START_SIDE,-1);line->undoChangeProperty(Pid::FREE_SLUR_END_SIDE,1);score->endCmd();
    QVERIFY(line->spannerSegments().size()>1);const auto midi=freeSlurMidi(score.get());
    const auto left=line->frontSegment()->freeCurvePoints().back().p;
    const auto right=line->backSegment()->freeCurvePoints().front().p;
    const auto from=line->frontSegment()->freeCurvePoints().front().p;
    std::array<FreeSlurEndpoint,2> endpoints;endpoints[0].manualPosition=true;endpoints[0].position={.6,-.2};
    score->startCmd();line->undoChangeProperty(Pid::FREE_SLUR_ENDPOINTS,FreeSlur::encodeEndpoints(endpoints));score->endCmd();
    QVERIFY(QLineF(line->frontSegment()->freeCurvePoints().front().p-from,QPointF(.6,-.2)*score->spatium()).length()<.01*score->spatium());
    QVERIFY(QLineF(left,line->frontSegment()->freeCurvePoints().back().p).length()<.001*score->spatium());
    QVERIFY(QLineF(right,line->backSegment()->freeCurvePoints().front().p).length()<.001*score->spatium());
    std::unique_ptr<Slur> clone(line->clone());QCOMPARE(clone->getProperty(Pid::FREE_SLUR_ENDPOINTS),line->getProperty(Pid::FREE_SLUR_ENDPOINTS));
    Slur* linked=nullptr;for(auto pair:part->spannerMap().map())if(pair.second->isSlur())linked=toSlur(pair.second);
    QVERIFY(linked);QCOMPARE(linked->getProperty(Pid::FREE_SLUR_ENDPOINTS),line->getProperty(Pid::FREE_SLUR_ENDPOINTS));
    score->undoRedo(true,&ed);QVERIFY(!line->freeEndpointsEdited());QCOMPARE(freeSlurMidi(score.get()),midi);
    QVERIFY(!linked->freeEndpointsEdited());
}

void TestPerformanceEditor::freeSlurNativeEndpointsAndEditing()
{
    auto main=Ms::mscore;QVERIFY(main);main->resize(1150,760);main->show();
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));
    for(bool crossStaff:{false,true}) {
        auto score=main->readScore(QString(TESTROOT)+(crossStaff?
            "/mtest/mscore/performanceeditor/repair054-crossstaff.mscx":"/mtest/mscore/performanceeditor/freeslur.mscx"));
        QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
        auto line=mixedSlur085(score,crossStaff,true);auto segment=line->frontSegment();
        auto view=main->currentScoreView();const auto midi=freeSlurMidi(score);const double sp=score->spatium();
        // Native slurs in each direction supply an independent endpoint oracle.
        for(bool start:{true,false}) {
            Slur native(*line);native.setProperty(Pid::FREE_SLUR_MODE,false);native.setAutoplace(false);
            native.setProperty(Pid::SLUR_DIRECTION,QVariant::fromValue(start?Direction::UP:Direction::DOWN));
            native.layout();QVERIFY(native.frontSegment());
            for(auto piece:native.spannerSegments())piece->setAutoplace(false);
            native.layout();
            const auto oracle=native.frontSegment()->gripsPositions();const auto actual=segment->gripsPositions();
            const int grip=int(start?Grip::START:Grip::END);
            qInfo("Native endpoint: cross=%d start=%d up=%d nativeUp=%d actual=(%.3f,%.3f) expected=(%.3f,%.3f) origins=(%.3f,%.3f)/(%.3f,%.3f)",
                crossStaff,start,line->up(),native.up(),actual[grip].x(),actual[grip].y(),oracle[grip].x(),oracle[grip].y(),
                segment->pagePos().x(),segment->pagePos().y(),native.frontSegment()->pagePos().x(),native.frontSegment()->pagePos().y());
            QVERIFY2(QLineF(actual[grip],oracle[grip]).length()<.02*sp,"Free endpoints must follow native stem and beam placement");
        }
        FreeSlurNode locked;QVERIFY(segment->displayedFreeNode(0,&locked));locked.locked=true;
        score->startCmd();line->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode({locked}));score->endCmd();
        for(auto grip:{Grip::START,Grip::END,Grip::BEZIER1,Grip::BEZIER2}) {
            view->changeState(ViewState::NORMAL);score->select(segment,SelectType::SINGLE);
            view->startEdit(segment,grip);view->adjustCanvasPosition(line->startCR(),false);
            main->activateWindow();view->setFocus();QTest::qWait(50);view->updateGrips();
            const auto original=line->getProperty(Pid::FREE_SLUR_ENDPOINTS);const int undo=score->undoStack()->getCurIdx();
            const auto old=segment->gripsPositions();
            const auto point=view->matrix().map(view->getEditData().grip[int(grip)].center()).toPoint();
            const auto target=point+QPoint(10,-7);const auto expected=view->toLogical(target)-view->toLogical(point);
            QVERIFY(view->rect().contains(point));QVERIFY(view->rect().contains(target));
            QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,point);
            QCOMPARE(int(view->getEditData().curGrip),int(grip));
            QMouseEvent move(QEvent::MouseMove,target,target,view->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);QApplication::sendEvent(view,&move);
            QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,target);
            QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
            const auto now=segment->gripsPositions();
            QVERIFY2(QLineF(now[int(grip)]-old[int(grip)],expected).length()<.05*sp,"Endpoint and tangent drags must follow the mouse");
            QVERIFY2(QLineF(now[6],old[6]).length()<.001*sp,"Endpoint edits must keep a fixed interior node in place");
            QCOMPARE(line->getProperty(Pid::FREE_SLUR_NODES).toString(),FreeSlur::encode({locked}));
            main->activateWindow();view->setFocus();QTest::keyClick(view,Qt::Key_Z,Qt::ControlModifier);
            QTRY_COMPARE(line->getProperty(Pid::FREE_SLUR_ENDPOINTS),original);
            main->cmd(Shortcut::getShortcut("redo")->action());QVERIFY(line->freeEndpointsEdited());
            const auto path=output.filePath(QString("slur-endpoint-%1-%2-085.mscz").arg(crossStaff).arg(int(grip)));
            view->changeState(ViewState::NORMAL);QFileInfo info(path);QVERIFY(score->saveCompressedFile(info,false,false));
            std::unique_ptr<MasterScore> restored(readCreatedScore(path));QVERIFY(restored);
            Slur* found=nullptr;for(auto pair:restored->spannerMap().map())if(pair.second->isSlur())found=toSlur(pair.second);
            QVERIFY(found);QCOMPARE(found->getProperty(Pid::FREE_SLUR_ENDPOINTS),line->getProperty(Pid::FREE_SLUR_ENDPOINTS));
            QCOMPARE(freeSlurMidi(restored.get()),midi);
            const auto saved=found->frontSegment()->gripsPositions();
            const auto current=segment->gripsPositions();
            for(auto end:{Grip::START,Grip::END,Grip::BEZIER1,Grip::BEZIER2})
                QVERIFY(QLineF(saved[int(end)]-found->frontSegment()->pagePos(),current[int(end)]-segment->pagePos()).length()<.01*sp);

            view->startEdit(segment,grip);view->setFocus();QTest::qWait(30);view->updateGrips();
            const auto beforeCancel=line->getProperty(Pid::FREE_SLUR_ENDPOINTS);const int cancelUndo=score->undoStack()->getCurIdx();
            const auto from=view->matrix().map(view->getEditData().grip[int(grip)].center()).toPoint(),to=from+QPoint(8,6);
            QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,from);
            QMouseEvent cancelMove(QEvent::MouseMove,to,to,view->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view,&cancelMove);QApplication::sendEvent(view,&cancelMove);
            QTest::keyClick(view,Qt::Key_Escape);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,to);
            QTRY_COMPARE(line->getProperty(Pid::FREE_SLUR_ENDPOINTS),beforeCancel);
            QCOMPARE(score->undoStack()->getCurIdx(),cancelUndo);

            view->startEdit(segment,grip);view->setFocus();QTest::keyClick(view,Qt::Key_Home);
            const auto home=line->freeEndpoints()[grip==Grip::START||grip==Grip::BEZIER1?0:1];
            if(grip==Grip::START||grip==Grip::END)QVERIFY(!home.manualPosition);else QVERIFY(!home.manualTangent);
            score->undoRedo(true,&ed);QCOMPARE(line->getProperty(Pid::FREE_SLUR_ENDPOINTS),beforeCancel);
            view->startEdit(segment,grip);view->setFocus();QTest::keyClick(view,Qt::Key_Right);
            QVERIFY(line->getProperty(Pid::FREE_SLUR_ENDPOINTS)!=beforeCancel);
            score->undoRedo(true,&ed);QCOMPARE(line->getProperty(Pid::FREE_SLUR_ENDPOINTS),beforeCancel);
            score->undoRedo(true,&ed);QCOMPARE(line->getProperty(Pid::FREE_SLUR_ENDPOINTS),original);
        }
        score->select(segment,SelectType::SINGLE);view->startEdit(segment,Grip::START);QTest::qWait(50);
        QVERIFY(view->grab().save(output.filePath(crossStaff?"slur-native-endpoints-crossstaff-085.png":"slur-native-endpoints-085.png")));
    }
}
