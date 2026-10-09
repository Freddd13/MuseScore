// Included in TestPerformanceEditor: exercise the native host, not a UI mock.
      void finePedalNativeLayoutMidiUndoAndPersistence()
            {
            std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
            score->startCmd(); score->appendMeasures(1); score->endCmd();
            score->doLayout();
            auto segmentCount = [](Score* s) { int n = 0; for (auto segment = s->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest)) ++n; return n; };
            const int originalSegments = segmentCount(score.get());
            QVERIFY(ParameterEdit::pedal(score.get(), 4, 615, 1350, nullptr));
            Pedal* pedal = nullptr;
            for (const auto& item : score->spannerMap().map()) if (item.second->isPedal()) pedal = toPedal(item.second);
            QVERIFY(pedal); score->doLayout(); QVERIFY(!pedal->spannerSegments().empty());
            score->startCmd(); score->checkSpanner(Fraction(0, 1), score->endTick()); score->endCmd();
            bool retained = false; for (const auto& item : score->spannerMap().map()) if (item.second == pedal) retained = true;
            QVERIFY(retained); QCOMPARE(pedal->tick().ticks(), 615); QCOMPARE(segmentCount(score.get()), originalSegments);
            auto line = pedal->frontSegment(); QVERIFY(line); QVERIFY(line->system());
            const auto a = line->pos(), b = a + line->pos2(); QVERIFY(b.x() > a.x());
            auto s0 = score->tick2segment(Fraction::fromTicks(480), false, SegmentType::ChordRest);
            auto s1 = score->tick2segment(Fraction::fromTicks(960), false, SegmentType::ChordRest);
            const double expected = s0->pos().x() + s0->measure()->pos().x() + (s1->pos().x() - s0->pos().x()) * (615 - 480) / 480.0;
            QVERIFY(std::abs(a.x() - expected) < .001);
            EventMap events; score->renderMidi(&events, false, false, defaultState);
            bool on = false, off = false;
            for (const auto& item : events) if (item.second.type() == ME_CONTROLLER && item.second.controller() == CTRL_SUSTAIN) {
                  if (item.second.value() == 127) { QCOMPARE(item.first, 615 + 3 - MScore::pedalEventsMinTicks); on = true; }
                  if (item.second.value() == 0 && item.first > 0) { QCOMPARE(item.first, 1350 + 2 - MScore::pedalEventsMinTicks); off = true; }
                  }
            QVERIFY(on); QVERIFY(off);
            const QString path = _directory.path() + "/fine-pedal.mscx";
            QVERIFY(saveScore(score.get(), path)); std::unique_ptr<MasterScore> reopened(readCreatedScore(path)); QVERIFY(reopened);
            QCOMPARE(segmentCount(reopened.get()), originalSegments);
            bool found = false;
            for (const auto& item : reopened->spannerMap().map()) if (item.second->isPedal()) {
                  QCOMPARE(item.second->tick().ticks(), 615); QCOMPARE(item.second->tick2().ticks(), 1350); found = true;
                  }
            QVERIFY(found);
            QFile::remove("performance-fine-pedal-016.mscx"); QVERIFY(QFile::copy(path, "performance-fine-pedal-016.mscx"));
            QVERIFY(ParameterEdit::pedal(score.get(), 4, 637, 1409, pedal));
            score->undoRedo(true, &ed); QCOMPARE(pedal->tick().ticks(), 615); QCOMPARE(pedal->tick2().ticks(), 1350);
            score->undoRedo(false, &ed); QCOMPARE(pedal->tick().ticks(), 637); QCOMPARE(pedal->tick2().ticks(), 1409);
            QVERIFY(ParameterEdit::pedal(score.get(), 4, 615, 2535, pedal)); score->doLayout();
            QVERIFY(saveScore(score.get(), path)); reopened.reset(readCreatedScore(path)); QVERIFY(reopened);
            found = false; for (const auto& item : reopened->spannerMap().map()) if (item.second->isPedal()) {
                  QCOMPARE(item.second->tick().ticks(), 615); QCOMPARE(item.second->tick2().ticks(), 2535); found = true;
                  }
            QVERIFY(found);
            const int endTick = score->endTick().ticks();
            for (const auto interval : {qMakePair(1, 15), qMakePair(615, endTick), qMakePair(endTick - 17, endTick - 1)}) {
                  QVERIFY(ParameterEdit::pedal(score.get(), 4, interval.first, interval.second, pedal)); score->doLayout();
                  QVERIFY(saveScore(score.get(), path)); reopened.reset(readCreatedScore(path)); QVERIFY(reopened);
                  found = false; for (const auto& item : reopened->spannerMap().map()) if (item.second->isPedal()) {
                        QCOMPARE(item.second->tick().ticks(), interval.first); QCOMPARE(item.second->tick2().ticks(), interval.second); found = true;
                        }
                  QVERIFY(found);
                  }
            }
      void scoreControlsTempoNodesFinePedalAndTips()
            {
            auto main = Ms::mscore; QVERIFY(main);
            auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx"); QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score)); main->resize(1280, 1000); main->show();
            auto editor = main->performanceEditor(); editor->findChild<QAction*>("performanceBandHeight80")->trigger(); editor->show(); QTest::qWait(80);
            auto view = main->currentScoreView();
            auto parameter = editor->findChild<QComboBox*>("performanceParameter"); auto tool = editor->findChild<QComboBox*>("performanceTool");
            auto timeline = editor->findChild<QWidget*>("performanceParameterCanvas"); QVERIFY(timeline);
            parameter->setCurrentIndex(0); tool->setCurrentIndex(0); editor->findChild<QComboBox*>("performanceAxis")->setCurrentIndex(0);
            editor->findChild<QComboBox*>("performanceScope")->setCurrentIndex(2);
            for (auto check : editor->findChildren<QCheckBox*>()) check->setChecked(true);
            auto note = noteAt(score, 480, 0); QVERIFY(PerformanceSelection::apply(score, {note}, note)); editor->selectionChanged(); editor->setSelectedValue(80); editor->fit(true); QTest::qWait(80);
            const QImage before = timeline->grab().toImage(); view->repaint();
            const QImage stripBefore = view->grab().toImage();
            auto system = score->firstMeasure()->system(); const double right = qMin(double(view->width() - 12), view->matrix().map(system->lastMeasure()->canvasPos() + QPointF(system->lastMeasure()->width(), 0)).x());
            const double left = qMax(56.0, view->matrix().map(system->firstMeasure()->canvasPos()).x());
            const int w = right - left + 54 < 400 ? 22 : 30;
            const QPoint plus(qRound(right + 4 - 6 * w - 3 + w + (w - 2) / 2.0), view->height() - 80 - 26 - 26 + 12);
            const auto matrix = view->matrix(); const auto position = score->playPos(); const int undo = score->undoStack()->getCurIdx();
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, plus); QTest::qWait(30);
            QCOMPARE(view->matrix(), matrix); QCOMPARE(score->playPos(), position); QCOMPARE(score->undoStack()->getCurIdx(), undo);
            QCOMPARE(timeline->grab().toImage(), before);
            const QImage stripAfter = view->grab().toImage(); const double ratio = stripAfter.devicePixelRatio();
            const QRect axis(qRound((left - 48) * ratio), qRound((view->height() - 106) * ratio), qRound(46 * ratio), qRound(80 * ratio));
            QVERIFY(stripBefore.copy(axis) != stripAfter.copy(axis));
            view->grab().save("performance-score-axis-016.png");
            auto staffButton = editor->findChild<QToolButton*>("performanceStaffFilter"); QVERIFY(staffButton);
            QVERIFY(QMetaObject::invokeMethod(staffButton->menu(), "aboutToShow", Qt::DirectConnection));
            auto staff0 = staffButton->menu()->findChild<QAction*>("performanceStaff0"); QVERIFY(staff0); staff0->setChecked(false);
            editor->setSelectedValue(95); QTest::qWait(30); QCOMPARE(note->customizeVelocity(NoteVelocity::referenceBase(note)), 80); const auto nativeSelection = score->selection().noteList(); QVERIFY(std::find(nativeSelection.begin(), nativeSelection.end(), note) != nativeSelection.end());
            staff0->setChecked(true);
            auto tips = editor->findChild<QAction*>("performanceNoteTooltips"); QVERIFY(tips); tips->setChecked(false);
            QTest::mouseClick(editor->findChild<QToolButton*>("performancePitchReset"), Qt::LeftButton); QTest::qWait(30);
            auto notes = editor->findChild<QWidget*>("performanceNoteCanvas"); auto pitch = notes->findChild<QScrollBar*>();
            const QPoint hit(80, qRound((127 - pitch->value() / 100.0 - note->ppitch()) * 12 + 6));
            QHelpEvent help(QEvent::ToolTip, hit, notes->mapToGlobal(hit)); QApplication::sendEvent(notes, &help); QTest::qWait(20); QVERIFY(!QToolTip::isVisible()); tips->setChecked(true);
            parameter->showPopup(); QTest::qWait(40); parameter->view()->grab().save("performance-parameter-popup-016.png"); parameter->hidePopup();
            parameter->setCurrentIndex(1); tool->setCurrentIndex(0); editor->fit(); QTest::qWait(30);
            auto scroll = editor->findChild<QScrollBar*>(QString(), Qt::FindDirectChildrenOnly); QVERIFY(scroll);
            auto x = [&](int tick) { return 76 + qRound((tick - scroll->value()) * (timeline->width() - 90.0) / scroll->pageStep()); };
            const int y = 18 + qRound((240 - 100) * (timeline->height() - 36) / 235.0);
            QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(x(480), y)); QTest::qWait(50);
            QVERIFY(std::abs(score->tempo(Fraction::fromTicks(480)) * 60 - 100) < 2);
            const auto oldTempo = score->tempo(Fraction::fromTicks(480));
            const int tempoUndo = score->undoStack()->getCurIdx();
            QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(x(480), y)); QTest::qWait(30);
            QCOMPARE(score->tempo(Fraction::fromTicks(480)), oldTempo); QCOMPARE(score->undoStack()->getCurIdx(), tempoUndo);
            QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(x(480), y));
            QMouseEvent motion(QEvent::MouseMove, QPointF(x(480), y - 12), Qt::NoButton, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(timeline, &motion);
            QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(x(480), y - 12)); QTest::qWait(50);
            QVERIFY(score->tempo(Fraction::fromTicks(480)) > oldTempo); editor->grab().save("performance-tempo-nodes-016.png");
            parameter->setCurrentIndex(2); QTest::qWait(40); auto grid = editor->findChild<QComboBox*>("performancePedalGrid"); QVERIFY(grid->isVisible()); grid->setCurrentIndex(2);
            QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(x(615), 25));
            QMouseEvent pedalMove(QEvent::MouseMove, QPointF(x(1335), 25), Qt::NoButton, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(timeline, &pedalMove);
            QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(x(1335), 25)); QTest::qWait(60);
            Pedal* pedal = nullptr; for (const auto& item : score->spannerMap().map()) if (item.second->isPedal()) pedal = toPedal(item.second);
            QVERIFY(pedal); QVERIFY(std::abs(pedal->tick().ticks() - 615) <= 15); QVERIFY(std::abs(pedal->tick2().ticks() - 1335) <= 15);
            QCOMPARE(pedal->tick().ticks() % 30, 0); QCOMPARE(pedal->tick2().ticks() % 30, 0);
            const QImage pedalImage = timeline->grab().toImage(); const double dpr = pedalImage.devicePixelRatio();
            auto pixel = [&](int y) { return pedalImage.pixelColor(qRound(x(900) * dpr), qRound(y * dpr)); };
            const QColor bg = pixel(8);
            auto difference = [&](QColor color) { return std::abs(color.red() - bg.red()) + std::abs(color.green() - bg.green()) + std::abs(color.blue() - bg.blue()); };
            QVERIFY(difference(pixel(18)) > difference(pixel(timeline->height() / 2)) + 20);
            timeline->grab().save("performance-fine-pedal-016.png"); editor->grab().save("performance-editor-016.png"); view->grab().save("performance-score-pedal-016.png");
            parameter->setCurrentIndex(0); grid->setCurrentIndex(0); editor->findChild<QAction*>("performanceBandHeight48")->trigger();
            }
      void finePedalLinkedExcerpt()
            {
            std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
            score->startCmd(); score->appendMeasures(1); score->endCmd();
            score->startCmd(); score->insertMeasure(ElementType::VBOX, score->first()); score->endCmd();
            auto part = new Score(score.get()); auto excerpt = new Excerpt(score.get()); excerpt->setPartScore(part);
            excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano"); Excerpt::createExcerpt(excerpt); score->excerpts().append(excerpt); part->doLayout();
            QVERIFY(ParameterEdit::pedal(part, 4, 615, 2535, nullptr));
            bool found = false; for (const auto& item : score->spannerMap().map()) if (item.second->isPedal()) {
                  QCOMPARE(item.second->tick().ticks(), 615); QCOMPARE(item.second->tick2().ticks(), 2535); found = true;
                  }
            QVERIFY(found); score->undoRedo(true, &ed);
            for (const auto& item : part->spannerMap().map()) QVERIFY(!item.second->isPedal());
            score->undoRedo(false, &ed); part->doLayout();
            found = false; for (const auto& item : part->spannerMap().map()) if (item.second->isPedal()) {
                  QCOMPARE(item.second->tick().ticks(), 615); QCOMPARE(item.second->tick2().ticks(), 2535); QVERIFY(!item.second->spannerSegments().empty()); found = true;
                  }
            QVERIFY(found);
            }
