#include "libmscore/fingering.h"

static QVariantList regionalMusicValues(Score* score, int from, int until)
{
    QVariantList values;
    for (auto segment = score->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest)) {
        if (segment->tick().ticks() < from) continue;
        if (segment->tick().ticks() >= until) break;
        for (int track = 0; track < score->ntracks(); ++track) {
            auto element = segment->element(track);
            if (!element || !element->isChordRest()) continue;
            auto cr = toChordRest(element);
            QVariantMap value{{"tick", segment->tick().ticks()}, {"track", track},
                              {"written", cr->ticks().toString()}, {"actual", cr->actualTicks().toString()},
                              {"rest", cr->isRest()}};
            if (cr->isChord()) {
                auto chord = toChord(cr);
                value["staffMove"] = chord->staffMove();
                QVariantList notes;
                for (auto note : chord->notes()) {
                    QStringList fingers;
                    for (auto item : note->el())
                        if (item->type() == ElementType::FINGERING)
                            fingers.append(static_cast<Fingering*>(item)->xmlText());
                    std::sort(fingers.begin(), fingers.end());
                    notes.append(QVariantMap{{"pitch", note->pitch()}, {"tpc1", note->tpc1()},
                                             {"tpc2", note->tpc2()}, {"type", int(note->veloType())},
                                             {"velocity", note->veloOffset()}, {"tieFor", bool(note->tieFor())},
                                             {"tieBack", bool(note->tieBack())}, {"fingers", fingers}});
                }
                value["notes"] = notes;
            }
            values.append(value);
        }
    }
    return values;
}

void TestP1Native::regionalRealPianoWritings()
{
    const auto path = qEnvironmentVariable("P2_VARIANT_SCORE");
    if (path.isEmpty()) QSKIP("Supply a local score copy with P2_VARIANT_SCORE");
    auto score = mscore->readScore(path);
    QVERIFY(score);
    mscore->setCurrentScoreView(mscore->appendScore(score));
    QVERIFY(score->nmeasures() >= 24);
    auto measureAt = [&](int index) {
        auto measure = score->firstMeasure();
        for (int position = 0; measure && position < index; ++position) measure = measure->nextMeasure();
        return measure;
    };
    const int first = measureAt(8)->tick().ticks(), end = measureAt(11)->endTick().ticks();
    const int second = measureAt(16)->tick().ticks(), secondEnd = measureAt(19)->endTick().ticks();
    const auto before = regionalMusicValues(score, 0, first);
    const auto between = regionalMusicValues(score, end, second);
    const auto after = regionalMusicValues(score, secondEnd, score->lastMeasure()->endTick().ticks());
    QVariantList tracks;
    for (int track = 0; track < score->ntracks(); ++track) tracks.append(track);
    auto firstA = RegionalVariants::create(score, first, end, tracks, "前段写法");
    QVERIFY2(firstA.value("ok").toBool(), qPrintable(firstA.value("error").toString()));
    auto secondA = RegionalVariants::create(score, second, secondEnd, tracks, "后段写法");
    QVERIFY2(secondA.value("ok").toBool(), qPrintable(secondA.value("error").toString()));
    const auto firstGroup = firstA.value("group").toString(), secondGroup = secondA.value("group").toString();
    auto editable = [&](int from, int until) -> Note* {
        for (auto segment = score->firstSegment(SegmentType::ChordRest); segment;
             segment = segment->next1(SegmentType::ChordRest)) {
            const int tick = segment->tick().ticks();
            if (tick < from) continue;
            if (tick >= until) break;
            for (int track = 0; track < score->ntracks(); ++track) {
                auto element = segment->element(track);
                if (element && element->isChord())
                    for (auto note : toChord(element)->notes())
                        if (!note->tieBack() && !note->tieFor()) return note;
            }
        }
        return nullptr;
    };
    auto change = [&](int from, int until, const char* action, int velocity, bool fixed, const QString& finger) {
        auto note = editable(from, until);
        if (!note) return false;
        score->select(note); mscore->cmd(getAction(action));
        score->startCmd();
        note->undoChangeProperty(Pid::VELO_TYPE, int(fixed ? Note::ValueType::USER_VAL : Note::ValueType::OFFSET_VAL));
        note->undoChangeProperty(Pid::VELO_OFFSET, velocity);
        if (!finger.isEmpty()) {
            auto mark = new Fingering(score); mark->setTrack(note->track()); mark->setParent(note);
            mark->setXmlText(finger); score->undoAddElement(mark);
        }
        score->endCmd();
        return true;
    };
    const auto originalSecond = regionalMusicValues(score, second, secondEnd);
    QVERIFY(change(first, end, "pitch-up", 19, false, "2"));
    auto expectedA = regionalMusicValues(score, first, end);
    auto firstB = RegionalVariants::duplicate(score, firstGroup, "前段B", true);
    QVERIFY(firstB.value("ok").toBool());
    QVERIFY(change(first, end, "pitch-up-octave", 92, true, "3"));
    const auto expectedB = regionalMusicValues(score, first, end);
    const auto combination = RegionalVariants::storeCombination(score, "前B后A");
    QVERIFY(combination.value("ok").toBool());
    auto secondB = RegionalVariants::duplicate(score, secondGroup, "后段B", true);
    QVERIFY(secondB.value("ok").toBool());
    QVERIFY(change(second, secondEnd, "pitch-down", -12, false, "4"));
    const auto expectedSecondB = regionalMusicValues(score, second, secondEnd);
    const auto both = RegionalVariants::storeCombination(score, "两段B");
    QVERIFY(both.value("ok").toBool());
    for (int repeat = 0; repeat < 2; ++repeat) {
        QElapsedTimer timer; timer.start();
        auto chosen = RegionalVariants::selectMany(score, {{firstGroup, firstA.value("variant")},
                                                          {secondGroup, secondA.value("variant")}});
        QVERIFY2(chosen.value("ok").toBool(), qPrintable(chosen.value("error").toString()));
        qInfo("real score combination switch %d: %.2f ms, %d staves / %d measures", repeat,
              timer.nsecsElapsed()/1e6, score->nstaves(), score->nmeasures());
        QCOMPARE(regionalMusicValues(score, first, end), expectedA);
        QCOMPARE(regionalMusicValues(score, second, secondEnd), originalSecond);
        auto back = RegionalVariants::selectCombination(score, combination.value("id").toString());
        QVERIFY2(back.value("ok").toBool(), qPrintable(back.value("error").toString()));
        QCOMPARE(regionalMusicValues(score, first, end), expectedB);
        QCOMPARE(regionalMusicValues(score, second, secondEnd), originalSecond);
        auto allB = RegionalVariants::selectCombination(score, both.value("id").toString());
        QVERIFY2(allB.value("ok").toBool(), qPrintable(allB.value("error").toString()));
        QCOMPARE(regionalMusicValues(score, first, end), expectedB);
        QCOMPARE(regionalMusicValues(score, second, secondEnd), expectedSecondB);
    }
    auto writingA = RegionalVariants::select(score, firstGroup, firstA.value("variant").toString());
    QVERIFY(writingA.value("ok").toBool());
    QVERIFY(change(first, end, "pitch-down", 27, false, "1"));
    expectedA = regionalMusicValues(score, first, end);
    QVERIFY(RegionalVariants::selectCombination(score, both.value("id").toString()).value("ok").toBool());
    QCOMPARE(regionalMusicValues(score, first, end), expectedB);
    const QDir artifacts(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", settings.path()));
    QFileInfo saved(artifacts.filePath("variants-real-091.mscz"));
    QVERIFY2(score->saveCompressedFile(saved, false, false), qPrintable(MScore::lastError));
    std::unique_ptr<MasterScore> reopened(mscore->readScore(saved.filePath()));
    QVERIFY(reopened);
    QCOMPARE(regionalMusicValues(reopened.get(), first, end), expectedB);
    QCOMPARE(regionalMusicValues(reopened.get(), second, secondEnd), expectedSecondB);
    QCOMPARE(regionalMusicValues(reopened.get(), 0, first), before);
    QCOMPARE(regionalMusicValues(reopened.get(), end, second), between);
    QCOMPARE(regionalMusicValues(reopened.get(), secondEnd, reopened->lastMeasure()->endTick().ticks()), after);
    auto back = RegionalVariants::select(reopened.get(), firstGroup, firstA.value("variant").toString());
    QVERIFY2(back.value("ok").toBool(), qPrintable(back.value("error").toString()));
    QCOMPARE(regionalMusicValues(reopened.get(), first, end), expectedA);
    EditData edit;
    reopened->undoRedo(true, &edit);
    QCOMPARE(regionalMusicValues(reopened.get(), first, end), expectedB);
    reopened->undoRedo(false, &edit);
    QCOMPARE(regionalMusicValues(reopened.get(), first, end), expectedA);
    QByteArray ordinaryBytes;
    QBuffer ordinaryArchive(&ordinaryBytes); QVERIFY(ordinaryArchive.open(QIODevice::WriteOnly));
    QVERIFY2(RegionalVariants::exportOrdinary(score, &ordinaryArchive, "variants-main-ordinary-091.mscx", true),
             qPrintable(MScore::lastError));
    QSaveFile ordinary(artifacts.filePath("variants-main-ordinary-091.mscz"));
    QVERIFY(ordinary.open(QIODevice::WriteOnly));
    QCOMPARE(ordinary.write(ordinaryBytes), qint64(ordinaryBytes.size()));
    QVERIFY(ordinary.commit());
    std::unique_ptr<MasterScore> mainVersion(mscore->readScore(ordinary.fileName()));
    QVERIFY(mainVersion);
    QCOMPARE(regionalMusicValues(mainVersion.get(), first, end), expectedA);
    QCOMPARE(regionalMusicValues(mainVersion.get(), second, secondEnd), originalSecond);
    QVERIFY(!RegionalVariants::active(mainVersion.get()));
    auto selectedNote = editable(first, end); QVERIFY(selectedNote);
    mscore->resize(1280, 900); mscore->show();
    mscore->currentScoreView()->adjustCanvasPosition(selectedNote, false);
    showRegionalVariantPanel(firstGroup);
    auto dock = mscore->findChild<QDockWidget*>("kumoPanel/regionalVariantsDock");
    QVERIFY(dock); dock->setFloating(true); dock->resize(460, 650); dock->show();
    QTest::qWait(100);
    auto scope = dock->findChild<QLabel*>("regionalCurrent"); QVERIFY(scope);
    QVERIFY(scope->text().contains(QString::fromUtf8("全部声部")));
    QVERIFY(scope->height() <= scope->fontMetrics().height() * 4);
    QVERIFY(dock->grab().save(artifacts.filePath("variants-real-panel-091.png")));
    dock->hide();
}

void TestP1Native::regionalControlsFollowVisibleSystem()
{
    const auto path = qEnvironmentVariable("P2_VARIANT_SCORE");
    if (path.isEmpty()) QSKIP("Supply a local score copy with P2_VARIANT_SCORE");
    auto score = mscore->readScore(path); QVERIFY(score);
    mscore->setCurrentScoreView(mscore->appendScore(score));
    QVariantList tracks;
    for (int track = 0; track < score->ntracks(); ++track) tracks.append(track);
    auto nearGroup = RegionalVariants::create(score, 0, score->firstMeasure()->endTick().ticks(), tracks, "开头");
    auto farGroup = RegionalVariants::create(score, score->lastMeasure()->tick().ticks(),
                                       score->lastMeasure()->endTick().ticks(), tracks, "结尾");
    QVERIFY2(nearGroup.value("ok").toBool(), qPrintable(nearGroup.value("error").toString()));
    QVERIFY2(farGroup.value("ok").toBool(), qPrintable(farGroup.value("error").toString()));
    mscore->resize(1280, 900); mscore->show();
    auto view = mscore->currentScoreView();
    view->adjustCanvasPosition(score->firstMeasure(), false);
    setRegionalControlsVisible(true); view->update();
    auto button = [&](const QVariantMap& group) {
        return view->findChild<QToolButton*>("regionalScoreSwitch/"+group.value("group").toString());
    };
    QTRY_VERIFY(button(nearGroup) && button(nearGroup)->isVisible());
    const QDir artifacts(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", settings.path()));
    QVERIFY(view->grab().save(artifacts.filePath("variants-visible-start-091.png")));
    QVERIFY(!view->toPhysical(score->lastMeasure()->system()->canvasBoundingRect()).intersects(view->rect()));
    QTRY_VERIFY2(!button(farGroup) || !button(farGroup)->isVisible(), "视口外谱行的写法按钮不应挤在屏幕边缘");
    view->adjustCanvasPosition(score->lastMeasure(), false); view->update();
    QTRY_VERIFY(button(farGroup) && button(farGroup)->isVisible());
    QTRY_VERIFY(!button(nearGroup) || !button(nearGroup)->isVisible());
    QVERIFY(view->grab().save(artifacts.filePath("variants-visible-end-091.png")));
    setRegionalControlsVisible(false);
}
