// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QObject>
#include <QPointer>
#include <QSettings>
#include "libmscore/score.h"

namespace Ms {
class Score;
class DocumentUiState final : public QObject {
    Q_OBJECT
    QPointer<Score> _score;
    bool _restoring = false;
    quint64 _serial = 0;
public:
    explicit DocumentUiState(QObject* parent = nullptr);
    Score* score() const;
    bool restoring() const { return _restoring; }
    void activate(Score*);
    void capture(Score*);
signals:
    void captureRequested(Score*);
    void scoreChanged(Score*);
};
DocumentUiState* documentUiState();
void prepareDocumentUiSave(Score*);

// Uses each score's tool state for personal UI keys, and regular settings for
// machine-wide devices, paths, shortcuts and shared piano configuration.
class DocumentSettings : public QSettings {
    QPointer<Score> _score;
    QString fullKey(const QString&) const;
public:
    explicit DocumentSettings(Score* score = nullptr);
    QVariant value(const QString&, const QVariant& fallback = QVariant()) const;
    void setValue(const QString&, const QVariant&);
    bool contains(const QString&) const;
    void remove(const QString&);
    QStringList allKeys() const;
    QStringList childKeys() const;
    QStringList childGroups() const;
};
}
