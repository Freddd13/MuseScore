#include "libmscore/stafftext.h"
// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
namespace {
QMap<int, QVariantMap> pedalGraph106(PerformanceEditor* editor) {
    QMap<int, QVariantMap> rows;
    for (const auto& row : editor->findChild<QWidget*>("performanceParameterCanvas")->property("pedalPlaybackRanges").toList()) {
        const auto map = row.toMap(); rows.insert(map.value("from").toInt(), map);
    }
    return rows;
}
}
void TestPerformanceEditor::pedalGraphMatchesPlayback106() {
    expressionHost080(_directory.path()); auto main = Ms::mscore; QVERIFY(main);
    auto score = fixture(); QVERIFY(score);
    QVERIFY(ParameterEdit::pedal(score, 4, 0, 480, nullptr)); QVERIFY(ParameterEdit::pedal(score, 4, 480, 1920, nullptr));
    PedalPlayback::Settings settings; settings.piano = true;
    QVERIFY(ParameterEdit::pedalPlayback(score, score->parts().front(), nullptr, settings, false));
    PedalWindow105 window; QVERIFY(window.open(main, score));
    QTRY_VERIFY(window.canvas->property("pedalPlaybackReady").toBool());
    const auto actual = sustain106(score); const auto rows = pedalGraph106(window.editor); QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].value("down").toInt(), actual[0].first); QCOMPARE(rows[0].value("up").toInt(), actual[1].first);
    QCOMPARE(rows[480].value("down").toInt(), actual[2].first); QCOMPARE(rows[480].value("up").toInt(), actual[3].first);
    QVERIFY(rows[480].value("ready").toBool());
    const auto state = score->state(); const int undo = score->undoStack()->getCurIdx();
    window.editor->fit(false); window.dock->resize(1040, 600); QTest::qWait(100);
    QCOMPARE(pedalGraph106(window.editor), rows); QCOMPARE(score->state(), state); QCOMPARE(score->undoStack()->getCurIdx(), undo);
    const auto output = qEnvironmentVariable("P2_PEDAL_GRAPH_IMAGE");
    if (!output.isEmpty()) QVERIFY(window.dock->grab().save(output));
    qInfo() << "pedal preview max batch ns" << window.canvas->property("pedalPlaybackMaxBatchNs");
}
void TestPerformanceEditor::pedalGraphDragAndCancel106() {
    expressionHost080(_directory.path()); auto main = Ms::mscore; QVERIFY(main);
    auto score = fixture(); QVERIFY(score);
    QVERIFY(ParameterEdit::pedal(score, 4, 0, 480, nullptr)); QVERIFY(ParameterEdit::pedal(score, 4, 480, 1920, nullptr));
    PedalPlayback::Settings settings; settings.piano = true;
    QVERIFY(ParameterEdit::pedalPlayback(score, score->parts().front(), nullptr, settings, false));
    PedalWindow105 window; QVERIFY(window.open(main, score));
    QTRY_VERIFY(window.canvas->property("pedalPlaybackReady").toBool());
    auto second = pedalAt105(score, 480); QVERIFY(second);
    window.drag(window.point(480, 6), window.point(720, 6));
    QCOMPARE(second->tick().ticks(), 720); QTRY_VERIFY(window.canvas->property("pedalPlaybackReady").toBool());
    QCOMPARE(pedalGraph106(window.editor)[720].value("down").toInt(), sustain106(score)[2].first);
    score->undoRedo(true, &ed); main->endCmd(); QTest::qWait(100);
    QCOMPARE(second->tick().ticks(), 480); QTRY_VERIFY(window.canvas->property("pedalPlaybackReady").toBool());
    const int undo = score->undoStack()->getCurIdx();
    QTest::mousePress(window.canvas, Qt::LeftButton, Qt::NoModifier, window.point(480, 6));
    QMouseEvent move(QEvent::MouseMove, QPointF(window.point(720, 6)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.canvas, &move); QTest::keyClick(window.canvas, Qt::Key_Escape);
    QTest::mouseRelease(window.canvas, Qt::LeftButton, Qt::NoModifier, window.point(720, 6));
    QCOMPARE(second->tick().ticks(), 480); QCOMPARE(score->undoStack()->getCurIdx(), undo);
    window.editor->findChild<QCheckBox*>("performanceScoreBand")->setChecked(false);
    window.editor->hide(); QTest::qWait(70); QVERIFY(!window.canvas->property("pedalPlaybackBusy").toBool());
    window.editor->show(); QTest::qWait(100); QTRY_VERIFY(window.canvas->property("pedalPlaybackReady").toBool());
}
void TestPerformanceEditor::pedalGraphAfterPlayAndHide106() {
    expressionHost080(_directory.path()); auto main = Ms::mscore; QVERIFY(main); QVERIFY(!Ms::seq);
    auto score = expressionScore080(main, _directory.filePath("pedal-playing-106.mscx")); QVERIFY(score);
    for (int bar = 0; bar < 4; ++bar) QVERIFY(ParameterEdit::pedal(score, 0, bar * 1920, (bar + 1) * 1920, nullptr));
    PedalPlayback::Settings settings; settings.piano = true;
    QVERIFY(ParameterEdit::pedalPlayback(score, score->parts().front(), nullptr, settings, false));
    score->startCmd(); score->insertMeasure(ElementType::VBOX, score->first()); score->endCmd();
    auto linked = new Score(score); auto excerpt = new Excerpt(score);
    excerpt->setPartScore(linked); excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano");
    score->startCmd(); Excerpt::createExcerpt(excerpt); score->excerpts().append(excerpt); score->endCmd(); linked->doLayout();
    main->showPerformanceEditor(false); expressionShowMain080(main);
    MasterSynthesizer synth; MScore::sampleRate = 44100; auto recording = new PerformanceRecordingSynth;
    synth.registerSynthesizer(recording); synth.setSampleRate(44100); synth.init();
    Ms::seq = new PerformanceAuditionSeq; auto sequence = Ms::seq;
    std::unique_ptr<Seq, std::function<void(Seq*)>> guard(sequence, [](Seq* s) {
        stopPlaybackPreview(true); s->stopWait(); s->waitForStoppedRendering(); s->setScoreView(nullptr); delete s; Ms::seq = nullptr;
    });
    auto driver = new PerformanceTestDriver(sequence); sequence->setDriver(driver); sequence->setMasterSynthesizer(&synth);
    QVERIFY(sequence->init()); sequence->setScoreView(main->currentScoreView());
    QVERIFY(sequence->canStart()); getAction("play")->setChecked(true); sequence->start(); driver->pulse();
    QTRY_VERIFY(sequence->isPlaying());
    main->showPerformanceEditor(true); auto editor = main->performanceEditor(); QVERIFY(editor);
    editor->findChild<QTabBar*>("workflowExpressionTabs")->setCurrentIndex(0);
    auto parameter = editor->findChild<QComboBox*>("performanceParameter"); parameter->setCurrentIndex(0); parameter->setCurrentIndex(2);
    auto canvas = editor->findChild<QWidget*>("performanceParameterCanvas"); QVERIFY(canvas);
    const bool ready = QTest::qWaitFor([&] { return !canvas->property("pedalPlaybackRanges").toList().isEmpty(); }, 2000);
    if (!ready) {
        quint64 checkVersion = 0; QVector<PreparedPedalPoint> checkPoints;
        qInfo() << "graph playback diagnostic" << sequence->preparedPedalSnapshot(score, checkVersion, &checkPoints) << checkVersion << checkPoints.size()
            << "playing" << sequence->isPlaying() << "same score" << (sequence->score() == score) << "visible" << canvas->isVisible() << canvas->size() << "parameter" << parameter->currentIndex()
            << "editor score" << editor->property("scoreSession") << "source settings" << PedalPlayback::partData(score->parts().front())
            << "busy" << canvas->property("pedalPlaybackBusy") << "progress" << canvas->property("pedalPlaybackProgress");
        for (auto label : editor->findChildren<QLabel*>()) if (label->isVisible()) qInfo() << "visible label" << label->text();
        main->grab().save("E:/programming/funcodes/muse3_plugins/P2/out/pedal-106-playing-diagnostic.png");
    }
    QVERIFY(ready);
    auto rows = pedalGraph106(editor); QVERIFY(rows[0].value("ready").toBool()); QVERIFY(rows[0].value("down").toInt() > 0);
    quint64 version = 0; QVector<PreparedPedalPoint> points;
    QVERIFY(sequence->preparedPedalSnapshot(score, version, &points)); QVERIFY(version > 0); QVERIFY(points.size() >= 8);
    const auto output = qEnvironmentVariable("P2_PEDAL_PLAYING_IMAGE");
    if (!output.isEmpty()) QVERIFY(editor->parentWidget()->grab().save(output));
    quint64 linkedVersion = 0; QVector<PreparedPedalPoint> linkedPoints;
    QVERIFY(sequence->preparedPedalSnapshot(linked, linkedVersion, &linkedPoints)); QCOMPARE(linkedPoints.size(), points.size());
    ScoreView linkedView; linkedView.setScore(linked); linkedView.resize(800, 500); linkedView.show();
    editor->setView(&linkedView); QTest::qWait(100);
    QTRY_VERIFY(!canvas->property("pedalPlaybackRanges").toList().isEmpty());
    QVERIFY(pedalGraph106(editor)[0].value("ready").toBool());
    editor->setView(main->currentScoreView()); linkedView.hide(); QTest::qWait(100);
    const auto attacks = recording->onPitches.size(); QTest::qWait(120); QCOMPARE(recording->onPitches.size(), attacks);
    sequence->stopWait(); sequence->waitForStoppedRendering();
    QTRY_VERIFY(canvas->property("pedalPlaybackReady").toBool());
    rows = pedalGraph106(editor); QCOMPARE(rows.size(), 4); for (const auto& row : rows) QVERIFY(row.value("ready").toBool());
    const int automaticDown = rows[0].value("down").toInt();
    auto written = renderPlaybackPreview(score, score, false); auto writtenCopy = written;
    preparePlaybackPreview(score, "pedal-written-106", std::move(written), std::move(writtenCopy));
    const QVariantMap range{{"start",0},{"end",score->lastMeasure()->endTick().ticks()},{"occurrence",1}};
    QVERIFY(startPlaybackPreview(score, false, range).value("ok").toBool()); driver->pulse();
    QTRY_VERIFY(sequence->isPlaying());
    QTRY_VERIFY(pedalGraph106(editor).contains(0) && pedalGraph106(editor)[0].value("down").toInt() < automaticDown);
    QCOMPARE(pedalGraph106(editor)[0].value("down").toInt(), 3 - MScore::pedalEventsMinTicks);
    stopPlaybackPreview(true); sequence->stopWait(); sequence->waitForStoppedRendering();
    QTRY_VERIFY(canvas->property("pedalPlaybackReady").toBool());
    main->showPerformanceEditor(false); QTest::qWait(60); QVERIFY(!canvas->property("pedalPlaybackBusy").toBool());
}

void TestPerformanceEditor::pedalGraphBatchAndReuse106() {
    expressionHost080(_directory.path()); auto main = Ms::mscore; QVERIFY(main);
    QString xml = "<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part><Staff id=\"1\">";
    for (int bar = 0; bar < 160; ++bar) {
        xml += "<Measure><voice>";
        if (!bar) xml += "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
        for (int pitch : {48,52,55,60}) xml += QString("<Chord><durationType>quarter</durationType><Note><pitch>%1</pitch><tpc>%2</tpc></Note></Chord>").arg(pitch).arg(pitch % 12 == 4 ? 18 : pitch % 12 == 7 ? 15 : 14);
        xml += "</voice></Measure>";
    }
    xml += "</Staff></Score></museScore>";
    const auto path = _directory.filePath("pedal-large-106.mscx"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(xml.toUtf8()); file.close();
    auto score = main->readScore(path); QVERIFY(score);
    score->startCmd();
    for (int bar = 0; bar < 160; ++bar) {
        auto pedal = new Pedal(score); pedal->setTrack(0); pedal->setTrack2(0); pedal->setTick(Fraction::fromTicks(bar*1920)); pedal->setTicks(Fraction::fromTicks(1920)); score->undoAddElement(pedal);
    }
    score->endCmd();
    PedalPlayback::Settings settings; settings.piano = true;
    QVERIFY(ParameterEdit::pedalPlayback(score, score->parts().front(), nullptr, settings, false));
    PedalWindow105 window; QVERIFY(window.open(main, score));
    QTRY_VERIFY(window.canvas->property("pedalPlaybackReady").toBool()); QCOMPARE(pedalGraph106(window.editor).size(), 160);
    const auto progress = window.canvas->property("pedalPlaybackProgress");
    const auto maxBatch = window.canvas->property("pedalPlaybackMaxBatchNs").toLongLong();
    qInfo() << "160 bar pedal max batch ms" << maxBatch / 1000000.;
    QVERIFY(maxBatch > 0); QVERIFY(maxBatch < 30000000);
    window.dock->resize(1000, 620); window.editor->fit(false); QTest::qWait(120);
    QCOMPARE(window.canvas->property("pedalPlaybackProgress"), progress);
    QCOMPARE(window.canvas->property("pedalPlaybackMaxBatchNs").toLongLong(), maxBatch);
    QVERIFY(!window.canvas->property("pedalPlaybackBusy").toBool());
}

void TestPerformanceEditor::pedalManualControllersAcrossChunks106() {
    std::unique_ptr<MasterScore> score(readScore("mscore/performanceeditor/pedal_106_piano.mscx")); QVERIFY(score);
    for (int bar = 0; bar < 4; ++bar) QVERIFY(ParameterEdit::pedal(score.get(), 4, bar*1920, (bar+1)*1920, nullptr));
    PedalPlayback::Settings settings; settings.piano = true;
    QVERIFY(ParameterEdit::pedalPlayback(score.get(), score->parts().front(), nullptr, settings, false));
    NamedEventList action; action.name = "held-half-pedal"; action.events.push_back(MidiCoreEvent(ME_CONTROLLER, 0, CTRL_SUSTAIN, 63));
    score->parts().front()->instrument()->setMidiActions({action});
    auto segment = score->tick2segment(Fraction::fromTicks(3840), false, SegmentType::ChordRest); QVERIFY(segment);
    auto text = new StaffText(score.get()); text->setPlainText("pedal test"); text->setTrack(0); text->setParent(segment);
    ChannelActions actions; actions.channel = 0; actions.midiActionNames.append(action.name); text->channelActions()->append(actions);
    score->startCmd(); score->undoAddElement(text); score->endCmd(); score->setExpandRepeats(false);
    SynthesizerState synth; MidiRenderer::Context context(synth); context.metronome = false;
    auto signature = [](const EventMap& events) {
        QStringList result;
        for (const auto& row : events) if (row.second.type() == ME_CONTROLLER && row.second.controller() == CTRL_SUSTAIN)
            result.append(QString("%1:%2").arg(row.first).arg(row.second.value()));
        return result;
    };
    EventMap whole; MidiRenderer(score.get()).renderScore(&whole, context);
    EventMap chunks; MidiRenderer renderer(score.get()); renderer.setMinChunkSize(1); renderer.renderScore(&chunks, context);
    QVERIFY(signature(whole).contains("3840:63")); QCOMPARE(signature(chunks), signature(whole));
    score->startCmd(); score->insertMeasure(ElementType::VBOX, score->first()); score->endCmd();
    auto linked = new Score(score.get()); auto excerpt = new Excerpt(score.get());
    excerpt->setPartScore(linked); excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano");
    score->startCmd(); Excerpt::createExcerpt(excerpt); score->excerpts().append(excerpt); score->endCmd();
    QVERIFY(PedalPlayback::retainedParts(linked).contains(linked->parts().front()));
}
