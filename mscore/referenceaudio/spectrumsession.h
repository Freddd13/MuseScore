// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "spectrum.h"
#include <QObject>
#include <QThreadPool>
#include <QVariantMap>
#include <QCache>
#include <memory>

class ReferenceSpectrumSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QImage image READ image NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(QVariantMap cacheOptions READ cacheOptions NOTIFY cacheOptionsChanged)
    QThreadPool _pool;
    QCache<QString, KumoAudio::SpectrumTile> _memory;
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
    QString status() const { return _status; }
    QImage image() const { return _tile.image; }
    QVariantMap info() const;
    QVariantMap cacheOptions() const;
    Q_INVOKABLE void request(const QVariantMap &);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void setCacheOptions(const QString &, int memoryMiB, int diskMiB);
  signals:
    void changed();
    void cacheOptionsChanged();
};
