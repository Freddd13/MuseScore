// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mediaexport.h"
#include "audioreader.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QtConcurrent>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
ReferenceAudioExport::ReferenceAudioExport(QObject* parent) : QObject(parent)
{
    _pool.setMaxThreadCount(1);
}
ReferenceAudioExport::~ReferenceAudioExport()
{
    if (_cancel)
        *_cancel = true;
    _pool.waitForDone();
}
void ReferenceAudioExport::cancel()
{
    if (_cancel)
        *_cancel = true;
}
void ReferenceAudioExport::render(const QVariantMap& request)
{
    cancel();
    const auto generation = ++_generation;
    _cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = _cancel;
    _busy = true;
    _progress = 0;
    _status = tr("正在准备视频声音…");
    emit changed();
    QtConcurrent::run(&_pool, [this, request, generation, cancelled] {
        QElapsedTimer watch;
        watch.start();
        const auto url = QUrl(request.value("path").toString());
        const auto path = url.isLocalFile() ? url.toLocalFile() : request.value("path").toString();
        const auto destination = request.value("destination").toString();
        const int rate = request.value("sampleRate", 48000).toInt();
        const double duration = request.value("duration").toDouble();
        const auto pieces = request.value("pieces").toList();
        QString problem;
        if (rate < 8000 || rate > 192000 || !std::isfinite(duration) || duration <= 0 ||
            duration > 86400000 || pieces.size() > 4096 || destination.isEmpty())
            problem = tr("声音范围或采样率不正确。");
        QFileInfo file(path);
        const auto identity = request.value("identity").toMap();
        if (!file.isFile())
            problem = tr("找不到原曲：%1").arg(path);
        if (!identity.isEmpty() &&
            (identity.value("size").toLongLong() != file.size() ||
             identity.value("modified").toLongLong() != file.lastModified().toMSecsSinceEpoch()))
            problem = tr("原曲文件已改变，请重新选择音频。");
        double previous = 0;
        for (auto value : pieces) {
            const auto p = value.toMap();
            const double from = p.value("from").toDouble(), until = p.value("until").toDouble(),
                         at = p.value("at").toDouble(), length = p.value("duration").toDouble();
            if (!std::isfinite(from) || !std::isfinite(until) || !std::isfinite(at) ||
                !std::isfinite(length) || from < 0 || until <= from || length <= 0 ||
                at < previous - .01 || at + length > duration + .01 ||
                (until - from) / length < .5 || (until - from) / length > 1.5) {
                problem = tr("音频片段的起止或播放速度不正确。");
                break;
            }
            previous = at + length;
        }
        const qint64 total = qMax<qint64>(0, qRound64(duration * rate / 1000.));
        qint64 written = 0;
        QSaveFile output(destination);
        QElapsedTimer notify;
        notify.start();
        if (problem.isEmpty() && !output.open(QIODevice::WriteOnly))
            problem = tr("无法写入视频声音：%1").arg(output.errorString());
        auto write = [&](const float* data, int frames) {
            if (*cancelled)
                return false;
            const qint64 bytes = qint64(frames) * 2 * sizeof(float);
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
            QByteArray little(int(bytes), Qt::Uninitialized);
            for (int n = 0; n < frames * 2; ++n) {
                quint32 bits;
                std::memcpy(&bits, data + n, 4);
                qToLittleEndian(bits, reinterpret_cast<uchar*>(little.data() + n * 4));
            }
            const auto accepted = output.write(little);
#else
            const auto accepted=output.write(reinterpret_cast<const char*>(data),bytes);
#endif
            if (accepted != bytes) {
                problem = tr("声音写入未完成：%1").arg(output.errorString());
                return false;
            }
            written += frames;
            if (notify.elapsed() >= 100) {
                notify.restart();
                const double fraction = double(written) / qMax<qint64>(1, total);
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, fraction] {
                        if (generation != _generation)
                            return;
                        _progress = fraction;
                        emit changed();
                    },
                    Qt::QueuedConnection);
            }
            return !*cancelled;
        };
        std::array<float, 8192> silence{};
        auto zeros = [&](qint64 end) {
            while (written < end && !*cancelled && problem.isEmpty()) {
                const int frames = int(qMin<qint64>(4096, end - written));
                if (!write(silence.data(), frames))
                    return false;
            }
            return !*cancelled && problem.isEmpty();
        };
        if (problem.isEmpty())
            for (int index = 0; index < pieces.size() && !*cancelled; ++index) {
                const auto p = pieces[index].toMap();
                const qint64 start = qRound64(p.value("at").toDouble() * rate / 1000.);
                qint64 end = qRound64((p.value("at").toDouble() + p.value("duration").toDouble()) *
                                      rate / 1000.);
                if (!zeros(start))
                    break;
                std::vector<KumoAudio::Segment> sequence;
                auto append = [&](const QVariantMap& piece) {
                    sequence.push_back(
                        {piece.value("from").toDouble(), piece.value("until").toDouble(),
                         (piece.value("until").toDouble() - piece.value("from").toDouble()) /
                             piece.value("duration").toDouble(),
                         piece.value("fade", false).toBool()});
                };
                append(p);
                while (index + 1 < pieces.size()) {
                    const auto next = pieces[index + 1].toMap();
                    const auto nextStart = qRound64(next.value("at").toDouble() * rate / 1000.);
                    if (nextStart != end ||
                        std::abs(next.value("from").toDouble() - sequence.back().end) > .01)
                        break;
                    append(next);
                    end =
                        qRound64((next.value("at").toDouble() + next.value("duration").toDouble()) *
                                 rate / 1000.);
                    ++index;
                }
                QString error;
                const bool ok = KumoAudio::streamSequence(
                    path, sequence, rate,
                    [&](const float* data, int frames) {
                        frames = int(qMin<qint64>(frames, end - written));
                        return frames <= 0 ? !*cancelled : write(data, frames);
                    },
                    *cancelled, &error);
                if (!ok && !*cancelled) {
                    if (problem.isEmpty())
                        problem = error.isEmpty() ? tr("这段录音读取未完成。") : error;
                    break;
                }
                if (!zeros(end))
                    break;
            }
        if (problem.isEmpty() && !*cancelled)
            zeros(total);
        if (problem.isEmpty() && !*cancelled && !output.commit())
            problem = tr("视频声音未能保存：%1").arg(output.errorString());
        else if (!problem.isEmpty() || *cancelled)
            output.cancelWriting();
        QVariantMap result{{"path", destination},
                           {"sampleRate", rate},
                           {"frames", written},
                           {"duration", double(written) * 1000 / qMax(1, rate)},
                           {"milliseconds", watch.elapsed()},
                           {"cancelled", bool(*cancelled)}};
        if (!problem.isEmpty())
            result["error"] = problem;
        QMetaObject::invokeMethod(
            this,
            [this, generation, result] {
                if (generation != _generation)
                    return;
                _busy = false;
                _progress = result.contains("error") || result.value("cancelled").toBool() ? 0 : 1;
                _status = result.contains("error")             ? result.value("error").toString()
                          : result.value("cancelled").toBool() ? tr("声音准备已取消。")
                                                               : tr("视频声音已准备好。");
                emit changed();
                emit finished(result);
            },
            Qt::QueuedConnection);
    });
}
