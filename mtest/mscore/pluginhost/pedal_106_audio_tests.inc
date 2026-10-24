// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mscore/performanceeditor/parameteredit.h"
#include "libmscore/pedal.h"
#include "libmscore/pedalplayback.h"
#include <QDataStream>
#include <cstring>
#include <QElapsedTimer>
void TestPluginHost::pedalRealPianoOutput106_data() {
    QTest::addColumn<QString>("source");
    QTest::newRow("SF2") << QString("SF2");
    QTest::newRow("Pianoteq") << QString("Pianoteq");
    QTest::newRow("CFX") << QString("CFX");
}
void TestPluginHost::pedalRealPianoOutput106() {
#ifdef KUMO_VST_HOST
    if (!qEnvironmentVariableIsSet("P2_PEDAL_AUDIO")) QSKIP("Real piano rendering is requested separately");
    QFETCH(QString, source);
    auto main = Ms::mscore; QVERIFY(main);
    const auto input = _settings.filePath("pedal-audio-106-" + source + ".mscx");
    QString xml = "<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel><program value=\"0\"/><synti>Fluid</synti></Channel></Instrument></Part><Staff id=\"1\">";
    const QVector<QVector<int>> pitches{{48, 52, 55}, {49, 54, 58}, {50, 53, 57}};
    const QVector<QVector<int>> spelling{{14,18,15},{21,20,24},{16,13,17}};
    for (int bar = 0; bar < 3; ++bar) {
        xml += "<Measure><voice>";
        if (!bar) xml += "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
        xml += "<Chord><durationType>whole</durationType>";
        for (int n = 0; n < pitches[bar].size(); ++n) xml += QString("<Note><pitch>%1</pitch><tpc>%2</tpc></Note>").arg(pitches[bar][n]).arg(spelling[bar][n]);
        xml += "</Chord></voice></Measure>";
    }
    xml += "</Staff></Score></museScore>";
    QFile file(input); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(xml.toUtf8()); file.close();
    auto score = main->readScore(input); QVERIFY(score); main->setCurrentScoreView(main->appendScore(score));
    for (int bar = 0; bar < 3; ++bar) QVERIFY(ParameterEdit::pedal(score, 0, bar * 1920, (bar + 1) * 1920, nullptr));
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
    synth->setSampleRate(44100); synth->setEffect(0, 0); synth->setEffect(1, 0);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti, synth.get());
    const auto sf = qEnvironmentVariable("P0_TEST_SOUNDFONT"); QVERIFY(QFileInfo::exists(sf));
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({sf}));
    auto channel = const_cast<Channel*>(score->midiMapping().front().articulation()); QVERIFY(channel);
    if (source != "SF2") {
        QString error; QVariantList list;
        if (source == "CFX") list = VstHost::scanVst2("E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll", &error);
        else list = VstHost::scanVst3("C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3", &error);
        QVERIFY2(!list.isEmpty(), qPrintable(error));
        const auto assigned = assignVstInstrument(score, channel, list.front().toMap());
        QVERIFY2(!assigned.contains("error"), qPrintable(assigned.value("error").toString()));
        if (source == "CFX") {
            auto engine = static_cast<VstSynth*>(synth->synthesizer("KumoVST")); QVERIFY(engine);
            auto plugin = engine->rack().plugin(channel->bank()); QVERIFY(plugin);
            engine->rack().suspend();
            const bool loaded = plugin->importPreset("E:/music_fonts/CFX Concert Grand/Presets/com.Plogue.Aria/02- Compact/Player/00_Default.ariax", &error);
            engine->rack().resume(); QVERIFY2(loaded, qPrintable(error));
            QVERIFY(!applyVstConfiguration(score, engine->configuration()).contains("error"));
        }
    }
    preferences.setPreference(PREF_EXPORT_AUDIO_NORMALIZE, false);
    preferences.setPreference(PREF_EXPORT_AUDIO_SAMPLERATE, 44100);
    auto notes = [&] {
        EventMap events; score->renderMidi(&events, false, false, main->synthesizerState()); QStringList result;
        for (const auto& row : events) if (row.second.type() == ME_NOTEON)
            result.append(QString("%1:%2:%3:%4").arg(row.first).arg(row.second.channel()).arg(row.second.pitch()).arg(row.second.velo()));
        return result;
    };
    const auto originalNotes = notes();
    const auto directory = QDir(qEnvironmentVariable("P2_PEDAL_AUDIO_DIR", "E:/programming/funcodes/muse3_plugins/P2/out/pedal-106-audio"));
    QVERIFY(QDir().mkpath(directory.path()));
    QJsonArray measurements; QByteArray firstAudio;
    for (int mode = 0; mode < 2; ++mode) {
        PedalPlayback::Settings settings; settings.piano = mode == 1;
        QVERIFY(ParameterEdit::pedalPlayback(score, score->parts().front(), nullptr, settings, false));
        QBuffer audio; QElapsedTimer timer; timer.start(); QVERIFY(main->saveAudio(score, &audio));
        const auto bytes = audio.data(); QVERIFY(bytes.size() > 44100 * 8 * 6);
        double peak = 0, energy = 0;
        for (int index = 0; index + 4 <= bytes.size(); index += 4) {
            float value; std::memcpy(&value, bytes.constData() + index, 4); QVERIFY(std::isfinite(value));
            peak = qMax(peak, qAbs(double(value))); energy += double(value) * value;
        }
        QVERIFY(peak > .001); QCOMPARE(notes(), originalNotes);
        const QString name = source + (mode ? "-piano.wav" : "-original.wav");
        QFile wave(directory.filePath(name)); QVERIFY(wave.open(QIODevice::WriteOnly));
        QDataStream output(&wave); output.setByteOrder(QDataStream::LittleEndian);
        output.writeRawData("RIFF", 4); output << quint32(bytes.size() + 36); output.writeRawData("WAVEfmt ", 8);
        output << quint32(16) << quint16(3) << quint16(2) << quint32(44100) << quint32(44100 * 8) << quint16(8) << quint16(32);
        output.writeRawData("data", 4); output << quint32(bytes.size()); QCOMPARE(output.writeRawData(bytes.constData(), bytes.size()), bytes.size()); wave.close();
        measurements.append(QJsonObject{{"file", name}, {"peak", peak}, {"rms", std::sqrt(energy / (bytes.size() / 4))}, {"frames", bytes.size() / 8}, {"renderMs", timer.elapsed()}});
        if (!mode) firstAudio = bytes;
        else QVERIFY(bytes != firstAudio);
    }
    QFile report(directory.filePath(source + ".json")); QVERIFY(report.open(QIODevice::WriteOnly));
    report.write(QJsonDocument(QJsonObject{{"source", source}, {"releaseMs", 40}, {"downMs", 60}, {"noteEventsPreserved", true}, {"measurements", measurements}}).toJson());
#else
    QFAIL("VST host is required by this check");
#endif
}
