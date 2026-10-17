// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "regionalvariants.h"
#include "audio.h"
#include "articulation.h"
#include "beam.h"
#include "chord.h"
#include "imageStore.h"
#include "instrument.h"
#include "lyrics.h"
#include "measure.h"
#include "note.h"
#include "part.h"
#include "regionalconnections.h"
#include "score.h"
#include "segment.h"
#include "select.h"
#include "slur.h"
#include "spanner.h"
#include "spannermap.h"
#include "staff.h"
#include "thirdparty/qzip/qzipreader_p.h"
#include "thirdparty/qzip/qzipwriter_p.h"
#include "tie.h"
#include "tremolo.h"
#include "tuplet.h"
#include "undo.h"
#include "xml.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDomDocument>
#include <QJsonDocument>
#include <QScopedValueRollback>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <memory>
namespace Ms {
namespace RegionalVariants {
namespace {
const char *property = "kumoRegionalVariants";
const int fragmentLimit = 16 * 1024 * 1024, documentLimit = 64 * 1024 * 1024;
thread_local bool saving = false;
QString id() {
      return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
}
QVariantMap error(const QString &reason, int from = -1, int until = -1) {
      return {{"error", reason}, {"expandStart", from}, {"expandEnd", until}};
}
QSet<int> trackSet(const QVariantList &values) {
      QSet<int> result;
      for (auto v : values)
            result.insert(v.toInt());
      return result;
}
QVariantList trackValues(const QSet<int> &tracks) {
      auto ordered = tracks.values();
      std::sort(ordered.begin(), ordered.end());
      QVariantList result;
      for (int t : ordered)
            result.append(t);
      return result;
}
QString signature(Score *score, int start, int end) {
      QByteArray bytes = QByteArray::number(score->nstaves()) + ":" + QByteArray::number(start) + ":" +
                         QByteArray::number(end);
      for (auto m = score->tick2measure(Fraction::fromTicks(start)); m && m->tick().ticks() < end;
           m = m->nextMeasure())
            bytes += ':' + m->ticks().toString().toUtf8() + ':' + m->timesig().toString().toUtf8();
      return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
class ChangeState : public UndoCommand {
      MasterScore *_score;
      QVariantMap _value;
      void flip(EditData *) override {
            auto previous = _score->property(property).toMap();
            _score->QObject::setProperty(property, _value);
            _value = previous;
            _score->cmdState().setUpdateMode(UpdateMode::UpdateAll);
            _score->setPlaylistDirty();
      }

    public:
      ChangeState(MasterScore *score, const QVariantMap &value) : _score(score), _value(value) {}
      const char *name() const override {
            return "ChangeRegionalVariants";
      }
};
int groupIndex(const QVariantList &groups, const QString &group) {
      for (int i = 0; i < groups.size(); ++i)
            if (groups[i].toMap().value("id").toString() == group)
                  return i;
      return -1;
}
int variantIndex(const QVariantList &variants, const QString &variant) {
      for (int i = 0; i < variants.size(); ++i)
            if (variants[i].toMap().value("id").toString() == variant)
                  return i;
      return -1;
}
QByteArray packed(const QByteArray &data) {
      return qCompress(data, 6).toBase64();
}
void rememberImages(QVariantMap &data, const QByteArray &xml) {
      auto assets = data.value("assets").toMap();
      for (auto image : imageStore) {
            QString name = image->hashName();
            if (!name.isEmpty() && xml.contains(name.toUtf8())) {
                  image->load();
                  assets["Pictures/" + name] = QString::fromLatin1(image->buffer().toBase64());
            }
      }
      data["assets"] = assets;
}
QByteArray unpacked(const QVariant &value, QString *problem) {
      QByteArray compressed = QByteArray::fromBase64(value.toByteArray());
      if (compressed.size() < 4) {
            *problem = QObject::tr("方案片段不完整。");
            return {};
      }
      quint32 length = (quint32(uchar(compressed[0])) << 24) | (quint32(uchar(compressed[1])) << 16) |
                       (quint32(uchar(compressed[2])) << 8) | quint32(uchar(compressed[3]));
      if (length > quint32(fragmentLimit)) {
            *problem = QObject::tr("方案片段过大，请缩小范围。");
            return {};
      }
      auto data = qUncompress(compressed);
      if (data.size() != int(length)) {
            *problem = QObject::tr("方案片段读取失败，请恢复以前的文件。");
            return {};
      }
      return data;
}
bool ownedSpanner(const Spanner *line, int start, int end, const QSet<int> &tracks) {
      return tracks.contains(line->track()) && tracks.contains(line->track2()) &&
             line->tick().ticks() >= start && line->tick2().ticks() < end &&
             (line->isSlur() || line->isGlissando());
}
// Validate ownership before passing clipboard XML to the native notation reader.
bool validPosition(const QString &text, int expected) {
      auto parts = text.split('/');
      if (parts.size() != 2)
            return false;
      bool a = false, b = false;
      int numerator = parts[0].toInt(&a), denominator = parts[1].toInt(&b);
      return a && b && numerator >= 0 && denominator > 0 &&
             Fraction(numerator, denominator).ticks() == expected;
}
bool validateFragment(const QVariantMap &group, const QByteArray &data, QString *problem) {
      QDomDocument document;
      if (data.size() > fragmentLimit || data.contains("<!DOCTYPE") || !document.setContent(data)) {
            *problem = QObject::tr("方案片段不是完整的记谱数据。");
            return false;
      }
      const auto root = document.documentElement();
      const auto tracks = trackSet(group.value("tracks").toList());
      if (tracks.isEmpty() || root.tagName() != "StaffList" || root.attribute("version") != MSC_VERSION ||
          !validPosition(root.attribute("tick"), group.value("start").toInt()) ||
          !validPosition(root.attribute("len"), group.value("end").toInt() - group.value("start").toInt())) {
            *problem = QObject::tr("方案位置、总拍数或记谱版本不一致。");
            return false;
      }
      auto ordered = tracks.values();
      std::sort(ordered.begin(), ordered.end());
      const int first = ordered.front() / VOICES, last = ordered.back() / VOICES;
      if (root.attribute("staff").toInt() != first || root.attribute("staves").toInt() != last - first + 1) {
            *problem = QObject::tr("方案片段包含不属于本组的谱表。");
            return false;
      }
      QSet<int> seen;
      const QSet<QString> allowed{"voiceOffset",
                                  "transposeChromatic",
                                  "transposeDiatonic",
                                  "location",
                                  "Chord",
                                  "Rest",
                                  "Tuplet",
                                  "endTuplet",
                                  "Beam",
                                  "Slur",
                                  "Glissando",
                                  "Spanner",
                                  "tick",
                                  "BarLine",
                                  "Fermata",
                                  "Sticking",
                                  "Breath",
                                  "Symbol",
                                  "Image",
                                  "TremoloBar"};
      for (auto staff = root.firstChildElement(); !staff.isNull(); staff = staff.nextSiblingElement()) {
            bool numeric = false;
            int index = staff.attribute("id").toInt(&numeric);
            if (staff.tagName() != "Staff" || !numeric || index < first || index > last ||
                seen.contains(index)) {
                  *problem = QObject::tr("方案谱表编号重复或不正确。");
                  return false;
            }
            seen.insert(index);
            int track = index * VOICES;
            bool offsets = false;
            for (auto node = staff.firstChildElement(); !node.isNull(); node = node.nextSiblingElement()) {
                  if (!allowed.contains(node.tagName())) {
                        *problem = QObject::tr("方案中包含不能独立切换的谱表设置：%1").arg(node.tagName());
                        return false;
                  }
                  if (node.tagName() == "voiceOffset") {
                        if (offsets) {
                              *problem = QObject::tr("方案声部声明重复。");
                              return false;
                        }
                        offsets = true;
                        QSet<int> voices;
                        for (auto v = node.firstChildElement(); !v.isNull(); v = v.nextSiblingElement()) {
                              bool ok = false;
                              int voice = v.attribute("id").toInt(&ok);
                              if (v.tagName() != "voice" || !ok || voice < 0 || voice >= VOICES ||
                                  !tracks.contains(index * VOICES + voice) || voices.contains(voice) ||
                                  v.text() != "0") {
                                    *problem = QObject::tr("方案试图替换未选中的声部。");
                                    return false;
                              }
                              voices.insert(voice);
                        }
                        for (int v = 0; v < VOICES; ++v)
                              if (tracks.contains(index * VOICES + v) && !voices.contains(v)) {
                                    *problem = QObject::tr("方案缺少选中声部的声明。");
                                    return false;
                              }
                  } else if (node.tagName() == "location") {
                        if (node.firstChildElement("staves").text().toInt() != 0 ||
                            node.firstChildElement("measures").text().toInt() != 0) {
                              *problem = QObject::tr("方案的音符位置跨出了所选谱表。");
                              return false;
                        }
                        track += node.firstChildElement("voices").text().toInt();
                  } else if (node.tagName() == "Chord" || node.tagName() == "Rest" ||
                             node.tagName() == "Tuplet") {
                        if (!offsets || !tracks.contains(track) || track / VOICES != index) {
                              *problem = QObject::tr("方案音符属于未选中的声部。");
                              return false;
                        }
                  }
            }
            if (!offsets) {
                  *problem = QObject::tr("方案缺少声部范围。");
                  return false;
            }
      }
      if (seen.size() != last - first + 1) {
            *problem = QObject::tr("方案缺少谱表。");
            return false;
      }
      return true;
}
bool replace(Score *score, const QVariantMap &group, const QByteArray &data, QString *problem,
             const QVariantMap &writing = {}) {
      if (writing.contains("connections") &&
          !RegionalConnections::validate(writing.value("connections").toList(), group, problem))
            return false;
      if (!validateFragment(group, data, problem))
            return false;
      const int start = group.value("start").toInt(), end = group.value("end").toInt();
      auto tracks = trackSet(group.value("tracks").toList());
      auto checked = check(score, start, end, trackValues(tracks));
      if (checked.contains("error")) {
            *problem = checked.value("error").toString();
            return false;
      }
      auto dst = score->tick2segment(Fraction::fromTicks(start), false, SegmentType::ChordRest);
      if (!dst) {
            *problem = QObject::tr("方案起点没有音符或休止，请扩大到完整小节。");
            return false;
      }
      QDomDocument document;
      if (!document.setContent(data) || document.documentElement().tagName() != "StaffList") {
            *problem = QObject::tr("方案片段格式不正确。");
            return false;
      }
      const auto root = document.documentElement();
      if (Fraction::fromString(root.attribute("tick")).ticks() != start ||
          Fraction::fromString(root.attribute("len")).ticks() != end - start) {
            *problem = QObject::tr("方案总拍数和位置不同，不能直接替换。");
            return false;
      }
      RegionalConnections::remove(score, start, end, tracks);
      QList<Spanner *> remove;
      for (auto pair : score->spannerMap().map())
            if (ownedSpanner(pair.second, start, end, tracks))
                  remove.append(pair.second);
      for (auto line : remove)
            score->undoRemoveElement(line);
      // Segment markings are not removed by makeGap; replace only owned markings.
      QList<Element *> markings;
      for (auto segment = score->firstMeasure()->first(); segment; segment = segment->next1()) {
            int tick = segment->tick().ticks();
            if (tick < start)
                  continue;
            if (tick >= end)
                  break;
            for (auto annotation : segment->annotations())
                  if (tracks.contains(annotation->track()) &&
                      (annotation->isFermata() || annotation->isSticking() || annotation->isSymbol() ||
                       annotation->isImage() || annotation->isTremoloBar()))
                        markings << annotation;
            if (segment->isBreathType())
                  for (int track : tracks)
                        if (auto breath = segment->element(track))
                              markings << breath;
      }
      for (auto marking : markings)
            score->undoRemoveElement(marking);
      XmlReader reader(data);
      reader.setPasteMode(true);
      if (!score->pasteStaff(reader, dst, root.attribute("staff").toInt())) {
            *problem = QObject::tr("片段不能粘贴到这里，原内容已恢复。");
            return false;
      }
      // pasteStaff probes past the root element; the complete XML was validated above.
      if (reader.hasError() && reader.error() != QXmlStreamReader::PrematureEndOfDocumentError) {
            *problem = QObject::tr("方案片段中的记谱数据不完整，原内容已恢复：%1").arg(reader.errorString());
            return false;
      }
      if (writing.contains("connections") &&
          !RegionalConnections::restore(score, writing.value("connections").toList(), problem))
            return false;
      return true;
}
QVariantMap capturedState(Score *score, QString *problem) {
      auto data = state(score);
      auto groups = data.value("groups").toList();
      for (int i = 0; i < groups.size(); ++i) {
            auto g = groups[i].toMap();
            if (g.value("signature").toString() !=
                signature(score, g.value("start").toInt(), g.value("end").toInt())) {
                  *problem = QObject::tr("第 %1 个方案范围的拍号、长度或谱表已经改变。请先调整方案范围。")
                                 .arg(i + 1);
                  return {};
            }
            auto variants = g.value("variants").toList();
            int selected = variantIndex(variants, g.value("active").toString());
            if (selected < 0) {
                  *problem = QObject::tr("当前方案记录缺失。");
                  return {};
            }
            auto bytes = capture(score, g.value("start").toInt(), g.value("end").toInt(),
                                 g.value("tracks").toList(), problem);
            if (!problem->isEmpty())
                  return {};
            rememberImages(data, bytes);
            auto v = variants[selected].toMap();
            v["data"] = QString::fromLatin1(packed(bytes));
            v["connections"] =
                RegionalConnections::capture(score, g.value("start").toInt(), g.value("end").toInt(),
                                             trackSet(g.value("tracks").toList()), problem);
            if (!problem->isEmpty())
                  return {};
            variants[selected] = v;
            g["variants"] = variants;
            groups[i] = g;
      }
      data["groups"] = groups;
      data["schema"] = 2;
      return data;
}
} // namespace
QVariantMap state(const Score *score) {
      if (!score)
            return {};
      return score->masterScore()->property(property).toMap();
}
bool active(const Score *score) {
      return !state(score).value("groups").toList().isEmpty();
}
bool writing() {
      return saving;
}
QVariantMap check(Score *score, int start, int end, const QVariantList &values) {
      if (!score || !score->isMaster())
            return error(QObject::tr("请在总谱中建立或切换分段方案。分谱会随总谱一起更新。"));
      auto tracks = trackSet(values);
      if (start < 0 || end <= start || tracks.isEmpty() || tracks.size() > 128 || !score->lastMeasure() ||
          end > score->lastMeasure()->endTick().ticks())
            return error(QObject::tr("请选择有效的小节和谱表声部。"));
      for (int track : tracks)
            if (track < 0 || track >= score->ntracks())
                  return error(QObject::tr("谱表或声部不存在。"));
      auto first = score->tick2measure(Fraction::fromTicks(start)),
           last = score->tick2measure(Fraction::fromTicks(end - 1));
      if (!first || !last || first->tick().ticks() != start || last->endTick().ticks() != end)
            return error(QObject::tr("第一版方案按完整小节保存，请扩大到小节两端。"),
                         first ? first->tick().ticks() : start, last ? last->endTick().ticks() : end);
      auto from = score->tick2segment(Fraction::fromTicks(start), false, SegmentType::ChordRest);
      for (auto segment = from; segment && segment->tick().ticks() < end;
           segment = segment->next1(SegmentType::ChordRest))
            for (int track : tracks) {
                  auto e = segment->element(track);
                  if (!e || !e->isChordRest())
                        continue;
                  auto cr = toChordRest(e);
                  int tick = segment->tick().ticks(), until = tick + cr->actualTicks().ticks();
                  if (until > end)
                        return error(QObject::tr("音符延续到范围之外，请扩大方案。"), start, until);
                  if (cr->tuplet()) {
                        auto tuplet = cr->tuplet();
                        while (tuplet->tuplet())
                              tuplet = tuplet->tuplet();
                        if (tuplet->tick().ticks() < start ||
                            tuplet->tick().ticks() + tuplet->actualTicks().ticks() > end)
                              return error(QObject::tr("连音组跨出方案范围，请把整个连音组放进方案。"),
                                           qMin(start, tuplet->tick().ticks()),
                                           qMax(end, tuplet->tick().ticks() + tuplet->actualTicks().ticks()));
                  }
                  if (cr->beam())
                        for (auto other : cr->beam()->elements())
                              if (other->tick().ticks() < start || other->tick().ticks() >= end ||
                                  !tracks.contains(other->track()))
                                    return error(
                                        QObject::tr("符杠连接到另一段或声部，请一起选择。"),
                                        qMin(start, other->tick().ticks()),
                                        qMax(end, other->tick().ticks() + other->actualTicks().ticks()));
                  for (auto lyric : cr->lyrics())
                        if (tick + lyric->ticks().ticks() > end)
                              return error(QObject::tr("歌词延续到下一段，请扩大方案范围。"), start,
                                           tick + lyric->ticks().ticks());
                  if (!e->isChord())
                        continue;
                  auto chord = toChord(e);
                  auto chords = chord->graceNotes();
                  chords.append(chord);
                  for (auto ownedChord : chords)
                        for (auto note : ownedChord->notes())
                              for (const auto &lines : {note->spannerFor(), note->spannerBack()})
                                    for (auto line : lines)
                                          if (line->isGlissando() && !ownedSpanner(line, start, end, tracks))
                                                return error(
                                                    QObject::tr("滑音连接到另一段或声部，请把两端一起加入方案。"),
                                                    qMin(start, line->tick().ticks()),
                                                    qMax(end, line->tick2().ticks() + 1));
                  if (chord->staffMove() != 0 && !tracks.contains(track + chord->staffMove() * VOICES))
                        return error(QObject::tr("跨谱表音符需要同时选择目标谱表的相应声部。"));
                  if (chord->tremolo() && chord->tremolo()->twoNotes()) {
                        auto tremolo = chord->tremolo();
                        for (auto other : {tremolo->chord1(), tremolo->chord2()})
                              if (other && (other->tick().ticks() < start || other->tick().ticks() >= end ||
                                            !tracks.contains(other->track())))
                                    return error(QObject::tr("双音震音连接到范围之外，请一起选择。"));
                  }
            }
      for(auto pair:score->spannerMap().map()) {
            auto line=pair.second;
            if(line->isGlissando() && line->tick2().ticks()>=start && line->tick().ticks()<end &&
                  (tracks.contains(line->track()) || tracks.contains(line->track2())) &&
                  !ownedSpanner(line,start,end,tracks))
                  return error(QObject::tr("滑音连接到另一段或声部，请把两端一起加入方案。"),qMin(start,line->tick().ticks()),qMax(end,line->tick2().ticks()+1));
            }
      QString connectionError;
      RegionalConnections::capture(score, start, end, tracks, &connectionError);
      if (!connectionError.isEmpty())
            return error(connectionError, start, end);
      return {{"ok", true}, {"start", start}, {"end", end}, {"tracks", trackValues(tracks)}};
}
QByteArray capture(Score *score, int start, int end, const QVariantList &values, QString *problem) {
      auto checked = check(score, start, end, values);
      if (checked.contains("error")) {
            *problem = checked.value("error").toString();
            return {};
      }
      auto tracks = trackSet(values);
      auto sorted = tracks.values();
      std::sort(sorted.begin(), sorted.end());
      const int first = sorted.front() / VOICES, last = sorted.back() / VOICES;
      auto from = score->tick2segment(Fraction::fromTicks(start), false, SegmentType::ChordRest);
      Segment *until = nullptr;
      for (auto s = from; s; s = s->next1(SegmentType::ChordRest))
            if (s->tick().ticks() >= end) {
                  until = s;
                  break;
            }
      QBuffer buffer;
      buffer.open(QIODevice::WriteOnly);
      XmlWriter xml(score, &buffer);
      xml.header();
      xml.setClipboardmode(true);
      xml.stag(
          QString("StaffList version=\"" MSC_VERSION "\" tick=\"%1\" len=\"%2\" staff=\"%3\" staves=\"%4\"")
              .arg(Fraction::fromTicks(start).toString(), Fraction::fromTicks(end - start).toString())
              .arg(first)
              .arg(last - first + 1));
      for (int staff = first; staff <= last; ++staff) {
            SelectionFilter filter(SelectionFilterType::ALL);
            for (int voice = 0; voice < VOICES; ++voice)
                  filter.setFiltered(SelectionFilterType(1 << voice),
                                     tracks.contains(staff * VOICES + voice));
            for (auto kind : {SelectionFilterType::DYNAMIC, SelectionFilterType::HAIRPIN,
                              SelectionFilterType::PEDAL_LINE, SelectionFilterType::OTHER_TEXT,
                              SelectionFilterType::OTHER_LINE, SelectionFilterType::CHORD_SYMBOL,
                              SelectionFilterType::FIGURED_BASS, SelectionFilterType::OTTAVA})
                  filter.setFiltered(kind, false);
            xml.setFilter(filter);
            xml.stag(QString("Staff id=\"%1\"").arg(staff));
            xml.stag("voiceOffset");
            for (int voice = 0; voice < VOICES; ++voice)
                  if (tracks.contains(staff * VOICES + voice))
                        xml.tag(QString("voice id=\"%1\"").arg(voice), 0);
            xml.etag();
            auto interval = score->staff(staff)->part()->instrument(Fraction::fromTicks(start))->transpose();
            xml.tag("transposeChromatic", interval.chromatic);
            xml.tag("transposeDiatonic", interval.diatonic);
            xml.setCurTrack(staff * VOICES);
            score->writeSegments(xml, staff * VOICES, (staff + 1) * VOICES, from, until, false, false);
            xml.etag();
      }
      xml.etag();
      auto bytes = buffer.data();
      if (bytes.size() > fragmentLimit) {
            *problem = QObject::tr("这一段数据过大，请拆成更小的方案组。");
            return {};
      }
      QDomDocument document;
      if (!document.setContent(bytes)) {
            *problem = QObject::tr("不能读取这一段的记谱数据。");
            return {};
      }
      // Staff-level settings stay shared; only notation and note-attached markings vary.
      const QSet<QString> shared{"TempoText", "Dynamic", "Harmony",     "StaffText",   "Text",
                                 "Marker",    "Jump",    "FretDiagram", "FiguredBass", "Clef",
                                 "KeySig",    "TimeSig", "BarLine"};
      for (auto staff = document.documentElement().firstChildElement("Staff"); !staff.isNull();
           staff = staff.nextSiblingElement("Staff"))
            for (auto node = staff.firstChild(); !node.isNull();) {
                  auto next = node.nextSibling();
                  if (shared.contains(node.toElement().tagName()))
                        staff.removeChild(node);
                  node = next;
            }
      RegionalConnections::strip(document);
      return document.toByteArray();
}
QVariantMap create(Score *score, int start, int end, const QVariantList &values, const QString &name) {
      if (!score || score->readOnly())
            return error(QObject::tr("这份乐谱只供查看，请在可编辑的总谱建立方案。"));
      auto checked = check(score, start, end, values);
      if (checked.contains("error"))
            return checked;
      auto data = state(score);
      auto groups = data.value("groups").toList();
      if (groups.size() >= 64)
            return error(QObject::tr("方案组已达64个，请把作品拆成几个文件。"));
      auto tracks = trackSet(values);
      for (auto value : groups) {
            auto g = value.toMap();
            if (start < g.value("end").toInt() && end > g.value("start").toInt())
                  for (int track : trackSet(g.value("tracks").toList()))
                        if (tracks.contains(track))
                              return error(QObject::tr("这段声部已经属于另一组方案，请改选范围或其它声部。"));
      }
      QString problem;
      auto bytes = capture(score, start, end, values, &problem);
      if (!problem.isEmpty())
            return error(problem);
      rememberImages(data, bytes);
      QString group = id(), variant = id();
      QVariantMap v{{"id", variant},
                    {"name", QObject::tr("A · 当前写法")},
                    {"data", QString::fromLatin1(packed(bytes))},
                    {"connections", RegionalConnections::capture(score, start, end, tracks, &problem)}};
      groups.append(QVariantMap{{"id", group},
                                {"name", name.trimmed().isEmpty() ? QObject::tr("分段方案") : name.left(80)},
                                {"start", start},
                                {"end", end},
                                {"tracks", trackValues(tracks)},
                                {"signature", signature(score, start, end)},
                                {"active", variant},
                                {"main", variant},
                                {"variants", QVariantList{v}}});
      data["schema"] = 2;
      data["groups"] = groups;
      const bool ownCommand = !score->undoStack()->active();
      if (ownCommand) score->startCmd();
      score->undo(new ChangeState(score->masterScore(), data));
      if (ownCommand) score->endCmd();
      return {{"ok", true}, {"group", group}, {"variant", variant}};
}
QVariantMap duplicate(Score *score, const QString &group, const QString &name, bool activate) {
      if (!score || score->readOnly())
            return error(QObject::tr("请选择可编辑的总谱。"));
      QString problem;
      auto data = capturedState(score, &problem);
      if (!problem.isEmpty())
            return error(problem);
      auto groups = data.value("groups").toList();
      int index = groupIndex(groups, group);
      if (index < 0)
            return error(QObject::tr("方案组不存在。"));
      auto g = groups[index].toMap();
      auto variants = g.value("variants").toList();
      if (variants.size() >= 32)
            return error(QObject::tr("这一组已有32种写法，请另建一组。"));
      int selected = variantIndex(variants, g.value("active").toString());
      if (selected < 0)
            return error(QObject::tr("当前写法不存在。"));
      auto v = variants[selected].toMap();
      v["id"] = id();
      v["name"] = name.trimmed().isEmpty() ? QObject::tr("新写法") : name.left(80);
      variants.append(v);
      g["variants"] = variants;
      if (activate)
            g["active"] = v.value("id");
      groups[index] = g;
      data["groups"] = groups;
      score->startCmd();
      score->undo(new ChangeState(score->masterScore(), data));
      score->endCmd();
      return {{"ok", true}, {"variant", v.value("id")}};
}
QVariantMap select(Score *score, const QString &group, const QString &variant) {
      if (!score || score->readOnly())
            return error(QObject::tr("请选择可编辑的总谱。"));
      QString problem;
      auto data = capturedState(score, &problem);
      if (!problem.isEmpty())
            return error(problem);
      auto groups = data.value("groups").toList();
      int index = groupIndex(groups, group);
      if (index < 0)
            return error(QObject::tr("方案组不存在。"));
      auto g = groups[index].toMap();
      auto variants = g.value("variants").toList();
      int target = variantIndex(variants, variant);
      if (target < 0)
            return error(QObject::tr("写法不存在。"));
      if (g.value("active").toString() == variant)
            return {{"ok", true}, {"unchanged", true}};
      auto bytes = unpacked(variants[target].toMap().value("data"), &problem);
      if (!problem.isEmpty())
            return error(problem);
      g["active"] = variant;
      groups[index] = g;
      data["groups"] = groups;
      score->startCmd();
      score->undo(new ChangeState(score->masterScore(), data));
      bool ok = replace(score, g, bytes, &problem, variants[target].toMap());
      score->endCmd(!ok);
      return ok ? QVariantMap{{"ok", true}} : error(problem);
}
QVariantMap chooseMain(Score *score, const QString &group, const QString &variant) {
      if (!score || score->readOnly())
            return error(QObject::tr("请选择可编辑的总谱。"));
      auto data = state(score);
      auto groups = data.value("groups").toList();
      int index = groupIndex(groups, group);
      if (index < 0)
            return error(QObject::tr("方案组不存在。"));
      auto g = groups[index].toMap();
      if (variantIndex(g.value("variants").toList(), variant) < 0)
            return error(QObject::tr("写法不存在。"));
      g["main"] = variant;
      groups[index] = g;
      data["groups"] = groups;
      score->startCmd();
      score->undo(new ChangeState(score->masterScore(), data));
      score->endCmd();
      return {{"ok", true}};
}
QVariantMap rename(Score *score, const QString &group, const QString &variant, const QString &name) {
      if (!score || score->readOnly() || name.trimmed().isEmpty())
            return error(QObject::tr("请输入名称，并选择可编辑的总谱。"));
      auto data = state(score);
      auto groups = data.value("groups").toList();
      int index = groupIndex(groups, group);
      if (index < 0)
            return error(QObject::tr("方案组不存在。"));
      auto g = groups[index].toMap();
      if (variant.isEmpty())
            g["name"] = name.left(80);
      else {
            auto versions = g.value("variants").toList();
            int v = variantIndex(versions, variant);
            if (v < 0)
                  return error(QObject::tr("写法不存在。"));
            auto value = versions[v].toMap();
            value["name"] = name.left(80);
            versions[v] = value;
            g["variants"] = versions;
      }
      groups[index] = g;
      data["groups"] = groups;
      score->startCmd();
      score->undo(new ChangeState(score->masterScore(), data));
      score->endCmd();
      return {{"ok", true}};
}
bool save(Score *input, QIODevice *destination, const QString &filename, bool thumbnail) {
      auto score = input->masterScore();
      QString problem;
      auto data = capturedState(score, &problem);
      if (!problem.isEmpty()) {
            MScore::lastError = problem;
            return false;
      }
      auto groups = data.value("groups").toList();
      bool different = false;
      for (auto value : groups) {
            auto g = value.toMap();
            different = different || g.value("active") != g.value("main");
      }
      QScopedValueRollback<bool> guard(saving, true);
      QByteArray currentBytes;
      QBuffer current(&currentBytes);
      current.open(QIODevice::WriteOnly);
      if (!score->saveCompressedFile(&current, filename, false, thumbnail && !different))
            return false;
      current.close();
      QByteArray mainBytes = currentBytes;
      if (different) {
            // Reuse the complete native archive reader so excerpts, resources and personal values survive.
            QBuffer readerBuffer(&currentBytes);
            readerBuffer.open(QIODevice::ReadOnly);
            auto copy = std::unique_ptr<MasterScore>(new MasterScore(score->style()));
            if (copy->loadCompressedMsc(&readerBuffer, true) != Score::FileError::FILE_NO_ERROR) {
                  MScore::lastError = QObject::tr("不能建立用于保存主方案的完整乐谱副本。");
                  return false;
            }
            for (auto value : groups) {
                  auto g = value.toMap();
                  if (g.value("active") == g.value("main"))
                        continue;
                  auto variants = g.value("variants").toList();
                  int main = variantIndex(variants, g.value("main").toString());
                  if (main < 0) {
                        MScore::lastError = QObject::tr("主方案选择缺失，请重新指定。");
                        return false;
                  }
                  auto bytes = unpacked(variants[main].toMap().value("data"), &problem);
                  if (!problem.isEmpty()) {
                        MScore::lastError = problem;
                        return false;
                  }
                  copy->startCmd();
                  bool ok = replace(copy.get(), g, bytes, &problem, variants[main].toMap());
                  copy->endCmd(!ok);
                  if (!ok) {
                        MScore::lastError = problem;
                        return false;
                  }
            }
            mainBytes.clear();
            QBuffer mainBuffer(&mainBytes);
            mainBuffer.open(QIODevice::WriteOnly);
            if (!copy->saveCompressedFile(&mainBuffer, filename, false, thumbnail))
                  return false;
            mainBuffer.close();
      }
      // Include images referenced by an inactive fragment while they are still in the score's archive.
      QBuffer sourceBuffer(&currentBytes);
      sourceBuffer.open(QIODevice::ReadOnly);
      MQZipReader sourceZip(&sourceBuffer);
      QVariantMap assets = data.value("assets").toMap();
      for (auto info : sourceZip.fileInfoList())
            if (info.isFile && info.filePath.startsWith("Pictures/"))
                  assets[info.filePath] = QString::fromLatin1(sourceZip.fileData(info.filePath).toBase64());
      data["assets"] = assets;
      QByteArray manifest = QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
      if (manifest.size() > documentLimit) {
            MScore::lastError = QObject::tr("所有方案材料超过64MB，请拆分文件后保存。");
            return false;
      }
      QBuffer mainReader(&mainBytes);
      mainReader.open(QIODevice::ReadOnly);
      MQZipReader zip(&mainReader);
      MQZipWriter output(destination);
      for (auto info : zip.fileInfoList()) {
            if (info.isDir)
                  output.addDirectory(info.filePath);
            else if (info.isFile)
                  output.addFile(info.filePath, zip.fileData(info.filePath));
      }
      output.addFile("Kumo/variants/index.json", manifest);
      output.close();
      if (output.status() != MQZipWriter::NoError) {
            MScore::lastError = QObject::tr("方案文件写入失败，原文件保留。");
            return false;
      }
      return true;
}
QVariantMap expand(Score *score, const QString &group, int start, int end) {
      if (!score || score->readOnly())
            return error(QObject::tr("请选择可编辑的总谱。"));
      QString problem;
      auto data = capturedState(score, &problem);
      if (!problem.isEmpty())
            return error(problem + QObject::tr(" 请撤销跨界连接，扩大范围后再添加连接。"));
      auto groups = data.value("groups").toList();
      int index = groupIndex(groups, group);
      if (index < 0)
            return error(QObject::tr("方案组不存在。"));
      auto old = groups[index].toMap();
      auto scope = check(score, start, end, old.value("tracks").toList());
      if (scope.contains("error"))
            return scope;
      if (start > old.value("start").toInt() || end < old.value("end").toInt())
            return error(QObject::tr("扩大范围不能丢掉原来包含的小节。"));
      for (int i = 0; i < groups.size(); ++i)
            if (i != index) {
                  auto other = groups[i].toMap();
                  if (start < other.value("end").toInt() && end > other.value("start").toInt())
                        for (int track : trackSet(other.value("tracks").toList()))
                              if (trackSet(old.value("tracks").toList()).contains(track))
                                    return error(QObject::tr("扩大后的声部范围与另一组方案重叠。"));
            }
      QScopedValueRollback<bool> guard(saving, true);
      QByteArray snapshot;
      QBuffer output(&snapshot);
      output.open(QIODevice::WriteOnly);
      if (!score->saveCompressedFile(&output, "score.mscx", false, false))
            return error(MScore::lastError);
      output.close();
      auto expanded = old;
      expanded["start"] = start;
      expanded["end"] = end;
      expanded["signature"] = signature(score, start, end);
      auto versions = old.value("variants").toList();
      for (int i = 0; i < versions.size(); ++i) {
            QBuffer input(&snapshot);
            input.open(QIODevice::ReadOnly);
            std::unique_ptr<MasterScore> copy(new MasterScore(score->style()));
            if (copy->loadCompressedMsc(&input, true) != Score::FileError::FILE_NO_ERROR)
                  return error(QObject::tr("不能读取用于扩大范围的乐谱副本。"));
            auto v = versions[i].toMap();
            if (v.value("id") != old.value("active")) {
                  auto bytes = unpacked(v.value("data"), &problem);
                  if (!problem.isEmpty())
                        return error(problem);
                  copy->startCmd();
                  bool ok = replace(copy.get(), old, bytes, &problem, v);
                  copy->endCmd(!ok);
                  if (!ok)
                        return error(problem);
            }
            auto bytes = capture(copy.get(), start, end, old.value("tracks").toList(), &problem);
            if (!problem.isEmpty())
                  return error(problem);
            rememberImages(data, bytes);
            v["data"] = QString::fromLatin1(packed(bytes));
            v["connections"] = RegionalConnections::capture(copy.get(), start, end,
                                                            trackSet(old.value("tracks").toList()), &problem);
            if (!problem.isEmpty())
                  return error(problem);
            versions[i] = v;
      }
      expanded["variants"] = versions;
      groups[index] = expanded;
      data["groups"] = groups;
      score->startCmd();
      score->undo(new ChangeState(score->masterScore(), data));
      score->endCmd();
      return {{"ok", true}};
}
QVariantMap importVersion(Score *score, Score *source, const QString &group, const QString &name) {
      if (!score || score->readOnly() || !source)
            return error(QObject::tr("请选择可编辑的总谱和可读取的旧方案。"));
      auto data = state(score);
      auto groups = data.value("groups").toList();
      int index = groupIndex(groups, group);
      if (index < 0)
            return error(QObject::tr("先建立要接收旧写法的方案组。"));
      auto g = groups[index].toMap();
      int start = g.value("start").toInt(), end = g.value("end").toInt();
      if (source->nstaves() != score->nstaves() ||
          signature(source, start, end) != g.value("signature").toString())
            return error(QObject::tr("旧方案的谱表、拍号或小节长度不同，不能导入这一组。"));
      QString problem;
      auto bytes = capture(source, start, end, g.value("tracks").toList(), &problem);
      if (!problem.isEmpty())
            return error(problem);
      auto versions = g.value("variants").toList();
      if (versions.size() >= 32)
            return error(QObject::tr("这一组写法已达32个。"));
      rememberImages(data, bytes);
      QString key = id();
      versions.append(QVariantMap{
          {"id", key},
          {"name", name.left(80)},
          {"data", QString::fromLatin1(packed(bytes))},
          {"connections", RegionalConnections::capture(source, start, end,
                                                       trackSet(g.value("tracks").toList()), &problem)}});
      g["variants"] = versions;
      groups[index] = g;
      data["groups"] = groups;
      const bool ownCommand = !score->undoStack()->active();
      if (ownCommand) score->startCmd();
      score->undo(new ChangeState(score->masterScore(), data));
      if (ownCommand) score->endCmd();
      return {{"ok", true}, {"variant", key}};
}
bool exportOrdinary(Score *score, QIODevice *destination, const QString &filename, bool useMain) {
      QScopedValueRollback<bool> guard(saving, true);
      if (!useMain)
            return score->saveCompressedFile(destination, filename, false, false);
      QByteArray archive;
      QBuffer buffer(&archive);
      buffer.open(QIODevice::WriteOnly);
      if (!save(score, &buffer, filename, false))
            return false;
      buffer.close();
      QBuffer input(&archive);
      input.open(QIODevice::ReadOnly);
      MQZipReader zip(&input);
      MQZipWriter output(destination);
      for (auto item : zip.fileInfoList())
            if (item.isFile && !item.filePath.startsWith("Kumo/"))
                  output.addFile(item.filePath, zip.fileData(item.filePath));
      output.close();
      return output.status() == MQZipWriter::NoError;
}
bool restore(MasterScore *score, const QByteArray &bytes, QString *problem) {
      if (bytes.isEmpty())
            return true;
      if (bytes.size() > documentLimit) {
            *problem = QObject::tr("方案材料过大，不能安全读取。");
            return false;
      }
      QJsonParseError parse;
      auto document = QJsonDocument::fromJson(bytes, &parse);
      if (parse.error != QJsonParseError::NoError || !document.isObject()) {
            *problem = QObject::tr("方案目录损坏，请恢复以前的文件。");
            return false;
      }
      auto data = document.toVariant().toMap();
      if (data.value("schema").toInt() != 1 && data.value("schema").toInt() != 2) {
            *problem = QObject::tr("这份方案来自不同版本，请使用对应版本的软件。");
            return false;
      }
      auto groups = data.value("groups").toList();
      if (groups.size() > 64) {
            *problem = QObject::tr("方案组过多，不能读取。");
            return false;
      }
      QSet<QString> groupIds;
      QVector<QVariantMap> checkedGroups;
      for (auto value : groups) {
            auto g = value.toMap();
            QString group = g.value("id").toString();
            if (group.isEmpty() || groupIds.contains(group)) {
                  *problem = QObject::tr("方案组编号重复或缺失。");
                  return false;
            }
            groupIds.insert(group);
            auto scope =
                check(score, g.value("start").toInt(), g.value("end").toInt(), g.value("tracks").toList());
            if (scope.contains("error")) {
                  *problem = scope.value("error").toString();
                  return false;
            }
            if (g.value("signature").toString() !=
                signature(score, g.value("start").toInt(), g.value("end").toInt())) {
                  *problem = QObject::tr("方案拍号、总拍数或谱表与主乐谱不同。");
                  return false;
            }
            for (auto previous : checkedGroups)
                  if (g.value("start").toInt() < previous.value("end").toInt() &&
                      g.value("end").toInt() > previous.value("start").toInt())
                        for (int track : trackSet(g.value("tracks").toList()))
                              if (trackSet(previous.value("tracks").toList()).contains(track)) {
                                    *problem = QObject::tr("两个方案组占用了同一段声部。");
                                    return false;
                              }
            checkedGroups.append(g);
            auto variants = g.value("variants").toList();
            if (variants.isEmpty() || variants.size() > 32) {
                  *problem = QObject::tr("方案写法数量不正确。");
                  return false;
            }
            QSet<QString> ids;
            for (auto item : variants) {
                  auto v = item.toMap();
                  QString key = v.value("id").toString();
                  if (key.isEmpty() || ids.contains(key)) {
                        *problem = QObject::tr("写法编号重复或缺失。");
                        return false;
                  }
                  ids.insert(key);
                  auto xml = unpacked(v.value("data"), problem);
                  if (!problem->isEmpty())
                        return false;
                  if (!validateFragment(g, xml, problem))
                        return false;
                  if (v.contains("connections") &&
                      !RegionalConnections::validate(v.value("connections").toList(), g, problem))
                        return false;
            }
            if (!ids.contains(g.value("active").toString()) || !ids.contains(g.value("main").toString())) {
                  *problem = QObject::tr("当前或主方案选择不存在。");
                  return false;
            }
      }
      const auto combinations=data.value("combinations").toList();QSet<QString> combinationIds;
      if(combinations.size()>64){*problem=QObject::tr("组合数量超过64个。");return false;}
      for(auto value:combinations){const auto combination=value.toMap();const auto key=combination.value("id").toString();const auto choices=combination.value("choices").toMap();
            if(key.isEmpty()||combinationIds.contains(key)||choices.isEmpty()){*problem=QObject::tr("组合名称或写法记录不完整。");return false;}
            combinationIds.insert(key);
            for(auto i=choices.begin();i!=choices.end();++i){const int group=groupIndex(groups,i.key());
                  if(group<0||variantIndex(groups[group].toMap().value("variants").toList(),i.value().toString())<0){*problem=QObject::tr("组合中的写法已经改变，请恢复先前保存的文件。");return false;}
                  }
            }
      auto assets = data.value("assets").toMap();
      for (auto it = assets.begin(); it != assets.end(); ++it) {
            if (!it.key().startsWith("Pictures/") || it.key().contains("..") || it.key().contains('\\')) {
                  *problem = QObject::tr("方案图片路径不正确。");
                  return false;
            }
            auto image = QByteArray::fromBase64(it.value().toByteArray());
            if (image.size() > fragmentLimit) {
                  *problem = QObject::tr("方案图片过大。");
                  return false;
            }
            imageStore.add(it.key(), image);
      }
      score->QObject::setProperty(property, data);
      score->startCmd();
      bool ok = true;
      for (auto value : groups) {
            auto g = value.toMap();
            if (g.value("active") == g.value("main"))
                  continue;
            auto variants = g.value("variants").toList();
            int current = variantIndex(variants, g.value("active").toString());
            auto fragment = unpacked(variants[current].toMap().value("data"), problem);
            if (!problem->isEmpty() || !replace(score, g, fragment, problem, variants[current].toMap())) {
                  ok = false;
                  break;
            }
      }
      score->endCmd(!ok);
      if (!ok) {
            score->QObject::setProperty(property, QVariant());
            return false;
      }
      score->undoStack()->clearAfterLoad();
      return true;
}
#include "regionalvarianttools.inc"
} // namespace RegionalVariants
} // namespace Ms
