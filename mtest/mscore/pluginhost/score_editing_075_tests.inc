// Check the user's modifier-wheel operation through the visible score surface.
void TestPluginHost::nearestWheelOnBandAxis()
{
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1200, 900);
    main->show();
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    auto view = main->currentScoreView();
    auto placement = editor->findChild<QAction*>("performanceBandPosition0");
    QVERIFY(placement);
    placement->trigger();
    editor->setScoreBandEnabled(true);
    main->showPerformanceEditor(false);
    QTest::qWait(150);
    view->repaint();

    const auto matrix = view->matrix();
    const int x = qRound(qMax(56.0, matrix.map(score->firstMeasure()->canvasPos()).x()) - 24);
    const QPoint point(x, view->height() - 66);
    const auto history = score->undoStack()->getCurIdx();
    QWheelEvent wheel(point, view->mapToGlobal(point), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::AltModifier | Qt::ShiftModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(view, &wheel);
    QVERIFY2(editor->hasPending(), "Alt+Shift on the band axis must adjust the nearest score note");
    QCOMPARE(view->matrix(), matrix);
    QTRY_COMPARE(score->undoStack()->getCurIdx(), history + 1);
    view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()))
                         .filePath("nearest-band-axis-075.png"));
    getAction("undo")->trigger();
    QTRY_COMPARE(score->undoStack()->getCurIdx(), history);
    editor->setScoreBandEnabled(false);
}

void TestPluginHost::scoreBandStaysNearSelectedSystem()
{
    QFile fixture(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QString xml = QString::fromUtf8(fixture.readAll());
    const auto measure = [](int pitch) {
        return QString("<Measure><voice><Chord><durationType>whole</durationType>"
                       "<Note><pitch>%1</pitch><tpc>14</tpc></Note></Chord></voice></Measure>").arg(pitch);
    };
    xml.replace("<Score>", "<Score><Style><minSystemDistance>4</minSystemDistance>"
                "<maxSystemDistance>4</maxSystemDistance></Style>");
    xml.replace("</Measure></Staff><Staff id=\"2\"><Measure",
                "</Measure>" + measure(72) + "</Staff><Staff id=\"2\"><Measure");
    xml.replace("</Measure></Staff></Score>", "</Measure>" + measure(48) + "</Staff></Score>");
    const QString path = _settings.filePath("band-two-systems-075.mscx");
    QFile output(path);QVERIFY(output.open(QIODevice::WriteOnly));output.write(xml.toUtf8());output.close();
    auto main = Ms::mscore;
    auto score = main->readScore(path);QVERIFY(score);
    auto lineBreak = new LayoutBreak(score);
    lineBreak->setLayoutBreakType(LayoutBreak::LINE);
    lineBreak->setParent(score->firstMeasure());score->firstMeasure()->add(lineBreak);score->doLayout();
    main->setCurrentScoreView(main->appendScore(score));main->resize(1200, 940);main->show();
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();auto view = main->currentScoreView();
    editor->findChild<QAction*>("performanceBandHeight48")->trigger();
    editor->findChild<QAction*>("performanceBandPosition1")->trigger();
    editor->setScoreBandEnabled(true);main->showPerformanceEditor(false);QTest::qWait(150);

    for (int tick : {480, 1920}) {
        auto element = score->tick2segment(Fraction::fromTicks(tick), false, SegmentType::ChordRest)->element(0);
        QVERIFY(element && element->isChord());auto note = toChord(element)->notes().front();
        score->select(note);editor->selectionChanged();view->adjustCanvasPosition(note, false);QTest::qWait(100);
        for (int zoom : {0, 120, -120}) {
        if (zoom) {
            const auto before = view->matrix();
            const auto position = before.mapRect(note->canvasBoundingRect()).center();
            QWheelEvent wheel(position, view->mapToGlobal(position.toPoint()), QPoint(), QPoint(0, zoom),
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QElapsedTimer timer;timer.start();QApplication::sendEvent(view, &wheel);
            qInfo("Native score zoom: %.3f ms", timer.nsecsElapsed()/1e6);
            QVERIFY(view->matrix() != before);QTest::qWait(100);
        }
        const auto image = view->grab().toImage();int top = image.height(), bottom = -1;
        for (int y = 0; y < image.height(); ++y) {
            int background = 0;
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).rgb() == QColor("#f1f2ef").rgb())++background;
            if (background > 100) {top = qMin(top, y);bottom = qMax(bottom, y);}
        }
        QVERIFY(bottom >= top);
        auto system = note->chord()->measure()->system();
        const double rowTop = view->matrix().map(QPointF(0, system->canvasPos().y()+system->staff(0)->bbox().top())).y();
        const double rowBottom = view->matrix().map(QPointF(0, system->canvasPos().y()+system->staff(1)->bbox().bottom())).y();
        const double frameTop = top/image.devicePixelRatio(), frameBottom = bottom/image.devicePixelRatio();
        const double distance = frameTop >= rowBottom ? frameTop-rowBottom : rowTop-frameBottom;
        view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()))
                             .filePath(QString("band-row-%1-zoom%2-075.png").arg(tick).arg(zoom)));
        QVERIFY2(distance >= 0 && distance < 80, qPrintable(QString("Selected row at %1–%2, band at %3–%4")
                     .arg(rowTop).arg(rowBottom).arg(frameTop).arg(frameBottom)));
        if(tick==1920 && zoom==0) {
            auto last=system->lastMeasure();
            const double right=view->matrix().map(last->canvasPos()+QPointF(last->width(),0)).x();
            const int history=score->undoStack()->getCurIdx();
            QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,QPoint(qRound(right-12),qRound(frameTop+12)));
            QTRY_VERIFY(qobject_cast<QMenu*>(QApplication::activePopupWidget()));
            auto menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            for(int action=2;action<6;++action)QVERIFY(menu->findChild<QAction*>(QString("performanceScoreBandAction%1").arg(action)));
            QVERIFY(menu->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("band-short-menu-075.png")));
            auto match=menu->findChild<QAction*>("performanceScoreBandAction4");
            QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(match).center());
            QTRY_VERIFY(!QApplication::activePopupWidget());QCOMPARE(score->undoStack()->getCurIdx(),history);
        }
        }
    }
    editor->setScoreBandEnabled(false);
}
