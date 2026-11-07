// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "scorevideo.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <limits>
namespace Ms
{
QVariantMap defaultScoreVideoSettings()
{
    return {{"schema", 1},
            {"width", 1920},
            {"height", 1080},
            {"fps", 30},
            {"alignment", "top"},
            {"uniformScale", true},
            {"margin", 64},
            {"background", "#ffffff"},
            {"transparent", false},
            {"invert", false},
            {"cursor", true},
            {"highlight", false},
            {"cursorColor", "#2881d0"},
            {"highlightColor", "#2881d0"},
            {"cursorWidth", 2.},
            {"audioKind", "score"},
            {"audioId", QString()},
            {"audioMapping", "associations"},
            {"volume", 1.},
            {"tail", 1.5},
            {"includeLead", true},
            {"expandRepeats", true}};
}
int scoreVideoCueAt(const VideoSnapshot& snapshot, double seconds)
{
    if (snapshot.cues.isEmpty())
        return -1;
    const auto next =
        std::upper_bound(snapshot.cues.cbegin(), snapshot.cues.cend(), seconds,
                         [](double time, const VideoCue& cue) { return time < cue.from; });
    return next == snapshot.cues.cbegin() ? 0 : int(next - snapshot.cues.cbegin() - 1);
}
QRectF scoreVideoPictureRect(const VideoSnapshot& snapshot, int index, const QSize& size)
{
    if (index < 0 || index >= snapshot.pictures.size() || size.isEmpty())
        return {};
    const auto settings = snapshot.settings;
    const auto& picture = snapshot.pictures[index];
    if (picture.pixels.isEmpty())
        return {};
    const double previewScale =
        double(size.width()) / qMax(1, settings.value("width", 1920).toInt());
    const double margin = qBound(0., settings.value("margin", 64).toDouble() * previewScale,
                                 qMin(size.width(), size.height()) * .4);
    const double width = size.width() - 2 * margin, height = size.height() - 2 * margin;
    double before = 1, after = 1, above = 1, below = 1;
    for (const auto& group : snapshot.pictures) {
        before = qMax(before, group.firstStaffX);
        after = qMax(after, group.pixels.width() - group.firstStaffX);
        above = qMax(above, group.firstStaffY);
        below = qMax(below, group.pixels.height() - group.firstStaffY);
    }
    const bool upper = settings.value("alignment", "top").toString() == "top";
    const double common = qMin(width / (before + after), height / (above + below));
    const double anchor = margin + above * common;
    double scale = common;
    if (!settings.value("uniformScale", true).toBool()) {
        scale = qMin(width / picture.pixels.width(), height / picture.pixels.height());
        if (upper) {
            if (picture.firstStaffX > 0)
                scale = qMin(scale, before * common / picture.firstStaffX);
            if (picture.pixels.width() > picture.firstStaffX)
                scale = qMin(scale, (size.width() - margin - (margin + before * common)) /
                                        (picture.pixels.width() - picture.firstStaffX));
            if (picture.firstStaffY > 0)
                scale = qMin(scale, (anchor - margin) / picture.firstStaffY);
            if (picture.pixels.height() > picture.firstStaffY)
                scale = qMin(scale, (size.height() - margin - anchor) /
                                        (picture.pixels.height() - picture.firstStaffY));
        }
    }
    if (!std::isfinite(scale) || scale <= 0)
        return {};
    const QSizeF extent = QSizeF(picture.pixels) * scale;
    const auto offset = settings.value("pictureOffsets").toMap().value(picture.id).toMap();
    const double x = (upper ? margin + before * common - picture.firstStaffX * scale
                            : (size.width() - extent.width()) / 2.) +
                     offset.value("x").toDouble() * size.width() / 100.;
    const double y =
        (upper ? anchor - picture.firstStaffY * scale : (size.height() - extent.height()) / 2.) +
        offset.value("y").toDouble() * size.height() / 100.;
    return {QPointF(x, y), extent};
}
QImage scoreVideoFrame(const VideoSnapshot& snapshot, double seconds, VideoFrameCache* cache,
                       QSize size)
{
    if (!cache)
        return {};
    if (size.isEmpty())
        size = {snapshot.settings.value("width", 1920).toInt(),
                snapshot.settings.value("height", 1080).toInt()};
    const int cueIndex = scoreVideoCueAt(snapshot, seconds);
    if (cueIndex < 0 || size.width() < 2 || size.height() < 2 || size.width() > 7680 ||
        size.height() > 7680)
        return {};
    const auto& cue = snapshot.cues[cueIndex];
    if (cue.picture < 0 || cue.picture >= snapshot.pictures.size())
        return {};
    const auto& picture = snapshot.pictures[cue.picture];
    if (cache->file != picture.file || cache->ink.isNull()) {
        cache->ink = QImage(picture.file).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        cache->file = picture.file;
        cache->picture = cue.picture;
        cache->inverted = false;
    }
    if (cache->ink.isNull())
        return {};
    const bool inverse = snapshot.settings.value("invert", false).toBool();
    if (inverse != cache->inverted) {
        auto straight = cache->ink.convertToFormat(QImage::Format_ARGB32);
        straight.invertPixels(QImage::InvertRgb);
        cache->ink = straight.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        cache->inverted = inverse;
    }
    QImage frame(size, QImage::Format_ARGB32_Premultiplied);
    if (frame.isNull())
        return {};
    auto background = QColor(snapshot.settings.value("background", "#ffffff").toString());
    if (!background.isValid())
        background = Qt::white;
    if (inverse)
        background =
            QColor(255 - background.red(), 255 - background.green(), 255 - background.blue());
    frame.fill(snapshot.settings.value("transparent", false).toBool() ? Qt::transparent
                                                                      : background);
    const auto target = scoreVideoPictureRect(snapshot, cue.picture, size);
    if (target.isEmpty())
        return {};
    const double scale = target.width() / picture.pixels.width();
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(target.topLeft());
    painter.scale(scale, scale);
    painter.drawImage(QPointF(), cache->ink);
    if (snapshot.settings.value("highlight", false).toBool()) {
        const auto color = QColor(snapshot.settings.value("highlightColor", "#2881d0").toString());
        for (const auto& note : cue.notes)
            if (seconds >= note.from && seconds < note.until) {
                const auto box = note.box.toAlignedRect().intersected(cache->ink.rect());
                if (box.isEmpty())
                    continue;
                auto mask = cache->ink.copy(box);
                QPainter tint(&mask);
                tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(mask.rect(), color);
                tint.end();
                painter.drawImage(box.topLeft(), mask);
            }
    }
    if (snapshot.settings.value("cursor", true).toBool() && !cue.clock.isEmpty() &&
        seconds >= cue.clock.front().seconds && seconds <= cue.clock.back().seconds) {
        const auto next = std::upper_bound(
            cue.clock.cbegin(), cue.clock.cend(), seconds,
            [](double time, const VideoClockPoint& point) { return time < point.seconds; });
        double x = cue.clock.back().x;
        if (next != cue.clock.cend() && next != cue.clock.cbegin()) {
            const auto previous = next - 1;
            const double duration = next->seconds - previous->seconds;
            x = previous->x + (duration > 0 ? (seconds - previous->seconds) / duration : 1.) *
                                  (next->x - previous->x);
        } else if (next == cue.clock.cbegin())
            x = next->x;
        auto color = QColor(snapshot.settings.value("cursorColor", "#2881d0").toString());
        painter.setPen(
            QPen(color, qMax(.5, snapshot.settings.value("cursorWidth", 2.).toDouble()) / scale));
        painter.drawLine(QPointF(x, 0), QPointF(x, picture.pixels.height()));
    }
    painter.end();
    return frame;
}
QString scoreVideoEncoder()
{
    const auto custom = QSettings().value("scoreVideo/ffmpeg").toString();
    if (!custom.isEmpty() && QFileInfo(custom).isExecutable())
        return custom;
    const QDir directory(QCoreApplication::applicationDirPath());
    for (const auto& name : {QString("tools/video/ffmpeg.exe"), QString("ffmpeg.exe"),
                             QString("tools/video/ffmpeg"), QString("ffmpeg")}) {
        const auto path = directory.filePath(name);
        if (QFileInfo(path).isExecutable())
            return path;
    }
    return QStandardPaths::findExecutable("ffmpeg");
}
} // namespace Ms
