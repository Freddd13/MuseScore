// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "arrangement.h"
#include "featuregate.h"
#include "playbackpreview.h"
#include "regionalcomparison.h"
#include "temporaryscore.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/plugin/api/handmarkers.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "libmscore/chord.h"
#include "libmscore/beam.h"
#include "libmscore/articulation.h"
#include "libmscore/arpeggio.h"
#include "libmscore/fingering.h"
#include "libmscore/harmony.h"
#include "libmscore/repeatlist.h"
#include "libmscore/input.h"
#include "libmscore/measure.h"
#include "libmscore/part.h"
#include "libmscore/note.h"
#include "libmscore/notevelocity.h"
#include "libmscore/velocitybackup.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/regionalvariants.h"
#include "libmscore/regionalconnections.h"
#include "libmscore/segment.h"
#include "libmscore/select.h"
#include "libmscore/staff.h"
#include "libmscore/symbol.h"
#include "libmscore/tie.h"
#include "libmscore/tuplet.h"
#include "libmscore/tremolo.h"
#include "libmscore/undo.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QDataStream>
#include <QElapsedTimer>
#include <algorithm>
#include <memory>
#include <cmath>

namespace Ms {
namespace {
struct Option {
    QString id, snapshot, title, summary;
    QVariantMap scope, values, brief;
    QVariantList differences;
    QVariantMap preparation;
    std::shared_ptr<MasterScore> copy;
    bool used = false;
};
class Options final : public QObject {
  public:
    QList<Option> choices;
    QMap<QString, QVariantMap> operations;
    QMap<QString, QByteArray> operationInputs;
    QStringList operationOrder;
    QString adoptionText;
    QSet<QString> adopted;
    explicit Options(Score *score) : QObject(score) { setObjectName("kumoArrangementOptions"); }
};
Options *store(Score *score) {
    if (!score)
        return nullptr;
    for (auto child : score->children())
        if (auto value = dynamic_cast<Options *>(child))
            return value;
    return new Options(score);
}
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QVariantMap error(const QString &text) { return {{"error", text}}; }
QVariantList tracks(const QVariantMap &scope) {
    QVariantList out;
    for (int i = scope.value("firstTrack").toInt(); i < scope.value("endTrack").toInt(); ++i)
        out.append(i);
    return out;
}
Fraction fraction(const QVariant &value) {
    auto row = value.toMap();
    const int n = row.value("numerator").toInt(), d = row.value("denominator").toInt();
    return d > 0 && d <= 1000000 && qAbs(qint64(n)) <= 100000000 ? Fraction(n, d) / Fraction(4, 1)
                                                                 : Fraction(0, 0);
}
Note *noteFor(Score *score, const QString &id) {
    auto words = id.split(':');
    if (words.size() < 3 || words.size() > 4)
        return nullptr;
    bool a = false, b = false, c = false;
    int tick = words[0].toInt(&a), track = words[1].toInt(&b), index = words[2].toInt(&c);
    if (!a || !b || !c || tick < 0 || track < 0 || track >= score->ntracks() || index < 0)
        return nullptr;
    auto segment = score->tick2segment(Fraction::fromTicks(tick), false, SegmentType::ChordRest);
    auto e = segment && segment->tick().ticks() == tick ? segment->element(track) : nullptr;
    if (!e || !e->isChord())
        return nullptr;
    auto chord = toChord(e);
    if (words.size() == 4) {
        bool valid = false;
        int g = words[3].mid(1).toInt(&valid);
        if (!valid || !words[3].startsWith('g') || g < 0 || g >= int(chord->graceNotes().size()))
            return nullptr;
        chord = chord->graceNotes()[g];
    }
    return index < int(chord->notes().size()) ? chord->notes()[index] : nullptr;
}
bool contains(const QVariantMap &scope, int at, int track) {
    return at >= scope.value("start").toInt() && at < scope.value("end").toInt() &&
           track >= scope.value("firstTrack").toInt() && track < scope.value("endTrack").toInt();
}
QSet<int> protectedTracks(const QVariantMap &brief) {
    QSet<int> result;
    for (auto value : brief.value("preserveTracks").toList())
        result.insert(value.toInt());
    if (!brief.value("allowMelodyChanges").toBool() && !brief.contains("preserveTracks"))
        result.insert(0);
    return result;
}
QByteArray digest(Score *score, const QVariantMap &scope) {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    for (auto s = score->firstSegment(SegmentType::ChordRest);
         s && s->tick().ticks() < scope.value("end").toInt(); s = s->next1(SegmentType::ChordRest))
        if (s->tick().ticks() >= scope.value("start").toInt())
            for (auto value : tracks(scope)) {
                int track = value.toInt();
                auto e = s->element(track);
                if (!e || !e->isChordRest())
                    continue;
                auto cr = toChordRest(e);
                out << s->tick().toString() << track << cr->actualTicks().toString() << e->isRest();
                if (e->isChord())
                    for (auto n : toChord(e)->notes()) {
                        out << n->pitch() << n->tpc1() << n->tpc2() << int(n->veloType()) << n->veloOffset()
                            << bool(n->tieBack()) << bool(n->tieFor());
                        out<<int(n->chord()->playEventType());if(n->chord()->playEventType()==PlayEventType::User)out<<n->playEvents().size();if(n->chord()->playEventType()==PlayEventType::User)for(const auto &event:n->playEvents())out<<event.pitch()<<event.ontime()<<event.len();
                        for (auto a : n->el())
                            if (a->isFingering() || a->isSymbol())
                                out << a->mimeData(QPointF());
                    }
            }
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
QVariantList differences(Score *score, Score *other, const QVariantMap &scope) {
    QVariantList result;
    const int start = scope.value("start").toInt(), end = scope.value("end").toInt();
    auto values = [](Element *e) {
        QVariantMap result;
        if (!e || !e->isChordRest())
            return result;
        auto cr = toChordRest(e);
        result["duration"] = cr->actualTicks().toString();
        result["rest"] = e->isRest();
        QVariantList notes;
        if (e->isChord())
            for (auto n : toChord(e)->notes()) {
                QStringList marks, fingerings, otherMarks, hands;
                for (auto a : n->el()) {
                    marks << a->accessibleInfo();
                    if (a->isFingering())
                        fingerings << toFingering(a)->plainText();
                    else {
                        ElementList one;one.push_back(a);
                        auto handMarks=PluginAPI::readHandMarkers(one,cr->tick().ticks(),n->track(),-1);
                        if(handMarks.isEmpty())otherMarks << a->accessibleInfo();
                        for(const auto& value:handMarks){auto hand=value.toMap();hands << hand.value("hand").toString()+(hand.value("end").toBool()?":end":"");}
                    }
                }
                notes << QVariantMap{{"pitch", n->pitch()},
                                     {"tpc1", n->tpc1()},
                                     {"tpc2", n->tpc2()},
                                     {"velocityType", int(n->veloType())},
                                     {"velocityRaw", n->veloOffset()},
                                     {"marks", marks}, {"fingerings", fingerings}, {"hands",hands}, {"otherMarks", otherMarks}};
            }
        result["notes"] = notes;
        return result;
    };
    for (auto v : tracks(scope)) {
        int track = v.toInt();
        QSet<int> positions;
        for (auto s : {score, other})
            for (auto segment = s->firstSegment(SegmentType::ChordRest);
                 segment && segment->tick().ticks() < end; segment = segment->next1(SegmentType::ChordRest))
                if (segment->tick().ticks() >= start && segment->element(track))
                    positions.insert(segment->tick().ticks());
        auto ordered = positions.values();
        std::sort(ordered.begin(), ordered.end());
        for (int tick : ordered) {
            auto valueAt = [&](Score *s) {
                auto segment = s->tick2segment(Fraction::fromTicks(tick), false, SegmentType::ChordRest);
                return values(segment && segment->tick().ticks() == tick ? segment->element(track) : nullptr);
            };
            auto a = valueAt(score), b = valueAt(other);
            if (a == b)
                continue;
            auto measure = score->tick2measure(Fraction::fromTicks(tick));
            result << QVariantMap{{"tick", tick},
                                  {"track", track},
                                  {"measure", measure->no() + 1},
                                  {"current", a},
                                  {"other", b}};
        }
    }
    return result;
}
bool mutate(Score *score, const Option &option, QString *problem) {
    const auto changes = option.values.value("changes").toList();
    const auto replace = option.values.value("voices").toList();
    const auto protect = protectedTracks(option.brief);
    QSet<QString> protectedNotes;
    for (auto value : option.brief.value("preserveNotes").toList())
        protectedNotes.insert(value.toMap().value("id", value).toString());
    struct Edit {
        Note *note;
        QVariantMap data;
    };
    QList<Edit> edits;
    QSet<Note *> seen;
    QSet<int> rebuilt, connectionTracks;
    // A note written on the other staff keeps its owning voice. Include the
    // destination in validation only; its existing notes remain untouched.
    auto supportingTracks = [&](int track) {
        QSet<int> result{track};
        const int start = option.scope.value("start").toInt();
        const int end = option.scope.value("end").toInt();
        for (auto segment = score->tick2segment(Fraction::fromTicks(start), false, SegmentType::ChordRest);
             segment && segment->tick().ticks() < end; segment = segment->next1(SegmentType::ChordRest)) {
            auto e = segment->element(track);
            if (e && e->isChord()) {
                auto chord = toChord(e);
                if (chord->staffMove())
                    result.insert(track + chord->staffMove() * VOICES);
                if (chord->beam())
                    for (auto member : chord->beam()->elements())
                        if (member->track() != track) {
                            *problem = QObject::tr("这组符杠连接了另一条声部，请把两条声部的节奏一起整理。");
                            return QVariantList{};
                        }
            }
        }
        connectionTracks.unite(result);
        QVariantList values;
        for (int value : result)
            values.append(value);
        return values;
    };
    struct TupletPlan {
        Fraction start, end;
        int actual, normal;
    };
    QMap<int, QList<TupletPlan>> tupletPlans;
    QMap<QString, QList<std::shared_ptr<Element>>> noteMarks, chordMarks;
    QMap<QString, QVariantMap> sourceAnchors;
    QSet<QString> capturedChords;
    QMap<QString, QPair<int, int>> sourceVelocity, sourceCurrentVelocity;
    QMap<QString,NoteEventList> sourceEvents;QSet<QString> sourceManual;
    QMap<QString,QPair<Note*,QVariantMap>> timingHeads;
    QMap<QString,QPair<Note*,QString>> copyHeads;
    QMap<QString, QMap<Pid, QVariant>> sourceProperties, chordProperties;
    QMap<QString, Note *> newHeads, newTails;
    auto retain = [](QList<std::shared_ptr<Element>> &list, Element *element) {
        auto item = element->clone();
        item->setParent(nullptr);
        list.append(std::shared_ptr<Element>(item));
    };
    if (changes.size() + replace.size() > 4000) {
        *problem = QObject::tr("这次变化较多，请分成几个谱段采用。");
        return false;
    }
    for (auto value : changes) {
        auto row = value.toMap(), ref = row.value("ref").toMap();
        auto id = ref.value("id").toString();
        auto n = noteFor(score, id);
        if (!n || seen.contains(n) || ref.value("snapshotId") != option.snapshot ||
            !contains(option.scope, n->chord()->tick().ticks(), n->track())) {
            *problem = QObject::tr("建议中的音符位置已经变化，请重新读取这一段。");
            return false;
        }
        if (row.contains("pitch") && (protectedNotes.contains(id) || protect.contains(n->track()))) {
            *problem = QObject::tr("建议改到了需要保留的旋律或声部，请调整写法或本次要求。");
            return false;
        }
        const QSet<QString> allowed{"ref", "pitch", "tpc1", "tpc2", "velocity", "finger", "hand", "handEnd"};
        for (auto key : row.keys())
            if (!allowed.contains(key)) {
                *problem = QObject::tr("建议中有尚未支持的音符修改：%1").arg(key);
                return false;
            }
        if (row.contains("pitch")) {
            const int pitch = row["pitch"].toInt();
            if (pitch < 0 || pitch > 127 || !row.contains("tpc1") || !row.contains("tpc2") ||
                row["tpc1"].toInt() < -1 || row["tpc1"].toInt() > 33 || row["tpc2"].toInt() < -1 ||
                row["tpc2"].toInt() > 33) {
                *problem = QObject::tr("请为新音高提供有效的音名拼写。");
                return false;
            }
            if ((tpc2pitch(row["tpc1"].toInt()) % 12 + 12) % 12 != pitch % 12) {
                *problem = QObject::tr("新音高和音名拼写不一致，请核对后提交。");
                return false;
            }
            if (option.brief.value("preserveHarmony", true).toBool() && pitch % 12 != n->pitch() % 12) {
                bool supported = false;
                const auto part = n->staff()->part();
                for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                    auto segment=n->chord()->segment();
                    while(segment && !segment->element(track))segment=segment->prev1(SegmentType::ChordRest);
                    auto e = segment?segment->element(track):nullptr;
                    if (e && e->isChord())
                        for (auto tone : toChord(e)->notes()) {
                            auto last=tone;QSet<Note*> heldChain;
                            while(last->tieFor() && last->tieFor()->endNote() && !heldChain.contains(last)) {
                                heldChain.insert(last);last=last->tieFor()->endNote();
                            }
                            supported |= tone->play() && last->chord()->endTick()>n->chord()->tick() && tone->pitch() % 12 == pitch % 12;
                        }
                }
                if (!supported) {
                    *problem = QObject::tr("这个新音高需要补充和声说明，请确认本段和弦后再提交。");
                    return false;
                }
            }
            if (n->tieBack() || n->tieFor()) {
                *problem = QObject::tr("这个音连着延音线。请把整条延音包含在同一段声部写法中。");
                return false;
            }
        }
        if (row.contains("velocity") && (row["velocity"].toInt() < 1 || row["velocity"].toInt() > 127)) {
            *problem = QObject::tr("音符力度应在1至127之间。");
            return false;
        }
        if (row.contains("finger") && (row["finger"].toInt() < 1 || row["finger"].toInt() > 5)) {
            *problem = QObject::tr("钢琴指法请使用1至5。");
            return false;
        }
        if (row.contains("hand") && row["hand"] != "left" && row["hand"] != "right") {
            *problem = QObject::tr("请选择左手或右手。");
            return false;
        }
        seen.insert(n);
        edits.append({n, row});
    }
    for (const auto &edit : edits)
        if (edit.data.contains("pitch")) {
            const int pitch = edit.data["pitch"].toInt();
            for (auto other : edit.note->chord()->notes())
                if (other != edit.note) {
                    int otherPitch = other->pitch();
                    for (const auto &changed : edits)
                        if (changed.note == other)
                            otherPitch = changed.data.value("pitch", otherPitch).toInt();
                    if (pitch == otherPitch && edit.note->pitch() != other->pitch()) {
                        *problem = QObject::tr("改写后同一和弦中出现重复音，请合并该音或调整音区。");
                        return false;
                    }
                }
        }
    // Each replacement describes a complete continuous voice within this scope.
    // Native note entry supplies gaps, rests, ties and linked-staff propagation.
    for (auto value : replace) {
        auto row = value.toMap();
        int track = row.value("track", -1).toInt();
        if (track < option.scope.value("firstTrack").toInt() ||
            track >= option.scope.value("endTrack").toInt() || protect.contains(track) ||
            rebuilt.contains(track)) {
            *problem = QObject::tr("请选择范围内可修改的声部，每条声部提交一种连续写法。");
            return false;
        }
        for (auto edit : edits)
            if (edit.note->track() == track) {
                *problem = QObject::tr("同一声部的逐音修改和整段写法请合并后提交。");
                return false;
            }
        for (const auto &id : protectedNotes) {
            auto n = noteFor(score, id);
            if (n && n->track() == track) {
                *problem = QObject::tr("这条声部包含要求保留的音，请保留它或缩小修改范围。");
                return false;
            }
        }
        const auto relatedTracks = supportingTracks(track);
        if (!problem->isEmpty())
            return false;
        auto checked = RegionalVariants::check(score, option.scope.value("start").toInt(),
                                               option.scope.value("end").toInt(), relatedTracks);
        if (checked.contains("error")) {
            *problem = checked.value("error").toString();
            return false;
        }
        Fraction cursor = Fraction::fromTicks(option.scope.value("start").toInt()),
                 end = Fraction::fromTicks(option.scope.value("end").toInt());
        Fraction previousEnd = cursor;
        for (auto value : row.value("tuplets").toList()) {
            auto t = value.toMap();
            auto from = fraction(t["quarterTime"]), duration = fraction(t["quarterDuration"]);
            int actual = t.value("actual", 3).toInt(), normal = t.value("normal", 2).toInt();
            if (!from.isValid() || !duration.isValid() || duration <= Fraction(0, 1) || from < previousEnd ||
                from + duration > end || actual < 2 || actual > 9 || normal < 1 || normal > 8 ||
                !TDuration::isValid(duration / Fraction(normal, 1))) {
                *problem = QObject::tr("连音组需要完整的起点、总拍数和比例，请按先后顺序提供。");
                return false;
            }
            auto m = score->tick2measure(from);
            if (!m || from + duration > m->endTick()) {
                *problem = QObject::tr("请把连音组放在同一小节内。");
                return false;
            }
            tupletPlans[track].append({from, from + duration, actual, normal});
            previousEnd = from + duration;
        }
        const auto events = row.value("events").toList();
        if (events.isEmpty() || events.size() > 4000) {
            *problem = QObject::tr("请提供这条声部的音符和休止符。");
            return false;
        }
        QSet<int> originalPcs;
        for (auto s = score->firstSegment(SegmentType::ChordRest); s && s->tick() < end;
             s = s->next1(SegmentType::ChordRest))
            if (s->tick() >= cursor) {
                auto e = s->element(track);
                if (e && e->isChord())
                    for (auto n : toChord(e)->notes())
                        originalPcs.insert(n->pitch() % 12);
            }
        for (int eventIndex = 0; eventIndex < events.size(); ++eventIndex) {
            auto e = events[eventIndex].toMap();
            auto duration = fraction(e.value("quarterDuration"));
            if (!duration.isValid() || duration <= Fraction(0, 1) || duration > Fraction(16, 1)) {
                *problem = QObject::tr("每个音或休止符都需要明确的时值。");
                return false;
            }
            if (e.contains("quarterTime") && fraction(e["quarterTime"]) != cursor) {
                *problem = QObject::tr("声部中有空缺或重叠，请用休止符补齐时间。");
                return false;
            }
            auto nominal = duration;
            for (const auto &t : tupletPlans[track]) {
                if (cursor < t.end && cursor + duration > t.start) {
                    if (cursor < t.start || cursor + duration > t.end) {
                        *problem = QObject::tr("这个音跨过了连音组边界，请用延音拆开后提交。");
                        return false;
                    }
                    nominal *= Fraction(t.actual, t.normal);
                }
            }
            const int denominator = nominal.denominator();
            if (denominator > 1024 || (denominator & (denominator - 1))) {
                *problem = QObject::tr("这个时值需要连音组，请同时提供连音组的范围和比例。");
                return false;
            }
            const auto pitches = e.value("pitches").toList();
            if (pitches.size() > 16) {
                *problem = QObject::tr("同一和弦的音过多，请拆分到不同声部。");
                return false;
            }
            if (e.value("tieNext").toBool()) {
                if (eventIndex + 1 >= events.size() || pitches.isEmpty()) {
                    *problem = QObject::tr("延音后还需要同一组音，请检查草稿时值。");
                    return false;
                }
                auto next = events[eventIndex + 1].toMap()["pitches"].toList();
                auto spelling = [](const QVariantList &ns) {
                    QStringList out;
                    for (auto v : ns) {
                        auto n = v.toMap();
                        out.append(QString("%1:%2:%3")
                                       .arg(n["pitch"].toInt())
                                       .arg(n["tpc1"].toInt())
                                       .arg(n["tpc2"].toInt()));
                    }
                    out.sort();
                    return out;
                };
                if (spelling(pitches) != spelling(next)) {
                    *problem = QObject::tr("延音两段的音高需要一致，请检查草稿。");
                    return false;
                }
            }
            QSet<int> distinct;
            for (auto value : pitches) {
                auto p = value.toMap();
                int pitch = p.value("pitch", -1).toInt();
                if (pitch < 0 || pitch > 127 || !p.contains("tpc1") || !p.contains("tpc2") ||
                    p["tpc1"].toInt() < -1 || p["tpc1"].toInt() > 33 || p["tpc2"].toInt() < -1 ||
                    p["tpc2"].toInt() > 33 || distinct.contains(pitch)) {
                    *problem = QObject::tr("请检查和弦中的音高和拼写。");
                    return false;
                }
                if ((tpc2pitch(p["tpc1"].toInt()) % 12 + 12) % 12 != pitch % 12 ||
                    (p.contains("velocity") && (p["velocity"].toInt() < 1 || p["velocity"].toInt() > 127))) {
                    *problem = QObject::tr("请核对新音符的音名拼写和力度。");
                    return false;
                }
                distinct.insert(pitch);
                if(p.value("tieNext").toBool()){
                    bool found=false;if(eventIndex+1<events.size())for(auto nextValue:events[eventIndex+1].toMap()["pitches"].toList()){const auto next=nextValue.toMap();found|=next["pitch"]==p["pitch"]&&next["tpc1"]==p["tpc1"]&&next["tpc2"]==p["tpc2"];}
                    if(!found){*problem=QObject::tr("延音后需要同一音高，请检查这个音的长度。");return false;}
                }
                if(p.contains("performance")){const auto t=p["performance"].toMap();const int offset=t["offset"].toInt(),length=t["duration"].toInt();if(qAbs(offset)>1920||length<1||length>64*DIVISION){*problem=QObject::tr("请把这段演奏细节分成较短的音再采用。");return false;}}
                const auto sourceId = p.value("sourceId",p.value("copySourceId")).toString();
                if (!sourceId.isEmpty() && !sourceAnchors.contains(sourceId)) {
                    auto origin = noteFor(score, sourceId);
                    if (!origin || (origin->track() != track && !(option.values.value("source").toMap().value("allowVoiceReassignment").toBool() && origin->track()/4==track/4 && !protect.contains(origin->track()))) ||
                        !contains(option.scope, origin->chord()->tick().ticks(), track)) {
                        *problem = QObject::tr("原音符已经变化，请重新读取音高。");
                        return false;
                    }
                    sourceAnchors[sourceId] = {{"tick", origin->chord()->tick().ticks()},
                                               {"track", track},
                                               {"pitch", origin->pitch()},
                                               {"tpc", origin->tpc1()}};
                    auto originalVelocity = VelocityBackup::original(origin);
                    sourceVelocity[sourceId] =
                        originalVelocity.first >= 0
                            ? originalVelocity
                            : QPair<int, int>{int(origin->veloType()), origin->veloOffset()};
                    sourceCurrentVelocity[sourceId] = {int(origin->veloType()), origin->veloOffset()};
                    sourceEvents[sourceId]=origin->playEvents();if(origin->chord()->playEventType()==PlayEventType::User)sourceManual.insert(sourceId);
                    for (auto pid : {Pid::COLOR, Pid::VISIBLE, Pid::SMALL, Pid::TUNING, Pid::HEAD_GROUP,
                                     Pid::HEAD_TYPE, Pid::HEAD_SCHEME, Pid::PLAY})
                        sourceProperties[sourceId][pid] = origin->getProperty(pid);
                    for (auto mark : origin->el())
                        retain(noteMarks[sourceId], mark);
                    auto old = origin->chord();
                    const auto chordKey = QString("%1:%2").arg(old->tick().ticks()).arg(track);
                    if (!capturedChords.contains(chordKey)) {
                        capturedChords.insert(chordKey);
                        for (auto pid : {Pid::STAFF_MOVE, Pid::STEM_DIRECTION, Pid::BEAM_MODE, Pid::SMALL})
                            chordProperties[sourceId][pid] = old->getProperty(pid);
                        if ((old->tremolo() && old->tremolo()->twoNotes()) || !old->lyrics().empty()) {
                            *problem = QObject::tr(
                                "这段带有歌词或连接两组音的震音，请先展开成普通音序，或选择另一条钢琴声部。");
                            return false;
                        }
                        for (auto mark : old->el())
                            retain(chordMarks[sourceId], mark);
                        for (auto mark : old->articulations())
                            retain(chordMarks[sourceId], mark);
                        if (old->arpeggio())
                            retain(chordMarks[sourceId], old->arpeggio());
                        if (old->tremolo())
                            retain(chordMarks[sourceId], old->tremolo());
                        for (auto grace : old->graceNotes())
                            retain(chordMarks[sourceId], grace);
                    }
                }
                if (option.brief.value("preserveHarmony", true).toBool() &&
                    !originalPcs.contains(pitch % 12)) {
                    *problem = QObject::tr("新写法加入了原声部以外的和声音，请确认本次采用的和声。");
                    return false;
                }
            }
            cursor += duration;
        }
        if (cursor != end) {
            *problem = QObject::tr("新写法的总拍数与选段不同，请补齐或调整时值。");
            return false;
        }
        rebuilt.insert(track);
    }
    auto connections = RegionalConnections::capture(score, option.scope["start"].toInt(),
                                                    option.scope["end"].toInt(), connectionTracks, problem);
    if (!problem->isEmpty())
        return false;
    QVariantList preserved;
    for (auto v : connections) {
        auto row = v.toMap();
        auto inside = [&](const QVariantMap &anchor) {
            return rebuilt.contains(anchor["track"].toInt()) &&
                   contains(option.scope, anchor["tick"].toInt(), anchor["track"].toInt());
        };
        if (!inside(row["start"].toMap()) && !inside(row["end"].toMap()))
            continue;
        if (row["type"] != "Tie" || !inside(row["start"].toMap()) || !inside(row["end"].toMap()) ||
            row["start"].toMap().value("grace", -1).toInt() >= 0 ||
            row["end"].toMap().value("grace", -1).toInt() >= 0)
            preserved.append(v);
    }
    if (!rebuilt.isEmpty())
        RegionalConnections::remove(score, option.scope["start"].toInt(), option.scope["end"].toInt(),
                                    rebuilt);
    for (auto edit : edits) {
        auto n = edit.note;
        const auto row = edit.data;
        if (row.contains("pitch"))
            score->undoChangePitch(n, row["pitch"].toInt(), row["tpc1"].toInt(), row["tpc2"].toInt());
        if (row.contains("velocity")) {
            VelocityBackup::remember(score, n);
            n->undoChangeProperty(Pid::VELO_TYPE, int(Note::ValueType::USER_VAL));
            n->undoChangeProperty(Pid::VELO_OFFSET, row["velocity"].toInt());
        }
        if (row.contains("finger")) {
            for (auto e : n->el())
                if (e->isFingering())
                    score->undoRemoveElement(e);
            auto text = new Fingering(score);
            text->setParent(n);
            text->setTrack(n->track());
            text->setXmlText(QString::number(row["finger"].toInt()));
            score->undoAddElement(text);
        }
        if (row.contains("hand")) {
            const auto oldMarks = n->el();
            for (auto e : oldMarks)
                if (e->isSymbol() && toSymbol(e)->handEligible() &&
                    toSymbol(e)->handEnd() == row.value("handEnd").toBool())
                    score->undoRemoveElement(e);
            auto symbol = new Symbol(score);
            symbol->setSym(row["hand"] == "left"
                               ? (row.value("handEnd").toBool() ? SymId::keyboardPlayWithLHEnd
                                                                : SymId::keyboardPlayWithLH)
                               : (row.value("handEnd").toBool() ? SymId::keyboardPlayWithRHEnd
                                                                : SymId::keyboardPlayWithRH));
            symbol->applyHandPreset();
            symbol->setParent(n);
            symbol->setTrack(n->track());
            score->undoAddElement(symbol);
        }
    }
    for (auto value : replace) {
        auto row = value.toMap();
        int track = row["track"].toInt();
        Fraction at = Fraction::fromTicks(option.scope["start"].toInt());
        InputState input = score->inputState();
        QSet<Tuplet *> oldTuplets;
        for (auto segment = score->tick2segment(at, false, SegmentType::ChordRest);
             segment && segment->tick().ticks() < option.scope["end"].toInt();
             segment = segment->next1(SegmentType::ChordRest)) {
            auto element = segment->element(track);
            if (!element || !element->isChordRest())
                continue;
            auto tuplet = toChordRest(element)->tuplet();
            if (!tuplet)
                continue;
            while (tuplet->tuplet())
                tuplet = tuplet->tuplet();
            oldTuplets.insert(tuplet);
        }
        for (auto tuplet : oldTuplets)
            score->cmdDeleteTuplet(tuplet, true);
        auto firstMeasure = score->tick2measure(at);
        auto firstSegment = firstMeasure->getSegment(SegmentType::ChordRest, at);
        score->setNoteRest(firstSegment, track, NoteVal(-1),
                           Fraction::fromTicks(option.scope["end"].toInt()) - at, Direction::AUTO, false,
                           true, &input);
        for (const auto &t : tupletPlans[track]) {
            auto measure = score->tick2measure(t.start);
            auto segment = measure->getSegment(SegmentType::ChordRest, t.start);
            score->setNoteRest(segment, track, NoteVal(-1), t.end - t.start, Direction::AUTO, false, false,
                               &input);
            auto cr = toChordRest(measure->getSegment(SegmentType::ChordRest, t.start)->element(track));
            auto tuplet = new Tuplet(score);
            tuplet->setParent(measure);
            tuplet->setTrack(track);
            tuplet->setTick(t.start);
            tuplet->setRatio(Fraction(t.actual, t.normal));
            tuplet->setTicks(t.end - t.start);
            tuplet->setBaseLen((t.end - t.start) / Fraction(t.normal, 1));
            score->cmdCreateTuplet(cr, tuplet);
        }
        QMap<int, Note *> tieFrom;
        for (auto value : row["events"].toList()) {
            auto event = value.toMap();
            auto duration = fraction(event["quarterDuration"]);
            auto pitches = event["pitches"].toList();
            auto measure = score->tick2measure(at);
            auto segment = measure ? measure->getSegment(SegmentType::ChordRest, at) : nullptr;
            if (!segment) {
                *problem = QObject::tr("新写法超出了这段乐谱。");
                return false;
            }
            NoteVal val(-1);
            if (!pitches.isEmpty()) {
                auto p = pitches.front().toMap();
                val.pitch = p["pitch"].toInt();
                val.tpc1 = p["tpc1"].toInt();
                val.tpc2 = p["tpc2"].toInt();
            }
            auto nominal = duration;
            bool inTuplet = false;
            for (const auto &t : tupletPlans[track])
                if (at >= t.start && at < t.end) {
                    nominal *= Fraction(t.actual, t.normal);
                    inTuplet = true;
                    break;
                }
            score->setNoteRest(segment, track, val, nominal, Direction::AUTO, false, !inTuplet, &input);
            const auto until = at + duration;
            // The primary tone can split at beats/barlines; double it across
            // those same tied pieces to keep each chord tone's duration.
            for (auto s = score->tick2segment(at, false, SegmentType::ChordRest); s && s->tick() < until;
                 s = s->next1(SegmentType::ChordRest)) {
                auto e = s->element(track);
                if (!e || !e->isChord())
                    continue;
                auto chord = toChord(e);
                for (int i = 1; i < pitches.size(); ++i) {
                    auto p = pitches[i].toMap();
                    auto n = new Note(score);
                    n->setParent(chord);
                    n->setTrack(track);
                    n->setPitch(p["pitch"].toInt());
                    n->setTpc1(p["tpc1"].toInt());
                    n->setTpc2(p["tpc2"].toInt());
                    score->undoAddElement(n);
                }
                for (auto n : chord->notes())
                    for (auto value : pitches) {
                        auto p = value.toMap();
                        if (p["pitch"].toInt() == n->pitch()) {
                            auto id = p.value("sourceId",p.value("copySourceId")).toString();
                            for (auto prop = chordProperties[id].cbegin(); prop != chordProperties[id].cend();
                                 ++prop)
                                if (chord->getProperty(prop.key()) != prop.value())
                                    chord->undoChangeProperty(prop.key(), prop.value());
                            for (auto prop = sourceProperties[id].cbegin();
                                 prop != sourceProperties[id].cend(); ++prop)
                                if (n->getProperty(prop.key()) != prop.value())
                                    n->undoChangeProperty(prop.key(), prop.value());
                            if (sourceVelocity.contains(id) && VelocityBackup::original(n).first < 0) {
                                n->undoChangeProperty(Pid::VELO_TYPE, sourceVelocity[id].first);
                                n->undoChangeProperty(Pid::VELO_OFFSET, sourceVelocity[id].second);
                                VelocityBackup::remember(score, n);
                            }
                            if (p.contains("velocity")) {
                                n->undoChangeProperty(Pid::VELO_TYPE, int(Note::ValueType::USER_VAL));
                                n->undoChangeProperty(Pid::VELO_OFFSET, p["velocity"].toInt());
                            } else if (sourceCurrentVelocity.contains(id)) {
                                n->undoChangeProperty(Pid::VELO_TYPE, sourceCurrentVelocity[id].first);
                                n->undoChangeProperty(Pid::VELO_OFFSET, sourceCurrentVelocity[id].second);
                            }
                        }
                    }
            }
            for (int i = 1; i < pitches.size(); ++i) {
                Note *previous = nullptr;
                for (auto s = score->tick2segment(at, false, SegmentType::ChordRest); s && s->tick() < until;
                     s = s->next1(SegmentType::ChordRest)) {
                    auto e = s->element(track);
                    if (!e || !e->isChord())
                        continue;
                    for (auto n : toChord(e)->notes())
                        if (n->pitch() == pitches[i].toMap()["pitch"].toInt()) {
                            if (previous) {
                                auto tie = new Tie(score);
                                tie->setStartNote(previous);
                                tie->setEndNote(n);
                                tie->setTrack(track);
                                tie->setTick(previous->tick());
                                tie->setTick2(n->tick());
                                score->undoAddElement(tie);
                            }
                            previous = n;
                            break;
                        }
                }
            }
            QMap<int, Note *> heads, tails;
            for (auto s = score->tick2segment(at, false, SegmentType::ChordRest); s && s->tick() < until;
                 s = s->next1(SegmentType::ChordRest)) {
                auto element = s->element(track);
                if (!element || !element->isChord())
                    continue;
                for (auto n : toChord(element)->notes()) {
                    if (!heads.contains(n->pitch()))
                        heads[n->pitch()] = n;
                    tails[n->pitch()] = n;
                }
            }
            for (auto pitch : heads.keys())
                if (tieFrom.contains(pitch)) {
                    auto before = tieFrom[pitch], after = heads[pitch];
                    auto tie = new Tie(score);
                    tie->setStartNote(before);
                    tie->setEndNote(after);
                    tie->setTrack(track);
                    tie->setTick(before->tick());
                    tie->setTick2(after->tick());
                    score->undoAddElement(tie);
                }
            for (auto value : pitches) {
                auto p = value.toMap();
                auto id = p["sourceId"].toString();
                int pitch = p["pitch"].toInt();
                const auto editId=p["editId"].toString();if(!editId.isEmpty()&&!p["copySourceId"].toString().isEmpty()&&heads.contains(pitch)&&!copyHeads.contains(editId))copyHeads[editId]={heads[pitch],p["copySourceId"].toString()};if(!editId.isEmpty()&&p.contains("performance")&&heads.contains(pitch)&&!timingHeads.contains(editId))timingHeads[editId]={heads[pitch],p["performance"].toMap()};
                if (id.isEmpty() || !heads.contains(pitch))
                    continue;
                if (!newHeads.contains(id)) {
                    newHeads[id] = heads[pitch];
                    for (const auto &prototype : noteMarks[id]) {
                        auto mark = prototype->clone();
                        mark->setParent(heads[pitch]);
                        mark->setTrack(track);
                        score->undoAddElement(mark);
                    }
                    for (const auto &prototype : chordMarks[id]) {
                        auto mark = prototype->clone();
                        mark->setParent(heads[pitch]->chord());
                        mark->setTrack(track);
                        score->undoAddElement(mark);
                    }
                }
                newTails[id] = tails[pitch];
            }
            tieFrom = event.value("tieNext").toBool() ? tails : QMap<int, Note *>();
            for(auto value:pitches){const auto p=value.toMap();if(p.value("tieNext").toBool()&&tails.contains(p["pitch"].toInt()))tieFrom[p["pitch"].toInt()]=tails[p["pitch"].toInt()];}
            at = until;
        }
    }
    for(auto i=newHeads.cbegin();i!=newHeads.cend();++i)if(sourceManual.contains(i.key())){i.value()->setPlayEvents(sourceEvents.value(i.key()));i.value()->chord()->setPlayEventType(PlayEventType::User);}
    for(auto copied:copyHeads){auto note=copied.first;const auto id=copied.second;for(const auto &prototype:noteMarks[id]){auto mark=prototype->clone();mark->setParent(note);mark->setTrack(note->track());score->undoAddElement(mark);}for(const auto &prototype:chordMarks[id]){auto mark=prototype->clone();mark->setParent(note->chord());mark->setTrack(note->track());score->undoAddElement(mark);}if(sourceManual.contains(id)){note->setPlayEvents(sourceEvents.value(id));note->chord()->setPlayEventType(PlayEventType::User);}}
    for(auto value:timingHeads){auto note=value.first;const auto timing=value.second;int remaining=timing["duration"].toInt();bool first=true;
        while(note){const int span=qMax(1,note->chord()->actualTicks().ticks());auto next=note->tieFor()?note->tieFor()->endNote():nullptr;const int length=next?qMin(span,remaining):remaining;NoteEventList events;events.clear();events.append(NoteEvent(0,first?(timing["offset"].toInt()>=0?int(std::ceil(double(timing["offset"].toInt())*1000/span)):int(std::floor(double(timing["offset"].toInt())*1000/span))):0,int(std::ceil(double(qMax(0,length))*1000/span))));note->setPlayEvents(events);note->chord()->setPlayEventType(PlayEventType::User);remaining=qMax(0,remaining-length);note=next;first=false;}
    }
    // Reattach lines to the same material when the rhythm moves its onsets.
    for (auto &value : preserved) {
        auto relation = value.toMap();
        for (const auto &name : QStringList{"start", "end"}) {
            auto anchor = relation[name].toMap();
            for (auto it = sourceAnchors.cbegin(); it != sourceAnchors.cend(); ++it) {
                auto old = it.value();
                if (old["tick"] != anchor["tick"] || old["track"] != anchor["track"])
                    continue;
                if (anchor.contains("pitch") && anchor.value("grace", -1).toInt() < 0 &&
                    (old["pitch"] != anchor["pitch"] || old["tpc"] != anchor["tpc"]))
                    continue;
                auto note = relation["type"] == "Tie" && name == "start" ? newTails.value(it.key())
                                                                         : newHeads.value(it.key());
                if (!note)
                    continue;
                anchor["tick"] = note->chord()->tick().ticks();
                anchor["track"] = note->track();
                if (anchor.contains("pitch") && anchor.value("grace", -1).toInt() < 0) {
                    anchor["pitch"] = note->pitch();
                    anchor["tpc"] = note->tpc1();
                }
                relation[name] = anchor;
                break;
            }
        }
        const int begin = relation["start"].toMap()["tick"].toInt(),
                  finish = relation["end"].toMap()["tick"].toInt();
        const bool grace = relation["start"].toMap().value("grace", -1).toInt() >= 0 ||
                           relation["end"].toMap().value("grace", -1).toInt() >= 0;
        if (begin > finish || begin == finish && !grace) {
            *problem = QObject::tr("草稿把连线两端放到了同一位置或调换了顺序，请调整相关音的拍位。");
            return false;
        }
        value = relation;
    }
    if (!preserved.isEmpty() && !RegionalConnections::restore(score, preserved, problem))
        return false;
    for (auto track : rebuilt)
        for (auto s = score->tick2segment(Fraction::fromTicks(option.scope["start"].toInt()), false,
                                          SegmentType::ChordRest);
             s && s->tick().ticks() < option.scope["end"].toInt(); s = s->next1(SegmentType::ChordRest)) {
            auto e = s->element(track);
            if (!e || !e->isChord())
                continue;
            for (auto n : toChord(e)->notes())
                if (n->tieFor() && n->tieFor()->endNote() &&
                    n->chord()->endTick() != n->tieFor()->endNote()->chord()->tick()) {
                    *problem = QObject::tr("新节奏改变了延音的连接位置，请连同延音两端一起调整写法。");
                    return false;
                }
        }
    return true;
}
QVariantMap summary(const Option &option) {
    return {{"id", option.id},
            {"snapshotId", option.snapshot},
            {"title", option.title},
            {"summary", option.summary},
            {"scope", option.scope},
            {"differences", option.differences},
            {"localSettings",option.brief.value("localSettings")},
            {"localKind",option.brief.value("localKind")},
            {"pianoSettings",option.brief.value("pianoSettings")},
            {"preparationMilliseconds", option.preparation},
            {"used", option.used}};
}
QSet<QString> adoptedOptions(Score *score) {
    auto cache=store(score);
    if(!cache)return {};
    const auto text=score->masterScore()->metaTag("kumo.arrangement");
    if(text.constData()!=cache->adoptionText.constData()&&text!=cache->adoptionText) {
        cache->adoptionText=text;
        cache->adopted.clear();
        const auto saved=QJsonDocument::fromJson(text.toUtf8()).object().toVariantMap();
        for(const auto &value:saved.value("adoptions").toList())cache->adopted.insert(value.toMap().value("id").toString());
    }
    return cache->adopted;
}
} // namespace

QString rollNotationIdentity(Score* score,const QVariantMap& scope){return score?QString::fromLatin1(digest(score,scope).toHex()):QString();}
QVariantMap applyRollNotation(PluginAPI::ScoreObserver* observer,Score* score,const QVariantMap& request){
    if(!personalFeaturesEnabled())return error(QObject::tr("个人扩展功能已关闭。"));
    if(!score||score->readOnly())return error(QObject::tr("打开可编辑的乐谱后写音。"));
    if(seq&&(!seq->backgroundRenderingIdle()||seq->isPlaying()))return error(QObject::tr("暂停播放后即可把这次写音保存到乐谱。"));
    if(request.value("snapshotId")!=observer->analysisContentRevision())return error(QObject::tr("乐谱刚有修改，请点“改用当前选区”重新读取后继续。"));
    const auto state=observer->arrangementState({{"scope",request["scope"]}});if(state.contains("error"))return state;
    Option option;option.snapshot=request["snapshotId"].toString();option.scope=state["scope"].toMap();option.values=request;option.brief={{"scope",option.scope},{"preserveTracks",request.value("lockedTracks",QVariantList{})},{"allowMelodyChanges",true},{"preserveHarmony",false}};
    const auto record=request.value("workflowRecord").toMap();if(QJsonDocument::fromVariant(record).toJson(QJsonDocument::Compact).size()>4*1024*1024)return error(QObject::tr("草稿较大，请按几个谱段保存。"));
    QString message;score->startCmd();const bool ok=mutate(score,option,&message);if(ok&&!record.isEmpty()){auto saved=record;auto drafts=saved["drafts"].toMap();const auto key=saved["activeId"].toString();auto entry=drafts[key].toMap();entry["identity"]=rollNotationIdentity(score,entry.value("scope",option.scope).toMap());entry["draft"]=false;drafts[key]=entry;saved["drafts"]=drafts;const auto text=QString::fromUtf8(QJsonDocument::fromVariant(saved).toJson(QJsonDocument::Compact));score->undo(new ChangeMetaText(score->masterScore(),"kumo.rollWriting",text));}score->endCmd(!ok);if(mscore&&mscore->currentScore()==score)mscore->endCmd();if(!ok)return error(message);
    if(mscore&&mscore->currentScore()==score)notifyArrangementSuggestions(score);return {{"ok",true},{"snapshotId",observer->analysisContentRevision()},{"identity",rollNotationIdentity(score,option.scope)}};
}

QVariantMap arrangementSuggestions(Score *score) {
    QVariantList items;
    const auto adopted=adoptedOptions(score);
    if (auto values = store(score))
        for (auto &option : values->choices) {
            option.used=adopted.contains(option.id);
            items.append(summary(option));
        }
    return {{"options", items}};
}
QVariantMap arrangementOperation(PluginAPI::ScoreObserver *observer, Score *score, const QString &action,
                                 const QVariantMap &request) {
    if (!score)
        return error(QObject::tr("打开乐谱后使用编配建议。"));
    auto values = store(score);
    const auto operation = request.value("operationId").toString();
    if (action == "arrangementOperationStatus")
        return values->operations.value(operation, {{"status", "unknown"}, {"operationId", operation}});
    const auto inputHash = QCryptographicHash::hash(
        action.toUtf8() + QJsonDocument::fromVariant(request).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256);
    if (!operation.isEmpty() && values->operations.contains(operation)) {
        if (values->operationInputs.value(operation) != inputHash)
            return error(QObject::tr("这个操作编号已经用于另一项修改，请为新操作使用新编号。"));
        return values->operations[operation];
    }
    auto execute = [&]() -> QVariantMap {
        if (action == "arrangementSuggestions")
            return arrangementSuggestions(score);
        if (action == "storeArrangementInfo") {
            if (request.value("snapshotId").toString() != observer->analysisContentRevision())
                return error(QObject::tr("乐谱已经更新，请重新读取后保存编曲资料。"));
            auto data = observer->workflowData("arrangement");
            const auto incoming = request.value("information").toMap();
            const QSet<QString> collections{"assertions", "materials", "alignments", "analysisRuns"};
            for (auto it = incoming.cbegin(); it != incoming.cend(); ++it) {
                if (it.key() == "schemaVersion")
                    continue;
                if (it.key() == "adoptions" && it.value() == data.value(it.key()))
                    continue;
                if (!collections.contains(it.key()))
                    return error(QObject::tr("请把编曲资料放入解释、材料、对应或分析记录中。"));
                if (it.value().userType() != QMetaType::QVariantList)
                    return error(QObject::tr("请按条目提供这类资料。"));
                auto rows = data.value(it.key()).toList();
                QSet<QString> incomingIds;
                for (auto value : it.value().toList()) {
                    auto row = value.toMap();
                    auto id = row.value("id").toString();
                    if (id.isEmpty() || id.size() > 120 || incomingIds.contains(id))
                        return error(QObject::tr("每条资料需要一个不同的编号，方便以后补充和修改。"));
                    if (row.value("origin").toMap().isEmpty() && row.value("provenance").toMap().isEmpty())
                        return error(QObject::tr("请给这条资料注明来源：用户说明、参考材料或算法结果。"));
                    incomingIds.insert(id);
                    if (!row.contains("createdAgainst"))
                        row["createdAgainst"] = observer->analysisContentRevision();
                    int index = -1;
                    for (int i = 0; i < rows.size(); ++i)
                        if (rows[i].toMap().value("id") == id) {
                            index = i;
                            break;
                        }
                    if (index < 0)
                        rows.append(row);
                    else {
                        auto existing = rows[index].toMap();
                        for (auto r = row.cbegin(); r != row.cend(); ++r)
                            existing[r.key()] = r.value();
                        rows[index] = existing;
                    }
                }
                if (rows.size() > 2048)
                    return error(QObject::tr("这类资料较多，请按作品分开保存。"));
                data[it.key()] = rows;
            }
            data["schemaVersion"] = "arrangement-information/1";
            auto result = observer->setWorkflowData("arrangement", data);
            result["snapshotId"] = observer->analysisContentRevision();
            return result;
        }
        if (action == "submitArrangementOptions") {
            const QString snapshot = request.value("snapshotId").toString();
            if (snapshot != observer->analysisContentRevision())
                return error(QObject::tr("乐谱已经更新，请按现在的谱子重新比较写法。"));
            if (!score->isMaster())
                return error(QObject::tr("请在总谱中比较编配写法。"));
            if (seq && (!seq->backgroundRenderingIdle() || seq->isPlaying()))
                return error(QObject::tr("停止播放后即可准备新的写法。"));
            const auto choices = request.value("options").toList();
            if (choices.isEmpty() || choices.size() > 3)
                return error(QObject::tr("每次提交一至三种写法。"));
            if (QJsonDocument::fromVariant(request).toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024)
                return error(QObject::tr("这次建议较长，请分段提交。"));
            QList<Option> prepared;
            for (auto value : choices) {
                auto row = value.toMap();
                auto state = observer->arrangementState({{"scope", row.value("scope")}});
                if (state.contains("error"))
                    return state;
                Option option;
                option.id = uuid();
                option.snapshot = snapshot;
                option.scope = state["scope"].toMap();
                option.title = row.value("title", QObject::tr("另一种写法")).toString().left(100);
                option.summary = row.value("summary").toString().left(2000);
                option.values = row;
                option.brief = request.value("taskBrief").toMap();
                const auto allowed = option.brief.value("scope").toMap();
                if (!allowed.isEmpty() &&
                    (option.scope["start"].toInt() < allowed["start"].toInt() ||
                     option.scope["end"].toInt() > allowed["end"].toInt() ||
                     option.scope["firstTrack"].toInt() < allowed["firstTrack"].toInt() ||
                     option.scope["endTrack"].toInt() > allowed["endTrack"].toInt()))
                    return error(QObject::tr("这版超出了本次编配范围，请重新选择范围后提交。"));
                QString problem;
                QElapsedTimer preparationClock;
                preparationClock.start();
                option.copy=std::shared_ptr<MasterScore>(score->masterScore()->clone(),TemporaryScoreDeleter{});
                option.preparation["cloneMs"] = double(preparationClock.nsecsElapsed()) / 1000000.;
                preparationClock.restart();
                if (!option.copy)
                    return error(QObject::tr("准备试听材料失败，请重试。"));
                option.copy->startCmd();
                const bool ok = mutate(option.copy.get(), option, &problem);
                option.copy->endCmd(!ok);
                if (!ok)
                    return error(problem);
                option.preparation["validationMs"] = double(preparationClock.nsecsElapsed()) / 1000000.;
                preparationClock.restart();
                option.copy->doLayout();
                option.preparation["layoutMs"] = double(preparationClock.nsecsElapsed()) / 1000000.;
                preparationClock.restart();
                option.differences = differences(score, option.copy.get(), option.scope);
                if (option.differences.isEmpty())
                    return error(QObject::tr("这版与当前写法相同，请补充具体变化。"));
                prepared.append(option);
            }
            values->choices = prepared;
            notifyArrangementSuggestions(score);
            if (request.value("showPanel", true).toBool())
                showSuggestionPanel(true);
            return arrangementSuggestions(score);
        }
        if (action == "arrangementOption") {
            const auto id = request.value("optionId").toString(), step = request.value("action").toString();
            if (step == "stop") {
                stopPlaybackPreview(true);
                if (mscore && mscore->currentScoreView())
                    showRegionalDifference(mscore->currentScoreView(), nullptr, {});
                return {{"ok", true}};
            }
            for (auto &option : values->choices)
                if (option.id == id) {
                    option.used=adoptedOptions(score).contains(option.id);
                    if (option.used)
                        return error(QObject::tr("这版已经采用，可以在乐谱中继续编辑，或按Ctrl+Z撤销。"));
                    if (option.snapshot != observer->analysisContentRevision())
                        return error(QObject::tr("乐谱已有修改，请让建议按现在的内容更新。"));
                    if (option.brief.contains("pianoSettings") && option.brief.value("pianoSettings").toMap()!=score->property("kumoPianoSettings").toMap())
                        return error(QObject::tr("分手或演奏检查设置已改变，请点“分析选段”更新建议。"));
                    if (mscore && mscore->currentScore() != score)
                        return error(QObject::tr("请先切回这份乐谱。"));
                    if (step == "preview") {
                        if (mscore && mscore->currentScoreView())
                            showRegionalDifference(mscore->currentScoreView(), option.copy.get(),
                                                   option.differences);
                        return {{"ok", true}, {"differences", option.differences}};
                    }
                    if (step == "performance") {
                        auto events = [&](Score *source) {
                            QVariantList result;
                            EventMap rendered;
                            source->renderMidi(&rendered, true, MScore::playRepeats,
                                               mscore->synthesizerState());
                            for (const auto &event : rendered) {
                                const auto &e = event.second;
                                const auto note = e.noteEventOwner() ? e.noteEventOwner() : e.note();
                                const int track = note          ? note->track()
                                                  : e.harmony() ? e.harmony()->track()
                                                                : -1;
                                const int tick = note ? note->chord()->tick().ticks()
                                                      : source->repeatList().utick2tick(event.first);
                                if (e.type() == ME_NOTEON && e.velo() > 0 &&
                                    contains(option.scope, tick, track))
                                    result.append(QVariantMap{{"unfoldedTick", event.first},
                                                              {"tick", tick},
                                                              {"track", track},
                                                              {"pitch", e.pitch()},
                                                              {"velocity", e.velo()},
                                                              {"channel", e.channel()}});
                                if (result.size() >= 20000)
                                    break;
                            }
                            return result;
                        };
                        return {{"original", events(score)},
                                {"proposed", events(option.copy.get())},
                                {"maximumAttacks", 20000}};
                    }
                    if (step == "audition" || step == "original") {
                        if (!playbackPreviewPrepared(score, option.id))
                            preparePlaybackPreview(score, option.id, renderPlaybackPreview(score, score),
                                                   renderPlaybackPreview(score, option.copy.get()));
                        auto range = option.scope;
                        if (request.value("exactStart").toBool())
                            range["exactStart"] = true;
                        return startPlaybackPreview(score, step == "audition", range);
                    }
                    if (seq && seq->isPlaying())
                        seq->stopWait();
                    stopPlaybackPreview(true);
                    if (seq && !seq->backgroundRenderingIdle())
                        return error(QObject::tr("正在准备声音，稍后即可采用。"));
                    if (step == "variant") {
                        QString group = request.value("groupId").toString();
                        QSet<int> affected;
                        for (auto v : option.values.value("changes").toList()) {
                            auto n = noteFor(score, v.toMap().value("ref").toMap().value("id").toString());
                            if (n)
                                affected.insert(n->track());
                        }
                        for (auto v : option.values.value("voices").toList())
                            affected.insert(v.toMap().value("track").toInt());
                        auto ordered = affected.values();
                        std::sort(ordered.begin(), ordered.end());
                        QVariantList targetTracks;
                        for (int t : ordered)
                            targetTracks.append(t);
                        if (group.isEmpty())
                            for (auto v : RegionalVariants::state(score).value("groups").toList()) {
                                auto g = v.toMap();
                                if (g["start"] == option.scope["start"] && g["end"] == option.scope["end"] &&
                                    g["tracks"].toList() == targetTracks) {
                                    group = g["id"].toString();
                                    break;
                                }
                            }
                        score->startCmd();
                        if (group.isEmpty()) {
                            auto created = RegionalVariants::create(score, option.scope["start"].toInt(),
                                                                    option.scope["end"].toInt(), targetTracks,
                                                                    option.title);
                            if (created.contains("error")) {
                                score->endCmd(true);
                                return created;
                            }
                            group = created.value("group").toString();
                        }
                        auto result =
                            RegionalVariants::importVersion(score, option.copy.get(), group, option.title);
                        score->endCmd(result.contains("error"));
                        if (!result.contains("error")) {
                            const auto revision = observer->analysisContentRevision();
                            const auto previous = option.snapshot;
                            for (auto &item : values->choices)
                                if (item.snapshot == previous) {
                                    item.snapshot = revision;
                                    auto edits = item.values.value("changes").toList();
                                    for (auto &v : edits) {
                                        auto row = v.toMap(), ref = row["ref"].toMap();
                                        ref["snapshotId"] = revision;
                                        row["ref"] = ref;
                                        v = row;
                                    }
                                    item.values["changes"] = edits;
                                }
                        }
                        notifyArrangementSuggestions(score);
                        return result;
                    }
                    if (step == "apply") {
                        if (score->readOnly())
                            return error(QObject::tr("另存为可编辑的乐谱后采用这版。"));
                        const auto expected = digest(option.copy.get(), option.scope);
                        QString problem;
                        score->startCmd();
                        bool ok = mutate(score, option, &problem);
                        if (ok && digest(score, option.scope) != expected) {
                            ok = false;
                            problem = QObject::tr("写入后的音符与预览不同，请重新准备这版。已恢复原写法。");
                        }
                        if (ok) {
                            auto saved = observer->workflowData("arrangement");
                            auto history = saved.value("adoptions").toList();
                            auto source = option.values.value("source").toMap();
                            if (source["kind"] == "rhythmTake" && !source.value("notes").toList().isEmpty()) {
                                auto materials = saved["materials"].toList();
                                auto id = "rhythm-" + source["sha256"].toString();
                                bool found = false;
                                for (auto v : materials)
                                    found |= v.toMap()["id"] == id;
                                if (!found)
                                    materials.append(
                                        QVariantMap{{"id", id},
                                                    {"origin", QVariantMap{{"kind", "midiRecording"},
                                                                           {"sha256", source["sha256"]}}},
                                                    {"path", source["path"]},
                                                    {"notes", source["notes"]}});
                                saved["materials"] = materials;
                                source.remove("notes");
                                source["recordingId"] = id;
                            }
                            history.append(QVariantMap{{"source", source},
                                                       {"id", option.id},
                                                       {"title", option.title},
                                                       {"sourceSnapshot", option.snapshot},
                                                       {"scope", option.scope},
                                                       {"summary", option.summary}});
                            while (history.size() > 100)
                                history.removeFirst();
                            saved["adoptions"] = history;
                            saved["schemaVersion"] = "arrangement-information/1";
                            const auto bytes =
                                QJsonDocument::fromVariant(saved).toJson(QJsonDocument::Compact);
                            if (bytes.size() > 4 * 1024 * 1024) {
                                ok = false;
                                problem = QObject::tr(
                                    "这份谱的编曲资料已超过4MiB，请另存作品或整理旧录音后再采用。");
                            } else
                                score->undo(new ChangeMetaText(score->masterScore(), "kumo.arrangement",
                                                               QString::fromUtf8(bytes)));
                        }
                        score->endCmd(!ok);
                        if (mscore)
                            mscore->endCmd();
                        if (!ok)
                            return error(problem);
                        option.used = true;
                        notifyArrangementSuggestions(score);
                        return {{"ok", true},
                                {"changed", option.differences.size()},
                                {"snapshotId", observer->analysisContentRevision()}};
                    }
                    return error(QObject::tr("请选择查看差异、试听、采用或存为另一种写法。"));
                }
            return error(QObject::tr("这项建议已经更新，请从列表重新选择。"));
        }
        return error(QObject::tr("未找到这项编配操作：%1").arg(action));
    };
    auto result = execute();
    if (!operation.isEmpty()) {
        result["operationId"] = operation;
        result["status"] = result.contains("error") ? "failed" : "completed";
        values->operations.insert(operation, result);
        values->operationInputs.insert(operation, inputHash);
        values->operationOrder.append(operation);
        while (values->operationOrder.size() > 32) {
            auto oldest = values->operationOrder.takeFirst();
            values->operations.remove(oldest);
            values->operationInputs.remove(oldest);
        }
    }
    return result;
}
} // namespace Ms
