// SPDX-License-Identifier: GPL-2.0-or-later
#include "vstpresets.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QObject>
namespace Ms {
QByteArray encodeVstPreset(const QByteArray &state, const QVariantMap &plugin) {
  QVariantMap identity;
  for (auto key : {"format", "uid", "name", "vendor"})
    identity[key] = plugin.value(key);
  return QJsonDocument::fromVariant(QVariantMap{{"schema", "kumo-vst-preset/1"},
                                                {"plugin", identity},
                                                {"state", QString::fromLatin1(state.toBase64())}})
      .toJson();
}
QByteArray readVstPreset(const QString &path, const QVariantMap &plugin, bool *wrapped,
                         QString *error) {
  *wrapped = false;
  if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive))
    return {};
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *error = QObject::tr("预设无法读取：%1").arg(path);
    return {};
  }
  if (file.size() > 96 * 1024 * 1024) {
    *error = QObject::tr("这份预设较大，请从插件窗口中载入。");
    return {};
  }
  const auto value = QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
  if (!value.value("schema").toString().startsWith("kumo-vst-preset/"))
    return {};
  *wrapped = true;
  if (value["schema"] != "kumo-vst-preset/1") {
    *error = QObject::tr("这份预设使用了更新的保存格式，请更新程序后载入。");
    return {};
  }
  const auto expected = value["plugin"].toMap();
  if (expected["uid"] != plugin["uid"] || expected["format"] != plugin["format"]) {
    *error = QObject::tr("这份预设用于%1，当前选中的是%2。选中相应插件后可以载入。")
                 .arg(expected["name"].toString(), plugin["name"].toString());
    return {};
  }
  auto bytes = QByteArray::fromBase64(value["state"].toString().toLatin1());
  if (bytes.isEmpty() || bytes.size() > 64 * 1024 * 1024) {
    *error = QObject::tr("预设中的声音数据不完整，请重新保存这份预设。");
    return {};
  }
  return bytes;
}
} // namespace Ms
