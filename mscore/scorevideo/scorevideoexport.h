// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "scorevideo.h"
#include <QFutureWatcher>
#include <atomic>
namespace Ms
{
class ScoreVideoExport final : public QObject
{
    Q_OBJECT
    QPointer<ScoreVideoDocument> _document;
    VideoSnapshot _snapshot;
    std::unique_ptr<MasterScore> _soundScore;
    QString _destination, _audio, _status;
    QObject* _audioJob = nullptr;
    QFutureWatcher<QVariantMap> _worker;
    std::shared_ptr<std::atomic_bool> _cancel;
    bool _busy = false, _previewOnly = false;
    double _progress = 0;
    void encode();
    void finish(const QVariantMap&);
  private slots:
    void audioFinished(QVariantMap);

  public:
    explicit ScoreVideoExport(QObject* parent = nullptr);
    ~ScoreVideoExport() override;
    bool busy() const { return _busy; }
    double progress() const { return _progress; }
    QString status() const { return _status; }
    Q_INVOKABLE bool start(ScoreVideoDocument*, const QString& destination,
                           bool previewOnly = false);
    Q_INVOKABLE void cancel();
  signals:
    void changed();
    void finished(QVariantMap result);
};
} // namespace Ms
