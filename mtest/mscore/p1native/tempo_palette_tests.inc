 void playableTempoPaletteReadAndApply(){
  auto panel=std::unique_ptr<PalettePanel>(MuseScore::newTempoPalettePanel());QVERIFY(panel);
  TextLine* rit=nullptr;TempoText* restore=nullptr;
  for(int i=0;i<panel->ncells();++i){auto e=panel->cell(i)->element.get();if(e->isTextLine()&&toTextLine(e)->ritPlay())rit=toTextLine(e);if(e->isTempoText()&&toTempoText(e)->restoreMode()==1)restore=toTempoText(e);}
  QVERIFY(rit);QVERIFY(restore);int count=panel->ncells();appendPlayableTempoSymbols(panel.get());QCOMPARE(panel->ncells(),count);
  PalettePanel older(PalettePanel::Type::Tempo);older.setName("Personal tempo");older.setGrid(73,31);
  auto original=new TempoText(gscore);original->setXmlText("My tempo");older.append(original,"My entry");
  auto loaded=PalettePanel::readMimeData(older.mimeData());QVERIFY(loaded);QCOMPARE(loaded->ncells(),3);QCOMPARE(loaded->gridSize(),QSize(73,31));QCOMPARE(loaded->cell(0)->name,QString("My entry"));
  auto again=PalettePanel::readMimeData(loaded->mimeData());QVERIFY(again);QCOMPARE(again->ncells(),3);
  rit->setBeginText("poco rit.");rit->setContinueText("continuation");rit->setEndText("end");rit->setLineVisible(false);rit->setProperty(Pid::RIT_TARGET,65.);
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));
  auto start=score->tick2segment(Fraction(),false,SegmentType::ChordRest);auto finish=score->tick2segment(Fraction::fromTicks(960),false,SegmentType::ChordRest);QVERIFY(start);QVERIFY(finish);
  score->selection().setRange(start,finish,0,1);score->selection().updateSelectedElements();int before=score->undoStack()->getCurIdx();
  auto widget=std::unique_ptr<Palette>(MuseScore::newTempoPalette());QVERIFY(widget);widget->setUseDoubleClickToActivate(true);widget->resize(400,500);widget->show();QTest::qWait(25);
  int index=-1;for(int i=0;i<widget->size();++i){auto e=widget->cellAt(i)->element.get();if(e->isTextLine()&&toTextLine(e)->ritPlay()){index=i;widget->cellAt(i)->element.reset(rit->clone());break;}}
  QVERIFY(index>=0);int width=widget->gridWidthM()+(widget->width()%widget->gridWidthM())/widget->columns();
  QPoint click((index%widget->columns())*width+width/2,(index/widget->columns())*widget->gridHeightM()+widget->gridHeightM()/2);QVERIFY(widget->rect().contains(click));
  QTest::mouseDClick(widget.get(),Qt::LeftButton,Qt::NoModifier,click);QTest::qWait(25);QCOMPARE(score->undoStack()->getCurIdx(),before+1);widget->hide();
  auto curves=TempoExpression::curves(score);QVERIFY(!curves.isEmpty());auto line=curves.back();QCOMPARE(line->beginText(),QString("poco rit."));QCOMPARE(line->continueText(),QString("continuation"));QCOMPARE(line->endText(),QString("end"));QVERIFY(!line->lineVisible());QCOMPARE(line->ritTarget(),65.);
  score->selection().setRange(start,finish,0,1);score->selection().updateSelectedElements();int current=score->undoStack()->getCurIdx();QVERIFY(!Palette::applyPaletteElement(rit));QCOMPARE(score->undoStack()->getCurIdx(),current);
  score->select(finish->firstElementForNavigation(0));restore->setXmlText("a tempo (back)");QVERIFY(Palette::applyPaletteElement(restore));
  TempoText* applied=nullptr;for(auto e:finish->annotations())if(e->isTempoText())applied=toTempoText(e);QVERIFY(applied);QCOMPARE(applied->restoreMode(),1);QCOMPARE(applied->xmlText(),QString("a tempo (back)"));
  EditData edit;score->undoRedo(true,&edit);for(auto e:finish->annotations())QVERIFY(!e->isTempoText());score->undoRedo(false,&edit);
  auto view=mscore->currentScoreView();QVERIFY(view);score->setLayoutAll();score->doLayout();
  auto point=finish->firstElementForNavigation(0)->canvasPos();QVERIFY(!applyPlayableTempoDrop(restore,point));
 }
