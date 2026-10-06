// GPL-2.0-or-later. Input/edit regressions against the real score model.
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <memory>
#include "mtest/testutils.h"
#include "libmscore/score.h"
#include "libmscore/inputrhythm.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/rest.h"
#include "libmscore/tie.h"
#include "libmscore/undo.h"
#include "libmscore/excerpt.h"
#include "libmscore/staff.h"
#include "libmscore/articulation.h"
#include "audio/midi/event.h"
using namespace Ms;
class TestInputRhythm : public QObject, public MTest {
      Q_OBJECT
      QTemporaryDir dir { QDir::currentPath()+"/rhythm-XXXXXX" };
      std::unique_ptr<MasterScore> blank(int numerator=4,int denominator=4)
            {
            QString xml="<museScore version=\"3.02\"><Score><Division>480</Division><Part>";
            for(int staff=1;staff<=2;++staff) xml+=QString("<Staff id=\"%1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>").arg(staff);
            xml+="<trackName>Piano</trackName><Instrument id=\"piano\"><trackName>Piano</trackName><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part>";
            for(int staff=1;staff<=2;++staff) {
                  xml+=QString("<Staff id=\"%1\">").arg(staff);
                  for(int measure=1;measure<=3;++measure) {
                        xml+=QString("<Measure number=\"%1\"><voice>").arg(measure);
                        if(measure==1) xml+=QString("<TimeSig><sigN>%1</sigN><sigD>%2</sigD></TimeSig>").arg(numerator).arg(denominator);
                        xml+=QString("<Rest><durationType>measure</durationType><duration>%1/%2</duration></Rest></voice></Measure>").arg(numerator).arg(denominator);
                        }
                  xml+="</Staff>";
                  }
            xml+="</Score></museScore>";
            const QString path=dir.filePath("blank.mscx"); QFile f(path);
            if(!f.open(QIODevice::WriteOnly) || f.write(xml.toUtf8())<0) return {};
            f.close(); return std::unique_ptr<MasterScore>(readCreatedScore(path));
            }
      Segment* prepare(Score* score, int tick, int track=0)
            {
            auto segment=score->firstMeasure()->first(SegmentType::ChordRest);
            if(tick) {score->startCmd();score->setNoteRest(segment,track,NoteVal(-1),Fraction::fromTicks(tick));score->endCmd();}
            return score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);
            }
      Note* enter(Score* score,Segment* segment,int duration,int pitch=60,int track=0)
            {
            auto& is=score->inputState();is.setTrack(track);is.setSegment(segment);is.setLastSegment(segment);
            is.setNoteEntryMode(true);is.setDuration(TDuration(Fraction::fromTicks(duration),true));
            NoteVal value(pitch);return score->addPitch(value,false);
            }
      QList<int> durations(Score* score,int start,int end,int track=0)
            {
            QList<int> result;
            for(int tick=start;tick<end;) {
                  auto cr=score->findCR(Fraction::fromTicks(tick),track);
                  if(!cr || cr->endTick().ticks()<=tick) break;
                  result.append(cr->actualTicks().ticks());tick=cr->endTick().ticks();
                  }
            return result;
            }
private slots:
      void initTestCase() { initMTest(); QVERIFY(dir.isValid()); }
      void init() { InputRhythm::setEnabled(true); }
      void cleanup() { InputRhythm::setEnabled(false); }
      void grouping_data()
            {
            QTest::addColumn<int>("n");QTest::addColumn<int>("d");QTest::addColumn<int>("start");
            QTest::addColumn<int>("duration");QTest::addColumn<bool>("rest");QTest::addColumn<QList<int>>("expected");
            QTest::newRow("whole")<<4<<4<<0<<1920<<false<<QList<int>{1920};
            QTest::newRow("short-long-short")<<4<<4<<480<<960<<false<<QList<int>{960};
            QTest::newRow("dotted-cross-midpoint")<<4<<4<<480<<720<<false<<QList<int>{480,240};
            QTest::newRow("offbeat-cross-midpoint")<<4<<4<<720<<480<<false<<QList<int>{240,240};
            QTest::newRow("barline")<<4<<4<<1440<<960<<false<<QList<int>{480,480};
            QTest::newRow("whole-rest")<<4<<4<<0<<1920<<true<<QList<int>{1920};
            QTest::newRow("split-rest")<<4<<4<<720<<480<<true<<QList<int>{240,240};
            QTest::newRow("2-4")<<2<<4<<0<<480<<false<<QList<int>{480};
            QTest::newRow("3-4")<<3<<4<<480<<480<<false<<QList<int>{480};
            QTest::newRow("6-8")<<6<<8<<0<<720<<false<<QList<int>{720};
            QTest::newRow("9-8")<<9<<8<<720<<720<<false<<QList<int>{720};
            QTest::newRow("12-8")<<12<<8<<720<<720<<false<<QList<int>{720};
            QTest::newRow("6-8-dorico-half")<<6<<8<<0<<960<<false<<QList<int>{720,240};
            QTest::newRow("6-8-whole-bar")<<6<<8<<0<<1440<<false<<QList<int>{1440};
            QTest::newRow("6-8-cross-big-beat")<<6<<8<<480<<480<<false<<QList<int>{240,240};
            QTest::newRow("6-8-intra-beat-quarter")<<6<<8<<0<<480<<false<<QList<int>{480};
            QTest::newRow("6-8-rest-half")<<6<<8<<0<<960<<true<<QList<int>{720,240};
            QTest::newRow("6-8-offbeat-rest")<<6<<8<<240<<480<<true<<QList<int>{240,240};
            QTest::newRow("9-8-cross-big-beat")<<9<<8<<480<<480<<false<<QList<int>{240,240};
            QTest::newRow("9-8-two-big-beats")<<9<<8<<0<<1440<<false<<QList<int>{1440};
            QTest::newRow("12-8-cross-big-beat")<<12<<8<<1200<<480<<false<<QList<int>{240,240};
            QTest::newRow("12-8-half-bar")<<12<<8<<0<<1440<<false<<QList<int>{1440};
            QTest::newRow("6-4-dorico-scaled")<<6<<4<<0<<1920<<false<<QList<int>{1440,480};
            QTest::newRow("6-16-dorico-scaled")<<6<<16<<0<<480<<false<<QList<int>{360,120};
            // In 2/2 a quarter is a half-beat syncopation, unlike 4/4.
            QTest::newRow("2-2-half-beat-syncopation")<<2<<2<<720<<480<<false<<QList<int>{480};
            QTest::newRow("3-4-full-bar")<<3<<4<<0<<1440<<false<<QList<int>{1440};
            QTest::newRow("3-4-no-artificial-midpoint")<<3<<4<<480<<960<<false<<QList<int>{960};
            QTest::newRow("3-8-full-bar")<<3<<8<<0<<720<<false<<QList<int>{720};
            }
      void grouping()
            {
            QFETCH(int,n);QFETCH(int,d);QFETCH(int,start);QFETCH(int,duration);QFETCH(bool,rest);QFETCH(QList<int>,expected);
            auto score=blank(n,d);QVERIFY(score);auto segment=prepare(score.get(),start);QVERIFY(segment);
            score->startCmd();
            if(rest) score->setNoteRest(segment,0,NoteVal(-1),Fraction::fromTicks(duration),Direction::AUTO,false,true);
            else QVERIFY(enter(score.get(),segment,duration));
            score->endCmd();QCOMPARE(durations(score.get(),start,start+duration),expected);
            if(!rest) {
                  auto first=toChord(score->findCR(Fraction::fromTicks(start),0))->upNote();
                  QCOMPARE(first->playTicksFraction().ticks(),duration);
                  QCOMPARE(score->inputState().tick().ticks(),start+duration);
                  }
            else if(start==0 && duration==score->firstMeasure()->ticks().ticks())
                  QVERIFY(score->findCR(Fraction(),0)->isFullMeasureRest());
            }
      void chordCursorUndoAndSave()
            {
            auto score=blank();QVERIFY(score);auto segment=prepare(score.get(),720);QVERIFY(segment);
            score->startCmd();QVERIFY(enter(score.get(),segment,480));
            const int cursor=score->inputState().tick().ticks();NoteVal value(64);QVERIFY(score->addPitch(value,true));score->endCmd();
            QCOMPARE(score->inputState().tick().ticks(),cursor);QCOMPARE(cursor,1200);
            for(int tick : {720,960}) {auto c=toChord(score->findCR(Fraction::fromTicks(tick),0));QCOMPARE(c->notes().size(),size_t(2));QVERIFY(c->findNote(64));}
            QCOMPARE(toChord(score->findCR(Fraction::fromTicks(720),0))->findNote(64)->playTicksFraction().ticks(),480);
            score->undoStack()->undo(&ed);QVERIFY(score->findCR(Fraction::fromTicks(720),0)->isRest());
            score->undoStack()->redo(&ed);QCOMPARE(durations(score.get(),720,1200),QList<int>({240,240}));
            const QString path=dir.filePath("saved.mscx");QVERIFY(saveScore(score.get(),path));
            auto reopened=std::unique_ptr<MasterScore>(readCreatedScore(path));QVERIFY(reopened);
            QCOMPARE(toChord(reopened->findCR(Fraction::fromTicks(720),0))->findNote(64)->playTicksFraction().ticks(),480);
            auto midiNotes=[](Score* source) {
                  EventMap events;source->renderMidi(&events,source->synthesizerState());
                  QStringList result;
                  for(const auto& item:events) if(!item.second.discard() && (item.second.type()==ME_NOTEON || item.second.type()==ME_NOTEOFF))
                        result.append(QString("%1/%2/%3/%4").arg(item.first).arg(item.second.type()).arg(item.second.pitch()).arg(item.second.velo()));
                  return result;
                  };
            InputRhythm::setEnabled(false);auto reference=blank();QVERIFY(reference);
            auto start=prepare(reference.get(),720);reference->startCmd();QVERIFY(enter(reference.get(),start,480));
            QVERIFY(reference->addPitch(value,true));reference->endCmd();
            QVERIFY(!midiNotes(reference.get()).isEmpty());QCOMPARE(midiNotes(reopened.get()),midiNotes(reference.get()));
            }
      void compoundEntryContinuesWithRequestedDuration()
            {
            auto score=blank(6,8);QVERIFY(score);
            auto& is=score->inputState();
            auto segment=prepare(score.get(),0);QVERIFY(segment);
            score->startCmd();QVERIFY(enter(score.get(),segment,960));score->endCmd();
            QCOMPARE(durations(score.get(),0,960),QList<int>({720,240}));
            QCOMPARE(is.tick().ticks(),960);QCOMPARE(is.duration().fraction(),Fraction(1,2));
            NoteVal next(62);score->startCmd();QVERIFY(score->addPitch(next,false));score->endCmd();
            QCOMPARE(is.tick().ticks(),1920);QCOMPARE(is.duration().fraction(),Fraction(1,2));
            QCOMPARE(toChord(score->findCR(Fraction(1,2),0))->upNote()->playTicksFraction().ticks(),960);
            }
      void toggleChangeAndIdempotence()
            {
            auto score=blank();QVERIFY(score);auto segment=prepare(score.get(),480);InputRhythm::setEnabled(false);
            score->startCmd();QVERIFY(enter(score.get(),segment,480));score->endCmd();
            auto cr=score->findCR(Fraction::fromTicks(480),0);
            score->startCmd();InputRhythm::changeDuration(score.get(),cr,Fraction::fromTicks(720));score->endCmd();
            QCOMPARE(durations(score.get(),480,1200),QList<int>({720}));
            InputRhythm::setEnabled(true);QCOMPARE(durations(score.get(),480,1200),QList<int>({720}));
            score->startCmd();QVERIFY(InputRhythm::normalize(score.get(),score->findCR(Fraction::fromTicks(480),0)));score->endCmd();
            QCOMPARE(durations(score.get(),480,1200),QList<int>({480,240}));
            cr=score->findCR(Fraction::fromTicks(480),0);score->startCmd();QVERIFY(!InputRhythm::normalize(score.get(),cr));score->endCmd();
            QCOMPARE(score->findCR(Fraction::fromTicks(480),0),cr);
            score->undoStack()->undo(&ed);QCOMPARE(durations(score.get(),480,1200),QList<int>({720}));
            }
      void localEditVoiceAndCrossStaff()
            {
            auto score=blank();QVERIFY(score);auto segment=prepare(score.get(),480);
            score->startCmd();QVERIFY(enter(score.get(),segment,480));score->endCmd();
            auto original=toChord(score->findCR(Fraction::fromTicks(480),0));original->setStaffMove(1);
            auto other=score->firstMeasure()->first(SegmentType::ChordRest)->cr(4);
            score->startCmd();InputRhythm::changeDuration(score.get(),original,Fraction::fromTicks(720));score->endCmd();
            QCOMPARE(durations(score.get(),480,1200),QList<int>({480,240}));
            for(int tick : {480,960}) QCOMPARE(score->findCR(Fraction::fromTicks(tick),0)->staffMove(),1);
            QCOMPARE(score->firstMeasure()->first(SegmentType::ChordRest)->cr(4),other);
            score->undoStack()->undo(&ed);QCOMPARE(durations(score.get(),480,960),QList<int>({480}));
            }
      void durationCommandAndIndependentVoice()
            {
            auto score=blank();QVERIFY(score);auto first=score->firstMeasure()->first(SegmentType::ChordRest);
            score->startCmd();score->expandVoice(first,1);score->setNoteRest(first,1,NoteVal(67),Fraction(1,4));score->endCmd();
            auto voice=score->findCR(Fraction(),1);
            auto segment=prepare(score.get(),480);score->startCmd();QVERIFY(enter(score.get(),segment,480));score->endCmd();
            score->inputState().setNoteEntryMode(false);
            score->select(toChord(score->findCR(Fraction(1,4),0))->upNote());
            score->startCmd();score->cmdIncDurationDotted();score->endCmd();
            QCOMPARE(durations(score.get(),480,1200),QList<int>({480,240}));
            QCOMPARE(score->findCR(Fraction(),1),voice);
            score->undoStack()->undo(&ed);QCOMPARE(durations(score.get(),480,960),QList<int>({480}));
            }
      void complexObjectsAreSkipped()
            {
            auto score=blank();QVERIFY(score);auto segment=prepare(score.get(),720);InputRhythm::setEnabled(false);
            score->startCmd();QVERIFY(enter(score.get(),segment,480));score->endCmd();InputRhythm::setEnabled(true);
            auto c=toChord(score->findCR(Fraction::fromTicks(720),0));auto note=c->upNote();
            c->setPlayEventType(PlayEventType::User);note->playEvents().front().setOntime(123);note->playEvents().front().setLen(543);
            score->startCmd();QVERIFY(!InputRhythm::normalize(score.get(),c));score->endCmd();
            QCOMPARE(note->playEvents().front().ontime(),123);QCOMPARE(note->playEvents().front().len(),543);
            QCOMPARE(score->findCR(Fraction::fromTicks(720),0),static_cast<ChordRest*>(c));
            c->setPlayEventType(PlayEventType::Auto);auto articulation=new Articulation(score.get());c->add(articulation);
            QVERIFY(!InputRhythm::eligible(c));
            }
      void rangeDurationCommandKeepsSelection()
            {
            auto score=blank();QVERIFY(score);auto segment=prepare(score.get(),480);
            score->startCmd();QVERIFY(enter(score.get(),segment,480));score->endCmd();
            score->inputState().setNoteEntryMode(false);
            score->selection().setRange(score->findCR(Fraction(1,4),0)->segment(),
                  score->tick2segment(Fraction(1,2),false,SegmentType::ChordRest),0,1);
            score->selection().updateSelectedElements();
            score->startCmd();score->cmdIncDurationDotted();score->endCmd();
            QCOMPARE(durations(score.get(),480,1200),QList<int>({480,240}));
            QVERIFY(score->selection().isRange());
            QCOMPARE(score->selection().tickStart(),Fraction(1,4));
            QCOMPARE(score->selection().tickEnd(),Fraction(5,8));
            score->undoStack()->undo(&ed);QCOMPARE(durations(score.get(),480,960),QList<int>({480}));
            }
      void appliedDurationAdvancesLogicalCursor()
            {
            auto score=blank();QVERIFY(score);auto segment=prepare(score.get(),480);
            score->startCmd();QVERIFY(enter(score.get(),segment,480));score->endCmd();
            auto chord=toChord(score->findCR(Fraction(1,4),0));
            score->select(chord->upNote());score->inputState().setSegment(chord->segment());
            score->inputState().setDuration(TDuration(Fraction(3,8),true));
            score->startCmd();score->cmdApplyInputState();score->endCmd();
            QCOMPARE(durations(score.get(),480,1200),QList<int>({480,240}));
            QCOMPARE(score->inputState().tick().ticks(),1200);
            score->undoStack()->undo(&ed);QCOMPARE(durations(score.get(),480,960),QList<int>({480}));
            }
      void linkedExcerpt()
            {
            auto score=blank();QVERIFY(score);
            score->startCmd();score->insertMeasure(ElementType::VBOX,score->first());
            Score* part=new Score(score.get());auto excerpt=new Excerpt(score.get());excerpt->setPartScore(part);
            excerpt->setTitle("Piano");excerpt->setParts(score->parts());Excerpt::createExcerpt(excerpt);
            score->excerpts().append(excerpt);score->endCmd();
            auto segment=prepare(score.get(),720);score->startCmd();QVERIFY(enter(score.get(),segment,480));score->endCmd();
            QCOMPARE(durations(part,720,1200),QList<int>({240,240}));
            QCOMPARE(toChord(part->findCR(Fraction::fromTicks(720),0))->upNote()->playTicksFraction().ticks(),480);
            }
      void externalTieConnections()
            {
            auto score=blank();QVERIFY(score);InputRhythm::setEnabled(false);
            score->startCmd();
            auto first=score->firstMeasure()->first(SegmentType::ChordRest);
            score->setNoteRest(first,0,NoteVal(60),Fraction(3,8));
            auto before=toChord(score->findCR(Fraction(),0));score->addNote(before,NoteVal(65));
            score->setNoteRest(score->tick2segment(Fraction(3,8),false,SegmentType::ChordRest),0,NoteVal(60),Fraction(1,4));
            auto target=toChord(score->findCR(Fraction(3,8),0));
            auto tie=new Tie(score.get());tie->setStartNote(before->findNote(60));tie->setEndNote(target->findNote(60));
            tie->setTick(Fraction());tie->setTick2(Fraction(3,8));tie->setTrack(0);score->undoAddElement(tie);score->endCmd();
            InputRhythm::setEnabled(true);score->startCmd();QVERIFY(InputRhythm::normalize(score.get(),target));score->endCmd();
            auto rewritten=toChord(score->findCR(Fraction(3,8),0))->findNote(60);
            QVERIFY(rewritten->tieBack());QCOMPARE(rewritten->tieBack()->startNote(),before->findNote(60));
            QCOMPARE(before->findNote(60)->tieFor()->endNote(),rewritten);
            QCOMPARE(before->findNote(60)->playTicksFraction(),Fraction(5,8));
            score->undoStack()->undo(&ed);QCOMPARE(before->findNote(60)->tieFor()->endNote()->chord()->ticks(),Fraction(1,4));
            }
      void externalCursorKeepsOldPolicy()
            {
            auto score=blank();QVERIFY(score);auto segment=prepare(score.get(),720);
            InputState external=score->inputState();external.setTrack(0);external.setSegment(segment);
            external.setDuration(TDuration(Fraction(1,4)));external.setNoteEntryMode(true);
            NoteVal value(60);score->startCmd();QVERIFY(score->addPitch(value,false,&external));score->endCmd();
            QCOMPARE(durations(score.get(),720,1200),QList<int>({480}));
            }
};
QTEST_MAIN(TestInputRhythm)
#include "tst_inputrhythm.moc"
