// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mscore/scorevideo/scorevideo.h"
#include "mscore/scorevideo/scorevideoexport.h"
#include "mscore/scorevideo/scorevideopanel.h"
#include <QProcess>
#include <QMenu>
static MasterScore* videoScore164(const QString& path)
{
    auto score = performanceScore123(path, 4);
    if (!score)
        return nullptr;
    PluginAPI::Score wrapped(score);
    score->startCmd();
    wrapped.appendPart("piano");
    auto line = new LayoutBreak(score);
    line->setLayoutBreakType(LayoutBreak::LINE);
    line->setParent(score->firstMeasure()->nextMeasure());
    score->undoAddElement(line);
    score->endCmd();
    score->doLayout();
    Ms::mscore->setCurrentScoreView(Ms::mscore->appendScore(score));
    return score;
}
void TestPluginHost::scoreVideoPictures164_data()
{
    QTest::addColumn<int>("count");
    for (int count : {2, 3, 4})
        QTest::newRow(qPrintable(QString::number(count) + "-staves")) << count;
}
void TestPluginHost::scoreVideoPictures164()
{
    QFETCH(int, count);
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    auto score = videoScore164(_settings.filePath(QString("video-%1-164.mscx").arg(count)));
    QVERIFY(score);
    QCOMPARE(score->nstaves(), 4);
    const auto undo = score->undoStack()->getCurIdx();
    const auto state = score->state();
    const bool dirty = score->dirty(), repeats = MScore::playRepeats;
    const qreal pixelRatio = MScore::pixelRatio;
    const auto selected = score->selection().noteList();
    ScoreVideoDocument document;
    document.setScore(score);
    QVariantList staves;
    for (int i = 0; i < count; ++i)
        staves.append(i);
    QSignalSpy ready(&document, &ScoreVideoDocument::prepared);
    document.prepare(
        {{"staves", staves}, {"audioKind", "none"}, {"width", 640}, {"height", 360}, {"fps", 30}});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
    QVERIFY2(document.ready(), qPrintable(document.status()));
    QVERIFY(document.current());
    QVERIFY(document.snapshot().pictures.size() >= 2);
    QCOMPARE(score->undoStack()->getCurIdx(), undo);
    QVERIFY(score->state() == state);
    QCOMPARE(score->dirty(), dirty);
    QCOMPARE(MScore::playRepeats, repeats);
    QCOMPARE(MScore::pixelRatio, pixelRatio);
    QCOMPARE(score->selection().noteList(), selected);
    VideoFrameCache cache;
    auto frame = scoreVideoFrame(document.snapshot(), 1., &cache);
    QVERIFY(!frame.isNull());
    QCOMPARE(frame.size(), QSize(640, 360));
    QVERIFY(frame.save(out.filePath(QString("video-%1-staves-164.png").arg(count))));
    const auto first = scoreVideoPictureRect(document.snapshot(), 0, frame.size()),
               second = scoreVideoPictureRect(document.snapshot(), 1, frame.size());
    const auto& a = document.snapshot().pictures[0];
    const auto& b = document.snapshot().pictures[1];
    QVERIFY(qAbs(first.y() + a.firstStaffY * first.width() / a.pixels.width() - second.y() -
                 b.firstStaffY * second.width() / b.pixels.width()) < .01);
    QVERIFY(document.changeSettings({{"transparent", true}, {"invert", true}, {"cursor", false}}));
    frame = scoreVideoFrame(document.snapshot(), 1., &cache);
    QCOMPARE(qAlpha(frame.pixel(0, 0)), 0);
    bool ink = false;
    for (int y = 0; y < cache.ink.height(); ++y) {
        const auto row = reinterpret_cast<const QRgb*>(cache.ink.constScanLine(y));
        for (int x = 0; x < cache.ink.width(); ++x) {
            const int alpha = qAlpha(row[x]);
            QVERIFY(qRed(row[x]) <= alpha && qGreen(row[x]) <= alpha && qBlue(row[x]) <= alpha);
            ink |= alpha > 0;
        }
    }
    QVERIFY(ink);
    QVERIFY(frame.save(out.filePath(QString("video-%1-transparent-164.png").arg(count))));
    qInfo() << "Video prepare" << count << document.state().value("readMilliseconds")
            << document.state().value("maximumGuiBatchMs");
    const auto preserved = document.snapshot().pictures[0].file;
    document.prepare();
    document.cancel();
    QTest::qWait(50);
    QVERIFY(document.ready());
    QCOMPARE(document.snapshot().pictures[0].file, preserved);
}
void TestPluginHost::scoreVideoTimeAndStorage164()
{
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    auto score = videoScore164(_settings.filePath("video-time-164.mscx"));
    QVERIFY(score);
    ScoreVideoDocument document;
    document.setScore(score);
    document.prepare({{"audioKind", "none"}});
    QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
    const auto base = document.snapshot();
    QVERIFY(base.cues.size() >= 2);
    const auto before = score->undoStack()->getCurIdx();
    const auto old = base.cues[1].from;
    QVERIFY(document.moveBoundary(1, old + .25));
    QCOMPARE(score->undoStack()->getCurIdx(), before + 1);
    QVERIFY(document.current());
    QCOMPARE(document.snapshot().cues[1].from, old + .25);
    getAction("undo")->trigger();
    document.restoreStoredSettings();
    QCOMPARE(document.snapshot().cues[1].from, old);
    getAction("redo")->trigger();
    document.restoreStoredSettings();
    QCOMPARE(document.snapshot().cues[1].from, old + .25);
    const auto rectangle = scoreVideoPictureRect(document.snapshot(), 0, {1920, 1080});
    QVERIFY(document.movePicture(0, 5., -2.));
    const auto moved = scoreVideoPictureRect(document.snapshot(), 0, {1920, 1080});
    QVERIFY(qAbs(moved.x() - rectangle.x() - 96) < .01);
    QVERIFY(qAbs(moved.y() - rectangle.y() + 21.6) < .01);
    const auto saved = out.filePath("score-video-164.mscz");
    QFileInfo info(saved);
    QVERIFY(score->saveCompressedFile(info, false, false));
    std::unique_ptr<MasterScore> reopened(Ms::mscore->readScore(saved));
    QVERIFY(reopened);
    QCOMPARE(reopened->metaTag("kumo.scoreVideo"), score->metaTag("kumo.scoreVideo"));
    const auto path = spectrumAudio078(_settings.filePath("video-reference-164.wav"));
    QVERIFY(!path.isEmpty());
    const QVariantMap association{
        {"id", "video-association"},
        {"start", 0},
        {"end", 7680},
        {"from", 2000.},
        {"until", 14000.},
        {"active", true},
        {"occurrence", 1},
        {"anchors", QVariantList{QVariantMap{{"tick", 3840}, {"ms", 6500.}}}}};
    const QVariantMap track{{"id", "video-audio"},
                            {"name", QString::fromUtf8("测试原曲")},
                            {"path", path},
                            {"duration", 24000.},
                            {"associations", QVariantList{association}}};
    score->setMetaTag(
        "kumo.reference",
        QString::fromUtf8(
            QJsonDocument::fromVariant(QVariantMap{{"schema", 2}, {"tracks", QVariantList{track}}})
                .toJson(QJsonDocument::Compact)));
    document.prepare({{"audioKind", "reference"}, {"audioId", "video-audio"}});
    QTRY_VERIFY_WITH_TIMEOUT(!document.busy(), 15000);
    QVERIFY2(document.ready(), qPrintable(document.status()));
    QCOMPARE(document.snapshot().cues[0].clock.front().seconds, 2.);
    QCOMPARE(document.snapshot().cues[1].clock.front().seconds, 6.5);
    QVERIFY(!document.snapshot().audioPieces.isEmpty());
    QCOMPARE(document.snapshot().duration, 15.5);
    QVERIFY(document.changeSettings({{"audioMapping", "whole"}}));
    QCOMPARE(document.snapshot().duration, 25.5);
    QCOMPARE(document.snapshot().cues[0].clockSource, QString("estimate"));
    QVERIFY(document.moveBoundary(1, 14.));
    QCOMPARE(document.snapshot().cues[1].clock.front().seconds, 14.);
    QCOMPARE(document.snapshot().audioPieces.front().toMap().value("until").toDouble(), 24000.);
}
void TestPluginHost::scoreVideoExport164()
{
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    auto score = videoScore164(_settings.filePath("video-encode-164.mscx"));
    QVERIFY(score);
    ScoreVideoDocument document;
    document.setScore(score);
    document.prepare(
        {{"audioKind", "none"}, {"width", 320}, {"height", 180}, {"fps", 10}, {"tail", 0.}});
    QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
    QVERIFY(!scoreVideoEncoder().isEmpty());
    ScoreVideoExport job;
    QSignalSpy done(&job, &ScoreVideoExport::finished);
    auto path = out.filePath("native-score-164.mp4");
    QVERIFY(job.start(&document, path));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    auto result = done.takeFirst().front().toMap();
    QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
    QVERIFY(QFileInfo(path).size() > 1000);
    qInfo() << "MP4" << result;
    QVERIFY(document.changeSettings({{"transparent", true}, {"invert", true}}));
    path = out.filePath("native-alpha-164.mov");
    QVERIFY(job.start(&document, path));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    result = done.takeFirst().front().toMap();
    QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
    QVERIFY(QFileInfo(path).size() > 1000);
    qInfo() << "MOV" << result;
    const auto protectedPath = out.filePath("cancel-preserved-164.mov");
    QFile protectedFile(protectedPath);
    QVERIFY(protectedFile.open(QIODevice::WriteOnly));
    protectedFile.write("previous-video");
    protectedFile.close();
    QVERIFY(job.start(&document, protectedPath));
    job.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
    QVERIFY(done.takeFirst().front().toMap().value("cancelled").toBool());
    QVERIFY(protectedFile.open(QIODevice::ReadOnly));
    QCOMPARE(protectedFile.readAll(), QByteArray("previous-video"));
    QVERIFY(document.changeSettings(
        {{"transparent", false}, {"invert", false}, {"audioKind", "score"}}));
    QVERIFY(job.start(&document, out.filePath("preview.wav"), true));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    result = done.takeFirst().front().toMap();
    QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
    QFile wav(result.value("previewPath").toString());
    QVERIFY(wav.open(QIODevice::ReadOnly));
    QVERIFY(wav.size() > 44);
    QCOMPARE(wav.read(4), QByteArray("RIFF"));
    wav.seek(0);
    QFile kept(out.filePath("score-sound-preview-164.wav"));
    QVERIFY(kept.open(QIODevice::WriteOnly));
    QVERIFY(kept.write(wav.readAll()) > 44);
    kept.close();
    qInfo() << "Prepared sound" << result;
    QVERIFY(job.start(&document, out.filePath("native-sound-164.mp4")));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    result = done.takeFirst().front().toMap();
    QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
    const auto source = spectrumAudio078(_settings.filePath("video-render-reference.wav"));
    QVERIFY(!source.isEmpty());
    const QVariantMap association{
        {"id", "a"},       {"start", 0},
        {"end", 7680},     {"from", 2000.},
        {"until", 14000.}, {"active", true},
        {"occurrence", 1}, {"anchors", QVariantList{QVariantMap{{"tick", 3840}, {"ms", 6500.}}}}};
    score->setMetaTag(
        "kumo.reference",
        QString::fromUtf8(
            QJsonDocument::fromVariant(
                QVariantMap{{"schema", 2},
                            {"tracks", QVariantList{QVariantMap{
                                           {"id", "recording"},
                                           {"name", "Recording"},
                                           {"path", source},
                                           {"duration", 24000.},
                                           {"associations", QVariantList{association}}}}}})
                .toJson(QJsonDocument::Compact)));
    document.prepare({{"audioKind", "reference"}, {"audioId", "recording"}, {"tail", 1.5}});
    QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
    QVERIFY(job.start(&document, out.filePath("reference-sound-164.mp4")));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    result = done.takeFirst().front().toMap();
    QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
    QCOMPARE(result.value("duration").toDouble(), 15.5);
    qInfo() << "Reference export" << result;
    QVERIFY(document.changeSettings(
        {{"audioKind", "none"}, {"width", 1920}, {"height", 1080}, {"fps", 30}, {"tail", 0.}}));
    QVERIFY(job.start(&document, out.filePath("default-1080-164.mp4")));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 45000);
    result = done.takeFirst().front().toMap();
    QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
    QCOMPARE(result.value("frames").toInt(), 240);
    qInfo() << "Default 1080p30" << result;
    QVERIFY(document.changeSettings(
        {{"audioKind", "score"}, {"width", 320}, {"height", 180}, {"fps", 10}}));
    QVERIFY(job.start(&document, out.filePath("frozen-source-closed-164.mp4")));
    document.setScore(nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    result = done.takeFirst().front().toMap();
    QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
    QVERIFY(QFileInfo(out.filePath("frozen-source-closed-164.mp4")).size() > 1000);
}
void TestPluginHost::scoreVideoPanel164()
{
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    auto score = videoScore164(_settings.filePath("video-panel-164.mscx"));
    QVERIFY(score);
    ScoreVideoPanel panel(Ms::mscore);
    panel.setFloating(true);
    panel.resize(1040, 720);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QTRY_VERIFY_WITH_TIMEOUT(panel.document()->ready(), 15000);
    QVERIFY(panel.document()->changeSettings(
        {{"audioKind", "none"}, {"width", 1920}, {"height", 1080}}));
    auto preview = panel.findChild<QWidget*>("scoreVideoPreview");
    auto timeline = panel.findChild<QWidget*>("scoreVideoTimeline");
    auto play = panel.findChild<QPushButton*>("scoreVideoPlay");
    QVERIFY(preview);
    QVERIFY(preview->height() > 150);
    QVERIFY(play->width() < 160);
    QVERIFY(timeline->height() >= 50);
    auto rows = panel.findChild<QTreeWidget*>("scoreVideoCues");
    QVERIFY(rows && rows->topLevelItemCount() > 1);
    auto cut = qobject_cast<QDoubleSpinBox*>(rows->itemWidget(rows->topLevelItem(1), 1));
    QVERIFY(cut);
    QTest::mouseClick(cut, Qt::LeftButton);
    cut->selectAll();
    QTest::keyClicks(cut, "4.25");
    QTest::mouseClick(panel.findChild<QPushButton*>("scoreVideoPrepare"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(panel.document()->ready(), 15000);
    QCOMPARE(panel.document()->snapshot().cues[1].from, 4.25);
    panel.document()->restoreTiming();
    QVERIFY(panel.grab().save(out.filePath("video-panel-164.png")));
    preview->setFocus();
    QTest::keyClick(preview, Qt::Key_Space);
    QVERIFY(panel.isPlaying());
    QTest::qWait(130);
    QVERIFY(panel.position() > 0.05);
    QTest::keyClick(preview, Qt::Key_Space);
    QVERIFY(!panel.isPlaying());
    const double paused = panel.position();
    QTest::qWait(60);
    QCOMPARE(panel.position(), paused);
    QTest::keyClick(preview, Qt::Key_Home);
    QCOMPARE(panel.position(), 0.);
    const auto index = score->undoStack()->getCurIdx();
    auto frameSize = QSize(1920, 1080);
    frameSize.scale(preview->size() - QSize(16, 16), Qt::KeepAspectRatio);
    const QPoint frameOrigin((preview->width() - frameSize.width()) / 2,
                             (preview->height() - frameSize.height()) / 2);
    const QPoint center =
        (scoreVideoPictureRect(panel.document()->snapshot(), 0, frameSize).center() + frameOrigin)
            .toPoint();
    QTest::mousePress(preview, Qt::LeftButton, {}, center);
    QTest::mouseMove(preview, center + QPoint(30, 10));
    QTest::mouseRelease(preview, Qt::LeftButton, {}, center + QPoint(30, 10));
    QCOMPARE(score->undoStack()->getCurIdx(), index + 1);
    QVERIFY(!panel.document()->settings().value("pictureOffsets").toMap().isEmpty());
    getAction("undo")->trigger();
    panel.document()->restoreStoredSettings();
    QVERIFY(panel.document()->settings().value("pictureOffsets").toMap().isEmpty());
    const double until = panel.document()->snapshot().duration;
    QTest::mouseClick(timeline, Qt::LeftButton, {}, QPoint(timeline->width() / 3, 15));
    QVERIFY(panel.position() > until * .2 && panel.position() < until * .45);
    panel.resize(640, 520);
    QTest::qWait(80);
    QVERIFY(play->isVisible());
    QVERIFY(play->geometry().right() < panel.width());
    QVERIFY(preview->height() >= 100);
    QVERIFY(panel.grab().save(out.filePath("video-panel-small-164.png")));
    panel.close();
    QVERIFY(!panel.isPlaying());
    panel.show();
    QTest::qWait(50);
    QVERIFY(panel.document()->ready());
    QVERIFY(panel.document()->changeSettings({{"audioKind", "score"}}));
    panel.seek(0);
    QTest::mouseClick(play, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(panel.isPlaying(), 15000);
    auto project = referenceProject();
    QTRY_VERIFY_WITH_TIMEOUT(project->property("position").toDouble() > 80, 6000);
    QVERIFY(panel.position() >= 0.);
    QTest::mouseClick(play, Qt::LeftButton);
    QVERIFY(!panel.isPlaying());
    QVERIFY(!project->property("playing").toBool());
    panel.seek(1.);
    QTest::mouseClick(play, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(panel.isPlaying(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(project->property("position").toDouble() > 1100, 6000);
    panel.stopPlayback();
    QVERIFY(!project->property("playing").toBool());
    const double end = panel.document()->snapshot().duration;
    panel.seek(end - .1);
    panel.togglePlayback();
    QTRY_VERIFY_WITH_TIMEOUT(!panel.isPlaying(), 6000);
    QCOMPARE(panel.position(), end);
    panel.togglePlayback();
    QTRY_VERIFY_WITH_TIMEOUT(panel.isPlaying(), 3000);
    panel.stopPlayback();
    panel.close();
}
void TestPluginHost::scoreVideoMusic164()
{
    const bool migrationChoice = preferences.getBool(PREF_MIGRATION_DO_NOT_ASK_ME_AGAIN);
    preferences.setPreference(PREF_MIGRATION_DO_NOT_ASK_ME_AGAIN, true);
    const auto restoreMigration = qScopeGuard(
        [&] { preferences.setPreference(PREF_MIGRATION_DO_NOT_ASK_ME_AGAIN, migrationChoice); });
    for (const auto& name : {QString("/mtest/libmscore/midi/testPausesRepeats.mscx"),
                             QString("/mtest/testscript/scripts/palette_gracenotes_1.mscx"),
                             QString("/mtest/testscript/scripts/palette_tremolo_1.mscx")}) {
        auto score = Ms::mscore->readScore(QString(TESTROOT) + name);
        QVERIFY2(score, qPrintable(name));
        Ms::mscore->setCurrentScoreView(Ms::mscore->appendScore(score));
        const bool expand = MScore::playRepeats;
        const auto before = score->state();
        ScoreVideoDocument document;
        document.setScore(score);
        document.prepare({{"audioKind", "none"}, {"tail", 1.5}});
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QVERIFY(score->state() == before);
        QCOMPARE(MScore::playRepeats, expand);
        const auto snapshot = document.snapshot();
        int notes = 0;
        double previous = -1;
        for (const auto& cue : snapshot.cues) {
            QVERIFY(cue.from >= previous);
            previous = cue.until;
            QVERIFY(cue.from < cue.until);
            QVERIFY(!cue.clock.isEmpty());
            double last = -1;
            for (const auto& point : cue.clock) {
                QVERIFY(point.seconds >= last);
                last = point.seconds;
            }
            for (const auto& ink : cue.notes) {
                QVERIFY(ink.from >= -.000001);
                QVERIFY(ink.until > ink.from);
                ++notes;
            }
        }
        QVERIFY(notes > 0);
        qInfo() << "Music timing" << name << snapshot.duration << snapshot.preRoll
                << snapshot.cues.size() << notes;
        if (name.contains("Repeats")) {
            QVERIFY(snapshot.cues.size() > snapshot.pictures.size());
            QVERIFY(document.changeSettings({{"expandRepeats", false}}));
            document.prepare();
            QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
            QVERIFY(document.snapshot().duration < snapshot.duration ||
                    document.snapshot().cues.size() <= document.snapshot().pictures.size());
        }
    }
}
void TestPluginHost::scoreVideoWorkspace164()
{
    auto main = Ms::mscore;
    auto score = videoScore164(_settings.filePath("video-workspace-164.mscx"));
    QVERIFY(score);
    main->resize(1280, 900);
    main->show();
    // Use the actions created by normal startup. Creating a private menu here
    // hides omissions in the actual menu and toolbar initialization.
    auto action = main->findChild<QAction*>("kumo-score-video");
    QVERIFY(action);
    QVERIFY(!action->icon().isNull());
    auto toolbar = main->findChild<QToolBar*>("kumoPersonalTools");
    QVERIFY(toolbar);
    auto toolbarHasAction = [toolbar, action] {
        for (auto button : toolbar->findChildren<QToolButton*>())
            if (button->defaultAction() == action)
                return true;
        return false;
    };
    QVERIFY(toolbarHasAction());
    auto tools = Workspace::findMenuFromString("menu-tools");
    QVERIFY(tools && main->menuBar()->actions().contains(tools->menuAction()));
    QCOMPARE(tools->actions().count(action), 1);

    QDir().mkpath(dataPath + "/workspaces/global");
    Workspace::writeGlobalMenuBar(main->menuBar());
    QFile saved(dataPath + "/workspaces/global/menubar.xml");
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QByteArray xml = saved.readAll();
    saved.close();
    QVERIFY(xml.contains("<action>kumo-score-video</action>"));
    xml.replace("<action>kumo-score-video</action>", "");
    QVERIFY(saved.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(saved.write(xml), qint64(xml.size()));
    saved.close();
    XmlReader reader(xml);
    QVERIFY(reader.readNextStartElement());
    Workspace workspace;
    workspace.read(reader);
    main->updateMenus();
    main->updateMenus();
    tools = Workspace::findMenuFromString("menu-tools");
    QVERIFY(tools && main->menuBar()->actions().contains(tools->menuAction()));
    QCOMPARE(tools->actions().count(action), 1);
    QVERIFY(tools->actions().indexOf(action) < tools->actions().indexOf(getAction("transpose")));
    QCOMPARE(main->findChild<QAction*>("kumo-score-video"), action);
    QVERIFY(toolbarHasAction());
    auto arrange = main->findChild<QAction*>("kumo-workspace-arrange");
    auto post = main->findChild<QAction*>("kumo-workspace-post");
    QVERIFY(arrange && post);
    arrange->trigger();
    if (action->isChecked())
        action->trigger();
    action->trigger();
    QTRY_COMPARE(main->property("kumoWorkspaceView").toString(), QString("post"));
    auto panel = main->findChild<ScoreVideoPanel*>();
    QVERIFY(panel);
    QTRY_VERIFY(panel->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(panel->document()->ready(), 15000);
    main->resizeDocks({panel}, {560}, Qt::Vertical);
    QTest::qWait(120);
    const int height = panel->height();
    arrange->trigger();
    QTRY_VERIFY(panel->isHidden());
    post->trigger();
    QTRY_VERIFY(panel->isVisible());
    QTest::qWait(120);
    QVERIFY(qAbs(panel->height() - height) < 45);
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    QVERIFY(main->grab().save(out.filePath("video-workspace-164.png")));
    panel->hide();
    arrange->trigger();
    QVERIFY(!QWidget::mouseGrabber());
}
void TestPluginHost::scoreVideoLong164()
{
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    const auto audio = _settings.filePath("video-ten-minutes-164.wav");
    QFile file(audio);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream wave(&file);
    wave.setByteOrder(QDataStream::LittleEndian);
    const int frames = 600 * 8000;
    wave.writeRawData("RIFF", 4);
    wave << quint32(frames * 2 + 36);
    wave.writeRawData("WAVEfmt ", 8);
    wave << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2)
         << quint16(16);
    wave.writeRawData("data", 4);
    wave << quint32(frames * 2);
    for (int i = 0; i < frames; ++i)
        wave << qint16(4000 * std::sin(i * 2 * 3.141592653589793 * 330 / 8000));
    file.close();
    auto score = videoScore164(_settings.filePath("video-long-164.mscx"));
    QVERIFY(score);
    score->setMetaTag(
        "kumo.reference",
        QString::fromUtf8(
            QJsonDocument::fromVariant(
                QVariantMap{{"tracks", QVariantList{QVariantMap{{"id", "long"},
                                                                {"name", "Ten minutes"},
                                                                {"path", audio},
                                                                {"duration", 600000.}}}}})
                .toJson(QJsonDocument::Compact)));
    ScoreVideoDocument document;
    document.setScore(score);
    document.prepare({{"audioKind", "reference"},
                      {"audioId", "long"},
                      {"audioMapping", "whole"},
                      {"tail", 0.},
                      {"width", 320},
                      {"height", 180},
                      {"fps", 1}});
    QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
    QCOMPARE(document.snapshot().duration, 600.);
    ScoreVideoExport job;
    QSignalSpy done(&job, &ScoreVideoExport::finished);
    QVERIFY(job.start(&document, out.filePath("ten-minutes-164.mp4")));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 120000);
    const auto result = done.takeFirst().front().toMap();
    QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
    QCOMPARE(result.value("duration").toDouble(), 600.);
    QCOMPARE(result.value("frames").toInt(), 600);
    qInfo() << "Ten minute video" << result;
}
