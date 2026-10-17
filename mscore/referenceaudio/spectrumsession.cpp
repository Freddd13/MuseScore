// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumsession.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QSaveFile>
#include <QBuffer>
#include <QFileInfo>
#include <QDataStream>
#include <QDateTime>
#include <QUrl>
#include <QElapsedTimer>
#include <QtConcurrent>

namespace {
QString defaultCache() {
#ifdef Q_OS_WIN
    if (QDir("E:/").exists())
        return "E:/MuseScoreCache";
#endif
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}
QString cachePath(const QString &directory, const QString &key) {
    return QDir(directory).filePath(key + ".kspec");
}
bool readCache(const QString &path, KumoAudio::SpectrumTile *tile) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_9);
    quint32 magic = 0;
    stream >> magic;
    if (magic != 0x4b535031)
        return false;
    stream >> tile->fromMs >> tile->untilMs >> tile->durationMs >> tile->lowPitch >> tile->highPitch >>
        tile->sampleRate;
    QByteArray png;
    stream >> png;
    if (stream.status() != QDataStream::Ok)
        return false;
    tile->image = QImage::fromData(png, "PNG");
    return !tile->image.isNull() && tile->image.width() <= 1024 && tile->image.height() <= 1024;
}
void writeCache(const QString &directory, const QString &key, const KumoAudio::SpectrumTile &tile,
                int limitMiB) {
    if (!limitMiB || tile.image.isNull() || !QDir().mkpath(directory))
        return;
    QSaveFile file(cachePath(directory, key));
    if (!file.open(QIODevice::WriteOnly))
        return;
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    tile.image.save(&buffer, "PNG");
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_9);
    stream << quint32(0x4b535031) << tile.fromMs << tile.untilMs << tile.durationMs << tile.lowPitch
           << tile.highPitch << tile.sampleRate << png;
    if (stream.status() != QDataStream::Ok || !file.commit())
        return;
    // Only this module's hashed cache files in its own versioned subdirectory.
    const auto files = QDir(directory).entryInfoList({"*.kspec"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
    qint64 used = 0;
    for (const auto &f : files)
        used += f.size();
    for (auto it = files.crbegin(); it != files.crend() && used > qint64(limitMiB) * 1024 * 1024; ++it) {
        if (it->completeBaseName().size() != 64)
            continue;
        if (QFile::remove(it->absoluteFilePath()))
            used -= it->size();
    }
}
} // namespace

ReferenceSpectrumSession::ReferenceSpectrumSession(QObject *parent) : QObject(parent), _memory(128 * 1024) {
    _pool.setMaxThreadCount(1);
    QSettings settings;
    _directory = QDir(settings.value("spectrum/cacheDirectory", defaultCache()).toString())
                     .filePath("kumo-spectrum-v1");
    _memory.setMaxCost(qBound(8, settings.value("spectrum/memoryMiB", 128).toInt(), 1024) * 1024);
    _diskMiB = qBound(0, settings.value("spectrum/diskMiB", 1024).toInt(), 16384);
    _status = tr("选择原曲音轨后查看频谱。");
}
ReferenceSpectrumSession::~ReferenceSpectrumSession() {
    if (_cancel)
        *_cancel = true;
    _pool.waitForDone();
}
void ReferenceSpectrumSession::cancel() {
    ++_generation;
    _pending.clear();
    if (_cancel)
        *_cancel = true;
}
QVariantMap ReferenceSpectrumSession::info() const {
    return {{"sourcePath", _publishedPath},   {"from", _tile.fromMs},
            {"until", _tile.untilMs},         {"duration", _tile.durationMs},
            {"lowPitch", _tile.lowPitch},     {"highPitch", _tile.highPitch},
            {"sampleRate", _tile.sampleRate}, {"computeMilliseconds", _elapsedMs}};
}
QVariantMap ReferenceSpectrumSession::cacheOptions() const {
    return {{"directory", QFileInfo(_directory).absolutePath()},
            {"memoryMiB", _memory.maxCost() / 1024},
            {"diskMiB", _diskMiB},
            {"usedMemoryKiB", _memory.totalCost()}};
}
void ReferenceSpectrumSession::setCacheOptions(const QString &directory, int memoryMiB, int diskMiB) {
    cancel();
    _directory = QDir(directory).filePath("kumo-spectrum-v1");
    _memory.setMaxCost(qBound(8, memoryMiB, 1024) * 1024);
    _diskMiB = qBound(0, diskMiB, 16384);
    QSettings s;
    s.setValue("spectrum/cacheDirectory", directory);
    s.setValue("spectrum/memoryMiB", _memory.maxCost() / 1024);
    s.setValue("spectrum/diskMiB", _diskMiB);
    emit cacheOptionsChanged();
}
void ReferenceSpectrumSession::request(const QVariantMap &values) {
    ++_generation;
    if (_busy) {
        if (_cancel)
            *_cancel = true;
        _pending = values;
        return;
    }
    begin(values);
}
void ReferenceSpectrumSession::begin(const QVariantMap &values) {
    KumoAudio::SpectrumRequest r;
    const auto path = values.value("path").toString();
    r.path = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    r.fromMs = values.value("from", 0).toDouble();
    r.untilMs = values.value("until", 8000).toDouble();
    r.columns = values.value("columns", 512).toInt();
    r.rows = values.value("rows", 352).toInt();
    r.fftSize = values.value("fftSize", 16384).toInt();
    r.lowPitch = values.value("lowPitch", 20.5).toDouble();
    r.highPitch = values.value("highPitch", 108.5).toDouble();
    r.tuning = values.value("tuning", 440).toDouble();
    const auto key = KumoAudio::spectrumKey(r);
    if (auto cached = _memory.object(key)) {
        _elapsedMs = 0;
        _tile = *cached;
        _publishedPath = r.path;
        _status = tr("频谱已更新");
        emit changed();
        return;
    }
    _busy = true;
    _status = tr("正在计算可见频谱…");
    emit changed();
    _cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = _cancel;
    const auto version = _generation;
    const auto directory = _directory;
    const int diskMiB = _diskMiB;
    QtConcurrent::run(&_pool, [this, r, key, version, cancelled, directory, diskMiB] {
        QElapsedTimer clock;
        clock.start();
        KumoAudio::SpectrumTile tile;
        if (!diskMiB || !readCache(cachePath(directory, key), &tile))
            tile = KumoAudio::spectrum(r, *cancelled);
        if (!*cancelled && tile.error.isEmpty())
            writeCache(directory, key, tile, diskMiB);
        const auto elapsed = clock.elapsed();
        QMetaObject::invokeMethod(
            this,
            [this, tile, key, version, cancelled, elapsed, path = r.path] {
                _busy = false;
                if (version == _generation && !*cancelled) {
                    _elapsedMs = elapsed;
                    _tile = tile;
                    _publishedPath = path;
                    if (!tile.image.isNull())
                        _memory.insert(
                            key, new KumoAudio::SpectrumTile(tile),
                            qMax(1, int(qint64(tile.image.bytesPerLine()) * tile.image.height() / 1024)));
                    _status = tile.error.isEmpty() ? tr("频谱已更新") : tile.error;
                }
                emit changed();
                if (!_pending.isEmpty()) {
                    auto next = _pending;
                    _pending.clear();
                    begin(next);
                }
            },
            Qt::QueuedConnection);
    });
}
