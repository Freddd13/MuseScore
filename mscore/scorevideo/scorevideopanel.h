// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "scorevideoexport.h"
#include <QDockWidget>
#include <QElapsedTimer>
#include <QTimer>
class QMenu;
class QAction;
class QLabel;
class QPushButton;
class QComboBox;
class QTreeWidget;
class QProgressBar;
namespace Ms
{
class MuseScore;
class VideoPreview;
class VideoTimeline;
class ScoreVideoPanel final : public QDockWidget
{
    Q_OBJECT
    ScoreVideoDocument _document;
    ScoreVideoExport _export;
    VideoPreview* _preview;
    VideoTimeline* _timeline;
    QLabel* _status;
    QLabel* _positionLabel;
    QPushButton* _play;
    QPushButton* _prepare;
    QPushButton* _write;
    QPushButton* _cancel;
    QComboBox* _sound;
    QTreeWidget* _rows;
    QProgressBar* _progress;
    QTimer _timer;
    QElapsedTimer _clock;
    double _position = 0, _base = 0, _audioDuration = 0;
    bool _playing = false, _updating = false, _preparingPreview = false, _playAfterPrepare = false;
    QString _previewPath, _previewKey;
    QVariantMap _previewPlan;
    void refresh();
    void commitCues();
    void poll();
    void playPrepared();
    QString soundKey() const;

  protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

  public:
    explicit ScoreVideoPanel(QWidget* parent = nullptr);
    ~ScoreVideoPanel() override;
    ScoreVideoDocument* document() { return &_document; }
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE bool isPlaying() const { return _playing; }
    double position() const { return _position; }
};
QAction* scoreVideoAction(MuseScore*);
void addScoreVideoAction(QMenu*, MuseScore*);
} // namespace Ms
