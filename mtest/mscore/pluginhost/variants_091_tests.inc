// Continue editing a complete-score writing after the normal Save As operation.
void TestPluginHost::legacyVariantsFollowSaveAs()
{
    auto main = Ms::mscore;
    auto source = main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
    QVERIFY(source);
    main->setCurrentScoreView(main->appendScore(source));
    main->resize(1280, 900); main->show();
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementVariants/ArrangementVariants_MS3.qml");
    QmlPlugin* panel = nullptr;
    for (auto window : QGuiApplication::allWindows())
        if (auto view = qobject_cast<QQuickView*>(window))
            if (auto root = qobject_cast<QmlPlugin*>(view->rootObject()))
                if (root->menuPath() == "Plugins.Arrangement Variants") panel = root;
    QVERIFY(panel);
    panel->setProperty("workspace", QUrl::fromLocalFile(_settings.filePath("save-as-091.json")).toString());
    QVERIFY(QMetaObject::invokeMethod(panel, "newGroup"));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("busy").toBool(), 6000);
    QTRY_VERIFY(main->currentScore() != source);
    auto writing = main->currentScore();
    auto entries = [&] {
        return panel->property("groups").value<QJSValue>().toVariant().toList().front().toMap().value("entries").toList();
    };
    const auto initialPath = QDir(_settings.path()).filePath(entries()[1].toMap().value("path").toString());
    PluginAPI::Score wrapped(writing); PluginAPI::ScoreObserver observer;
    observer.setScore(&wrapped);
    const auto session = observer.workflowStatus().value("scoreSession").toString();
    QVERIFY(!session.isEmpty());
    auto note = toChord(writing->findCR(Fraction::fromTicks(480), 0))->notes().front();
    writing->startCmd(); writing->undoChangePitch(note, 60, 14, 14); writing->endCmd();
    const QString moved = _settings.filePath(QString::fromUtf8("另存的A写法.mscz"));
    QVERIFY(main->saveAs(writing, false, moved, "mscz"));
    QCOMPARE(writing->masterScore()->fileInfo()->absoluteFilePath(), QFileInfo(moved).absoluteFilePath());
    QCOMPARE(observer.workflowStatus().value("scoreSession").toString(), session);
    QCOMPARE(QFileInfo(QDir(_settings.path()).filePath(entries()[1].toMap().value("path").toString())).canonicalFilePath(),
             QFileInfo(moved).canonicalFilePath());
    QFile index(_settings.filePath("save-as-091.json")); QVERIFY(index.open(QIODevice::ReadOnly));
    QVERIFY(index.readAll().contains(QString::fromUtf8("另存的A写法").toUtf8())); index.close();
    const auto copy = _settings.filePath("separate-copy.mscz");
    QVERIFY(main->saveAs(writing, true, copy, "mscz"));
    QCOMPARE(writing->masterScore()->fileInfo()->absoluteFilePath(), QFileInfo(moved).absoluteFilePath());
    writing->startCmd(); writing->undoChangePitch(note, 62, 16, 16); writing->endCmd();
    QVERIFY(writing->masterScore()->dirty());
    QVERIFY(QMetaObject::invokeMethod(panel, "switchTo", Q_ARG(QVariant, 0), Q_ARG(QVariant, false)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("busy").toBool(), 6000);
    QVERIFY(main->currentScore() != writing);
    std::unique_ptr<MasterScore> saved(main->readScore(moved));
    QVERIFY(saved);
    QCOMPARE(toChord(saved->findCR(Fraction::fromTicks(480), 0))->notes().front()->pitch(), 62);
    QVERIFY(QMetaObject::invokeMethod(panel, "switchTo", Q_ARG(QVariant, 1), Q_ARG(QVariant, false)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("busy").toBool(), 6000);
    QCOMPARE(main->currentScore(), writing);
    QCOMPARE(note->pitch(), 62);
    QVERIFY(!writing->masterScore()->dirty());
    auto previousFile = main->readScore(initialPath); QVERIFY(previousFile);
    main->setCurrentScoreView(main->appendScore(previousFile));
    QTRY_VERIFY(!panel->property("currentOwner").isValid() ||
                panel->property("currentOwner").isNull() ||
                panel->property("currentOwner").value<QJSValue>().isNull());
    QVERIFY(observer.workflowStatus().value("scoreSession").toString() !=
            workflowScoreSession(previousFile));
    QVERIFY(QMetaObject::invokeMethod(panel, "switchTo", Q_ARG(QVariant, 1), Q_ARG(QVariant, false)));
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("busy").toBool(), 6000);
    QCOMPARE(main->currentScore(), writing);
    QCOMPARE(note->pitch(), 62);
    const auto groups = panel->property("groups").value<QJSValue>().toVariant().toList();
    const auto entry = groups.front().toMap().value("entries").toList()[1].toMap();
    QCOMPARE(QFileInfo(QDir(_settings.path()).filePath(entry.value("path").toString())).canonicalFilePath(),
             QFileInfo(moved).canonicalFilePath());
    const QDir artifacts(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    if (auto view = qobject_cast<QQuickView*>(panel->window()))
        QVERIFY(view->grabWindow().save(artifacts.filePath("variants-save-as-091.png")));
}
