// Hand brackets are optional geometry on the four existing SMuFL symbols. GPL v2.
#include <limits>
void TestPerformanceEditor::handSymbolPlacementAndDefaults()
      {
      std::unique_ptr<MasterScore> score(readScore("mscore/performanceeditor/freeslur.mscx"));QVERIFY(score);score->doLayout();auto chord=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto add=[&](SymId id,Element* parent) {auto mark=new Symbol(score.get());mark->setSym(id);mark->applyHandPreset();mark->setParent(parent);mark->setTrack(parent->track());score->startCmd();score->undoAddElement(mark);score->endCmd();return mark;};
      auto right=add(SymId::keyboardPlayWithRH,chord->upNote()),left=add(SymId::keyboardPlayWithLH,chord);
      QVERIFY(right->pagePos().y()>chord->upNote()->pagePos().y());QVERIFY(left->gripsPositions()[2].y()<chord->upNote()->pagePos().y());
      auto beforeGrip=left->gripsPositions()[2];score->startCmd();left->undoChangeProperty(Pid::HAND_BRACKET_V,-1.5);score->endCmd();QVERIFY(std::abs(left->gripsPositions()[2].y()-beforeGrip.y()-1.*score->spatium())<.001);
      auto original=right->pos();score->startCmd();right->undoChangeProperty(Pid::OFFSET,QPointF(1,2)*score->spatium());score->endCmd();const auto manualPosition=original+QPointF(1,2)*score->spatium();QCOMPARE(right->pos(),manualPosition);for(int i=0;i<3;++i)score->doLayout();QCOMPARE(right->pos(),manualPosition);
      auto file=_directory.path()+"/hand-anchor.mscx";QVERIFY(saveScore(score.get(),file));std::unique_ptr<MasterScore> restored(readCreatedScore(file));QVERIFY(restored);auto restoredChord=toChord(restored->firstMeasure()->first(SegmentType::ChordRest)->element(0));Symbol* restoredRight=nullptr;for(auto e:restoredChord->upNote()->el())if(e->isSymbol() && !toSymbol(e)->handLeft())restoredRight=toSymbol(e);QVERIFY(restoredRight);QVERIFY(QLineF(restoredRight->pos(),manualPosition).length()<.001);
      score->startCmd();right->undoChangeProperty(Pid::AUTOPLACE,false);score->endCmd();QCOMPARE(right->pos(),manualPosition);
      auto normal=HandBracket::defaults(SymId::keyboardPlayWithRHEnd);QVERIFY(HandBracket::setDefaults(SymId::keyboardPlayWithRHEnd,{-1.5,-.8,.1,true}));Symbol end(score.get());end.setSym(SymId::keyboardPlayWithRHEnd);end.applyHandPreset();QCOMPARE(end.getProperty(Pid::HAND_BRACKET_H).toDouble(),-1.5);QVERIFY(end.getProperty(Pid::HAND_BRACKET_TEXT).toBool());QVERIFY(!HandBracket::setDefaults(end.sym(),{2,-1,4,false}));QVERIFY(HandBracket::setDefaults(end.sym(),normal));
      }
static QImage handImage(Symbol* symbol)
      {
      QImage image(500,300,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
      QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.translate(250,180);symbol->draw(&painter);return image;
      }
static Symbol* handOnChord(Ms::Chord* chord)
      {
      for (auto e:chord->el()) if (e->isSymbol() && toSymbol(e)->handEligible()) return toSymbol(e);
      return nullptr;
      }
void TestPerformanceEditor::handSymbolModelAndPersistence()
      {
      std::unique_ptr<MasterScore> score(readScore("mscore/performanceeditor/freeslur.mscx"));QVERIFY(score);score->doLayout();
      auto chord=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));const auto midi=freeSlurMidi(score.get());
      Symbol legacy(score.get());legacy.setSym(SymId::keyboardPlayWithRH);legacy.layout();auto image=handImage(&legacy);auto box=legacy.bbox();
      QVERIFY(!legacy.handBracket());QCOMPARE(legacy.gripsCount(),0);QVERIFY(legacy.setProperty(Pid::HAND_BRACKET_H,5.0));legacy.layout();QCOMPARE(handImage(&legacy),image);QCOMPARE(legacy.bbox(),box);
      QVERIFY(legacy.setProperty(Pid::HAND_BRACKET_MODE,true));legacy.layout();QVERIFY(handImage(&legacy)!=image);QCOMPARE(legacy.gripsCount(),3);
      for (auto id:{SymId::keyboardPlayWithRH,SymId::keyboardPlayWithLH,SymId::keyboardPlayWithRHEnd,SymId::keyboardPlayWithLHEnd}) {
            Symbol mark(score.get());mark.setSym(id);mark.applyHandPreset();mark.layout();QVERIFY(mark.handBracket());
            QCOMPARE(mark.getProperty(Pid::HAND_BRACKET_H).toDouble(),mark.handEnd() ? -.6:.6);
            auto grips=mark.gripsPositions();const qreal unit=mark.spatium()*mark.mag();
            QCOMPARE(grips[0].y()-mark.pagePos().y(),mark.handLeft() ? -2.5*unit:0.0);QCOMPARE(grips[1].y()-mark.pagePos().y(),mark.handLeft()?0.0:-2.5*unit);
            QVERIFY(mark.shape().contains(QPointF((grips[0].x()-mark.pagePos().x())/2,grips[0].y()-mark.pagePos().y())));
            }
      Symbol other(score.get());other.setSym(SymId::accidentalSharp);other.layout();auto ordinary=handImage(&other);other.setProperty(Pid::HAND_BRACKET_MODE,true);other.layout();QCOMPARE(handImage(&other),ordinary);QCOMPARE(other.gripsCount(),0);
      auto left=new Symbol(score.get());left->setSym(SymId::keyboardPlayWithLH);left->applyHandPreset();EditData drop;drop.dropElement=left;
      score->startCmd();QCOMPARE(chord->drop(drop),static_cast<Element*>(left));score->endCmd();QCOMPARE(left->parent(),static_cast<Element*>(chord));QCOMPARE(handOnChord(chord),left);
      auto right=new Symbol(score.get());right->setSym(SymId::keyboardPlayWithRH);right->applyHandPreset();drop.dropElement=right;
      score->startCmd();QCOMPARE(chord->downNote()->drop(drop),static_cast<Element*>(right));score->endCmd();QCOMPARE(right->parent(),static_cast<Element*>(chord->downNote()));
      auto start=score->undoStack()->getCurIdx();score->startCmd();left->undoChangeProperty(Pid::HAND_BRACKET_H,3.5);left->undoChangeProperty(Pid::HAND_BRACKET_V,-2.5);left->undoChangeProperty(Pid::HAND_BRACKET_TEXT,true);score->endCmd();QCOMPARE(score->undoStack()->getCurIdx(),start+1);QCOMPARE(freeSlurMidi(score.get()),midi);
      std::unique_ptr<Ms::Chord> clone(chord->clone());auto copied=handOnChord(clone.get());QVERIFY(copied);QCOMPARE(copied->getProperty(Pid::HAND_BRACKET_H),left->getProperty(Pid::HAND_BRACKET_H));QVERIFY(copied->getProperty(Pid::HAND_BRACKET_TEXT).toBool());QCOMPARE(copied->parent(),static_cast<Element*>(clone.get()));
      auto oldLowest=clone->downNote();auto replacement=new Note(*oldLowest);replacement->setPitch(oldLowest->pitch()+4);clone->add(replacement);clone->remove(oldLowest);delete oldLowest;QCOMPARE(handOnChord(clone.get()),copied);
      const auto old=left->getProperty(Pid::HAND_BRACKET_V);
      for (auto invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),41.0,-41.0}) {QVERIFY(!left->setProperty(Pid::HAND_BRACKET_V,invalid));QCOMPARE(left->getProperty(Pid::HAND_BRACKET_V),old);}
      QVERIFY(!left->setProperty(Pid::HAND_BRACKET_WIDTH,.01));QVERIFY(!left->setProperty(Pid::HAND_BRACKET_WIDTH,2.1));
      score->startCmd();score->insertMeasure(ElementType::VBOX,score->first());score->endCmd();auto part=new Score(score.get());auto excerpt=new Excerpt(score.get());excerpt->setPartScore(part);excerpt->setParts({score->parts().front()});excerpt->setTitle("Piano");score->startCmd();Excerpt::createExcerpt(excerpt);score->excerpts().append(excerpt);score->endCmd();part->doLayout();
      auto linked=handOnChord(toChord(part->firstMeasure()->first(SegmentType::ChordRest)->element(0)));QVERIFY(linked);QCOMPARE(linked->getProperty(Pid::HAND_BRACKET_H),left->getProperty(Pid::HAND_BRACKET_H));QVERIFY(left->linkList().size()>1);
      score->startCmd();left->undoChangeProperty(Pid::HAND_BRACKET_H,-4.0);score->endCmd();QCOMPARE(linked->getProperty(Pid::HAND_BRACKET_H).toDouble(),-4.0);score->undoRedo(true,&ed);QCOMPARE(left->getProperty(Pid::HAND_BRACKET_H).toDouble(),3.5);QCOMPARE(linked->getProperty(Pid::HAND_BRACKET_H).toDouble(),3.5);
      QTemporaryDir directory;QVERIFY(directory.isValid());
      for (const char* ext:{"mscx","mscz"}) {
            QString file=directory.path()+"/hands."+ext;if(QString(ext)=="mscz") {QFileInfo info(file);QVERIFY(score->saveCompressedFile(info,false,false));}else QVERIFY(saveScore(score.get(),file));
            std::unique_ptr<MasterScore> restored(readCreatedScore(file));QVERIFY(restored);auto c=toChord(restored->firstMeasure()->first(SegmentType::ChordRest)->element(0));auto mark=handOnChord(c);QVERIFY(mark);QVERIFY(mark->handBracket());QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),3.5);QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_V).toDouble(),-2.5);QVERIFY(mark->getProperty(Pid::HAND_BRACKET_TEXT).toBool());
            bool noteMark=false;for (auto e:c->downNote()->el()) if(e->isSymbol()) {noteMark=true;QVERIFY(toSymbol(e)->handBracket());}QVERIFY(noteMark);QCOMPARE(freeSlurMidi(restored.get()),midi);
            }
      auto originalSp=score->spatium();auto originalWidth=left->bbox().width();score->setSpatium(originalSp*1.5);score->doLayout();QCOMPARE(left->getProperty(Pid::HAND_BRACKET_H).toDouble(),3.5);QVERIFY(left->bbox().width()>originalWidth*1.4);score->setSpatium(originalSp);score->doLayout();
      auto line=new LayoutBreak(score.get());line->setLayoutBreakType(LayoutBreak::Type::LINE);line->setParent(score->firstMeasure());score->startCmd();score->undoAddElement(line);score->endCmd();QCOMPARE(left->parent(),static_cast<Element*>(chord));QCOMPARE(left->getProperty(Pid::HAND_BRACKET_V).toDouble(),-2.5);QCOMPARE(freeSlurMidi(score.get()),midi);
      score->select(left);QCOMPARE(chord->nextElement(),static_cast<Element*>(chord->downNote()));
      score->startCmd();score->undoRemoveElement(left);score->endCmd();QVERIFY(!handOnChord(chord));score->undoRedo(true,&ed);QCOMPARE(handOnChord(chord),left);
      }
void TestPerformanceEditor::handSymbolInspectorAndGrips()
      {
      if(!Ms::mscore) {QCoreApplication::setOrganizationName("PerformanceRegression");QCoreApplication::setApplicationName("HandSymbolInspector");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,_directory.path());dataPath=_directory.path();MScore::noGui=true;noSeq=true;converterMode=true;initMuseScoreResources();QStringList args;MuseScore::init(args);}
      auto main=Ms::mscore;main->resize(1250,850);main->show();
      std::unique_ptr<PalettePanel> palette(MuseScore::newFingeringPalettePanel());int hands=0;
      for(int i=0;i<palette->ncells();++i) {auto cell=palette->cell(i);if(!cell->element || !cell->element->isSymbol())continue;auto symbol=toSymbol(cell->element.get());if(!symbol->handEligible())continue;++hands;QVERIFY(symbol->handBracket());QCOMPARE(cell->mag,.55);auto restored=PaletteCell::readMimeData(cell->mimeData());QVERIFY(restored&&restored->element);QVERIFY(toSymbol(restored->element.get())->handBracket());}QCOMPARE(hands,4);
      const auto cellCount=palette->ncells();completeHandBracketPalette(palette.get());QCOMPARE(palette->ncells(),cellCount);
      auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/performanceeditor/freeslur.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));auto chord=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));auto mark=new Symbol(score);mark->setSym(SymId::keyboardPlayWithLH);mark->setParent(chord->downNote());mark->setTrack(0);score->startCmd();score->undoAddElement(mark);score->endCmd();
      auto select=[&] {score->startCmd();score->select(mark);score->endCmd();getAction("inspector")->setChecked(true);main->cmd(getAction("inspector"));QTest::qWait(40);};select();
      auto inspector=main->findChild<Inspector*>();QVERIFY(inspector);auto stack=inspector->findChild<QStackedWidget*>();QVERIFY(stack);auto currentPanel=[&] {return qobject_cast<InspectorHandSymbol*>(stack->currentWidget());};
      auto panel=currentPanel();QVERIFY(panel);auto preset=panel->findChild<QPushButton*>("handBracketPreset");QVERIFY(preset);auto before=score->undoStack()->getCurIdx();QTest::mouseClick(preset,Qt::LeftButton);QTRY_VERIFY(mark->handBracket());QCOMPARE(score->undoStack()->getCurIdx(),before+1);score->undoRedo(true,&ed);QVERIFY(!mark->handBracket());score->undoRedo(false,&ed);select();
      panel=currentPanel();QVERIFY(panel);auto spin=panel->findChild<QDoubleSpinBox*>("handBracketH");auto label=panel->findChild<QLabel*>("handBracketHLabel");QVERIFY(spin&&label);QVERIFY(spin->isEnabled());QVERIFY(spin->minimum()>0);QCOMPARE(panel->findChild<QDoubleSpinBox*>("handBracketV")->value(),1.0);QPoint p=label->rect().center(),to=p+QPoint(20,0);
      before=score->undoStack()->getCurIdx();QTest::mousePress(label,Qt::LeftButton,Qt::NoModifier,p);QMouseEvent motion(QEvent::MouseMove,to,to,label->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);QApplication::sendEvent(label,&motion);QCOMPARE(spin->value(),2.2);QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),2.0);QTest::keyClick(label,Qt::Key_Escape);QTest::mouseRelease(label,Qt::LeftButton,Qt::NoModifier,to);QCOMPARE(score->undoStack()->getCurIdx(),before);
      QTest::mousePress(label,Qt::LeftButton,Qt::NoModifier,p);QApplication::sendEvent(label,&motion);QTest::mouseRelease(label,Qt::LeftButton,Qt::NoModifier,to);QTRY_COMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),2.2);QCOMPARE(score->undoStack()->getCurIdx(),before+1);
      spin->setFocus();spin->selectAll();QTest::keyClicks(spin,"3.5");QTest::keyClick(spin,Qt::Key_Return);QTRY_COMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),3.5);
      auto flip=panel->findChild<QPushButton*>("handBracketFlipH");QVERIFY(flip);QTest::mouseClick(flip,Qt::LeftButton);QTRY_COMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),-3.5);score->undoRedo(true,&ed);QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),3.5);
      select();auto view=main->currentScoreView();view->setFocus();
      const qreal unit=mark->spatium()*mark->mag();auto hit=view->matrix().map(mark->canvasPos()+QPointF(1.75*unit,-unit)).toPoint();
      QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,hit);QTest::mouseDClick(view,Qt::LeftButton,Qt::NoModifier,hit);QCOMPARE(view->getEditData().element,static_cast<Element*>(mark));
      view->getEditData().curGrip=Grip::END;view->updateGrips();QCOMPARE(view->getEditData().grips,3);view->setFocus();auto original=mark->getProperty(Pid::HAND_BRACKET_V);before=score->undoStack()->getCurIdx();QTest::keyClick(view,Qt::Key_Up);QVERIFY(mark->getProperty(Pid::HAND_BRACKET_V)!=original);QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),3.5);QCOMPARE(score->undoStack()->getCurIdx(),before+1);score->undoRedo(true,&ed);QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_V),original);
      view->startEdit(mark,Grip::END);view->updateGrips();QPoint handle=view->matrix().map(view->getEditData().grip[1].center()).toPoint(),moved=handle+QPoint(0,-25);
      auto drag=[&] {QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,handle);QCOMPARE(int(view->getEditData().curGrip),1);QMouseEvent move(QEvent::MouseMove,moved,moved,view->mapToGlobal(moved),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&move);QApplication::sendEvent(view,&move);};before=score->undoStack()->getCurIdx();drag();QVERIFY(mark->getProperty(Pid::HAND_BRACKET_V)!=original);QTest::keyClick(view,Qt::Key_Escape);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,moved);QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_V),original);QCOMPARE(score->undoStack()->getCurIdx(),before);
      drag();QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,moved);QVERIFY(mark->getProperty(Pid::HAND_BRACKET_V)!=original);QCOMPARE(score->undoStack()->getCurIdx(),before+1);score->undoRedo(true,&ed);QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_V),original);view->changeState(ViewState::NORMAL);select();
      panel=currentPanel();QVERIFY(panel);spin=panel->findChild<QDoubleSpinBox*>("handBracketH");auto vertical=panel->findChild<QDoubleSpinBox*>("handBracketV");score->setIsPlaying(true);spin->setValue(4);vertical->setValue(2.5);flip=panel->findChild<QPushButton*>("handBracketFlipH");QVERIFY(flip);QTest::mouseClick(flip,Qt::LeftButton);QTest::mouseClick(flip,Qt::LeftButton);QVERIFY(main->performanceEditor()->hasPending());QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),3.5);score->setIsPlaying(false);QTRY_COMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),4.0);QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_V).toDouble(),-2.5);
      auto text=panel->findChild<QCheckBox*>("handBracketText");QTest::mouseClick(text,Qt::LeftButton,Qt::NoModifier,QPoint(8,text->height()/2));QVERIFY(mark->getProperty(Pid::HAND_BRACKET_TEXT).toBool());
      const SymId allHands[]={SymId::keyboardPlayWithLH,SymId::keyboardPlayWithLHEnd,SymId::keyboardPlayWithRH,SymId::keyboardPlayWithRHEnd};QVector<HandBracket::Style> savedStyles;for(auto id:allHands)savedStyles.append(HandBracket::defaults(id));
      auto savedStyle=HandBracket::defaults(mark->sym());auto saveDefault=panel->findChild<QPushButton*>("handBracketSaveDefault");auto useDefault=panel->findChild<QPushButton*>("handBracketUseDefault");QVERIFY(saveDefault&&useDefault);
      QTest::mouseClick(saveDefault,Qt::LeftButton);QCOMPARE(HandBracket::defaults(mark->sym()).horizontal,4.0);
      bool newDefaultFound=false;
      for(auto paletteModel:main->getPaletteWorkspace()->paletteModels()) {
      if(!paletteModel)continue;
      for(int row=0;row<paletteModel->rowCount() && !newDefaultFound;++row) {
            auto parent=paletteModel->index(row,0);
            for(int column=0;column<paletteModel->rowCount(parent);++column) {
                  auto cell=paletteModel->findCell(paletteModel->index(column,0,parent));
                  if(!cell || cell->custom || !cell->element || !cell->element->isSymbol())continue;
                  auto prototype=toSymbol(cell->element.get());if(prototype->sym()!=mark->sym() || !prototype->handBracket())continue;
                  QCOMPARE(prototype->getProperty(Pid::HAND_BRACKET_H).toDouble(),4.0);
                  auto added=prototype->clone();EditData drop;drop.dropElement=added;score->startCmd();chord->downNote()->drop(drop);score->endCmd();
                  QCOMPARE(added->getProperty(Pid::HAND_BRACKET_H).toDouble(),4.0);score->undoRedo(true,&ed);newDefaultFound=true;break;
                  }
            }
      }
      QVERIFY(newDefaultFound);QVERIFY(panel->findChild<QLabel*>("handBracketDefaultStatus")->text().startsWith(QString::fromUtf8("已保存")));
      QVERIFY(HandBracket::setDefaults(mark->sym(),{2,-1,.12,false}));loadHandBracketDefaults();QCOMPARE(HandBracket::defaults(mark->sym()).horizontal,4.0);
      score->startCmd();mark->undoChangeProperty(Pid::HAND_BRACKET_H,3.0);score->endCmd();select();panel=currentPanel();QVERIFY(panel);useDefault=panel->findChild<QPushButton*>("handBracketUseDefault");before=score->undoStack()->getCurIdx();QTest::mouseClick(useDefault,Qt::LeftButton);QTRY_COMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),4.0);QCOMPARE(score->undoStack()->getCurIdx(),before+1);
      QSettings handSettings;handSettings.remove(QString("handBracket/%1").arg(Sym::id2name(mark->sym())));handSettings.sync();QVERIFY(HandBracket::setDefaults(mark->sym(),savedStyle));for(int i=0;i<4;++i){handSettings.remove(QString("handBracket/%1").arg(Sym::id2name(allHands[i])));QVERIFY(HandBracket::setDefaults(allHands[i],savedStyles[i]));}handSettings.sync();
      auto right=new Symbol(score);right->setSym(SymId::keyboardPlayWithRH);right->applyHandPreset();right->setParent(toChord(score->firstMeasure()->findSegment(SegmentType::ChordRest,Fraction::fromTicks(480))->element(0)));right->setTrack(0);right->setProperty(Pid::HAND_BRACKET_TEXT,true);score->startCmd();score->undoAddElement(right);score->endCmd();
      view->setFocus();auto rightHit=view->matrix().map(right->canvasPos()+QPointF(0,-.5*right->spatium()*right->mag())).toPoint();
      QCOMPARE(view->elementNear(view->toLogical(rightHit)),static_cast<Element*>(right));
      QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,rightHit);QTest::mouseDClick(view,Qt::LeftButton,Qt::NoModifier,rightHit);QCOMPARE(view->getEditData().element,static_cast<Element*>(right));QCOMPARE(view->getEditData().grips,4);view->updateGrips();
      const auto textOriginal=right->getProperty(Pid::HAND_TEXT_OFFSET);const auto lineOriginal=right->offset();const int textUndo=score->undoStack()->getCurIdx();
      view->adjustCanvasPosition(right,false);QTest::qWait(60);view->updateGrips();
      auto textPoint=view->matrix().map(view->getEditData().grip[3].center()).toPoint(),textMoved=textPoint+QPoint(18,10);
      QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,textPoint);QCOMPARE(int(view->getEditData().curGrip),3);QMouseEvent textMotion(QEvent::MouseMove,textMoved,textMoved,view->mapToGlobal(textMoved),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&textMotion);QApplication::sendEvent(view,&textMotion);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,textMoved);
      QVERIFY(right->getProperty(Pid::HAND_TEXT_OFFSET)!=textOriginal);QCOMPARE(right->offset(),lineOriginal);QVERIFY(!right->getProperty(Pid::HAND_TEXT_AUTO).toBool());QCOMPARE(score->undoStack()->getCurIdx(),textUndo+1);score->undoRedo(true,&ed);QCOMPARE(right->getProperty(Pid::HAND_TEXT_OFFSET),textOriginal);QVERIFY(right->getProperty(Pid::HAND_TEXT_AUTO).toBool());
      view->changeState(ViewState::NORMAL);select();panel=currentPanel();QVERIFY(panel);
      panel->grab().save("hand-symbol-inspector.png");view->startEdit(mark,Grip::START);QTest::qWait(50);view->grab().save("hand-symbol-edit.png");view->changeState(ViewState::NORMAL);
      QVERIFY(saveScore(score,"hand-symbol-validation.mscx"));QVERIFY(main->savePdf(score,"hand-symbol.pdf"));QFile svg("hand-symbol.svg");QVERIFY(svg.open(QIODevice::WriteOnly));QVERIFY(main->saveSvg(score,&svg));svg.close();
      }

void TestPerformanceEditor::restoredTempoLayout()
      {
      std::unique_ptr<MasterScore> score(readScore("mscore/performanceeditor/freeslur.mscx"));QVERIFY(score);score->doLayout();auto segment=score->firstMeasure()->first(SegmentType::ChordRest);
      auto tempo=new TempoText(score.get());tempo->setParent(segment);tempo->setTrack(0);tempo->setXmlText("a tempo");tempo->setProperty(Pid::TEMPO_RESTORE_MODE,1);score->startCmd();score->undoAddElement(tempo);score->endCmd();QVERIFY(std::abs(tempo->x())<.01*score->spatium());
      score->startCmd();static_cast<Element*>(tempo)->undoChangeProperty(Pid::AUTOPLACE,false,PropertyFlags::UNSTYLED);static_cast<Element*>(tempo)->undoChangeProperty(Pid::OFFSET,QPointF(2,-7)*score->spatium(),PropertyFlags::UNSTYLED);score->endCmd();const auto offset=tempo->offset();score->doLayout();QCOMPARE(tempo->offset(),offset);
      }
