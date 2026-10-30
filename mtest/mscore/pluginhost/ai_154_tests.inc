// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
void TestPluginHost::aiConnectionRuntimeVersions154()
{
    QTemporaryDir state(_settings.path()+"/ai-versions-XXXXXX"); QVERIFY(state.isValid());
    const auto oldState=qgetenv("MCP_MUSESCORE_STATE_DIR"), oldCodex=qgetenv("CODEX_HOME");
    auto restore=qScopeGuard([&]{shutdownBuiltinAI();qputenv("MCP_MUSESCORE_STATE_DIR",oldState);qputenv("CODEX_HOME",oldCodex);});
    shutdownBuiltinAI();qputenv("MCP_MUSESCORE_STATE_DIR",state.path().toUtf8());qputenv("CODEX_HOME",(state.path()+"/codex").toUtf8());
    auto main=mscore;auto score=identityScore140("版本检查");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    main->resize(1280,900);main->show();QSettings().setValue("workflow/aiEnabled",true);initializeBuiltinAI();
    const QDir root(QDir(QCoreApplication::applicationDirPath()).filePath("tools/mcp"));
    QFile current(root.filePath("current.json"));QVERIFY(current.open(QIODevice::ReadOnly));
    const auto directory=QJsonDocument::fromJson(current.readAll()).object()["directory"].toString();QVERIFY(!directory.isEmpty());
    QProcess oldClient,newClient;
    auto cleanup=qScopeGuard([&]{for(auto process:{&oldClient,&newClient})if(process->state()!=QProcess::NotRunning){process->closeWriteChannel();if(!process->waitForFinished(4000)){process->kill();process->waitForFinished(4000);}}});
    const auto initialize=QByteArray("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"runtime-check\",\"version\":\"1\"}}}\n");
    for(auto process:{&oldClient,&newClient}){
        process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
#ifdef Q_OS_WIN
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a){a->flags|=0x08000000;});
#endif
        process->start(root.filePath(process==&oldClient?"MuseScoreMCP.exe":directory+"/MuseScoreMCP.exe"),{"--stdio","--application",QCoreApplication::applicationFilePath()});
        QVERIFY(process->waitForStarted(5000));process->write(initialize);
        QByteArray response;QTRY_VERIFY_WITH_TIMEOUT((response+=process->readAllStandardOutput()).contains("serverInfo"),15000);
        if(process==&newClient)QVERIFY(response.contains("2.7.2"));
        process->write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
    }
    showAIConnection();auto box=main->findChild<QDialog*>("kumoAIConnection");QVERIFY(box);box->resize(680,640);
    auto reload=box->findChild<QPushButton*>("aiRefreshSessions");auto status=box->findChild<QLabel*>("aiConnectionStatus");QVERIFY(reload&&status);
    QTRY_VERIFY_WITH_TIMEOUT(reload->isEnabled(),15000);QTest::mouseClick(reload,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("2.7.2")&&status->text().contains(QString::fromUtf8("旧版本连接")),15000);
    auto more=box->findChild<QToolButton*>("aiSessionDetailsToggle");QVERIFY(more);if(!more->isChecked())QTest::mouseClick(more,Qt::LeftButton);
    auto tree=box->findChild<QTreeWidget*>("aiMcpSessions");QVERIFY(tree);QCOMPARE(tree->topLevelItemCount(),2);
    bool oldFound=false,newFound=false;
    for(int i=0;i<tree->topLevelItemCount();++i){auto row=tree->topLevelItem(i);const auto token=row->data(0,Qt::UserRole).toString();
        if(token.startsWith(QString::number(oldClient.processId())+":")){oldFound=true;QCOMPARE(row->text(1),QString("2.3.0"));}
        if(token.startsWith(QString::number(newClient.processId())+":")){newFound=true;QCOMPARE(row->text(1),QString("2.7.2"));}
    }
    QVERIFY(oldFound&&newFound);
    auto check=box->findChild<QPushButton*>("aiCheckConnection");QVERIFY(check&&check->isEnabled());
    auto scroll=box->findChild<QScrollArea*>("aiConnectionScroll");QVERIFY(scroll);scroll->ensureWidgetVisible(check);QTest::mouseClick(check,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(check->isEnabled()&&status->text().contains(QString::fromUtf8("已连接")),15000);
    QVERIFY(status->text().contains("2.7.2"));QVERIFY(status->text().contains(QString::fromUtf8("旧版本连接")));
    QVERIFY(oldClient.state()!=QProcess::NotRunning&&newClient.state()!=QProcess::NotRunning);
    QTest::qWait(100);QVERIFY(box->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",state.path())).filePath("ai-versions-154.png")));
    box->close();
}
