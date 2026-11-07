// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QObject>
#include <QThreadPool>
#include <QVariantMap>
#include <atomic>
#include <memory>
class ReferenceAudioExport final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    QThreadPool _pool;
    std::shared_ptr<std::atomic_bool> _cancel;
    quint64 _generation = 0;
    bool _busy = false;
    double _progress = 0;
    QString _status;

  public:
    explicit ReferenceAudioExport(QObject* parent = nullptr);
    ~ReferenceAudioExport() override;
    bool available() const { return true; }
    bool busy() const { return _busy; }
    double progress() const { return _progress; }
    QString status() const { return _status; }
    Q_INVOKABLE void render(const QVariantMap& request);
    Q_INVOKABLE void cancel();
  signals:
    void changed();
    void finished(const QVariantMap& result);
};
