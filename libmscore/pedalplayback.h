// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#ifndef MS_PEDALPLAYBACK_H
#define MS_PEDALPLAYBACK_H
#include "pedal.h"
#include "part.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>
#include <functional>
#include <algorithm>
#include <map>
namespace Ms { namespace PedalPlayback {
struct Settings {
    bool piano = false;
    int releaseMs = 40;
    int downMs = 60;
    bool delayFirst = true;
};
inline Settings decode(const QString& data) {
    Settings s;
    if (data.isEmpty()) return s;
    const auto obj = QJsonDocument::fromJson(data.toUtf8()).object();
    s.piano = obj.value("piano").toBool();
    s.releaseMs = qBound(1, obj.value("releaseMs").toInt(40), 300);
    s.downMs = qBound(0, obj.value("downMs").toInt(60), 500);
    s.delayFirst = obj.value("delayFirst").toBool(true);
    return s;
}
inline QString encode(const Settings& s) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"version", 1}, {"piano", s.piano},
        {"releaseMs", s.releaseMs}, {"downMs", s.downMs}, {"delayFirst", s.delayFirst}}).toJson(QJsonDocument::Compact));
}
inline QString partData(const Part* part) {
    return part ? part->masterPart()->getProperty(Pid::PEDAL_PLAYBACK).toString() : QString();
}
inline Settings effective(const Pedal* pedal) {
    const auto own = pedal->getProperty(Pid::PEDAL_PLAYBACK).toString();
    return decode(own.isEmpty() ? partData(pedal->part()) : own);
}
// Value-only schedule. Sources stay in the renderer; this part can also serve previews.
struct Span { int from, until, down, up; Settings settings; };
struct Attack { int nominal, actual; };
inline QVector<Span> schedule(QVector<Span> spans, const QVector<Attack>& attacks,
    const std::function<double(int)>& seconds, const std::function<int(double)>& tickAt) {
    std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.from < b.from; });
    std::map<int, int> onset;
    for (const auto& a : attacks) {
        auto it = onset.find(a.nominal);
        if (it == onset.end()) onset.emplace(a.nominal, a.actual);
        else it->second = std::min(it->second, a.actual);
    }
    for (int i = 0; i < spans.size(); ++i) {
        auto& span = spans[i];
        if (!span.settings.piano) continue;
        auto attack = onset.find(span.from);
        // A manually moved endpoint between onsets keeps its exact written position.
        if (attack == onset.end()) continue;
        const bool connected = i > 0 && spans[i - 1].until >= span.from;
        if (!connected && !span.settings.delayFirst) continue;
        const int desired = tickAt(seconds(attack->second) + span.settings.downMs / 1000.);
        const int limit = span.until - 1;
        span.down = qBound(std::min(attack->second, span.from), desired, limit);
    }
    for (int i = 1; i < spans.size(); ++i) {
        auto& next = spans[i]; auto& previous = spans[i - 1];
        if (!next.settings.piano || previous.until < next.from || !onset.count(next.from)) continue;
        const int desired = tickAt(seconds(next.down) - next.settings.releaseMs / 1000.);
        // Short passages compress the lift; every held interval and up gap stays valid.
        if (next.down - previous.down >= 2)
            previous.up = qBound(previous.down + 1, desired, next.down - 1);
    }
    return spans;
}
}}
#endif
