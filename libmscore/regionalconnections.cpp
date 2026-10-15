// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "regionalconnections.h"
#include "chord.h"
#include "measure.h"
#include "note.h"
#include "score.h"
#include "segment.h"
#include "slur.h"
#include "spannermap.h"
#include "tie.h"
#include "xml.h"
#include <QBuffer>
#include <QDomDocument>
#include <algorithm>
#include <memory>
namespace Ms {
namespace RegionalConnections {
namespace {
bool inside(const Element *e, int start, int end, const QSet<int> &tracks) {
      return e && tracks.contains(e->track()) && e->tick().ticks() >= start && e->tick().ticks() < end;
}
QList<Spanner *> touching(Score *score, int start, int end, const QSet<int> &tracks) {
      QSet<Spanner *> found;
      auto inspect = [&](Chord *chord) {
            for (auto note : chord->notes())
                  for (auto tie : {note->tieBack(), note->tieFor()})
                        if (tie && (inside(tie->startElement(), start, end, tracks) ||
                                    inside(tie->endElement(), start, end, tracks)))
                              found.insert(tie);
      };
      auto first = score->tick2measure(Fraction::fromTicks(start));
      for (auto segment = first ? first->first(SegmentType::ChordRest) : nullptr; segment;
           segment = segment->next1(SegmentType::ChordRest)) {
            if (segment->tick().ticks() < start)
                  continue;
            if (segment->tick().ticks() >= end)
                  break;
            for (int track : tracks)
                  if (auto e = segment->element(track))
                        if (e->isChord()) {
                              auto chord = toChord(e);
                              inspect(chord);
                              for (auto grace : chord->graceNotes())
                                    inspect(grace);
                        }
      }
      for (auto pair : score->spannerMap().map())
            if (pair.second->isSlur()) {
                  auto line = pair.second;
                  line->startCR();
                  line->endCR();
                  if (inside(line->startElement(), start, end, tracks) ||
                      inside(line->endElement(), start, end, tracks))
                        found.insert(line);
            }
      auto result = found.values();
      std::sort(result.begin(), result.end(), [](const Spanner *a, const Spanner *b) {
            if (a->tick() != b->tick())
                  return a->tick() < b->tick();
            if (a->track() != b->track())
                  return a->track() < b->track();
            if (a->type() != b->type())
                  return int(a->type()) < int(b->type());
            if(a->isTie() && b->isTie()) {
                  auto left=toTie(a)->startNote(),right=toTie(b)->startNote();
                  return left && right ? left->pitch()<right->pitch() : bool(left)<bool(right);
                  }
            return false;
      });
      return result;
}
QVariantMap anchor(Element *e) {
      auto cr = e->isNote() ? toNote(e)->chord() : toChordRest(e);
      int grace = -1;
      if (cr->isChord() && toChord(cr)->isGrace()) {
            auto main = toChord(cr->parent());
            grace = main->graceNotes().indexOf(toChord(cr));
            cr = main;
      }
      QVariantMap value{{"tick", cr->tick().ticks()}, {"track", cr->track()}, {"grace", grace}};
      if (e->isNote()) {
            auto note = toNote(e);
            value["pitch"] = note->pitch();
            value["tpc"] = note->tpc1();
      }
      return value;
}
Element *resolve(Score *score, const QVariantMap &value) {
      if (value.value("track", -1).toInt() < 0 || value.value("track").toInt() >= score->ntracks() ||
          value.value("tick", -1).toInt() < 0)
            return nullptr;
      auto cr = score->findCR(Fraction::fromTicks(value.value("tick").toInt()), value.value("track").toInt());
      if (!cr || cr->tick().ticks() != value.value("tick").toInt())
            return nullptr;
      const int grace = value.value("grace", -1).toInt();
      if (grace >= 0) {
            if (!cr->isChord() || grace >= toChord(cr)->graceNotes().size())
                  return nullptr;
            cr = toChord(cr)->graceNotes()[grace];
      }
      if (!value.contains("pitch"))
            return cr;
      if (cr->isChord())
            for (auto note : toChord(cr)->notes())
                  if (note->pitch() == value.value("pitch").toInt() &&
                      note->tpc1() == value.value("tpc").toInt())
                        return note;
      return nullptr;
}
} // namespace
QVariantList capture(Score *score, int start, int end, const QSet<int> &tracks, QString *error) {
      QVariantList result;
      for (auto line : touching(score, start, end, tracks)) {
            auto a = line->startElement(), b = line->endElement();
            if (!a || !b || !(a->isNote() || a->isChordRest()) || !(b->isNote() || b->isChordRest())) {
                  *error = QObject::tr("这条连线的两端需要重新指定，请选中连线检查起点和终点。");
                  return {};
            }
            if (a->track() != b->track() && (!tracks.contains(a->track()) || !tracks.contains(b->track()))) {
                  *error = QObject::tr("连线连接到另一声部，请把两端的声部一起加入方案。");
                  return {};
            }
            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            XmlWriter xml(score, &buffer);
            xml.setClipboardmode(true);
            line->write(xml);
            xml.flush();
            if (buffer.data().isEmpty()) {
                  *error = QObject::tr("这条连线需要整理后再保存，请检查连线两端。");
                  return {};
            }
            result.append(QVariantMap{{"type", line->isTie() ? "Tie" : "Slur"},
                                      {"start", anchor(a)},
                                      {"end", anchor(b)},
                                      {"xml", QString::fromLatin1(buffer.data().toBase64())}});
      }
      return result;
}
bool validate(const QVariantList &values, const QVariantMap &group, QString *error) {
      const int start = group.value("start").toInt(), end = group.value("end").toInt();
      QSet<int> tracks;
      for (auto value : group.value("tracks").toList())
            tracks.insert(value.toInt());
      if (values.size() > 8192) {
            *error = QObject::tr("这一段的连线数量过多，请拆成较短的方案。");
            return false;
      }
      for (auto value : values) {
            auto item = value.toMap();
            const auto type = item.value("type").toString();
            auto a = item.value("start").toMap(), b = item.value("end").toMap();
            auto owned = [&](const QVariantMap &p) {
                  return tracks.contains(p.value("track").toInt()) && p.value("tick").toInt() >= start &&
                         p.value("tick").toInt() < end;
            };
            const auto bytes = QByteArray::fromBase64(item.value("xml").toByteArray());
            QDomDocument document;
            if ((type != "Tie" && type != "Slur") || (!owned(a) && !owned(b)) || bytes.size() > 256 * 1024 ||
                bytes.contains("<!DOCTYPE") || !document.setContent(bytes) ||
                document.documentElement().tagName() != type) {
                  *error = QObject::tr("方案的连线记录不完整，请从以前保存的版本恢复。");
                  return false;
            }
            if (type == "Tie" &&
                (!a.contains("pitch") || !b.contains("pitch") || a.value("pitch") != b.value("pitch"))) {
                  *error = QObject::tr("延音线两端的音高需要相同，请检查方案中的延音线。");
                  return false;
            }
      }
      return true;
}
void strip(QDomDocument &document) {
      const auto lines = document.elementsByTagName("Spanner");
      for (int i = lines.size() - 1; i >= 0; --i) {
            auto node = lines.at(i);
            auto type = node.toElement().attribute("type");
            if (type == "Tie" || type == "Slur")
                  node.parentNode().removeChild(node);
      }
}
void remove(Score *score, int start, int end, const QSet<int> &tracks) {
      for (auto line : touching(score, start, end, tracks))
            score->undoRemoveElement(line);
}
bool restore(Score *score, const QVariantList &values, QString *error) {
      // Resolve all endpoints before adding any line. The enclosing score command
      // rolls back the notation and links together when an endpoint changed.
      QList<QPair<Element *, Element *>> ends;
      for (auto value : values) {
            auto item = value.toMap();
            auto a = resolve(score, item.value("start").toMap()),
                 b = resolve(score, item.value("end").toMap());
            if (!a || !b ||
                (item.value("type") == "Tie" &&
                 (!a->isNote() || !b->isNote() || toNote(a)->pitch() != toNote(b)->pitch()))) {
                  const int tick = item.value("start").toMap().value("tick").toInt();
                  auto measure = score->tick2measure(Fraction::fromTicks(tick));
                  const double beat = measure ? 1. + (tick - measure->tick().ticks()) /
                                                         double(480 * 4 / measure->timesig().denominator())
                                              : 1.;
                  *error = QObject::tr("第%1小节第%2拍的连线端点已改变。请检查两端的音高和位置，调整后再切换。")
                               .arg(measure ? measure->no() + 1 : 1)
                               .arg(beat, 0, 'f', 2);
                  return false;
            }
            ends.append(qMakePair(a, b));
      }
      for (int i = 0; i < values.size(); ++i) {
            const auto item = values[i].toMap();
            const bool tie = item.value("type") == "Tie";
            std::unique_ptr<Spanner> line(tie ? static_cast<Spanner *>(new Tie(score))
                                              : static_cast<Spanner *>(new Slur(score)));
            XmlReader xml(QByteArray::fromBase64(item.value("xml").toByteArray()));
            xml.setPasteMode(true);
            xml.setTrack(ends[i].first->track());
            if (!xml.readNextStartElement()) {
                  *error = QObject::tr("连线外观读取失败，请恢复以前的方案。");
                  return false;
            }
            line->read(xml);
            if (xml.hasError()) {
                  *error = QObject::tr("连线外观读取失败：%1").arg(xml.errorString());
                  return false;
            }
            auto a = ends[i].first, b = ends[i].second;
            line->setStartElement(a);
            line->setEndElement(b);
            line->setTrack(a->track());
            line->setTrack2(b->track());
            line->setTick(a->tick());
            line->setTick2(b->tick());
            if (tie)
                  line->setParent(a);
            score->undoAddElement(line.release());
      }
      return true;
}
} // namespace RegionalConnections
} // namespace Ms
