// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "libmscore/pedalplayback.h"
#include "libmscore/rendermidi.h"
#include "libmscore/pedalplaybackrender.h"
namespace {
QVector<QPair<int, int>> sustain106(Score* score) {
    EventMap events; score->renderMidi(&events, true, false, SynthesizerState{});
    QVector<QPair<int, int>> result;
    for (const auto& row : events) if (row.second.type() == ME_CONTROLLER && row.second.controller() == CTRL_SUSTAIN)
        result.append({row.first, row.second.value()});
    return result;
}
}
void TestPerformanceEditor::pedalSchedule106() {
    using namespace PedalPlayback;
    Settings piano; piano.piano = true;
    auto seconds = [](int t) { return t / 1000.; };
    auto tickAt = [](double t) { return qRound(t * 1000); };
    QVector<Span> input{{0, 1000, 1, 1000, piano}, {1000, 2000, 1001, 2000, piano}};
    auto result = schedule(input, {{0, 0}, {1000, 1020}}, seconds, tickAt);
    QCOMPARE(result[0].down, 60); QCOMPARE(result[0].up, 1040); QCOMPARE(result[1].down, 1080);
    QCOMPARE(result[1].up, 2000);
    input[0].until = input[0].up = 800;
    result = schedule(input, {{0, 0}, {1000, 1000}}, seconds, tickAt);
    QCOMPARE(result[0].up, 800); QCOMPARE(result[1].down, 1060);
    input[1].settings.delayFirst = false;
    result = schedule(input, {{0, 0}, {1000, 1000}}, seconds, tickAt);
    QCOMPARE(result[1].down, 1001);
    result = schedule(input, {{0, 0}}, seconds, tickAt);
    QCOMPARE(result[1].down, 1001);
    // A very short group still has a valid down/up and a nonempty lift.
    input = {{0, 4, 0, 4, piano}, {4, 8, 4, 8, piano}};
    result = schedule(input, {{0, 0}, {4, 4}}, seconds, tickAt);
    QVERIFY(result[0].down < result[0].up); QVERIFY(result[0].up < result[1].down); QVERIFY(result[1].down < result[1].up);
    const auto saved = encode(piano); QCOMPARE(decode(saved).releaseMs, 40); QVERIFY(decode(saved).piano);
}
void TestPerformanceEditor::pedalPlaybackPersistence106() {
    std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
    QVERIFY(ParameterEdit::pedal(score.get(), 4, 0, 480, nullptr));
    QVERIFY(ParameterEdit::pedal(score.get(), 4, 480, 1920, nullptr));
    auto part = score->parts().front(); auto second = pedalAt105(score.get(), 480); QVERIFY(second);
    const auto old = sustain106(score.get()); const auto notes = pedalNotes105(score.get());
    PedalPlayback::Settings settings; settings.piano = true;
    const int undo = score->undoStack()->getCurIdx();
    QVERIFY(ParameterEdit::pedalPlayback(score.get(), part, nullptr, settings, false));
    QCOMPARE(score->undoStack()->getCurIdx(), undo + 1);
    const auto actual = sustain106(score.get()); QCOMPARE(actual.size(), 4);
    QVERIFY(actual[0].first > old[0].first);
    QVERIFY(actual[2].first > actual[1].first + 10);
    const double gap = score->utick2utime(actual[2].first) - score->utick2utime(actual[1].first);
    QVERIFY(qAbs(gap - .040) < .003);
    QCOMPARE(pedalNotes105(score.get()), notes);
    score->undoRedo(true, &ed); QCOMPARE(sustain106(score.get()), old);
    score->undoRedo(false, &ed); QCOMPARE(sustain106(score.get()), actual);
    const auto file = _directory.path() + "/pedal-playback-106.mscx"; QVERIFY(saveScore(score.get(), file));
    std::unique_ptr<MasterScore> loaded(readCreatedScore(file)); QVERIFY(loaded);
    QCOMPARE(sustain106(loaded.get()), actual); QCOMPARE(pedalNotes105(loaded.get()), notes);
    settings.piano = false;
    QVERIFY(ParameterEdit::pedalPlayback(score.get(), part, second, settings, false));
    QVERIFY(!PedalPlayback::effective(second).piano);
    const auto explicitSf2 = sustain106(score.get()); QCOMPARE(explicitSf2[2].first, old[2].first);
    QVERIFY(ParameterEdit::pedalPlayback(score.get(), part, second, settings, true));
    QVERIFY(PedalPlayback::effective(second).piano); QCOMPARE(sustain106(score.get()), actual);
}
void TestPerformanceEditor::pedalPlaybackWindow106() {
    expressionHost080(_directory.path()); auto main = Ms::mscore; QVERIFY(main);
    auto score = fixture(); QVERIFY(score);
    QVERIFY(ParameterEdit::pedal(score, 4, 0, 480, nullptr)); QVERIFY(ParameterEdit::pedal(score, 4, 480, 1920, nullptr));
    PedalWindow105 window; QVERIFY(window.open(main, score));
    auto button = window.editor->findChild<QToolButton*>("performancePedalPlayback"); QVERIFY(button); QVERIFY(button->isVisible());
    QTest::mouseClick(button, Qt::LeftButton); QTest::qWait(100);
    auto dialog = window.editor->findChild<QDialog*>("performancePedalPlaybackDialog"); QVERIFY(dialog); QVERIFY(dialog->isVisible());
    auto mode = dialog->findChild<QComboBox*>("pedalPlaybackMode"); QVERIFY(mode);
    mode->setCurrentIndex(mode->findData(1));
    auto buttons = dialog->findChild<QDialogButtonBox*>(); QVERIFY(buttons);
    QTest::mouseClick(buttons->button(QDialogButtonBox::Apply), Qt::LeftButton); QTest::qWait(120);
    QVERIFY(PedalPlayback::decode(PedalPlayback::partData(score->parts().front())).piano);
    const auto screenshot = QString::fromLocal8Bit(qgetenv("P2_PEDAL_106_SCREENSHOT"));
    if (!screenshot.isEmpty()) QVERIFY(dialog->grab().save(screenshot));
    QVERIFY(dialog->height() < 430); QVERIFY(dialog->width() < 650);
    QTest::mouseClick(buttons->button(QDialogButtonBox::Close), Qt::LeftButton); QTest::qWait(50);
    QVERIFY(!window.editor->findChild<QDialog*>("performancePedalPlaybackDialog"));
}

void TestPerformanceEditor::pedalPlaybackChunked106() {
    std::unique_ptr<MasterScore> score(readScore("mscore/performanceeditor/pedal_106_piano.mscx")); QVERIFY(score);
    QCOMPARE(score->nmeasures(), 4);
    for (int bar = 0; bar < 4; ++bar) QVERIFY(ParameterEdit::pedal(score.get(), 4, bar * 1920, (bar + 1) * 1920, nullptr));
    const auto notes = pedalNotes105(score.get());
    auto chord = toChord(score->findCR(Fraction::fromTicks(1920), 4)); QVERIFY(chord);
    chord->setPlayEventType(PlayEventType::User);
    for (auto note : chord->notes()) { NoteEventList list; list.clear(); list.append(NoteEvent(0, 60, 900)); note->setPlayEvents(list); }
    QVERIFY(ParameterEdit::tempos(score.get(), {{1920, 90}, {3840, 140}}, -1));
    PedalPlayback::Settings settings; settings.piano = true;
    QVERIFY(ParameterEdit::pedalPlayback(score.get(), score->parts().front(), nullptr, settings, false));
    score->setExpandRepeats(true);
    SynthesizerState synth; MidiRenderer::Context context(synth); context.metronome = false; context.renderHarmony = true;
    EventMap full; MidiRenderer fullRenderer(score.get()); fullRenderer.renderScore(&full, context);
    EventMap chunks; MidiRenderer chunkRenderer(score.get()); chunkRenderer.setMinChunkSize(1); chunkRenderer.renderScore(&chunks, context);
    auto signature = [](const EventMap& events) {
        QStringList result;
        for (const auto& row : events) if (row.second.type() == ME_CONTROLLER && row.second.controller() == CTRL_SUSTAIN)
            result.append(QString("%1:%2:%3").arg(row.first).arg(row.second.channel()).arg(row.second.value()));
        return result;
    };
    QCOMPARE(signature(chunks), signature(full)); QCOMPARE(signature(full).size(), 16);
    // Continuous/manual sustain data keeps the written path, including half-pedal values.
    EventMap manual; NPlayEvent cc(ME_CONTROLLER, 0, CTRL_SUSTAIN, 60); cc.setOriginatingStaff(0); manual.insert({20, cc});
    auto handled = PedalPlayback::render(score.get(), &manual, nullptr, 0, 7680, 0);
    QVERIFY(handled.isEmpty()); QCOMPARE(manual.size(), size_t(1)); QCOMPARE(manual.begin()->second.value(), 60);
}

void TestPerformanceEditor::pedalPlaybackLinked106() {
    std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
    score->startCmd(); score->insertMeasure(ElementType::VBOX, score->first()); score->endCmd();
    auto linked = new Score(score.get()); auto excerpt = new Excerpt(score.get());
    excerpt->setPartScore(linked); excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano");
    score->startCmd(); Excerpt::createExcerpt(excerpt); score->excerpts().append(excerpt); score->endCmd(); linked->doLayout();
    QVERIFY(ParameterEdit::pedal(score.get(), 4, 0, 480, nullptr)); QVERIFY(ParameterEdit::pedal(score.get(), 4, 480, 1920, nullptr));
    PedalPlayback::Settings settings; settings.piano = true; settings.releaseMs = 45;
    QVERIFY(ParameterEdit::pedalPlayback(linked, linked->parts().front(), nullptr, settings, false));
    QCOMPARE(sustain106(score.get()), sustain106(linked));
    auto pedal = pedalAt105(linked, 480); QVERIFY(pedal); QVERIFY(PedalPlayback::effective(pedal).piano);
    settings.downMs = 80;
    QVERIFY(ParameterEdit::pedalPlayback(linked, linked->parts().front(), pedal, settings, false));
    QCOMPARE(sustain106(score.get()), sustain106(linked));
    QCOMPARE(PedalPlayback::effective(pedalAt105(score.get(), 480)).downMs, 80);
    score->undoRedo(true, &ed); QCOMPARE(PedalPlayback::effective(pedal).downMs, 60);
    score->undoRedo(false, &ed); QCOMPARE(PedalPlayback::effective(pedal).downMs, 80);
    const auto file = _directory.path() + "/pedal-linked-106.mscz"; QFileInfo compressed(file); QVERIFY(score->saveCompressedFile(compressed, false, false));
    std::unique_ptr<MasterScore> loaded(readCreatedScore(file)); QVERIFY(loaded); QCOMPARE(loaded->excerpts().size(), 1);
    auto restored = loaded->excerpts().front()->partScore();
    QCOMPARE(sustain106(loaded.get()), sustain106(restored));
}

void TestPerformanceEditor::pedalExportChoice106() {
    std::unique_ptr<MasterScore> score(fixture()); QVERIFY(score);
    QVERIFY(ParameterEdit::pedal(score.get(), 4, 0, 480, nullptr)); QVERIFY(ParameterEdit::pedal(score.get(), 4, 480, 1920, nullptr));
    auto bytes = [&](const MidiExportOptions& options) {
        QBuffer buffer; buffer.open(QIODevice::WriteOnly); ExportMidi exporter(score.get());
        if (!exporter.write(&buffer, false, false, SynthesizerState{}, options)) return QByteArray();
        return buffer.data();
    };
    const auto original = bytes({}); QVERIFY(!original.isEmpty());
    PedalPlayback::Settings settings; settings.piano = true;
    QVERIFY(ParameterEdit::pedalPlayback(score.get(), score->parts().front(), nullptr, settings, false));
    MidiExportOptions options; options.writtenPedal = true;
    QCOMPARE(bytes(options), original); QVERIFY(bytes({}) != original);
    auto controllerSignature = [](const EventMap& events) {
        QStringList result;
        for (const auto& row : events) if (row.second.type() == ME_CONTROLLER && row.second.controller() == CTRL_SUSTAIN)
            result.append(QString("%1:%2").arg(row.first).arg(row.second.value()));
        return result;
    };
    for (bool written : {false,true}) {
        options.writtenPedal = written;
        ExportMidi exporter(score.get()); exporter.collectGateData = true;
        QBuffer buffer; buffer.open(QIODevice::WriteOnly);
        QVERIFY(exporter.write(&buffer, MScore::playRepeats, false, SynthesizerState{}, options));
        QCOMPARE(exporter.gateData.writtenPedal, written);
        auto material = prepareGateAudition(score.get(), exporter.gateData, exporter.gateData.notes);
        const auto expected = controllerSignature(renderPlaybackPreview(score.get(), score.get(), !written).events);
        QCOMPARE(controllerSignature(material.original.events), expected);
        QCOMPARE(controllerSignature(material.proposed.events), expected);
    }
    MidiCropPanel panel(nullptr, [&] { return QList<Score*>{score.get()}; });
    panel.show(); QTest::qWait(70);
    auto check = panel.findChild<QCheckBox*>("midiWrittenPedal"); QVERIFY(check); check->setChecked(false);
    QTest::mouseClick(check, Qt::LeftButton, Qt::NoModifier, QPoint(7, check->height() / 2));
    QVERIFY(panel.usesWrittenPedal()); QVERIFY(panel.options(score.get()).writtenPedal);
    panel.close();
}
