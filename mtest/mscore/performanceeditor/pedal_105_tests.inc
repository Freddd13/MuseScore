// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
namespace {
struct PedalWindow105 {
    PerformanceEditor *editor = nullptr;
    QWidget *canvas = nullptr;
    QDockWidget *dock = nullptr;
    MasterScore *score = nullptr;
    bool open(MuseScore *main, MasterScore *value) {
        score = value;
        main->setCurrentScoreView(main->appendScore(score));
        expressionShowMain080(main);
        main->showPerformanceEditor(true);
        editor = main->performanceEditor();
        if (!editor) return false;
        editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
        auto parameter = editor->findChild<QComboBox*>("performanceParameter");
        parameter->setCurrentIndex(0); parameter->setCurrentIndex(2);
        editor->findChild<QComboBox*>("performancePedalGrid")->setCurrentIndex(0);
        canvas = editor->findChild<QWidget*>("performanceParameterCanvas");
        dock = qobject_cast<QDockWidget*>(editor->parentWidget());
        if (!canvas || !dock) return false;
        dock->setFloating(true); dock->resize(1100, 650); dock->show(); dock->activateWindow(); canvas->setFocus();
        QTest::qWait(150); editor->fit(false); QTest::qWait(80);
        return canvas->isVisible() && canvas->height() >= 40;
    }
    ~PedalWindow105() {
        if (dock) dock->setFloating(false);
    }
    QPoint point(int tick, int offset = 0, int y = 6) const {
        return QPoint(76 + qRound((canvas->width() - 90) * double(tick) / score->lastMeasure()->endTick().ticks()) + offset, y);
    }
    void drag(QPoint from, QPoint to) {
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
        QTest::qWait(100);
    }
};
Pedal *pedalAt105(Score *score, int tick) {
    for (const auto &row : score->spannerMap().map())
        if (row.second->isPedal() && row.second->tick().ticks() == tick) return toPedal(row.second);
    return nullptr;
}
}
void TestPerformanceEditor::pedalAdjacentHandles105() {
    expressionHost080(_directory.path()); auto main = Ms::mscore; QVERIFY(main);
    auto score = fixture(); QVERIFY(score);
    QVERIFY(ParameterEdit::pedal(score, 4, 0, 960, nullptr));
    QVERIFY(ParameterEdit::pedal(score, 4, 960, 1920, nullptr));
    auto first = pedalAt105(score, 0), second = pedalAt105(score, 960);
    QVERIFY(first); QVERIFY(second);
    PedalWindow105 window; QVERIFY(window.open(main, score));
    const int undo = score->undoStack()->getCurIdx();
    window.drag(window.point(960, 6), window.point(1200, 6));
    QCOMPARE(second->tick().ticks(), 1200);
    QCOMPARE(second->tick2().ticks(), 1920);
    QCOMPARE(first->tick2().ticks(), 960);
    QCOMPARE(score->undoStack()->getCurIdx(), undo + 1);
    score->undoRedo(true, &ed); main->endCmd(); QTest::qWait(80);
    QCOMPARE(second->tick().ticks(), 960);
    window.drag(window.point(960, -6), window.point(720, -6));
    QCOMPARE(first->tick2().ticks(), 720);
    QCOMPARE(second->tick().ticks(), 960);
    score->undoRedo(true, &ed); main->endCmd(); QTest::qWait(80);
    QCOMPARE(first->tick2().ticks(), 960);
}

namespace {
QStringList pedalRanges105(Score* score) {
    QStringList result;
    for (const auto& row : score->spannerMap().map()) if (row.second->isPedal())
        result.append(QString("%1:%2:%3").arg(row.second->track()).arg(row.second->tick().ticks()).arg(row.second->tick2().ticks()));
    result.sort(); return result;
}
QStringList pedalNotes105(Score* score) {
    QStringList result; EventMap events;
    score->renderMidi(&events, true, false, SynthesizerState{});
    for (const auto& row : events) if (row.second.type() == ME_NOTEON || row.second.type() == ME_NOTEOFF)
        result.append(QString("%1:%2:%3:%4:%5").arg(row.first).arg(row.second.type()).arg(row.second.channel()).arg(row.second.pitch()).arg(row.second.velo()));
    return result;
}
}
void TestPerformanceEditor::pedalGapFillSplitLinked105() {
    std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
    score->startCmd(); score->insertMeasure(ElementType::VBOX, score->first()); score->endCmd();
    auto linked = new Score(score.get()); auto excerpt = new Excerpt(score.get());
    excerpt->setPartScore(linked); excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano");
    Excerpt::createExcerpt(excerpt); score->excerpts().append(excerpt); linked->doLayout();
    QVERIFY(ParameterEdit::pedal(score.get(), 4, 0, 1920, nullptr));
    auto original = pedalAt105(score.get(), 0); QVERIFY(original);
    original->setBeginText("Ped."); original->setLineColor(QColor("#63838f"));
    const auto notes = pedalNotes105(score.get());
    const int before = score->undoStack()->getCurIdx();
    QVERIFY(ParameterEdit::paintPedal(score.get(), 0, 600, 900, false));
    QCOMPARE(score->undoStack()->getCurIdx(), before + 1);
    QCOMPARE(pedalRanges105(score.get()), QStringList({"4:0:600", "4:900:1920"}));
    QCOMPARE(pedalRanges105(linked), pedalRanges105(score.get()));
    for (const auto& row : score->spannerMap().map()) if (row.second->isPedal()) {
        QCOMPARE(toPedal(row.second)->beginText(), QString("Ped."));
        QCOMPARE(toPedal(row.second)->lineColor(), QColor("#63838f"));
    }
    QCOMPARE(pedalNotes105(score.get()), notes);
    EventMap gapEvents; score->renderMidi(&gapEvents,true,false,defaultState);
    bool released=false, pressed=false;
    for(const auto& row:gapEvents)if(row.second.type()==ME_CONTROLLER && row.second.controller()==CTRL_SUSTAIN) {
        if(row.first>=600 && row.first<=603 && row.second.value()==0)released=true;
        if(row.first>=900 && row.first<=903 && row.second.value()==127)pressed=true;
    }
    QVERIFY(released);QVERIFY(pressed);
    const QString path = _directory.path() + "/pedal-gap-105.mscx";
    QVERIFY(saveScore(score.get(), path)); std::unique_ptr<MasterScore> reopened(readCreatedScore(path)); QVERIFY(reopened);
    QCOMPARE(pedalRanges105(reopened.get()), pedalRanges105(score.get()));
    QCOMPARE(pedalNotes105(reopened.get()), notes);
    score->undoRedo(true, &ed); QCOMPARE(pedalRanges105(score.get()), QStringList({"4:0:1920"}));
    QCOMPARE(pedalRanges105(linked), pedalRanges105(score.get()));
    score->undoRedo(false, &ed); QCOMPARE(pedalRanges105(score.get()), QStringList({"4:0:600", "4:900:1920"}));
    QVERIFY(ParameterEdit::paintPedal(score.get(), 0, 480, 1080, true));
    QCOMPARE(pedalRanges105(score.get()), QStringList({"4:0:1920"}));
    QCOMPARE(pedalRanges105(linked), pedalRanges105(score.get()));
    original = pedalAt105(score.get(), 0); QVERIFY(original);
    QVERIFY(ParameterEdit::splitPedal(score.get(), original, 960));
    QCOMPARE(pedalRanges105(score.get()), QStringList({"4:0:960", "4:960:1920"}));
    const int splitUndo = score->undoStack()->getCurIdx();
    QVERIFY(!ParameterEdit::paintPedal(score.get(), 0, 480, 1440, true));
    QCOMPARE(score->undoStack()->getCurIdx(), splitUndo);
    QCOMPARE(pedalRanges105(score.get()), QStringList({"4:0:960", "4:960:1920"}));
    QCOMPARE(pedalRanges105(linked), pedalRanges105(score.get()));
    QVERIFY(ParameterEdit::removePedal(score.get(), pedalAt105(score.get(), 0)));
    QCOMPARE(pedalRanges105(score.get()), QStringList({"4:960:1920"}));
    score->undoRedo(true, &ed); QCOMPARE(pedalRanges105(score.get()), QStringList({"4:0:960", "4:960:1920"}));
    QCOMPARE(pedalRanges105(linked), pedalRanges105(score.get()));
    QCOMPARE(pedalNotes105(score.get()), notes);
    QString error;
    QVERIFY(!ParameterEdit::paintPedal(score.get(), 0, 1800, 2100, false, &error)); QVERIFY(!error.isEmpty());
    score->setIsPlaying(true);
    QVERIFY(!ParameterEdit::paintPedal(score.get(), 0, 100, 200, false));
    score->setIsPlaying(false);
}
void TestPerformanceEditor::pedalGuiPaintMoveDelete105() {
    expressionHost080(_directory.path()); auto main = Ms::mscore; QVERIFY(main);
    auto score = fixture(); QVERIFY(score);
    QVERIFY(ParameterEdit::pedal(score, 4, 0, 1920, nullptr));
    PedalWindow105 window; QVERIFY(window.open(main, score));
    auto tools = window.editor->findChild<QComboBox*>("performancePedalTool"); QVERIFY(tools);
    auto remove = window.editor->findChild<QToolButton*>("performanceDeletePedal"); QVERIFY(remove);
    const auto notes = pedalNotes105(score);
    auto staffButton=window.editor->findChild<QToolButton*>("performanceStaffFilter");QVERIFY(staffButton);
    auto clickStaff=[&](int index) {
        auto menu=staffButton->menu();menu->popup(staffButton->mapToGlobal(QPoint(0,staffButton->height())));
        QTest::qWait(40);auto action=menu->findChild<QAction*>(QString("performanceStaff%1").arg(index));
        if(!action || !menu->isVisible())return false;
        QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());QTest::qWait(50);
        return !menu->isVisible();
    };
    QVERIFY(clickStaff(1));
    tools->setCurrentIndex(2);
    const int gapUndo=score->undoStack()->getCurIdx();
    QTest::mousePress(window.canvas,Qt::LeftButton,Qt::NoModifier,window.point(480,0,20));
    QMouseEvent previewMove(QEvent::MouseMove,window.point(960,0,20),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(window.canvas,&previewMove);QTest::qWait(40);
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:1920"}));
    QVERIFY(window.dock->grab().save("E:/programming/funcodes/muse3_plugins/P2/out/pedal-gap-preview-105.png"));
    QTest::keyClick(window.canvas,Qt::Key_Escape);QTest::mouseRelease(window.canvas,Qt::LeftButton,Qt::NoModifier,window.point(960,0,20));
    QCOMPARE(score->undoStack()->getCurIdx(),gapUndo);
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:1920"}));
    window.drag(window.point(480, 0, 20), window.point(960, 0, 20));
    QCOMPARE(pedalRanges105(score), QStringList({"4:0:480", "4:960:1920"}));
    Shortcut::load();
    for(const char* id:{"undo","redo"}) { auto shortcut=Shortcut::getShortcut(id);shortcut->setStandardKey(shortcut->standardKey()); }
    QVERIFY(!getAction("undo")->shortcuts().isEmpty());
    window.dock->activateWindow();window.canvas->setFocus();QTest::qWait(100);
    QTRY_VERIFY(window.dock->isActiveWindow());
    QVERIFY(getAction("undo")->isEnabled());
    QTest::keyClick(window.canvas,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(100);
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:1920"}));
    QTest::keyClick(window.canvas,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier);QTest::qWait(100);
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:480", "4:960:1920"}));
    tools->setCurrentIndex(0);
    window.drag(window.point(1440, 0, window.canvas->height()/2), window.point(1320, 0, window.canvas->height()/2));
    QCOMPARE(pedalRanges105(score), QStringList({"4:0:480", "4:840:1800"}));
    QVERIFY(remove->isEnabled());
    QTest::keyClick(window.canvas, Qt::Key_Right, Qt::ShiftModifier); QTest::qWait(100);
    QCOMPARE(pedalRanges105(score), QStringList({"4:0:480", "4:841:1801"}));
    score->undoRedo(true, &ed); main->endCmd(); QTest::qWait(100);
    QCOMPARE(pedalRanges105(score), QStringList({"4:0:480", "4:840:1800"}));
    const int index = score->undoStack()->getCurIdx();
    QTest::mousePress(window.canvas, Qt::LeftButton, Qt::NoModifier, window.point(840, 6));
    QMouseEvent move(QEvent::MouseMove, window.point(1080,6), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.canvas,&move); QTest::qWait(30);
    QTest::keyClick(window.canvas,Qt::Key_Escape);
    QTest::mouseRelease(window.canvas,Qt::LeftButton,Qt::NoModifier,window.point(1080,6));
    QCOMPARE(score->undoStack()->getCurIdx(),index);
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:480", "4:840:1800"}));
    QTest::mouseClick(remove,Qt::LeftButton); QTest::qWait(100);
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:480"}));
    QTest::mouseClick(window.canvas,Qt::LeftButton,Qt::NoModifier,window.point(240,0,30));
    QTest::keyClick(window.canvas,Qt::Key_Delete); QTest::qWait(100);
    QVERIFY(pedalRanges105(score).isEmpty());
    score->undoRedo(true,&ed);main->endCmd();QTest::qWait(100);
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:480"}));
    tools->setCurrentIndex(1);
    window.drag(window.point(360,0,20),window.point(1920,0,20));
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:1920"}));
    tools->setCurrentIndex(3);
    QTest::mouseClick(window.canvas,Qt::LeftButton,Qt::NoModifier,window.point(960,0,30));QTest::qWait(100);
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:960", "4:960:1920"}));
    QCOMPARE(pedalNotes105(score),notes);
    tools->setCurrentIndex(0);QTest::mouseClick(window.canvas,Qt::LeftButton,Qt::NoModifier,window.point(1440,0,30));
    QTest::qWait(50);
    QVERIFY(window.dock->grab().save("E:/programming/funcodes/muse3_plugins/P2/out/pedal-edit-105.png"));
    const QString path=_directory.path()+"/pedal-ui-105.mscx";QVERIFY(saveScore(score,path));
    std::unique_ptr<MasterScore> reopened(readCreatedScore(path));QVERIFY(reopened);
    QCOMPARE(pedalRanges105(reopened.get()),pedalRanges105(score));
    QVERIFY(remove->isEnabled());
    QVERIFY(clickStaff(0));QVERIFY(!remove->isEnabled());
    QVERIFY(clickStaff(1));
    QTest::mouseClick(window.canvas,Qt::LeftButton,Qt::NoModifier,window.point(1440,0,30));
    QVERIFY(remove->isEnabled());
}

void TestPerformanceEditor::pedalStripAndInterruptedGesture105() {
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(main);
    auto score=fixture();QVERIFY(score);
    QVERIFY(ParameterEdit::pedal(score,4,0,960,nullptr));QVERIFY(ParameterEdit::pedal(score,4,960,1920,nullptr));
    PedalWindow105 window;QVERIFY(window.open(main,score));
    auto editor=window.editor;auto view=main->currentScoreView();
    const auto band=editor->findChild<QCheckBox*>("performanceScoreBand");
    // Use the public setting; the native strip itself receives the mouse events.
    const bool wasOn=editor->scoreBandEnabled();
    auto fixed=editor->findChild<QAction*>("performanceBandPosition0");
    auto follow=editor->findChild<QAction*>("performanceBandPosition1");
    const bool wasFollow=follow->isChecked();
    const int oldHeight=QSettings().value("performanceEditor/bandHeight",48).toInt();
    fixed->trigger();editor->findChild<QAction*>("performanceBandHeight80")->trigger();
    editor->setScoreBandEnabled(true);
    auto note=noteAt(score);score->select(note);main->endCmd();editor->selectionChanged();
    QTest::qWait(100);view->repaint();
    auto x=[&](int tick) {
        const auto matrix=view->matrix();
        auto seg=score->firstSegment(SegmentType::ChordRest);
        Segment* before=seg;
        for(;seg;seg=seg->next1(SegmentType::ChordRest)) {
            if(seg->tick().ticks()==tick)return matrix.map(seg->canvasPos()).x();
            if(seg->tick().ticks()>tick)break;
            before=seg;
        }
        const double a=before->tick().ticks(),b=seg?seg->tick().ticks():score->lastMeasure()->endTick().ticks();
        const double xa=matrix.map(before->canvasPos()).x();
        const double xb=matrix.map(seg?seg->canvasPos():score->lastMeasure()->canvasPos()+QPointF(score->lastMeasure()->width(),0)).x();
        return xa+(xb-xa)*(tick-a)/(b-a);
    };
    const int y=view->height()-80-26;
    auto point=[&](int tick,int offset){return QPoint(qRound(x(tick))+offset,y);};
    QVERIFY(view->rect().contains(point(960,6)));
    const int before=score->undoStack()->getCurIdx();
    QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,point(960,6));
    QMouseEvent move(QEvent::MouseMove,point(1200,6),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(view,&move);QTest::qWait(30);
    QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,point(1200,6));QTest::qWait(100);
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:960", "4:1200:1920"}));
    QCOMPARE(score->undoStack()->getCurIdx(),before+1);
    QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,point(1200,6));
    QApplication::sendEvent(view,&move);
    QEvent deactivate(QEvent::ApplicationDeactivate);QApplication::sendEvent(qApp,&deactivate);
    QVERIFY(QWidget::mouseGrabber()!=view);
    QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,point(1200,6));
    QCOMPARE(score->undoStack()->getCurIdx(),before+1);
    auto tools=editor->findChild<QComboBox*>("performancePedalTool");tools->setCurrentIndex(2);
    // The parameter canvas remains usable after the strip gives back its mouse.
    window.drag(window.point(1440,0,20),window.point(1680,0,20));
    QCOMPARE(pedalRanges105(score),QStringList({"4:0:960", "4:1200:1440", "4:1680:1920"}));
    tools->setCurrentIndex(0);
    QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,QPoint(qRound(x(1800)),y+20));QTest::qWait(60);
    view->repaint();QVERIFY(view->grab().save("E:/programming/funcodes/muse3_plugins/P2/out/pedal-strip-105.png"));
    const int imageLeft=qMax(0,qRound(x(0))-115),imageRight=qMin(view->width(),qRound(x(1920))+20);
    QVERIFY(view->grab(QRect(imageLeft,y-30,imageRight-imageLeft,112)).save("E:/programming/funcodes/muse3_plugins/P2/out/pedal-strip-detail-105.png"));
    editor->setScoreBandEnabled(wasOn);
    if(wasFollow)follow->trigger();
    if(auto action=editor->findChild<QAction*>(QString("performanceBandHeight%1").arg(oldHeight)))action->trigger();
}

void TestPerformanceEditor::pedalPaintInstrumentScope105() {
    const QString path=_directory.path()+"/pedal-two-parts-105.mscx";
    QString xml="<museScore version=\"3.02\"><Score><Division>480</Division>";
    for(int staff=1;staff<=2;++staff)xml+=QString("<Part><Staff id=\"%1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff><trackName>%2</trackName><Instrument id=\"%3\"><Channel/></Instrument></Part>").arg(staff).arg(staff==1?"Piano":"Strings").arg(staff==1?"piano":"strings");
    for(int staff=1;staff<=2;++staff)xml+=QString("<Staff id=\"%1\"><Measure><voice><Clef><concertClefType>G</concertClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>whole</durationType><Note><pitch>%2</pitch><tpc>14</tpc></Note></Chord></voice></Measure></Staff>").arg(staff).arg(staff==1?60:72);
    xml+="</Score></museScore>";QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml.toUtf8());file.close();
    std::unique_ptr<MasterScore> score(readCreatedScore(path));QVERIFY(score);QCOMPARE(score->parts().size(),2);
    QVERIFY(ParameterEdit::pedal(score.get(),0,0,1920,nullptr));
    QVERIFY(ParameterEdit::pedal(score.get(),4,0,1920,nullptr));
    Pedal* strings=nullptr;for(const auto& row:score->spannerMap().map())if(row.second->isPedal()&&row.second->track()==4)strings=toPedal(row.second);
    QVERIFY(strings);const auto notes=pedalNotes105(score.get());
    QVERIFY(ParameterEdit::paintPedal(score.get(),0,480,960,false));
    QCOMPARE(pedalRanges105(score.get()),QStringList({"0:0:480","0:960:1920","4:0:1920"}));
    QCOMPARE(strings->tick().ticks(),0);QCOMPARE(strings->tick2().ticks(),1920);
    QCOMPARE(pedalNotes105(score.get()),notes);
    score->undoRedo(true,&ed);
    QCOMPARE(pedalRanges105(score.get()),QStringList({"0:0:1920","4:0:1920"}));
}
