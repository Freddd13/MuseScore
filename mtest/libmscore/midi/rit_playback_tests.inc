namespace {
TextLine* addTestRit(MasterScore* score, int from, int until)
      {
      auto line = new TextLine(score, true); line->setTrack(0); line->setTick(Fraction::fromTicks(from)); line->setTicks(Fraction::fromTicks(until-from));
      line->setBeginText("rit."); line->setProperty(Pid::RIT_MODE, true); line->setProperty(Pid::RIT_PLAY, true);
      score->startCmd(); score->undoAddElement(line); score->endCmd(); return line;
      }
double referenceRitSeconds(const TextLine* line, double start, double from, double until)
      {
      const int intervals = 100000;
      double result = 0, step = (until-from)/intervals;
      for (int i=0;i<intervals;++i) result += step / (DIVISION * TempoExpression::value(line,start,from+(i+.5)*step));
      return result;
      }
}
void TestMidi::ritCurveTimingAndLayout()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR+"timed-arpeggio.mscx")); QVERIFY(score);
      auto line = addTestRit(score.get(),0,1920);
      const double exact = -10 * std::log(.8); // four beats, 120 -> 96 BPM
      QVERIFY(std::abs(score->tempomap()->tick2time(1920)-exact)<.000001);
      QCOMPARE(score->tempo(Fraction::fromTicks(1920)),1.6);
      for (double curvature : {.1,1.0,2.0,8.0}) {
            score->startCmd(); line->undoChangeProperty(Pid::RIT_CURVE,curvature); score->endCmd();
            const double expected = referenceRitSeconds(line,2,0,1920);
            const double actual = score->tempomap()->tick2time(1920);
            QVERIFY2(std::abs(actual-expected)/expected*60 < .005,qPrintable(QString("curvature %1, error/min %2").arg(curvature).arg(std::abs(actual-expected)/expected*60)));
            for (int tick=1;tick<1920;tick+=79) {
                  const double prefix=referenceRitSeconds(line,2,0,tick);
                  QVERIFY2(std::abs(score->tempomap()->tick2time(tick)-prefix)<.0001,"Prefix error must stay below 0.1 ms at native ticks");
                  }
            score->setLayoutMode(LayoutMode::LINE); score->doLayout(); QVERIFY(std::abs(score->tempomap()->tick2time(1920)-actual)<1e-9);
            score->doLayoutRange(Fraction::fromTicks(1920),Fraction::fromTicks(3840)); QVERIFY(std::abs(score->tempomap()->tick2time(1920)-actual)<1e-9);
            score->tempomap()->setRelTempo(1.25); QCOMPARE(score->tempomap()->tick2time(1920),actual/1.25);
            score->tempomap()->setRelTempo(1);
            }
      score->startCmd(); line->undoChangeProperty(Pid::RIT_PLAY,false); score->endCmd();
      QCOMPARE(score->tempomap()->tick2time(1920),2.0); score->undoRedo(true,&ed); QVERIFY(line->ritPlay());
      score->undoRedo(false,&ed); QCOMPARE(score->tempomap()->tick2time(1920),2.0);
      score->undoRedo(true,&ed); score->firstMeasure()->setRepeatStart(true); score->lastMeasure()->setRepeatEnd(true);
      score->setExpandRepeats(true); score->setPlaylistDirty();
      QCOMPARE(score->repeatList().ticks(),7680);
      const double duration=score->tempomap()->tick2time(3840);
      QVERIFY(std::abs(score->repeatList().utick2utime(7680)-2*duration)<1e-8);
      QVERIFY(std::abs(score->repeatList().utick2utime(5760)-duration-score->tempomap()->tick2time(1920))<1e-8);
      }
void TestMidi::ritRestoreAndFermata()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR+"timed-arpeggio.mscx")); QVERIFY(score);
      auto first = addTestRit(score.get(),0,960); auto second = addTestRit(score.get(),960,1920);
      QCOMPARE(first->tick2().ticks(),960); QCOMPARE(second->tick().ticks(),960); QCOMPARE(second->tick2().ticks(),1920);
      QTemporaryDir exactDir; QVERIFY(exactDir.isValid());
      QVERIFY(saveScore(score.get(),exactDir.path()+"/exact.mscx"));
      std::unique_ptr<MasterScore> exactScore(readCreatedScore(exactDir.path()+"/exact.mscx")); QVERIFY(exactScore);
      auto exactCurves=TempoExpression::curves(exactScore.get()); QCOMPARE(exactCurves.size(),2);
      QCOMPARE(exactCurves.front()->tick2().ticks(),960); QCOMPARE(exactCurves.back()->tick().ticks(),960);
      score->startCmd(); second->undoChangeProperty(Pid::RIT_PLAY,false); score->endCmd();
      QCOMPARE(second->tick().ticks(),960); QCOMPARE(second->tick2().ticks(),1920); score->undoRedo(true,&ed);
      auto segment = score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest);
      auto restore = new TempoText(score.get()); restore->setParent(segment); restore->setTrack(0); restore->setXmlText("a tempo"); restore->setProperty(Pid::TEMPO_RESTORE_MODE,1);
      score->startCmd(); score->undoAddElement(restore); score->endCmd(); QCOMPARE(score->tempo(segment->tick()),1.6);
      score->startCmd(); static_cast<Element*>(restore)->undoChangeProperty(Pid::TEMPO_RESTORE_MODE,2); score->endCmd(); QCOMPARE(score->tempo(segment->tick()),2.0);
      score->startCmd(); static_cast<Element*>(restore)->undoChangeProperty(Pid::TEMPO,1.5); static_cast<Element*>(restore)->undoChangeProperty(Pid::TEMPO_RESTORE_MODE,3); score->endCmd(); QCOMPARE(score->tempo(segment->tick()),1.5);
      score->startCmd(); first->undoChangeProperty(Pid::RIT_START_BPM,150.0); score->endCmd(); QCOMPARE(first->ritDerivedStart(),2.5);
      auto start = score->firstMeasure()->first(SegmentType::ChordRest);
      auto fermata = new Fermata(SymId::fermataAbove,score.get()); fermata->setTrack(0); fermata->setParent(start); fermata->setTimeStretch(2);
      score->startCmd(); score->undoAddElement(fermata); score->endCmd();
      const double expected = referenceRitSeconds(first,2.5,0,960) + referenceRitSeconds(first,2.5,0,start->ticks().ticks()-1);
      QVERIFY(std::abs(score->tempomap()->tick2time(960)-expected)<.00001);
      auto before = score->tempomap()->tick2time(960); score->tempomap()->setPause(240,.3); TempoExpression::rebuild(score.get());
      QVERIFY(std::abs(score->tempomap()->tick2time(960)-before-.3)<.0000001);
      }
void TestMidi::ritConflictPersistenceAndParts()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR+"timed-arpeggio.mscx")); QVERIFY(score);
      score->startCmd(); score->insertMeasure(ElementType::VBOX,score->first()); score->endCmd();
      auto part = new Score(score.get()); auto excerpt = new Excerpt(score.get()); excerpt->setPartScore(part); excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano");
      Excerpt::createExcerpt(excerpt); score->excerpts().append(excerpt); part->doLayout();
      auto line = addTestRit(score.get(),0,1920); TextLine* linked = nullptr;
      for (const auto& item : part->spannerMap().map()) if (item.second->isTextLine()) linked = toTextLine(item.second);
      QVERIFY(linked); part->startCmd(); linked->undoChangeProperty(Pid::RIT_TARGET,75.0); linked->undoChangeProperty(Pid::RIT_START_BPM,100.0); part->endCmd();
      QCOMPARE(line->ritTarget(),75.0); QCOMPARE(line->ritStartBpm(),100.0);
      score->undoRedo(true,&ed); QCOMPARE(line->ritTarget(),80.0); QCOMPARE(line->ritStartBpm(),0.0); score->undoRedo(false,&ed);
      std::unique_ptr<TextLine> copy(line->clone()); QCOMPARE(copy->ritStartBpm(),100.0);
      QVERIFY(!TempoExpression::conflict(score.get(),480,2400).isEmpty()); QVERIFY(TempoExpression::conflict(score.get(),1920,3840).isEmpty());
      auto middle = score->firstMeasure()->findSegment(SegmentType::ChordRest,Fraction::fromTicks(480)); QVERIFY(middle);
      auto text = new TempoText(score.get()); text->setTrack(0); text->setParent(middle); text->setTempo(1.0);
      score->startCmd(); score->undoAddElement(text); score->endCmd();
      QVERIFY(!TempoExpression::conflict(score.get(),0,1920,line).isEmpty()); QCOMPARE(score->tempo(Fraction::fromTicks(480)),1.0);
      score->undoRedo(true,&ed); QVERIFY(TempoExpression::conflict(score.get(),0,1920,line).isEmpty());
      QTemporaryDir dir; QVERIFY(dir.isValid());
      for (const auto& extension : {"mscx","mscz"}) {
            QString file=dir.path()+"/rit."+extension;
            if (QString(extension)=="mscz") { QFileInfo info(file); QVERIFY(score->saveCompressedFile(info,false,false)); }
            else QVERIFY(saveScore(score.get(),file));
            std::unique_ptr<MasterScore> restored(readCreatedScore(file)); QVERIFY(restored); auto curves=TempoExpression::curves(restored.get()); QCOMPARE(curves.size(),1);
            QCOMPARE(curves.front()->ritTarget(),75.0); QCOMPARE(curves.front()->ritStartBpm(),100.0);
            QVERIFY(std::abs(restored->tempomap()->tick2time(1920)-score->tempomap()->tick2time(1920))<1e-9);
            }
      }
