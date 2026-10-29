// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "arrangement.h"
#include "featuregate.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/repeatlist.h"
#include "libmscore/select.h"
#include "libmscore/segment.h"
#include "libmscore/spanner.h"
#include "libmscore/spannermap.h"
#include "libmscore/staff.h"
#include "libmscore/tempo.h"
#include "libmscore/tie.h"
#include "libmscore/tuplet.h"
#include "mscore/performanceeditor/parameteredit.h"
#include "mscore/seq.h"
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <QCoreApplication>
#include <QElapsedTimer>
#ifdef Q_OS_WIN
#include <QSslConfiguration>
#endif

namespace Ms {
namespace PluginAPI {
namespace {
#ifdef Q_OS_WIN
bool bridgeCleanupRegistered = false;
void finishBridgeNetwork() {
    // Qt 5.15's default SSL/DTLS values otherwise survive until DLL teardown,
    // after OpenSSL's process cleanup. Release them while both libraries live.
    QSslConfiguration::setDefaultConfiguration(QSslConfiguration());
    QSslConfiguration::setDefaultDtlsConfiguration(QSslConfiguration());
    bridgeCleanupRegistered = false;
}
#endif
QVariantMap quarters(Fraction value) {
    value *= Fraction(4, 1);
    value.reduce();
    return {{"numerator", value.numerator()}, {"denominator", value.denominator()}};
}
QVariantMap scopeFor(Ms::Score *score, int at, const QVariantMap &request) {
    const int finish = score->lastMeasure()->endTick().ticks();
    auto scope = request.value("scope").toMap();
    if (scope.isEmpty()) {
        const auto &selection = score->selection();
        int from = qMax(0, at), until = from + 1, first = 0, last = score->ntracks();
        bool disjoint = false;
        if (selection.isRange()) {
            from = selection.tickStart().ticks();
            until = selection.tickEnd().ticks();
            first = selection.staffStart() * VOICES;
            last = selection.staffEnd() * VOICES;
        } else {
            int count = 0;
            for (auto e : selection.elements())
                if (e->isNote() || e->isChordRest()) {
                    auto cr = e->isNote() ? toNote(e)->chord() : toChordRest(e);
                    if (!count) {
                        from = cr->tick().ticks();
                        until = cr->endTick().ticks();
                        first = cr->track();
                        last = first + 1;
                    } else {
                        from = qMin(from, cr->tick().ticks());
                        until = qMax(until, cr->endTick().ticks());
                        first = qMin(first, cr->track());
                        last = qMax(last, cr->track() + 1);
                    }
                    ++count;
                }
            disjoint = count > 1;
            if (!count) {
                auto m = score->tick2measure(Fraction::fromTicks(qMin(from, finish - 1)));
                from = m->tick().ticks();
                until = m->endTick().ticks();
            }
        }
        scope = {{"start", from},
                 {"end", until},
                 {"firstTrack", first},
                 {"endTrack", last},
                 {"noncontiguous", disjoint}};
    }
    if (request.value("whole").toBool())
        scope = {{"start", 0}, {"end", finish}, {"firstTrack", 0}, {"endTrack", score->ntracks()}};
    if (request.contains("firstMeasure")) {
        const int first = request.value("firstMeasure").toInt(),
                  last = request.value("lastMeasure", score->nmeasures()).toInt();
        if (first < 1 || last < first || last > score->nmeasures())
            return {};
        auto m = score->firstMeasure();
        for (int i = 1; i < first; ++i)
            m = m->nextMeasure();
        scope["start"] = m->tick().ticks();
        for (int i = first; i < last; ++i)
            m = m->nextMeasure();
        scope["end"] = m->endTick().ticks();
    }
    const int start = scope.value("start", -1).toInt(), end = scope.value("end", -1).toInt();
    const int first = scope.value("firstTrack", -1).toInt(), last = scope.value("endTrack", -1).toInt();
    if (start < 0 || end <= start || end > finish || first < 0 || last <= first || last > score->ntracks())
        return {};
    auto m = score->tick2measure(Fraction::fromTicks(start));
    auto n = score->tick2measure(Fraction::fromTicks(end - 1));
    scope["firstMeasure"] = m->no() + 1;
    scope["lastMeasure"] = n->no() + 1;
    scope["quarterStart"] = quarters(Fraction::fromTicks(start));
    scope["quarterEnd"] = quarters(Fraction::fromTicks(end));
    return scope;
}
QVariantMap point(const Ms::Note *n) {
    const auto &notes = n->chord()->notes();
    return {{"tick", n->chord()->tick().ticks()},
            {"track", n->track()},
            {"index", int(std::find(notes.begin(), notes.end(), n) - notes.begin())}};
}
} // namespace

void ScoreObserver::prepareLocalBridge() {
#ifdef Q_OS_WIN
    if (!bridgeCleanupRegistered) {
        qAddPostRoutine(finishBridgeNetwork);
        bridgeCleanupRegistered = true;
    }
#endif
}

QVariantMap ScoreObserver::arrangementState(const QVariantMap &request) {
    if (!personalFeaturesEnabled()) return {{"error", tr("个人扩展功能已关闭。")}};
    if (!_score || !_score->lastMeasure())
        return {{"error", tr("打开乐谱后选择要编配的一段。")}};
    auto scope = scopeFor(_score, tick(), request);
    if (scope.isEmpty())
        return {{"error", tr("这个范围已经变化，请重新选择小节和声部。")}};
    const auto snapshot = analysisContentRevision();
    const auto scoreInfo = scoreIdentity();
    const auto harmonic = _score->property("kumoSharedHarmony").toMap();
    QVariantMap harmony = harmonic;
    harmony["freshness"] = harmonic.value("snapshotId") == snapshot ? "current" : "stale";
    if (harmonic.isEmpty())
        harmony = {{"availability", "notProvided"}, {"processing", "notRequested"}};
    return {{"schemaVersion", "arrangement-context/1"},
            {"scoreSessionId", analysisScoreId()},
            {"documentId", scoreInfo.value("filePath")},
            {"snapshotId", snapshot},
            {"title", scoreInfo.value("displayTitle")},
            {"identity", scoreInfo},
            {"scope", scope},
            {"ticksPerQuarter", DIVISION},
            {"playing", playing()},
            {"capabilities", QVariantMap{{"pagedNotes", true},
                                         {"exactQuarterTime", true},
                                         {"localOptions", true},
                                         {"geometry", true},
                                         {"nativeUndo", true}}},
            {"information", workflowData("arrangement")},
            {"reference", workflowData("reference")},
            {"manualHarmony", workflowData("harmony")},
            {"pianoSettings", _score->property("kumoPianoSettings")},
            {"harmonyAnalysis", harmony},
            {"coverage", QVariantMap{{"notation", "notesRestsAndListedRelations"},
                                     {"audioFeatures", "notRequested"},
                                     {"performance", "availableOnRequest"}}}};
}

QVariantMap ScoreObserver::arrangementRange(const QVariantMap &request) {
    if (!personalFeaturesEnabled()) return {{"error", tr("个人扩展功能已关闭。")}};
    QElapsedTimer work;
    work.start();
    if (!_score || request.value("snapshotId").toString() != analysisContentRevision())
        return {{"error", tr("乐谱已经更新，请重新读取这一段。")}};
    auto scope = scopeFor(_score, tick(), request);
    if (scope.isEmpty())
        return {{"error", tr("请选择有效的乐谱范围。")}};
    if (playing())
        return {{"error", tr("请暂停乐谱播放后读取完整声部。已有任务可以继续查看。")}};
    const auto id = analysisContentRevision();
    if (seq && !seq->backgroundRenderingIdle())
        return {{"error", tr("正在准备声音，稍后即可读取演奏数值。")}};
    if (_score->property("kumoArrangementVelocityRevision").toString() != id) {
        _score->updateVelo();
        _score->QObject::setProperty("kumoArrangementVelocityRevision", id);
    }
    const int start = scope.value("start").toInt(), end = scope.value("end").toInt();
    const int first = scope.value("firstTrack").toInt(), last = scope.value("endTrack").toInt();
    const int from = request.value("fromTick", start).toInt();
    auto page = analysisRange(start, end, first, last, from, request.value("limit", 64).toInt());
    if (page.contains("error"))
        return page;
    QVariantList notes, relations;
    const auto fields = request.value("fields").toStringList();
    auto complete = [&](QVariantMap row, Ms::Note *note, int grace = -1) {
        auto chord = note->chord();
        row["measure"] = chord->measure()->no() + 1;
        row["tuningCents"] = note->tuning();
        row["quarterWrittenDuration"] = quarters(chord->ticks());
        row["quarterTime"] = quarters(chord->tick());
        row["quarterDuration"] = quarters(chord->actualTicks());
        row["tpc1"] = note->tpc1();
        row["tpc2"] = note->tpc2();
        row["velocityType"] = int(note->veloType());
        row["velocityRaw"] = note->veloOffset();
        row["playEventType"]=int(chord->playEventType());QVariantList events;for(const auto &event:note->playEvents())events.append(QVariantMap{{"pitch",event.pitch()},{"onTime",event.ontime()},{"length",event.len()}});row["playEvents"]=events;
        const int base = NoteVelocity::referenceBase(note);
        row["velocityBase"] = base;
        row["velocity"] = NoteVelocity::effective(base, note->veloType(), note->veloOffset());
        row["graceIndex"] = grace;
        if (grace >= 0)
            row["id"] = row.value("id").toString() + QString(":g%1").arg(grace);
        row["ref"] = QVariantMap{{"snapshotId", id}, {"id", row.value("id")}};
        if (chord->tuplet())
            row["tuplet"] = QVariantMap{{"ratio", chord->tuplet()->ratio().toString()},
                                        {"start", chord->tuplet()->tick().ticks()},
                                        {"duration", chord->tuplet()->actualTicks().ticks()}};
        if (fields.contains("geometry"))
            row["location"] = scoreLocation(chord->tick().ticks(), note->track());
        if (note->tieBack())
            relations.append(
                QVariantMap{{"kind", "tie"},
                            {"start", point(note->tieBack()->startNote())},
                            {"end", point(note)},
                            {"crossesStart", note->tieBack()->startNote()->chord()->tick().ticks() < start}});
        if (note->tieFor() && note->tieFor()->endNote()->chord()->tick().ticks() >= end)
            relations.append(QVariantMap{{"kind", "tie"},
                                         {"start", point(note)},
                                         {"end", point(note->tieFor()->endNote())},
                                         {"crossesEnd", true}});
        notes.append(row);
    };
    QSet<Ms::Chord *> graceRead;
    for (auto value : page.value("notes").toList()) {
        auto row = value.toMap();
        auto note = resolve(row);
        if (!note)
            continue;
        complete(row, note);
        auto chord = note->chord();
        if (graceRead.contains(chord))
            continue;
        graceRead.insert(chord);
        for (int g = 0; g < int(chord->graceNotes().size()); ++g) {
            auto grace = chord->graceNotes()[g];
            int i = 0;
            for (auto n : grace->notes()) {
                auto item = describe(n, n->ppitch());
                item["tick"] = chord->tick().ticks();
                item["id"] = QString("%1:%2:%3").arg(chord->tick().ticks()).arg(chord->track()).arg(i++);
                item["duration"] = grace->actualTicks().ticks();
                item["grace"] = true;
                complete(item, n, g);
            }
        }
    }
    if (from == start && start > 0)
        for (int track = first; track < last; ++track) {
            auto cr = _score->findCR(Fraction::fromTicks(start - 1), track);
            if (!cr || !cr->isChord() || cr->tick().ticks() >= start || cr->endTick().ticks() <= start)
                continue;
            auto context = analysisRange(cr->tick().ticks(), start, track, track + 1, cr->tick().ticks(), 1);
            for (auto value : context.value("notes").toList()) {
                auto row = value.toMap();
                auto n = resolve(row);
                if (!n)
                    continue;
                row["contextOnly"] = true;
                complete(row, n);
            }
        }
    page["notes"] = notes;
    auto rests = page.value("rests").toList();
    for (auto &value : rests) {
        auto row = value.toMap();
        auto s = _score->tick2segment(Fraction::fromTicks(row.value("tick").toInt()), false,
                                      SegmentType::ChordRest);
        auto cr = s ? toChordRest(s->element(row.value("track").toInt())) : nullptr;
        if (cr) {
            row["quarterTime"] = quarters(cr->tick());
            row["quarterDuration"] = quarters(cr->actualTicks());
            row["duration"] = cr->actualTicks().ticks();
        }
        value = row;
    }
    page["rests"] = rests;
    if (from == start) {
        QVariantList tempos;
        tempos.append(QVariantMap{{"tick", start},
                                  {"bpm", _score->tempomap()->tempo(start) * 60.},
                                  {"seconds", _score->tempomap()->tick2time(start)},
                                  {"tempoScale", _score->tempomap()->relTempo()}});
        for (const auto &entry : *_score->tempomap())
            if (entry.first >= start && entry.first < end)
                tempos.append(QVariantMap{{"tick", entry.first},
                                          {"bpm", entry.second.tempo * 60.},
                                          {"seconds", entry.second.time},
                                          {"pauseSeconds", entry.second.pause},
                                          {"type", int(entry.second.type)}});
        page["tempos"] = tempos;
        for (auto entry : _score->spannerMap().findOverlapping(start, end - 1)) {
            auto line = entry.value;
            if (line->track() < first || line->track() >= last)
                continue;
            if (line->tick().ticks() < start || line->tick2().ticks() >= end)
                relations.append(QVariantMap{{"kind", line->name()},
                                             {"start", line->tick().ticks()},
                                             {"end", line->tick2().ticks()},
                                             {"track", line->track()},
                                             {"endTrack", line->track2()}});
        }
        QVariantList occurrences;
        int occurrence = 0;
        for (auto r : _score->repeatList())
            if (r->tick < end && r->tick + r->len() > start)
                occurrences.append(QVariantMap{{"occurrence", ++occurrence},
                                               {"start", qMax(start, r->tick)},
                                               {"end", qMin(end, r->tick + r->len())},
                                               {"unfoldedStart", r->utick + qMax(0, start - r->tick)},
                                               {"seconds", r->utime},
                                               {"playbackCount", r->playbackCount}});
        page["occurrences"] = occurrences;
        page["repeatCoverage"] = occurrences.isEmpty() ? "notPrepared" : "prepared";
    }
    page["boundaryRelations"] = relations;
    page["snapshotId"] = id;
    page["scope"] = scope;
    page["milliseconds"] = double(work.nsecsElapsed()) / 1000000.;
    return page;
}
void ScoreObserver::publishPianoSettings(const QVariantMap &settings) {
    if (!_score || QJsonDocument::fromVariant(settings).toJson(QJsonDocument::Compact).size() > 1024 * 1024)
        return;
    QVariantMap values;
    for (const auto &name : QStringList{"roles", "handOverrides", "left", "right", "minimumHoldMs", "style"})
        if (settings.contains(name))
            values[name] = settings[name];
    if(_score->property("kumoPianoSettings").toMap()!=values) {
        _score->QObject::setProperty("kumoPianoSettings", values);
        notifyArrangementSuggestions(_score);
    }
}
void ScoreObserver::publishHarmonyAnalysis(const QVariantMap &result) {
    if (!_score || result.value("snapshotId").toString() != analysisContentRevision())
        return;
    if (QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024)
        return;
    _score->QObject::setProperty("kumoSharedHarmony", result);
}
QVariantMap ScoreObserver::arrangementRequest(const QString &action, const QVariantMap &request) {
    if (!personalFeaturesEnabled()) return {{"error", tr("个人扩展功能已关闭。")}};
    if (action == "arrangementState")
        return arrangementState(request);
    if (action == "arrangementRange")
        return arrangementRange(request);
    if (action == "arrangementImage")
        return arrangementImage(request);
    return arrangementOperation(this, _score, action, request);
}
} // namespace PluginAPI
} // namespace Ms
