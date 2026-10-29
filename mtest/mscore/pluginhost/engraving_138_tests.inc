// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
namespace {
QByteArray systemLockFixture138()
{
    QByteArray xml("<museScore version=\"3.02\"><Score><Division>480</Division>"
        "<Part><Staff id=\"1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>"
        "<Staff id=\"2\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>"
        "<trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part>");
    for(int staff=0;staff<2;++staff) {
        xml+="<Staff id=\""+QByteArray::number(staff+1)+"\">";
        for(int bar=0;bar<8;++bar) {
            xml+="<Measure><voice>";
            if(!bar)xml+="<Clef><concertClefType>"+QByteArray(staff?"F":"G")+"</concertClefType><transposingClefType>"+QByteArray(staff?"F":"G")+"</transposingClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
            for(int beat=0;beat<4;++beat)xml+="<Chord><durationType>quarter</durationType><Note><pitch>"+QByteArray::number((staff?48:72)+beat*2)+"</pitch><tpc>"+QByteArray::number(14+beat*2)+"</tpc></Note></Chord>";
            xml+="</voice>";
            if(!staff&&!bar)xml+="<LayoutBreak><subtype>nobreak</subtype></LayoutBreak>";
            if(!staff&&bar==3)xml+="<LayoutBreak><subtype>page</subtype></LayoutBreak>";
            xml+="</Measure>";
        }
        xml+="</Staff>";
    }
    xml+="</Score></museScore>";
    return xml;
}
}

void TestPluginHost::systemLockInsertAndPart138()
{
    const auto filePath=_settings.filePath("lock-insert-138.mscx");
    const auto xml=systemLockFixture138();
    QFile fixture(filePath);QVERIFY(fixture.open(QIODevice::WriteOnly));fixture.write(xml);fixture.close();
    auto main=Ms::mscore;auto score=main->readScore(filePath);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    score->startCmd();score->insertMeasure(ElementType::VBOX,score->first());score->endCmd();
    auto part=new Score(score);auto excerpt=new Excerpt(score);excerpt->setPartScore(part);excerpt->setParts({score->parts().front()});excerpt->setTitle("Piano part");part->setExcerpt(excerpt);score->startCmd();score->undo(new AddExcerpt(excerpt));Excerpt::createExcerpt(excerpt);score->endCmd();part->doLayout();score->doLayout();
    QVector<Measure*> bars;for(auto bar=score->firstMeasure();bar;bar=bar->nextMeasure())bars.append(bar);
    auto locks=[](Score* s){int n=0;for(auto bar=s->first();bar;bar=bar->next())for(auto e:bar->el())if(e->isLayoutBreak()&&!toLayoutBreak(e)->systemLockData().isEmpty())++n;return n;};
    QCOMPARE(locks(part),0);QVERIFY(bars[0]->system()==bars[3]->system());
    score->select(score->findCR(bars[1]->tick(),0));
    const auto result=changeSystemLocks(score,true,false);QCOMPARE(result.value("count").toInt(),1);QVERIFY(locks(score)>0);
    QCOMPARE(locks(part),0);
    const int undo=score->undoStack()->getCurIdx();
    score->startCmd();score->insertMeasure(ElementType::MEASURE,bars[2]);score->endCmd();score->doLayout();
    auto inserted=bars[2]->prevMeasure();QVERIFY(inserted!=bars[1]);
    qInfo("Inserted inside locked row: noBreak=%d; master locks=%d; part locks=%d",inserted->noBreak(),locks(score),locks(part));
    QVERIFY2(inserted->noBreak(),"A measure inserted within a locked row must remain in that row");
    QVERIFY(bars[0]->system()==bars[3]->system());QCOMPARE(locks(part),0);QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
    EditData edit;score->undoRedo(true,&edit);QCOMPARE(bars[2]->prevMeasure(),bars[1]);score->undoRedo(false,&edit);QCOMPARE(bars[2]->prevMeasure(),inserted);QVERIFY(inserted->noBreak());
    const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path());
    QFileInfo info(output+"/lock-insert-138.mscz");QVERIFY(score->saveCompressedFile(info,false,false));
    std::unique_ptr<MasterScore> read(main->readScore(info.filePath()));QVERIFY(read);read->doLayout();QCOMPARE(locks(read.get()),locks(score));
    const auto cleared=changeSystemLocks(read.get(),false,true,true);QCOMPARE(cleared.value("count").toInt(),1);
    QVERIFY(read->firstMeasure()->noBreak());QVERIFY(read->lastMeasure()->prevMeasure()->prevMeasure()->prevMeasure()->prevMeasure()->pageBreak());
    QCOMPARE(locks(read.get()),0);QCOMPARE(locks(part),0);
}

void TestPluginHost::systemLockIndependentParts138()
{
    const auto path=_settings.filePath("lock-parts-138.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(systemLockFixture138());file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    auto part=new Score(score);auto excerpt=new Excerpt(score);excerpt->setPartScore(part);excerpt->setParts({score->parts().front()});excerpt->setTitle("Independent part");part->setExcerpt(excerpt);score->startCmd();score->undo(new AddExcerpt(excerpt));Excerpt::createExcerpt(excerpt);score->endCmd();part->doLayout();score->doLayout();
    QVector<Measure*> bars;for(auto bar=score->firstMeasure();bar;bar=bar->nextMeasure())bars.append(bar);
    score->select(score->findCR(bars[1]->tick(),0));QCOMPARE(changeSystemLocks(score,true,false).value("count").toInt(),1);
    // Parts have their own pagination. Establish the second row explicitly.
    auto partBoundary=part->tick2measure(bars[3]->tick());
    if(!partBoundary->pageBreak()) {
        auto page=new LayoutBreak(part);page->setLayoutBreakType(LayoutBreak::PAGE);page->setParent(partBoundary);page->setTrack(0);
        part->startCmd();part->undoAddElement(page);part->endCmd();
    }
    part->doLayout();QVERIFY(part->firstMeasure()->system()!=part->tick2measure(bars[5]->tick())->system());
    part->select(part->findCR(bars[5]->tick(),0));QCOMPARE(changeSystemLocks(part,true,false).value("count").toInt(),1);
    score->startCmd();score->insertMeasure(ElementType::MEASURE,bars[0]);score->endCmd();
    QVERIFY(!score->firstMeasure()->noBreak());QVERIFY(!part->firstMeasure()->noBreak());
    score->startCmd();score->insertMeasure(ElementType::MEASURE,bars[2]);score->endCmd();
    auto insideMaster=bars[2]->prevMeasure();QVERIFY(insideMaster->noBreak());QVERIFY(!part->tick2measure(insideMaster->tick())->noBreak());
    score->startCmd();score->insertMeasure(ElementType::MEASURE,bars[5]);score->endCmd();
    auto insidePart=bars[5]->prevMeasure();QVERIFY(!insidePart->noBreak());QVERIFY(part->tick2measure(insidePart->tick())->noBreak());
    EditData edit;for(int n=0;n<3;++n)score->undoRedo(true,&edit);
    QCOMPARE(score->firstMeasure(),bars[0]);QCOMPARE(bars[2]->prevMeasure(),bars[1]);QCOMPARE(bars[5]->prevMeasure(),bars[4]);
    for(int n=0;n<3;++n)score->undoRedo(false,&edit);
    QVERIFY(insideMaster->noBreak());QVERIFY(!insidePart->noBreak());QVERIFY(part->tick2measure(insidePart->tick())->noBreak());
    const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path());QFileInfo info(output+"/lock-independent-parts-138.mscz");QVERIFY(score->saveCompressedFile(info,false,false));
    std::unique_ptr<MasterScore> read(main->readScore(info.filePath()));QVERIFY(read);read->doLayout();
    QVERIFY(!read->excerpts().isEmpty());auto restoredPart=read->excerpts().front()->partScore();QVERIFY(restoredPart);restoredPart->doLayout();
    QVERIFY(read->tick2measure(insideMaster->tick())->noBreak());QVERIFY(!restoredPart->tick2measure(insideMaster->tick())->noBreak());
    QVERIFY(!read->tick2measure(insidePart->tick())->noBreak());QVERIFY(restoredPart->tick2measure(insidePart->tick())->noBreak());
    QCOMPARE(changeSystemLocks(read.get(),false,true,true).value("count").toInt(),1);
    QVERIFY(restoredPart->tick2measure(insidePart->tick())->noBreak());
}

void TestPluginHost::systemLockMenuRange138()
{
    const auto path=_settings.filePath("lock-menu-138.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(systemLockFixture138());file.close();
    auto main=Ms::mscore;main->resize(1200,820);main->show();main->raise();main->activateWindow();QVERIFY(QTest::qWaitForWindowActive(main));auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));score->doLayout();
    QVector<Measure*> bars;for(auto bar=score->firstMeasure();bar;bar=bar->nextMeasure())bars.append(bar);
    score->select(toChord(score->findCR(bars[1]->tick(),0))->upNote(),SelectType::SINGLE);score->select(toChord(score->findCR(bars[5]->tick(),0))->upNote(),SelectType::RANGE);
    QVERIFY(score->selection().isRange());
    auto menu=main->findChild<QMenu*>("kumo-system-lock-menu");auto action=main->findChild<QAction*>("kumo-system-lock-selected");QVERIFY(menu&&action);
    const int undo=score->undoStack()->getCurIdx();menu->popup(main->mapToGlobal(QPoint(170,120)));QTest::qWait(50);
    QVERIFY(menu->isVisible());QVERIFY(!menu->actionGeometry(action).isEmpty());
    QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());QTRY_VERIFY(!menu->isVisible());
    QCOMPARE(score->undoStack()->getCurIdx(),undo+1);QVERIFY(bars[1]->noBreak());QVERIFY(bars[5]->noBreak());QVERIFY(bars[7]->lineBreak());
    const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path());auto view=main->currentScoreView();view->adjustCanvasPosition(bars[0],false);QTest::qWait(40);
    const auto area=view->toPhysical(bars[0]->system()->canvasBoundingRect()).adjusted(-10,-35,10,15).intersected(view->rect());
    QVERIFY(view->grab(area).save(output+"/system-lock-score-138.png"));
    QFileInfo saved(output+"/lock-selected-138.mscz");QVERIFY(score->saveCompressedFile(saved,false,false));
    QVERIFY(main->savePdf(score,output+"/lock-selected-138.pdf"));
    auto unlock=main->findChild<QAction*>("kumo-system-lock-all");QVERIFY(unlock);menu->popup(main->mapToGlobal(QPoint(170,120)));QTest::qWait(30);
    QVERIFY(menu->grab().save(output+"/system-lock-menu-138.png"));QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(unlock).center());
    QTRY_VERIFY(!menu->isVisible());QVERIFY(!bars[1]->noBreak());QVERIFY(!bars[5]->noBreak());QVERIFY(bars[0]->noBreak());QVERIFY(bars[3]->pageBreak());
    EditData edit;score->undoRedo(true,&edit);QVERIFY(bars[1]->noBreak());QVERIFY(bars[5]->noBreak());score->undoRedo(false,&edit);QVERIFY(!bars[5]->noBreak());
}
