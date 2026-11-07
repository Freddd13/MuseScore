// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "timingeditor.h"
#include "performanceeditor.h"
#include "miditrackpane.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "mscore/shortcut.h"
#include "mscore/workflow/toolicons.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/performancetiming.h"
#include "libmscore/rendermidi.h"
#include "libmscore/select.h"
#include "audio/midi/event.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolButton>
#include <QToolTip>
#include <algorithm>

namespace Ms
{
TimingEditor::TimingEditor(PerformanceEditor* editor, QWidget* parent)
    : QWidget(parent), _editor(editor)
{
    setObjectName("performanceTimingEditor");
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    _toggle = new QToolButton(this);
    _toggle->setObjectName("performanceTimingToggle");
    _toggle->setText(tr("演奏调整"));
    _toggle->setCheckable(true);
    _toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setComponentToolIcon(_toggle, "range");
    _toggle->setToolTip(tr(
        "拖音符移动实际起音，拖两端调整按键时间；细轮廓保留谱面时值。Alt自由调整，Ctrl+Z撤销。"));
    layout->addWidget(_toggle);
    _grid = new QComboBox(this);
    _grid->setObjectName("performanceTimingGrid");
    for (int denominator : {8, 16, 32, 64, 128})
        _grid->addItem(tr("1/%1").arg(denominator), DIVISION * 4 / denominator);
    _grid->addItem(tr("自由"), 1);
    _grid->setCurrentIndex(1);
    _grid->setToolTip(tr("实际起音和松键的吸附细分；按住Alt临时自由调整。"));
    layout->addWidget(_grid);
    _restore = new QToolButton(this);
    _restore->setText(tr("恢复演奏时间"));
    _restore->setObjectName("performanceTimingRestore");
    _restore->setPopupMode(QToolButton::InstantPopup);
    setComponentToolIcon(_restore, "reset");
    _restore->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto menu = new QMenu(_restore);
    _restore->setMenu(menu);
    for (int scope = 0; scope < 3; ++scope) {
        auto action = menu->addAction(QStringList{tr("选中音"), tr("当前选段"), tr("全谱")}[scope]);
        action->setObjectName(QString("performanceTimingRestore%1").arg(scope));
        connect(action, &QAction::triggered, this, [this, scope] { restore(scope); });
    }
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->actions()[0]->setEnabled(_score && !_score->selection().noteList().empty());
        menu->actions()[1]->setEnabled(_score && _score->selection().isRange());
        menu->actions()[2]->setEnabled(bool(_score));
    });
    layout->addWidget(_restore);
    _message = new QLabel(this);
    _message->setObjectName("performanceTimingMessage");
    layout->addWidget(_message);
    for (QWidget* control : QList<QWidget*>{_grid, _restore, _message})
        control->hide();
    _read.setSingleShot(true);
    connect(&_read, &QTimer::timeout, this, &TimingEditor::readNext);
    connect(_toggle, &QToolButton::toggled, this, &TimingEditor::setEnabledMode);
}
void TimingEditor::setEnabledMode(bool enabled)
{
    finish(false);
    for (QWidget* control : QList<QWidget*>{_grid, _restore, _message})
        control->setVisible(enabled);
    if (enabled) {
        if (auto writing = _editor->findChild<QToolButton*>("rollWriteToggle"))
            if (writing->defaultAction())
                writing->defaultAction()->setChecked(false);
        _editor->finishWheel();
        _editor->cancelGesture();
    }
    resetRead();
    contextChanged();
    _editor->updateSurfaces();
}
bool TimingEditor::active() const
{
    return _toggle->isChecked() && _ready && _score && _score == _editor->_score &&
           !_editor->_dirty && _state == _score->masterScore()->state() &&
           (!_editor->_midiTracks || !_editor->_midiTracks->active());
}
void TimingEditor::resetRead()
{
    finish(false);
    _read.stop();
    _ready = false;
    _rows.clear();
    _building.clear();
    _intervals.clear();
    _byNote.clear();
    _next = nullptr;
    _stale = 0;
}
void TimingEditor::contextChanged()
{
    if (!_toggle->isChecked())
        return;
    if (!_editor->_score || !_editor->isVisible() || !isVisible() ||
        (_editor->_midiTracks && _editor->_midiTracks->active())) {
        resetRead();
        _score = nullptr;
        return;
    }
    auto score = _editor->_score;
    if (_score != score) {
        resetRead();
        _score = score;
        _state = score->masterScore()->state();
    }
    if (_editor->_dirty || (seq && !seq->backgroundRenderingIdle())) {
        _message->setText(tr("准备演奏时间…"));
        if (!_read.isActive())
            _read.start(30);
        return;
    }
    const auto current = score->masterScore()->state();
    if (_state != current) {
        if (_ready && _signature == signature()) {
            // Velocity and layout edits keep the same timed events and live note order.
            _state = current;
            _read.stop();
            readyMessage();
            return;
        }
        resetRead();
        _state = current;
    }
    if (_ready) {
        _read.stop();
        if (!_drag)
            readyMessage();
        return;
    }
    if (!_ready && !_read.isActive()) {
        if (!_next)
            _next = score->firstMeasure();
        _message->setText(tr("读取演奏时间…"));
        _read.start(0);
    }
}
quint64 TimingEditor::signature() const
{
    quint64 result = 1469598103934665603ULL;
    auto add = [&](quint64 value) {
        result ^= value;
        result *= 1099511628211ULL;
    };
    for (const auto& info : _editor->_notes) {
        add(quintptr(info.note));
        add(info.tick);
        add(info.end);
        add(info.pitch);
        add(info.audible);
        auto chord = info.note->chord();
        if (chord->isGrace())
            chord = toChord(chord->parent());
        add(chord->actualTicks().ticks());
        const auto& events = info.note->playEvents();
        add(events.size());
        for (const auto& event : events) {
            add(event.pitch());
            add(event.ontime());
            add(event.len());
            add(event.velocitySourceIndex());
        }
        for (const auto& edit : PerformanceTiming::values(info.note)) {
            add(edit.index);
            add(edit.count);
            add(edit.pitch);
            add(edit.onset);
            add(edit.release);
        }
    }
    return result;
}
void TimingEditor::readNext()
{
    if (!_score || !_toggle->isChecked() || !isVisible() || !_editor->isVisible()) {
        resetRead();
        return;
    }
    if (_ready)
        return;
    if (_state != _score->masterScore()->state() || _editor->_dirty ||
        (seq && !seq->backgroundRenderingIdle())) {
        contextChanged();
        return;
    }
    if (!_next && !_ready)
        _next = _score->firstMeasure();
    QElapsedTimer timer;
    timer.start();
    while (_next) {
        EventMap events;
        MidiRenderer::collectPreparedMeasure(_score, _next, &events);
        QMap<QPair<quintptr, int>, Row> pending;
        for (const auto& pair : events) {
            const auto& event = pair.second;
            if (event.type() != ME_NOTEON || !event.noteEventOwner() || event.noteEventIndex() < 0)
                continue;
            auto owner = const_cast<Note*>(event.noteEventOwner());
            const auto source = event.note();
            const int noteIndex = _editor->_noteIndex.value(source, -1);
            if (noteIndex < 0)
                continue;
            auto chord = owner->chord();
            if (chord->isGrace())
                chord = toChord(chord->parent());
            if (event.nominalTick() != chord->tick().ticks())
                continue;
            if (event.velo() && event.noteEventIndex() == 0) {
                const auto adjustments = PerformanceTiming::values(owner);
                if (std::any_of(adjustments.cbegin(), adjustments.cend(),
                                [owner](const PerformanceTiming::Adjustment& a) {
                                    return !PerformanceTiming::matches(owner, a);
                                }))
                    ++_stale;
            }
            const auto key = qMakePair(quintptr(owner), event.noteEventIndex());
            if (event.velo()) {
                Row row;
                row.owner = owner;
                row.index = event.noteEventIndex();
                row.noteIndex = noteIndex;
                row.pitch = event.pitch();
                row.on = pair.first;
                const auto edit = PerformanceTiming::value(owner, row.index);
                row.originalOn = row.on - edit.onset;
                pending.insert(key, row);
            } else if (pending.contains(key)) {
                auto row = pending.take(key);
                row.end = pair.first;
                const auto edit = PerformanceTiming::value(owner, row.index);
                row.originalEnd = row.end - edit.release;
                _building.append(row);
            }
        }
        _next = _next->nextMeasure();
        if (timer.nsecsElapsed() >= 2000000)
            break;
    }
    if (_next) {
        _read.start(0);
        return;
    }
    _rows = std::move(_building);
    _building.clear();
    _intervals.clear();
    std::stable_sort(_rows.begin(), _rows.end(),
                     [](const Row& a, const Row& b) { return a.on < b.on; });
    _first = 0;
    _last = 0;
    for (int i = 0; i < _rows.size(); ++i) {
        _byNote.insert(_rows[i].noteIndex, i);
        _intervals.add(i, _rows[i].on, qMax(_rows[i].on + 1, _rows[i].end), _rows[i].pitch);
        _first = qMin(_first, _rows[i].on);
        _last = qMax(_last, _rows[i].end);
    }
    _ready = true;
    _signature = signature();
    ++_builds;
    readyMessage();
    _editor->surfaceResized(PerformanceSurface::Notes);
}
void TimingEditor::readyMessage()
{
    _message->setText(_stale ? tr("%1次发声 · %2处音型已改变").arg(_rows.size()).arg(_stale)
                             : tr("%1次发声").arg(_rows.size()));
    _message->setToolTip(
        _stale ? tr("这些音的发声次数或音高已经改变。可选中音，恢复演奏时间后重新拖动。")
               : QString());
}
int TimingEditor::startTick() const { return active() ? _first : 0; }
int TimingEditor::endTick() const { return active() ? _last : 0; }
QRectF TimingEditor::rectangle(const Row& row) const
{
    const double x = _editor->xForTick(row.on, false), end = _editor->xForTick(row.end, false);
    return {x, _editor->_viewport.pitchY(row.pitch) + 1, qMax(3., end - x - 1),
            qMax(2., _editor->_viewport.rowHeight - 2)};
}
QVector<int> TimingEditor::visibleRows() const
{
    if (!active())
        return {};
    const int from = _editor->tickForX(PerformanceViewport::gutter - 8, false),
              until = _editor->tickForX(_editor->_noteCanvas->width(), false);
    auto rows = _intervals.query(from, until);
    if (_drag)
        for (int i : _targets)
            if (!rows.contains(i))
                rows.append(i);
    return rows;
}
int TimingEditor::hit(QPointF point) const
{
    if (point.x() < PerformanceViewport::gutter)
        return -1;
    int chosen = -1;
    double best = 1e20;
    for (int i : visibleRows()) {
        const auto& row = _rows[i];
        const auto& note = _editor->_notes[row.noteIndex];
        if (!note.enabled || !rectangle(row).adjusted(-2, -1, 2, 1).contains(point))
            continue;
        const double distance =
            qAbs(rectangle(row).center().y() - point.y()) + (note.selected ? 0 : 100);
        if (distance < best) {
            best = distance;
            chosen = i;
        }
    }
    return chosen;
}
void TimingEditor::paint(QPainter& painter, const QRectF& area)
{
    if (!active())
        return;
    const auto& colors = _editor->_appearance.colors;
    for (int i : visibleRows()) {
        const auto& row = _rows[i];
        const auto& note = _editor->_notes[row.noteIndex];
        if (!note.inScope || (!note.enabled && !_editor->_ghostVoices) ||
            !_editor->_viewport.showsPitch(row.pitch))
            continue;
        const auto rect = rectangle(row);
        if (!rect.intersects(area))
            continue;
        const auto edit = _editor->_pending.value(note.note, {note.type, note.raw});
        const int velocity = NoteVelocity::effective(note.base, edit.type, edit.raw);
        auto color = _editor->_appearance.noteColor(velocity, note.track);
        if (!note.enabled)
            color.setAlpha(45);
        painter.setBrush(color);
        painter.setPen(
            QPen(note.selected ? colors[PerformanceAppearance::Selection] : color.darker(125),
                 note.selected ? 1.5 : 1));
        painter.drawRect(rect);
        if (note.selected && rect.width() >= 10) {
            painter.setPen(QPen(colors[PerformanceAppearance::Text], 1));
            for (double x : {rect.left() + 3, rect.right() - 3})
                painter.drawLine(QPointF(x, rect.top() + 2), QPointF(x, rect.bottom() - 2));
        }
        if (rect.width() > 40 && rect.height() >= 12) {
            painter.setPen(color.lightness() > 145 ? Qt::black : Qt::white);
            painter.drawText(rect.adjusted(5, 0, -5, 0), Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(note.name, Qt::ElideRight,
                                                              int(rect.width() - 10)));
        }
        if (row.on != row.originalOn || row.end != row.originalEnd) {
            auto edge = colors[PerformanceAppearance::Preview];
            edge.setAlpha(210);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(edge, 1.5));
            painter.drawLine(rect.topLeft(), rect.bottomLeft());
            painter.drawLine(rect.topRight(), rect.bottomRight());
        }
    }
}
void TimingEditor::repaintNote(int index)
{
    if (!active())
        return;
    for (int row : _byNote.values(index))
        _editor->_noteCanvas->update(rectangle(_rows[row]).adjusted(-3, -3, 3, 3).toAlignedRect());
}
void TimingEditor::finish(bool commit)
{
    if (!_drag)
        return;
    _drag = false;
    qApp->removeEventFilter(this);
    if (QWidget::mouseGrabber() == _editor->_noteCanvas)
        _editor->_noteCanvas->releaseMouse();
    _editor->_noteCanvas->unsetCursor();
    if (commit && _moved && _score && _state == _score->masterScore()->state() &&
        (!seq || seq->backgroundRenderingIdle())) {
        QMap<Note*, PerformanceTiming::Adjustments> edits;
        for (int i : _targets) {
            const auto& row = _rows[i];
            auto values = edits.contains(row.owner) ? edits.value(row.owner)
                                                    : PerformanceTiming::values(row.owner);
            auto a = PerformanceTiming::value(row.owner, row.index);
            a.onset = row.on - row.originalOn;
            a.release = row.end - row.originalEnd;
            auto found = std::find_if(
                values.begin(), values.end(),
                [&](const PerformanceTiming::Adjustment& value) { return value.index == a.index; });
            if (found == values.end())
                values.append(a);
            else
                *found = a;
            edits[row.owner] = values;
        }
        _editor->finishWheel();
        _score->startCmd();
        for (auto it = edits.cbegin(); it != edits.cend(); ++it)
            PerformanceTiming::change(_score, it.key(), it.value());
        _score->endCmd();
        _editor->_dirty = true;
        _editor->scheduleRefresh();
    } else
        for (int n = 0; n < _targets.size(); ++n) {
            _rows[_targets[n]].on = _before[n].first;
            _rows[_targets[n]].end = _before[n].second;
        }
    _targets.clear();
    _before.clear();
    _hit = -1;
    _moved = false;
    _editor->_noteCanvas->update();
}
bool TimingEditor::eventFilter(QObject* object, QEvent* event)
{
    if (!_drag)
        return false;
    if (event->type() == QEvent::ApplicationDeactivate ||
        event->type() == QEvent::WindowDeactivate ||
        (event->type() == QEvent::Hide && object == _editor->_noteCanvas) ||
        (event->type() == QEvent::MouseMove &&
         !(static_cast<QMouseEvent*>(event)->buttons() & Qt::LeftButton)))
        finish(true);
    else if (event->type() == QEvent::KeyPress &&
             static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        finish(false);
        return true;
    }
    return false;
}
bool TimingEditor::handle(QEvent* event)
{
    if (!_toggle->isChecked())
        return false;
    if (event->type() == QEvent::Hide) {
        finish(true);
        _read.stop();
        return false;
    }
    if (event->type() == QEvent::Show) {
        contextChanged();
        return false;
    }
    if (!active())
        return false;
    if (event->type() == QEvent::ToolTip) {
        auto help = static_cast<QHelpEvent*>(event);
        const int i = hit(help->pos());
        if (i >= 0) {
            const auto& row = _rows[i];
            const auto& note = _editor->_notes[row.noteIndex];
            QToolTip::showText(help->globalPos(),
                               tr("%1 · 谱表%2／声部%3\n实际起音 %4 tick，松键 %5 "
                                  "tick\n拖主体移动，拖两端改起止；Alt自由调整。")
                                   .arg(note.name)
                                   .arg(note.track / VOICES + 1)
                                   .arg(note.track % VOICES + 1)
                                   .arg(row.on)
                                   .arg(row.end),
                               _editor->_noteCanvas);
        } else
            QToolTip::hideText();
        return true;
    }
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        auto key = static_cast<QKeyEvent*>(event);
        if (_drag && key->key() == Qt::Key_Escape) {
            event->accept();
            if (event->type() == QEvent::KeyPress)
                finish(false);
            return true;
        }
        if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) {
            event->accept();
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                finish(false);
                getAction(key->matches(QKeySequence::Undo) ? "undo" : "redo")->trigger();
                contextChanged();
            }
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        const int i = hit(mouse->pos());
        if (i < 0)
            return false;
        _editor->selectIndices({_rows[i].noteIndex}, mouse->modifiers() & ~Qt::AltModifier, true);
        _editor->updateHover(_rows[i].noteIndex);
        if (_rows[i].pitch == _editor->_notes[_rows[i].noteIndex].pitch)
            _editor->audition(_rows[i].noteIndex);
        else
            _editor->auditionPitch(_rows[i].pitch);
        if (_score->readOnly() || (seq && (!seq->backgroundRenderingIdle() || seq->isPlaying()))) {
            return true;
        }
        _hit = i;
        _press = mouse->pos();
        _moved = false;
        _drag = true;
        const auto rect = rectangle(_rows[i]);
        const double left = qAbs(mouse->pos().x() - rect.left()),
                     right = qAbs(mouse->pos().x() - rect.right());
        _handle = qMin(left, right) <= 5 ? (left < right ? 1 : 2) : 0;
        _targets.clear();
        _before.clear();
        if (_editor->_selectedIndices.size() > 1 && _editor->_notes[_rows[i].noteIndex].selected) {
            for (int n = 0; n < _rows.size(); ++n)
                if (_editor->_notes[_rows[n].noteIndex].selected &&
                    _editor->_notes[_rows[n].noteIndex].enabled)
                    _targets.append(n);
        } else
            _targets.append(i);
        for (int n : _targets)
            _before.append({_rows[n].on, _rows[n].end});
        qApp->installEventFilter(this);
        _editor->_noteCanvas->setCursor(_handle ? Qt::SizeHorCursor : Qt::ClosedHandCursor);
        _editor->_noteCanvas->grabMouse();
        _editor->_noteCanvas->setFocus();
        return true;
    }
    if (event->type() == QEvent::MouseMove) {
        auto mouse = static_cast<QMouseEvent*>(event);
        if (!_drag) {
            const int i = hit(mouse->pos());
            if (i >= 0) {
                const auto rect = rectangle(_rows[i]);
                _editor->_noteCanvas->setCursor(qMin(qAbs(mouse->pos().x() - rect.left()),
                                                     qAbs(mouse->pos().x() - rect.right())) <= 5
                                                    ? Qt::SizeHorCursor
                                                    : Qt::OpenHandCursor);
            } else
                _editor->_noteCanvas->unsetCursor();
            return false;
        }
        if (!_moved &&
            (mouse->pos() - _press).manhattanLength() < QApplication::startDragDistance())
            return true;
        _moved = true;
        const int position = _targets.indexOf(_hit);
        const int initial = _handle == 2 ? _before[position].second : _before[position].first;
        const int raw =
            _editor->tickForX(mouse->pos().x(), false) - _editor->tickForX(_press.x(), false);
        const int quantum =
            (mouse->modifiers() & Qt::AltModifier) ? 1 : _grid->currentData().toInt();
        int delta = qRound(double(initial + raw) / quantum) * quantum - initial;
        for (int n = 0; n < _targets.size(); ++n) {
            const auto b = _before[n];
            if (_handle == 1)
                delta = qMin(delta, b.second - b.first - 1);
            if (_handle == 2)
                delta = qMax(delta, b.first + 1 - b.second);
        }
        for (int n = 0; n < _targets.size(); ++n) {
            auto& row = _rows[_targets[n]];
            row.on = _before[n].first + (_handle == 2 ? 0 : delta);
            row.end = _before[n].second + (_handle == 1 ? 0 : delta);
        }
        const auto& row = _rows[_hit];
        _message->setText(tr("起音%1 · 松键%2 tick").arg(row.on).arg(row.end));
        _editor->_noteCanvas->update();
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && _drag) {
        if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            finish(true);
            return true;
        }
    }
    return false;
}
void TimingEditor::restore(int scope)
{
    finish(false);
    if (!_score || _score->readOnly() || (seq && !seq->backgroundRenderingIdle()))
        return;
    QSet<Note*> targets;
    QSet<int> indices;
    const int from = _score->selection().isRange() ? _score->selection().tickStart().ticks() : 0;
    const int until = _score->selection().isRange() ? _score->selection().tickEnd().ticks() : 0;
    for (int i = 0; i < _editor->_notes.size(); ++i) {
        const auto& info = _editor->_notes[i];
        if (scope == 0 && !info.selected)
            continue;
        if (scope == 1 && (until <= from || info.tick < from || info.tick >= until))
            continue;
        indices.insert(i);
        if (!PerformanceTiming::values(info.note).isEmpty())
            targets.insert(info.note);
        auto first = info.note->firstTiedNote();
        if (!PerformanceTiming::values(first).isEmpty())
            targets.insert(first);
    }
    for (const auto& row : _rows)
        if (indices.contains(row.noteIndex) && !PerformanceTiming::values(row.owner).isEmpty())
            targets.insert(row.owner);
    if (targets.isEmpty()) {
        _message->setText(tr("所选范围沿用原演奏时间"));
        return;
    }
    _score->startCmd();
    for (auto note : targets)
        PerformanceTiming::change(_score, note, {});
    _score->endCmd();
    _editor->_dirty = true;
    _editor->scheduleRefresh();
}
QVariantMap TimingEditor::state() const
{
    QVariantList rows;
    for (int i : visibleRows()) {
        const auto& row = _rows[i];
        const auto& note = _editor->_notes[row.noteIndex];
        rows.append(QVariantMap{{"index", i},
                                {"event", row.index},
                                {"pitch", row.pitch},
                                {"on", row.on},
                                {"end", row.end},
                                {"originalOn", row.originalOn},
                                {"originalEnd", row.originalEnd},
                                {"track", note.track},
                                {"enabled", note.enabled},
                                {"selected", note.selected},
                                {"rect", rectangle(row)}});
    }
    return {{"builds", _builds}, {"enabled", _toggle->isChecked()}, {"ready", _ready},
            {"rows", rows},      {"total", _rows.size()},           {"message", _message->text()}};
}
} // namespace Ms
