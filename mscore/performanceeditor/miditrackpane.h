// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "../workflow/miditracks.h"
#include "../workflow/miditransport.h"
#include "performanceview.h"
#include "libmscore/score.h"
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <atomic>
#include <memory>

class QCheckBox;
class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPainter;
class QSpinBox;
class QSlider;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
namespace Ms
{
class MidiCaptureSession;
class PerformanceEditor;
QAction* midiTracksAction();
class MidiTrackPane final : public QWidget
{
    Q_OBJECT
    PerformanceEditor* _editor;
    MidiTrackDocument _document;
    QTreeWidget* _list;
    QToolButton *_toggle, *_play, *_record, *_restore, *_delete, *_import, *_add;
    QComboBox *_source, *_tool, *_grid, *_timeView;
    QSpinBox* _velocity;
    QSpinBox* _volume;
    QSlider* _volumeSlider;
    QCheckBox* _withScore;
    QLabel* _label;
    QPointer<QLabel> _status;
    QPointer<QWidget> _bar;
    QWidget* _parameterInset = nullptr;
    QTimer _poll;
    QTimer _soundTail;
    QFutureWatcher<QVariantMap> _loading;
    std::shared_ptr<std::atomic_bool> _cancel;
    QPointer<MasterScore> _jobScore;
    QPointer<MasterScore> _recordScore;
    QPointer<MasterScore> _previewScore;
    int _previewPitch = -1, _previewChannel = -1;
    MidiCaptureSession* _capture = nullptr;
    QString _activeId, _recordId;
    double _recordAt = 0, _cursorSeconds = 0;
    quint64 _playbackId = 0;
    quint64 _monitorId = 0;
    MidiTrackSound _monitorSound;
    int _monitorRate = 44100, _monitorChannel = 0;
    bool _scorePlayback = false;
    bool _scorePlaybackStarted = false;
    QElapsedTimer _startWait;
    QPointer<QObject> _playSession;
    QString _scorePreviewKey;
    bool _internal = false, _editingMidi = false, _recordImport = false, _enabled = false,
         _closing = false;
    int _previousParameter = 0, _previousAxis = 0;
    PerformanceRange _previousRange{1, 127};
    bool _previousComparison = false;
    bool _previousWriting = false;
    int _previousArrangement = 0;
    QHash<QString, QVariantMap> _associations;
    QPointer<QObject> _referenceProject;
    QHash<MasterScore*, QString> _chosenTracks;
    struct Row {
        QString track, id;
        double start = 0, end = 0;
        int tick = 0, until = 0, pitch = 60, velocity = 96;
        bool active = false;
    };
    struct Hit {
        int row;
        QRectF rect;
    };
    QVector<Row> _rows;
    QHash<QString, int> _activeRows;
    QVector<Hit> _hits;
    PerformanceIntervalIndex _intervals;
    QSet<QString> _selected;
    QSet<int> _pitches;
    int _lastTick = 0;
    bool _gesture = false, _moved = false, _drawing = false;
    bool _marquee = false, _parameterGesture = false, _marqueeAdd = false;
    QSet<QString> _beforeSelected;
    ScoreContentState _musicState;
    int _edge = 0, _anchor = -1;
    QPointF _press, _lastPointer;
    double _deltaStart = 0, _deltaEnd = 0;
    int _deltaPitch = 0, _deltaVelocity = 0;
    QVariantList _before;
    QVariantList _clipboard;
    QString _sourceText;
    void rebuild();
    void updateMode();
    void updateActions();
    void rebuildRows();
    void revealMaterial(double seconds);
    void releasePreview();
    bool prepareMonitor();
    void showMessage(const QString&);
    void syncInset();
    void loadFinished();
    void loadPath(const QString&, bool reveal);
    void chooseImport();
    void removeTrack();
    void restoreTrack();
    void duplicateTrack();
    void exportTrack(bool mappedTime = false);
    void alignTrack();
    void showMore();
    void togglePlayback();
    void stopTransport(bool releaseSounds = false);
    void registerPlaybackSession(double restartAt, bool followsScore);
    void toggleRecording();
    void poll();
    void setVelocity();
    void setVolume();
    void nudgeNotes(int key, Qt::KeyboardModifiers);
    void adoptToScore();
    void selectTrack(const QString&);
    void changeItem(QTreeWidgetItem*, int);
    int tickForSeconds(const QVariantMap&, double) const;
    double secondsForTick(const QVariantMap&, int) const;
    QRectF rectangle(const Row&, bool parameter) const;
    int hit(QPointF, bool parameter) const;
    void refreshAssociations();
    bool finishGesture(bool commit);
    void beginGesture(QPointF, bool parameter, Qt::KeyboardModifiers);
    void moveGesture(QPointF, bool parameter);
    void eraseNotes();
    void copyNotes();
    void pasteNotes();
    bool eventFilter(QObject*, QEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
  private slots:
    void referenceChanged();

  public:
    MidiTrackPane(PerformanceEditor*, QWidget* noteArea, QVBoxLayout*);
    ~MidiTrackPane() override;
    void shutdown();
    void setWorkspaceVisible(bool);
    void attachParameterArea(QWidget*);
    bool active() const { return _editingMidi; }
    bool ownsPlaybackCursor() const { return active() && (_playbackId || _scorePlayback); }
    bool scoreVisible() const;
    int endTick() const { return _enabled ? _lastTick + DIVISION * 4 : 0; }
    QSet<int> visiblePitches() const { return _enabled ? _pitches : QSet<int>(); }
    void contextChanged();
    void paintNotes(QPainter&, const QRectF&);
    bool paintValues(QPainter&, const QRectF&);
    bool paintPlayhead(QPainter&, QWidget*);
    bool handle(QEvent*, bool parameter, bool ruler);
    Q_INVOKABLE QVariantMap state();
    Q_INVOKABLE void importFile(const QString&);
};
} // namespace Ms
