void TestPluginHost::chordHandlesStayAtMusicRange()
{
    auto main = Ms::mscore;
    auto score = main->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    main->resize(1200, 900);main->show();QTest::qWait(60);
    auto view = main->currentScoreView();PluginAPI::Score wrapped(score);
    PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    auto notes = observer.snapshot(480, 0, 4).value("notes").toList();QVERIFY(!notes.isEmpty());
    auto descriptor = notes.front().toMap();
    descriptor["chord"] = "B7/F#";descriptor["degree"] = QString::fromUtf8("IV⁴₃");
    descriptor["chordTick"] = 480;descriptor["chordUntil"] = 1920;
    descriptor["onTop"] = true;descriptor["avoidScoreContent"] = false;
    QVariantMap region{{"start",480},{"end",1920},{"track",0},{"offsetX",0.},{"offsetY",0.}};
    observer.setScorePreview({descriptor});observer.setChordEditRegion(region);QTest::qWait(50);
    auto handles = view->findChild<QWidget*>("harmonyScoreRangeEditor");QVERIFY(handles);
    QTRY_VERIFY(handles->isVisible());
    const auto start = handles->property("rangeStartHandle").toPointF();
    const auto end = handles->property("rangeEndHandle").toPointF();
    QVERIFY(start.x() < end.x());
    const auto marker = view->notePreviewMarkerBox(&observer, 480, 0);
    QVERIFY(!marker.isEmpty());
    descriptor["offsetX"] = 3.;descriptor["offsetY"] = 8.;
    region["offsetX"] = 3.;region["offsetY"] = 8.;
    observer.setScorePreview({descriptor});observer.setChordEditRegion(region);QTest::qWait(50);
    QVERIFY(view->notePreviewMarkerBox(&observer,480,0) != marker);
    QCOMPARE(handles->property("rangeStartHandle").toPointF(), start);
    QCOMPARE(handles->property("rangeEndHandle").toPointF(), end);
    const auto measure = score->firstMeasure();
    const auto barEnd = view->matrix().map(measure->canvasPos()+QPointF(measure->width(),0));
    QVERIFY(qAbs(end.x()-barEnd.x())<1.);
    view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()))
                          .filePath("harmony-independent-range-075.png"));
    observer.clearAllPreviews();
}

void TestPluginHost::harmonyDisplayAndHold()
{
    QFile fixture(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QString xml = QString::fromUtf8(fixture.readAll());
    xml.replace("</Clef>", "</Clef><KeySig><accidental>6</accidental></KeySig>");
    xml.replace("<pitch>62</pitch><tpc>16</tpc>", "<pitch>63</pitch><tpc>23</tpc>");
    xml.replace("<pitch>36</pitch><tpc>14</tpc>", "<pitch>54</pitch><tpc>20</tpc>");
    xml.replace("<pitch>52</pitch><tpc>18</tpc>", "<pitch>59</pitch><tpc>19</tpc>");
    xml.replace("<pitch>55</pitch><tpc>15</tpc>", "<pitch>63</pitch><tpc>23</tpc>");
    QFile output(_settings.filePath("harmony-b7-075.mscx"));
    QVERIFY(output.open(QIODevice::WriteOnly));output.write(xml.toUtf8());output.close();
    auto main = Ms::mscore;
    auto score = main->readScore(output.fileName());QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1200, 950);main->show();
    auto chord = score->tick2segment(Fraction::fromTicks(480), false, SegmentType::ChordRest)->element(0);
    QVERIFY(chord && chord->isChord());score->select(toChord(chord)->notes().front());
    showHarmonyPanel(true);
    QmlPlugin* root = nullptr;
    for (auto window : QGuiApplication::allWindows())
        if (auto view = qobject_cast<QQuickView*>(window))
            if (auto panel = qobject_cast<QmlPlugin*>(view->rootObject()))
                if (panel->menuPath() == "Plugins.Harmony Assistant")root = panel;
    QVERIFY(root);
    const auto option = [root](const QString& name, const QVariant& value) {
        return QMetaObject::invokeMethod(root, "changeSummary", Q_ARG(QVariant, name), Q_ARG(QVariant, value));
    };
    QVERIFY(option("resultMode", 1));QVERIFY(option("holdLastHarmony", false));
    QVERIFY(QMetaObject::invokeMethod(root, "refreshSelection"));
    QTRY_VERIFY_WITH_TIMEOUT(!root->property("analysisDirty").toBool(), 10000);
    QTRY_COMPARE(root->property("summaryChordText").toString(), QString::fromUtf8("B7/F♯"));
    QCOMPARE(root->property("summaryDegreeText").toString(), QString::fromUtf8("IV⁴₃"));
    QCOMPARE(root->property("chordText").toString(), root->property("summaryChordText").toString());
    QCOMPARE(root->property("degreeText").toString(), root->property("summaryDegreeText").toString());

    QVERIFY(option("holdLastHarmony", true));
    auto rest = score->tick2segment(Fraction::fromTicks(960), false, SegmentType::ChordRest)->element(0);
    QVERIFY(rest && rest->isRest());score->select(rest);main->selectionChanged(score->selection().state());
    QVERIFY(QMetaObject::invokeMethod(root, "refreshSelection"));
    QTRY_COMPARE(root->property("currentTick").toInt(), 960);
    QTRY_COMPARE(root->property("summaryChordText").toString(), QString::fromUtf8("B7/F♯"));
    QVERIFY(root->property("matchText").toString().contains(QString::fromUtf8("保留上一和弦")));
    QVERIFY(option("holdLastHarmony", false));QVERIFY(QMetaObject::invokeMethod(root, "updateTexts"));
    QTRY_COMPARE(root->property("summaryChordText").toString(), QString::fromUtf8("—"));
    QVERIFY(option("holdLastHarmony", true));QVERIFY(QMetaObject::invokeMethod(root, "analyze"));
    QVERIFY(QMetaObject::invokeMethod(root, "setPanelFloating", Q_ARG(bool, true)));
    QVERIFY(QMetaObject::invokeMethod(root, "resizePanel", Q_ARG(int, 380), Q_ARG(int, 850)));
    QTest::qWait(120);
    QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()))
                                            .filePath("harmony-b7-held-075.png")));
    showHarmonyPanel(false);
}
