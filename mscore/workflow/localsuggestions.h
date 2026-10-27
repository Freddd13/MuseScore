// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "mscore/plugin/api/scoreobserver.h"
#include <QTimer>

namespace Ms {
// Owns one cancellable read/worker job. Only QVariant values leave the GUI.
class LocalSuggestionTask : public QObject {
    Q_OBJECT
    PluginAPI::ScoreObserver _observer;
    QTimer _pages;
    QVariantMap _state, _data, _settings, _config;
    QString _kind;
    int _cursor = 0, _limit = 32, _token = 0;
    bool _running = false, _readingPlayback = false;
    void page();
    void playbackPage();
    void beginCalculation();
    void finish(const QString &message);

  public:
    explicit LocalSuggestionTask(QObject *parent = nullptr);
    void start(Score *, const QString &kind, const QVariantMap &settings);
    void cancel();
    bool running() const { return _running; }
  signals:
    void progress(const QString &message);
    void completed(const QVariantMap &result);
};
} // namespace Ms
