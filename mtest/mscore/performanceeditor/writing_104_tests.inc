// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include <QUndoStack>
namespace
{
class RollWindow104
{
    QAction *_restore = nullptr;
    bool _settingsRead = false, _autoBefore = true, _precisionBefore = false;
    QList<bool> _locksBefore;

  public:
    MasterScore *score = nullptr;
    PerformanceEditor *editor = nullptr;
    RollWritingBar *bar = nullptr;
    QWidget *canvas = nullptr;
    QComboBox *track = nullptr;
    QComboBox *grid = nullptr;
    QToolButton *locks = nullptr;
    QToolButton *options = nullptr;
    bool open(MuseScore *main, const QString &file)
    {
        rollWritingAction()->setChecked(false);
        score = expressionScore080(main, file);
        if (!score)
            return false;
        expressionShowMain080(main);
        score->select(toChord(score->findCR(Fraction(), 0))->notes().front());
        score->select(toChord(score->findCR(Fraction::fromTicks(3360), 0))->notes().front(),
                      SelectType::RANGE);
        main->showPerformanceEditor(true);
        editor = main->performanceEditor();
        editor->findChild<QTabBar *>("workflowExpressionTabs")->setCurrentIndex(0);
        bar = editor->findChild<RollWritingBar *>("rollWritingBar");
        canvas = editor->findChild<QWidget *>("performanceNoteCanvas");
        if (!bar || !canvas)
            return false;
        _restore = editor->findChild<QAction *>("editorArea/restore/performanceNotesArea");
        editor->findChild<QAction *>("editorArea/float/performanceNotesArea")->trigger();
        auto dock = qobject_cast<QDockWidget *>(
            canvas->parentWidget()->property("kumoDetachedDock").value<QObject *>());
        if (!dock)
            return false;
        dock->resize(1100, 720);
        dock->show();
        dock->activateWindow();
        QTest::qWait(100);
        rollWritingAction()->setChecked(true);
        if (!ready())
            return false;
        auto mode = bar->findChild<QComboBox *>("rollWriteMode");
        mode->setCurrentIndex(0);
        if (!ready())
            return false;
        track = bar->findChild<QComboBox *>("rollWriteTrack");
        grid = bar->findChild<QComboBox *>("rollWriteGrid");
        locks = bar->findChild<QToolButton *>("rollWriteLocks");
        options = bar->findChild<QToolButton *>("rollWriteOptions");
        if (!track || !grid || !locks || !options)
            return false;
        _autoBefore = locks->menu()->actions().front()->isChecked();
        _precisionBefore = options->menu()->actions().front()->isChecked();
        QMetaObject::invokeMethod(locks->menu(), "aboutToShow");
        for(int index=2;index<locks->menu()->actions().size();++index)
            _locksBefore.append(locks->menu()->actions()[index]->isChecked());
        _settingsRead = true;
        options->menu()->actions().front()->setChecked(false);
        locks->menu()->actions().front()->setChecked(true);
        for (int index = 2; index < 6; ++index)
            if (index < locks->menu()->actions().size() && locks->menu()->actions()[index]->isChecked())
                if (!clickMenu(locks, index))
                    return false;
        editor->fit(false);
        auto pitchScroll = canvas->findChild<QScrollBar *>();
        pitchScroll->setValue((127 - 92) * 100);
        QTest::qWait(60);
        const auto scope = bar->state()["scope"].toMap();
        return scope["start"].toInt() == 0 && scope["end"].toInt() == 3840;
    }
    ~RollWindow104()
    {
        if (_settingsRead) {
            locks->menu()->actions().front()->setChecked(_autoBefore);
            options->menu()->actions().front()->setChecked(_precisionBefore);
            for(int index=0;index<_locksBefore.size();++index)
                if(index+2<locks->menu()->actions().size())locks->menu()->actions()[index+2]->setChecked(_locksBefore[index]);
        }
        rollWritingAction()->setChecked(false);
        if (_restore)
            _restore->trigger();
    }
    bool ready() const
    {
        return QTest::qWaitFor([&] { return bar->state()["ready"].toBool(); }, 10000);
    }
    QPoint point(int tick, int pitch) const
    {
        const auto state = bar->state();
        const auto scroll = canvas->findChild<QScrollBar *>();
        const double top = 127 - scroll->value() / 100., row = canvas->height() * 100. / scroll->pageStep();
        return QPoint(qRound(state["fromX"].toDouble() + (tick - state["scope"].toMap()["start"].toInt()) *
                                                             state["quarterPixels"].toDouble() / 480.),
                      qRound((top - pitch + .5) * row));
    }
    bool draw(int begin, int end, int pitch)
    {
        const auto a = point(begin, pitch), b = point(end, pitch);
        if (!canvas->rect().contains(a) || !canvas->rect().contains(b))
            return false;
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, a);
        qInfo() << "Roll104 press" << begin << end << pitch << a << b << "grabber" << QWidget::mouseGrabber();
        QTest::mouseMove(canvas, b, 30);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, b);
        const bool result = ready();
        qInfo() << "Roll104 result" << bar->state()["status"] << bar->state()["selected"];
        canvas->window()->grab().save(
            QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")).filePath("roll-probe-104.png"));
        return result;
    }
    bool clickMenu(QToolButton *button, int index)
    {
        bool clicked = false;
        auto menu = button->menu();
        QTimer click, timeout;
        click.setInterval(10);
        timeout.setSingleShot(true);
        QObject::connect(&click, &QTimer::timeout, menu,
                         [&]
                         {
                             if (menu->isVisible() && index < menu->actions().size())
                             {
                                 const auto rect = menu->actionGeometry(menu->actions()[index]);
                                 if (!rect.isEmpty())
                                 {
                                     clicked = true;
                                     click.stop();
                                     QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, rect.center());
                                 }
                             }
                         });
        QObject::connect(&timeout, &QTimer::timeout, menu, &QMenu::hide);
        click.start();
        timeout.start(2000);
        QTest::qWait(20);
        QTest::mouseClick(button, Qt::LeftButton);
        qInfo() << "Roll104 menu" << index << clicked << button->isEnabled() << button->isVisible();
        return clicked;
    }
};
QVariantMap sourceVoice104() { return {{"start", 0}, {"end", 7680}, {"firstTrack", 0}, {"endTrack", 1}}; }
QStringList midiPitch104(Score *score, int pitch)
{
    QStringList rows;
    for (const auto &value : renderPlaybackPreview(score, score).events)
    {
        const auto &e = value.second;
        if (e.pitch() == pitch && (e.type() == ME_NOTEON || e.type() == ME_NOTEOFF))
            rows.append(QString("%1:%2").arg(value.first).arg(e.velo()));
    }
    return rows;
}
QStringList voiceDetails104(Score *score)
{
    QStringList rows;
    for (auto s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
    {
        auto e = s->element(0);
        if (!e || !e->isChord())
            continue;
        auto c = toChord(e);
        for (auto note : c->notes())
        {
            QString row = QString("%1:%2:%3:%4:%5:%6:%7:%8")
                              .arg(c->tick().ticks())
                              .arg(c->actualTicks().ticks())
                              .arg(note->pitch())
                              .arg(note->tpc1())
                              .arg(note->tpc2())
                              .arg(int(note->veloType()))
                              .arg(note->veloOffset())
                              .arg(int(c->playEventType()));
            if (c->playEventType() == PlayEventType::User)
                for (const auto &event : note->playEvents())
                    row += QString("/%1,%2,%3").arg(event.pitch()).arg(event.ontime()).arg(event.len());
            rows.append(row);
        }
    }
    return rows;
}
QStringList originalMidi104(Score *score)
{
    const auto material = renderPlaybackPreview(score, score);
    QStringList rows;
    for (const auto &value : material.events)
    {
        const auto &e = value.second;
        if (e.pitch() < 80 && (e.type() == ME_NOTEON || e.type() == ME_NOTEOFF))
            rows.append(QString("%1:%2:%3:%4:%5")
                            .arg(value.first)
                            .arg(e.type())
                            .arg(e.channel())
                            .arg(e.pitch())
                            .arg(e.velo()));
    }
    return rows;
}
} // namespace

void TestPerformanceEditor::rollOverlapVoiceLocksAndRetry()
{
    expressionHost080(_directory.path());
    RollWindow104 w;
    QVERIFY(w.open(Ms::mscore, _directory.filePath("roll-overlap-104.mscx")));
    const auto identity = rollNotationIdentity(w.score, sourceVoice104());
    const auto details = voiceDetails104(w.score);
    const auto midi = originalMidi104(w.score);
    auto oldNote = toChord(w.score->findCR(Fraction(), 0))->notes().front();
    w.track->setCurrentIndex(w.track->findData(0));
    w.grid->setCurrentIndex(1);
    QVERIFY(w.draw(240, 720, 84));
    auto added = w.score->findCR(Fraction::fromTicks(240), 1);
    QVERIFY(added && added->isChord());
    QCOMPARE(toChord(added)->notes().front()->pitch(), 84);
    QCOMPARE(oldNote, toChord(w.score->findCR(Fraction(), 0))->notes().front());
    QCOMPARE(voiceDetails104(w.score), details);
    QCOMPARE(rollNotationIdentity(w.score, sourceVoice104()), identity);
    QCOMPARE(originalMidi104(w.score), midi);
    // All other voices are locked through the menu that the user operates.
    for (int index : {3, 4, 5})
        QVERIFY(w.clickMenu(w.locks, index));
    w.grid->setCurrentIndex(2);
    const auto rows = w.bar->state()["notes"].toList();
    const int undo = w.score->undoStack()->getCurIdx();
    QVERIFY(w.draw(120, 360, 85));
    QCOMPARE(w.bar->state()["notes"].toList(), rows);
    QCOMPARE(w.score->undoStack()->getCurIdx(), undo);
    QVERIFY(w.bar->state()["status"].toString().contains(QString::fromUtf8("四个声部")));
    QVERIFY(w.clickMenu(w.locks, 5));
    QVERIFY(w.draw(120, 360, 85));
    added = w.score->findCR(Fraction::fromTicks(120), 3);
    QVERIFY(added && added->isChord());
    QCOMPARE(toChord(added)->notes().front()->pitch(), 85);
    QCOMPARE(w.score->undoStack()->getCurIdx(), undo + 1);
    QCOMPARE(rollNotationIdentity(w.score, sourceVoice104()), identity);
    QFileInfo file(_directory.filePath("roll-overlap-applied-104.mscz"));
    QVERIFY(w.score->saveCompressedFile(file, false, false));
    std::unique_ptr<MasterScore> saved(Ms::mscore->readScore(file.absoluteFilePath()));
    QVERIFY(saved);
    QCOMPARE(rollNotationIdentity(saved.get(), sourceVoice104()), identity);
    QCOMPARE(originalMidi104(saved.get()), midi);
    QVERIFY(saved->findCR(Fraction::fromTicks(120), 3)->isChord());
}

void TestPerformanceEditor::rollPolyphonyKeepsIndependentLengths()
{
    expressionHost080(_directory.path());
    RollWindow104 w;
    QVERIFY(w.open(Ms::mscore, _directory.filePath("roll-polyphony-104.mscx")));
    const auto identity = rollNotationIdentity(w.score, sourceVoice104());
    w.track->setCurrentIndex(w.track->findData(1));
    w.grid->setCurrentIndex(1);
    QVERIFY(w.clickMenu(w.locks, 0)); // Keep both pitches in this voice.
    QVERIFY(w.draw(0, 960, 76));
    QVERIFY(w.draw(480, 720, 79));
    auto a = toChord(w.score->findCR(Fraction(), 1));
    QVERIFY(a && a->notes().front()->tieFor());
    auto middle = toChord(w.score->findCR(Fraction::fromTicks(480), 1));
    QVERIFY(middle);
    QCOMPARE(middle->notes().size(), size_t(2));
    for (auto note : middle->notes())
        if (note->pitch() == 76)
        {
            QVERIFY(note->tieBack());
            QVERIFY(note->tieFor());
        }
        else
        {
            QCOMPARE(note->pitch(), 79);
            QVERIFY(!note->tieBack());
            QVERIFY(!note->tieFor());
        }
    const auto low = midiPitch104(w.score, 76), high = midiPitch104(w.score, 79);
    qInfo() << "Independent roll lengths" << low << high;
    QCOMPARE(low.size(), 2);
    QVERIFY(low.front().startsWith("0:"));
    QVERIFY(low.back().startsWith("959:"));
    QCOMPARE(high.size(), 2);
    QVERIFY(high.front().startsWith("480:"));
    QVERIFY(high.back().startsWith("719:"));
    QCOMPARE(rollNotationIdentity(w.score, sourceVoice104()), identity);
    const auto output = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _directory.path()));
    QVERIFY(w.canvas->window()->grab().save(output.filePath("roll-independent-lengths-104.png")));
    getAction("undo")->trigger();
    QVERIFY(w.ready());
    QVERIFY(midiPitch104(w.score, 79).isEmpty());
    getAction("redo")->trigger();
    QVERIFY(w.ready());
    QCOMPARE(midiPitch104(w.score, 76), low);
    QCOMPARE(midiPitch104(w.score, 79), high);
    QFileInfo file(_directory.filePath("roll-polyphony-applied-104.mscz"));
    QVERIFY(w.score->saveCompressedFile(file, false, false));
    std::unique_ptr<MasterScore> saved(Ms::mscore->readScore(file.absoluteFilePath()));
    QVERIFY(saved);
    QCOMPARE(midiPitch104(saved.get(), 76), low);
    QCOMPARE(midiPitch104(saved.get(), 79), high);
}

void TestPerformanceEditor::rollTripletsCrossBarAndQuantize()
{
    expressionHost080(_directory.path());
    RollWindow104 w;
    QVERIFY(w.open(Ms::mscore, _directory.filePath("roll-triplets-104.mscx")));
    const auto identity = rollNotationIdentity(w.score, sourceVoice104());
    w.track->setCurrentIndex(w.track->findData(2));
    w.grid->setCurrentIndex(4);
    QVERIFY(w.clickMenu(w.locks, 0));
    const int undo = w.score->undoStack()->getCurIdx();
    QVERIFY(w.draw(0, 160, 84));
    QVERIFY(w.draw(160, 320, 86));
    QVERIFY(w.draw(320, 480, 88));
    for (int tick : {0, 160, 320})
    {
        auto chord = toChord(w.score->findCR(Fraction::fromTicks(tick), 2));
        QVERIFY(chord && chord->tuplet());
        QCOMPARE(chord->actualTicks().ticks(), 160);
    }
    w.grid->setCurrentIndex(2);
    auto quantize = w.bar->findChild<QToolButton *>("rollWriteQuantize");
    QVERIFY(quantize);
    QTest::mouseClick(quantize, Qt::LeftButton);
    QVERIFY(w.ready());
    QCOMPARE(toChord(w.score->findCR(Fraction::fromTicks(320), 2))->actualTicks().ticks(), 160);
    w.grid->setCurrentIndex(1);
    QVERIFY(w.draw(1680, 2160, 90));
    auto first = toChord(w.score->findCR(Fraction::fromTicks(1680), 2));
    QVERIFY(first);
    QVERIFY(first->notes().front()->tieFor());
    QCOMPARE(first->actualTicks().ticks(), 240);
    const auto before = midiPitch104(w.score, 90);
    QCOMPARE(before.size(), 2);
    QVERIFY(before.front().startsWith("1680:"));
    QVERIFY(before.back().startsWith("2159:"));
    w.grid->setCurrentIndex(0);
    QTest::mouseClick(quantize, Qt::LeftButton);
    QVERIFY(w.ready());
    auto moved = toChord(w.score->findCR(Fraction::fromTicks(1920), 2));
    QVERIFY(moved);
    QCOMPARE(moved->actualTicks().ticks(), 480);
    QCOMPARE(moved->notes().front()->pitch(), 90);
    QVERIFY(!moved->notes().front()->tieBack());
    QVERIFY(!moved->notes().front()->tieFor());
    const auto after = midiPitch104(w.score, 90);
    QCOMPARE(after.size(), 2);
    QVERIFY(after.front().startsWith("1920:"));
    QVERIFY(after.back().startsWith("2399:"));
    QCOMPARE(rollNotationIdentity(w.score, sourceVoice104()), identity);
    QVERIFY(w.score->undoStack()->getCurIdx() > undo);
    QFileInfo file(_directory.filePath("roll-triplets-applied-104.mscz"));
    QVERIFY(w.score->saveCompressedFile(file, false, false));
    std::unique_ptr<MasterScore> saved(Ms::mscore->readScore(file.absoluteFilePath()));
    QVERIFY(saved);
    QCOMPARE(midiPitch104(saved.get(), 90), after);
    QVERIFY(toChord(saved->findCR(Fraction(), 2))->tuplet());
    getAction("undo")->trigger();
    QVERIFY(w.ready());
    QCOMPARE(midiPitch104(w.score, 90), before);
    getAction("redo")->trigger();
    QVERIFY(w.ready());
    QCOMPARE(midiPitch104(w.score, 90), after);
}

void TestPerformanceEditor::rhythmCorrectByNamedSourceAndUndo()
{
    expressionHost080(_directory.path());
    auto main = Ms::mscore;
    QSettings().setValue("workflow/recordingDirectory", _directory.filePath("rhythm-recordings-104"));
    auto score = expressionScore080(main, _directory.filePath("rhythm-correct-104.mscx"));
    QVERIFY(score);
    expressionShowMain080(main);
    score->select(toChord(score->findCR(Fraction(), 0))->notes().front());
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    auto tabs = editor->findChild<QTabBar *>("workflowExpressionTabs");
    tabs->setCurrentIndex(3);
    auto pane = editor->findChild<RhythmPane *>();
    QVERIFY(pane);
    struct Restore
    {
        QTabBar *tabs;
        QAction *area = nullptr;
        ~Restore()
        {
            if (area)
                area->trigger();
            tabs->setCurrentIndex(0);
        }
    } restore{tabs};
    auto table = pane->findChild<QTableWidget *>("rhythmDraftTable");
    auto status = pane->findChild<QLabel *>("rhythmStatus");
    auto choose = pane->findChild<QPushButton *>("rhythmChooseSource");
    auto ignore = pane->findChild<QPushButton *>("rhythmIgnoreTap");
    auto apply = pane->findChild<QPushButton *>("rhythmApply");
    QVERIFY(table && status && choose && ignore && apply);
    QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 4, 10000);
    restore.area = editor->findChild<QAction *>("editorArea/restore/rhythmWritingPane");
    QVERIFY(restore.area);
    editor->findChild<QAction *>("editorArea/float/rhythmWritingPane")->trigger();
    auto dock = qobject_cast<QDockWidget *>(pane->property("kumoDetachedDock").value<QObject *>());
    QVERIFY(dock);
    dock->resize(1100, 620);
    dock->activateWindow();
    QTest::qWait(80);
    auto edits = pane->findChild<QUndoStack *>();
    QVERIFY(edits);
    table->setCurrentCell(0, 0);
    QVERIFY(choose->isEnabled());
    QVERIFY(!ignore->isEnabled());
    auto capture = pane->findChild<MidiCaptureSession *>();
    QVERIFY(capture);
    QSignalSpy finished(capture, &MidiCaptureSession::finished);
    QVERIFY(!capture->begin({{"monitor", false}, {"endMode", "manual"}}, false).contains("error"));
    for (int i = 0; i < 3; ++i)
    {
        QVERIFY(captureMidiInput(NPlayEvent(ME_NOTEON, 0, 30, 90 + i * 5), midiInputTime()));
        QTest::qWait(35);
        QVERIFY(captureMidiInput(NPlayEvent(ME_NOTEOFF, 0, 30, 0), midiInputTime()));
        if (i < 2)
            QTest::qWait(i == 0 ? 115 : 265);
    }
    capture->stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 10000);
    int estimated = -1, actual = -1;
    for (int i = 0; i < table->rowCount(); ++i)
    {
        if (table->item(i, 5)->text().contains(QString::fromUtf8("漏敲")))
            estimated = i;
        else if (table->item(i, 0)->text().toInt() > 0)
            actual = i;
    }
    qInfo() << "Rhythm104 recorded" << status->text() << "rows" << table->rowCount() << "estimated / actual"
            << estimated << actual;
    QVERIFY(estimated >= 0 && actual >= 0);
    table->setCurrentCell(estimated, 0);
    const int source = table->item(estimated, 0)->text().toInt();
    const int corrections = edits->index();
    QTest::mouseClick(choose, Qt::LeftButton);
    auto combo = table->findChild<QComboBox *>("rhythmSourceChoice");
    QTRY_VERIFY(combo && combo->view()->isVisible());
    QVERIFY(combo->count() == 5);
    QVERIFY(combo->itemText(1).contains("C4"));
    QVERIFY(combo->itemText(2).contains("D4"));
    QVERIFY(combo->itemText(1).contains(QString::fromUtf8("原谱第")));
    const auto output = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _directory.path()));
    QVERIFY(combo->view()->grab().save(output.filePath("rhythm-source-choices-104.png")));
    const auto index = combo->model()->index(combo->findData(source), 0);
    combo->view()->scrollTo(index);
    QTest::mouseClick(combo->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      combo->view()->visualRect(index).center());
    QTRY_VERIFY(apply->isEnabled());
    QTRY_VERIFY(!table->item(estimated, 5)->text().contains(QString::fromUtf8("漏敲")));
    QCOMPARE(edits->index(), corrections + 1);
    QVERIFY(!ignore->isEnabled());
    table->setCurrentCell(actual, 0);
    QTRY_VERIFY(ignore->isEnabled());
    const int rows = table->rowCount(), number = table->item(actual, 0)->text().toInt();
    QTest::mouseClick(ignore, Qt::LeftButton);
    QTRY_COMPARE(table->item(actual, 0)->text(), QString("0"));
    QCOMPARE(edits->index(), corrections + 2);
    QVERIFY(!ignore->isEnabled());
    QTest::keyClick(table, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE(table->rowCount(), rows);
    QTRY_COMPARE(table->item(actual, 0)->text().toInt(), number);
    QCOMPARE(edits->index(), corrections + 1);
    QTRY_VERIFY(apply->isEnabled());
    const int undo = score->undoStack()->getCurIdx();
    QTest::mouseClick(apply, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QString::fromUtf8("已采用")), 10000);
    QCOMPARE(score->undoStack()->getCurIdx(), undo + 1);
    QVector<int> pitches;
    for (auto s = score->firstSegment(SegmentType::ChordRest); s && s->tick().ticks() < 1920;
         s = s->next1(SegmentType::ChordRest))
    {
        auto e = s->element(0);
        if (e && e->isChord())
            for (auto note : toChord(e)->notes())
                if (!note->tieBack())
                    pitches.append(note->pitch());
    }
    QCOMPARE(pitches, QVector<int>({60, 62, 64, 65}));
    QFileInfo file(_directory.filePath("rhythm-correct-applied-104.mscz"));
    QVERIFY(score->saveCompressedFile(file, false, false));
    std::unique_ptr<MasterScore> saved(main->readScore(file.absoluteFilePath()));
    QVERIFY(saved);
    QVERIFY(pane->grab().save(output.filePath("rhythm-corrected-104.png")));
    getAction("undo")->trigger();
    QTRY_COMPARE(score->undoStack()->getCurIdx(), undo);
    getAction("redo")->trigger();
    QTRY_COMPARE(score->undoStack()->getCurIdx(), undo + 1);
    auto reopened=saved.release();main->setCurrentScoreView(main->appendScore(reopened));
    QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),4,10000);
    const bool dirty=reopened->dirty();const int history=reopened->undoStack()->getCurIdx();
    auto reuse=pane->findChild<QPushButton*>("rhythmRestoreRecording");QVERIFY(reuse);QTest::mouseClick(reuse,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);QCOMPARE(table->rowCount(),4);
    for(int row=0;row<table->rowCount();++row)QVERIFY(!table->item(row,5)->text().contains(QString::fromUtf8("漏敲")));
    QCOMPARE(reopened->dirty(),dirty);QCOMPARE(reopened->undoStack()->getCurIdx(),history);
}
