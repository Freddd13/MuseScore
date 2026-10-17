// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QWidget>
#include <QVariantMap>
#include <QPointer>
#include <QImage>
#include <QTimer>
#include <QVector>
class QGridLayout;
class QSplitter;
class QMouseEvent;
class QWheelEvent;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QToolButton;
class QLabel;
class QListWidget;
class QPainter;
namespace Ms {
class Score;
class PerformanceEditor;
class SpectrumCanvas;
class SpectrumPane : public QWidget {
    Q_OBJECT
    friend class SpectrumCanvas;
    PerformanceEditor *_editor;
    QPointer<Score> _documentScore;
    bool _hasPick = false, _metadataCancelled = false;
    double _pickMs = 0, _pickFrequency = 440;
    QObject *_session = nullptr, *_media = nullptr, *_audio = nullptr;
    QComboBox *_tracks = nullptr, *_clips = nullptr, *_display = nullptr, *_preset = nullptr,
              *_tool = nullptr;
    QDoubleSpinBox *_tuning = nullptr, *_speed = nullptr;
    QSpinBox *_transpose = nullptr;
    QToolButton *_play = nullptr, *_loop = nullptr, *_harmonics = nullptr;
    QLabel *_status = nullptr;
    QListWidget *_marks = nullptr;
    SpectrumCanvas *_canvas = nullptr;
    QGridLayout *_tools = nullptr;
    QVector<QWidget *> _controls;
    int _columns = 7;
    QTimer _requestTimer;
    QImage _image;
    QVariantMap _info, _request, _track, _clip;
    QVariantList _annotations;
    QString _path, _savedData, _playOwner;
    double _playStart = 0, _playUntil = 0, _playSpeed = 1, _lastAudioMs = 0;
    double _from = 0, _span = 8000, _low = 20.5, _high = 108.5, _cursorMs = 0, _cursorPitch = 69;
    double _a = -1, _b = -1;
    QPointF _press;
    double _pressFrom = 0;
    bool _panning = false, _dragging = false;
    bool _audioLoaded = false;
    Q_SLOT void refreshTracks();
    Q_SLOT void audioChanged();
    Q_SLOT void projectChanged();
    void refreshMarks();
    void updateActions();
    int inputPitch(const QVariantMap &) const;
    void requestVisible();
    void ensureSessions();
    void saveMarks();
    void insertPitches();
    void playSource();
    void seekSource(double);
    bool ownsPlayback() const;
    double sourcePosition() const;
    void cacheSettings();
    double timeAtX(double) const;
    double xAtTime(double) const;
    double pitchAtY(double, bool overlay = false) const;
    double yAtPitch(double, bool overlay = false) const;
    void paint(QPainter &, const QRectF &, bool overlay);
    void pointer(QMouseEvent *, bool release);
    void wheel(QWheelEvent *);
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
  private slots:
    void spectrumChanged();

  public:
    explicit SpectrumPane(PerformanceEditor *);
    Q_SLOT void viewChanged();
    void scoreChanged();
    void applyAppearance();
    void paintOverlay(QPainter &, const QRectF &);
    bool overlayEvent(QEvent *);
};
void installSpectrumPane(PerformanceEditor *, QSplitter *);
} // namespace Ms
