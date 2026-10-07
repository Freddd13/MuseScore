#ifndef MS_PERFORMANCEEDITOR_H
#define MS_PERFORMANCEEDITOR_H
#include <QWidget>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QSet>
#include "libmscore/mscoreview.h"
#include "libmscore/score.h"
#include "parameteredit.h"
class QGridLayout;
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
            };
      struct SegmentInfo { int tick; QPointF position; System* system; double bpm; };
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
      QWidget* _canvas;
      QLabel* _status;
      QComboBox *_scope, *_voice, *_axis, *_parameter, *_tool;
      QCheckBox *_handles, *_band;
      QDoubleSpinBox* _number;
      QScrollBar* _scroll;
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
      double _pixelsPerQuarter = 72;
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
      void paintTimeline(QPainter&);
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
