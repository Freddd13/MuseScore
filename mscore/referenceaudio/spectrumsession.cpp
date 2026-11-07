// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumsession.h"
#include "spectrumprojection.h"
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
    _pending.clear();_backgroundBusy=false;
    if (_cancel)
        *_cancel = true;
    _status=tr("频谱分析已停止，已有结果保留。");
    emit changed();
}
QVariantMap ReferenceSpectrumSession::info() const {
    return {{"sourcePath", _publishedPath},   {"from", _tile.fromMs},
            {"until", _tile.untilMs},         {"duration", _tile.durationMs},
            {"lowPitch", _tile.lowPitch},     {"highPitch", _tile.highPitch},
            {"sampleRate", _tile.sampleRate}, {"computeMilliseconds", _elapsedMs},
            {"workerMilliseconds",_workerMs},{"computedBlocks",_computedBlocks},{"cachedBlocks",_cachedBlocks},{"requestedBlocks",_requestedBlocks},{"blockMs",4000},{"canonicalRows",1024},{"preparedBlocks",_preparedBlocks},{"totalBlocks",_totalBlocks},{"backgroundComputed",_backgroundComputed},{"backgroundBusy",_backgroundBusy}};
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
    if(_cancel)*_cancel=true;
    if (_busy) {
        if (_cancel)
            *_cancel = true;
        _pending = values;
        return;
    }
    begin(values);
}
void ReferenceSpectrumSession::begin(const QVariantMap& values) {
    KumoAudio::SpectrumRequest r;
    const auto path=values.value("path").toString();
    r.path=QUrl(path).isLocalFile()?QUrl(path).toLocalFile():path;
    r.fromMs=values.value("from",0).toDouble();r.untilMs=values.value("until",8000).toDouble();
    r.columns=values.value("columns",512).toInt();r.rows=values.value("rows",352).toInt();
    r.fftSize=values.value("fftSize",16384).toInt();
    r.lowPitch=values.value("lowPitch",20.5).toDouble();r.highPitch=values.value("highPitch",108.5).toDouble();
    r.tuning=values.value("tuning",440).toDouble();
    const auto issue=KumoAudio::validateSpectrumRequest(r);
    if(!issue.isEmpty()) {_tile={};_tile.error=issue;_publishedPath=r.path;_status=issue;emit changed();return;}
    _busy=true;_backgroundBusy=false;_preparedBlocks=0;_totalBlocks=0;
    _status=tr("正在准备这一段的频谱…");emit changed();
    _cancel=std::make_shared<std::atomic_bool>(false);
    const auto cancelled=_cancel;const auto version=_generation;const auto directory=_directory;
    const int diskMiB=_diskMiB;
    const bool refresh=values.value("refresh",false).toBool(),preload=values.value("preload",false).toBool();
    const double fundamentalMix=qBound(0.,values.value("fundamentalMix",0.).toDouble(),1.);
    QtConcurrent::run(&_pool,[this,r,version,cancelled,directory,diskMiB,refresh,preload,fundamentalMix] {
        QThread::currentThread()->setPriority(QThread::LowPriority);
        QElapsedTimer clock;clock.start();
        constexpr double blockMs=4000.;
        const int first=int(std::floor(r.fromMs/blockMs)),last=int(std::ceil(r.untilMs/blockMs))-1;
        const int count=last-first+1;
        auto load=[&](int index,bool force,bool& found) {
            const auto raw=KumoAudio::spectrumBlock(r,index);
            const auto key=KumoAudio::spectrumKey(raw);
            KumoAudio::SpectrumTile block;found=false;
            if(!force) {QMutexLocker lock(&_memoryMutex);if(auto item=_memory.object(key)){block=*item;found=true;}}
            if(!found&&!force&&diskMiB)found=readCache(cachePath(directory,key),&block);
            if(found && (block.image.format()!=QImage::Format_Grayscale8 || block.image.width()!=raw.columns ||
               block.image.height()!=raw.rows || block.lowPitch!=raw.lowPitch || block.highPitch!=raw.highPitch ||
               block.fromMs!=raw.fromMs || block.untilMs!=raw.untilMs || block.sampleRate<=0))found=false;
            if(!found)block=KumoAudio::spectrum(raw,*cancelled);
            if(*cancelled || !block.error.isEmpty() || block.image.isNull())return block;
            if(!found)writeCache(directory,key,block,diskMiB);
            {QMutexLocker lock(&_memoryMutex);if(!*cancelled)_memory.insert(key,new KumoAudio::SpectrumTile(block),qMax(1,int(qint64(block.image.bytesPerLine())*block.image.height()/1024)));}
            return block;
        };
        KumoAudio::SpectrumTile tile;tile.fromMs=r.fromMs;tile.untilMs=r.untilMs;tile.lowPitch=r.lowPitch;tile.highPitch=r.highPitch;
        tile.image=QImage(r.columns,r.rows,QImage::Format_Grayscale8);tile.image.fill(0);
        int computed=0,cached=0,completed=0;
        for(int index=first;index<=last && !*cancelled;++index) {
            bool found=false;auto block=load(index,refresh,found);
            if(*cancelled)break;
            if(!block.error.isEmpty()){tile.error=block.error;break;}
            if(block.image.isNull()){tile.error=tr("这一段的频谱尚未生成，请重新分析。");break;}
            if(found)++cached;else ++computed;
            tile.durationMs=block.durationMs;tile.sampleRate=block.sampleRate;
            if(fundamentalMix>0) {
                const auto key=KumoAudio::spectrumKey(KumoAudio::spectrumBlock(r,index))+":fundamental:"+QString::number(qRound(fundamentalMix*1000));
                bool filtered=false;
                if(!refresh){QMutexLocker lock(&_memoryMutex);if(auto prior=_memory.object(key)){block=*prior;filtered=true;}}
                if(!filtered) {
                    block.image=KumoAudio::emphasizeFundamental(block,fundamentalMix,*cancelled);
                    if(!*cancelled){QMutexLocker lock(&_memoryMutex);_memory.insert(key,new KumoAudio::SpectrumTile(block),qMax(1,int(qint64(block.image.bytesPerLine())*block.image.height()/1024)));}
                }
            }
            KumoAudio::projectSpectrumBlock(r,block,tile.image,*cancelled);++completed;
            QMetaObject::invokeMethod(this,[this,version,completed,count] {
                if(version!=_generation)return;
                _status=tr("频谱已准备 %1 / %2 段").arg(completed).arg(count);emit changed();
            },Qt::QueuedConnection);
        }
        if(!tile.error.isEmpty() || completed!=count)tile.image={};
        const auto elapsed=clock.elapsed();
        const int total=tile.durationMs>0?int(std::ceil(tile.durationMs/blockMs)):0;
        const bool background=preload && total>0 && tile.error.isEmpty() && !tile.image.isNull() && !*cancelled;
        QMetaObject::invokeMethod(this,[this,tile,version,cancelled,elapsed,computed,cached,count,background,total,path=r.path] {
            _busy=false;
            if(version!=_generation && _pending.isEmpty())_status=tr("频谱分析已停止，已有结果保留。");
            if(version==_generation && !*cancelled) {
                _elapsedMs=computed?elapsed:0;_workerMs=elapsed;_computedBlocks=computed;_cachedBlocks=cached;_requestedBlocks=count;
                _backgroundBusy=background;_totalBlocks=total;_preparedBlocks=qMin(total,count);
                _tile=tile;_publishedPath=path;
                _status=tile.error.isEmpty()?tr("频谱已更新 · %1 段复用，%2 段新分析").arg(cached).arg(computed):tile.error;
            }
            emit changed();
            if(!_pending.isEmpty()){const auto next=_pending;_pending.clear();begin(next);}
        },Qt::QueuedConnection);
        if(!background)return;
        int ready=qMax(0,qMin(last,total-1)-qMax(first,0)+1),fresh=0;
        QElapsedTimer notification;notification.start();QString backgroundError;
        auto prepare=[&](int index) {
            if(index<0 || index>=total || (index>=first&&index<=last) || *cancelled)return;
            bool found=false;const auto block=load(index,refresh,found);
            if(*cancelled)return;
            if(!block.error.isEmpty() || block.image.isNull()){backgroundError=block.error.isEmpty()?tr("这一段音频未能读取。"):block.error;*cancelled=true;return;}
            ++ready;if(!found)++fresh;
            if(notification.elapsed()>=250) {
                notification.restart();
                QMetaObject::invokeMethod(this,[this,version,ready,total] {
                    if(version!=_generation)return;
                    _preparedBlocks=ready;_status=tr("频谱可用 · 后台准备 %1 / %2 段").arg(ready).arg(total);emit changed();
                },Qt::QueuedConnection);
            }
        };
        prepare(first-1);prepare(last+1);
        // Keep only a block and the bounded image cache while preparing a long recording.
        for(int index=0;index<total && !*cancelled;++index)
            if(index!=first-1 && index!=last+1)prepare(index);
        QMetaObject::invokeMethod(this,[this,version,cancelled,ready,total,fresh,backgroundError] {
            if(version!=_generation)return;
            _backgroundBusy=false;_preparedBlocks=ready;_backgroundComputed=fresh;
            if(!backgroundError.isEmpty())_status=tr("当前频谱可用；后台准备暂停：%1").arg(backgroundError);
            else if(!*cancelled)_status=tr("整条录音的频谱已准备好 · %1 段").arg(total);
            emit changed();
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
