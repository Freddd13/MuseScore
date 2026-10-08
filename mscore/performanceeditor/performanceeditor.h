#ifndef MS_PERFORMANCEEDITOR_H
#define MS_PERFORMANCEEDITOR_H
#include <QWidget>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QSet>
#include <QRegion>
#include <QTransform>
#include "libmscore/mscoreview.h"
#include "libmscore/score.h"
#include "parameteredit.h"
#include "performanceview.h"
#include "performancesettings.h"
class QGridLayout;
class QSplitter;
class QToolButton;
class QResizeEvent;
class QComboBox;
class QCheckBox;
class QLabel;
class QScrollBar;
class QDoubleSpinBox;
namespace Ms {
class ScoreView;
class Segment;
class System;
class TempoText;
class PerformanceCanvas;
class PerformanceEditor : public QWidget, public MuseScoreView {
      Q_OBJECT
      friend class PerformanceCanvas;
      struct NoteInfo {
            Note* note;
            int tick, end, track, pitch, base, raw, baseMin, baseMax;
            Note::ValueType type;
            bool selected, audible;
            QRectF bounds;
            System* system;
            QString name, instrument, written, position, duration;
            const Part* part = nullptr;
            bool inScope = true, enabled = true;
            };
      struct SegmentInfo { int tick; QPointF position; System* system; double bpm; Segment* segment = nullptr; };
      struct MeasureInfo { int from, until, bar, beat; };
      QVector<MeasureInfo> _measures;
      struct TempoInfo { TempoText* text; int tick; double bpm; bool visible; };
      struct PedalInfo { Pedal* pedal; int from, until, track; };
      QPointer<ScoreView> _view;
      QMetaObject::Connection _scoreDestroyed;
      QVector<NoteInfo> _notes;
      QVector<SegmentInfo> _segments, _systemSegments;
      QVector<TempoInfo> _tempos;
      QVector<PedalInfo> _pedals;
      QHash<const Element*, int> _noteIndex;
      QSet<const Element*> _layoutIndex;
      QMap<Note*, VelocityEdit> _pending, _gestureBefore;
      QMap<int, double> _tempoDraft;
      QWidget* _canvas = nullptr;
      PerformanceCanvas *_noteCanvas = nullptr, *_ruler = nullptr;
      QSplitter* _splitter = nullptr;
      QScrollBar* _pitchScroll = nullptr;
      QScrollBar* _valueScroll = nullptr;
      QToolButton *_playButton = nullptr, *_auditionButton = nullptr;
      QRegion _overlayDamage;
      QTransform _overlayMatrix;
      QPointF _overlayPointer;
      bool _trackingOwned = false, _trackingBefore = false;
      bool _overBand = false, _rangePanning = false, _noteGesture = false;
      bool _cycleOnClick = false;
      QVector<int> _pressedCandidates;
      QToolButton* _followButton = nullptr;
      PerformanceViewport _viewport;
      PerformanceAppearance _appearance;
      PerformanceIntervalIndex _intervals;
      quint64 _visualRevision = 1, _geometryRevision = 0;
      struct NoteGeometry { int index; QRectF rect; };
      QVector<NoteGeometry> _noteGeometry;
      QSet<int> _playingNotes;
      QMultiHash<const Element*, int> _linkedIndex;
      QTimer _playTimer, _moveTimer;
      QPointF _queuedPoint;
      bool _moveQueued = false, _selecting = false, _marquee = false, _seeking = false;
      bool _following = true, _automaticScroll = false, _geometryDirty = false;
      bool _ghostVoices = true, _showValues = false, _snapshotDegraded = false;
      int _voiceMask = 15, _playTick = -1, _seekTick = -1;
      QPointF _marqueeStart, _marqueeEnd;
      Qt::KeyboardModifiers _selectionModifiers;
      QList<Note*> _selectionBefore;
      struct ViewMemory { PerformanceViewport viewport; int tick; };
      QHash<Score*, ViewMemory> _viewMemory;
      QSet<Score*> _rememberedScores;
      QVector<QMetaObject::Connection> _transportConnections;
      int rangeKind() const;
      void invalidateVisual();
      void updateSurfaces();
      void surfaceResized(PerformanceSurface);
      void paintBackground(QPainter&, PerformanceSurface);
      void paintForeground(QPainter&, PerformanceSurface);
      void buildGeometry();
      QVector<int> hits(QPointF, bool parameter = false, bool onScore = false) const;
      void cycleHit(const QVector<int>&, int step = 1);
      void audition(int index);
      void auditionPitch(int pitch);
      void togglePlayback(bool startOnly = false);
      void locateSelection();
      void zoomVertical(bool notes, double factor);
      void syncValueScroll();
      void applyAppearance();
      void overlayViewChanged();
      void invalidateOverlay();
      void syncOverlayTracking();
      bool overlayEvent(QEvent*);
      bool surfaceEvent(QObject*, QEvent*);
      void selectIndices(const QVector<int>&, Qt::KeyboardModifiers, bool preserveGroup = false);
      void selectVoices(int scope);
      void rebuildFilter();
      void centerPitch(bool selection = false, bool full = false);
      void resetRange(bool data);
      void pauseFollow();
      void syncTransport();
      void updatePlayhead();
      void seek(int);
      void playbackNotes();
      void updateHover(int);
      bool hasNoteValue(const NoteInfo&) const;
      bool noteEditable(const NoteInfo&) const;
      QString noteTooltip(int) const;
      void cancelSurfaceGesture();
      void queueGesture(QPointF);
      void drainGesture();
      QLabel* _status;
      QComboBox *_scope, *_voice, *_axis, *_parameter, *_tool;
      QCheckBox *_handles, *_band;
      QDoubleSpinBox* _number;
      QScrollBar* _scroll = nullptr;
      QGridLayout *_topRow = nullptr, *_actionsRow = nullptr;
      QTimer _refreshTimer, _commitTimer;
      ScoreContentState _state;
      bool _fitOnRefresh = true, _dirty = true, _committing = false, _dragging = false, _scoreGesture = false, _offsetDrag = false;
      int _hover = -1, _anchor = -1, _unlockedTempo = -1, _endTick = 1, _pedalFrom = 0, _pedalUntil = 0;
      Pedal* _pedalTarget = nullptr;
      bool _pedalEnd = false;
      int _pedalTrack = 0;
      QVector<int> _pedalStarts, _pedalEnds;
      System* _system = nullptr;
      QRectF _lane, _scoreLane, _gestureLane;
      QPointF _press, _last;

      int _relativeMax = 100, _contextTrack = 0, _selectedTempoTick = -1;
      double _tempoMax = 240;
      QVector<int> _dragTargets, _selectedIndices, _systemIndices;
      QMap<int, double> _dragOriginal;
      void scheduleRefresh();
      void refresh();
      void status(const QString& = QString());
      void updateSelection();
      bool overlayAllowed() const;
      QRectF laneRect() const;
      double xForTick(int tick, bool onScore) const;
      int tickForX(double x, bool onScore) const;
      int snap(int tick, bool end = false) const;
      double valueForY(double y, const QRectF&) const;
      double yForValue(double value, const QRectF&) const;
      double noteValue(int index) const;
      void setNoteValue(int index, double value, bool report = true);
      QVector<int> selectedNotes() const;
      void beginGesture(QPointF, QRectF, bool, int forcedAnchor = -1);
      void moveGesture(QPointF);
      void finishGesture();
      void cancelGesture();
      void applyPending();
      void paintLane(QPainter&, const QRectF&, bool onScore);

      void paintOverlay(QPainter&);
      bool eventFilter(QObject*, QEvent*) override;
      void resizeEvent(QResizeEvent*) override;
   public:
      explicit PerformanceEditor(QWidget* parent = nullptr);
      ~PerformanceEditor() override;
      void setView(ScoreView*);
      void selectionChanged();
      void convertSelected(Note::ValueType);
      void setSelectedValue(double);
      void fit(bool selection = false);
      bool hasPending() const { return !_pending.isEmpty(); }
      void flushPending();
      static void paintForView(ScoreView*, QPainter&);
      static void flushForScore(Score*);
      void layoutChanged() override;
      void dataChanged(const QRectF&) override { scheduleRefresh(); }
      void updateAll() override { scheduleRefresh(); }
      void removeScore() override { setView(nullptr); }
      void onElementDestruction(Element*) override;
      void drawBackground(QPainter*, const QRectF&) const override {}
      const QRect geometry() const override { return QWidget::geometry(); }
      };
}
#endif
