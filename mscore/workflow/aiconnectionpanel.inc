// Included inside aiconnection.cpp's private namespace.
class ConnectionDialog : public QDialog {
  QLabel *status = nullptr, *connectionState = nullptr, *windowCaption = nullptr;
  QListWidget *windows = nullptr;
  QTreeWidget *sessions = nullptr;
  QPushButton *serviceToggle = nullptr, *endSession = nullptr, *useWindow = nullptr;
  QCheckBox *automatic = nullptr, *codexEnabled = nullptr;
  QToolButton *sessionMore = nullptr;
  QWidget *sessionDetails = nullptr;
  QList<QPushButton *> operations;
  QJsonObject lastCodex;
  QTimer refresh;
  bool busy = false;
  QString lastWindows;

  void refreshService() {
    connectionState->setText(bridge ? bridge->status() : tr("AI 读谱连接已停止。点击“开启读谱连接”后可继续使用。"));
    serviceToggle->setText(bridge ? tr("停止读谱连接") : tr("开启读谱连接"));
    setComponentToolIcon(serviceToggle, bridge ? "stop" : "play");
  }
  void refreshWindows() {
    const auto files = QDir(stateDirectory()).entryList({"instance-*.json"}, QDir::Files);
    QJsonArray values;
    for (const auto &file : files) {
      auto value = readJson(QDir(stateDirectory()).filePath(file));
      if (!value.isEmpty() && alive(qint64(value.value("pid").toDouble()))) values.append(value);
    }
    const QString selected = readJson(QDir(stateDirectory()).filePath("selection.json")).value("id").toString();
    const QString key = QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact)) + selected;
    if (key == lastWindows) return;
    lastWindows = key;
    const QString browsing = windows->currentItem() ? windows->currentItem()->data(Qt::UserRole).toString() : selected;
    QSignalBlocker block(windows);
    windows->clear();
    for (const auto &item : values) {
      const auto value = item.toObject();
      const auto identity = value.value("identity").toObject();
      const QString kind = !identity.value("hasScore").toBool() ? QString()
          : identity.value("isMaster").toBool() ? tr("总谱") : tr("分谱：%1").arg(identity.value("partName").toString());
      const QString detail = tr("%1 · 进程%2 · %3").arg(identity.value("fileName").toString().isEmpty() ? tr("尚未保存文件") : identity.value("fileName").toString())
          .arg(value.value("pid").toInt()).arg(value.value("version").toString());
      auto row = new QListWidgetItem(value.value("score").toString() + (kind.isEmpty() ? QString() : " · " + kind)
          + (identity.value("modified").toBool() ? tr(" · 有修改") : QString())
          + (value.value("id").toString() == selected ? tr(" · 本次使用") : QString()) + "\n" + detail, windows);
      row->setData(Qt::UserRole, value.value("id").toString());
      row->setData(Qt::UserRole + 1, value.value("application").toString());
      row->setToolTip(tr("谱面标题：%1\n作品信息：%2\n文件：%3\n程序：%4")
          .arg(identity.value("visibleTitle").toString(), identity.value("workTitle").toString(),
               identity.value("filePath").toString(), value.value("application").toString()));
      if (value.value("id").toString() == browsing) windows->setCurrentItem(row);
    }
    if (windows->currentRow() < 0 && windows->count() == 1) windows->setCurrentRow(0);
    windows->setVisible(!values.isEmpty());
    windows->setFixedHeight(qBound(48, values.size() * (windows->fontMetrics().height() * 2 + 10) + 4, 108));
    windowCaption->setText(values.isEmpty() ? tr("读谱连接准备好后，这里会显示可用的乐谱窗口。") : tr("连接哪个程序窗口（读取该窗口当前标签页）"));
    updateOperations();
  }
  void updateOperations() {
    for (auto button : operations) button->setEnabled(!busy);
    codexEnabled->setEnabled(!busy && lastCodex.value("configured").toBool());
    const auto item = sessions->currentItem();
    endSession->setEnabled(!busy && item && !item->data(0, Qt::UserRole + 1).toBool());
    useWindow->setEnabled(!busy && windows->currentItem());
  }
  void setCodexState(const QJsonObject &value) {
    if (!value.isEmpty()) lastCodex = value;
    QSignalBlocker block(codexEnabled);
    codexEnabled->setChecked(lastCodex.value("enabled").toBool());
    codexEnabled->setToolTip(lastCodex.value("configured").toBool()
      ? tr("控制 Codex 是否启动 MuseScore MCP。修改后在 Codex 重新连接；已打开的连接可在列表中单独结束。")
      : tr("先点击“配置 Codex”，再设置是否自动启动。"));
  }
  void updateSessions(const QJsonArray &values) {
    const auto current = sessions->currentItem();
    const auto token = current ? current->data(0, Qt::UserRole).toString() : QString();
    const int scroll = sessions->verticalScrollBar()->value();
    sessions->clear();
    for (const auto &item : values) {
      const auto value = item.toObject();
      const bool active = value.value("active").toInt() > 0;
      const QString phase = !value.value("known").toBool() ? tr("旧连接 · 活动状态待核对")
        : active ? tr("正在处理任务")
        : value.value("requests").toInt() > 0 ? tr("等待下一次请求") : tr("等待首次请求");
      const auto stamp = [](double seconds) { return QDateTime::fromMSecsSinceEpoch(qRound64(seconds * 1000)).toString("MM-dd HH:mm:ss"); };
      auto row = new QTreeWidgetItem(sessions, {tr("进程 %1").arg(value.value("pid").toInt()), stamp(value.value("started").toDouble()),
        value.value("lastRequest").toDouble() > 0 ? stamp(value.value("lastRequest").toDouble()) : tr("未记录"), phase});
      row->setData(0, Qt::UserRole, value.value("token").toString());
      row->setData(0, Qt::UserRole + 1, active);
      if (row->data(0, Qt::UserRole).toString() == token) sessions->setCurrentItem(row);
    }
    sessions->verticalScrollBar()->setValue(scroll);
    sessionMore->setText(tr("Codex 连接（%1）").arg(values.size()));
    sessions->setFixedHeight(qBound(72, sessions->header()->height() + (sessions->fontMetrics().height() + 8) * values.size() + 4, 168));
  }
  void run(const QString &mode, const QString &token = QString()) {
    if (busy) return;
    if (!QFileInfo::exists(mcpProgram())) {
      status->setText(tr("MCP 程序未找到，请安装完整程序包。\n%1").arg(mcpProgram()));
      setCodexState({}); return;
    }
    busy = true; updateOperations();
    status->setText(mode == "--sessions" ? tr("正在读取 Codex 连接…")
      : mode == "--register-codex" || mode == "--enable-codex" || mode == "--disable-codex" ? tr("正在保存 Codex 连接设置…")
      : mode == "--end-session" ? tr("正在结束所选连接…") : tr("正在检查程序与乐谱连接…"));
    auto process = new QProcess(this);
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
    auto timer = new QTimer(process); timer->setSingleShot(true); timer->start(30000);
    connect(timer, &QTimer::timeout, process, [process] { process->setProperty("timedOut", true); process->kill(); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
      if (error != QProcess::FailedToStart) return;
      busy = false; setCodexState({}); updateOperations();
      status->setText(tr("MCP 程序无法启动：%1").arg(process->errorString())); process->deleteLater();
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
      [this, process, mode](int, QProcess::ExitStatus) {
        busy = false;
        const auto result = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        if (result.value("ok").toBool()) {
          if (result.contains("codex")) setCodexState(result.value("codex").toObject());
          if (result.contains("sessions")) updateSessions(result.value("sessions").toArray());
          if (mode == "--sessions") status->setText(tr("已读取连接。结束所选项会断开该 Codex 会话的 MuseScore 工具。"));
          else status->setText(result.value("message").toString(tr("连接成功。Codex 可以读取当前乐谱。")));
        } else {
          setCodexState({});
          status->setText(result.value("error").toString(process->property("timedOut").toBool()
            ? tr("操作超时，请重试。") : tr("MCP 检查未完成，请确认程序包完整后重试。")));
        }
        updateOperations(); refreshWindows(); refreshService(); process->deleteLater();
      });
    const auto item = windows->currentItem();
    const QString target = item ? item->data(Qt::UserRole + 1).toString() : QCoreApplication::applicationFilePath();
    QStringList args{mode}; if (!token.isEmpty()) args << token;
    args << "--application" << target; process->start(mcpProgram(), args);
  }
  void showEvent(QShowEvent *event) override {
    QDialog::showEvent(event); refreshWindows(); refreshService(); refresh.start();
    QTimer::singleShot(0, this, [this] { if (isVisible() && !busy) run("--sessions"); });
  }
  void hideEvent(QHideEvent *event) override {
    refresh.stop(); QDialog::hideEvent(event);
  }
public:
  explicit ConnectionDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("AI 连接")); setObjectName("kumoAIConnection"); setModal(false); resize(680, 520);
    auto outer = new QVBoxLayout(this);
    auto scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setObjectName("aiConnectionScroll");
    outer->addWidget(scroll, 1); auto page = new QWidget; scroll->setWidget(page); auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 4, 0); layout->setSpacing(8);
    auto intro = new QLabel(tr("在这里控制 AI 读谱、Codex 启动和已打开的连接。")); intro->setWordWrap(true); layout->addWidget(intro);
    connectionState = new QLabel; connectionState->setObjectName("aiReadServiceStatus"); connectionState->setWordWrap(true); layout->addWidget(connectionState);
    auto row = new QHBoxLayout; layout->addLayout(row);
    serviceToggle = new QPushButton; serviceToggle->setObjectName("aiReadServiceToggle"); row->addWidget(serviceToggle);
    automatic = new QCheckBox(tr("启动软件时自动开启")); automatic->setObjectName("aiReadAutostart");
    automatic->setChecked(QSettings().value("workflow/aiAutostart", true).toBool()); row->addWidget(automatic); row->addStretch();
    connect(automatic, &QCheckBox::toggled, this, [](bool checked) { QSettings().setValue("workflow/aiAutostart", checked); });
    connect(serviceToggle, &QPushButton::clicked, this, [this] {
      const bool enable = !bridge; QSettings().setValue("workflow/aiEnabled", enable);
      if (enable) initializeBuiltinAI(); else shutdownBuiltinAI();
      lastWindows.clear(); refreshWindows(); refreshService();
      status->setText(enable ? tr("读谱连接已开启。") : tr("读谱连接已停止。Codex 的现有进程可在下方单独结束。"));
      QTimer::singleShot(250, this, [this] { refreshWindows(); });
    });
    windowCaption = new QLabel(tr("使用哪份乐谱")); windowCaption->setWordWrap(true); layout->addWidget(windowCaption);
    windows = new QListWidget; windows->setObjectName("aiConnectionWindows"); windows->setMinimumHeight(52); windows->setMaximumHeight(88); layout->addWidget(windows);
    row = new QHBoxLayout; layout->addLayout(row);
    auto use = useWindow = new QPushButton(tr("使用选中的窗口")); use->setObjectName("aiUseWindow"); setComponentToolIcon(use, "panel"); row->addWidget(use); row->addStretch();
    connect(windows, &QListWidget::itemSelectionChanged, this, [this] { updateOperations(); });
    connect(use, &QPushButton::clicked, this, [this] {
      auto item = windows->currentItem(); if (!item) return;
      if (saveJson(QDir(stateDirectory()).filePath("selection.json"), {{"id", item->data(Qt::UserRole).toString()}})) {
        status->setText(tr("已选择：%1。Codex 跟随该窗口的当前乐谱标签页。").arg(item->text().section('\n', 0, 0)));
        refreshWindows();
      }
    });
    row = new QHBoxLayout; layout->addLayout(row);
    auto configure = new QPushButton(tr("配置 Codex")); setComponentToolIcon(configure, "settings"); configure->setObjectName("aiConfigureCodex"); row->addWidget(configure);
    auto check = new QPushButton(tr("检查连接")); setComponentToolIcon(check, "refresh"); check->setObjectName("aiCheckConnection"); row->addWidget(check);
    auto copy = new QPushButton(tr("复制配置")); setComponentToolIcon(copy, "copy"); row->addWidget(copy); row->addStretch();
    connect(configure, &QPushButton::clicked, this, [this] { run("--register-codex"); });
    connect(check, &QPushButton::clicked, this, [this] { run("--diagnose"); });
    connect(copy, &QPushButton::clicked, this, [this] {
      const auto item = windows->currentItem();
      const auto target = item ? item->data(Qt::UserRole + 1).toString() : QCoreApplication::applicationFilePath();
      const auto args = QString::fromUtf8(QJsonDocument(QJsonArray{"--stdio", "--application", target}).toJson(QJsonDocument::Compact));
      QApplication::clipboard()->setText("[mcp_servers.musescore]\ncommand = " + quoted(mcpProgram()) + "\nargs = " + args + "\nenabled = true\nstartup_timeout_sec = 20\ntool_timeout_sec = 60\n");
      status->setText(tr("已复制。在 Codex 配置文件中加入这段内容，再重新连接。"));
    });
    codexEnabled = new QCheckBox(tr("允许 Codex 启动 MuseScore MCP")); codexEnabled->setObjectName("aiCodexEnabled"); codexEnabled->setEnabled(false); layout->addWidget(codexEnabled);
    connect(codexEnabled, &QCheckBox::clicked, this, [this](bool checked) { run(checked ? "--enable-codex" : "--disable-codex"); });
    row = new QHBoxLayout; layout->addLayout(row);
    sessionMore = new QToolButton; sessionMore->setObjectName("aiSessionDetailsToggle"); sessionMore->setText(tr("Codex 连接")); sessionMore->setCheckable(true);
    sessionMore->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); row->addWidget(sessionMore);
    auto reload = new QPushButton(tr("刷新连接列表")); reload->setObjectName("aiRefreshSessions"); setComponentToolIcon(reload, "refresh"); row->addWidget(reload); row->addStretch();
    sessionDetails = new QWidget; auto details = new QVBoxLayout(sessionDetails); details->setContentsMargins(0, 0, 0, 0); layout->addWidget(sessionDetails);
    sessions = new QTreeWidget; sessions->setObjectName("aiMcpSessions"); sessions->setRootIsDecorated(false); sessions->setHeaderLabels({tr("连接"), tr("启动时间"), tr("最近请求"), tr("状态")});
    sessions->setFixedHeight(72); sessions->header()->setSectionResizeMode(QHeaderView::ResizeToContents); sessions->header()->setStretchLastSection(true); details->addWidget(sessions);
    row = new QHBoxLayout; details->addLayout(row);
    endSession = new QPushButton(tr("结束所选连接")); endSession->setObjectName("aiEndSession"); setComponentToolIcon(endSession, "stop"); row->addWidget(endSession); row->addStretch();
    connect(sessions, &QTreeWidget::itemSelectionChanged, this, [this] { updateOperations(); });
    connect(endSession, &QPushButton::clicked, this, [this] { if (const auto item = sessions->currentItem()) run("--end-session", item->data(0, Qt::UserRole).toString()); });
    connect(reload, &QPushButton::clicked, this, [this] { run("--sessions"); });
    connect(sessionMore, &QToolButton::toggled, this, [this](bool checked) {
      sessionDetails->setVisible(checked); sessionMore->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow); QSettings().setValue("workflow/aiShowProcesses", checked);
    });
    sessionMore->setChecked(QSettings().value("workflow/aiShowProcesses", false).toBool());
    sessionDetails->setVisible(sessionMore->isChecked()); sessionMore->setArrowType(sessionMore->isChecked() ? Qt::DownArrow : Qt::RightArrow);
    auto note = new QLabel(tr("每个 Codex 会话可有自己的 MCP 进程。关闭自动启动后，在 Codex 重新连接使设置生效；已打开的连接可逐个结束。")); note->setWordWrap(true); layout->addWidget(note);
    auto path = new QLabel(QCoreApplication::applicationFilePath()); path->setTextInteractionFlags(Qt::TextSelectableByMouse); path->setWordWrap(true); layout->addWidget(path); layout->addStretch();
    status = new QLabel(tr("首次使用请点击“配置 Codex”，再到 Codex 重新连接。")); status->setObjectName("aiConnectionStatus"); status->setWordWrap(true); status->setTextInteractionFlags(Qt::TextSelectableByMouse); outer->addWidget(status);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Help | QDialogButtonBox::Close); outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::helpRequested, this, [] { showPersonalGuide("docs/AI_IMAGES_141_GUIDE.md"); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    operations = {configure, check, copy, use, reload, serviceToggle}; updateOperations(); refreshService();
    refresh.setInterval(1500);
    connect(&refresh, &QTimer::timeout, this, [this] { refreshWindows(); refreshService(); });
  }
};
