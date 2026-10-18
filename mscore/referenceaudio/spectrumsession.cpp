// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumsession.h"
#include "tempoestimate.h"
#include <QThread>
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
#include <QMutexLocker>
#include <cmath>
#include <algorithm>

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
    _pool.setMaxThreadCount(1);_tempoPool.setMaxThreadCount(1);
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
    if(_tempoCancel)*_tempoCancel=true;
    _pool.waitForDone();_tempoPool.waitForDone();
}
void ReferenceSpectrumSession::cancel() {
    ++_generation;++_tempoGeneration;if(_tempoCancel)*_tempoCancel=true;
    if(_tempoBusy){_tempoBusy=false;emit tempoStateChanged();}
    _pending.clear();
    if (_cancel)
        *_cancel = true;
}
QVariantMap ReferenceSpectrumSession::info() const {
    return {{"sourcePath", _publishedPath},   {"from", _tile.fromMs},
            {"until", _tile.untilMs},         {"duration", _tile.durationMs},
            {"lowPitch", _tile.lowPitch},     {"highPitch", _tile.highPitch},
            {"sampleRate", _tile.sampleRate}, {"computeMilliseconds", _elapsedMs},
            {"workerMilliseconds",_workerMs},{"computedBlocks",_computedBlocks},{"cachedBlocks",_cachedBlocks},{"requestedBlocks",_requestedBlocks},{"blockMs",4000}};
}
QVariantMap ReferenceSpectrumSession::cacheOptions() const {
    QMutexLocker lock(&_memoryMutex);
    return {{"directory", QFileInfo(_directory).absolutePath()},
            {"memoryMiB", _memory.maxCost() / 1024},
            {"diskMiB", _diskMiB},
            {"usedMemoryKiB", _memory.totalCost()}};
}
void ReferenceSpectrumSession::setCacheOptions(const QString &directory, int memoryMiB, int diskMiB) {
    cancel();
    _directory = QDir(directory).filePath("kumo-spectrum-v1");
    {QMutexLocker lock(&_memoryMutex);
    _memory.setMaxCost(qBound(8, memoryMiB, 1024) * 1024);}
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
    const auto issue=KumoAudio::validateSpectrumRequest(r);
    if(!issue.isEmpty()) { _tile={};_tile.error=issue;_publishedPath=r.path;_status=issue;emit changed();return; }
    _busy = true;
    _status = tr("正在准备这一段的频谱…");
    emit changed();
    _cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = _cancel;
    const auto version = _generation;
    const auto directory = _directory;
    const int diskMiB = _diskMiB;
    const bool refresh=values.value("refresh",false).toBool();
    const double fundamentalMix=values.value("fundamentalMix",0.).toDouble();
    QtConcurrent::run(&_pool, [this, r, version, cancelled, directory, diskMiB, refresh, fundamentalMix] {
        QElapsedTimer clock;clock.start();
        constexpr double blockMs=4000.;
        const int first=int(std::floor(r.fromMs/blockMs));
        const int last=int(std::ceil(r.untilMs/blockMs))-1;
        const int count=last-first+1;
        QVector<KumoAudio::SpectrumTile> blocks;
        blocks.reserve(count);
        int computed=0,cached=0;
        QString error;
        for(int index=first;index<=last && !*cancelled;++index) {
            auto raw=r;raw.fromMs=index*blockMs;raw.untilMs=raw.fromMs+blockMs;
            raw.columns=r.fftSize==4096?400:200;
            const auto key=KumoAudio::spectrumKey(raw);
            KumoAudio::SpectrumTile block;
            bool found=false;
            if(!refresh){QMutexLocker lock(&_memoryMutex);if(auto item=_memory.object(key)){block=*item;found=true;}}
            if(!found&&!refresh&&diskMiB)found=readCache(cachePath(directory,key),&block);
            if(found && (block.image.format()!=QImage::Format_Grayscale8 || block.image.width()!=raw.columns || block.image.height()!=raw.rows))found=false;
            if(found)++cached;
            else {block=KumoAudio::spectrum(raw,*cancelled);++computed;}
            if(*cancelled)break;
            if(!block.error.isEmpty()){error=block.error;break;}
            if(block.image.isNull()){error=tr("这一段的频谱尚未生成，请重新分析。");break;}
            if(!found)writeCache(directory,key,block,diskMiB);
            {QMutexLocker lock(&_memoryMutex);if(!*cancelled)_memory.insert(key,new KumoAudio::SpectrumTile(block),qMax(1,int(qint64(block.image.bytesPerLine())*block.image.height()/1024)));}
            blocks.append(block);
            QMetaObject::invokeMethod(this,[this,version,index,first,count]{
                if(version!=_generation)return;
                _status=tr("频谱已准备 %1 / %2 段").arg(index-first+1).arg(count);emit changed();
            },Qt::QueuedConnection);
        }
        KumoAudio::SpectrumTile tile;tile.fromMs=r.fromMs;tile.untilMs=r.untilMs;tile.lowPitch=r.lowPitch;tile.highPitch=r.highPitch;tile.error=error;
        if(!*cancelled&&error.isEmpty()&&blocks.size()==count){
            tile.durationMs=blocks.front().durationMs;tile.sampleRate=blocks.front().sampleRate;
            tile.image=QImage(r.columns,r.rows,QImage::Format_Grayscale8);tile.image.fill(0);
            // Pool raw columns when zooming out so short attacks remain visible.
            for(int x=0;x<r.columns && !*cancelled;++x){
                const double from=r.fromMs+x*(r.untilMs-r.fromMs)/r.columns;
                const double until=r.fromMs+(x+1)*(r.untilMs-r.fromMs)/r.columns;
                const int begin=qMax(first,int(std::floor(from/blockMs))),end=qMin(last,int(std::floor(std::nextafter(until,from)/blockMs)));
                for(int blockIndex=begin;blockIndex<=end;++blockIndex){
                    const auto& block=blocks[blockIndex-first];
                    const int left=qBound(0,int(std::floor((from-block.fromMs)/blockMs*block.image.width())),block.image.width()-1);
                    const int right=qBound(left+1,int(std::ceil((until-block.fromMs)/blockMs*block.image.width())),block.image.width());
                    for(int y=0;y<r.rows;++y){const auto row=block.image.constScanLine(y);uchar value=0;for(int at=left;at<right;++at)value=qMax(value,row[at]);tile.image.scanLine(y)[x]=qMax(tile.image.constScanLine(y)[x],value);}
                }
            }
        }
        if(!*cancelled&&tile.error.isEmpty()&&fundamentalMix>0)tile.image=KumoAudio::emphasizeFundamental(tile,fundamentalMix,*cancelled);
        const auto elapsed=clock.elapsed();
        QMetaObject::invokeMethod(this,[this,tile,version,cancelled,elapsed,computed,cached,count,path=r.path]{
            _busy=false;
            if(version!=_generation && _pending.isEmpty())_status=tr("频谱分析已停止，已有结果保留。");
            if(version==_generation&&!*cancelled){
                _elapsedMs=computed?elapsed:0;_workerMs=elapsed;_computedBlocks=computed;_cachedBlocks=cached;_requestedBlocks=count;
                _tile=tile;_publishedPath=path;
                _status=tile.error.isEmpty() ? tr("频谱已更新 · %1 段复用，%2 段新分析").arg(cached).arg(computed) : tile.error;
            }
            emit changed();
            if(!_pending.isEmpty()){auto next=_pending;_pending.clear();begin(next);}
        },Qt::QueuedConnection);
    });
}

void ReferenceSpectrumSession::estimateTempo(const QVariantMap& request){
    if(_tempoCancel)*_tempoCancel=true;
    const auto generation=++_tempoGeneration;_tempoCancel=std::make_shared<std::atomic_bool>(false);const auto cancel=_tempoCancel;_tempoBusy=true;emit tempoStateChanged();
    QtConcurrent::run(&_tempoPool,[this,request,generation,cancel]{
        QThread::currentThread()->setPriority(QThread::LowPriority);
        const auto result=KumoAudio::estimateTempo(request,*cancel);
        QMetaObject::invokeMethod(this,[this,result,generation,cancel]{if(generation!=_tempoGeneration)return;_tempoBusy=false;emit tempoStateChanged();if(!*cancel)emit tempoReady(result);},Qt::QueuedConnection);
    });
}
