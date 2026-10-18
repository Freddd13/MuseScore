// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "spectrum.h"
#include <QObject>
#include <QThreadPool>
#include <QVariantMap>
#include <QCache>
#include <QMutex>
#include <memory>

class ReferenceSpectrumSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool tempoBusy READ tempoBusy NOTIFY tempoStateChanged)
    Q_PROPERTY(QImage image READ image NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(QVariantMap cacheOptions READ cacheOptions NOTIFY cacheOptionsChanged)
    QThreadPool _pool,_tempoPool;
    std::shared_ptr<std::atomic_bool> _tempoCancel;
    quint64 _tempoGeneration=0;
    bool _tempoBusy=false;
    QCache<QString, KumoAudio::SpectrumTile> _memory;
    mutable QMutex _memoryMutex;
    int _computedBlocks=0, _cachedBlocks=0, _requestedBlocks=0;
    qint64 _workerMs=0;
    std::shared_ptr<std::atomic_bool> _cancel;
    QString _directory, _status, _publishedPath;
    quint64 _generation = 0;
    int _diskMiB = 1024;
    qint64 _elapsedMs = 0;
    bool _busy = false;
    KumoAudio::SpectrumTile _tile;
    QVariantMap _pending;
    void begin(const QVariantMap &);

  public:
    explicit ReferenceSpectrumSession(QObject * = nullptr);
    ~ReferenceSpectrumSession() override;
    bool available() const { return true; }
    bool busy() const { return _busy; }
    bool tempoBusy() const {return _tempoBusy;}
    QString status() const { return _status; }
    QImage image() const { return _tile.image; }
    QVariantMap info() const;
    QVariantMap cacheOptions() const;
    Q_INVOKABLE void request(const QVariantMap &);
    Q_INVOKABLE void estimateTempo(const QVariantMap &);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void setCacheOptions(const QString &, int memoryMiB, int diskMiB);
  signals:
    void changed();
    void cacheOptionsChanged();
    void tempoStateChanged();
    void tempoReady(const QVariantMap &);
};
