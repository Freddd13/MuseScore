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
#include <QMap>
#include <QSet>
#include <algorithm>
#include <vector>
namespace Ms {
namespace VstHost {
namespace {
struct AriaNode {
  QString name,text;
  QMap<QString,QString> attributes;
  std::vector<AriaNode> content;
};
AriaNode readAriaNode(QXmlStreamReader& reader) {
  AriaNode node;node.name=reader.qualifiedName().toString();
  for (const auto& attribute:reader.attributes())
    node.attributes.insert(attribute.qualifiedName().toString(),attribute.value().toString());
  for (const auto& ns:reader.namespaceDeclarations())
    node.attributes.insert(ns.prefix().isEmpty()?QString("xmlns"):QString("xmlns:")+ns.prefix().toString(),ns.namespaceUri().toString());
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isEndElement()) break;
    if (reader.isStartElement()) node.content.push_back(readAriaNode(reader));
    else if (reader.isCharacters() && !reader.isWhitespace()) {
      AriaNode text;text.text=reader.text().toString();node.content.push_back(std::move(text));
    }
  }
  return node;
}
void writeComparableAria(AriaNode& node,QXmlStreamWriter& writer) {
  if (node.name.isEmpty()) { writer.writeCharacters(node.text);return; }
  writer.writeStartElement(node.name);
  const bool counter=node.name=="Settings" || node.name=="Slot" || node.name=="EffectSlot";
  for (auto it=node.attributes.cbegin();it!=node.attributes.cend();++it)
    if (!(counter && it.key()=="sc")) writer.writeAttribute(it.key(),it.value());
  if (node.name=="EffectSlot") {
    // ARIA writes these named data attachments in instance-dependent order.
    // Keep effect slots and all payloads intact; compare each Target by name.
    std::vector<size_t> positions;QSet<QString> names;bool unique=true;
    for (size_t i=0;i<node.content.size();++i) if (node.content[i].name=="Target") {
      const auto name=node.content[i].attributes.value("name");
      unique &= !name.isEmpty() && !names.contains(name);names.insert(name);positions.push_back(i);
    }
    if (unique && positions.size()>1) {
      std::vector<AriaNode> targets;
      for (const auto i:positions) targets.push_back(std::move(node.content[i]));
      std::sort(targets.begin(),targets.end(),[](const AriaNode& a,const AriaNode& b) {
        return a.attributes.value("name")<b.attributes.value("name");
      });
      for (size_t i=0;i<positions.size();++i) node.content[positions[i]]=std::move(targets[i]);
    }
  }
  for (auto& child:node.content) writeComparableAria(child,writer);
  writer.writeEndElement();
}
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
  if (!reader.readNextStartElement() || reader.name()!="AriaSave" ||
      reader.attributes().value("productID")!="1016") return unit;
  auto root=readAriaNode(reader);
  while (!reader.atEnd()) reader.readNext();
  if (reader.hasError()) return unit;
  writeComparableAria(root,writer);
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
