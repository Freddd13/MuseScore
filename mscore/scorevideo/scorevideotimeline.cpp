// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "scorevideo.h"
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QUrl>
#include <algorithm>
#include <cmath>
namespace Ms
{
namespace
{
VideoClockPoint pointAtTick(const QVector<VideoClockPoint>& points, int tick)
{
    if (points.isEmpty())
        return {};
    auto next =
        std::upper_bound(points.cbegin(), points.cend(), tick,
                         [](int at, const VideoClockPoint& point) { return at < point.tick; });
    if (next == points.cbegin())
        return points.front();
    if (next == points.cend())
        return points.back();
    const auto previous = next - 1;
    const double ratio = double(tick - previous->tick) / qMax(1, next->tick - previous->tick);
    return {previous->nativeSeconds + ratio * (next->nativeSeconds - previous->nativeSeconds),
            previous->seconds + ratio * (next->seconds - previous->seconds),
            previous->x + ratio * (next->x - previous->x), tick};
}
double audioAt(const QVariantMap& association, int tick)
{
    auto points = association.value("anchors").toList();
    points.prepend(
        QVariantMap{{"tick", association.value("start")}, {"ms", association.value("from")}});
    points.append(
        QVariantMap{{"tick", association.value("end")}, {"ms", association.value("until")}});
    for (int i = 1; i < points.size(); ++i) {
        const auto a = points[i - 1].toMap(), b = points[i].toMap();
        if (tick <= b.value("tick").toInt())
            return a.value("ms").toDouble() +
                   (tick - a.value("tick").toInt()) *
                       (b.value("ms").toDouble() - a.value("ms").toDouble()) /
                       qMax(1, b.value("tick").toInt() - a.value("tick").toInt());
    }
    return association.value("until").toDouble();
}
double outputAt(const QVector<VideoClockPoint>& points, double native)
{
    if (points.isEmpty())
        return native;
    auto next = std::upper_bound(
        points.cbegin(), points.cend(), native,
        [](double at, const VideoClockPoint& point) { return at < point.nativeSeconds; });
    if (next == points.cbegin())
        return native + points.front().seconds - points.front().nativeSeconds;
    if (next == points.cend())
        return native + points.back().seconds - points.back().nativeSeconds;
    const auto previous = next - 1;
    const double length = next->nativeSeconds - previous->nativeSeconds;
    return previous->seconds + (length > 0 ? (native - previous->nativeSeconds) / length : 1.) *
                                   (next->seconds - previous->seconds);
}
QString audioFile(Score* score, const QVariantMap& track)
{
    const auto url = QUrl(track.value("path", track.value("originalPath")).toString());
    const auto original = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const auto relative = track.value("relativePath").toString();
    if (score && !relative.isEmpty()) {
        const auto candidate = QFileInfo(score->masterScore()->fileInfo()->absoluteFilePath())
                                   .absoluteDir()
                                   .filePath(relative);
        if (QFileInfo(candidate).isFile())
            return QFileInfo(candidate).absoluteFilePath();
    }
    return QFileInfo(original).absoluteFilePath();
}
} // namespace
QVariantList ScoreVideoDocument::audioSources() const
{
    QVariantList result;
    for (auto value : _reference.value("tracks").toList()) {
        const auto track = value.toMap();
        const auto path = audioFile(_source, track);
        result.append(QVariantMap{{"id", track.value("id")},
                                  {"name", track.value("name")},
                                  {"path", path},
                                  {"missing", !QFileInfo(path).isFile()},
                                  {"duration", track.value("duration")},
                                  {"associations", track.value("associations").toList().size()}});
    }
    return result;
}
void ScoreVideoDocument::rebuildTimeline()
{
    if (_native.cues.isEmpty())
        return;
    _snapshot = _native;
    _snapshot.settings = _settings;
    _snapshot.audioKind = _settings.value("audioKind", "score").toString();
    _snapshot.audioId = _settings.value("audioId").toString();
    _snapshot.audioPath.clear();
    _snapshot.audioPieces.clear();
    const double tail = qBound(0., _settings.value("tail", 1.5).toDouble(), 30.);
    _snapshot.duration = _native.duration + tail;
    QVariantMap track;
    if (_snapshot.audioKind == "reference")
        for (auto value : _reference.value("tracks").toList())
            if (value.toMap().value("id").toString() == _snapshot.audioId) {
                track = value.toMap();
                break;
            }
    if (_snapshot.audioKind == "reference") {
        const auto path = audioFile(_source, track);
        QFileInfo file(path);
        if (track.isEmpty() || !file.isFile()) {
            _status = track.isEmpty() ? tr("选择一条原曲音轨后即可继续。")
                                      : tr("找不到原曲：%1。请在原曲参考中找回文件。").arg(path);
            emit changed();
            return;
        }
        _snapshot.audioPath = path;
        _snapshot.audioIdentity = {{"size", file.size()},
                                   {"modified", file.lastModified().toMSecsSinceEpoch()}};
        const auto associations = track.value("associations").toList();
        const double fileDuration = track.value("duration").toDouble() / 1000.;
        if (associations.isEmpty() || _settings.value("audioMapping").toString() == "whole") {
            if (fileDuration <= 0) {
                _snapshot.audioPath.clear();
                _status = tr("在原曲参考中打开这条录音后，可以按整曲安排视频。");
                emit changed();
                return;
            }
            const double scale = fileDuration / qMax(.001, _native.duration);
            for (auto& cue : _snapshot.cues) {
                cue.clockSource = "estimate";
                cue.from = cue.automaticFrom *= scale;
                cue.until = cue.automaticUntil *= scale;
                for (auto& point : cue.clock)
                    point.seconds *= scale;
                for (auto& note : cue.notes) {
                    note.from *= scale;
                    note.until *= scale;
                }
            }
            _snapshot.duration = fileDuration + tail;
            _snapshot.audioPieces = {QVariantMap{{"from", 0.},
                                                 {"until", fileDuration * 1000},
                                                 {"at", 0.},
                                                 {"duration", fileDuration * 1000},
                                                 {"fade", false}}};
        } else {
            _snapshot.cues.clear();
            double cursor = 0;
            int missing = 0;
            double lastAudio = -1;
            for (const auto& original : _native.cues) {
                QVector<int> boundaries{original.start, original.end};
                for (auto value : associations) {
                    const auto a = value.toMap();
                    if (!a.value("active", true).toBool() || a.value("stale").toBool() ||
                        a.value("occurrence", 1).toInt() != original.occurrence)
                        continue;
                    for (const char* key : {"start", "end"}) {
                        const int tick = a.value(key).toInt();
                        if (tick > original.start && tick < original.end)
                            boundaries.append(tick);
                    }
                }
                std::sort(boundaries.begin(), boundaries.end());
                boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                                 boundaries.end());
                for (int i = 1; i < boundaries.size(); ++i) {
                    const int start = boundaries[i - 1], end = boundaries[i];
                    QVariantMap association;
                    for (auto value : associations) {
                        const auto a = value.toMap();
                        if (a.value("active", true).toBool() && !a.value("stale").toBool() &&
                            a.value("occurrence", 1).toInt() == original.occurrence &&
                            a.value("start").toInt() <= start && a.value("end").toInt() >= end) {
                            association = a;
                            break;
                        }
                    }
                    auto cue = original;
                    cue.start = start;
                    cue.end = end;
                    cue.id += QString(":%1-%2").arg(start).arg(end);
                    cue.clock.clear();
                    cue.notes.clear();
                    const auto from = pointAtTick(original.clock, start),
                               until = pointAtTick(original.clock, end);
                    cue.clock.append(from);
                    for (const auto& point : original.clock)
                        if (point.tick > start && point.tick < end)
                            cue.clock.append(point);
                    cue.clock.append(until);
                    for (auto value : association.value("anchors").toList()) {
                        const int tick = value.toMap().value("tick").toInt();
                        if (tick > start && tick < end)
                            cue.clock.append(pointAtTick(original.clock, tick));
                    }
                    std::stable_sort(cue.clock.begin(), cue.clock.end(),
                                     [](const VideoClockPoint& a, const VideoClockPoint& b) {
                                         return a.nativeSeconds < b.nativeSeconds;
                                     });
                    const double audioFrom = association.isEmpty()
                                                 ? 0
                                                 : audioAt(association, start) / 1000.,
                                 audioUntil =
                                     association.isEmpty() ? 0 : audioAt(association, end) / 1000.;
                    if (!association.isEmpty() && _snapshot.cues.isEmpty() && cursor == 0 &&
                        _settings.value("includeLead", true).toBool() && audioFrom > 0) {
                        _snapshot.audioPieces.append(QVariantMap{{"from", 0.},
                                                                 {"until", audioFrom * 1000},
                                                                 {"at", 0.},
                                                                 {"duration", audioFrom * 1000},
                                                                 {"fade", false}});
                        cursor += audioFrom;
                    }
                    const double duration =
                        association.isEmpty() ? qMax(.001, until.nativeSeconds - from.nativeSeconds)
                                              : audioUntil - audioFrom;
                    if (duration <= 0 || !std::isfinite(duration)) {
                        _snapshot.audioPath.clear();
                        _status = tr("原曲片段的起止顺序需要调整。");
                        emit changed();
                        return;
                    }
                    cue.automaticFrom = cue.from = _snapshot.cues.isEmpty() ? 0 : cursor;
                    cue.automaticUntil = cue.until = cursor + duration;
                    cue.clockSource = association.isEmpty() ? "score-gap" : "association";
                    cue.label = original.label + " · " +
                                (association.isEmpty()
                                     ? tr("待关联")
                                     : association.value("name", tr("原曲片段")).toString());
                    for (auto& point : cue.clock)
                        point.seconds =
                            cursor + (association.isEmpty()
                                          ? point.nativeSeconds - from.nativeSeconds
                                          : (audioAt(association, point.tick) / 1000. - audioFrom));
                    const double low = start == original.start ? original.automaticFrom
                                                               : from.nativeSeconds,
                                 high = end == original.end ? original.automaticUntil
                                                            : until.nativeSeconds;
                    for (auto note : original.notes)
                        if (note.until > low && note.from < high) {
                            note.from = outputAt(cue.clock, note.from);
                            note.until = outputAt(cue.clock, note.until);
                            cue.notes.append(note);
                        }
                    if (association.isEmpty()) {
                        ++missing;
                        lastAudio = -1;
                    } else {
                        _snapshot.audioPieces.append(QVariantMap{{"from", audioFrom * 1000},
                                                                 {"until", audioUntil * 1000},
                                                                 {"at", cursor * 1000},
                                                                 {"duration", duration * 1000},
                                                                 {"fade", false}});
                        lastAudio = audioUntil;
                    }
                    cursor += duration;
                    _snapshot.cues.append(cue);
                }
            }
            _snapshot.duration = cursor + tail;
            if (lastAudio >= 0 && fileDuration > lastAudio && tail > 0) {
                const double kept = qMin(tail, fileDuration - lastAudio);
                _snapshot.audioPieces.append(QVariantMap{{"from", lastAudio * 1000},
                                                         {"until", (lastAudio + kept) * 1000},
                                                         {"at", cursor * 1000},
                                                         {"duration", kept * 1000},
                                                         {"fade", false}});
            }
            _status =
                missing
                    ? tr("已按原曲关联安排；%1段留出乐谱时间，声音为空，可在原曲参考中补充关联。")
                          .arg(missing)
                    : tr("已按原曲关联与内部对齐点安排。");
        }
    }
    const auto sourceKey =
        _snapshot.audioKind == "reference" ? "reference/" + _snapshot.audioId : QString("score");
    const auto overrides = _settings.value("timingOverrides").toMap().value(sourceKey).toMap();
    const double minimum = qMin(1. / qMax(1, _settings.value("fps", 30).toInt()),
                                _snapshot.duration / qMax(1, _snapshot.cues.size() + 1) * .25);
    for (int i = 1; i < _snapshot.cues.size(); ++i) {
        auto& cue = _snapshot.cues[i];
        if (!overrides.contains(cue.id))
            continue;
        const double at = overrides.value(cue.id).toDouble();
        if (std::isfinite(at))
            cue.from = at;
    }
    for (int i = 1; i < _snapshot.cues.size(); ++i) {
        auto& cue = _snapshot.cues[i];
        cue.from = qBound(_snapshot.cues[i - 1].from + minimum, cue.from,
                          _snapshot.duration - (_snapshot.cues.size() - i) * minimum);
    }
    for (int i = 0; i < _snapshot.cues.size(); ++i) {
        auto& cue = _snapshot.cues[i];
        cue.until = i + 1 < _snapshot.cues.size() ? _snapshot.cues[i + 1].from : _snapshot.duration;
        const double clockUntil = i + 1 < _snapshot.cues.size() ? cue.until : cue.automaticUntil;
        if (cue.clockSource == "estimate" &&
            (cue.from != cue.automaticFrom || clockUntil != cue.automaticUntil)) {
            const double scale =
                (clockUntil - cue.from) / qMax(.001, cue.automaticUntil - cue.automaticFrom);
            auto move = [&](double time) { return cue.from + (time - cue.automaticFrom) * scale; };
            for (auto& point : cue.clock)
                point.seconds = move(point.seconds);
            for (auto& note : cue.notes) {
                note.from = move(note.from);
                note.until = move(note.until);
            }
        }
    }
}
} // namespace Ms
