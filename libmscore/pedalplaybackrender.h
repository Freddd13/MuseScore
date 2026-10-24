// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#ifndef MS_PEDALPLAYBACKRENDER_H
#define MS_PEDALPLAYBACKRENDER_H
#include "pedalplayback.h"
#include "score.h"
#include "staff.h"
#include "note.h"
#include "chord.h"
#include "repeatlist.h"
#include "playbacktiming.h"
#include "tempo.h"
#include "stafftextbase.h"
#include "segment.h"
#include "audio/midi/event.h"
#include <QSet>
namespace Ms { namespace PedalPlayback {
inline bool enabled(Score* score, const QSet<const Part*>& retained = {}) {
    QHash<QString,bool> modes;
    for (const auto& row : score->spannerMap().map()) {
        if (!row.second->isPedal() || retained.contains(row.second->part())) continue;
        const auto own = row.second->getProperty(Pid::PEDAL_PLAYBACK).toString();
        const auto data = own.isEmpty() ? partData(row.second->part()) : own;
        if (data.isEmpty()) continue;
        if (!modes.contains(data)) modes.insert(data, decode(data).piano);
        if (modes.value(data)) return true;
    }
    return false;
}
// Read authored controller sources once, so a later manual action protects the
// same instrument in both incremental playback and a full export.
inline QSet<const Part*> retainedParts(Score* score) {
    QSet<const Part*> result;
    if (!score->isMaster()) {
        const auto master = retainedParts(score->masterScore());
        for (auto part : score->parts()) if (master.contains(part->masterPart())) result.insert(part);
    }
    for (auto segment = score->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest)) {
        for (auto element : segment->annotations()) {
            if (!element->isStaffTextBase() || result.contains(element->part())) continue;
            auto instrument = element->part()->instrument(segment->tick());
            for (const auto& actions : *toStaffTextBase(element)->channelActions()) {
                if (actions.channel < 0 || actions.channel >= instrument->channel().size()) continue;
                for (const auto& name : actions.midiActionNames) {
                    auto list = instrument->midiAction(name, actions.channel); if (!list) continue;
                    for (const auto& event : list->events)
                        if (event.type() == ME_CONTROLLER && event.controller() == CTRL_SUSTAIN) result.insert(element->part());
                }
            }
        }
    }
    return result;
}
struct Planned { Pedal* source; Span span; int channel; };
inline QVector<Planned> plan(Score* score, const EventMap* events, const EventMap* lookahead,
    int offset, bool writtenTimeline = false, const QSet<const Part*>& retained = {}) {
    QVector<Planned> result;
    std::map<const Part*, QVector<Pedal*>> parts;
    for (const auto& row : score->spannerMap().map()) if (row.second->isPedal())
        parts[row.second->part()].append(toPedal(row.second));
    const auto seconds = [score, writtenTimeline](int tick) { return writtenTimeline ? score->tempomap()->tick2time(tick) : PlaybackTiming::time(score, tick); };
    const auto tickAt = [score, writtenTimeline](double time) {
        if (writtenTimeline) return PlaybackTiming::tickAtTime(score, time);
        return time < 0 ? qRound(time * DIVISION * score->tempo(Fraction(0, 1))) : score->utime2utick(time);
    };
    for (const auto& part : parts) {
        if (retained.contains(part.first)) continue;
        if (std::none_of(part.second.begin(), part.second.end(), [](const Pedal* p) { return effective(p).piano; })) continue;
        QVector<Attack> attacks; bool manual = false;
        auto collect = [&](const EventMap* source) {
            if (!source) return;
            for (const auto& row : *source) {
                const auto& event = row.second;
                const int staff = event.getOriginatingStaff();
                if (staff < 0 || staff >= score->nstaves() || score->staff(staff)->part() != part.first) continue;
                if (event.type() == ME_CONTROLLER && event.controller() == CTRL_SUSTAIN && !event.generatedPedal()) manual = true;
                if (event.type() == ME_NOTEON && event.velo() && event.note() && !event.note()->chord()->isGrace())
                    attacks.append({event.nominalTick(), row.first});
            }
        };
        collect(events); collect(lookahead);
        if (manual) continue;
        QVector<Span> spans;
        auto pedals = part.second;
        std::stable_sort(pedals.begin(), pedals.end(), [](const Pedal* a, const Pedal* b) { return a->tick() < b->tick(); });
        for (auto pedal : pedals) {
            const int begin = pedal->tick().ticks() + offset, end = pedal->tick2().ticks() + offset;
            if (!spans.isEmpty() && spans.back().up >= begin + 2) spans.back().up = begin + 2 - MScore::pedalEventsMinTicks;
            spans.append({begin, end, begin + 3 - MScore::pedalEventsMinTicks,
                end + 2 - MScore::pedalEventsMinTicks, effective(pedal)});
        }
        spans = schedule(spans, attacks, seconds, tickAt);
        for (int i = 0; i < pedals.size(); ++i) {
            const auto pedal = pedals[i]; auto span = spans[i];
            span.up = qMin(span.up, writtenTimeline && score->lastMeasure() ? score->lastMeasure()->endTick().ticks() : score->repeatList().ticks());
            const int channel = part.first->instrument(pedal->tick())->channel(pedal->staff()->channel(pedal->tick(), 0))->channel();
            result.append({pedal, span, channel});
        }
    }
    return result;
}
inline QSet<const Spanner*> render(Score* score, EventMap* events, const EventMap* lookahead,
    int from, int until, int offset, const QSet<const Part*>& retained = {}) {
    QSet<const Spanner*> handled;
    for (const auto& row : plan(score, events, lookahead, offset, false, retained)) {
        auto pedal = row.source; handled.insert(pedal);
        auto insert = [&](int tick, int value) {
            NPlayEvent event(ME_CONTROLLER, row.channel, CTRL_SUSTAIN, value);
            event.setOriginatingStaff(pedal->staffIdx()); event.setGeneratedPedal(true);
            event.setNominalTick((value ? pedal->tick() : pedal->tick2()).ticks() + offset); events->insert({tick, event});
        };
        if (pedal->tick().ticks() >= from && pedal->tick().ticks() < until) insert(row.span.down, 127);
        if (pedal->tick2().ticks() >= from && pedal->tick2().ticks() <= until)
            insert(qMin(row.span.up, score->repeatList().ticks()), 0);
    }
    return handled;
}
}}
#endif
