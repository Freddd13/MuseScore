// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "arrangementcontext.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "libmscore/chord.h"
#include "libmscore/clef.h"
#include "libmscore/instrument.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/repeatlist.h"
#include "libmscore/score.h"
#include "libmscore/staff.h"
#include <QHash>
#include <QPointer>
#include <QTextDocumentFragment>
#include <climits>

namespace Ms {
namespace {
struct MeasureNumbers {
    QPointer<Score> score;
    ScoreContentState state;
    QHash<const Measure *, int> ordinals;
};
MeasureNumbers &numbers(Score *score) {
    static MeasureNumbers cache;
    if (cache.score != score || cache.state != score->masterScore()->state()) {
        cache.score = score;
        cache.state = score->masterScore()->state();
        cache.ordinals.clear();
        int ordinal = 0;
        for (auto measure = score->firstMeasure(); measure; measure = measure->nextMeasure())
            cache.ordinals.insert(measure, ++ordinal);
    }
    return cache;
}
QString text(const QString &xml) { return QTextDocumentFragment::fromHtml(xml).toPlainText(); }
QString spelled(int pitch, int tpc) {
    return tpc2name(tpc, NoteSpellingType::STANDARD, NoteCaseType::CAPITAL)
        + QString::number((pitch - int(tpc2alter(tpc))) / 12 - 1);
}
}

int arrangementMeasureOrdinal(const Measure *measure) {
    return measure ? numbers(measure->score()).ordinals.value(measure) : 0;
}

QVariantMap arrangementSourceDetails(Score *score, const QVariantMap &scope) {
    const auto at = Fraction::fromTicks(scope.value("start").toInt());
    const int first = scope.value("firstTrack").toInt(), end = scope.value("endTrack").toInt();
    QVariantList parts, staves;
    for (auto part : score->parts()) {
        if (part->endTrack() <= first || part->startTrack() >= end) continue;
        auto instrument = part->instrument(at);
        QString name = part->partName();
        if (name.isEmpty()) name = part->instrumentName(at);
        QVariantList longNames, shortNames;
        for (const auto &entry : part->longNames(at)) longNames.append(QVariantMap{{"text",text(entry.name())},{"xml",entry.name()},{"position",entry.pos()}});
        for (const auto &entry : part->shortNames(at)) shortNames.append(QVariantMap{{"text",text(entry.name())},{"xml",entry.name()},{"position",entry.pos()}});
        const auto transpose = instrument->transpose();
        QVariantList changes;
        for (const auto &entry : *part->instruments())
            if (entry.first > at.ticks() && entry.first < scope.value("end").toInt()) {
                const auto interval=entry.second->transpose();
                changes.append(QVariantMap{{"tick",entry.first},{"instrumentId",entry.second->instrumentId()},
                    {"transpose",QVariantMap{{"chromatic",interval.chromatic},{"diatonic",interval.diatonic}}}});
            }
        parts.append(QVariantMap{{"id",part->startTrack()},{"firstTrack",part->startTrack()},{"endTrack",part->endTrack()},
            {"name",text(name)},{"longName",text(part->longName(at))},{"shortName",text(part->shortName(at))},
            {"longNames",longNames},{"shortNames",shortNames},{"instrumentId",part->instrumentId(at)},{"instrumentChanges",changes},
            {"transpose",QVariantMap{{"chromatic",transpose.chromatic},{"diatonic",transpose.diatonic}}},
            {"origin","nativeNotation"}});
        for (auto staff : *part->staves()) {
            const int index = staff->idx();
            if ((index+1)*VOICES <= first || index*VOICES >= end) continue;
            staves.append(QVariantMap{{"index",index},{"partId",part->startTrack()},{"indexInPart",part->staves()->indexOf(staff)},
                {"firstTrack",index*VOICES},{"endTrack",(index+1)*VOICES},{"partName",text(name)},
                {"name",QString("%1 · %2").arg(text(name)).arg(part->staves()->indexOf(staff)+1)},
                {"clef",int(staff->clef(at))},{"clefName",QString::fromLatin1(ClefInfo::tag(staff->clef(at)))},
                {"key",int(staff->key(at))},{"visible",staff->show()},
                {"origin","nativeNotation"}});
        }
    }
    return {{"parts",parts},{"staves",staves},{"measureCount",numbers(score).ordinals.size()},
        {"timeConventions",QVariantMap{{"ticksPerQuarter",DIVISION},{"rangeEnd","exclusive"},
             {"measureIndexBase",1},{"staffIndexBase",0},{"voiceIndexBase",0},
             {"scoreTime","written ticks"},{"writtenSeconds","single traversal of the score tempo map"},
             {"renderedSeconds","playback with repeats and pauses"},{"referenceAudioUnit","milliseconds"}}}};
}

void arrangementPitchDetails(QVariantMap &row, const Note *note) {
    row["notatedPitch"] = note->epitch(); row["notatedTpc"] = note->tpc();
    row["concertPitch"] = note->pitch(); row["concertTpc"] = note->tpc1();
    row["soundingPitch"] = note->ppitch();
    row["noteName"] = spelled(note->epitch(),note->tpc());
    row["concertName"] = spelled(note->pitch(),note->tpc1());
    if ((note->ppitch()-note->pitch())%12==0)
        row["soundingName"] = spelled(note->ppitch(),note->tpc1());
    row["staff"] = note->staffIdx(); row["voice"] = note->voice();
    row["displayStaff"] = note->chord()->vStaffIdx();
    row["measure"] = arrangementMeasureOrdinal(note->chord()->measure());
    row["displayMeasureNumber"] = note->chord()->measure()->no()+1;
    const auto part=note->part();
    row["partName"] = text(part->partName().isEmpty() ? part->instrumentName(note->chord()->tick()) : part->partName());
}

QVariantMap arrangementPlaybackRead(PluginAPI::ScoreObserver *observer, Score *score,
                                   const QString &action, const QVariantMap &request) {
    const auto snapshot = observer->analysisContentRevision();
    if (!score || request.value("snapshotId").toString() != snapshot)
        return {{"error",QObject::tr("乐谱已经更新，请重新读取这一段。")}};
    const auto state = observer->arrangementState({{"scope",request.value("scope")}});
    if (state.contains("error")) return state;
    const auto scope = state.value("scope").toMap();
    const auto revision = observer->analysisPlaybackRevision();
    if (request.contains("playbackRevision") && request.value("playbackRevision").toString()!=revision)
        return {{"error",QObject::tr("演奏设置已经改变，请重新读取演奏结果。")}};
    QVariantMap result;
    if (action == "arrangementPlaybackPrepare")
        result = observer->analysisPlaybackPrepare(scope.value("start").toInt(),scope.value("end").toInt(),revision);
    else {
        result = observer->analysisPlaybackPage(request.value("fromTick",INT_MIN).toInt(),
            scope.value("firstTrack").toInt(),scope.value("endTrack").toInt(),request.value("limit",64).toInt(),revision);
        if (!result.contains("error")) {
            auto events = result.value("events").toList();
            for (auto &value : events) {
                auto event = value.toMap();
                if (event.value("kind") != "pedal") {
                    QString id = event.value("id").toString();
                    if (event.value("grace").toBool())
                        id=QString("%1:%2:%3:g%4").arg(event.value("tick").toInt()).arg(event.value("track").toInt())
                            .arg(event.value("index").toInt()).arg(event.value("graceIndex").toInt());
                    event["ref"] = QVariantMap{{"snapshotId",snapshot},{"id",id}};
                }
                event["scoreTick"] = score->repeatList().utick2tick(event.value("nominalTick").toInt());
                value = event;
            }
            result["events"] = events;
        }
    }
    result["snapshotId"] = snapshot; result["playbackRevision"] = revision; result["scope"] = scope;
    return result;
}
}
