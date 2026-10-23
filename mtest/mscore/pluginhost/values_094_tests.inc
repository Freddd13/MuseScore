// Dense piano values through the real editor and score overlay.
#include "mscore/performanceeditor/performancelabels.h"
#include <QSplitter>
void TestPluginHost::valueLabelsKeepNotesClear()
{
    const QRectF viewport(0,0,500,240), head(110,70,50,40);
    PerformanceLabelSpace layout(viewport); layout.reserve(head);
    QVector<QRectF> shown;
    QElapsedTimer clock; clock.start();
    for(int i=0;i<1000;++i) {
        const auto box=layout.place(QRectF(8+(i%15)*31,8+((i/15)%10)*23,24,16));
        if(box.isEmpty())continue;
        QVERIFY(viewport.contains(box)); QVERIFY(!box.intersects(head));
        for(const auto& previous:shown)QVERIFY(!box.intersects(previous));
        shown.append(box);
    }
    QVERIFY(shown.size()>80);
    qInfo("Dense value placement: %d visible / 1000 requested, %.3f ms",shown.size(),clock.nsecsElapsed()/1e6);
    // A wide value at the edge remains fully readable; a filled view stays finite.
    PerformanceLabelSpace edge(viewport);
    QVERIFY(viewport.contains(edge.place(QRectF(490,-10,55,18))));
    edge.reserve(viewport); QVERIFY(edge.place(QRectF(10,10,24,16)).isEmpty());
    PerformanceValueLayout cached;int builds=0;
    const QVector<QPair<int,QRectF>> requests{{1,QRectF(90,80,24,16)},{2,QRectF(90,80,24,16)}};
    const auto reserve=[&](PerformanceLabelSpace& space){++builds;space.reserve(head);};
    const auto first=cached.arrange(1,viewport,requests,{},reserve);QCOMPARE(builds,1);
    QCOMPARE(cached.arrange(1,viewport,requests,{},reserve),first);QCOMPARE(builds,1);
    const QVector<QPair<int,QRectF>> reversed{requests[1],requests[0]};
    cached.arrange(1,viewport,reversed,{},reserve);QCOMPARE(builds,2);
    cached.arrange(2,viewport,reversed,{},reserve);QCOMPARE(builds,3);
    cached.arrange(2,viewport,reversed,{QRectF(80,20,40,20)},reserve);QCOMPARE(builds,4);
    cached.arrange(2,viewport,{}, {},reserve);QCOMPARE(builds,4);
}
void TestPluginHost::denseVelocityValuesAndScoreBand()
{
    auto main = Ms::mscore;
    guiScaling = 1.; MuseScore::updateUiStyleAndTheme();
    QByteArray xml("<museScore version=\"3.02\"><Score><Division>480</Division>"
                   "<Style><minSystemDistance>14</minSystemDistance><maxSystemDistance>14</maxSystemDistance></Style><Part>");
    for (int staff=1; staff<=2; ++staff)
        xml += "<Staff id=\"" + QByteArray::number(staff) + "\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>";
    xml += "<trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part>";
    const int scale[] = {0, 2, 4, 5, 7, 9, 11};
    for (int staff=1; staff<=2; ++staff) {
        xml += "<Staff id=\"" + QByteArray::number(staff) + "\">";
        for (int bar=0; bar<4; ++bar) {
            xml += "<Measure><voice>";
            if (!bar) xml += staff==1 ? "<Clef><concertClefType>G</concertClefType></Clef>" : "<Clef><concertClefType>F</concertClefType></Clef>";
            for (int onset=0; onset<16; ++onset) {
                xml += "<Chord><durationType>16th</durationType>";
                for (int tone=0; tone<4; ++tone) {
                    const int degree = (onset%4) + tone*2;
                    const int pitch = (staff==1 ? 60 : 36) + scale[degree%7] + (degree/7)*12;
                    const int tpc = 14 + (degree%7==0 ? 0 : degree%7==1 ? 2 : degree%7==2 ? 4 : degree%7==3 ? -1 : degree%7==4 ? 1 : degree%7==5 ? 3 : 5);
                    xml += "<Note><pitch>" + QByteArray::number(pitch) + "</pitch><tpc>" + QByteArray::number(tpc) + "</tpc></Note>";
                }
                xml += "</Chord>";
            }
            xml += "</voice></Measure>";
        }
        xml += "</Staff>";
    }
    xml += "</Score></museScore>";
    const auto path = _settings.filePath("dense-values-094.mscx");
    QFile fixture(path); QVERIFY(fixture.open(QIODevice::WriteOnly)); fixture.write(xml); fixture.close();
    auto score = main->readScore(path); QVERIFY(score);
    auto lineBreak = new LayoutBreak(score); lineBreak->setLayoutBreakType(LayoutBreak::LINE);
    lineBreak->setParent(score->firstMeasure()->nextMeasure()); score->firstMeasure()->nextMeasure()->add(lineBreak);
    score->doLayout(); main->setCurrentScoreView(main->appendScore(score));
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->showNormal(); main->resize(1360,920); main->show(); main->showPerformanceEditor(true);
    auto editor=main->performanceEditor(); auto view=main->currentScoreView();
    editor->findChild<QComboBox*>("performanceParameter")->setCurrentIndex(0);
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    editor->findChild<QComboBox*>("performanceScope")->setCurrentIndex(2);
    editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
    main->resizeDocks({main->findChild<QDockWidget*>("performanceEditorDock")},{640},Qt::Vertical);
    editor->findChild<QSplitter*>("performanceMainSplitter")->setSizes({320,80});
    QAction* values=nullptr;
    for (auto action : editor->findChildren<QAction*>())
        if (action->text()==QString::fromUtf8("一直显示力度（谱面与卷帘）")) values=action;
    QVERIFY(values); values->setChecked(true);
    QTest::qWait(100); editor->fit(); QTest::qWait(100);
    auto canvas=editor->findChild<QWidget*>("performanceNoteCanvas"); QVERIFY(canvas && canvas->height()>40);
    const auto repaintCost=[&](QWidget* widget){QVector<double> samples;for(int i=0;i<16;++i){QElapsedTimer time;time.start();widget->repaint();samples.append(time.nsecsElapsed()/1e6);}std::sort(samples.begin(),samples.end());return samples[8];};
    values->setChecked(false);QTest::qWait(30);const double rollPlain=repaintCost(canvas);
    values->setChecked(true);QTest::qWait(30);const double rollValues=repaintCost(canvas);
    qInfo("Dense roll repaint median: plain %.3f ms / values %.3f ms",rollPlain,rollValues);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    const auto label=qEnvironmentVariable("P2_TEST_BATCH","values-094");
    QVERIFY(canvas->grab().save(output.filePath(label+"-roll.png")));
    score->select(toChord(score->firstMeasure()->findSegment(SegmentType::ChordRest,Fraction())->element(0))->notes().front());
    editor->selectionChanged(); main->showPerformanceEditor(false); QTest::qWait(100);
    values->setChecked(false);QTest::qWait(30);const double scorePlain=repaintCost(view);
    values->setChecked(true);QTest::qWait(30);const double scoreValues=repaintCost(view);
    qInfo("Dense score repaint median: plain %.3f ms / values %.3f ms",scorePlain,scoreValues);
    QVERIFY(view->grab().save(output.filePath(label+"-score.png")));
    values->setChecked(false);
    editor->findChild<QAction*>("performanceBandPosition1")->trigger();
    editor->findChild<QAction*>("performanceBandHeight48")->trigger(); editor->setScoreBandEnabled(true);
    for (int tick : {0,3840}) {
        auto note=toChord(score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest)->element(0))->notes().front();
        score->select(note); editor->selectionChanged(); view->adjustCanvasPosition(note,false); QTest::qWait(80);
        QVERIFY(view->grab().save(output.filePath(label+QString("-band-%1.png").arg(tick))));
    }
    const auto bandFrame=[&]() {
        const auto image=view->grab().toImage(); int top=-1,bottom=-1;
        for(int y=0;y<image.height();++y) {
            int count=0;
            for(int x=0;x<image.width();++x)if(image.pixelColor(x,y).rgb()==QColor("#f1f2ef").rgb())++count;
            if(count>150){if(top<0)top=y;bottom=y;}
        }
        return QRectF(0,top,view->width(),bottom-top+1);
    };
    const auto before=bandFrame(); QVERIFY(before.top()>=0); QVERIFY(before.height()>=76);
    auto selected=score->selection().noteList().front();
    const double right=qMin(double(view->width()-12),view->matrix().map(selected->chord()->measure()->system()->lastMeasure()->canvasPos()+QPointF(selected->chord()->measure()->system()->lastMeasure()->width(),0)).x());
    const int history=score->undoStack()->getCurIdx();
    const QPoint edge(qRound(right-12),qRound(before.bottom()));
    QMouseEvent hoverEdge(QEvent::MouseMove,edge,edge,view->mapToGlobal(edge),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(view,&hoverEdge);QCOMPARE(view->cursor().shape(),Qt::SizeVerCursor);
    QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,edge);
    const QPoint resized=edge+QPoint(0,32);
    QMouseEvent movement(QEvent::MouseMove,resized,resized,view->mapToGlobal(resized),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(view,&movement); QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,resized);
    QTRY_COMPARE(QSettings().value("performanceEditor/bandHeight").toInt(),80);
    QCOMPARE(score->undoStack()->getCurIdx(),history); QVERIFY(!QWidget::mouseGrabber());
    const auto after=bandFrame(); QVERIFY(after.height()>before.height()+25);
    const int old=selected->customizeVelocity(NoteVelocity::referenceBase(selected));
    const QPoint endpoint(qRound(view->matrix().map(selected->canvasBoundingRect().center()).x()),qRound(after.top()+26+80*(127.-old)/126.));
    QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,endpoint);
    const QPoint changed=endpoint-QPoint(0,8);
    QMouseEvent velocityMove(QEvent::MouseMove,changed,changed,view->mapToGlobal(changed),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(view,&velocityMove); QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,changed);
    editor->flushPending(); QTRY_VERIFY(selected->customizeVelocity(NoteVelocity::referenceBase(selected))>old);
    QCOMPARE(score->undoStack()->getCurIdx(),history+1);
    getAction("undo")->trigger(); QTRY_COMPARE(selected->customizeVelocity(NoteVelocity::referenceBase(selected)),old);
    QTest::qWait(80); QVERIFY(bandFrame().height()>100);
    QVERIFY(view->grab().save(output.filePath(label+"-band-resized.png")));
    editor->setScoreBandEnabled(false);
}

void TestPluginHost::realScoreValuesAndBand()
{
    const auto source=qEnvironmentVariable("P2_VALUES_SCORE");
    if(source.isEmpty())QSKIP("Supply a local score copy with P2_VALUES_SCORE");
    const auto path=_settings.filePath("real-values-094.mscz"); QVERIFY(QFile::copy(source,path));
    auto main=Ms::mscore; auto score=main->readScore(path); QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1360,920);main->show();
    main->showPerformanceEditor(true);auto editor=main->performanceEditor();auto view=main->currentScoreView();
    editor->findChild<QComboBox*>("performanceParameter")->setCurrentIndex(0);
    editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
    editor->findChild<QComboBox*>("performanceScope")->setCurrentIndex(2);
    editor->findChild<QAction*>("performanceBandPosition1")->trigger();
    editor->findChild<QAction*>("performanceBandHeight48")->trigger();
    auto measure=score->firstMeasure();for(int i=0;i<24&&measure->nextMeasure();++i)measure=measure->nextMeasure();
    Note* note=nullptr;
    for(auto segment=measure->first(SegmentType::ChordRest);segment&&segment->measure()==measure&&!note;segment=segment->next1(SegmentType::ChordRest))
        for(int track=0;track<score->nstaves()*VOICES&&!note;++track)
            if(auto element=segment->element(track))if(element->isChord())note=toChord(element)->notes().front();
    QVERIFY(note);score->select(note);editor->selectionChanged();
    main->showPerformanceEditor(false);view->adjustCanvasPosition(note,false);QTest::qWait(100);
    QAction* values=nullptr;for(auto action:editor->findChildren<QAction*>())if(action->text()==QString::fromUtf8("一直显示力度（谱面与卷帘）"))values=action;
    QVERIFY(values);const int history=score->undoStack()->getCurIdx();
    values->setChecked(false);QTest::qWait(80);const auto plain=view->grab().toImage();
    values->setChecked(true);QTest::qWait(100);
    qInfo()<<"real values selection"<<note->track()<<note->pitch()<<note->play()<<note->hidden()<<note->tieBack()<<NoteVelocity::referenceBase(note)<<"view"<<view->mscoreState()<<view->noteEntryMode()<<view->editMode()<<view->fotoMode()<<"values"<<values->isChecked();
    for(auto text:editor->findChildren<QLabel*>())if(text->text().contains(QString::fromUtf8("可编辑选音")))qInfo()<<text->text();
    QVERIFY2(plain!=view->grab().toImage(),"真实谱打开持续力度显示后可见数字");
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QVERIFY(view->grab().save(output.filePath("real-values-094.png")));
    QVector<double> samples;
    for(int i=0;i<20;++i){QElapsedTimer time;time.start();view->repaint();samples.append(time.nsecsElapsed()/1e6);}
    std::sort(samples.begin(),samples.end());qInfo("Real score values: %d staves, repaint median %.3f ms / p95 %.3f ms",score->nstaves(),samples[10],samples[18]);
    values->setChecked(false);editor->setScoreBandEnabled(true);QTest::qWait(80);
    QVERIFY(view->grab().save(output.filePath("real-band-094.png")));
    QCOMPARE(score->undoStack()->getCurIdx(),history);editor->setScoreBandEnabled(false);
}
