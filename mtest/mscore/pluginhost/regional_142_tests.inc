// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "libmscore/pedal.h"
#include "libmscore/dynamic.h"
#include "libmscore/hairpin.h"
#include "libmscore/stafftext.h"
namespace {
QVariantList pedalRanges142(Score* score)
{
    QVariantList result;
    for (auto pair : score->spannerMap().map()) {
        auto line = pair.second;
        if (line->isPedal())
            result.append(QVariantMap{{"start", line->tick().ticks()},
                                      {"end", line->tick2().ticks()},
                                      {"track", line->track()}});
    }
    return result;
}
}

void TestPluginHost::regionalPedalsSurvive142()
{
    const auto path = _settings.filePath("regional-pedals-142.mscx");
    QFile fixture(path); QVERIFY(fixture.open(QIODevice::WriteOnly));
    fixture.write(systemLockFixture138()); fixture.close();
    auto main = Ms::mscore; auto score = main->readScore(path); QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    auto addPedal = [&](int start, int end) {
        auto pedal = new Pedal(score);
        pedal->setTrack(4); pedal->setTrack2(4);
        pedal->setTick(Fraction::fromTicks(start)); pedal->setTick2(Fraction::fromTicks(end));
        score->startCmd(); score->undoAddElement(pedal); score->endCmd(); main->endCmd();
    };
    addPedal(0, 1920);
    const auto original = pedalRanges142(score); QCOMPARE(original.size(), 1);
    const auto a = RegionalVariants::create(score, 0, 3840, {4}, "左手写法");
    QVERIFY2(a.value("ok").toBool(), qPrintable(a.value("error").toString()));
    const auto group = a.value("group").toString();
    const auto b = RegionalVariants::duplicate(score, group, "B", true);
    QVERIFY(b.value("ok").toBool());
    auto note = [&] { return toChord(score->findCR(Fraction(), 4))->upNote(); };
    score->startCmd(); note()->undoChangeProperty(Pid::PITCH, 50);
    note()->undoChangeProperty(Pid::TPC1,16);note()->undoChangeProperty(Pid::TPC2,16);score->endCmd();
    QVERIFY(RegionalVariants::select(score, group, a.value("variant").toString()).value("ok").toBool());
    QCOMPARE(pedalRanges142(score), original);
    // A pedal added while writing A belongs to the shared performance markings.
    addPedal(1920, 3840);
    const auto both = pedalRanges142(score); QCOMPARE(both.size(), 2);
    QVERIFY(RegionalVariants::select(score, group, b.value("variant").toString()).value("ok").toBool());
    QCOMPARE(note()->pitch(), 50); QCOMPARE(pedalRanges142(score), both);
    QVERIFY(RegionalVariants::select(score, group, a.value("variant").toString()).value("ok").toBool());
    QCOMPARE(pedalRanges142(score), both);
    EditData edit;
    score->undoRedo(true, &edit); QCOMPARE(pedalRanges142(score), both);
    score->undoRedo(false, &edit); QCOMPARE(pedalRanges142(score), both);
    QVERIFY(RegionalVariants::select(score, group, b.value("variant").toString()).value("ok").toBool());
    const int beforeSave=score->undoStack()->getCurIdx();
    QFileInfo saved(_settings.filePath("regional-pedals-142.mscz"));
    QVERIFY(score->saveCompressedFile(saved, false, false));
    QCOMPARE(note()->pitch(),50); QCOMPARE(pedalRanges142(score),both);
    QCOMPARE(score->undoStack()->getCurIdx(),beforeSave);
    std::unique_ptr<MasterScore> reopened(main->readScore(saved.filePath())); QVERIFY(reopened);
    QCOMPARE(pedalRanges142(reopened.get()), both);
    QVERIFY(RegionalVariants::select(reopened.get(), group, b.value("variant").toString()).value("ok").toBool());
    QCOMPARE(pedalRanges142(reopened.get()), both);
}

void TestPluginHost::regionalSharedControls142()
{
    const auto path = _settings.filePath("regional-shared-142.mscx");
    QFile fixture(path); QVERIFY(fixture.open(QIODevice::WriteOnly));
    fixture.write(systemLockFixture138()); fixture.close();
    auto main = Ms::mscore; auto score = main->readScore(path); QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    auto part = new Score(score); auto excerpt = new Excerpt(score);
    excerpt->setPartScore(part); excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano");
    part->setExcerpt(excerpt); score->startCmd(); score->undo(new AddExcerpt(excerpt));
    Excerpt::createExcerpt(excerpt); score->endCmd();
    auto pedal = new Pedal(score); pedal->setTrack(4); pedal->setTrack2(4);
    pedal->setTick(Fraction::fromTicks(1920)); pedal->setTick2(Fraction::fromTicks(3840));
    auto crossing = new Pedal(score); crossing->setTrack(4); crossing->setTrack2(4);
    crossing->setTick(Fraction()); crossing->setTick2(Fraction::fromTicks(2400));
    auto hairpin = new Hairpin(score); hairpin->setTrack(4); hairpin->setTrack2(4);
    hairpin->setTick(Fraction::fromTicks(1920)); hairpin->setTick2(Fraction::fromTicks(3840));
    auto dynamic = new Dynamic(score); dynamic->setTrack(4);
    dynamic->setParent(score->tick2segment(Fraction::fromTicks(1920),false,SegmentType::ChordRest));
    dynamic->setDynamicType("mp");
    auto text = new StaffText(score); text->setTrack(4); text->setParent(dynamic->parent()); text->setXmlText("sempre");
    score->startCmd(); for (auto e : QList<Element*>{pedal,crossing,hairpin,dynamic,text}) score->undoAddElement(e);
    score->endCmd(); main->endCmd();
    auto midiPedals = [&](Score* target) {
        EventMap events; target->renderMidi(&events,false,false,main->synthesizerState()); QStringList result;
        for (const auto& row:events) if (row.second.type()==ME_CONTROLLER && row.second.controller()==CTRL_SUSTAIN)
            result.append(QString("%1:%2:%3").arg(row.first).arg(row.second.channel()).arg(row.second.value()));
        return result;
    };
    const auto originalMidi = midiPedals(score); QVERIFY(originalMidi.size()>=4);
    const auto pedals = pedalRanges142(score), partPedals = pedalRanges142(part); QCOMPARE(partPedals.size(),2);
    const auto a = RegionalVariants::create(score, 1920, 5760, {0,4}, "两手写法"); QVERIFY(a.value("ok").toBool());
    const auto group = a.value("group").toString();
    const auto b = RegionalVariants::duplicate(score,group,"B",true); QVERIFY(b.value("ok").toBool());
    main->resize(1280,900); main->show(); showRegionalVariantPanel(group); QTest::qWait(100);
    auto choose = [&](const QString& id) {
        auto button = main->findChild<QPushButton*>("regionalQuick/"+id);
        if (!button || !button->isVisible()) return false;
        QTest::mouseClick(button,Qt::LeftButton); return true;
    };
    QVERIFY(choose(a.value("variant").toString()));
    QCOMPARE(pedalRanges142(score),pedals); QCOMPARE(pedalRanges142(part),partPedals);
    QVERIFY(score->spannerMap().map().count(pedal->tick().ticks())>0);
    QVERIFY(dynamic->parent()->isSegment());
    const auto annotations=toSegment(dynamic->parent())->annotations();
    QVERIFY(std::find(annotations.begin(),annotations.end(),dynamic)!=annotations.end());
    QVERIFY(std::find(annotations.begin(),annotations.end(),text)!=annotations.end());
    QCOMPARE(midiPedals(score),originalMidi);
    score->startCmd(); pedal->undoChangeProperty(Pid::SPANNER_TICKS,Fraction::fromTicks(1440));
    dynamic->undoChangeProperty(Pid::VELOCITY,61); score->endCmd(); main->endCmd();
    const auto changed = pedalRanges142(score); const auto changedMidi = midiPedals(score);
    QVERIFY(choose(b.value("variant").toString())); QCOMPARE(pedalRanges142(score),changed);
    QCOMPARE(dynamic->velocity(),61); QCOMPARE(midiPedals(score),changedMidi);
    EditData edit; score->undoRedo(true,&edit); QCOMPARE(pedalRanges142(score),changed);
    score->undoRedo(false,&edit); QCOMPARE(pedalRanges142(score),changed);
    score->startCmd(); score->undoRemoveElement(pedal); score->endCmd(); main->endCmd();
    const auto deleted = pedalRanges142(score); QCOMPARE(deleted.size(),1);
    QVERIFY(choose(a.value("variant").toString())); QCOMPARE(pedalRanges142(score),deleted);
    QVERIFY(choose(b.value("variant").toString())); QCOMPARE(pedalRanges142(score),deleted);
    QFileInfo saved(_settings.filePath("regional-shared-142.mscz")); QVERIFY(score->saveCompressedFile(saved,false,false));
    std::unique_ptr<MasterScore> read(main->readScore(saved.filePath())); QVERIFY(read);
    QCOMPARE(pedalRanges142(read.get()),deleted);
    QVERIFY(RegionalVariants::select(read.get(),group,b.value("variant").toString()).value("ok").toBool());
    QCOMPARE(pedalRanges142(read.get()),deleted);
    // Ordinary paste still replaces the destination markings as before.
    QString problem; auto fragment=RegionalVariants::capture(read.get(),1920,5760,{0,4},&problem);
    QVERIFY(problem.isEmpty()); auto extra=new Pedal(read.get()); extra->setTrack(4);extra->setTrack2(4);
    extra->setTick(Fraction::fromTicks(1920));extra->setTick2(Fraction::fromTicks(3840));
    read->startCmd();read->undoAddElement(extra);read->endCmd();
    read->startCmd(); XmlReader reader(fragment);reader.setPasteMode(true);
    QVERIFY(read->pasteStaff(reader,read->tick2segment(Fraction::fromTicks(1920),false,SegmentType::ChordRest),0));
    read->endCmd(); QCOMPARE(pedalRanges142(read.get()),deleted);
}
