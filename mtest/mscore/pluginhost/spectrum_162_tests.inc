// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mscore/performanceeditor/spectrumpane.h"
#include "mscore/performanceeditor/spectrumdisplay.h"
#include "mscore/workflow/miditracks.h"
#include <QSplitterHandle>
namespace
{
QByteArray midiTrackFixture160();
}
void TestPluginHost::spectrumInteraction162()
{
    auto score = spectrumScore078();
    QVERIFY(score);
    const auto path = spectrumAudio078(_settings.filePath("spectrum-interaction-162.wav"));
    QVERIFY(spectrumTracks078(score, path));
    auto main = Ms::mscore;
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    auto tabs = editor->findChild<QTabBar*>("workflowExpressionTabs");
    QVERIFY(tabs);
    tabs->setCurrentIndex(2);
    auto pane = editor->findChild<SpectrumPane*>("transcriptionSpectrumPane");
    QVERIFY(pane);
    auto choice = pane->findChild<QComboBox*>("transcriptionClips");
    QTRY_COMPARE(choice->count(), 2);
    choice->setCurrentIndex(0);
    auto display = pane->findChild<QComboBox*>("transcriptionDisplay");
    display->setCurrentIndex(0);
    editor->findChild<QAction*>("editorArea/float/transcriptionSpectrumPane")->trigger();
    auto dock = main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");
    QVERIFY(dock);
    QTest::qWait(150);
    dock->resize(940, 750);
    dock->raise();
    dock->activateWindow();
    const auto restore = qScopeGuard([&] {
        editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();
        tabs->setCurrentIndex(0);
    });
    auto canvas = pane->findChild<QWidget*>("transcriptionSpectrumCanvas");
    QVERIFY(canvas);
    auto session = pane->findChild<QObject*>("transcriptionSpectrumSession");
    QVERIFY(session);
    QTRY_VERIFY_WITH_TIMEOUT(!qvariant_cast<QImage>(session->property("image")).isNull(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("backgroundBusy").toBool(), 15000);
    QCOMPARE(session->property("info").toMap().value("canonicalRows").toInt(), 1024);
    auto settings = pane->findChild<QToolButton*>("spectrumDisplaySettings");
    auto panel = pane->findChild<QScrollArea*>("spectrumOptionsScroll");
    QVERIFY(settings && panel);
    if (!panel->isVisible())
        QTest::mouseClick(settings, Qt::LeftButton);
    QTRY_VERIFY(panel->isVisible());
    auto split = pane->findChild<QSplitter*>("spectrumWorkspaceSplitter");
    QVERIFY(split);
    QTest::qWait(100);
    const auto sizes = split->sizes();
    QVERIFY(sizes.size() == 2);
    auto handle = split->handle(1);
    QVERIFY(handle);
    const auto start = handle->rect().center(), finish = start + QPoint(0, 55);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, finish, handle->mapToGlobal(finish), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &move);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, finish);
    qInfo() << "Spectrum splitter" << sizes << split->sizes() << split->geometry()
            << handle->geometry();
    dock->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS") +
                      "/spectrum-splitter-162.png");
    QVERIFY(split->sizes()[0] > sizes[0]);
    QVERIFY(panel->widget()->findChild<SpectrumDisplayControls*>()->width() <= 560);
    auto options = pane->findChild<SpectrumDisplayControls*>();
    QVERIFY(options);
    auto reference = pane->findChild<QToolButton*>("spectrumReferenceSettings");
    QVERIFY(reference);
    panel->ensureWidgetVisible(reference);
    QTest::qWait(60);
    const auto original = qvariant_cast<QImage>(session->property("image"));
    QTimer::singleShot(60, [reference] {
        auto menu = reference->menu();
        if (!menu)
            return;
        auto count = menu->findChild<QSpinBox*>("spectrumHarmonicCount");
        if (count) {
            count->setFocus();
            QTest::keyClick(count, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(count, "4");
            QTest::keyClick(count, Qt::Key_Tab);
        }
        menu->close();
    });
    QTest::mouseClick(reference, Qt::LeftButton);
    QCOMPARE(options->values().value("harmonicCount").toInt(), 4);
    QCOMPARE(qvariant_cast<QImage>(session->property("image")).cacheKey(), original.cacheKey());
    QTest::mouseClick(settings, Qt::LeftButton);
    QTRY_VERIFY(!panel->isVisible());
    QVERIFY(canvas->height() > 300);
    const auto view = pane->viewInfo();
    const QPoint point(
        PerformanceViewport::gutter + 150,
        qRound((view.value("highPitch").toDouble() - 69.) /
               (view.value("highPitch").toDouble() - view.value("lowPitch").toDouble()) *
               canvas->height()));
    QMouseEvent hover(QEvent::MouseMove, point, canvas->mapToGlobal(point), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &hover);
    QCOMPARE(canvas->property("hoverPitch").toInt(), 69);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, point);
    auto harmonic = pane->findChild<QToolButton*>("spectrumHarmonics");
    QVERIFY(harmonic);
    if (!harmonic->isChecked())
        QTest::mouseClick(harmonic, Qt::LeftButton);
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    QTest::qWait(80);
    QVERIFY(dock->grab().save(out.filePath("spectrum-reference-162.png")));
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(canvas, &leave);
    QCOMPARE(canvas->property("hoverPitch").toInt(), -1);
    QTest::mouseClick(settings, Qt::LeftButton);
    QTest::qWait(60);
    QVERIFY(dock->grab().save(out.filePath("spectrum-settings-162.png")));
    dock->close();
}
void TestPluginHost::spectrumSharedTimeline162()
{
    auto score = spectrumScore078();
    QVERIFY(score);
    const auto path = spectrumAudio078(_settings.filePath("spectrum-timeline-162.wav"));
    QVERIFY(spectrumTracks078(score, path));
    auto main = Ms::mscore;
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    auto tabs = editor->findChild<QTabBar*>("workflowExpressionTabs");
    tabs->setCurrentIndex(2);
    auto pane = main->findChild<SpectrumPane*>("transcriptionSpectrumPane");
    QVERIFY(pane);
    auto choice = pane->findChild<QComboBox*>("transcriptionClips");
    QVERIFY(choice);
    QTRY_COMPARE(choice->count(), 2);
    choice->setCurrentIndex(0);
    auto display = pane->findChild<QComboBox*>("transcriptionDisplay");
    display->setCurrentIndex(1);
    QTRY_VERIFY(pane->isVisible());
    QTest::qWait(200);
    auto notes = editor->findChild<QWidget*>("performanceNoteCanvas");
    auto ruler = editor->findChild<QWidget*>("performanceTimeRuler");
    if (!ruler)
        for (auto child : editor->findChildren<QWidget*>())
            if (child->objectName().contains("Ruler") &&
                !child->property("performanceExtraRuler").toBool())
                ruler = child;
    QVERIFY(notes && ruler);
    if (auto split = editor->findChild<QSplitter*>("performanceSplitter"))
        split->setSizes({240, 200, 180});
    auto session = pane->findChild<QObject*>("transcriptionSpectrumSession");
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(), 15000);
    QScrollBar* scroll = nullptr;
    for (auto bar : editor->findChildren<QScrollBar*>(QString(), Qt::FindDirectChildrenOnly))
        if (bar->orientation() == Qt::Horizontal)
            scroll = bar;
    QVERIFY(scroll);
    scroll->setValue(0);
    const auto before = pane->viewInfo();
    spectrumWheel078(notes, QPoint(300, notes->height() / 2), 120);
    QTest::qWait(150);
    const auto after = pane->viewInfo();
    qInfo() << "Spectrum common timeline" << before << after << notes->geometry()
            << notes->isVisible();
    QVERIFY(after.value("until").toDouble() - after.value("from").toDouble() <
            before.value("until").toDouble() - before.value("from").toDouble());
    auto project = referenceProject();
    const auto volume = project->property("volume");
    project->setProperty("volume", 0.);
    const auto cleanup = qScopeGuard([&] {
        QMetaObject::invokeMethod(project, "stop");
        project->setProperty("volume", volume);
    });
    QTest::mouseClick(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(210, 8));
    const auto position = pane->viewInfo().value("sourcePosition").toDouble();
    QVERIFY(position > 0);
    ruler->setFocus();
    QTest::keyClick(ruler, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(project->property("playing").toBool(), 10000);
    QTest::qWait(100);
    QTest::keyClick(notes, Qt::Key_Space);
    QTRY_VERIFY(!project->property("playing").toBool());
    auto parameter = editor->findChild<QWidget*>("performanceParameterCanvas");
    QVERIFY(parameter);
    parameter->setFocus();
    QTest::keyClick(parameter, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(project->property("playing").toBool(), 10000);
    QTest::keyClick(parameter, Qt::Key_Space);
    QTRY_VERIFY(!project->property("playing").toBool());
    editor->findChild<QAction*>("editorArea/float/performanceNotesArea")->trigger();
    auto dock = main->findChild<QDockWidget*>("kumoEditorArea/performanceNotesArea");
    QVERIFY(dock);
    const auto restore = qScopeGuard([&] {
        editor->findChild<QAction*>("editorArea/restore/performanceNotesArea")->trigger();
        tabs->setCurrentIndex(0);
    });
    QTest::qWait(150);
    dock->resize(900, 400);
    dock->raise();
    dock->activateWindow();
    QWidget* extra = nullptr;
    for (auto widget : dock->findChildren<QWidget*>())
        if (widget->property("performanceExtraRuler").toBool())
            extra = widget;
    QVERIFY(extra);
    QTest::mouseClick(extra, Qt::LeftButton, Qt::NoModifier, QPoint(240, 6));
    qInfo() << "Spectrum extra ruler" << tabs->currentIndex() << pane->isVisible()
            << extra->isVisible() << extra->focusPolicy() << extra->hasFocus();
    QTest::keyClick(extra, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(project->property("playing").toBool(), 10000);
    QTest::keyClick(extra, Qt::Key_Space);
    QTRY_VERIFY(!project->property("playing").toBool());
}
void TestPluginHost::spectrumMidiOverlay162()
{
    auto score = spectrumScore078();
    QVERIFY(score);
    const auto path = spectrumAudio078(_settings.filePath("spectrum-midi-162.wav"));
    QVERIFY(spectrumTracks078(score, path));
    auto main = Ms::mscore;
    main->showPerformanceEditor(true);
    auto editor = main->performanceEditor();
    editor->showMidiTracks(true);
    const auto out = qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path());
    const auto file = out + "/spectrum-midi-162.mid";
    QFile source(file);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(midiTrackFixture160());
    source.close();
    auto midi = main->findChild<QWidget*>("midiTrackPane");
    QVERIFY(midi);
    QVERIFY(QMetaObject::invokeMethod(midi, "importFile", Q_ARG(QString, file)));
    MidiTrackDocument doc;
    doc.setScore(score);
    QTRY_VERIFY_WITH_TIMEOUT((doc.reload(), !doc.tracks().isEmpty()), 5000);
    const auto id = doc.tracks().front().toMap().value("id").toString();
    QVERIFY(doc.setAlignment(id, "spectrum-source-078", 500, {}));
    auto tabs = editor->findChild<QTabBar*>("workflowExpressionTabs");
    tabs->setCurrentIndex(2);
    auto pane = main->findChild<SpectrumPane*>("transcriptionSpectrumPane");
    QVERIFY(pane);
    auto choice = pane->findChild<QComboBox*>("transcriptionClips");
    QVERIFY(choice);
    QTRY_COMPARE(choice->count(), 2);
    choice->setCurrentIndex(0);
    auto display = pane->findChild<QComboBox*>("transcriptionDisplay");
    display->setCurrentIndex(0);
    auto canvas = pane->findChild<QWidget*>("transcriptionSpectrumCanvas");
    auto session = pane->findChild<QObject*>("transcriptionSpectrumSession");
    QTRY_VERIFY_WITH_TIMEOUT(!qvariant_cast<QImage>(session->property("image")).isNull(), 15000);
    const auto raw = qvariant_cast<QImage>(session->property("image"));
    auto showMidi = pane->findChild<QCheckBox*>("spectrumShowMidi");
    QVERIFY(showMidi);
    showMidi->setChecked(true);
    showMidi->clicked(true);
    QTest::qWait(50);
    const auto withMidi = canvas->grab().toImage();
    showMidi->setChecked(false);
    showMidi->clicked(false);
    QTest::qWait(50);
    const auto withoutMidi = canvas->grab().toImage();
    QVERIFY(withMidi != withoutMidi);
    QCOMPARE(qvariant_cast<QImage>(session->property("image")).cacheKey(), raw.cacheKey());
    showMidi->setChecked(true);
    showMidi->clicked(true);
    QTest::qWait(60);
    QVERIFY(main->grab().save(out + "/spectrum-midi-overlay-162.png"));
    const auto fileName = out + "/spectrum-midi-162.mscz";
    QFileInfo info(fileName);
    QVERIFY(score->saveCompressedFile(info, false, false));
    std::unique_ptr<MasterScore> reopened(main->readScore(fileName));
    QVERIFY(reopened);
    MidiTrackDocument saved;
    saved.setScore(reopened.get());
    QCOMPARE(saved.track(id).value("offsetMs").toInt(), 500);
    tabs->setCurrentIndex(0);
    editor->showMidiTracks(false);
}
