void TestMidi::accentLinkedParts()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx")); QVERIFY(score);
      score->startCmd(); score->insertMeasure(ElementType::VBOX, score->first()); score->endCmd();
      auto partScore = new Score(score.get());
      auto excerpt = new Excerpt(score.get()); excerpt->setPartScore(partScore);
      excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano");
      Excerpt::createExcerpt(excerpt); score->excerpts().append(excerpt); partScore->doLayout();
      auto first = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      score->startCmd(); score->select(first->downNote(), SelectType::SINGLE, 0); score->addArticulation(SymId::articAccentAbove); score->endCmd();
      auto masterAccent = first->articulations().front();
      auto partChord = toChord(partScore->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      QCOMPARE(partChord->articulations().size(), 1); auto linked = partChord->articulations().front();
      QVERIFY(linked->getProperty(Pid::ARTIC_VELOCITY_MODE).toBool());
      partScore->startCmd(); linked->undoChangeProperty(Pid::ARTIC_VELOCITY_PERCENT, 107.5); partScore->endCmd();
      QCOMPARE(masterAccent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 107.5);
      EditData ed; score->undoRedo(true, &ed);
      QCOMPARE(masterAccent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 115.0); QCOMPARE(linked->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 115.0);
      score->undoRedo(false, &ed); QCOMPARE(linked->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 107.5);
      }

void TestMidi::accentVelocityPlaybackAndBase()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx")); QVERIFY(score);
      auto chord = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto note = chord->downNote(); auto instrument = chord->part()->instrument(chord->tick());
      MidiArticulation old; old.name = "sforzato"; old.velocity = 120; old.gateTime = 100;
      instrument->setArticulation({old});
      auto accent = new Articulation(SymId::articAccentAbove, score.get()); accent->setTrack(0); accent->setParent(chord); chord->add(accent);
      SynthesizerState state;
      auto velocity = [&]() { EventMap events; score->renderMidi(&events, false, false, state); for (const auto& e : events) if (e.second.note() == note && e.second.velo()) return e.second.velo(); return -1; };
      QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_MODE).toBool(), false);
      QCOMPARE(accent->velocityMultiplier(instrument), 1.2);
      int base = note->staff()->velocities().val(chord->tick());
      QCOMPARE(velocity(), qBound(1, int(base * 1.2), 127));
      accent->applyLightAccentPreset(); QCOMPARE(accent->velocityMultiplier(instrument), 1.15);
      QCOMPARE(velocity(), qBound(1, int(base * 1.15), 127));
      QCOMPARE(NoteVelocity::referenceBase(note), qBound(1, int(base * 1.15), 127));
      note->setVeloOffset(-10); QCOMPARE(velocity(), NoteVelocity::effective(NoteVelocity::referenceBase(note), note->veloType(), note->veloOffset()));
      note->setVeloType(Note::ValueType::USER_VAL); note->setVeloOffset(80);
      accent->setProperty(Pid::ARTIC_VELOCITY_PERCENT, 180.0); QCOMPARE(velocity(), 80);
      note->setVeloType(Note::ValueType::OFFSET_VAL); note->setVeloOffset(-50);
      accent->setProperty(Pid::ARTIC_VELOCITY_PERCENT, 400.0);
      QCOMPARE(NoteVelocity::referenceBase(note), 127); QCOMPARE(velocity(), 64);
      QCOMPARE(NoteVelocity::effective(NoteVelocity::referenceBase(note), note->veloType(), note->veloOffset()), velocity());
      QVERIFY(!accent->setProperty(Pid::ARTIC_VELOCITY_PERCENT, std::numeric_limits<double>::quiet_NaN()));
      QVERIFY(!accent->setProperty(Pid::ARTIC_VELOCITY_PERCENT, 0.0)); QVERIFY(!accent->setProperty(Pid::ARTIC_VELOCITY_PERCENT, 401.0));
      accent->setProperty(Pid::ARTIC_VELOCITY_PERCENT, 100.0); note->setVeloOffset(0); QCOMPARE(velocity(), base);
      accent->setPlayArticulation(false); accent->setProperty(Pid::ARTIC_VELOCITY_PERCENT, 200.0); QCOMPARE(velocity(), base);
      QCOMPARE(NoteVelocity::referenceBase(note), base);
      }

void TestMidi::accentPresetsCloneUndoAndPersistence()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR + "timed-arpeggio.mscx")); QVERIFY(score);
      auto first = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
      auto second = toChord(score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest)->element(0));
      for (auto symbol : {SymId::articAccentAbove, SymId::articAccentBelow, SymId::articAccentStaccatoAbove,
            SymId::articTenutoAccentAbove, SymId::articSoftAccentTenutoStaccatoAbove}) {
            Articulation a(symbol, score.get()); QVERIFY(!a.getProperty(Pid::ARTIC_VELOCITY_MODE).toBool()); a.applyLightAccentPreset();
            QVERIFY(a.getProperty(Pid::ARTIC_VELOCITY_MODE).toBool()); QCOMPARE(a.getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 115.0);
            }
      Articulation marcato(SymId::articMarcatoAbove, score.get()); marcato.applyLightAccentPreset(); QVERIFY(!marcato.getProperty(Pid::ARTIC_VELOCITY_MODE).toBool());
      score->startCmd(); score->select(first->downNote(), SelectType::SINGLE, 0); score->addArticulation(SymId::articAccentAbove); score->endCmd();
      QCOMPARE(first->articulations().size(), 1); auto accent = first->articulations().front(); QVERIFY(accent->getProperty(Pid::ARTIC_VELOCITY_MODE).toBool());
      score->startCmd();
      auto copy = toArticulation(accent->linkedClone()); copy->setParent(second); copy->setTrack(0); second->add(copy);
      score->endCmd();
      score->startCmd(); accent->undoChangeProperty(Pid::ARTIC_VELOCITY_PERCENT, 109.0); score->endCmd();
      QCOMPARE(copy->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 109.0);
      EditData ed; score->undoRedo(true, &ed); QCOMPARE(accent->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 115.0);
      QCOMPARE(copy->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 115.0);
      score->undoRedo(false, &ed); std::unique_ptr<Ms::Chord> clone(new Ms::Chord(*first));
      QCOMPARE(clone->articulations().front()->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 109.0);
      QTemporaryDir dir; QVERIFY(dir.isValid());
      for (const auto& suffix : {"mscx", "mscz"}) {
            QString path = dir.path() + "/accent." + suffix;
            if (QString(suffix) == "mscz") { QFileInfo info(path); QVERIFY(score->saveCompressedFile(info, false, false)); }
            else QVERIFY(saveScore(score.get(), path));
            std::unique_ptr<MasterScore> restored(readCreatedScore(path)); QVERIFY(restored);
            auto a = toChord(restored->firstMeasure()->first(SegmentType::ChordRest)->element(0))->articulations().front();
            QVERIFY(a->getProperty(Pid::ARTIC_VELOCITY_MODE).toBool()); QCOMPARE(a->getProperty(Pid::ARTIC_VELOCITY_PERCENT).toDouble(), 109.0);
            }
      }
