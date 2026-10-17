// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QWidget>
#include <QPointer>
#include <QVariantMap>
#include <QTimer>
#include <memory>
#include <atomic>
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/workflow/midicapture.h"
class QComboBox;
class QCheckBox;
class QSpinBox;
class QToolButton;
class QPushButton;
class QLabel;
class QSplitter;
class QTableWidget;
namespace Ms {
class Score;
class PerformanceEditor;
class RhythmCanvas;
class RhythmPane : public QWidget {
    Q_OBJECT
    friend class RhythmCanvas;
    PluginAPI::ScoreObserver _observer;
    MidiCaptureSession _capture;
    QPointer<Score> _score;
    QComboBox *_tracks, *_grid, *_endMode;
    QCheckBox *_releases;
    QSpinBox *_duration;
    QToolButton *_record;
    QPushButton *_apply;
    QLabel *_scopeLabel, *_status;
    QTableWidget *_table;
    RhythmCanvas *_canvas;
    QTimer _pages, _draftTimer;
    QVariantMap _state, _data, _midi, _draft;
    QVariantList _rows;
    QString _candidate;
    int _cursor = 0, _token = 0, _readGeneration = 0;
    bool _ready = false, _editing = false, _manualRows = false, _applied = false, _calculating = false;
    std::shared_ptr<std::atomic_bool> _cancelRead;
    QVariantMap settings() const;
    int quantize(int tick) const;
    void readPage();
    void queueDraft(bool fresh = false);
    void calculateDraft();
    void fillTable();
    void editCell(int, int);
    void record();
    void recordingChanged();
    bool prepare();
    void operate(const QString &);
    void restoreRecording();
    void resetForScore();
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

  public:
    explicit RhythmPane(PerformanceEditor *);
    ~RhythmPane() override;
    Q_SLOT void readScope();
    Q_SLOT void loadMidiFile(const QString &);
};
void installRhythmPane(PerformanceEditor *, QSplitter *);
} // namespace Ms
