// SPDX-License-Identifier: GPL-2.0-or-later
#include "featuregate.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>
#include <QSet>
#include <QSettings>
#include <QToolButton>
namespace Ms {
namespace {
struct Configuration {
  QString path, reason;
  bool enabled = false;
};
const Configuration &configuration() {
  static Configuration state;
  const QString path = personalFeatureConfigurationPath();
  if (state.path == path)
    return state;
  state = Configuration();
  state.path = path;
  QFileInfo file(path);
  if (!file.isFile() || !file.isReadable()) {
    state.reason = QObject::tr("扩展功能已关闭，配置文件未找到或无法读取。");
    return state;
  }
  QSettings settings(path, QSettings::IniFormat);
  const QString value = settings.value("features/enable_exp_feature")
                            .toString()
                            .trimmed()
                            .toLower();
  if (settings.status() != QSettings::NoError ||
      (value != "true" && value != "false")) {
    state.reason = QObject::tr(
        "扩展功能已关闭，请将 enable_exp_feature 填为 true 或 false。");
    return state;
  }
  state.enabled = value == "true";
  state.reason = state.enabled ? QObject::tr("个人扩展功能已开启。")
                               : QObject::tr("个人扩展功能已关闭。");
  return state;
}
} // namespace
QString personalFeatureConfigurationPath() {
  // Native tests use a separate installation configuration, without changing
  // the user's file.
  const QString test =
      qApp ? qApp->property("kumoFeatureConfiguration").toString() : QString();
  return test.isEmpty()
             ? QDir(QCoreApplication::applicationDirPath()).filePath("kumo.ini")
             : test;
}
bool personalFeaturesEnabled() { return configuration().enabled; }
QString personalFeatureStatus() {
  return configuration().reason + "\n" + configuration().path;
}
bool personalAction(const QString &key) {
  static const QSet<QString> keys{
      "performance-editor",     "performance-score-band",
      "performance-expression", "harmony-assistant",
      "reference-player",       "reference-audio-play",
      "reference-joint-play",   "arrangement-suggestions",
      "regional-variants",      "kumo-ai-connection",
      "kumo-spectrum",          "kumo-rhythm",
      "kumo-roll-writing",
      "kumo-system-lock-menu", "kumo-system-lock-current", "kumo-system-lock-selected",
      "kumo-system-lock-unlock", "kumo-system-lock-all",
      "kumo-workspace",         "tap-tempo",
      "auto-rhythmic-input",    "auto-tie-beams",
      "coherent-tie-stems"};
  return keys.contains(key);
}
bool personalPlugin(const QString &path) {
  static const QSet<QString> names{
      "HarmonyAssistant_MS3.qml",    "PerformanceExpression_MS3.qml",
      "ReferencePlayer_MS3.qml",     "ArrangementAssistant_MS3.qml",
      "ArrangementVariants_MS3.qml", "musescore-mcp-websocket.qml"};
  return names.contains(QFileInfo(path).fileName());
}
bool retiredPersonalPlugin(const QString &path) {
  static const QSet<QString> names{
      "HarmonyAssistant_MS3.qml",      "HarmonyAssistant.view.qml",
      "PerformanceExpression_MS3.qml", "PerformanceExpression.view.qml",
      "ReferencePlayer_MS3.qml",       "ReferencePlayer.view.qml",
      "musescore-mcp-websocket.qml"};
  return names.contains(QFileInfo(path).fileName());
}
void markPersonalAction(QAction *action) {
  if (!action)
    return;
  action->setProperty("kumoExperimental", true);
  if (!personalFeaturesEnabled()) {
    action->setEnabled(false);
    action->setVisible(false);
  }
}
void applyPersonalFeatureVisibility(QMainWindow *window) {
  if (!window || personalFeaturesEnabled())
    return;
  for (auto action : window->findChildren<QAction *>())
    if (personalAction(action->objectName()) ||
        action->property("kumoExperimental").toBool()) {
      action->setEnabled(false);
      action->setVisible(false);
    }
  for (auto menu : window->findChildren<QMenu *>())
    for (auto action : menu->actions())
      if (personalAction(action->data().toString()) ||
          action->property("kumoExperimental").toBool()) {
        action->setEnabled(false);
        action->setVisible(false);
      }
  for (auto button : window->findChildren<QToolButton *>())
    if (auto action = button->defaultAction())
      if (personalAction(action->objectName()) ||
          action->property("kumoExperimental").toBool())
        button->hide();
}
} // namespace Ms
