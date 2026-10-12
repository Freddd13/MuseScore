      void quarterChangedAfterDottedQuarter()
            {
            auto score=blank();QVERIFY(score);
            score->startCmd();QVERIFY(enter(score.get(),prepare(score.get(),0),720));score->endCmd();
            score->startCmd();auto note=enter(score.get(),score->inputState().segment(),240);QVERIFY(note);score->endCmd();
            auto target=toChord(score->findCR(Fraction::fromTicks(720),0));score->select(target->upNote());
            score->inputState().setSegment(target->segment());score->inputState().setDuration(TDuration(Fraction(1,4)));
            score->startCmd();score->cmdApplyInputState();score->endCmd();
            QCOMPARE(durations(score.get(),0,1200),QList<int>({720,240,240}));QCOMPARE(score->inputState().tick().ticks(),1200);
            auto first=toChord(score->findCR(Fraction::fromTicks(720),0));auto last=toChord(score->findCR(Fraction::fromTicks(960),0));
            QVERIFY(first->upNote()->tieFor());QCOMPARE(first->upNote()->tieFor()->endNote(),last->upNote());
            QCOMPARE(first->upNote()->playTicksFraction().ticks(),480);score->doLayout();
            QVERIFY(!first->beam() || first->beam()!=last->beam());
            auto midi=[](Score* source){EventMap events;source->renderMidi(&events,source->synthesizerState());QStringList notes;for(const auto& item:events)if(!item.second.discard()&&(item.second.type()==ME_NOTEON||item.second.type()==ME_NOTEOFF))notes.append(QString("%1/%2/%3/%4").arg(item.first).arg(item.second.type()).arg(item.second.pitch()).arg(item.second.velo()));return notes;};
            const auto before=midi(score.get());QVERIFY(!before.isEmpty());
            InputNotation::setTiedBeams(true);score->doLayout();QVERIFY(first->beam());QCOMPARE(first->beam(),last->beam());
            QCOMPARE(first->beamMode(),Beam::Mode::AUTO);QCOMPARE(last->beamMode(),Beam::Mode::AUTO);QCOMPARE(midi(score.get()),before);
            last->setBeamMode(Beam::Mode::BEGIN);score->doLayout();QVERIFY(!first->beam()||first->beam()!=last->beam());
            }
      void threeThreeTwoStemsAndManualSettings()
            {
            auto score=blank();QVERIFY(score);
            auto segment=prepare(score.get(),0);
            for(auto pair:QVector<QPair<int,int>>{{360,67},{360,71},{240,74}}){score->startCmd();QVERIFY(enter(score.get(),segment,pair.first,pair.second));score->endCmd();segment=score->inputState().segment();}
            QCOMPARE(durations(score.get(),0,960),QList<int>({360,120,240,240}));score->doLayout();
            auto start=toChord(score->findCR(Fraction::fromTicks(360),0));auto end=toChord(score->findCR(Fraction::fromTicks(480),0));
            QVERIFY(start->beam());QVERIFY(end->beam());QVERIFY(start->beam()!=end->beam());
            QVERIFY(start->up()!=end->up());QCOMPARE(start->upNote()->playTicksFraction().ticks(),360);
            InputNotation::setCoherentStems(true);score->doLayout();QCOMPARE(start->up(),end->up());
            QCOMPARE(int(start->stemDirection()),int(Direction::AUTO));QCOMPARE(int(end->stemDirection()),int(Direction::AUTO));
            start->setStemDirection(Direction::UP);score->doLayout();QVERIFY(start->up());QVERIFY(!end->up());
            start->setStemDirection(Direction::AUTO);start->setStaffMove(1);score->doLayout();QCOMPARE(start->staffMove(),1);
            InputNotation::setCoherentStems(false);score->doLayout();QCOMPARE(start->staffMove(),1);
            }
      void manualNoBeamSurvivesDurationChanges()
            {
            auto score=blank();QVERIFY(score);auto segment=prepare(score.get(),480);
            score->startCmd();QVERIFY(enter(score.get(),segment,480));score->endCmd();
            auto chord=toChord(score->findCR(Fraction::fromTicks(480),0));chord->setBeamMode(Beam::Mode::NONE);
            QVERIFY(!InputRhythm::eligible(chord));score->startCmd();InputRhythm::changeDuration(score.get(),chord,Fraction::fromTicks(720));score->endCmd();
            QCOMPARE(durations(score.get(),480,1200),QList<int>({720}));
            QCOMPARE(score->findCR(Fraction::fromTicks(480),0)->beamMode(),Beam::Mode::NONE);
            score->undoStack()->undo(&ed);QCOMPARE(durations(score.get(),480,960),QList<int>({480}));
            }
