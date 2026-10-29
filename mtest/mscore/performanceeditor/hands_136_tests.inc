#include "mscore/miconengine.h"
static QtMessageHandler iconPrevious136=nullptr;
static int iconWarnings136=0;
static void iconMessages136(QtMsgType type,const QMessageLogContext& context,const QString& message)
{
    if(type==QtWarningMsg&&message.startsWith("QPainter::"))++iconWarnings136;
    if(iconPrevious136)iconPrevious136(type,context,message);
}
void TestPerformanceEditor::iconEmptySize136()
{
    const auto path=_directory.filePath("icon-empty-size-136.svg");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"><path fill=\"#112233\" d=\"M2 2h12v12H2z\"/></svg>");file.close();
    MIconEngine icon;icon.addFile(path,QSize(),QIcon::Normal,QIcon::Off);iconWarnings136=0;iconPrevious136=qInstallMessageHandler(iconMessages136);auto restore=qScopeGuard([]{qInstallMessageHandler(iconPrevious136);});
    for(auto size:{QSize(0,16),QSize(16,0),QSize(-1,16),QSize(0,0)})QVERIFY(icon.pixmap(size,QIcon::Normal,QIcon::Off).isNull());
    const auto normal=icon.pixmap(QSize(16,16),QIcon::Normal,QIcon::Off);QVERIFY(!normal.isNull());QCOMPARE(normal.size(),QSize(16,16));QCOMPARE(normal.toImage().pixelColor(8,8),QColor("#112233"));QCOMPARE(iconWarnings136,0);
}
static MasterScore* handFixture136(MuseScore* main,const QString& path,int pitch,bool crossing,bool chordTone)
{
    const int spelling[]{14,21,16,11,18,13,20,15,22,17,12,19};
    QString xml="<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff><Staff id=\"2\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part>";
    for(int staff=1;staff<=2;++staff){xml+=QString("<Staff id=\"%1\"><Measure><voice><Clef><concertClefType>%2</concertClefType><transposingClefType>%2</transposingClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>").arg(staff).arg(staff==1?"G":"F");
        for(int beat=0;beat<4;++beat){
            if(staff==2){xml+="<Rest><durationType>quarter</durationType></Rest>";continue;}
            xml+="<Chord><durationType>quarter</durationType>";if(crossing)xml+="<staffMove>1</staffMove>";
            for(int value:chordTone?QVector<int>{pitch-4,pitch,pitch+3}:QVector<int>{pitch})xml+=QString("<Note><pitch>%1</pitch><tpc>%2</tpc></Note>").arg(value).arg(spelling[value%12]);
            xml+="</Chord>";
        }xml+="</voice></Measure></Staff>";
    }xml+="</Score></museScore>";QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(xml.toUtf8())<0)return nullptr;file.close();
    auto score=main->readScore(path);if(score)main->setCurrentScoreView(main->appendScore(score));return score;
}
void TestPerformanceEditor::handPlacementRange136_data()
{
    QTest::addColumn<int>("pitch");QTest::addColumn<bool>("crossing");QTest::addColumn<bool>("chordTone");QTest::addColumn<int>("role");
    struct Example{const char* name;int pitch;bool crossing;bool chordTone;};
    const Example examples[]={{"line",71,false,false},{"space",69,false,false},{"lower-ledger",60,false,false},{"lower-ledger-space",59,false,false},{"upper-ledger",81,false,false},{"upper-space",79,false,false},{"inner-chord",71,false,true},{"cross-staff",60,true,false}};
    for(const auto& e:examples)for(int role=0;role<4;++role)QTest::newRow(qPrintable(QString("%1-%2").arg(e.name).arg(role)))<<e.pitch<<e.crossing<<e.chordTone<<role;
}
void TestPerformanceEditor::handPlacementRange136()
{
    QFETCH(int,pitch);QFETCH(bool,crossing);QFETCH(bool,chordTone);QFETCH(int,role);auto main=Ms::mscore;QVERIFY(main);
    auto score=handFixture136(main,_directory.filePath("hand-position-136.mscx"),pitch,crossing,chordTone);QVERIFY(score);
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->next1(SegmentType::ChordRest)->element(0));QVERIFY(chord);
    Note* target=nullptr;for(auto note:chord->notes())if(note->pitch()==pitch)target=note;QVERIFY(target);
    const SymId roles[]{SymId::keyboardPlayWithLH,SymId::keyboardPlayWithLHEnd,SymId::keyboardPlayWithRH,SymId::keyboardPlayWithRHEnd};
    auto mark=new Symbol(score);mark->setSym(roles[role]);mark->applyHandPreset();mark->setParent(target);mark->setTrack(target->track());
    const auto originalMidi=freeSlurMidi(score);score->startCmd();score->undoAddElement(mark);score->endCmd();main->endCmd();score->doLayout();
    const auto head=target->canvasBoundingRect();const auto grips=mark->gripsPositions();const double sp=mark->spatium()*mark->mag();
    const double gap=mark->handEnd()?grips[0].x()-target->pagePos().x()-target->bbox().right():target->pagePos().x()+target->bbox().left()-grips[0].x();
    QVERIFY2(gap>=.14*sp&&gap<=.91*sp,qPrintable(QString("Horizontal gap %1 sp").arg(gap/sp)));
    const double expectedEdge=target->pagePos().y()+(mark->handLeft()?target->bbox().top():target->bbox().bottom());
    QVERIFY(mark->handLeft()?grips[2].y()<expectedEdge:grips[2].y()>expectedEdge);
    const double staffTop=chord->measure()->system()->staffYpage(chord->vStaffIdx());
    const double space=(grips[2].y()-staffTop)/chord->spatium()-.5;QVERIFY(qAbs(space-qRound(space))<.001);
    QCOMPARE(freeSlurMidi(score),originalMidi);const auto position=mark->pagePos();for(int i=0;i<3;++i)score->doLayout();QVERIFY(QLineF(position,mark->pagePos()).length()<.001);
    const auto saved=_directory.filePath("hand-position-saved-136.mscz");QFileInfo info(saved);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> loaded(main->readScore(saved));QVERIFY(loaded);loaded->doLayout();
    auto newChord=toChord(loaded->firstSegment(SegmentType::ChordRest)->next1(SegmentType::ChordRest)->element(0));Symbol* found=nullptr;
    for(auto note:newChord->notes())for(auto e:note->el())if(e->isSymbol()&&toSymbol(e)->handEligible()){found=toSymbol(e);QCOMPARE(note->pitch(),pitch);}
    QVERIFY(found);QCOMPARE(found->sym(),mark->sym());QVERIFY2(QLineF(found->pagePos(),position).length()<.001,qPrintable(QString("Original %1,%2; reopened %3,%4").arg(position.x()).arg(position.y()).arg(found->pagePos().x()).arg(found->pagePos().y())));QCOMPARE(freeSlurMidi(loaded.get()),originalMidi);
    auto changed=toNote(found->parent());const int spelling[]{14,21,16,11,18,13,20,15,22,17,12,19};
    loaded->startCmd();loaded->undoChangePitch(changed,pitch+2,spelling[(pitch+2)%12],spelling[(pitch+2)%12]);loaded->endCmd();loaded->doLayout();
    QVERIFY(QLineF(found->pos(),HandBracket::position(found)).length()<.001);EditData edit;loaded->undoRedo(true,&edit);loaded->doLayout();QVERIFY(QLineF(found->pagePos(),position).length()<.001);
    main->showPerformanceEditor(false);main->resize(1120,760);main->show();auto view=main->currentScoreView();view->adjustCanvasPosition(target,false);QTest::qWait(20);
    const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path());const auto rect=view->toPhysical(score->firstMeasure()->canvasBoundingRect()).adjusted(-20,-35,20,35).intersected(view->rect());
    QVERIFY(view->grab(rect).save(output+QString("/hand-%1-%2-%3-136.png").arg(pitch).arg(crossing?"cross":chordTone?"chord":"single").arg(role)));
    if(crossing&&role==3){QFileInfo copy(output+"/hand-cross-staff-136.mscz");QVERIFY(score->saveCompressedFile(copy,false,false));}
}
void TestPerformanceEditor::handBasicReset136()
{
    auto main=Ms::mscore;QVERIFY(main);auto score=handFixture136(main,_directory.filePath("hand-basic-136.mscx"),69,false,false);QVERIFY(score);
    const SymId roles[]{SymId::keyboardPlayWithLH,SymId::keyboardPlayWithLHEnd,SymId::keyboardPlayWithRH,SymId::keyboardPlayWithRHEnd};
    QVector<HandBracket::Style> originals;QSettings settings;QVariantMap oldValues;
    for(auto role:roles){originals.append(HandBracket::defaults(role));const auto prefix=QString("handBracket/%1/").arg(Sym::id2name(role));for(auto key:settings.allKeys())if(key.startsWith(prefix))oldValues[key]=settings.value(key);}
    auto restore=qScopeGuard([&]{for(int i=0;i<4;++i){settings.remove(QString("handBracket/%1").arg(Sym::id2name(roles[i])));HandBracket::setDefaults(roles[i],originals[i]);}for(auto it=oldValues.cbegin();it!=oldValues.cend();++it)settings.setValue(it.key(),it.value());settings.sync();loadHandBracketDefaults();});
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(0));auto mark=new Symbol(score);mark->setSym(SymId::keyboardPlayWithRH);mark->applyHandPreset();mark->setParent(chord->upNote());mark->setTrack(0);
    score->startCmd();score->undoAddElement(mark);mark->undoChangeProperty(Pid::HAND_BRACKET_H,1.2);mark->undoChangeProperty(Pid::HAND_BRACKET_V,-3.);score->endCmd();main->endCmd();
    auto select=[&]{score->select(mark,SelectType::SINGLE);getAction("inspector")->setChecked(true);main->cmd(getAction("inspector"));QApplication::processEvents();};select();
    auto inspector=main->findChild<Inspector*>();QVERIFY(inspector);auto stack=inspector->findChild<QStackedWidget*>();QVERIFY(stack);auto panel=qobject_cast<InspectorHandSymbol*>(stack->currentWidget());QVERIFY(panel);
    auto save=panel->findChild<QPushButton*>("handBracketSaveDefault");QVERIFY(save);QTest::mouseClick(save,Qt::LeftButton);
    for(auto role:roles){const auto value=HandBracket::defaults(role);QCOMPARE(qAbs(value.horizontal),1.2);QCOMPARE(value.vertical,-3.);QCOMPARE(value.horizontal<0,role==SymId::keyboardPlayWithLHEnd||role==SymId::keyboardPlayWithRHEnd);}
    auto preset=panel->findChild<QPushButton*>("handBracketPreset");QVERIFY(preset);const int undo=score->undoStack()->getCurIdx();QTest::mouseClick(preset,Qt::LeftButton);QTRY_COMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),.6);QTRY_VERIFY(!main->performanceEditor()->hasPending());QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_V).toDouble(),-2.5);QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
    QCOMPARE(HandBracket::defaults(mark->sym()).horizontal,1.2);select();panel=qobject_cast<InspectorHandSymbol*>(stack->currentWidget());QVERIFY(panel);
    auto horizontal=panel->findChild<QDoubleSpinBox*>("handBracketH"),vertical=panel->findChild<QDoubleSpinBox*>("handBracketV");QVERIFY(horizontal&&vertical);
    auto resetFor=[](QDoubleSpinBox* spin){auto grid=qobject_cast<QGridLayout*>(spin->parentWidget()->layout());if(!grid)return static_cast<QToolButton*>(nullptr);int row,column,rows,columns;grid->getItemPosition(grid->indexOf(spin),&row,&column,&rows,&columns);auto item=grid->itemAtPosition(row,2);return item?qobject_cast<QToolButton*>(item->widget()):nullptr;};
    auto hReset=resetFor(horizontal),vReset=resetFor(vertical);QVERIFY(hReset&&vReset);
    QTest::mouseClick(hReset,Qt::LeftButton);QTRY_VERIFY(!main->performanceEditor()->hasPending());QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_H).toDouble(),1.2);
    QTest::mouseClick(vReset,Qt::LeftButton);QTRY_VERIFY(!main->performanceEditor()->hasPending());QCOMPARE(mark->getProperty(Pid::HAND_BRACKET_V).toDouble(),-3.);
    EditData edit;score->undoRedo(true,&edit);score->undoRedo(true,&edit);score->undoRedo(false,&edit);score->undoRedo(false,&edit);
    const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path());QVERIFY(panel->grab().save(output+"/hand-controls-136.png"));
}
void TestPerformanceEditor::handLegacyAutomaticRecovery136()
{
    auto main=Ms::mscore;QVERIFY(main);const SymId roles[]{SymId::keyboardPlayWithLH,SymId::keyboardPlayWithLHEnd,SymId::keyboardPlayWithRH,SymId::keyboardPlayWithRHEnd};
    for(auto role:roles){
        auto score=handFixture136(main,_directory.filePath("hand-legacy-source-136.mscx"),69,false,false);QVERIFY(score);auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(0));
        auto mark=new Symbol(score);mark->setSym(role);mark->applyHandPreset();mark->setParent(chord->upNote());mark->setTrack(0);score->startCmd();score->undoAddElement(mark);score->endCmd();main->endCmd();const auto automatic=mark->pagePos();
        const auto xmlPath=_directory.filePath("hand-legacy-copy-136.mscx");QVERIFY(saveScore(score,xmlPath));QFile file(xmlPath);QVERIFY(file.open(QIODevice::ReadOnly));auto xml=file.readAll();file.close();QVERIFY(xml.contains("<handPlacement>1</handPlacement>"));xml.replace("<handPlacement>1</handPlacement>","");QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(xml)==xml.size());file.close();
        std::unique_ptr<MasterScore> old(main->readScore(xmlPath));QVERIFY(old);old->doLayout();auto oldChord=toChord(old->firstSegment(SegmentType::ChordRest)->element(0));Symbol* restored=nullptr;for(auto e:oldChord->upNote()->el())if(e->isSymbol()&&toSymbol(e)->handEligible())restored=toSymbol(e);QVERIFY(restored);
        old->startCmd();restored->undoChangeProperty(Pid::AUTOPLACE,false);old->endCmd();old->startCmd();restored->undoChangeProperty(Pid::AUTOPLACE,true);old->endCmd();old->doLayout();QVERIFY(QLineF(restored->pagePos(),automatic).length()<.001);
        const auto saved=_directory.filePath("hand-legacy-recovered-136.mscz");QFileInfo info(saved);QVERIFY(old->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> reopened(main->readScore(saved));QVERIFY(reopened);reopened->doLayout();auto newChord=toChord(reopened->firstSegment(SegmentType::ChordRest)->element(0));Symbol* result=nullptr;for(auto e:newChord->upNote()->el())if(e->isSymbol()&&toSymbol(e)->handEligible())result=toSymbol(e);QVERIFY(result);QVERIFY(QLineF(result->pagePos(),automatic).length()<.001);
    }
}
void TestPerformanceEditor::handLegacyConversion136_data()
{
    QTest::addColumn<int>("role");for(int role=0;role<4;++role)QTest::newRow(qPrintable(QString::number(role)))<<role;
}
void TestPerformanceEditor::handLegacyConversion136()
{
    QFETCH(int,role);auto main=Ms::mscore;QVERIFY(main);auto score=handFixture136(main,_directory.filePath("hand-legacy-136.mscx"),69,false,false);QVERIFY(score);
    const SymId roles[]{SymId::keyboardPlayWithLH,SymId::keyboardPlayWithLHEnd,SymId::keyboardPlayWithRH,SymId::keyboardPlayWithRHEnd};
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(0));auto mark=new Symbol(score);mark->setSym(roles[role]);mark->setParent(chord->upNote());mark->setTrack(0);
    score->startCmd();score->undoAddElement(mark);score->endCmd();main->endCmd();score->select(mark,SelectType::SINGLE);
    getAction("inspector")->setChecked(true);main->cmd(getAction("inspector"));QTest::qWait(40);
    auto inspector=main->findChild<Inspector*>();QVERIFY(inspector);auto stack=inspector->findChild<QStackedWidget*>();QVERIFY(stack);auto panel=qobject_cast<InspectorHandSymbol*>(stack->currentWidget());QVERIFY(panel);
    auto mode=panel->findChild<QCheckBox*>("handBracketMode");QVERIFY(mode&&!mode->isChecked());const auto before=mark->canvasBoundingRect();const int undo=score->undoStack()->getCurIdx();
    QTest::mouseClick(mode,Qt::LeftButton,Qt::NoModifier,QPoint(8,mode->height()/2));QTRY_VERIFY(mark->handBracket()&&!main->performanceEditor()->hasPending());main->endCmd();
    const auto after=mark->canvasBoundingRect();const double sp=mark->spatium()*mark->mag();
    QVERIFY2(QLineF(before.center(),after.center()).length()<.15*sp,qPrintable(QString("Old/new centers %1,%2 -> %3,%4").arg(before.center().x()).arg(before.center().y()).arg(after.center().x()).arg(after.center().y())));
    QVERIFY(qAbs(before.width()-after.width())<.15*sp);QVERIFY(qAbs(before.height()-after.height())<.15*sp);QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
    EditData edit;score->undoRedo(true,&edit);QVERIFY(!mark->handBracket());QVERIFY(QLineF(before.center(),mark->canvasBoundingRect().center()).length()<.001);score->undoRedo(false,&edit);QVERIFY(mark->handBracket());QVERIFY(QLineF(after.center(),mark->canvasBoundingRect().center()).length()<.001);
}
