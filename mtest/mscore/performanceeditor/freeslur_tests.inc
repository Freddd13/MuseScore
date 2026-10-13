static Slur* addFreeSlur(Score* score)
      {
      auto start=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto endSegment=score->lastMeasure()->last();while(endSegment->segmentType()!=SegmentType::ChordRest) endSegment=endSegment->prev();
      auto end=toChord(endSegment->element(0));
      auto slur=new Slur(score);slur->setTrack(0);slur->setTrack2(0);slur->setTick(start->tick());slur->setTicks(end->tick()-start->tick());slur->setStartElement(start);slur->setEndElement(end);
      score->startCmd();score->undoAddElement(slur);score->endCmd();return slur;
      }
static QImage freeSlurImage(SlurSegment* segment)
      {
      QImage image(1600,500,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.translate(50,200);segment->draw(&painter);return image;
      }
static QVector<QVector<int>> freeSlurMidi(Score* score)
      {
      EventMap events;score->renderMidi(&events,false,false,defaultState);QVector<QVector<int>> result;
      for(const auto& e:events) if(e.second.type()==ME_NOTEON) result.append({e.first,e.second.pitch(),e.second.velo(),e.second.channel()});return result;
      }
void TestPerformanceEditor::freeSlurModelAndReflow()
      {
      std::unique_ptr<MasterScore> score(readScore("mscore/performanceeditor/freeslur.mscx"));QVERIFY(score);score->doLayout();
      score->startCmd();score->insertMeasure(ElementType::VBOX,score->first());score->endCmd();
      auto part=new Score(score.get());auto excerpt=new Excerpt(score.get());excerpt->setPartScore(part);excerpt->setParts({score->parts().front()});excerpt->setTitle("Piano");Excerpt::createExcerpt(excerpt);score->excerpts().append(excerpt);part->doLayout();
      auto slur=addFreeSlur(score.get());QVERIFY(slur->frontSegment());auto legacy=freeSlurImage(slur->frontSegment());auto midi=freeSlurMidi(score.get());
      QVERIFY(!slur->freeMode());QCOMPARE(slur->frontSegment()->gripsCount(),6);
      score->startCmd();slur->undoChangeProperty(Pid::FREE_SLUR_MODE,true);score->endCmd();score->doLayout();QCOMPARE(freeSlurImage(slur->frontSegment()),legacy); // empty mode does not rebuild the shape
      QVector<FreeSlurNode> nodes={{.25,{0,-3},{0,0}},{.5,{0,2},{0,0}},{.75,{0,-2},{4,0}}};const auto data=FreeSlur::encode(nodes);
      auto before=score->undoStack()->getCurIdx();score->startCmd();slur->undoChangeProperty(Pid::FREE_SLUR_NODES,data);score->endCmd();QCOMPARE(score->undoStack()->getCurIdx(),before+1);QCOMPARE(slur->frontSegment()->gripsCount(),15);
      QVERIFY(freeSlurImage(slur->frontSegment())!=legacy);QCOMPARE(freeSlurMidi(score.get()),midi);
      auto handles=slur->frontSegment()->gripsPositions();for(int i=6;i<int(handles.size());i+=3) {const auto left=handles[i]-handles[i+1],right=handles[i+2]-handles[i];QVERIFY(std::abs(left.x()*right.y()-left.y()*right.x())<1e-7);QVERIFY(left.x()*right.x()+left.y()*right.y()>0);}
      const auto originalSpatium=score->spatium();score->setSpatium(originalSpatium*1.25);score->doLayout();QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES).toString(),data);score->setSpatium(originalSpatium);score->doLayout();
      Slur* linked=nullptr;for(const auto& sp:part->spannerMap().map()) if(sp.second->isSlur()) linked=toSlur(sp.second);QVERIFY(linked);QVERIFY(linked->freeMode());QCOMPARE(linked->getProperty(Pid::FREE_SLUR_NODES).toString(),data);
      score->undoRedo(true,&ed);QCOMPARE(slur->freeNodes().size(),0);QCOMPARE(linked->freeNodes().size(),0);score->undoRedo(false,&ed);
      std::unique_ptr<Slur> clone(slur->clone());QCOMPARE(clone->getProperty(Pid::FREE_SLUR_NODES),slur->getProperty(Pid::FREE_SLUR_NODES));QCOMPARE(clone->freeMode(),true);
      for(const QString bad:{"null","[[0,0,0,0,0]]","[[1,0,0,0,0]]","[[0.5,101,0,0,0]]","[[0.5,0,0,401,0]]","[[0.5,0,0,0,0],[0.5,0,0,0,0]]"}) {QVERIFY(!slur->setProperty(Pid::FREE_SLUR_NODES,bad));QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES).toString(),data);}
      QTemporaryDir directory;QVERIFY(directory.isValid());
      for(const char* ext:{"mscx","mscz"}) {
            QString file=directory.path()+"/curve."+ext;if(QString(ext)=="mscz") {QFileInfo info(file);QVERIFY(score->saveCompressedFile(info,false,false));}else QVERIFY(saveScore(score.get(),file));
            std::unique_ptr<MasterScore> restored(readCreatedScore(file));QVERIFY(restored);Slur* r=nullptr;for(const auto& sp:restored->spannerMap().map()) if(sp.second->isSlur())r=toSlur(sp.second);QVERIFY(r);QCOMPARE(r->getProperty(Pid::FREE_SLUR_NODES).toString(),data);QVERIFY(r->freeMode());
            }
      auto layoutBreak=new LayoutBreak(score.get());layoutBreak->setLayoutBreakType(LayoutBreak::Type::LINE);layoutBreak->setParent(score->firstMeasure());score->startCmd();score->undoAddElement(layoutBreak);score->endCmd();
      QVERIFY(slur->spannerSegments().size()>1);int grips=0;for(auto segment:slur->spannerSegments()) grips+=segment->gripsCount()-6;QCOMPARE(grips,9);QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES).toString(),data);
      score->undoRedo(true,&ed);QCOMPARE(slur->frontSegment()->gripsCount(),15);QCOMPARE(freeSlurMidi(score.get()),midi);
      QVector<FreeSlurNode> dense;for(int i=1;i<=32;++i)dense.append({i/33.0,{0,(i%2 ? -1.0:1.0)}, {}});QVERIFY(slur->setProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(dense)));
      QElapsedTimer timer;timer.start();for(int i=0;i<20;++i)score->doLayout();qInfo("32-node slur, 20 layouts: %.3f ms",timer.nsecsElapsed()/1e6);QCOMPARE(slur->frontSegment()->gripsCount(),102);
      dense.append({.999,{}, {}});QVERIFY(!slur->setProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(dense)));
      QVERIFY(slur->setProperty(Pid::FREE_SLUR_NODES,data));score->doLayout();auto segment=slur->frontSegment();auto positions=segment->gripsPositions();QVERIFY(segment->shape().contains(positions[6]-segment->pagePos()));
      score->startCmd();segment->reset();score->endCmd();QCOMPARE(slur->freeNodes().size(),0);score->undoRedo(true,&ed);QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES).toString(),data);
      }
void TestPerformanceEditor::freeSlurInspectorAndGrips()
      {
      if(!Ms::mscore) {QCoreApplication::setOrganizationName("PerformanceRegression");QCoreApplication::setApplicationName("FreeSlurInspector");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,_directory.path());dataPath=_directory.path();MScore::noGui=true;noSeq=true;converterMode=true;initMuseScoreResources();QStringList args;MuseScore::init(args);}
      auto main=Ms::mscore;main->resize(1250,850);main->show();auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/performanceeditor/freeslur.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));auto slur=addFreeSlur(score);
      auto select=[&] {score->startCmd();score->select(slur->frontSegment(),SelectType::SINGLE,0);score->endCmd();getAction("inspector")->setChecked(true);main->cmd(getAction("inspector"));QTest::qWait(40);};select();
      auto panel=main->findChild<InspectorSlurTie*>();QVERIFY(panel);auto mode=panel->findChild<QCheckBox*>("freeSlurMode");QVERIFY(mode);QVERIFY(mode->isEnabled());QTest::mouseClick(mode,Qt::LeftButton,Qt::NoModifier,QPoint(8,mode->height()/2));QVERIFY(mode->isChecked());QVERIFY(slur->freeMode());
      auto add=panel->findChild<QToolButton*>("freeSlurAdd");QVERIFY(add);int before=score->undoStack()->getCurIdx();QTest::mouseClick(add,Qt::LeftButton);QTRY_COMPARE(slur->freeNodes().size(),1);QCOMPARE(score->undoStack()->getCurIdx(),before+1);QCOMPARE(slur->frontSegment()->gripsCount(),9);
      auto spin=panel->findChild<QDoubleSpinBox*>("freeSlurValue1");auto label=panel->findChild<QLabel*>("freeSlurValue1Label");QVERIFY(spin&&label);QPoint p=label->rect().center(),to=p+QPoint(20,0);QTest::mousePress(label,Qt::LeftButton,Qt::NoModifier,p);QMouseEvent motion(QEvent::MouseMove,to,to,label->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);QApplication::sendEvent(label,&motion);QCOMPARE(spin->value(),.2);QCOMPARE(slur->freeNodes()[0].offset.y(),0.0);QTest::keyClick(label,Qt::Key_Escape);QTest::mouseRelease(label,Qt::LeftButton,Qt::NoModifier,to);QCOMPARE(slur->freeNodes()[0].offset.y(),0.0);
      spin->setFocus();spin->selectAll();QTest::keyClicks(spin,"-3");QTest::keyClick(spin,Qt::Key_Return);QTRY_COMPARE(slur->freeNodes()[0].offset.y(),-3.0);
      auto view=main->currentScoreView();view->startEdit(slur->frontSegment(),Grip(6));view->updateGrips();QCOMPARE(view->getEditData().grips,9);view->setFocus();
      before=score->undoStack()->getCurIdx();const auto original=slur->getProperty(Pid::FREE_SLUR_NODES);QTest::keyClick(view,Qt::Key_Up);QVERIFY(slur->getProperty(Pid::FREE_SLUR_NODES)!=original);QCOMPARE(score->undoStack()->getCurIdx(),before+1);score->undoRedo(true,&ed);QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES),original);
      QTest::qWait(25);view->repaint();view->updateGrips();auto mouseNode=view->matrix().map(view->getEditData().grip[6].center()).toPoint();auto mouseMoved=mouseNode+QPoint(0,25);before=score->undoStack()->getCurIdx();
      QVERIFY(view->rect().contains(mouseNode));QVERIFY(view->rect().contains(mouseMoved));
      auto drag=[&] {QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,mouseNode);QCOMPARE(int(view->getEditData().curGrip),6);QMouseEvent move(QEvent::MouseMove,mouseMoved,mouseMoved,view->mapToGlobal(mouseMoved),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&move);QApplication::sendEvent(view,&move);};
      drag();QVERIFY(slur->getProperty(Pid::FREE_SLUR_NODES)!=original);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,mouseMoved);QCOMPARE(score->undoStack()->getCurIdx(),before+1);score->undoRedo(true,&ed);QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES),original);
      view->updateGrips();mouseNode=view->matrix().map(view->getEditData().grip[6].center()).toPoint();mouseMoved=mouseNode+QPoint(0,25);before=score->undoStack()->getCurIdx();drag();QTest::keyClick(view,Qt::Key_Escape);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,mouseMoved);QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES),original);QCOMPARE(score->undoStack()->getCurIdx(),before);
      view->getEditData().curGrip=Grip(7);QTest::keyClick(view,Qt::Key_Right);QVERIFY(!slur->freeNodes()[0].tangent.isNull());QTest::keyClick(view,Qt::Key_Home);QVERIFY(slur->freeNodes()[0].tangent.isNull());
      view->getEditData().curGrip=Grip(6);QTest::keyClick(view,Qt::Key_Delete);QCOMPARE(slur->freeNodes().size(),0);QCOMPARE(view->getEditData().grips,6);QVERIFY(score->spannerMap().map().size()>0);score->undoRedo(true,&ed);QCOMPARE(slur->freeNodes().size(),1);view->changeState(ViewState::NORMAL);select();
      panel=main->findChild<InspectorSlurTie*>();spin=panel->findChild<QDoubleSpinBox*>("freeSlurValue0");score->setIsPlaying(true);spin->setValue(1.0);auto vertical=panel->findChild<QDoubleSpinBox*>("freeSlurValue1");vertical->setValue(-4.0);QTest::qWait(40);QVERIFY(main->performanceEditor()->hasPending());QCOMPARE(slur->freeNodes()[0].offset.x(),0.0);score->setIsPlaying(false);QTRY_COMPARE(slur->freeNodes()[0].offset.x(),1.0);QCOMPARE(slur->freeNodes()[0].offset.y(),-4.0);
      score->startCmd();slur->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode({{.25,{0,-3},{}},{.5,{0,2},{}},{.75,{0,-2},{2,0}}}));score->endCmd();select();
      panel=main->findChild<InspectorSlurTie*>();panel->grab().save("free-slur-inspector.png");view->startEdit(slur->frontSegment(),Grip(6));QTest::qWait(50);view->grab().save("free-slur-edit.png");view->changeState(ViewState::NORMAL);
      QVERIFY(saveScore(score,"free-slur-validation.mscx"));QVERIFY(main->savePdf(score,"free-slur.pdf"));QFile svg("free-slur.svg");QVERIFY(svg.open(QIODevice::WriteOnly));QVERIFY(main->saveSvg(score,&svg));svg.close();QVERIFY(QFileInfo("free-slur.pdf").size()>100);QVERIFY(QFileInfo("free-slur.svg").size()>100);
      }
void TestPerformanceEditor::freeSlurAutomaticGeometry()
      {
      using FreeSlur::CurvePoint;
      QVector<CurvePoint> line={{0,{0,0},{100,0}},{1,{100,0},{100,0}}};
      QVector<QRectF> obstacles={{40,-4,20,8}};
      const auto result=FreeSlur::avoid(line,obstacles,true,10);
      QCOMPARE(result.collisions,0);QVERIFY(result.candidates>0);QCOMPARE(result.points.size(),2);
      QCOMPARE(result.points.front().p,line.front().p);QCOMPARE(result.points.back().p,line.back().p);
      QVERIFY(FreeSlur::curvePosition(result.points,.5).y()<-4);
      auto below=FreeSlur::avoid(line,obstacles,false,10);QCOMPARE(below.collisions,0);QVERIFY(FreeSlur::curvePosition(below.points,.5).y()>4);
      auto clear=FreeSlur::avoid(line,{},true,10);QCOMPARE(clear.candidates,0);QCOMPARE(clear.points.front().d,line.front().d);
      line.front().locked=line.back().locked=true;
      auto local=FreeSlur::avoid(line,obstacles,true,10);QCOMPARE(local.collisions,0);QVERIFY(local.points.size()>2);QVERIFY(local.points.size()<=6);
      QCOMPARE(local.points.front().d,line.front().d);QCOMPARE(local.points.back().d,line.back().d);
      line.insert(1,{.5,{50,0},{100,0},true});
      auto blocked=FreeSlur::avoid(line,obstacles,true,10);QVERIFY(blocked.collisions>0);
      auto fixed=std::find_if(blocked.points.begin(),blocked.points.end(),[](const CurvePoint& p){return p.t==.5;});QVERIFY(fixed!=blocked.points.end());QCOMPARE(fixed->p,QPointF(50,0));QCOMPARE(fixed->d,QPointF(100,0));
      line[1].locked=false;auto unlocked=FreeSlur::avoid(line,obstacles,true,10);QCOMPARE(unlocked.collisions,0);
      auto repeated=FreeSlur::avoid(line,obstacles,true,10);QCOMPARE(repeated.points.size(),unlocked.points.size());for(int i=0;i<repeated.points.size();++i){QCOMPARE(repeated.points[i].p,unlocked.points[i].p);QCOMPARE(repeated.points[i].d,unlocked.points[i].d);}
      for(const auto& p:unlocked.points)QVERIFY(std::abs(p.p.y())<=60.001);
      QVector<QRectF> excessive;for(int i=0;i<2049;++i)excessive.append(obstacles[0]);auto limited=FreeSlur::avoid(line,excessive,true,10);QVERIFY(limited.limited);QCOMPARE(limited.candidates,0);
      QVector<QRectF> dense;for(int i=0;i<2048;++i)dense.append({i*100.0/2048,-4,.1,8});
      QElapsedTimer large;large.start();auto denseResult=FreeSlur::avoid(line,dense,true,10);QVERIFY(denseResult.candidates<=30);qInfo("automatic slur, 2048 shapes: %.3f ms",large.nsecsElapsed()/1e6);
      QElapsedTimer timer;timer.start();for(int i=0;i<200;++i)FreeSlur::avoid(line,obstacles,true,10);qInfo("automatic slur, 200 constrained candidates: %.3f ms",timer.nsecsElapsed()/1e6);
      }
void TestPerformanceEditor::freeSlurAutomaticPersistence()
      {
      std::unique_ptr<MasterScore> score(readScore("mscore/performanceeditor/freeslur.mscx"));QVERIFY(score);score->doLayout();auto slur=addFreeSlur(score.get());auto midi=freeSlurMidi(score.get());
      QVector<FreeSlurNode> nodes;QVERIFY(FreeSlur::decode("[[0.5,0,2,0,0]]",&nodes));QVERIFY(nodes[0].locked);QCOMPARE(FreeSlur::encode(nodes),QString("[[0.5,0,2,0,0]]"));
      nodes[0].locked=false;const auto data=FreeSlur::encode(nodes);QVERIFY(data.contains("false"));QVERIFY(!FreeSlur::decode("[[0.5,0,2,0,0,0]]",&nodes));QVERIFY(FreeSlur::decode(data,&nodes));QVERIFY(!nodes[0].locked);
      score->startCmd();slur->undoChangeProperty(Pid::FREE_SLUR_MODE,true);slur->undoChangeProperty(Pid::FREE_SLUR_NODES,data);slur->undoChangeProperty(Pid::FREE_SLUR_AUTO,true);score->endCmd();
      auto segment=slur->frontSegment();auto middle=score->firstMeasure()->findSegment(SegmentType::ChordRest,Fraction::fromTicks(480));QVERIFY(middle);
      const auto handles=segment->gripsPositions();const auto local=handles[6]-segment->pagePos();
      middle->staffShape(0).add(QRectF(local-QPointF(4,4),QSizeF(8,8)).translated(-(middle->pos()+middle->measure()->pos()-segment->pos())));
      segment->computeBezier();QVERIFY(segment->gripsPositions()[6]!=handles[6]);
      const auto undo=score->undoStack()->getCurIdx();for(int i=0;i<5;++i)score->doLayout();QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES).toString(),data);QCOMPARE(freeSlurMidi(score.get()),midi);
      std::unique_ptr<Slur> clone(slur->clone());QVERIFY(clone->freeAuto());QCOMPARE(clone->freeNodes()[0].locked,false);
      QTemporaryDir directory;for(const char* ext:{"mscx","mscz"}) {
            const auto path=directory.path()+"/auto."+ext;if(QString(ext)=="mscz"){QFileInfo file(path);QVERIFY(score->saveCompressedFile(file,false,false));}else QVERIFY(saveScore(score.get(),path));
            std::unique_ptr<MasterScore> restored(readCreatedScore(path));QVERIFY(restored);Slur* r=nullptr;for(const auto& s:restored->spannerMap().map())if(s.second->isSlur())r=toSlur(s.second);QVERIFY(r);QVERIFY(r->freeAuto());QVERIFY(!r->freeNodes()[0].locked);QCOMPARE(r->getProperty(Pid::FREE_SLUR_NODES).toString(),data);
            }
      auto layoutBreak=new LayoutBreak(score.get());layoutBreak->setLayoutBreakType(LayoutBreak::Type::LINE);layoutBreak->setParent(score->firstMeasure());score->startCmd();score->undoAddElement(layoutBreak);score->endCmd();QVERIFY(slur->spannerSegments().size()>1);QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES).toString(),data);
      score->startCmd();slur->reset();score->endCmd();QVERIFY(!slur->freeAuto());score->undoRedo(true,&ed);QVERIFY(slur->freeAuto());QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES).toString(),data);
      }
void TestPerformanceEditor::freeSlurBeamAvoidance()
      {
      QFile fixture(QString(TESTROOT)+"/mtest/mscore/performanceeditor/freeslur.mscx");QVERIFY(fixture.open(QIODevice::ReadOnly));auto xml=fixture.readAll().replace("quarter","eighth");
      QTemporaryDir directory;QFile file(directory.path()+"/beams.mscx");QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();std::unique_ptr<MasterScore> score(readCreatedScore(file.fileName()));QVERIFY(score);score->doLayout();
      auto first=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));auto last=toChord(score->firstMeasure()->findSegment(SegmentType::ChordRest,Fraction::fromTicks(720))->element(0));QVERIFY(first->beam());
      auto slur=new Slur(score.get());slur->setTrack(0);slur->setTrack2(0);slur->setTick(first->tick());slur->setTicks(last->tick()-first->tick());slur->setStartElement(first);slur->setEndElement(last);
      score->startCmd();score->undoAddElement(slur);slur->undoChangeProperty(Pid::SLUR_DIRECTION,QVariant::fromValue(Direction::UP));slur->undoChangeProperty(Pid::FREE_SLUR_MODE,true);slur->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode({{.3,{}, {},false}}));score->endCmd();
      auto segment=slur->frontSegment();const auto center=first->beam()->pagePos()+first->beam()->bbox().center();const auto node=segment->gripsPositions()[6];auto nodes=slur->freeNodes();nodes[0].offset=(center-node)/score->spatium();QVERIFY(slur->setProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(nodes)));segment->computeBezier();
      QVERIFY(QLineF(segment->gripsPositions()[6],center).length()<.001);
      // Remove ordinary note/stem rectangles so only the independently collected
      // native beam outline can move this point away from the beam.
      for(auto s=score->firstMeasure()->first();s && s->measure()==score->firstMeasure();s=s->next1())s->staffShape(0).clear();
      const auto saved=slur->getProperty(Pid::FREE_SLUR_NODES);QVERIFY(slur->setProperty(Pid::FREE_SLUR_AUTO,true));segment->computeBezier();QVERIFY(QLineF(segment->gripsPositions()[6],center).length()>score->spatium()*.2);QCOMPARE(segment->freeCollisions(),0);QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES),saved);
      }
void TestPerformanceEditor::freeSlurAutomaticControls()
      {

      if(!Ms::mscore) {QCoreApplication::setOrganizationName("PerformanceRegression");QCoreApplication::setApplicationName("FreeSlurAutoInspector");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,_directory.path());dataPath=_directory.path();MScore::noGui=true;noSeq=true;converterMode=true;initMuseScoreResources();QStringList args;MuseScore::init(args);}
      auto main=Ms::mscore;main->resize(1250,900);main->show();auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/performanceeditor/freeslur.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));auto slur=addFreeSlur(score);
      score->startCmd();slur->undoChangeProperty(Pid::FREE_SLUR_MODE,true);slur->undoChangeProperty(Pid::FREE_SLUR_NODES,QString("[[0.5,0,2,0,0]]"));score->select(slur->frontSegment(),SelectType::SINGLE,0);score->endCmd();getAction("inspector")->setChecked(true);main->cmd(getAction("inspector"));QTest::qWait(40);
      auto dock=main->findChild<Inspector*>();QVERIFY(dock);auto stack=dock->findChild<QStackedWidget*>();QVERIFY(stack);
      auto panelNow=[&] {return qobject_cast<InspectorSlurTie*>(stack->currentWidget());};
      auto clickBox=[&](const char* id) {auto box=panelNow()->findChild<QCheckBox*>(id);QVERIFY(box);QTest::mouseClick(box,Qt::LeftButton,Qt::NoModifier,QPoint(8,box->height()/2));};
      QVERIFY(panelNow());QVERIFY(panelNow()->findChild<QCheckBox*>("freeSlurLock")->isChecked());
      clickBox("freeSlurAuto");QTRY_VERIFY(slur->freeAuto());
      clickBox("freeSlurLock");QTRY_VERIFY(!slur->freeNodes()[0].locked);
      auto shown=slur->frontSegment()->gripsPositions()[6];
      clickBox("freeSlurLock");QTRY_VERIFY(slur->freeNodes()[0].locked);QVERIFY(QLineF(shown,slur->frontSegment()->gripsPositions()[6]).length()<.001);
      clickBox("freeSlurLock");QTRY_VERIFY(!slur->freeNodes()[0].locked);
      // Starting a drag on an automatically moved point keeps its visible position.
      auto segment=slur->frontSegment();auto before=segment->gripsPositions()[6];score->startCmd();EditData drag;drag.curGrip=Grip(6);drag.delta=QPointF(0,.1*score->spatium());segment->startEditDrag(drag);segment->editDrag(drag);auto after=segment->gripsPositions()[6];QVERIFY(QLineF(before+drag.delta,after).length()<.001);segment->endEditDrag(drag);score->endCmd();QVERIFY(slur->freeNodes()[0].locked);
      auto position=panelNow()->findChild<QDoubleSpinBox*>("freeSlurAddPosition");auto add=panelNow()->findChild<QToolButton*>("freeSlurAdd");QVERIFY(position&&add);position->setValue(25);
      FreeSlurNode expected;QVERIFY(segment->displayedFreeNode(.25,&expected));QTest::mouseClick(add,Qt::LeftButton);QTRY_COMPARE(slur->freeNodes().size(),2);QCOMPARE(slur->freeNodes()[0].offset,expected.offset);QCOMPARE(slur->freeNodes()[0].tangent,expected.tangent);
      const auto old=slur->getProperty(Pid::FREE_SLUR_NODES);const int undo=score->undoStack()->getCurIdx();auto restore=panelNow()->findChild<QToolButton*>("freeSlurRestoreAuto");QVERIFY(restore);QTest::mouseClick(restore,Qt::LeftButton);QTRY_COMPARE(slur->freeNodes().size(),0);QVERIFY(slur->freeAuto());QCOMPARE(score->undoStack()->getCurIdx(),undo+1);getAction("undo")->trigger();QTRY_COMPARE(slur->getProperty(Pid::FREE_SLUR_NODES),old);
      // Native undo restores the selection recorded before the command. Select
      // the restored line again before querying its inspector controls.
      score->startCmd();score->select(slur->frontSegment(),SelectType::SINGLE,0);score->endCmd();main->cmd(getAction("inspector"));
      QTRY_VERIFY(panelNow());QTRY_VERIFY(panelNow()->findChild<QComboBox*>("freeSlurNode"));QTRY_COMPARE(panelNow()->findChild<QComboBox*>("freeSlurNode")->count(),2);
      auto view=main->currentScoreView();view->setOffset(0,0);view->adjustCanvasPosition(slur->startCR(),false);QTest::qWait(40);QVERIFY(panelNow());panelNow()->grab().save("free-slur-auto-inspector.png");view->grab().save("free-slur-auto-score.png");QVERIFY(main->savePdf(score,"free-slur-auto.pdf"));QFile svg("free-slur-auto.svg");QVERIFY(svg.open(QIODevice::WriteOnly));QVERIFY(main->saveSvg(score,&svg));svg.close();QVERIFY(QFileInfo("free-slur-auto.pdf").size()>100);QCOMPARE(slur->getProperty(Pid::FREE_SLUR_NODES),old);
      }
