// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin.h"
#include <QFile>
#include <QJsonDocument>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtEndian>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
namespace Ms {
namespace VstHost {
namespace {
QVariantMap comparableUnit(QVariantMap unit) {
  const auto descriptor = unit.value("plugin").toMap();
  if (descriptor.value("format") != "VST2" ||
      !descriptor.value("vendor").toString().contains("Garritan", Qt::CaseInsensitive))
    return unit;
  auto saved = QJsonDocument::fromJson(QByteArray::fromBase64(
      unit.value("state").toString().toLatin1())).toVariant().toMap();
  const auto chunk = QByteArray::fromBase64(saved.value("chunk").toByteArray());
  if (chunk.size() < 9 || !chunk.startsWith("CEGP"))
    return unit;
  const auto length = qFromLittleEndian<quint32>(
      reinterpret_cast<const uchar*>(chunk.constData() + 4));
  if (!length || length > 64 * 1024 * 1024)
    return unit;
  auto compressed = chunk.mid(4);
  qToBigEndian(length, reinterpret_cast<uchar*>(compressed.data()));
  const auto xml = qUncompress(compressed);
  if (quint32(xml.size()) != length)
    return unit;
  QXmlStreamReader reader(xml);
  QByteArray normalized;
  QXmlStreamWriter writer(&normalized);
  bool root = false;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      if (!root) {
        // CFX's ARIA payload increments sc on every state read. Keep the
        // original payload for restoration; omit this counter only in comparison.
        if (reader.name() != "AriaSave" || reader.attributes().value("productID") != "1016")
          return unit;
        root = true;
      }
      writer.writeStartElement(reader.qualifiedName().toString());
      const bool counter = reader.name() == "Settings" || reader.name() == "Slot" ||
                           reader.name() == "EffectSlot";
      for (const auto& attribute : reader.attributes())
        if (!(counter && attribute.name() == "sc"))
          writer.writeAttribute(attribute.qualifiedName().toString(), attribute.value().toString());
    } else if (reader.isEndElement())
      writer.writeEndElement();
    else if (reader.isCharacters() && !reader.isWhitespace())
      writer.writeCharacters(reader.text().toString());
  }
  if (!root || reader.hasError())
    return unit;
  saved["chunk"] = QString::fromLatin1(normalized.toBase64());
  unit["state"] = QString::fromLatin1(
      QJsonDocument::fromVariant(saved).toJson(QJsonDocument::Compact).toBase64());
  return unit;
}
QVariantList comparableEffects(QVariantList effects) {
  for (auto& effect : effects)
    effect = comparableUnit(effect.toMap());
  return effects;
}
QVariantMap comparableConfiguration(QVariantMap value) {
  auto slots = value.value("slots").toList();
  for (auto& item : slots) {
    auto slot = item.toMap();
    slot["source"] = comparableUnit(slot.value("source").toMap());
    slot["effects"] = comparableEffects(slot.value("effects").toList());
    item = slot;
  }
  value["slots"] = slots;
  value["masterEffects"] = comparableEffects(value.value("masterEffects").toList());
  return value;
}
} // namespace
bool sameConfiguration(const QVariantMap& a, const QVariantMap& b) {
  return a == b || comparableConfiguration(a) == comparableConfiguration(b);
}
void placeEditorOnScreen(QWidget* window) {
  if (!window || !window->isVisible() || window->isMaximized() || window->isFullScreen())
    return;
  auto screen = QGuiApplication::screenAt(window->frameGeometry().center());
  if (!screen)
    screen = window->parentWidget() ? window->parentWidget()->screen() : QGuiApplication::primaryScreen();
  if (!screen)
    return;
  const auto area = screen->availableGeometry().adjusted(6, 6, -6, -6);
  const auto frame = window->frameGeometry();
  if (area.contains(frame))
    return;
  window->move(qBound(area.left(), frame.left(), qMax(area.left(), area.right() - frame.width() + 1)),
               qBound(area.top(), frame.top(), qMax(area.top(), area.bottom() - frame.height() + 1)));
}
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
