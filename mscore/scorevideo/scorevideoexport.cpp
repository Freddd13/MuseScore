// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "scorevideoexport.h"
#include "mscore/musescore.h"
#include "mscore/preferences.h"
#include "mscore/workflow/referencefactory.h"
#include "libmscore/tempo.h"
#include <QApplication>
#include <QFile>
#include <QDataStream>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>
#include <QtConcurrent>
#include <cmath>
namespace Ms
{
ScoreVideoExport::ScoreVideoExport(QObject* parent) : QObject(parent)
{
    connect(&_worker, &QFutureWatcher<QVariantMap>::finished, this,
            [this] { finish(_worker.result()); });
}
ScoreVideoExport::~ScoreVideoExport()
{
    cancel();
    _worker.waitForFinished();
}
void ScoreVideoExport::cancel()
{
    if (_cancel)
        _cancel->store(true);
    if (_audioJob)
        QMetaObject::invokeMethod(_audioJob, "cancel");
}
void ScoreVideoExport::finish(const QVariantMap& result)
{
    _busy = false;
    _status = result.value("cancelled").toBool() ? tr("已取消。")
              : result.contains("error")         ? result.value("error").toString()
              : result.contains("previewPath")   ? tr("预览声音已准备好。")
                                                 : tr("视频已保存：%1").arg(_destination);
    if (!result.contains("error") && !result.value("cancelled").toBool())
        _progress = 1;
    _snapshot = {};
    _soundScore.reset();
    emit changed();
    emit finished(result);
}
bool ScoreVideoExport::start(ScoreVideoDocument* document, const QString& destination,
                             bool previewOnly)
{
    if (_busy || !document || !document->ready() || !document->current() || destination.isEmpty())
        return false;
    if (!previewOnly && scoreVideoEncoder().isEmpty()) {
        _status = tr("未找到视频编码工具。请检查程序目录中的 tools/video/ffmpeg.exe。");
        emit changed();
        return false;
    }
    _previewOnly = previewOnly;
    _document = document;
    _snapshot = document->snapshot();
    _destination = destination;
    _audio.clear();
    _cancel = std::make_shared<std::atomic_bool>(false);
    _busy = true;
    _progress = 0;
    _status = tr("正在准备视频声音…");
    emit changed();
    const auto audioFile = _snapshot.assets->filePath("video-audio.f32");
    if (_snapshot.audioKind == "reference") {
        if (!_audioJob) {
            _audioJob = createReferenceAudioExport(this);
            connect(_audioJob, SIGNAL(finished(QVariantMap)), this,
                    SLOT(audioFinished(QVariantMap)));
        }
        if (!_audioJob->property("available").toBool()) {
            finish({{"error", _audioJob->property("status").toString()}});
            return false;
        }
        const double fps = _snapshot.settings.value("fps", 30).toDouble();
        QVariantMap request{{"path", _snapshot.audioPath},
                            {"identity", _snapshot.audioIdentity},
                            {"destination", audioFile},
                            {"sampleRate", 48000},
                            {"duration", std::ceil(_snapshot.duration * fps) / fps * 1000},
                            {"pieces", _snapshot.audioPieces}};
        const bool accepted =
            QMetaObject::invokeMethod(_audioJob, "render", Q_ARG(QVariantMap, request));
        if (!accepted) {
            finish({{"error", _audioJob->property("status").toString()}});
            return false;
        }
    } else if (_snapshot.audioKind == "score") {
        _soundScore.reset(document->preparedScore()->clone());
        _soundScore->setExpandRepeats(_snapshot.settings.value("expandRepeats", true).toBool());
        _soundScore->tempomap()->setRelTempo(document->preparedScore()->tempomap()->relTempo());
        QTimer::singleShot(0, this, [this, audioFile] {
            if (!_document || _cancel->load()) {
                finish({{"cancelled", true}});
                return;
            }
            QProgressDialog progress(tr("正在准备乐谱声音…"), tr("取消"), 0, 100,
                                     qobject_cast<QWidget*>(parent()));
            progress.setWindowModality(Qt::ApplicationModal);
            progress.setMinimumDuration(0);
            progress.show();
            QFile file(audioFile);
            bool ok = true;
            _snapshot.sampleRate = preferences.getInt(PREF_EXPORT_AUDIO_SAMPLERATE);
            if (ok)
                ok = mscore->saveAudio(_soundScore.get(), &file, [&](float value) {
                    progress.setValue(qBound(0, int(value * 100), 99));
                    QApplication::processEvents();
                    return !progress.wasCanceled() && !_cancel->load();
                });
            file.close();
            if (progress.wasCanceled() || _cancel->load()) {
                finish({{"cancelled", true}});
                return;
            }
            if (!ok) {
                finish({{"error", tr("乐谱声音未能准备好，请检查当前音源。")}});
                return;
            }
            _audio = audioFile;
            encode();
        });
    } else
        encode();
    return true;
}
void ScoreVideoExport::audioFinished(QVariantMap result)
{
    if (!_busy)
        return;
    if (result.contains("error") || result.value("cancelled").toBool()) {
        finish(result);
        return;
    }
    _audio = result.value("path").toString();
    _snapshot.sampleRate = result.value("sampleRate", 48000).toInt();
    encode();
}
void ScoreVideoExport::encode()
{
    _status = _previewOnly ? tr("正在准备预览声音…") : tr("正在生成视频…");
    emit changed();
    const auto snapshot = _snapshot;
    const auto audio = _audio, destination = _destination, encoder = scoreVideoEncoder();
    const auto cancelled = _cancel;
    const bool previewOnly = _previewOnly;
    QPointer<ScoreVideoExport> owner(this);
    _worker.setFuture(QtConcurrent::run([snapshot, audio, destination, encoder, cancelled, owner,
                                         previewOnly]() -> QVariantMap {
        if (previewOnly) {
            if (audio.isEmpty())
                return {{"previewPath", QString()}};
            QFile input(audio);
            QSaveFile output(snapshot.assets->filePath("preview.wav"));
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
                return {{"error", QObject::tr("无法准备预览声音文件。")}};
            const auto fps = snapshot.settings.value("fps", 30).toInt();
            const quint64 size =
                quint64(std::ceil(snapshot.duration * fps) / fps * snapshot.sampleRate + .5) * 8;
            QDataStream header(&output);
            header.setByteOrder(QDataStream::LittleEndian);
            if (size + 36 > 0xffffffffULL) {
                header.writeRawData("RF64", 4);
                header << quint32(0xffffffff);
                header.writeRawData("WAVEds64", 8);
                header << quint32(28) << quint64(size + 72) << size << quint64(size / 8)
                       << quint32(0);
            } else {
                header.writeRawData("RIFF", 4);
                header << quint32(size + 36);
                header.writeRawData("WAVE", 4);
            }
            header.writeRawData("fmt ", 4);
            header << quint32(16) << quint16(3) << quint16(2) << quint32(snapshot.sampleRate)
                   << quint32(snapshot.sampleRate * 8) << quint16(8) << quint16(32);
            header.writeRawData("data", 4);
            header << quint32(size > 0xffffffffULL ? 0xffffffffULL : size);
            quint64 written = 0;
            const QByteArray silence(65536, 0);
            while (written < size) {
                if (cancelled->load()) {
                    output.cancelWriting();
                    return {{"cancelled", true}};
                }
                const auto length = qint64(qMin<quint64>(silence.size(), size - written));
                const auto block = input.atEnd() ? silence.left(int(length)) : input.read(length);
                if (block.isEmpty() || output.write(block) != block.size())
                    return {{"error", output.errorString()}};
                written += block.size();
            }
            if (!output.commit())
                return {{"error", output.errorString()}};
            return {{"previewPath", output.fileName()},
                    {"audioDuration", double(size) / 8 / snapshot.sampleRate}};
        }
        const auto settings = snapshot.settings;
        const int fps = settings.value("fps", 30).toInt(),
                  width = settings.value("width", 1920).toInt(),
                  height = settings.value("height", 1080).toInt();
        const qint64 frames = qMax<qint64>(1, qint64(std::ceil(snapshot.duration * fps)));
        const bool transparent = settings.value("transparent").toBool();
        if (!transparent && (width % 2 || height % 2))
            return {{"error", QObject::tr("MP4的宽、高请设为偶数像素。")}};
        const auto output = snapshot.assets->filePath(transparent ? "encoded.mov" : "encoded.mp4");
        QStringList args{"-hide_banner",
                         "-loglevel",
                         "error",
                         "-y",
                         "-f",
                         "rawvideo",
                         "-pixel_format",
                         "rgba",
                         "-video_size",
                         QString("%1x%2").arg(width).arg(height),
                         "-framerate",
                         QString::number(fps),
                         "-i",
                         "pipe:0"};
        if (!audio.isEmpty())
            args << "-f"
                 << "f32le"
                 << "-ar" << QString::number(snapshot.sampleRate) << "-ac"
                 << "2"
                 << "-i" << audio << "-af"
                 << QString("volume=%1,apad")
                        .arg(qBound(0., settings.value("volume", 1.).toDouble(), 2.), 0, 'f', 4);
        args << "-map"
             << "0:v:0";
        if (!audio.isEmpty())
            args << "-map"
                 << "1:a:0";
        if (transparent)
            args << "-c:v"
                 << "prores_ks"
                 << "-profile:v"
                 << "4"
                 << "-pix_fmt"
                 << "yuva444p10le"
                 << "-alpha_bits"
                 << "16";
        else
            args << "-c:v"
                 << "libx264"
                 << "-preset"
                 << "fast"
                 << "-crf"
                 << "18"
                 << "-pix_fmt"
                 << "yuv420p"
                 << "-movflags"
                 << "+faststart";
        if (!audio.isEmpty())
            args << "-c:a" << (transparent ? "pcm_s16le" : "aac");
        args << "-t" << QString::number(double(frames) / fps, 'f', 9) << output;
        QProcess process;
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(encoder, args);
        if (!process.waitForStarted(5000))
            return {{"error", QObject::tr("视频编码工具未能启动：%1").arg(process.errorString())}};
        QByteArray errors;
        VideoFrameCache cache;
        QElapsedTimer clock;
        clock.start();
        qint64 notified = 0;
        auto stop = [&] {
            process.kill();
            process.waitForFinished(3000);
        };
        auto drain = [&] {
            errors += process.readAllStandardError();
            if (errors.size() > 16000)
                errors = errors.right(16000);
        };
        for (qint64 frame = 0; frame < frames; ++frame) {
            if (cancelled->load()) {
                stop();
                return {{"cancelled", true}};
            }
            auto image = scoreVideoFrame(snapshot, double(frame) / fps, &cache)
                             .convertToFormat(QImage::Format_RGBA8888);
            if (image.isNull()) {
                stop();
                return {{"error", QObject::tr("第%1帧的谱行图像读取失败。").arg(frame)}};
            }
            const char* bytes = reinterpret_cast<const char*>(image.constBits());
            qint64 remaining = image.sizeInBytes();
            while (remaining > 0) {
                if (cancelled->load()) {
                    stop();
                    return {{"cancelled", true}};
                }
                drain();
                if (process.state() != QProcess::Running) {
                    drain();
                    return {
                        {"error", QObject::tr("视频编码中断：%1").arg(QString::fromUtf8(errors))}};
                }
                if (process.bytesToWrite() > image.sizeInBytes() * 2) {
                    process.waitForBytesWritten(30);
                    continue;
                }
                const auto written = process.write(bytes, qMin<qint64>(remaining, 1024 * 1024));
                if (written < 0) {
                    stop();
                    return {{"error", QObject::tr("无法向视频编码器传送画面。")}};
                }
                bytes += written;
                remaining -= written;
                if (written == 0)
                    process.waitForBytesWritten(30);
            }
            if (clock.elapsed() - notified > 100) {
                notified = clock.elapsed();
                if (owner)
                    QMetaObject::invokeMethod(
                        owner,
                        [owner, frame, frames] {
                            if (owner) {
                                owner->_progress = double(frame) / frames;
                                emit owner->changed();
                            }
                        },
                        Qt::QueuedConnection);
            }
        }
        process.closeWriteChannel();
        while (process.state() != QProcess::NotRunning) {
            if (cancelled->load()) {
                stop();
                return {{"cancelled", true}};
            }
            process.waitForFinished(50);
            drain();
        }
        drain();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return {{"error", QObject::tr("视频编码失败：%1").arg(QString::fromUtf8(errors))}};
        QFile input(output);
        QSaveFile target(destination);
        if (!input.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly))
            return {{"error", QObject::tr("无法写入视频文件：%1").arg(destination)}};
        while (!input.atEnd()) {
            if (cancelled->load()) {
                target.cancelWriting();
                return {{"cancelled", true}};
            }
            const auto bytes = input.read(1024 * 1024);
            if (target.write(bytes) != bytes.size()) {
                target.cancelWriting();
                return {{"error", target.errorString()}};
            }
        }
        if (!target.commit())
            return {{"error", target.errorString()}};
        return {{"path", destination},
                {"frames", frames},
                {"fps", fps},
                {"duration", double(frames) / fps},
                {"milliseconds", clock.elapsed()}};
    }));
}
} // namespace Ms
