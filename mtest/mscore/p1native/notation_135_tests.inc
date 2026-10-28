 void tapFormattedTempo135_data(){
  QTest::addColumn<QString>("source");QTest::addColumn<QString>("expected");
  const QString quarter="<sym>metNoteQuarterUp</sym>",dot="<sym>metAugmentationDot</sym>";
  const QVector<QPair<QString,double>> units{{"<sym>metNote16thUp</sym>",.25},{"<sym>metNote8thUp</sym>",.5},{"<sym>metNote8thUp</sym>"+dot,.75},{quarter,1.},{quarter+dot,1.5},{"<sym>metNoteHalfUp</sym>",2.},{"<sym>metNoteHalfUp</sym>"+dot,3.},{"<sym>metNoteWhole</sym>",4.}};
  for(int i=0;i<units.size();++i){const auto& unit=units[i];const auto number=QString::number(150./unit.second,'f',2);
   QTest::newRow(qPrintable(QString("unit-%1").arg(i)))<<"Andante "+unit.first+" = 80"<<"Andante "+unit.first+" = "+number;
   QTest::newRow(qPrintable(QString("bold-unit-%1").arg(i)))<<"Andante "+unit.first+" = <b>80</b>"<<"Andante "+unit.first+" = <b>"+number+"</b>";
  }
  QTest::newRow("styled-symbol-and-dot")<<"Andante <b>"+quarter+"</b>"+dot+" = 80"<<"Andante <b>"+quarter+"</b>"+dot+" = 100.00";
  QTest::newRow("formatted-digits")<<quarter+" = 8<b>0</b>"<<quarter+" = 150.00<b></b>";
  QTest::newRow("font-size-and-value")<<"<font size=\"10\"/>"+quarter+" = <i>80</i>"<<"<font size=\"10\"/>"+quarter+" = <i>150.00</i>";
  QTest::newRow("approximately")<<quarter+" ≈ 80"<<quarter+" ≈ 150.00";
  QTest::newRow("ca")<<quarter+" = ca. 80"<<quarter+" = ca. 150.00";
  QTest::newRow("music-space")<<quarter+" =<sym>space</sym><b>80</b>"<<quarter+" =<sym>space</sym><b>150.00</b>";
  QTest::newRow("nonbreaking-space")<<quarter+" =&nbsp;<b>80</b>"<<quarter+" =&nbsp;<b>150.00</b>";
 }
 void tempoWorkspaceImport135(){
  const auto path=qEnvironmentVariable("D1_WORKSPACE_FIXTURE");if(path.isEmpty())QSKIP("Set D1_WORKSPACE_FIXTURE to a read-only workspace.xml copy.");
  QFile source(path);QVERIFY(source.open(QIODevice::ReadOnly));QDomDocument document;QVERIFY(document.setContent(source.readAll()));const auto nodes=document.elementsByTagName("Palette");std::unique_ptr<PalettePanel> palette;
  for(int i=0;i<nodes.size();++i)if(nodes.at(i).toElement().attribute("name")=="Tempo"){
   QDomDocument mime;mime.appendChild(mime.importNode(nodes.at(i),true));palette=PalettePanel::readMimeData(mime.toByteArray());break;
  }QVERIFY(palette);const int cells=palette->ncells();const auto grid=palette->gridSize();
  for(int pass=0;pass<3;++pass){int rit=0,restore=0;for(int i=0;i<palette->ncells();++i){auto cell=palette->cell(i);auto e=cell->element.get();if(!e)continue;
   if(!cell->custom&&e->isTextLine()&&toTextLine(e)->ritPlay()){++rit;QCOMPARE(toTextLine(e)->beginText(),QString("rit."));}
   if(!cell->custom&&e->isTempoText()&&toTempoText(e)->restoreMode()==1)++restore;
  }QCOMPARE(rit,1);QCOMPARE(restore,1);QCOMPARE(palette->ncells(),cells);QCOMPARE(palette->gridSize(),grid);palette=PalettePanel::readMimeData(palette->mimeData());QVERIFY(palette);}
  qInfo()<<"Actual workspace tempo cells / grid"<<cells<<grid;
 }
 void tapFormattedTempo135(){QFETCH(QString,source);QFETCH(QString,expected);QCOMPARE(tapTempoText(source,150.),expected);}
 void ritFactoryTargetStyle135(){
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));
  score->startCmd();score->undoChangeStyleVal(Sid::tempoFontSize,16.);score->endCmd();
  auto palette=std::unique_ptr<PalettePanel>(MuseScore::newTempoPalettePanel());QVERIFY(palette);const TextLine* source=nullptr;
  for(int i=0;i<palette->ncells();++i){auto e=palette->cell(i)->element.get();if(e->isTextLine()&&toTextLine(e)->ritPlay())source=toTextLine(e);}QVERIFY(source);
  QVERIFY(ParameterEdit::tempoCurve(score,0,960,nullptr,false,nullptr,source));auto curves=TempoExpression::curves(score);QCOMPARE(curves.size(),1);QCOMPARE(curves.front()->getProperty(Pid::BEGIN_FONT_SIZE).toDouble(),16.);
  curves.front()->styleChanged();QCOMPARE(curves.front()->getProperty(Pid::CONTINUE_FONT_SIZE).toDouble(),16.);
  std::unique_ptr<TextLine> manual(source->clone());manual->setProperty(Pid::BEGIN_FONT_SIZE,18.);manual->setPropertyFlags(Pid::BEGIN_FONT_SIZE,PropertyFlags::NOSTYLE);
  QVERIFY(ParameterEdit::tempoCurve(score,960,1920,nullptr,false,nullptr,manual.get()));curves=TempoExpression::curves(score);QCOMPARE(curves.size(),2);QCOMPARE(curves.back()->getProperty(Pid::BEGIN_FONT_SIZE).toDouble(),18.);
 }
 void tapSelectedTempo135_data(){QTest::addColumn<bool>("targetFirst");QTest::newRow("first")<<true;QTest::newRow("last")<<false;}
 void tapSelectedTempo135(){
  QFETCH(bool,targetFirst);
  auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);mscore->setCurrentScoreView(mscore->appendScore(score));mscore->resize(1120,760);mscore->show();
  auto segment=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);QVERIFY(segment);
  auto other=new TempoText(score);other->setParent(segment);other->setTrack(0);other->setFollowText(false);other->setXmlText("Upper <sym>metNoteQuarterUp</sym> = 120");other->setTempo(2.);
  auto target=new TempoText(score);target->setParent(segment);target->setTrack(4);target->setFollowText(false);target->setXmlText("Lower <b><sym>metNoteQuarterUp</sym></b><sym>metAugmentationDot</sym> = <i>60</i>");target->setTempo(1.5);
  score->startCmd();score->undoAddElement(targetFirst?target:other);score->undoAddElement(targetFirst?other:target);score->endCmd();mscore->endCmd();
  const auto originalOrder=segment->annotations();const double originalTempo=score->tempo(Fraction::fromTicks(480));
  const QString original=target->xmlText();const int before=score->undoStack()->getCurIdx();showTapMarkDialog(target);auto dialog=mscore->findChild<QDialog*>("tapMarkDialog");QVERIFY(dialog);QTest::qWait(30);
  auto close=qScopeGuard([&]{if(auto open=mscore->findChild<QDialog*>("tapMarkDialog"))open->reject();});
  auto unit=dialog->findChild<QComboBox*>("tapMarkUnit");QVERIFY(unit);const double initialUnit=unit->currentData().toDouble();unit->setCurrentIndex(unit->findData(1.5));
  for(int i=0;i<4;++i)QVERIFY(QMetaObject::invokeMethod(TapTempo::instance(mscore),"tapRecorded",Q_ARG(qint64,qint64(i)*600)));
  auto save=dialog->findChild<QPushButton*>("tapMarkSave");QVERIFY(save->isEnabled());QTest::mouseClick(save,Qt::LeftButton);QTest::qWait(30);
  QCOMPARE(target->tempo(),2.5);QCOMPARE(other->tempo(),2.);QCOMPARE(score->undoStack()->getCurIdx(),before+1);QVERIFY(target->xmlText().contains("<i>100.00</i>"));QVERIFY(!target->followText());
  QCOMPARE(score->tempo(Fraction::fromTicks(480)),2.5);
  const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",settings.path());
  const auto label=targetFirst?QString("first"):QString("last");
  auto saveMidi=[&](Score* source,const QString& suffix){QFile file(output+"/tap-selected-"+label+suffix+"-135.mid");if(!file.open(QIODevice::WriteOnly))return false;ExportMidi exporter(source);return exporter.write(&file,true,true,mscore->synthesizerState());};
  QVERIFY(saveMidi(score,QString()));
  QCOMPARE(initialUnit,1.5);
  EditData edit;score->undoRedo(true,&edit);QCOMPARE(target->tempo(),1.5);QCOMPARE(target->xmlText(),original);QVERIFY(segment->annotations()==originalOrder);QCOMPARE(score->tempo(Fraction::fromTicks(480)),originalTempo);
  score->undoRedo(false,&edit);QCOMPARE(target->tempo(),2.5);QCOMPARE(score->tempo(Fraction::fromTicks(480)),2.5);QCOMPARE(other->xmlText(),QString("Upper <sym>metNoteQuarterUp</sym> = 120"));
  const auto path=settings.filePath("tap-selected-135.mscz");QFileInfo file(path);QVERIFY(score->saveCompressedFile(file,false,false));auto loaded=mscore->readScore(path);QVERIFY(loaded);QCOMPARE(loaded->tempo(Fraction::fromTicks(480)),2.5);int found=0;
  for(auto e:loaded->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->annotations())if(e->isTempoText()){
   auto mark=toTempoText(e);if(mark->xmlText().startsWith("Lower ")){QCOMPARE(mark->tempo(),2.5);QVERIFY(mark->xmlText().contains("<i>100.00</i>"));++found;}else {QVERIFY(mark->xmlText().startsWith("Upper "));QCOMPARE(mark->tempo(),2.);}
  }QCOMPARE(found,1);QVERIFY(saveMidi(loaded,"-reopened"));delete loaded;
 }
