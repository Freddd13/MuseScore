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
      view->updateGrips();auto mouseNode=view->matrix().map(view->getEditData().grip[6].center()).toPoint();auto mouseMoved=mouseNode+QPoint(0,25);before=score->undoStack()->getCurIdx();
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
