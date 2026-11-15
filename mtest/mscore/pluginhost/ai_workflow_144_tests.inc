// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
void TestPluginHost::aiSuggestionState144() {
    auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    mscore->setCurrentScoreView(mscore->appendScore(score));guiScaling=1.0;MuseScore::updateUiStyleAndTheme();
    mscore->resize(1200,820);mscore->show();
    PluginAPI::Score wrapper(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapper);
    auto bass=toChord(score->findCR(Fraction(),4))->notes().front();
    auto submit=[&]{
        const auto state=observer.arrangementState({{"whole",true}});
        QVariantMap ref{{"id","0:4:0"},{"snapshotId",state["snapshotId"]}};
        QVariantMap option{{"title",QString::fromUtf8(QByteArray::fromHex("e5b7a6e6898be8bdbbe4b880e4ba9b"))},{"summary",QString::fromUtf8(QByteArray::fromHex("e4bf9de79599e6978be5be8befbc8ce4bd8ee99fb3e98787e794a8e58a9be5baa63533e38082"))},{"scope",state["scope"]},
            {"changes",QVariantList{QVariantMap{{"ref",ref},{"velocity",53}}}}};
        return observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",state["snapshotId"]},{"options",QVariantList{option}}});
    };
    auto result=submit();QVERIFY2(!result.contains("error"),qUtf8Printable(result["error"].toString()));
    auto panel=mscore->findChild<QWidget*>("arrangementSuggestionPanel");QVERIFY(panel);
    auto choices=panel->findChild<QTreeWidget*>("arrangementChoices");QVERIFY(choices);
    auto apply=panel->findChild<QPushButton*>("arrangement-apply");QVERIFY(apply);
    auto cancel=panel->findChild<QPushButton*>("localSuggestionCancel");QVERIFY(cancel);
    QTRY_VERIFY(apply->isEnabled());QVERIFY(!cancel->isEnabled());
    score->startCmd();bass->undoChangeProperty(Pid::VELO_OFFSET,3);score->endCmd();mscore->endCmd();
    QTRY_VERIFY(!apply->isEnabled());QCOMPARE(choices->topLevelItem(0)->text(1),QString::fromUtf8(QByteArray::fromHex("e8b0b1e5ad90e5b7b2e4bfaee694b9")));
    auto layer=mscore->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer");
    QVERIFY(layer);QVERIFY(!layer->isVisible());
    const auto folder=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QTest::qWait(80);QVERIFY(mscore->grab().save(folder.filePath("ai-stale-144.png")));
    result=submit();QVERIFY(!result.contains("error"));QTRY_VERIFY(apply->isEnabled());
    const int undo=score->undoStack()->getCurIdx();QTest::mouseClick(apply,Qt::LeftButton);
    QTRY_COMPARE(score->undoStack()->getCurIdx(),undo+1);QCOMPARE(bass->veloOffset(),53);
    QVERIFY(!apply->isEnabled());QCOMPARE(choices->topLevelItem(0)->text(1),QString::fromUtf8(QByteArray::fromHex("e5b7b2e98787e794a8")));
    const auto path=folder.filePath("ai-applied-144.mscz");QFileInfo info(path);QVERIFY(score->saveCompressedFile(info,false,false));
    std::unique_ptr<MasterScore> saved(mscore->readScore(path));QVERIFY(saved);
    QCOMPARE(toChord(saved->findCR(Fraction(),4))->notes().front()->veloOffset(),53);
    mscore->cmd(getAction("undo"));QCOMPARE(bass->veloOffset(),3);
    QTRY_COMPARE(choices->topLevelItem(0)->text(1),QString::fromUtf8(QByteArray::fromHex("e8b0b1e5ad90e5b7b2e4bfaee694b9")));
    QVERIFY(!arrangementSuggestions(score)["options"].toList().first().toMap()["used"].toBool());
    mscore->cmd(getAction("redo"));QCOMPARE(bass->veloOffset(),53);
    QTRY_COMPARE(choices->topLevelItem(0)->text(1),QString::fromUtf8(QByteArray::fromHex("e5b7b2e98787e794a8")));
    QTest::qWait(80);QVERIFY(mscore->grab().save(folder.filePath("ai-adopt-144.png")));
    showSuggestionPanel(false);
}

void TestPluginHost::nativeImageAndVelocityProposal168()
{
    auto xml = arrangementScore115({QVariantList{66, 0, 1920, 0, 20},
                                    QVariantList{49, 0, 1920, 4, 21}});
    xml.replace("</TimeSig>", "</TimeSig><KeySig><accidental>2</accidental></KeySig>");
    const QByteArray measureEnd = "</voice></Measure>";
    const QByteArray staffEnd = measureEnd + "</Staff>";
    const int upperStaff = xml.indexOf(staffEnd), lowerStaff = xml.lastIndexOf(staffEnd);
    QVERIFY(upperStaff >= 0 && lowerStaff > upperStaff);
    xml.insert(lowerStaff + measureEnd.size(),
               "<Measure><voice><Chord><durationType>whole</durationType>"
               "<Note><pitch>49</pitch><tpc>21</tpc></Note>"
               "<Note><pitch>54</pitch><tpc>20</tpc></Note></Chord></voice></Measure>");
    xml.insert(upperStaff + measureEnd.size(),
               "<Measure><voice><Chord><durationType>whole</durationType>"
               "<Note><pitch>78</pitch><tpc>20</tpc></Note></Chord></voice></Measure>");
    const auto path = _settings.filePath("native-image-velocity-168.mscx");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(xml), qint64(xml.size()));
    file.close();

    auto score = mscore->readScore(path);
    QVERIFY(score);
    mscore->setCurrentScoreView(mscore->appendScore(score));
    const auto close = qScopeGuard([&] {
        showSuggestionPanel(false);
        score->undoStack()->setClean();
        mscore->closeScore(score);
    });
    score->doLayout();
    const auto measure = score->firstMeasure()->nextMeasure();
    QVERIFY(measure);
    QVERIFY(measure->system() == score->firstMeasure()->system());
    const auto chordRest = score->findCR(measure->tick(), 4);
    QVERIFY(chordRest && chordRest->isChord());
    const auto chord = toChord(chordRest);
    QCOMPARE(int(chord->notes().size()), 2);
    auto edited = chord->downNote();
    auto locked = chord->upNote();
    for (auto note : {edited, locked}) {
        note->setVeloType(Note::ValueType::USER_VAL);
        note->setVeloOffset(67);
    }
    const auto editedId = arrangementNoteIdentity(edited);
    const auto lockedId = arrangementNoteIdentity(locked);

    PluginAPI::Score wrapped(score);
    PluginAPI::ScoreObserver observer;
    observer.setScore(&wrapped);
    const auto state = observer.arrangementState({{"firstMeasure", 2}, {"lastMeasure", 2}});
    QVERIFY2(!state.contains("error"), qUtf8Printable(state.value("error").toString()));
    QVariantMap request{{"snapshotId", state.value("snapshotId")}, {"scope", state.value("scope")}};
    auto image = observer.arrangementImage(request);
    QVERIFY2(!image.contains("error"), qUtf8Printable(image.value("error").toString()));
    const auto context = image.value("musicalContext").toMap();
    QVERIFY(!context.value("systemHeaderShown").toBool());
    const auto staves = context.value("staves").toList();
    QCOMPARE(staves.size(), 2);
    QCOMPARE(staves.last().toMap().value("key").toInt(), 2);
    QCOMPARE(staves.last().toMap().value("timeSignature").toMap().value("numerator").toInt(), 4);
    request["wholeSystem"] = true;
    const auto full = observer.arrangementImage(request);
    QVERIFY2(!full.contains("error"), qUtf8Printable(full.value("error").toString()));
    QCOMPARE(full.value("measures").toList().size(), 2);
    QImage png;
    QVERIFY(png.loadFromData(QByteArray::fromBase64(full.value("pngBase64").toString().toLatin1()), "PNG"));
    QVERIFY(png.width() > image.value("width").toInt());
    QVERIFY(full.value("musicalContext").toMap().value("systemHeaderShown").toBool());

    const auto result = observer.arrangementRequest("proposeVelocityAdjustment", {
        {"snapshotId", state.value("snapshotId")}, {"scope", state.value("scope")},
        {"kind", "background"}, {"roles", QVariantMap{{"4", "background"}}},
        {"lockedNotes", QVariantList{lockedId}}, {"preserveManual", false}, {"showPanel", false},
    });
    QVERIFY2(!result.contains("error"), qUtf8Printable(result.value("error").toString()));
    const auto options = result.value("options").toList();
    QCOMPARE(options.size(), 1);
    const auto option = options.first().toMap();
    const auto changes = option.value("velocityDifferences").toList();
    QCOMPARE(changes.size(), 1);
    const auto row = changes.first().toMap();
    QCOMPARE(row.value("ref").toMap().value("id").toString(), editedId);
    QCOMPARE(row.value("before").toInt(), 67);
    QCOMPARE(row.value("after").toInt(), 64);
    QCOMPARE(edited->veloOffset(), 67);
    QCOMPARE(locked->veloOffset(), 67);

    const auto undo = score->undoStack()->getCurIdx();
    const auto applied = observer.arrangementRequest("arrangementOption", {
        {"optionId", option.value("id")}, {"action", "apply"}, {"noteIds", QVariantList{editedId}},
    });
    QVERIFY2(!applied.contains("error"), qUtf8Printable(applied.value("error").toString()));
    QCOMPARE(score->undoStack()->getCurIdx(), undo + 1);
    QCOMPARE(edited->veloType(), Note::ValueType::USER_VAL);
    QCOMPARE(edited->veloOffset(), 64);
    QCOMPARE(locked->veloOffset(), 67);
    mscore->cmd(getAction("undo"));
    QCOMPARE(edited->veloOffset(), 67);
}
