// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "performanceview.h"
#include "libmscore/score.h"
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
class QLabel;
class QToolButton;
class QComboBox;
class QPainter;
namespace Ms
{
class PerformanceEditor;
class Measure;
class TimingEditor final : public QWidget
{
    Q_OBJECT
    PerformanceEditor* _editor;
    QToolButton *_toggle, *_restore;
    QComboBox* _grid;
    QLabel* _message;
    QPointer<Score> _score;
    ScoreContentState _state;
    quint64 _signature = 0;
    int _builds = 0;
    QTimer _read;
    Measure* _next = nullptr;
    bool _ready = false, _drag = false, _moved = false;
    struct Row {
        Note* owner = nullptr;
        int index = 0, noteIndex = -1, pitch = 60;
        int on = 0, end = 1, originalOn = 0, originalEnd = 1;
    };
    QVector<Row> _rows, _building;
    PerformanceIntervalIndex _intervals;
    QMultiHash<int, int> _byNote;
    QVector<int> _targets;
    QVector<QPair<int, int>> _before;
    int _hit = -1, _handle = 0;
    int _first = 0, _last = 0;
    int _stale = 0;
    QPointF _press;
    void setEnabledMode(bool);
    void readNext();
    void resetRead();
    quint64 signature() const;
    void readyMessage();
    QRectF rectangle(const Row&) const;
    QVector<int> visibleRows() const;
    int hit(QPointF) const;
    void finish(bool commit);
    void restore(int scope);
    bool eventFilter(QObject*, QEvent*) override;

  public:
    TimingEditor(PerformanceEditor*, QWidget* parent);
    void contextChanged();
    void completeGesture() { finish(true); }
    bool active() const;
    int noteAt(QPointF point) const
    {
        const int i = hit(point);
        return i < 0 ? -1 : _rows[i].noteIndex;
    }
    int startTick() const;
    int endTick() const;
    void repaintNote(int);
    bool handle(QEvent*);
    void paint(QPainter&, const QRectF&);
    Q_INVOKABLE QVariantMap state() const;
};
} // namespace Ms
