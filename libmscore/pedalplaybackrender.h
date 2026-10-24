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
#include "audio/midi/event.h"
#include <QSet>
namespace Ms { namespace PedalPlayback {
inline bool enabled(Score* score) {
    for (const auto& row : score->spannerMap().map())
        if (row.second->isPedal() && effective(toPedal(row.second)).piano) return true;
    return false;
}
inline QSet<const Spanner*> render(Score* score, EventMap* events, const EventMap* lookahead,
    int from, int until, int offset) {
    QSet<const Spanner*> handled;
    std::map<const Part*, QVector<Pedal*>> parts;
    for (const auto& row : score->spannerMap().map()) if (row.second->isPedal())
        parts[row.second->part()].append(toPedal(row.second));
    const auto seconds = [score](int tick) { return PlaybackTiming::time(score, tick); };
    const auto tickAt = [score](double time) {
        return time < 0 ? qRound(time * DIVISION * score->tempo(Fraction(0, 1))) : score->utime2utick(time);
    };
    for (const auto& part : parts) {
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
            const auto pedal = pedals[i]; const auto& span = spans[i];
            handled.insert(pedal);
            const int channel = part.first->instrument(pedal->tick())->channel(pedal->staff()->channel(pedal->tick(), 0))->channel();
            auto insert = [&](int tick, int value) {
                NPlayEvent event(ME_CONTROLLER, channel, CTRL_SUSTAIN, value);
                event.setOriginatingStaff(pedal->staffIdx()); event.setGeneratedPedal(true); events->insert({tick, event});
            };
            if (pedal->tick().ticks() >= from && pedal->tick().ticks() < until) insert(span.down, 127);
            if (pedal->tick2().ticks() >= from && pedal->tick2().ticks() <= until)
                insert(qMin(span.up, score->repeatList().ticks()), 0);
        }
    }
    return handled;
}
}}
#endif
