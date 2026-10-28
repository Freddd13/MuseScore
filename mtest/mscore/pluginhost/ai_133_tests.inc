void TestPluginHost::aiConnectionServiceControls133() {
    QTemporaryDir state(_settings.path()+"/ai-state-XXXXXX"); QVERIFY(state.isValid());
    QSettings settings; const auto enabled=settings.value("workflow/aiEnabled"), automatic=settings.value("workflow/aiAutostart"), details=settings.value("workflow/aiShowProcesses");
    const auto oldState=qgetenv("MCP_MUSESCORE_STATE_DIR"), oldCodex=qgetenv("CODEX_HOME");
    auto restore=qScopeGuard([&]{shutdownBuiltinAI();qputenv("MCP_MUSESCORE_STATE_DIR",oldState);qputenv("CODEX_HOME",oldCodex);for(const auto& pair:QList<QPair<QString,QVariant>>{{"workflow/aiEnabled",enabled},{"workflow/aiAutostart",automatic},{"workflow/aiShowProcesses",details}})if(pair.second.isValid())settings.setValue(pair.first,pair.second);else settings.remove(pair.first);});
    shutdownBuiltinAI(); qputenv("MCP_MUSESCORE_STATE_DIR",state.path().toUtf8()); qputenv("CODEX_HOME",(state.path()+"/codex").toUtf8());
    settings.setValue("workflow/aiEnabled",true); initializeBuiltinAI();
    QTRY_VERIFY_WITH_TIMEOUT(!QDir(state.path()).entryList({"instance-*.json"},QDir::Files).isEmpty(),10000);
    showAIConnection(); auto box=mscore->findChild<QDialog*>("kumoAIConnection"); QVERIFY(box);
    auto toggle=box->findChild<QPushButton*>("aiReadServiceToggle");auto autoCheck=box->findChild<QCheckBox*>("aiReadAutostart");auto more=box->findChild<QToolButton*>("aiSessionDetailsToggle");
    QVERIFY(toggle&&autoCheck&&more); QTRY_VERIFY_WITH_TIMEOUT(toggle->isEnabled(),15000);
    QTest::mouseClick(toggle,Qt::LeftButton);QTRY_VERIFY(QDir(state.path()).entryList({"instance-*.json"},QDir::Files).isEmpty());
    QVERIFY(!settings.value("workflow/aiEnabled").toBool()); box->close();showAIConnection();
    QTest::qWait(100);QVERIFY(QDir(state.path()).entryList({"instance-*.json"},QDir::Files).isEmpty());initializeBuiltinAI();QVERIFY(QDir(state.path()).entryList({"instance-*.json"},QDir::Files).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(toggle->isEnabled(),15000);QTest::mouseClick(toggle,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!QDir(state.path()).entryList({"instance-*.json"},QDir::Files).isEmpty(),10000);
    const bool before=autoCheck->isChecked();QTest::mouseClick(autoCheck,Qt::LeftButton,Qt::NoModifier,QPoint(8,autoCheck->height()/2));QCOMPARE(settings.value("workflow/aiAutostart").toBool(),!before);
    if(!more->isChecked())QTest::mouseClick(more,Qt::LeftButton);
    box->resize(680,640);QTest::qWait(100);auto status=box->findChild<QLabel*>("aiConnectionStatus");QVERIFY(status&&!status->text().isEmpty());
    QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(box->grab().save(out.filePath("ai-service-133.png")));box->close();
}

void TestPluginHost::aiConnectionCodexProcessControls133() {
    QTemporaryDir state(_settings.path()+"/ai-process-XXXXXX"); QVERIFY(state.isValid());
    const auto oldState=qgetenv("MCP_MUSESCORE_STATE_DIR"),oldCodex=qgetenv("CODEX_HOME");
    auto restore=qScopeGuard([&]{qputenv("MCP_MUSESCORE_STATE_DIR",oldState);qputenv("CODEX_HOME",oldCodex);});
    qputenv("MCP_MUSESCORE_STATE_DIR",state.path().toUtf8());qputenv("CODEX_HOME",(state.path()+"/codex").toUtf8());
    showAIConnection();auto box=mscore->findChild<QDialog*>("kumoAIConnection");QVERIFY(box);
    auto configure=box->findChild<QPushButton*>("aiConfigureCodex");auto reload=box->findChild<QPushButton*>("aiRefreshSessions");auto status=box->findChild<QLabel*>("aiConnectionStatus");
    QVERIFY(configure&&reload&&status);QTRY_VERIFY_WITH_TIMEOUT(configure->isEnabled(),15000);QTest::mouseClick(configure,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QString::fromUtf8("已配置")),15000);
    auto check=box->findChild<QCheckBox*>("aiCodexEnabled");QVERIFY(check&&check->isChecked());
    const auto config=state.path()+"/codex/config.toml";
    QTest::mouseClick(check,Qt::LeftButton,Qt::NoModifier,QPoint(8,check->height()/2));QTRY_VERIFY_WITH_TIMEOUT(check->isEnabled(),15000);
    QFile file(config);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.readAll().contains("enabled = false"));file.close();
    QTest::mouseClick(check,Qt::LeftButton,Qt::NoModifier,QPoint(8,check->height()/2));QTRY_VERIFY_WITH_TIMEOUT(check->isEnabled(),15000);
    QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.readAll().contains("enabled = true"));file.close();
    QProcess child;child.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
#ifdef Q_OS_WIN
    child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a){a->flags|=0x08000000;});
#endif
    auto childCleanup=qScopeGuard([&]{if(child.state()!=QProcess::NotRunning){child.kill();child.waitForFinished(5000);}});
    const QDir mcpRoot(QDir(QCoreApplication::applicationDirPath()).filePath("tools/mcp"));QString exe=mcpRoot.filePath("MuseScoreMCP.exe");QFile current(mcpRoot.filePath("current.json"));
    if(current.open(QIODevice::ReadOnly)){const auto directory=QJsonDocument::fromJson(current.readAll()).object()["directory"].toString();if(!directory.isEmpty())exe=mcpRoot.filePath(directory+"/MuseScoreMCP.exe");}
    child.start(exe,{"--stdio","--application",QCoreApplication::applicationFilePath()});QVERIFY(child.waitForStarted(5000));const auto pid=child.processId();
    child.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"native-check\",\"version\":\"1\"}}}\n");
    QByteArray response;QTRY_VERIFY_WITH_TIMEOUT((response+=child.readAllStandardOutput()).contains("serverInfo"),15000);
    child.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
    QTest::mouseClick(reload,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(reload->isEnabled(),15000);
    auto tree=box->findChild<QTreeWidget*>("aiMcpSessions");QVERIFY(tree);QTreeWidgetItem* row=nullptr;
    for(int i=0;i<tree->topLevelItemCount();++i)if(tree->topLevelItem(i)->data(0,Qt::UserRole).toString().startsWith(QString::number(pid)+":"))row=tree->topLevelItem(i);
    QVERIFY(row);auto more=box->findChild<QToolButton*>("aiSessionDetailsToggle");if(!more->isChecked())QTest::mouseClick(more,Qt::LeftButton);
    tree->setCurrentItem(row);QTest::qWait(50);box->resize(680,640);QTest::qWait(100);
    QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(box->grab().save(out.filePath("ai-processes-133.png")));
    auto end=box->findChild<QPushButton*>("aiEndSession");QVERIFY(end&&end->isEnabled());auto scroll=box->findChild<QScrollArea*>("aiConnectionScroll");QVERIFY(scroll);scroll->ensureWidgetVisible(end);
    QTest::mouseClick(end,Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(child.state(),QProcess::NotRunning,15000);
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QString::fromUtf8("已结束所选")),15000);QVERIFY(!end->isEnabled());box->close();
}
