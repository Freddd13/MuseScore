// SPDX-License-Identifier: GPL-2.0-or-later
#include "aiconnection.h"
#include "toolicons.h"
#include "featuregate.h"
#include "libmscore/score.h"
#include "mscore/musescore.h"
#include "mscore/plugin/qmlplugin.h"
#include "mscore/plugin/qmlpluginengine.h"
#include "mscore/plugin/api/scoreobserver.h"
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
#include <QSettings>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QHeaderView>
#include <QScrollArea>
#include <QScrollBar>
#include <QCheckBox>
#include <QToolButton>
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
  const QDir root(QDir(QCoreApplication::applicationDirPath()).filePath("tools/mcp"));
  QFile pointer(root.filePath("current.json"));
  if (pointer.open(QIODevice::ReadOnly)) {
    const auto directory = QJsonDocument::fromJson(pointer.readAll()).object().value("directory").toString();
    if (!directory.isEmpty() && !directory.contains('/') && !directory.contains('\\') && directory != "." && directory != "..") {
      const auto program = root.filePath(directory + "/MuseScoreMCP.exe");
      if (QFileInfo::exists(program)) return program;
    }
  }
  return root.filePath("MuseScoreMCP.exe");
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
  QString id = PluginAPI::ScoreObserver::applicationInstanceId(), endpoint,
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
    const auto identity = PluginAPI::ScoreObserver::scoreIdentityFor(owner ? owner->currentScore() : nullptr);
    const QString score = identity.value("hasScore").toBool()
                              ? identity.value("displayTitle").toString().simplified()
                              : tr("尚未打开乐谱");
    const bool active =
        QGuiApplication::applicationState() == Qt::ApplicationActive;
    const auto identityJson = QJsonObject::fromVariantMap(identity);
    const QString key = QString::fromUtf8(QJsonDocument(identityJson).toJson(QJsonDocument::Compact)) + (active ? "1" : "0");
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
              {"identity", identityJson},
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

#include "aiconnectionpanel.inc"
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
  if (!personalFeaturesEnabled() || bridge || !mscore ||
      !QSettings().value("workflow/aiEnabled", true).toBool())
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
    if (qEnvironmentVariable("KUMO_MCP_AUTOSTART") != "0" &&
        QSettings().value("workflow/aiAutostart", true).toBool())
      QTimer::singleShot(0, owner, [] {
        QSettings().setValue("workflow/aiEnabled", true);
        initializeBuiltinAI();
      });
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
