// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin.h"
#include <QFile>
namespace Ms {
namespace VstHost {
QVariantMap Descriptor::value() const {
  return {{"path", path}, {"format", format}, {"uid", uid},
          {"name", name}, {"vendor", vendor}, {"instrument", instrument}};
}
Descriptor Descriptor::fromValue(const QVariantMap &value) {
  return {value["path"].toString(),   value["format"].toString(),
          value["uid"].toString(),    value["name"].toString(),
          value["vendor"].toString(), value["instrument"].toBool()};
}
bool Plugin::importPreset(const QString &path, QString *error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = QStringLiteral("预设文件无法读取：") + path;
    return false;
  }
  if (file.size() > 64 * 1024 * 1024) {
    if (error)
      *error = QStringLiteral("这个预设大于 64 MiB，请从插件界面中打开。");
    return false;
  }
  return restore(file.readAll(), error);
}
} // namespace VstHost
} // namespace Ms
