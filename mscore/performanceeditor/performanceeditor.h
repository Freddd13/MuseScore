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
class QWheelEvent;
class QGridLayout;
class QSplitter;
class QToolButton;
class QResizeEvent;
class QComboBox;
class QMenu;
class QCheckBox;
class QLabel;
class QScrollBar;
class QDoubleSpinBox;
namespace Ms {
class ScoreView;
class Segment;
class System;
class TempoText;
class TextLine;
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
      struct TempoCurveInfo { TextLine* line = nullptr; int from = 0, until = 0; double start = 120, target = 96, shape = 1; };
      struct TempoPlotPoint { int tick; double bpm; bool ramp; bool end = false; };
      QVector<TempoCurveInfo> _tempoCurves;
      QVector<TempoPlotPoint> _tempoPlot;
      int _curveIndex = -1, _curveGrip = -1;
      TextLine* _selectedCurve = nullptr;
      TempoCurveInfo _curveDraft;
      void refreshTempoCurves();
      bool beginTempoCurve(QPointF, const QRectF&, bool);
      void moveTempoCurve(QPointF);
      bool finishTempoCurve(QString*);
      bool paintTempoCurves(QPainter&, const QRectF&, bool);
      bool tempoCurveMenu(QPointF, bool);
      bool setTempoCurveNumber(double);
      QVector<PedalInfo> _pedals;
      QHash<const Element*, int> _noteIndex;
      QSet<const Element*> _layoutIndex;
      QMap<Note*, VelocityEdit> _pending, _gestureBefore;
      struct PropertyDraft { Element* element; Pid pid; QVariant value; };
      QVector<PropertyDraft> _propertyPending;
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
      PerformanceViewport _scoreViewport;
      QSet<int> _excludedStaves;
      QMenu* _staffMenu = nullptr;
      QComboBox* _pedalGrid = nullptr;
      QLabel* _parameterHint = nullptr;
      bool _noteTips = true, _scoreVoiceColors = true, _scoreRangePanning = false;
      int _bandHeight = 48, _tempoPointTick = -1;
      QRectF _scoreAxis;
      std::array<QRectF, 6> _scoreControls;
      void rebuildStaffMenu();
      bool staffEnabled(int track) const;
      QColor scoreNoteColor(int index) const;
      void resetScoreRange(bool data);
      void scoreRangeControl(int control);
      int nearestSegment(int tick) const;
      bool pedalVisible(const PedalInfo&) const;
      bool pedalBoundary(int tick, int track, bool end) const;
      QString parameterTooltip(QPointF, bool onScore) const;
      PerformanceAppearance _appearance;
      PerformanceIntervalIndex _intervals;
      quint64 _visualRevision = 1, _geometryRevision = 0;
      struct NoteGeometry { int index; QRectF rect; };
      QVector<NoteGeometry> _noteGeometry;
      QSet<int> _playingNotes;
      QMultiHash<const Element*, int> _linkedIndex;
      QTimer _playTimer, _moveTimer, _wheelTimer;
      QToolButton* _wheelButton = nullptr;
      int _wheelIndex = -1;
      QPointF _wheelPoint;
      QPointer<QWidget> _wheelSurface;
      double _wheelValue = 0, _wheelRemainder = 0;
      QMap<Note*, VelocityEdit> _wheelBefore;
      bool wheelVelocity(QWheelEvent*, bool notes, bool onScore = false);
      void finishWheel(bool commit = true);
      void cancelWheel();
      QRectF wheelBadge(QWidget*) const;
      void paintWheelBadge(QPainter&, QWidget*) const;
      void repaintWheelTarget(int);
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
      std::array<QHash<int, QSet<int>>, 2> _pedalBoundaries;
      System* _system = nullptr;
      QRectF _lane, _scoreLane, _gestureLane;
      QPointF _press, _last;

      int _relativeMax = 100, _contextTrack = 0, _selectedTempoTick = -1;
      double _tempoMax = 240;
      double _tempoOriginalBpm = 0;
      QVector<int> _dragTargets, _selectedIndices, _systemIndices;
      QMap<int, double> _dragOriginal;
      void scheduleRefresh();
      void refresh();
      void status(const QString& = QString());
      void updateSelection();
      void refreshLayout();
      void setOverlaySystem(System*);
      System* systemAtTick(int) const;
      bool overlayAllowed() const;
      QRectF laneRect() const;
      double xForTick(int tick, bool onScore) const;
      int tickForX(double x, bool onScore) const;
      int snap(int tick, bool end = false) const;
      double valueForY(double y, const QRectF&, bool onScore = false) const;
      double yForValue(double value, const QRectF&, bool onScore = false) const;
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
      bool hasPending() const { return !_pending.isEmpty() || !_propertyPending.isEmpty(); }
      void queueProperty(Element*, Pid, const QVariant&);
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
