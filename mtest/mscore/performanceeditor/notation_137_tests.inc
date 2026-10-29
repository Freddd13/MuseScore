// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
namespace {
QByteArray denseSlur137Fixture()
{
    QByteArray xml("<museScore version=\"3.02\"><Score><Division>480</Division>"
        "<Part><Staff id=\"1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>"
        "<Staff id=\"2\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>"
        "<trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part>");
    const int pitches[] = {72,73,76,74,71,60,61,64,62,72,74,76,77,79,81,80};
    const int tpcs[] = {14,21,18,16,19,14,21,18,16,14,16,18,13,15,17,22};
    for (int staff=0; staff<2; ++staff) {
        xml += "<Staff id=\"" + QByteArray::number(staff+1) + "\">";
        for (int bar=0; bar<4; ++bar) {
            xml += "<Measure><voice>";
            if (!bar) xml += "<Clef><concertClefType>" + QByteArray(staff?"F":"G") +
                "</concertClefType><transposingClefType>" + QByteArray(staff?"F":"G") +
                "</transposingClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
            for (int n=0; n<16; ++n) {
                xml += "<Chord><durationType>16th</durationType>";
                if (!staff && n>=5 && n<=8) xml += "<staffMove>1</staffMove>";
                xml += "<Note><pitch>" + QByteArray::number(pitches[n]-(staff?24:0)) +
                    "</pitch><tpc>" + QByteArray::number(tpcs[n]) + "</tpc></Note></Chord>";
            }
            xml += "</voice></Measure>";
        }
        xml += "</Staff>";
    }
    return xml + "</Score></museScore>";
}
Slur* addDenseSlur137(MasterScore* score)
{
    auto first = score->firstSegment(SegmentType::ChordRest)->element(0);
    auto last = score->tick2segment(score->lastMeasure()->endTick()-Fraction::fromTicks(120),false,SegmentType::ChordRest)->element(0);
    auto line = new Slur(score);
    line->setTrack(0); line->setTrack2(0);
    line->setTick(toChord(first)->tick());
    line->setTicks(toChord(last)->tick()-line->tick());
    line->setStartElement(first); line->setEndElement(last);
    line->setProperty(Pid::FREE_SLUR_MODE,true);
    score->startCmd(); score->undoAddElement(line); score->endCmd();
    score->doLayout();
    return line;
}
}

void TestPerformanceEditor::freeSlurInteriorStaffDense()
{
    const auto path = _directory.filePath("dense-crossstaff-137.mscx");
    QFile fixture(path); QVERIFY(fixture.open(QIODevice::WriteOnly));
    fixture.write(denseSlur137Fixture()); fixture.close();
    std::unique_ptr<MasterScore> score(readCreatedScore(path)); QVERIFY(score);
    score->doLayout();
    auto line = addDenseSlur137(score.get());
    auto target = toChord(score->tick2segment(Fraction::fromTicks(720),false,SegmentType::ChordRest)->element(0));
    QCOMPARE(target->vStaffIdx(),1); QCOMPARE(line->startCR()->vStaffIdx(),0); QCOMPARE(line->endCR()->vStaffIdx(),0);
    const double at = double(target->tick().ticks())/line->ticks().ticks();
    score->startCmd(); line->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode({{at,{}, {},false}})); score->endCmd();
    auto segment = line->frontSegment();
    auto head = target->upNote()->bbox().translated(target->upNote()->pagePos());
    auto nodes = line->freeNodes();
    nodes[0].offset = (head.center()-segment->gripsPositions()[6])/score->spatium();
    score->startCmd(); line->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(nodes)); score->endCmd();
    QVERIFY(QLineF(segment->gripsPositions()[6],head.center()).length()<.001*score->spatium());
    const auto saved = line->getProperty(Pid::FREE_SLUR_NODES);
    score->startCmd(); line->undoChangeProperty(Pid::FREE_SLUR_AUTO,true); score->endCmd();
    qInfo("Interior cross-staff: automatic node movement %.3f spaces, reported collisions %d",
        QLineF(segment->gripsPositions()[6],head.center()).length()/score->spatium(),segment->freeCollisions());
    QVERIFY2(!head.adjusted(-.1*score->spatium(),-.1*score->spatium(),.1*score->spatium(),.1*score->spatium()).contains(segment->gripsPositions()[6]),
        "Automatic slur must consider notes that move to another staff inside the phrase");
    QCOMPARE(line->getProperty(Pid::FREE_SLUR_NODES),saved);

    // Isolate the other staff's real noteheads from the already-covered beams
    // and source-staff shapes. The moved chord retains its native layout.
    line->setProperty(Pid::FREE_SLUR_AUTO,false);
    for (auto s=score->firstSegment(SegmentType::ChordRest); s; s=s->next1(SegmentType::ChordRest))
        for (int track:{0,4}) if (auto element=s->element(track))
            if (element->isChord()) toChord(element)->setBeamMode(Beam::Mode::NONE);
    score->doLayout(); segment=line->frontSegment();
    head=target->upNote()->bbox().translated(target->upNote()->pagePos());
    nodes=line->freeNodes();
    nodes[0].offset+=(head.center()-segment->gripsPositions()[6])/score->spatium();
    line->setProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(nodes)); segment->computeBezier();
    QVERIFY(QLineF(segment->gripsPositions()[6],head.center()).length()<.001*score->spatium());
    for (auto s=score->firstMeasure()->first(); s; s=s->next1()) s->staffShape(0).clear();
    line->setProperty(Pid::FREE_SLUR_AUTO,true); segment->computeBezier();
    qInfo("Interior cross-staff noteheads only: automatic node movement %.3f spaces, reported collisions %d",
        QLineF(segment->gripsPositions()[6],head.center()).length()/score->spatium(),segment->freeCollisions());
    QVERIFY2(!head.contains(segment->gripsPositions()[6]),"The visited staff must contribute its actual notehead obstacles");
}

void TestPerformanceEditor::freeSlurDenseSystemsRoundTrip()
{
    auto main=Ms::mscore; QVERIFY(main); main->showPerformanceEditor(false);
    main->resize(1280,900); main->show();
    const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path());
    for (bool mixed:{false,true}) {
        const auto path=_directory.filePath(QString("dense-slur-%1-137.mscx").arg(mixed));
        QFile fixture(path); QVERIFY(fixture.open(QIODevice::WriteOnly));
        fixture.write(denseSlur137Fixture()); fixture.close();
        auto score=main->readScore(path); QVERIFY(score); main->setCurrentScoreView(main->appendScore(score));
        auto split=new LayoutBreak(score); split->setLayoutBreakType(LayoutBreak::Type::LINE);
        split->setParent(score->firstMeasure()->nextMeasure());
        score->startCmd(); score->undoAddElement(split); score->endCmd();
        auto line=addDenseSlur137(score); const auto midi=freeSlurMidi(score);
        QVector<FreeSlurNode> nodes{{.21,{0,-.5},{},false,mixed?-1:0,0},{.71,{0,.5},{},false,mixed?1:0,mixed?1:0}};
        score->startCmd(); line->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(nodes));
        line->undoChangeProperty(Pid::FREE_SLUR_AUTO,true);
        line->undoChangeProperty(Pid::FREE_SLUR_START_SIDE,mixed?-1:0);
        line->undoChangeProperty(Pid::FREE_SLUR_END_SIDE,mixed?1:0); score->endCmd(); score->doLayout();
        QVERIFY(line->spannerSegments().size()>1); const auto saved=line->getProperty(Pid::FREE_SLUR_NODES);
        QVector<QVector<FreeSlur::CurvePoint>> shapes;
        for (auto native:line->spannerSegments()) {
            auto segment=toSlurSegment(native); const auto points=segment->freeCurvePoints(); QVERIFY(points.size()>=2);
            shapes.append(points);
            for (int n=0;n<points.size();++n) {
                QVERIFY(std::isfinite(points[n].p.x())); QVERIFY(std::isfinite(points[n].p.y()));
                if(n) { QVERIFY(points[n].t>points[n-1].t); QVERIFY(points[n].p.x()>=points[n-1].p.x()-.1*score->spatium()); }
                if(n&&n<points.size()-1) {
                    const double delta=qMin(points[n].t-points[n-1].t,points[n+1].t-points[n].t)*1e-6;
                    const auto a=FreeSlur::curvePoint(points,points[n].t-delta).d;
                    const auto b=FreeSlur::curvePoint(points,points[n].t+delta).d;
                    QVERIFY(QLineF(a,b).length()<.05*score->spatium());
                }
            }
        }
        const int undo=score->undoStack()->getCurIdx(); QElapsedTimer timer; timer.start();
        for(int n=0;n<12;++n)score->doLayout();
        qInfo("Dense multi-system slur, mixed=%d: %d segments, 12 layouts %.3f ms",mixed,int(shapes.size()),timer.nsecsElapsed()/1e6);
        QCOMPARE(score->undoStack()->getCurIdx(),undo); QCOMPARE(line->getProperty(Pid::FREE_SLUR_NODES),saved);
        const auto file=output+QString("/slur-dense-%1-137.mscz").arg(mixed);
        QFileInfo info(file); QVERIFY(score->saveCompressedFile(info,false,false));
        std::unique_ptr<MasterScore> read(main->readScore(file)); QVERIFY(read); read->doLayout();
        Slur* found=nullptr;for(auto pair:read->spannerMap().map())if(pair.second->isSlur())found=toSlur(pair.second);
        QVERIFY(found); QCOMPARE(found->getProperty(Pid::FREE_SLUR_NODES),saved);
        QCOMPARE(found->spannerSegments().size(),line->spannerSegments().size());
        for(int i=0;i<shapes.size();++i) {
            const auto now=toSlurSegment(found->spannerSegments()[i])->freeCurvePoints();
            QCOMPARE(now.size(),shapes[i].size());
            for(int n=0;n<now.size();++n)QVERIFY(QLineF(now[n].p,shapes[i][n].p).length()<.01*score->spatium());
        }
        QCOMPARE(freeSlurMidi(read.get()),midi);
        auto view=main->currentScoreView(); view->adjustCanvasPosition(line->startCR(),false); QTest::qWait(50);
        auto system=line->frontSegment()->system();
        const auto area=system->canvasBoundingRect().united(line->frontSegment()->canvasBoundingRect());
        const auto rect=view->toPhysical(area).adjusted(-15,-15,15,15).intersected(view->rect());
        QVERIFY(view->grab(rect).save(output+QString("/slur-dense-%1-137.png").arg(mixed)));
        QVERIFY(main->savePdf(score,output+QString("/slur-dense-%1-137.pdf").arg(mixed)));
    }
}

void TestPerformanceEditor::freeSlurWideStaffRouting()
{
    using FreeSlur::CurvePoint;
    const QVector<CurvePoint> line{{0,{0,0},{220,0},true},{1,{220,0},{220,0},true}};
    const QVector<QRectF> heads{{80,70,60,12}};
    const auto route=FreeSlur::avoid(line,heads,true,10,{{0,1,1}},true);
    qInfo("Wide staff route: center %.3f spaces, collisions %d, points %d",
        FreeSlur::curvePosition(route.points,.5).y()/10,route.collisions,route.points.size());
    QCOMPARE(route.collisions,0);
    QVERIFY(FreeSlur::curvePosition(route.points,.5).y()>82);
    QCOMPARE(route.points.front().p,line.front().p); QCOMPARE(route.points.back().p,line.back().p);
    QCOMPARE(route.points.front().d,line.front().d); QCOMPARE(route.points.back().d,line.back().d);
    for(int n=0;n<=128;++n)QVERIFY(FreeSlur::curvePosition(route.points,n/128.).y()<240.001);
}

void TestPerformanceEditor::notationInspectorSavedValues137()
{
    auto main=Ms::mscore; QVERIFY(main); main->resize(1280,900); main->show(); main->showPerformanceEditor(false);
    const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path());
    for(int kind=0;kind<5;++kind) {
        auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/midi/timed-arpeggio.mscx"); QVERIFY(score);
        main->setCurrentScoreView(main->appendScore(score));
        auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(0));
        Element* owner=nullptr; const char* field=nullptr; const char* name=nullptr; QList<Pid> properties;
        score->startCmd();
        if(kind==0) {
            auto a=new Arpeggio(score); a->setTrack(0); a->setParent(chord); score->undoAddElement(a); owner=a;
            field="arpIntervalMs";name="arpeggio";properties={Pid::ARP_TIMING_MODE,Pid::ARP_INTERVAL_MS,Pid::ARP_OFFSET_MS};
        } else if(kind==1) {
            score->setGraceNote(chord,62,NoteType::APPOGGIATURA,DIVISION/2); owner=chord;
            properties={Pid::GRACE_PLAY_MODE,Pid::GRACE_DURATION_MODE,Pid::GRACE_DURATION};field="graceDuration";name="grace";
        } else if(kind==2) {
            score->select(chord->downNote(),SelectType::SINGLE); score->addArticulation(SymId::articAccentAbove);
            owner=chord->articulations().front();properties={Pid::ARTIC_VELOCITY_MODE,Pid::ARTIC_VELOCITY_PERCENT};
            field="articVelocityPercent";name="accent";
        } else if(kind==3) {
            auto tremolo=new Tremolo(score); tremolo->setTremoloType(TremoloType::R16); tremolo->setTrack(0);
            tremolo->setParent(chord);tremolo->setChords(chord,nullptr); score->undoAddElement(tremolo);owner=tremolo;
            for(auto id:PlaybackEnvelope::properties())properties.append(id);field="envelopeEnd";name="envelope";
        } else {
            auto trill=new Trill(score);trill->setTrack(0);trill->setTick(Fraction(0,1));trill->setTicks(Fraction(1,4));score->undoAddElement(trill);owner=trill;
            for(auto id:PlaybackEnvelope::properties())properties.append(id);field="envelopeEnd";name="trill";
        }
        score->endCmd();
        auto selected=kind==1?static_cast<Element*>(chord->graceNotes().front()->downNote()):kind==4?static_cast<Trill*>(owner)->frontSegment():owner;
        score->select(selected,SelectType::SINGLE);getAction("inspector")->setChecked(true);main->cmd(getAction("inspector"));QTest::qWait(40);
        auto inspector=main->findChild<Inspector*>();QVERIFY(inspector);auto stack=inspector->findChild<QStackedWidget*>();QVERIFY(stack);auto panel=stack->currentWidget();QVERIFY(panel);
        if(kind<=1) {
            auto preset=panel->findChild<QPushButton*>(kind?"graceApplyPreset":"arpeggioApplyPreset");QVERIFY(preset);QTest::mouseClick(preset,Qt::LeftButton);
        } else if(kind>=3) {
            auto mode=panel->findChild<QComboBox*>("envelopeMode");QVERIFY(mode);QTest::mouseClick(mode,Qt::LeftButton);QTest::keyClick(mode,Qt::Key_Home);QTest::keyClick(mode,Qt::Key_Down);QTest::keyClick(mode,Qt::Key_Return);
        }
        QTRY_VERIFY(!main->performanceEditor()->hasPending());
        auto spin=panel->findChild<QDoubleSpinBox*>(field);QVERIFY(spin&&spin->isEnabled());
        const auto pid=kind==0?Pid::ARP_INTERVAL_MS:kind==1?Pid::GRACE_DURATION:kind==2?Pid::ARTIC_VELOCITY_PERCENT:Pid::ENVELOPE_END;
        const auto old=owner->getProperty(pid);const int undo=score->undoStack()->getCurIdx();
        spin->setFocus();spin->selectAll();QTest::keyClicks(spin,kind<2?"73":kind==2?"112":"84");QTest::keyClick(spin,Qt::Key_Return);
        QTRY_VERIFY(!main->performanceEditor()->hasPending());QCOMPARE(owner->getProperty(pid).toDouble(),kind<2?73.:kind==2?112.:84.);
        QCOMPARE(score->undoStack()->getCurIdx(),undo+1);const auto changed=owner->getProperty(pid);
        score->undoRedo(true,&ed);QCOMPARE(owner->getProperty(pid),old);score->undoRedo(false,&ed);QCOMPARE(owner->getProperty(pid),changed);
        const auto midi=freeSlurMidi(score);QList<QVariant> values;for(auto id:properties)values.append(owner->getProperty(id));
        const auto file=output+QString("/%1-saved-137.mscz").arg(name);QFileInfo info(file);QVERIFY(score->saveCompressedFile(info,false,false));
        std::unique_ptr<MasterScore> read(main->readScore(file));QVERIFY(read);read->doLayout();auto c=toChord(read->firstSegment(SegmentType::ChordRest)->element(0));
        Element* restored=kind==0?static_cast<Element*>(c->arpeggio()):kind==1?static_cast<Element*>(c):kind==2?static_cast<Element*>(c->articulations().front()):kind==3?static_cast<Element*>(c->tremolo()):nullptr;
        if(kind==4)for(auto pair:read->spannerMap().map())if(pair.second->isTrill())restored=pair.second;
        QVERIFY(restored);for(int n=0;n<properties.size();++n)QCOMPARE(restored->getProperty(properties[n]),values[n]);
        QCOMPARE(freeSlurMidi(read.get()),midi);
        score->select(selected,SelectType::SINGLE);main->cmd(getAction("inspector"));QTest::qWait(40);panel=stack->currentWidget();QVERIFY(panel);
        auto shown=panel->findChild<QDoubleSpinBox*>(field);QVERIFY(shown);
        QVERIFY(shown->parentWidget()->grab().save(output+QString("/%1-controls-137.png").arg(name)));
        qInfo("Inspector save/reopen: %s, actual MIDI events preserved",name);
    }
}
