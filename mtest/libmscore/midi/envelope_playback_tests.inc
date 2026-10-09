namespace {
Ms::Chord* envelopeChord(MasterScore* score, int tick)
      { return toChord(score->firstMeasure()->findSegment(SegmentType::ChordRest,Fraction::fromTicks(tick))->element(0)); }
Tremolo* addEnvelopeTremolo(MasterScore* score, Ms::Chord* chord, TremoloType type, int mode)
      {
      auto t=new Tremolo(score); t->setTremoloType(type); t->applyEnvelopePreset(mode); t->setTrack(chord->track()); t->setParent(chord);
      auto next=chord->segment()->next(SegmentType::ChordRest);
      t->setChords(chord,t->twoNotes() && next ? toChord(next->element(chord->track())) : nullptr);
      score->startCmd(); score->undoAddElement(t); score->endCmd(); return t;
      }
QVector<QPair<int,NPlayEvent>> envelopeAttacks(MasterScore* score, const Note* note=nullptr)
      {
      EventMap events; SynthesizerState state; score->setPlaylistDirty(); score->renderMidi(&events,false,false,state);
      QVector<QPair<int,NPlayEvent>> result;
      for (const auto& e:events) if(e.second.type()==ME_NOTEON && e.second.velo()>0 && (!note || e.second.note()==note)) result.append(qMakePair(e.first,e.second));
      return result;
      }
void envelopeAbsolute(Ms::Chord* chord, int velocity)
      { for(auto note:chord->notes()) { note->setVeloType(Note::ValueType::USER_VAL); note->setVeloOffset(velocity); } }
void envelopeTie(MasterScore* score, Note* first, Note* second)
      {
      auto tie=new Tie(score); tie->setStartNote(first); tie->setEndNote(second); tie->setTrack(first->track());
      first->setTieFor(tie); second->setTieBack(tie);
      }
}
void TestMidi::envelopeSingleAndTies()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR+"envelope.mscx")); QVERIFY(score);
      auto first=envelopeChord(score.get(),0); auto second=envelopeChord(score.get(),480); auto third=envelopeChord(score.get(),960);
      auto note=first->downNote(); envelopeAbsolute(first,80); envelopeAbsolute(second,80); envelopeAbsolute(third,80);
      auto tremolo=addEnvelopeTremolo(score.get(),first,TremoloType::R16,0);
      auto events=envelopeAttacks(score.get(),note); QCOMPARE(events.size(),4); for(auto e:events) QCOMPARE(e.second.velo(),80);
      tremolo->applyEnvelopePreset(1); events=envelopeAttacks(score.get(),note); QCOMPARE(events.size(),4); QCOMPARE(events[0].second.velo(),80);
      for(int i=1;i<events.size();++i) QCOMPARE(events[i].second.velo(),72);
      QCOMPARE(note->veloOffset(),80); QCOMPARE(note->veloType(),Note::ValueType::USER_VAL);
      note->setVeloType(Note::ValueType::OFFSET_VAL); note->setVeloOffset(-10); events=envelopeAttacks(score.get(),note);
      QCOMPARE(events.front().second.velo(),72); QCOMPARE(events.back().second.velo(),65); envelopeAbsolute(first,80);
      tremolo->applyEnvelopePreset(4); tremolo->setProperty(Pid::ENVELOPE_START,400.0); tremolo->setProperty(Pid::ENVELOPE_END,400.0);
      events=envelopeAttacks(score.get(),note); for(auto e:events) QCOMPARE(e.second.velo(),127);
      tremolo->setProperty(Pid::ENVELOPE_START,1.0); tremolo->setProperty(Pid::ENVELOPE_END,1.0);
      events=envelopeAttacks(score.get(),note); for(auto e:events) QCOMPARE(e.second.velo(),1); tremolo->applyEnvelopePreset(1);
      envelopeTie(score.get(),note,second->downNote()); envelopeTie(score.get(),second->downNote(),third->downNote());
      auto next=addEnvelopeTremolo(score.get(),second,TremoloType::R16,1);
      events=envelopeAttacks(score.get(),note); QCOMPARE(events.size(),12);
      for(int i=0;i<events.size();++i) { QCOMPARE(events[i].first,i*120); QCOMPARE(events[i].second.velo(),i ? 72 : 80); }
      QVERIFY(envelopeAttacks(score.get(),second->downNote()).isEmpty()); QVERIFY(envelopeAttacks(score.get(),third->downNote()).isEmpty());
      tremolo->applyEnvelopePreset(2); next->applyEnvelopePreset(2);
      events=envelopeAttacks(score.get(),note); QCOMPARE(events.size(),12); QCOMPARE(events.front().second.velo(),64); QCOMPARE(events.back().second.velo(),80);
      for(int i=1;i<events.size();++i) QVERIFY(events[i].second.velo()>=events[i-1].second.velo());
      tremolo->setProperty(Pid::ENVELOPE_CURVE,2.0); next->setProperty(Pid::ENVELOPE_CURVE,2.0);
      auto curved=envelopeAttacks(score.get(),note); QCOMPARE(curved.back().second.velo(),80); QVERIFY(curved[5].second.velo()<events[5].second.velo());
      tremolo->applyEnvelopePreset(3); next->applyEnvelopePreset(3); events=envelopeAttacks(score.get(),note);
      QCOMPARE(events.front().second.velo(),80); QCOMPARE(events.back().second.velo(),64);
      next->applyEnvelopePreset(1); events=envelopeAttacks(score.get(),note); QCOMPARE(events.size(),4);
      auto other=envelopeAttacks(score.get(),second->downNote()); QCOMPARE(other.size(),8); QCOMPARE(other.front().second.velo(),80); QCOMPARE(other.back().second.velo(),72);
      second->setPlayEventType(PlayEventType::User); second->downNote()->playEvents().clear(); second->downNote()->playEvents().append(NoteEvent(0,0,1000));
      events=envelopeAttacks(score.get(),note); QCOMPARE(events.size(),4);
      // The normal tie semantics can suppress a single custom continuation; its stored data is never regenerated.
      QCOMPARE(second->downNote()->playEvents().size(),1); QCOMPARE(second->downNote()->playEvents().front().len(),1000);
      QVERIFY(!tremolo->setProperty(Pid::ENVELOPE_CURVE,std::numeric_limits<double>::quiet_NaN()));
      QVERIFY(!tremolo->setProperty(Pid::ENVELOPE_START,0.0)); QVERIFY(!tremolo->setProperty(Pid::ENVELOPE_ALTERNATE,101.0));
      }
void TestMidi::envelopeDoubleAndCustom()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR+"envelope.mscx")); QVERIFY(score);
      auto first=envelopeChord(score.get(),0); auto second=envelopeChord(score.get(),480);
      auto n1=first->downNote(); auto n2=second->downNote(); n2->setPitch(64); n2->setTpcFromPitch(); envelopeAbsolute(first,80); envelopeAbsolute(second,100);
      auto tremolo=addEnvelopeTremolo(score.get(),first,TremoloType::C16,1); QVERIFY(tremolo->chord2()==second);
      auto all=envelopeAttacks(score.get()); auto a=envelopeAttacks(score.get(),n1); auto b=envelopeAttacks(score.get(),n2);
      QCOMPARE(a.size(),4); QCOMPARE(b.size(),4); QCOMPARE(a.front().second.velo(),80); QCOMPARE(a.back().second.velo(),72);
      for(auto e:b) { QCOMPARE(e.second.velo(),85); QCOMPARE(e.second.pitch(),64); QCOMPARE(e.second.noteEventOwner(),n1); QVERIFY(e.second.noteEventIndex()>=0); }
      for(int i=0;i<4;++i) { QCOMPARE(a[i].first,i*240); QCOMPARE(b[i].first,i*240+120); }
      n2->setPlay(false); QVERIFY(envelopeAttacks(score.get(),n2).isEmpty()); n2->setPlay(true);
      n2->setVeloType(Note::ValueType::OFFSET_VAL); n2->setVeloOffset(-10); b=envelopeAttacks(score.get(),n2); for(auto e:b) QCOMPARE(e.second.velo(),61); envelopeAbsolute(second,100);
      tremolo->applyEnvelopePreset(2); tremolo->setProperty(Pid::ENVELOPE_ALTERNATE,0.0);
      a=envelopeAttacks(score.get(),n1); b=envelopeAttacks(score.get(),n2); QCOMPARE(a.front().second.velo(),64); QCOMPARE(b.back().second.velo(),100);
      QVERIFY(a.back().second.velo()<80); // last attack belongs to chord 2
      auto extra=new Note(score.get()); extra->setPitch(67); extra->setTpcFromPitch(); extra->setTrack(0); extra->setParent(second); extra->setVeloType(Note::ValueType::USER_VAL); extra->setVeloOffset(90); second->add(extra);
      tremolo->applyEnvelopePreset(1); auto c=envelopeAttacks(score.get(),extra); QCOMPARE(c.size(),4); for(auto e:c) QCOMPARE(e.second.velo(),77);
      second->remove(extra); delete extra;
      first->setPlayEventType(PlayEventType::User); NoteEvent custom(0,125,250); custom.setVelocityFactor(.01); custom.setVelocitySourceIndex(0); n1->playEvents().clear(); n1->playEvents().append(custom);
      a=envelopeAttacks(score.get(),n1); QCOMPARE(a.size(),1); QCOMPARE(a.front().second.velo(),80); QCOMPARE(a.front().first,60);
      QCOMPARE(n1->playEvents().front().velocityFactor(),.01); // derived factors are ignored on user events
      }
void TestMidi::envelopeTrillAndPersistence()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR+"envelope.mscx")); QVERIFY(score);
      auto first=envelopeChord(score.get(),0); envelopeAbsolute(first,80); auto note=first->downNote();
      auto trill=new Trill(score.get()); trill->setTrack(0); trill->setTick(Fraction(0,1)); trill->setTicks(Fraction(1,4));
      score->startCmd(); score->undoAddElement(trill); score->endCmd();
      auto legacy=envelopeAttacks(score.get(),note); QVERIFY(legacy.size()>1); for(auto e:legacy) QCOMPARE(e.second.velo(),80);
      trill->applyEnvelopePreset(1); auto soft=envelopeAttacks(score.get(),note); QCOMPARE(soft.size(),legacy.size()); bool auxiliary=false;
      for(auto e:soft) { bool aux=e.second.pitch()!=60; auxiliary|=aux; QCOMPARE(e.second.velo(),aux ? 68 : 80); } QVERIFY(auxiliary);
      trill->applyEnvelopePreset(2); trill->setProperty(Pid::ENVELOPE_ALTERNATE,0.0); auto crescendo=envelopeAttacks(score.get(),note);
      QCOMPARE(crescendo.front().second.velo(),64); QCOMPARE(crescendo.back().second.velo(),80);
      score->startCmd(); trill->undoChangeProperty(Pid::ENVELOPE_CURVE,2.5); trill->undoChangeProperty(Pid::ENVELOPE_END,95.0); score->endCmd();
      score->undoRedo(true,&ed); QCOMPARE(trill->getProperty(Pid::ENVELOPE_CURVE).toDouble(),1.0); QCOMPARE(trill->getProperty(Pid::ENVELOPE_END).toDouble(),100.0);
      score->undoRedo(false,&ed); std::unique_ptr<Trill> copy(trill->clone()); QCOMPARE(copy->getProperty(Pid::ENVELOPE_CURVE).toDouble(),2.5);
      QTemporaryDir dir; QVERIFY(dir.isValid());
      for(const auto& ext:{"mscx","mscz"}) {
            QString file=dir.path()+"/envelope."+ext;
            if(QString(ext)=="mscz") { QFileInfo info(file); QVERIFY(score->saveCompressedFile(info,false,false)); } else QVERIFY(saveScore(score.get(),file));
            std::unique_ptr<MasterScore> restored(readCreatedScore(file)); QVERIFY(restored);
            Trill* r=nullptr; for(const auto& sp:restored->spannerMap().map()) if(sp.second->isTrill()) r=toTrill(sp.second);
            QVERIFY(r); for(auto pid:PlaybackEnvelope::properties()) QCOMPARE(r->getProperty(pid),trill->getProperty(pid));
            auto restoredEvents=envelopeAttacks(restored.get(),envelopeChord(restored.get(),0)->downNote()); auto original=envelopeAttacks(score.get(),note);
            QCOMPARE(restoredEvents.size(),original.size()); for(int i=0;i<original.size();++i) { QCOMPARE(restoredEvents[i].first,original[i].first); QCOMPARE(restoredEvents[i].second.velo(),original[i].second.velo()); }
            }
      score->startCmd(); score->undoRemoveElement(trill); score->select(note,SelectType::SINGLE,0); score->addArticulation(SymId::ornamentTrill); score->endCmd();
      auto articulation=first->articulations().front(); QCOMPARE(articulation->getProperty(Pid::ENVELOPE_MODE).toInt(),1);
      soft=envelopeAttacks(score.get(),note); QVERIFY(soft.size()>1); for(auto e:soft) QCOMPARE(e.second.velo(),e.second.pitch()==60 ? 80 : 68);
      }
void TestMidi::envelopeLinkedParts()
      {
      std::unique_ptr<MasterScore> score(readScore(DIR+"envelope.mscx")); QVERIFY(score);
      score->startCmd(); score->insertMeasure(ElementType::VBOX,score->first()); score->endCmd();
      auto part=new Score(score.get()); auto excerpt=new Excerpt(score.get()); excerpt->setPartScore(part); excerpt->setParts({score->parts().front()}); excerpt->setTitle("Piano");
      Excerpt::createExcerpt(excerpt); score->excerpts().append(excerpt); part->doLayout();
      auto t=addEnvelopeTremolo(score.get(),envelopeChord(score.get(),0),TremoloType::R16,1);
      auto linked=toChord(part->firstMeasure()->first(SegmentType::ChordRest)->element(0))->tremolo(); QVERIFY(linked);
      part->startCmd(); linked->undoChangeProperty(Pid::ENVELOPE_MODE,4); linked->undoChangeProperty(Pid::ENVELOPE_START,65.0); linked->undoChangeProperty(Pid::ENVELOPE_END,123.0); linked->undoChangeProperty(Pid::ENVELOPE_CURVE,3.0); linked->undoChangeProperty(Pid::ENVELOPE_ALTERNATE,-12.0); part->endCmd();
      for(auto pid:PlaybackEnvelope::properties()) QCOMPARE(t->getProperty(pid),linked->getProperty(pid));
      score->undoRedo(true,&ed); QCOMPARE(t->getProperty(Pid::ENVELOPE_MODE).toInt(),1); QCOMPARE(t->getProperty(Pid::ENVELOPE_START).toDouble(),100.0);
      score->undoRedo(false,&ed); std::unique_ptr<Tremolo> copy(t->clone()); for(auto pid:PlaybackEnvelope::properties()) QCOMPARE(copy->getProperty(pid),t->getProperty(pid));
      QTemporaryDir dir; QVERIFY(saveScore(score.get(),dir.path()+"/tremolo.mscx")); std::unique_ptr<MasterScore> restored(readCreatedScore(dir.path()+"/tremolo.mscx")); QVERIFY(restored);
      auto r=envelopeChord(restored.get(),0)->tremolo(); QVERIFY(r); for(auto pid:PlaybackEnvelope::properties()) QCOMPARE(r->getProperty(pid),t->getProperty(pid));
      }
void TestMidi::envelopeChunkAndDensity()
      {
      MCursor cursor; cursor.setTimeSig(Fraction(4,4)); cursor.createScore("envelope-density"); cursor.addPart("piano"); cursor.move(0,Fraction(0,1)); cursor.addTimeSig(Fraction(4,4));
      QVector<Ms::Chord*> chords;
      for(int i=0;i<128;++i) { auto chord=cursor.addChord(60,TDuration(TDuration::DurationType::V_WHOLE)); envelopeAbsolute(chord,80); chords.append(chord); }
      std::unique_ptr<MasterScore> score(cursor.score()); score->doLayout(); score->rebuildMidiMapping();
      for(auto chord:chords) addEnvelopeTremolo(score.get(),chord,TremoloType::R64,1);
      QElapsedTimer timer; timer.start(); auto events=envelopeAttacks(score.get()); const double ms=timer.nsecsElapsed()/1e6;
      QCOMPARE(events.size(),8192); qInfo("envelope benchmark: measures=128 attacks=%d render_ms=%.3f",events.size(),ms);
      QVERIFY2(ms<10000,"Preparing repeated attacks must remain practical");
      for(int i=1;i<chords.size();++i) envelopeTie(score.get(),chords[i-1]->downNote(),chords[i]->downNote());
      for(auto chord:chords) chord->tremolo()->applyEnvelopePreset(2);
      events=envelopeAttacks(score.get(),chords.front()->downNote()); QCOMPARE(events.size(),8192); QCOMPARE(events.front().second.velo(),64); QCOMPARE(events.back().second.velo(),80);
      MidiRenderer renderer(score.get()); renderer.setMinChunkSize(1); SynthesizerState state; MidiRenderer::Context context(state); context.metronome=false;
      const auto chunk=renderer.getChunkAt(30*1920); QCOMPARE(chunk.tick1(),0); QCOMPARE(chunk.tick2(),128*1920);
      EventMap chunkEvents; renderer.renderChunk(chunk,&chunkEvents,context); int attacks=0;
      for(const auto& e:chunkEvents) if(e.second.type()==ME_NOTEON && e.second.velo()>0) ++attacks; QCOMPARE(attacks,8192);
      }
