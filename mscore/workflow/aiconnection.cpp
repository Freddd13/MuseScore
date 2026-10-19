// SPDX-License-Identifier: GPL-2.0-or-later
#include "aiconnection.h"
#include "featuregate.h"
#include "libmscore/score.h"
#include "mscore/musescore.h"
#include "mscore/plugin/qmlplugin.h"
#include "mscore/plugin/qmlpluginengine.h"
#include "mscore/workspace.h"
#include "personalbranding.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QQmlComponent>
#include <QQmlContext>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTextBrowser>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#endif

namespace Ms {
namespace {
QString stateDirectory() {
  const auto override = qEnvironmentVariable("MCP_MUSESCORE_STATE_DIR");
  return override.isEmpty() ? QDir(QStandardPaths::writableLocation(
                                       QStandardPaths::GenericDataLocation))
                                  .filePath("Freddd13/MuseScoreMCP")
                            : override;
}
bool saveJson(const QString &path, const QJsonObject &value) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  const auto data = QJsonDocument(value).toJson(QJsonDocument::Compact);
  return file.write(data) == data.size() && file.commit();
}
QString mcpProgram() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath("tools/mcp/MuseScoreMCP.exe");
}
bool alive(qint64 pid) {
  if (pid <= 0)
    return false;
#ifdef Q_OS_WIN
  HANDLE handle =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
  if (!handle)
    return false;
  DWORD code = 0;
  const bool running =
      GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
  CloseHandle(handle);
  return running;
#else
  return kill(pid, 0) == 0;
#endif
}
QJsonObject readJson(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}
QString quoted(const QString &value) {
  const auto json = QString::fromUtf8(
      QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
  return json.mid(1, json.size() - 2);
}
class Bridge : public QObject {
  QPointer<QmlPlugin> root;
  QPointer<MuseScore> owner;
  QmlPluginEngine *engine = nullptr;
  QQmlComponent *component = nullptr;
  bool starting = true, closed = false;
  QTimer update;
  QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), endpoint,
          previous;
  int port = 0;

public:
  QString error;
  explicit Bridge(QObject *parent) : QObject(parent), owner(mscore) {
    QTcpServer probe;
    for (int value = 8765; value < 8865; ++value)
      if (probe.listen(QHostAddress::LocalHost, quint16(value))) {
        port = value;
        probe.close();
        break;
      }
    if (!port) {
      starting = false;
      error = tr("本机连接端口正在使用，请关闭重复的连接程序后再试。");
      return;
    }
    engine = new QmlPluginEngine;
    engine->rootContext()->setContextProperty("workflowBridgePort", port);
    engine->rootContext()->setContextProperty("workflowBuiltinBridge", true);
    component = new QQmlComponent(engine, this);
    connect(component, &QQmlComponent::statusChanged, this,
            [this] { finishLoading(); });
    component->loadUrl(
        QUrl::fromLocalFile(mscoreGlobalShare + "workflow/MuseScoreBridge.qml"),
        QQmlComponent::PreferSynchronous);
    finishLoading();
  }
  void finishLoading() {
    if (closed || root || !component || component->isLoading())
      return;
    if (component->isError()) {
      starting = false;
      error = tr("AI 连接文件未能读取：%1").arg(component->errorString());
      qWarning().noquote() << error;
      return;
    }
    if (component->status() != QQmlComponent::Ready)
      return;
    root = qobject_cast<QmlPlugin *>(component->create());
    if (!root) {
      starting = false;
      error = tr("AI 连接未能建立：%1").arg(component->errorString());
      qWarning().noquote() << error;
      return;
    }
    starting = false;
    root->setParent(this);
    root->setRequiresScore(false);
    root->setFilePath(mscoreGlobalShare + "workflow");
    QObject::connect(owner->getPluginEngine(), &QmlPluginEngine::endCmd, root,
                     &QmlPlugin::endCmd);
    root->runPlugin();
    QDir().mkpath(stateDirectory());
    endpoint = QDir(stateDirectory()).filePath("instance-" + id + ".json");
    update.setInterval(1500);
    connect(&update, &QTimer::timeout, this, [this] { publish(); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this] { publish(); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { close(); });
    update.start();
    QTimer::singleShot(0, this, [this] { publish(); });
  }
  ~Bridge() override {
    close();
    delete engine;
  }
  void publish() {
    if (!root || !personalFeaturesEnabled())
      return;
    error = root->property("nativeError").toString();
    if (!error.isEmpty())
      return;
    const QString score = owner && owner->currentScore()
                              ? owner->currentScore()->name()
                              : tr("尚未打开乐谱");
    const bool active =
        QGuiApplication::applicationState() == Qt::ApplicationActive;
    const QString key = score + (active ? "1" : "0");
    if (key == previous)
      return;
    previous = key;
    saveJson(endpoint,
             {{"schema", 1},
              {"id", id},
              {"pid", QCoreApplication::applicationPid()},
              {"application", QCoreApplication::applicationFilePath()},
              {"host", "127.0.0.1"},
              {"port", port},
              {"version", KUMO_PERSONAL_VERSION},
              {"score", score},
              {"active", active},
              {"updated", QDateTime::currentMSecsSinceEpoch()}});
  }
  void close() {
    closed = true;
    update.stop();
    if (component) {
      delete component;
      component = nullptr;
    }
    if (root) {
      root->disposePanelForReload();
      delete root;
      root = nullptr;
    }
    // Only the discovery record created by this running process is removed.
    if (!endpoint.isEmpty())
      QFile::remove(endpoint);
  }
  QString status() const {
    if (starting)
      return tr("正在准备本机连接…");
    if (!error.isEmpty())
      return error;
    return root && root->property("nativeConnections").toInt() > 0
               ? tr("Codex 已连接，可以读取和编辑当前乐谱。")
               : tr("程序已就绪，等待 Codex 连接。");
  }
};
QPointer<Bridge> bridge;
QPointer<QDialog> dialog, guide;
QPointer<QTextBrowser> browser;

class ConnectionDialog : public QDialog {
  QLabel *status;
  QLabel *connectionState;
  QListWidget *windows;
  QList<QPushButton *> operations;
  QTimer refresh;
  bool busy = false;
  QString lastWindows;
  void refreshWindows() {
    const auto files =
        QDir(stateDirectory()).entryList({"instance-*.json"}, QDir::Files);
    QJsonArray values;
    for (const auto &file : files) {
      auto value = readJson(QDir(stateDirectory()).filePath(file));
      if (!value.isEmpty() && alive(qint64(value.value("pid").toDouble())))
        values.append(value);
    }
    const QString key =
        QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));
    if (key == lastWindows)
      return;
    lastWindows = key;
    const QString selected =
        readJson(QDir(stateDirectory()).filePath("selection.json"))
            .value("id")
            .toString();
    windows->clear();
    for (const auto &item : values) {
      const auto value = item.toObject();
      auto row = new QListWidgetItem(
          tr("%1 · %2").arg(value.value("score").toString(),
                            value.value("version").toString()),
          windows);
      row->setData(Qt::UserRole, value.value("id").toString());
      row->setData(Qt::UserRole + 1, value.value("application").toString());
      row->setToolTip(value.value("application").toString());
      if (value.value("id").toString() == selected)
        windows->setCurrentItem(row);
    }
    if (windows->currentRow() < 0 && windows->count() == 1)
      windows->setCurrentRow(0);
  }
  void run(const QString &mode) {
    if (busy)
      return;
    if (!QFileInfo::exists(mcpProgram())) {
      status->setText(
          tr("MCP 程序未找到，请安装完整程序包。\n%1").arg(mcpProgram()));
      return;
    }
    busy = true;
    for (auto button : operations)
      button->setEnabled(false);
    status->setText(mode == "--register-codex" ? tr("正在保存 Codex 连接配置…")
                                               : tr("正在检查程序与乐谱连接…"));
    auto process = new QProcess(this);
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
          args->flags |= 0x08000000;
        });
#endif
    auto timer = new QTimer(process);
    timer->setSingleShot(true);
    timer->start(30000);
    connect(timer, &QTimer::timeout, process, [process] { process->kill(); });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError e) {
              if (e == QProcess::FailedToStart) {
                busy = false;
                for (auto button : operations)
                  button->setEnabled(true);
                status->setText(
                    tr("MCP 程序无法启动：%1").arg(process->errorString()));
                process->deleteLater();
              }
            });
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, mode](int, QProcess::ExitStatus) {
              busy = false;
              for (auto button : operations)
                button->setEnabled(true);
              const auto result =
                  QJsonDocument::fromJson(process->readAllStandardOutput())
                      .object();
              if (result.value("ok").toBool())
                status->setText(mode == "--register-codex"
                                    ? result.value("message").toString()
                                    : tr("连接成功。Codex 可以读取当前乐谱。"));
              else
                status->setText(result.value("error").toString(
                    tr("检查未完成。请确认 kumo.ini "
                       "已开启扩展、程序包完整，并重新检查连接。")));
              process->deleteLater();
              refreshWindows();
            });
    const auto item = windows->currentItem();
    const QString target = item ? item->data(Qt::UserRole + 1).toString()
                                : QCoreApplication::applicationFilePath();
    process->start(mcpProgram(), {mode, "--application", target});
  }
  void showEvent(QShowEvent *e) override {
    QDialog::showEvent(e);
    refreshWindows();
    refresh.start();
  }
  void hideEvent(QHideEvent *e) override {
    refresh.stop();
    QDialog::hideEvent(e);
  }

public:
  explicit ConnectionDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("AI 连接"));
    setObjectName("kumoAIConnection");
    setModal(false);
    resize(580, 390);
    auto layout = new QVBoxLayout(this);
    auto intro =
        new QLabel(tr("连接一次后，在 Codex 中直接说明要分析或修改的谱段。"));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto path = new QLabel(QCoreApplication::applicationFilePath());
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setWordWrap(true);
    layout->addWidget(path);
    connectionState = new QLabel;
    connectionState->setWordWrap(true);
    layout->addWidget(connectionState);
    windows = new QListWidget;
    windows->setObjectName("aiConnectionWindows");
    layout->addWidget(windows, 1);
    auto use = new QPushButton(tr("使用选中的窗口"));
    layout->addWidget(use);
    connect(use, &QPushButton::clicked, this, [this] {
      auto item = windows->currentItem();
      if (!item)
        return;
      if (saveJson(QDir(stateDirectory()).filePath("selection.json"),
                   {{"id", item->data(Qt::UserRole).toString()}}))
        status->setText(tr("已选择，后续 Codex 操作使用这个窗口。"));
    });
    auto row = new QHBoxLayout;
    layout->addLayout(row);
    auto configure = new QPushButton(tr("配置 Codex"));
    configure->setObjectName("aiConfigureCodex");
    row->addWidget(configure);
    auto check = new QPushButton(tr("检查连接"));
    check->setObjectName("aiCheckConnection");
    row->addWidget(check);
    auto copy = new QPushButton(tr("复制配置"));
    row->addWidget(copy);
    operations = {configure, check, copy, use};
    connect(configure, &QPushButton::clicked, this,
            [this] { run("--register-codex"); });
    connect(check, &QPushButton::clicked, this, [this] { run("--diagnose"); });
    connect(copy, &QPushButton::clicked, this, [this] {
      const auto item = windows->currentItem();
      const QString target = item ? item->data(Qt::UserRole + 1).toString()
                                  : QCoreApplication::applicationFilePath();
      const auto args = QString::fromUtf8(
          QJsonDocument(QJsonArray{"--stdio", "--application", target})
              .toJson(QJsonDocument::Compact));
      QApplication::clipboard()->setText(
          "[mcp_servers.musescore]\ncommand = " + quoted(mcpProgram()) +
          "\nargs = " + args +
          "\nstartup_timeout_sec = 20\ntool_timeout_sec = 60\n");
      status->setText(
          tr("已复制。在 Codex 配置文件中加入这段内容，再重新连接。"));
    });
    connectionState->setText(bridge ? bridge->status()
                                    : personalFeatureStatus());
    status = new QLabel(
        tr("首次使用请点击“配置 Codex”，随后重新打开 Codex 对话并检查连接。"));
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status->setObjectName("aiConnectionStatus");
    layout->addWidget(status);
    auto buttons =
        new QDialogButtonBox(QDialogButtonBox::Help | QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::helpRequested, this,
            [] { showPersonalGuide("docs/WORKSPACE_063_GUIDE.md"); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    refresh.setInterval(1500);
    connect(&refresh, &QTimer::timeout, this, [this] {
      refreshWindows();
      connectionState->setText(bridge ? bridge->status()
                                      : personalFeatureStatus());
    });
  }
};
void loadGuide(const QString &page) {
  if (!browser)
    return;
  const QString base = QDir(mscoreGlobalShare + "personal").absolutePath();
  const QString path =
      QDir(base).absoluteFilePath(page.isEmpty() ? "USER_GUIDE.md" : page);
  if (!QDir::cleanPath(path).startsWith(base + "/"))
    return;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    browser->setPlainText(QObject::tr("说明文件未找到：%1").arg(path));
    return;
  }
  browser->document()->setBaseUrl(
      QUrl::fromLocalFile(QFileInfo(path).absolutePath() + "/"));
  browser->setMarkdown(QString::fromUtf8(file.readAll()));
}
} // namespace
QString builtinPanelPath(const QString &name) {
  return mscoreGlobalShare + "workflow/panels/" + name + "/" + name +
         ".view.qml";
}
void initializeBuiltinAI() {
  if (!personalFeaturesEnabled() || bridge || !mscore)
    return;
  bridge = new Bridge(qApp);
}
void shutdownBuiltinAI() {
  if (bridge) {
    delete bridge;
    bridge = nullptr;
  }
}
void showAIConnection() {
  if (!personalFeaturesEnabled() || !mscore)
    return;
  initializeBuiltinAI();
  if (!dialog)
    dialog = new ConnectionDialog(mscore);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}
void addAIConnectionAction(QMenu *menu) {
  if (!menu)
    return;
  auto owner = mscore;
  for (auto widget = menu->parentWidget(); !owner && widget;
       widget = widget->parentWidget())
    owner = qobject_cast<MuseScore *>(widget);
  if (!owner)
    return;
  // Saved workspaces replace their menus. Keep the shared action alive with
  // the main window so its menu entry and toolbar can reuse it afterwards.
  auto action = owner->findChild<QAction *>("kumo-ai-connection",
                                           Qt::FindDirectChildrenOnly);
  if (!action) {
    action = new QAction(owner);
    action->setObjectName("kumo-ai-connection");
    markPersonalAction(action);
    Workspace::addActionAndString(action, "kumo-ai-connection");
    QObject::connect(action, &QAction::triggered, qApp,
                     [] { showAIConnection(); });
    if (qEnvironmentVariable("KUMO_MCP_AUTOSTART") != "0")
      QTimer::singleShot(0, owner, [] { initializeBuiltinAI(); });
  }
  action->setText(QObject::tr("AI 连接"));
  if (!menu->actions().contains(action))
    menu->addAction(action);
}
void showPersonalGuide(const QString &page) {
  if (!mscore)
    return;
  if (!guide) {
    guide = new QDialog(mscore);
    guide->setWindowTitle(QObject::tr("个人功能说明"));
    guide->resize(850, 680);
    auto layout = new QVBoxLayout(guide);
    browser = new QTextBrowser;
    browser->setOpenLinks(false);
    layout->addWidget(browser);
    QObject::connect(
        browser, &QTextBrowser::anchorClicked, guide, [](const QUrl &url) {
          if (url.scheme() == "http" || url.scheme() == "https")
            QDesktopServices::openUrl(url);
          else if (url.path().endsWith(".md")) {
            auto full =
                browser->document()->baseUrl().resolved(url).toLocalFile();
            loadGuide(
                QDir(mscoreGlobalShare + "personal").relativeFilePath(full));
          } else if (!url.fragment().isEmpty())
            browser->scrollToAnchor(url.fragment());
        });
  }
  loadGuide(page);
  guide->show();
  guide->raise();
  guide->activateWindow();
}
} // namespace Ms
