// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "miditrackpane.h"
#include "performanceeditor.h"
#include "rollwriting.h"
#include "mscore/musescore.h"
#include "mscore/seq.h"
#include "mscore/pianotools.h"
#include "mscore/workflow/miditransport.h"
#include "mscore/workflow/midicapture.h"
#include "mscore/workflow/referencefactory.h"
#include "libmscore/tempo.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QHelpEvent>
#include <QToolTip>
#include <QClipboard>
#include <QMimeData>
#include <QJsonDocument>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSpinBox>
#include <QSlider>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <climits>

namespace Ms
{
namespace
{
double linearMap(double x, const QVector<QPointF>& points, bool inverse)
{
    if (points.isEmpty())
        return x;
    auto a = [inverse](QPointF p) { return inverse ? p.y() : p.x(); };
    auto b = [inverse](QPointF p) { return inverse ? p.x() : p.y(); };
    if (points.size() == 1)
        return b(points.first());
    int right = 1;
    while (right < points.size() - 1 && x > a(points[right]))
        ++right;
    return b(points[right - 1]) + (x - a(points[right - 1])) *
                                      (b(points[right]) - b(points[right - 1])) /
                                      (a(points[right]) - a(points[right - 1]));
}
QVector<QPointF> associationPoints(const QVariantMap& association)
{
    QVector<QPointF> points;
    if (association.isEmpty())
        return points;
    points.append({association.value("from").toDouble(), association.value("start").toDouble()});
    for (const auto& value : association.value("anchors").toList()) {
        const auto anchor = value.toMap();
        points.append({anchor.value("ms").toDouble(), anchor.value("tick").toDouble()});
    }
    points.append({association.value("until").toDouble(), association.value("end").toDouble()});
    std::sort(points.begin(), points.end(), [](QPointF a, QPointF b) { return a.x() < b.x(); });
    for (int i = 1; i < points.size(); ++i)
        if (points[i].x() <= points[i - 1].x() || points[i].y() <= points[i - 1].y())
            return {};
    return points;
}
} // namespace
void MidiTrackPane::refreshAssociations()
{
    _associations.clear();
    bool needed = false;
    for (const auto& value : _document.tracks())
        if (!value.toMap().value("associationId").toString().isEmpty())
            needed = true;
    if (!needed)
        return;
    auto project = referenceProject();
    if (project && project != _referenceProject) {
        _referenceProject = project;
        connect(project, SIGNAL(tracksChanged()), this, SLOT(referenceChanged()),
                Qt::UniqueConnection);
    }
    if (project)
        for (const auto& value : project->property("tracks").toList()) {
            const auto track = value.toMap();
            for (const auto& item : track.value("associations").toList()) {
                const auto association = item.toMap();
                _associations.insert(track.value("id").toString() + ":" +
                                         association.value("id").toString(),
                                     association);
            }
        }
}
void MidiTrackPane::referenceChanged()
{
    if (_closing || !_enabled)
        return;
    const auto previous = _associations;
    refreshAssociations();
    if (previous != _associations) {
        finishGesture(true);
        stopTransport();
        rebuildRows();
        _editor->updateSurfaces();
    }
}
int MidiTrackPane::tickForSeconds(const QVariantMap& track, double seconds) const
{
    if (!_editor->_score)
        return 0;
    const double time = midiTrackTime(track, seconds);
    if (_timeView->currentIndex() == 1) {
        const auto points = associationPoints(_associations.value(
            track.value("audioId").toString() + ":" + track.value("associationId").toString()));
        if (!points.isEmpty())
            return qRound(linearMap(time * 1000, points, false));
    }
    return _editor->_score->tempomap()->time2tick(time);
}
double MidiTrackPane::secondsForTick(const QVariantMap& track, int tick) const
{
    if (!_editor->_score)
        return 0;
    if (_timeView->currentIndex() == 1) {
        const auto points = associationPoints(_associations.value(
            track.value("audioId").toString() + ":" + track.value("associationId").toString()));
        if (!points.isEmpty())
            return midiTrackTime(track, linearMap(tick, points, true) / 1000, true);
    }
    return midiTrackTime(track, _editor->_score->tempomap()->tick2time(tick), true);
}
void MidiTrackPane::updateMode()
{
    QSignalBlocker block(_source);
    const auto track = _document.track(_activeId);
    _source->clear();
    _source->addItem(tr("SF2钢琴"), QVariantMap{{"kind", "sf2"}});
    if (_editor->_score)
        for (int staff = 0; staff < _editor->_score->nstaves(); ++staff)
            _source->addItem(tr("谱表%1的音源").arg(staff + 1),
                             QVariantMap{{"kind", "score-piano"}, {"staff", staff}});
    const auto source = track.value("source").toMap();
    _source->setCurrentIndex(
        source.value("kind").toString() == "score-piano"
            ? qBound(1, source.value("staff").toInt() + 1, _source->count() - 1)
            : 0);
    _source->setToolTip(tr("SF2钢琴使用当前音色库；选择谱表音源时，VST与效果使用独立播放副本。"));
    {
        QSignalBlocker number(_volume), slider(_volumeSlider);
        const int level = qRound(track.value("volume", 1.).toDouble() * 100);
        _volume->setValue(level);
        _volumeSlider->setValue(level);
    }
    const bool editing = _enabled && !_activeId.isEmpty();
    if (editing != _editingMidi) {
        if (editing) {
            _previousParameter = _editor->_parameter->currentIndex();
            _previousAxis = _editor->_axis->currentIndex();
            _previousRange = _editor->_viewport.ranges[0];
            _previousComparison = _editor->_comparisonButton->isChecked();
            _editor->_comparisonButton->setChecked(false);
            _previousWriting = _editor->_rollWriting && _editor->_rollWriting->isVisible();
            if (_previousWriting)
                _editor->_rollWriting->hide();
            _previousArrangement = _editor->_velocityArrangement;
            _editor->_velocityArrangement = 0;
        }
        _editingMidi = editing;
        _editor->_parameter->setCurrentIndex(editing ? 0 : _previousParameter);
        _editor->_axis->setCurrentIndex(editing ? 0 : _previousAxis);
        if (!editing) {
            _editor->_viewport.ranges[0] = _previousRange;
            _editor->_comparisonButton->setChecked(_previousComparison);
            _editor->_velocityArrangement = _previousArrangement;
            if (_previousWriting && _editor->_rollWriting)
                _editor->_rollWriting->show();
        }
    }
    _bar->setVisible(editing);
    _status->setVisible(_enabled && !_status->text().isEmpty());
    _editor->_status->setVisible(!editing);
    if (auto toolbar = _editor->findChild<QWidget*>("performanceControlScroll")) {
        auto tabs = _editor->findChild<QTabBar*>("workflowExpressionTabs");
        toolbar->setVisible(!editing && (!tabs || tabs->currentIndex() != 3));
    }
    syncInset();
    for (auto box : {_editor->_parameter, _editor->_axis, _editor->_tool})
        box->setEnabled(!editing);
    for (const char* name :
         {"performanceConvertRelative", "performanceConvertAbsolute", "performanceWriteValue"})
        if (auto w = _editor->findChild<QWidget*>(name))
            w->setEnabled(!editing);
    _editor->_comparisonButton->setEnabled(!editing);
    if (editing)
        _editor->_viewport.ranges[0] = {1, 127};
    if (_enabled) {
        _poll.start();
        _musicState = _editor->_score ? _editor->_score->state() : ScoreContentState();
    } else
        _poll.stop();
}
void MidiTrackPane::rebuildRows()
{
    _rows.clear();
    _activeRows.clear();
    _hits.clear();
    _intervals.clear();
    _pitches.clear();
    _lastTick = 0;
    if (!_enabled || !_editor->_score)
        return;
    refreshAssociations();
    for (const auto& value : _document.tracks()) {
        const auto track = value.toMap();
        if (!track.value("visible", true).toBool())
            continue;
        const auto id = track.value("id").toString();
        for (const auto& item : track.value("notes").toList()) {
            const auto note = item.toMap();
            Row row;
            row.track = id;
            row.id = note.value("id").toString();
            row.start = note.value("start").toDouble();
            row.end = note.value("end").toDouble();
            row.pitch = note.value("pitch").toInt();
            row.velocity = note.value("velocity").toInt();
            row.active = id == _activeId;
            row.tick = tickForSeconds(track, row.start);
            row.until = qMax(row.tick + 1, tickForSeconds(track, row.end));
            _lastTick = qMax(_lastTick, row.until);
            _pitches.insert(row.pitch);
            _rows.append(row);
        }
    }
    std::stable_sort(_rows.begin(), _rows.end(),
                     [](const Row& a, const Row& b) { return a.tick < b.tick; });
    QSet<QString> valid;
    for (int i = 0; i < _rows.size(); ++i) {
        const auto& row = _rows[i];
        _intervals.add(i, row.tick, row.until, row.pitch);
        if (row.active) {
            valid.insert(row.id);
            _activeRows.insert(row.id, i);
        }
    }
    _selected.intersect(valid);
    _editor->rebuildPitchRows();
    _editor->surfaceResized(PerformanceSurface::Notes);
}
void MidiTrackPane::revealMaterial(double seconds)
{
    if (!_enabled || !isVisible() || !_editor)
        return;
    const auto track = _document.track(_activeId);
    const int target = tickForSeconds(track, seconds);
    int low = 127, high = 0, first = INT_MAX;
    for (const auto& row : _rows)
        if (row.active && row.until >= target && row.tick <= target + DIVISION * 4) {
            low = qMin(low, row.pitch);
            high = qMax(high, row.pitch);
            first = qMin(first, row.tick);
        }
    if (first == INT_MAX)
        for (const auto& row : _rows)
            if (row.active) {
                low = qMin(low, row.pitch);
                high = qMax(high, row.pitch);
                first = qMin(first, row.tick);
                if (high - low > 24)
                    break;
            }
    if (first == INT_MAX)
        return;
    const double visible = _editor->_noteCanvas->height() / _editor->_viewport.rowHeight;
    const double top = _editor->_viewport.pitchIndex(_editor->_viewport.topPitch),
                 bottom = top - visible;
    if (_editor->_viewport.pitchIndex(high) > top || _editor->_viewport.pitchIndex(low) < bottom)
        _editor->_viewport.topPitch = _editor->_viewport.pitchAtIndex(
            (_editor->_viewport.pitchIndex(low) + _editor->_viewport.pitchIndex(high) + visible) /
            2);
    if (first < _editor->_scroll->value() ||
        first > _editor->_scroll->value() + _editor->_scroll->pageStep())
        _editor->_scroll->setValue(qMax(0, first - DIVISION));
    _editor->surfaceResized(PerformanceSurface::Notes);
}
QRectF MidiTrackPane::rectangle(const Row& source, bool parameter) const
{
    Row row = source;
    if (_gesture && !_marquee && source.active && _selected.contains(source.id)) {
        const auto track = _document.track(source.track);
        row.tick = qMax(0, tickForSeconds(track, qMax(0., row.start + _deltaStart)));
        row.until =
            qMax(row.tick + 1, tickForSeconds(track, qMax(row.start + .001, row.end + _deltaEnd)));
        row.pitch = qBound(0, row.pitch + _deltaPitch, 127);
        row.velocity = qBound(1, row.velocity + _deltaVelocity, 127);
    }
    const double x = _editor->xForTick(row.tick, false);
    if (parameter) {
        const auto lane = _editor->laneRect();
        const double y = _editor->yForValue(row.velocity, lane, false);
        return {x - 3, y, 6, qMax(2., lane.bottom() - y)};
    }
    return {x, _editor->_viewport.pitchY(row.pitch),
            qMax(3., _editor->xForTick(row.until, false) - x),
            qMax(3., _editor->_viewport.rowHeight - 1)};
}
void MidiTrackPane::paintNotes(QPainter& painter, const QRectF& clip)
{
    if (!_enabled)
        return;
    _hits.clear();
    const auto& colors = _editor->_appearance.colors;
    const int from = _editor->tickForX(clip.left() - 8, false),
              until = _editor->tickForX(clip.right() + 8, false);
    painter.save();
    painter.setClipRect(clip);
    auto visible = _intervals.query(from, until);
    if (_gesture && !_marquee)
        for (const auto& id : _selected) {
            const int i = _activeRows.value(id, -1);
            if (i >= 0 && !visible.contains(i))
                visible.append(i);
        }
    std::stable_sort(visible.begin(), visible.end(),
                     [this](int a, int b) { return _rows[a].active < _rows[b].active; });
    for (int index : visible) {
        const auto& row = _rows[index];
        if (!_editor->_viewport.showsPitch(row.pitch))
            continue;
        const auto box = rectangle(row, false);
        if (!box.intersects(clip))
            continue;
        QColor color = palette().color(QPalette::Highlight);
        if (!row.active)
            color.setAlpha(65);
        else
            color.setAlpha(220);
        painter.setBrush(color);
        painter.setPen(QPen(_selected.contains(row.id) && row.active
                                ? colors[PerformanceAppearance::Selection]
                                : color.darker(125),
                            row.active && _selected.contains(row.id) ? 2 : 1));
        painter.drawRect(box.adjusted(.5, .5, -.5, -.5));
        if (row.active) {
            _hits.append({index, box});
            if (box.width() > 24 && box.height() > 12) {
                painter.setPen(color.lightness() > 145 ? Qt::black : Qt::white);
                painter.drawText(box.adjusted(3, 0, -2, 0), Qt::AlignVCenter,
                                 QString::number(row.velocity));
            }
        }
    }
    if (_marquee) {
        painter.setBrush(QColor(100, 170, 220, 30));
        painter.setPen(QPen(colors[PerformanceAppearance::Selection], 1, Qt::DashLine));
        painter.drawRect(QRectF(_press, _lastPointer).normalized());
    }
    painter.restore();
}
bool MidiTrackPane::paintValues(QPainter& painter, const QRectF& clip)
{
    if (!active())
        return false;
    painter.save();
    painter.setClipRect(clip);
    for (int i : _intervals.query(_editor->tickForX(clip.left() - 8, false),
                                  _editor->tickForX(clip.right() + 8, false))) {
        const auto& row = _rows[i];
        const auto box = rectangle(row, true);
        QColor color = palette().color(QPalette::Highlight);
        if (!row.active)
            color.setAlpha(50);
        painter.fillRect(box, color);
        if (row.active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(box.topLeft() + QPointF(3, 0), 3, 3);
        }
        if (row.active && _selected.contains(row.id)) {
            painter.setPen(
                QPen(_editor->_appearance.colors[PerformanceAppearance::Selection], 1.5));
            painter.setBrush(color);
            painter.drawEllipse(box.topLeft() + QPointF(3, 0), 4, 4);
        }
    }
    painter.restore();
    return true;
}
int MidiTrackPane::hit(QPointF point, bool parameter) const
{
    QVector<int> candidates;
    if (point.x() < PerformanceViewport::gutter)
        return -1;
    for (int i : _intervals.query(_editor->tickForX(point.x() - 8, false),
                                  _editor->tickForX(point.x() + 8, false)))
        if (_rows[i].active &&
            rectangle(_rows[i], parameter).adjusted(-3, -3, 3, 3).contains(point))
            candidates.append(i);
    std::stable_sort(candidates.begin(), candidates.end(), [this](int a, int b) {
        return _selected.contains(_rows[a].id) > _selected.contains(_rows[b].id);
    });
    return candidates.isEmpty() ? -1 : candidates.first();
}
void MidiTrackPane::beginGesture(QPointF point, bool parameter, Qt::KeyboardModifiers modifiers)
{
    finishGesture(true);
    _press = _lastPointer = point;
    _parameterGesture = parameter;
    _before = _document.track(_activeId).value("notes").toList();
    _beforeSelected = _selected;
    _marqueeAdd = bool(modifiers & Qt::ControlModifier);
    _anchor = hit(point, parameter);
    _edge = 0;
    _moved = false;
    _marquee = false;
    _drawing = false;
    if (_anchor >= 0) {
        const auto& row = _rows[_anchor];
        if (modifiers & Qt::ControlModifier) {
            if (_selected.contains(row.id))
                _selected.remove(row.id);
            else
                _selected.insert(row.id);
        } else if (!_selected.contains(row.id)) {
            _selected.clear();
            _selected.insert(row.id);
        }
        if (!parameter) {
            auto rect = rectangle(row, false);
            _edge = point.x() - rect.left() < 5 ? -1 : rect.right() - point.x() < 5 ? 1 : 0;
        }
    } else if (!parameter && _tool->currentIndex() == 1) {
        _drawing = true;
        _selected.clear();
    } else if (!parameter) {
        _marquee = true;
        if (!(modifiers & Qt::ControlModifier))
            _selected.clear();
    } else {
        _selected.clear();
        _editor->updateSurfaces();
        return;
    }
    _gesture = true;
    _deltaStart = _deltaEnd = 0;
    _deltaPitch = _deltaVelocity = 0;
    updateActions();
    _editor->updateSurfaces();
}
void MidiTrackPane::moveGesture(QPointF point, bool parameter)
{
    if (!_gesture || parameter != _parameterGesture)
        return;
    _lastPointer = point;
    if (!_moved && QLineF(_press, point).length() < QApplication::startDragDistance())
        return;
    _moved = true;
    if (_marquee) {
        _selected = _marqueeAdd ? _beforeSelected : QSet<QString>();
        const auto area = QRectF(_press, point).normalized();
        for (const auto& row : _rows)
            if (row.active && rectangle(row, false).intersects(area))
                _selected.insert(row.id);
    } else if (_drawing) {
    } // The temporary note is committed from the two pointer positions.
    else if (_parameterGesture) {
        _deltaVelocity = qRound((_press.y() - point.y()) * 126 / _editor->laneRect().height());
    } else if (_anchor >= 0) {
        const auto& row = _rows[_anchor];
        const auto track = _document.track(_activeId);
        const int quantum = _grid->currentData().toInt();
        int destination = _editor->tickForX(point.x(), false) -
                          _editor->tickForX(_press.x(), false) +
                          (_edge == 1 ? row.until : row.tick);
        if (quantum)
            destination = qRound(double(destination) / quantum) * quantum;
        const double delta =
            secondsForTick(track, qMax(0, destination)) - (_edge == 1 ? row.end : row.start);
        _deltaStart = _edge == 1 ? 0 : delta;
        _deltaEnd = _edge == -1 ? 0 : delta;
        if (!_edge) {
            const double rows = std::round((_press.y() - point.y()) / _editor->_viewport.rowHeight);
            _deltaPitch = qRound(_editor->_viewport.pitchAtIndex(
                              _editor->_viewport.pitchIndex(row.pitch) + rows)) -
                          row.pitch;
        }
        double earliest = 1e12;
        for (const auto& value : _before) {
            const auto n = value.toMap();
            if (_selected.contains(n.value("id").toString()))
                earliest = qMin(earliest, n.value("start").toDouble());
        }
        _deltaStart = qMax(-earliest, _deltaStart);
        if (!_edge)
            _deltaEnd = _deltaStart;
    }
    _editor->updateSurfaces();
}
bool MidiTrackPane::finishGesture(bool commit)
{
    if (!_gesture)
        return false;
    const bool changed = _moved || _drawing;
    _gesture = false;
    if (commit && changed && !_marquee) {
        auto notes = _before;
        const auto track = _document.track(_activeId);
        if (_drawing) {
            int a = _editor->tickForX(qMin(_press.x(), _lastPointer.x()), false),
                b = _editor->tickForX(qMax(_press.x(), _lastPointer.x()), false);
            const int step = _grid->currentData().toInt();
            if (step) {
                a = qRound(double(a) / step) * step;
                b = qMax(a + step, qRound(double(b) / step) * step);
            } else
                b = qMax(a + 60, b);
            const int pitch =
                qBound(0,
                       qRound(_editor->_viewport.pitchAtIndex(
                           _editor->_viewport.pitchIndex(_editor->_viewport.topPitch) -
                           _press.y() / _editor->_viewport.rowHeight + .5)),
                       127);
            const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            notes.append(QVariantMap{{"id", id},
                                     {"start", qMax(0., secondsForTick(track, a))},
                                     {"end", qMax(.001, secondsForTick(track, b))},
                                     {"pitch", pitch},
                                     {"velocity", _velocity->value()},
                                     {"channel", 0},
                                     {"port", 0}});
            _selected = {id};
        } else
            for (auto& value : notes) {
                auto n = value.toMap();
                if (!_selected.contains(n.value("id").toString()))
                    continue;
                const double start = qMax(0., n.value("start").toDouble() + _deltaStart),
                             end = qMax(start + .001, n.value("end").toDouble() + _deltaEnd);
                n.insert("start", start);
                n.insert("end", end);
                n.insert("pitch", qBound(0, n.value("pitch").toInt() + _deltaPitch, 127));
                n.insert("velocity", qBound(1, n.value("velocity").toInt() + _deltaVelocity, 127));
                value = n;
            }
        if (!_document.replaceNotes(_activeId, notes))
            showMessage(_document.error());
        rebuildRows();
    } else if (!commit)
        _selected = _beforeSelected;
    _marquee = _drawing = _moved = false;
    _deltaStart = _deltaEnd = 0;
    _deltaPitch = _deltaVelocity = 0;
    _before.clear();
    updateActions();
    if (_editor)
        _editor->updateSurfaces();
    return changed;
}
void MidiTrackPane::eraseNotes()
{
    finishGesture(true);
    auto notes = _document.track(_activeId).value("notes").toList();
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [this](const QVariant& n) {
                                   return _selected.contains(n.toMap().value("id").toString());
                               }),
                notes.end());
    if (_document.replaceNotes(_activeId, notes)) {
        _selected.clear();
        rebuildRows();
        updateActions();
    } else
        showMessage(_document.error());
}
void MidiTrackPane::copyNotes()
{
    finishGesture(true);
    _clipboard.clear();
    for (const auto& value : _document.track(_activeId).value("notes").toList())
        if (_selected.contains(value.toMap().value("id").toString()))
            _clipboard.append(value);
    if (!_clipboard.isEmpty() && !qEnvironmentVariableIsSet("PERFORMANCE_NO_SYSTEM_CLIPBOARD")) {
        auto mime = new QMimeData;
        mime->setData("application/x-kumo-midi-notes",
                      QJsonDocument::fromVariant(_clipboard).toJson(QJsonDocument::Compact));
        QApplication::clipboard()->setMimeData(mime);
    }
    showMessage(tr("已复制%1个MIDI音符。点时间尺定位，再粘贴。").arg(_clipboard.size()));
}
void MidiTrackPane::pasteNotes()
{
    finishGesture(true);
    if (!qEnvironmentVariableIsSet("PERFORMANCE_NO_SYSTEM_CLIPBOARD")) {
        const auto mime = QApplication::clipboard()->mimeData();
        if (mime && mime->hasFormat("application/x-kumo-midi-notes")) {
            const auto bytes = mime->data("application/x-kumo-midi-notes");
            if (bytes.size() <= 1024 * 1024)
                _clipboard = QJsonDocument::fromJson(bytes).toVariant().toList();
        }
    }
    if (_clipboard.isEmpty()) {
        showMessage(tr("先选择MIDI音符并复制，再到时间尺选择粘贴位置。"));
        return;
    }
    double first = 86400;
    for (const auto& value : _clipboard)
        first = qMin(first, value.toMap().value("start").toDouble());
    auto notes = _document.track(_activeId).value("notes").toList();
    QSet<QString> added;
    for (const auto& value : _clipboard) {
        auto note = value.toMap();
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        note["id"] = id;
        note["start"] = qMax(0., note.value("start").toDouble() - first + _cursorSeconds);
        note["end"] = note.value("end").toDouble() - first + _cursorSeconds;
        notes.append(note);
        added.insert(id);
    }
    if (_document.replaceNotes(_activeId, notes)) {
        _selected = added;
        rebuildRows();
        updateActions();
        showMessage(tr("已粘贴%1个音，Ctrl+Z可以恢复。").arg(added.size()));
    } else
        showMessage(_document.error());
}
void MidiTrackPane::setVelocity()
{
    finishGesture(true);
    auto notes = _document.track(_activeId).value("notes").toList();
    for (auto& value : notes) {
        auto n = value.toMap();
        if (_selected.contains(n.value("id").toString())) {
            n.insert("velocity", _velocity->value());
            value = n;
        }
    }
    if (_document.replaceNotes(_activeId, notes))
        rebuildRows();
    else
        showMessage(_document.error());
}
void MidiTrackPane::nudgeNotes(int key, Qt::KeyboardModifiers modifiers)
{
    finishGesture(true);
    if (_selected.isEmpty())
        return;
    const auto track = _document.track(_activeId);
    auto notes = track.value("notes").toList();
    const bool horizontal = key == Qt::Key_Left || key == Qt::Key_Right;
    const int direction = key == Qt::Key_Left || key == Qt::Key_Down ? -1 : 1;
    double first = 86400, delta = 0;
    for (const auto& item : notes) {
        const auto note = item.toMap();
        if (_selected.contains(note.value("id").toString()))
            first = qMin(first, note.value("start").toDouble());
    }
    if (horizontal) {
        const int quantum = _grid->currentData().toInt();
        delta =
            quantum && !(modifiers & Qt::AltModifier)
                ? secondsForTick(track, tickForSeconds(track, first) + direction * quantum) - first
                : direction * .01;
        delta = qMax(-first, delta);
    }
    for (auto& item : notes) {
        auto note = item.toMap();
        if (!_selected.contains(note.value("id").toString()))
            continue;
        if (horizontal) {
            note["start"] = note.value("start").toDouble() + delta;
            note["end"] = note.value("end").toDouble() + delta;
        } else
            note["pitch"] = qBound(0,
                                   note.value("pitch").toInt() +
                                       direction * ((modifiers & Qt::ShiftModifier) ? 12 : 1),
                                   127);
        item = note;
    }
    if (_document.replaceNotes(_activeId, notes)) {
        rebuildRows();
        _editor->updateSurfaces();
    } else
        showMessage(_document.error());
}
bool MidiTrackPane::eventFilter(QObject* object, QEvent* event)
{
    if (_closing || !_enabled)
        return false;
    if (event->type() == QEvent::ApplicationDeactivate ||
        event->type() == QEvent::WindowDeactivate) {
        if (_gesture)
            finishGesture(true);
        releasePreview();
    }
    if (_previewPitch >= 0 && event->type() == QEvent::MouseButtonPress &&
        object != _editor->_noteCanvas)
        releasePreview();
    const bool canvas =
        object == _editor->_noteCanvas || object == _editor->_canvas || object == _editor->_ruler;
    if(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress){
        auto widget=qobject_cast<QWidget*>(object);const auto key=static_cast<QKeyEvent*>(event);
        const bool list=widget&&(widget==_list||_list->isAncestorOf(widget));
        const bool owned=widget&&(list||widget==this||isAncestorOf(widget)||(_bar&&_bar->isAncestorOf(widget)));
        bool typing=false;for(auto parent=widget;parent;parent=parent->parentWidget()){if(qobject_cast<QLineEdit*>(parent)||qobject_cast<QAbstractSpinBox*>(parent)){typing=true;break;}if(parent==this||parent==_bar)break;}
        if(owned&&!typing&&((key->key()==Qt::Key_Space&&key->modifiers()==Qt::NoModifier&&active())||(key->key()==Qt::Key_Delete&&list))){
            event->accept();if(event->type()==QEvent::KeyPress&&!key->isAutoRepeat()){if(key->key()==Qt::Key_Space)togglePlayback();else removeTrack();}return true;
        }
    }
    if (canvas && active() &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right || key->key() == Qt::Key_Up ||
            key->key() == Qt::Key_Down) {
            event->accept();
            if (event->type() == QEvent::KeyPress)
                nudgeNotes(key->key(), key->modifiers());
            return true;
        }
        const bool space=key->key()==Qt::Key_Space&&key->modifiers()==Qt::NoModifier;
        if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Escape ||
            space || key->matches(QKeySequence::Undo) ||
            key->matches(QKeySequence::Redo) || key->matches(QKeySequence::Copy) ||
            key->matches(QKeySequence::Cut) || key->matches(QKeySequence::Paste)) {
            event->accept();
            if (event->type() == QEvent::KeyPress) {
                if (key->matches(QKeySequence::Copy) || key->matches(QKeySequence::Cut)) {
                    copyNotes();
                    if (key->matches(QKeySequence::Cut))
                        eraseNotes();
                } else if (key->matches(QKeySequence::Paste))
                    pasteNotes();
                else if (key->key() == Qt::Key_Delete)
                    eraseNotes();
                else if (key->key() == Qt::Key_Escape) {
                    finishGesture(false);
                    _selected.clear();
                    _editor->selectIndices({}, Qt::NoModifier);
                    _editor->updateSurfaces();
                } else if (space){
                    if(!key->isAutoRepeat())togglePlayback();
                }
                else {
                    finishGesture(true);
                    mscore->cmd(getAction(key->matches(QKeySequence::Undo) ? "undo" : "redo"));
                    contextChanged();
                }
            }
            return true;
        }
    }
    return false;
}
bool MidiTrackPane::handle(QEvent* event, bool parameter, bool ruler)
{
    if (!active())
        return false;
    if (event->type() == QEvent::ContextMenu && !ruler) {
        finishGesture(true);
        const auto context = static_cast<QContextMenuEvent*>(event);
        const int index = hit(context->pos(), parameter);
        if (index >= 0 && !_selected.contains(_rows[index].id)) {
            _selected = {_rows[index].id};
            updateActions();
            _editor->updateSurfaces();
        }
        QMenu menu(this);
        auto copy = menu.addAction(tr("复制选音"), this, &MidiTrackPane::copyNotes);
        copy->setEnabled(!_selected.isEmpty());
        menu.addAction(tr("粘贴到当前时间"), this, &MidiTrackPane::pasteNotes);
        auto remove = menu.addAction(tr("删除选音"), this, &MidiTrackPane::eraseNotes);
        remove->setEnabled(!_selected.isEmpty());
        menu.addSeparator();
        menu.addAction(tr("放入写谱草稿…"), this, &MidiTrackPane::adoptToScore);
        menu.exec(context->globalPos());
        return true;
    }
    if (event->type() == QEvent::ToolTip && !ruler) {
        const auto help = static_cast<QHelpEvent*>(event);
        const int index = hit(help->pos(), parameter);
        if (index >= 0) {
            const auto& row = _rows[index];
            static const char* names[] = {"C",  "C♯", "D",  "E♭", "E",  "F",
                                          "F♯", "G",  "A♭", "A",  "B♭", "B"};
            QToolTip::showText(help->globalPos(), tr("%1%2 · 力度%3\n起音%4秒 · 松键%5秒")
                                                      .arg(QString::fromUtf8(names[row.pitch % 12]))
                                                      .arg(row.pitch / 12 - 1)
                                                      .arg(row.velocity)
                                                      .arg(row.start, 0, 'f', 3)
                                                      .arg(row.end, 0, 'f', 3));
        } else
            QToolTip::hideText();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick ||
        event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseMove) {
        const auto mouse = static_cast<QMouseEvent*>(event);
        const auto point = mouse->localPos();
        if (_previewPitch >= 0 &&
            ((event->type() == QEvent::MouseButtonRelease) ||
             (event->type() == QEvent::MouseMove && !(mouse->buttons() & Qt::LeftButton)))) {
            releasePreview();
            return true;
        }
        if (!parameter && !ruler && point.x() < PerformanceViewport::gutter &&
            event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
            if (!_playbackId && !_scorePlayback &&
                (!_capture || !_capture->state().value("recording").toBool()) &&
                (!seq || !seq->isPlaying())) {
                const int pitch = _editor->keyboardPitch(point);
                if (pitch >= 0 && prepareMonitor()) {
                    _previewPitch = pitch;
                    _previewScore = _document.score();
                    sendMidiTrackMonitor(_monitorId,
                                         NPlayEvent(ME_NOTEON, 0, pitch, _velocity->value()));
                    if (mscore)
                        if (auto piano = mscore->findChild<PianoTools*>())
                            piano->pressPitch(pitch);
                }
            }
            return true;
        }
        if (_gesture && event->type() == QEvent::MouseMove &&
            !(mouse->buttons() & Qt::LeftButton)) {
            finishGesture(true);
            return true;
        }
        if (point.x() < PerformanceViewport::gutter && !_gesture)
            return false;
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
            if (ruler) {
                finishGesture(true);
                _cursorSeconds =
                    secondsForTick(_document.track(_activeId), _editor->tickForX(point.x(), false));
                _editor->updateSurfaces();
                return true;
            }
            if (_loading.isRunning() || (_capture && _capture->state().value("recording").toBool()))
                return true;
            beginGesture(point, parameter, mouse->modifiers());
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick && mouse->button() == Qt::LeftButton) {
            if (!(mouse->modifiers() & Qt::AltModifier))
                togglePlayback();
            return true;
        }
        if (_gesture && event->type() == QEvent::MouseMove) {
            moveGesture(point, parameter);
            return true;
        }
        if (_gesture && event->type() == QEvent::MouseButtonRelease) {
            moveGesture(point, parameter);
            finishGesture(true);
            return true;
        }
        if (event->type() == QEvent::MouseMove && !ruler) {
            auto widget = parameter ? _editor->_canvas : _editor->_noteCanvas;
            const int index = hit(point, parameter);
            widget->setCursor(index >= 0                   ? Qt::SizeAllCursor
                              : _tool->currentIndex() == 1 ? Qt::CrossCursor
                                                           : Qt::ArrowCursor);
            return true;
        }
        if (mouse->button() == Qt::RightButton)
            return true;
    }
    return false;
}
bool MidiTrackPane::paintPlayhead(QPainter& painter, QWidget* surface)
{
    if (!active())
        return false;
    if (!ownsPlaybackCursor() && seq && seq->isPlaying())
        return false;
    const int tick = tickForSeconds(_document.track(_activeId), _cursorSeconds);
    const double x = _editor->xForTick(tick, false);
    painter.setPen(QPen(_editor->_appearance.colors[PerformanceAppearance::Playhead], 1.5));
    painter.drawLine(QPointF(x, 0), QPointF(x, surface->height()));
    return true;
}
} // namespace Ms
