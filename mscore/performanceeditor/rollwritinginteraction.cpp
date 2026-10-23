// SPDX-License-Identifier: GPL-2.0-or-later
#include "libmscore/chord.h"
#include "libmscore/input.h"
#include "libmscore/note.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/select.h"
#include "libmscore/staff.h"
#include "libmscore/undo.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "mscore/shortcut.h"
#include "mscore/workflow/arrangement.h"
#include "mscore/workflow/playbackpreview.h"
#include "performanceeditor.h"
#include "rollnotation.h"
#include "rollwriting.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QHelpEvent>
#include <QHideEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QToolTip>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtConcurrent>
#include <algorithm>

namespace Ms {
namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString noteName(int pitch) {
  static const char *names[] = {"C",  "C♯", "D",  "E♭", "E",  "F",
                                "F♯", "G",  "A♭", "A",  "B♭", "B"};
  return QString::fromUtf8(names[qBound(0, pitch, 127) % 12]) +
         QString::number(pitch / 12 - 1);
}
} // namespace
int RollWritingBar::quantize(int tick, int track,
                             const QVariantMap &note) const {
  return rollSnapInScore(
      tick,
      note.value("tripletGrid").toBool()
          ? note.value("rasterQuantum", _grid->currentData()).toInt()
          : _grid->currentData().toInt(),
      note.value("tripletGrid").toBool() || _grid->currentIndex() >= 4, track,
      _state["scope"].toMap()["start"].toInt(), _measures, _tuplets);
}
int RollWritingBar::pitchAt(double y) const {
  return qBound(0,
                qRound(_editor->_viewport.pitchAtIndex(
                    _editor->_viewport.pitchIndex(_editor->_viewport.topPitch) -
                    y / _editor->_viewport.rowHeight + .5)),
                127);
}
QRectF RollWritingBar::rectangle(const QVariantMap &row) const {
  const double x = _editor->xForTick(row["start"].toInt(), false),
               end = _editor->xForTick(row["end"].toInt(), false);
  return QRectF(x, _editor->_viewport.pitchY(row["pitch"].toInt()) + 1,
                qMax(3., end - x - 1),
                qMax(2., _editor->_viewport.rowHeight - 2));
}
void RollWritingBar::ensureIndex() {
  if (!_indexDirty || _gesture)
    return;
  _intervals->clear();
  QVector<int> indices;
  indices.reserve(_rows.size());
  for (int i = 0; i < _rows.size(); ++i)
    indices.append(i);
  std::sort(indices.begin(), indices.end(), [this](int a, int b) {
    return _rows[a].toMap()["start"].toInt() <
           _rows[b].toMap()["start"].toInt();
  });
  for (int i : indices) {
    const auto row = _rows[i].toMap();
    _intervals->add(i, row["start"].toInt(), row["end"].toInt(),
                    row["pitch"].toInt());
  }
  _indexDirty = false;
}
int RollWritingBar::hit(QPointF point) const {
  const_cast<RollWritingBar *>(this)->ensureIndex();
  const int tick = _editor->tickForX(point.x(), false),
            margin =
                qMax(1, qRound(4 * 480 / _editor->_viewport.pixelsPerQuarter));
  int chosen = -1;
  for (int i :
       _intervals->query(tick - margin, tick + margin, pitchAt(point.y()) - 2,
                         pitchAt(point.y()) + 2)) {
    const auto row = _rows[i].toMap();
    if (!editableTrack(row["track"].toInt()) ||
        !rectangle(row).adjusted(-2, -2, 2, 2).contains(point))
      continue;
    if (chosen < 0 || _selected.contains(row["id"].toString()) ||
        row["track"] == _track->currentData())
      chosen = i;
  }
  return chosen;
}
void RollWritingBar::selectSourceNotes() {
  if (_editor->_dirty)
    _editor->refresh();
  QMap<int, QMap<int, QSet<int>>> wanted;
  for (auto value : _rows) {
    const auto row = value.toMap();
    if (_selected.contains(row["id"].toString()))
      wanted[row["start"].toInt()][row["track"].toInt()].insert(
          row["pitch"].toInt());
  }
  QVector<int> indices;
  for (int i = 0; i < _editor->_notes.size(); ++i) {
    const auto &note = _editor->_notes[i];
    auto tick = wanted.constFind(note.tick);
    if (tick == wanted.cend())
      continue;
    auto track = tick.value().constFind(note.track);
    if (track != tick.value().cend() && track.value().contains(note.pitch))
      indices.append(i);
  }
  if (!indices.isEmpty())
    _editor->selectIndices(indices, Qt::NoModifier);
  else if (!_selected.isEmpty()) {
    _score->deselectAll();
    _score->update();
    if (mscore && mscore->currentScore() == _score)
      mscore->selectionChanged(_score->selection().state());
  }
}
void RollWritingBar::begin(QPointF point, Qt::KeyboardModifiers modifiers) {
  stopAudition();
  _press = point;
  _pressTick = _editor->tickForX(point.x(), false);
  _cursorTick = _pressTick;
  _pressPitch = pitchAt(point.y());
  _before = _rows;
  _beforeSelected = _selected;
  _moveBase = _rows;
  _moved = false;
  _changed = false;
  _copy = modifiers & Qt::ControlModifier;
  _tail = false;
  const int index = hit(point);
  if (_tool->currentIndex() == 2) {
    if (index >= 0) {
      _selected = {_rows[index].toMap()["id"].toString()};
      eraseSelected();
    }
    return;
  }
  if (index >= 0) {
    const auto row = _rows[index].toMap();
    const QString id = row["id"].toString();
    if (modifiers & Qt::ShiftModifier) {
      if (_selected.contains(id))
        _selected.remove(id);
      else
        _selected.insert(id);
    } else if (!_selected.contains(id))
      _selected = {id};
    selectSourceNotes();
    _tail =
        rectangle(row).width() > 10 && point.x() >= rectangle(row).right() - 5;
    _gesture = true;
  } else if (_tool->currentIndex() == 1) {
    if (!(modifiers & Qt::ControlModifier))
      _selected.clear();
    _gesture = _marquee = true;
    _marqueeEnd = point;
  } else {
    const int track = _track->currentData().toInt();
    if (!editableTrack(track)) {
      setMessage(tr("当前声部已锁定，在“声部锁定”中解锁后可画音。"));
      return;
    }
    const auto scope = _state["scope"].toMap();
    if (_pressTick < scope["start"].toInt() ||
        _pressTick >= scope["end"].toInt()) {
      setMessage(tr("先选择这个小节，再点“改用当前选区”扩大写音范围。"));
      return;
    }
    const int quantum =
        rollGridQuantum(_pressTick, _grid->currentData().toInt(),
                        _grid->currentIndex() >= 4, track, _tuplets);
    const int start = qMax(scope["start"].toInt(), quantize(_pressTick, track));
    const int tpc = pitch2tpc(
        _pressPitch, _score->staff(track / 4)->key(Fraction::fromTicks(start)),
        Prefer::NEAREST);
    const auto id = freshId();
    _rows.append(QVariantMap{
        {"id", id},
        {"track", track},
        {"pitch", _pressPitch},
        {"tpc1", tpc},
        {"tpc2", tpc},
        {"start", start},
        {"end", qMin(start + quantum, scope["end"].toInt())},
        {"rawStart", _pressTick},
        {"rawEnd", qMin(_pressTick + quantum, scope["end"].toInt())},
        {"new", true},
        {"tripletGrid", _grid->currentIndex() >= 4},
        {"rasterQuantum", _grid->currentData()},
        {"edited", true},
        {"quantized", true},
        {"followDynamics", _followDynamics->isChecked()},
        {"velocity", _followDynamics->isChecked()
                         ? _score->staff(track / 4)->velocities().val(
                               Fraction::fromTicks(start))
                         : _fixedVelocity->value()}});
    _selected = {id};
    _gesture = true;
    _tail = true;
    _changed = true;
    _moveBase = _rows;
  }
  _gestureIndices.clear();
  for (int i = 0; i < _rows.size(); ++i)
    if (_selected.contains(_rows[i].toMap()["id"].toString()))
      _gestureIndices.append(i);
  if (_gesture)
    _editor->_noteCanvas->grabMouse();
  _editor->updateSurfaces();
}
void RollWritingBar::move(QPointF point) {
  if (!_gesture)
    return;
  if (_marquee) {
    _marqueeEnd = point;
    _editor->_noteCanvas->update();
    return;
  }
  const int tick = _editor->tickForX(point.x(), false),
            delta = tick - _pressTick,
            deltaPitch = pitchAt(point.y()) - _pressPitch;
  const auto scope = _state["scope"].toMap();
  const bool moved =
      (point - _press).manhattanLength() >= QApplication::startDragDistance();
  if (!moved && !_moved)
    return;
  _moved = true;
  auto source = _moveBase;
  if (_copy && moved) {
    source = _before;
    QSet<QString> copied;
    for (auto value : _before) {
      auto row = value.toMap();
      if (!_selected.contains(row["id"].toString()))
        continue;
      row["id"] = freshId();
      if (row.contains("sourceId"))
        row["copySourceId"] = row["sourceId"];
      row.remove("sourceId");
      row["new"] = true;
      source.append(row);
      copied.insert(row["id"].toString());
    }
    _selected = copied;
    _copy = false;
    _moveBase = source;
    _gestureIndices.clear();
    for (int i = 0; i < source.size(); ++i)
      if (_selected.contains(source[i].toMap()["id"].toString()))
        _gestureIndices.append(i);
  }
  _rows = source;
  for (int i : _gestureIndices) {
    auto row = _rows[i].toMap();
    if (!_selected.contains(row["id"].toString()) ||
        !editableTrack(row["track"].toInt()))
      continue;
    const int length = row["end"].toInt() - row["start"].toInt();
    if (_tail) {
      row["rawEnd"] =
          qBound(row["start"].toInt() + 1, tick, scope["end"].toInt());
      row["end"] = qMin(
          scope["end"].toInt(),
          qMax(row["start"].toInt() +
                   rollGridQuantum(row["start"].toInt(),
                                   _grid->currentData().toInt(),
                                   _grid->currentIndex() >= 4,
                                   row["track"].toInt(), _tuplets),
               quantize(row["rawEnd"].toInt(), row["track"].toInt(), row)));
    } else {
      const int start =
          qBound(scope["start"].toInt(), row["start"].toInt() + delta,
                 qMax(scope["start"].toInt(), scope["end"].toInt() - length));
      const int rawLength = row.value("rawEnd", row["end"]).toInt() -
                            row.value("rawStart", row["start"]).toInt(),
                offset = row.value("rawStart", row["start"]).toInt() -
                         row["start"].toInt();
      row["rawStart"] = start + offset;
      row["rawEnd"] = start + offset + rawLength;
      row["start"] = quantize(start, row["track"].toInt(), row);
      row["end"] = qMin(scope["end"].toInt(), row["start"].toInt() + length);
      row["pitch"] = qBound(0, row["pitch"].toInt() + deltaPitch, 127);
      const int tpc = pitch2tpc(row["pitch"].toInt(),
                                _score->staff(row["track"].toInt() / 4)
                                    ->key(Fraction::fromTicks(start)),
                                Prefer::NEAREST);
      row["tpc1"] = tpc;
      row["tpc2"] = tpc;
    }
    if (!row.value("tripletGrid").toBool() && _grid->currentIndex() >= 4) {
      row["tripletGrid"] = true;
      row["rasterQuantum"] = _grid->currentData();
    }
    row["edited"] = true;
    row["quantized"] = true;
    _rows[i] = row;
  }
  _changed = true;
  _editor->_noteCanvas->update();
}
void RollWritingBar::finish(QPointF point) {
  if (!_gesture)
    return;
  move(point);
  _editor->_noteCanvas->releaseMouse();
  _gesture = false;
  _indexDirty = true;
  if (_marquee) {
    const QRectF box = QRectF(_press, _marqueeEnd).normalized();
    for (auto value : _rows) {
      const auto row = value.toMap();
      if (editableTrack(row["track"].toInt()) && box.intersects(rectangle(row)))
        _selected.insert(row["id"].toString());
    }
    _marquee = false;
    selectSourceNotes();
    _editor->updateSurfaces();
    return;
  }
  if (_changed)
    calculate();
  else
    _editor->updateSurfaces();
}
void RollWritingBar::cancelGesture() {
  if (!_gesture)
    return;
  _rows = _before;
  _selected = _beforeSelected;
  _gesture = _marquee = false;
  _indexDirty = true;
  _editor->_noteCanvas->releaseMouse();
  _editor->updateSurfaces();
}
void RollWritingBar::eraseSelected() {
  if (!_ready || _busy || _selected.isEmpty())
    return;
  _before = _rows;
  _beforeSelected = _selected;
  QVariantList keep;
  for (auto value : _rows) {
    auto row = value.toMap();
    if (!_selected.contains(row["id"].toString()) ||
        !editableTrack(row["track"].toInt()))
      keep.append(row);
  }
  _rows = keep;
  _indexDirty = true;
  calculate();
}
void RollWritingBar::quantizeSelected() {
  if (!_ready || _busy || _selected.isEmpty()) {
    setMessage(tr("先选择要对齐网格的音符。"));
    return;
  }
  _before = _rows;
  _beforeSelected = _selected;
  for (auto &value : _rows) {
    auto row = value.toMap();
    if (_selected.contains(row["id"].toString()) &&
        editableTrack(row["track"].toInt())) {
      row["edited"] = true;
      row["quantized"] = false;
      row["tripletGrid"] = _grid->currentIndex() >= 4;
      row["rasterQuantum"] = _grid->currentData();
      value = row;
    }
  }
  calculate();
}
void RollWritingBar::duplicateSelected(int offset) {
  if (!_ready || _busy || _selected.isEmpty())
    return;
  _before = _rows;
  _beforeSelected = _selected;
  QSet<QString> copied;
  for (auto value : _before) {
    auto row = value.toMap();
    if (!_selected.contains(row["id"].toString()) ||
        !editableTrack(row["track"].toInt()))
      continue;
    row["id"] = freshId();
    if (row.contains("sourceId"))
      row["copySourceId"] = row["sourceId"];
    row.remove("sourceId");
    row["new"] = true;
    row["edited"] = true;
    row["start"] = row["start"].toInt() + offset;
    row["end"] = row["end"].toInt() + offset;
    row["rawStart"] = row["start"];
    row["rawEnd"] = row["end"];
    _rows.append(row);
    copied.insert(row["id"].toString());
  }
  _selected = copied;
  _indexDirty = true;
  calculate();
}
bool RollWritingBar::handle(QEvent *event) {
  if (!_enabled || !isVisible())
    return false;
  if (event->type() == QEvent::KeyPress ||
      event->type() == QEvent::ShortcutOverride) {
    auto key = static_cast<QKeyEvent *>(event);
    if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) {
      event->accept();
      if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
        const bool unfinished = _gesture;
        cancelGesture();
        if (unfinished) return true;
        stopAudition();
        getAction(key->matches(QKeySequence::Undo) ? "undo" : "redo")->trigger();
        contextChanged();
      }
      return true;
    }
    const bool escape = key->key() == Qt::Key_Escape,
               remove = key->key() == Qt::Key_Delete,
               all = key->matches(QKeySequence::SelectAll),
               copy = key->key() == Qt::Key_D &&
                      key->modifiers() == Qt::ControlModifier;
    if (escape || remove || all || copy) {
      event->accept();
      if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
        if (escape) {
          if (_gesture)
            cancelGesture();
          else if (_toggle->defaultAction())
            _toggle->defaultAction()->setChecked(false);
          else
            _toggle->setChecked(false);
        } else if (remove)
          eraseSelected();
        else if (all) {
          for (auto value : _rows) {
            const auto row = value.toMap();
            if (editableTrack(row["track"].toInt()))
              _selected.insert(row["id"].toString());
          }
          selectSourceNotes();
          _editor->updateSurfaces();
        } else
          duplicateSelected(_grid->currentData().toInt());
      }
      return true;
    }
  }
  if (event->type() == QEvent::ToolTip) {
    auto help = static_cast<QHelpEvent *>(event);
    const int index = hit(help->pos());
    if (index >= 0) {
      const auto row = _rows[index].toMap();
      QToolTip::showText(
          help->globalPos(),
          tr("%1 · 谱表 %2 · 声部 %3\n谱上 %4 拍 · 力度 %5")
              .arg(noteName(row["pitch"].toInt()))
              .arg(row["track"].toInt() / 4 + 1)
              .arg(row["track"].toInt() % 4 + 1)
              .arg(double(row["end"].toInt() - row["start"].toInt()) / 480, 0,
                   'g', 3)
              .arg(row["velocity"].toInt()),
          _editor->_noteCanvas);
    } else
      QToolTip::hideText();
    return true;
  }
  if (event->type() == QEvent::MouseButtonDblClick) {
    event->accept();
    return true;
  }
  if (event->type() == QEvent::MouseButtonPress ||
      event->type() == QEvent::MouseButtonRelease ||
      event->type() == QEvent::MouseMove) {
    auto mouse = static_cast<QMouseEvent *>(event);
    if (mouse->x() < PerformanceViewport::gutter && !_gesture)
      return false;
    if (!_ready || _busy) {
      if (event->type() == QEvent::MouseButtonPress)
        setMessage(_busy ? tr("正在完成刚才的写音，稍后可继续。")
                         : tr("先点“改用当前选区”读取要编辑的范围。"));
      return true;
    }
    if (event->type() == QEvent::MouseButtonPress &&
        mouse->button() == Qt::LeftButton) {
      begin(mouse->localPos(), mouse->modifiers());
      return true;
    }
    if (_gesture) {
      if (event->type() == QEvent::MouseButtonRelease)
        finish(mouse->localPos());
      else
        move(mouse->localPos());
      return true;
    }
    if (event->type() == QEvent::MouseMove) {
      const int index = hit(mouse->localPos());
      _editor->_noteCanvas->setCursor(
          index < 0 ? Qt::CrossCursor
          : mouse->x() >= rectangle(_rows[index].toMap()).right() - 5
              ? Qt::SizeHorCursor
              : Qt::SizeAllCursor);
      if (_editor->_hover >= 0)
        _editor->updateHover(-1);
      return true;
    }
  }
  return false;
}
bool RollWritingBar::dimsOriginal(int tick, int track) const {
  if (!_enabled || !_ready || !isVisible() ||
      (_mode->currentIndex() == 0 && !_gesture))
    return false;
  const auto scope = _state["scope"].toMap();
  return tick >= scope["start"].toInt() && tick < scope["end"].toInt() &&
         track >= scope["firstTrack"].toInt() &&
         track < scope["endTrack"].toInt() && !_locked.contains(track);
}
void RollWritingBar::paint(QPainter &p, const QRectF &bounds) {
  if (!_enabled || !_ready || !isVisible() ||
      (_mode->currentIndex() == 0 && !_gesture))
    return;
  ensureIndex();
  const QColor accent = palette().highlight().color();
  auto visible = _intervals->query(_editor->tickForX(bounds.left() - 2, false),
                                   _editor->tickForX(bounds.right() + 2, false),
                                   pitchAt(bounds.bottom()) - 1,
                                   pitchAt(bounds.top()) + 1);
  if (_gesture)
    for (int i : _gestureIndices)
      if (!visible.contains(i))
        visible.append(i);
  for (int index : visible) {
    const auto row = _rows[index].toMap();
    if (!editableTrack(row["track"].toInt()))
      continue;
    const auto rect = rectangle(row);
    if (!rect.intersects(bounds))
      continue;
    auto color = _editor->_appearance.noteColor(
        row.value("velocity", 80).toInt(), row["track"].toInt());
    if (_mode->currentIndex() == 1)
      color = _editor->_appearance.colors[PerformanceAppearance::Preview];
    color.setAlpha(210);
    p.setBrush(color);
    p.setPen(
        QPen(_selected.contains(row["id"].toString())
                 ? _editor->_appearance.colors[PerformanceAppearance::Selection]
                 : color.darker(140),
             _selected.contains(row["id"].toString()) ? 2 : 1));
    p.drawRect(rect);
    if (rect.width() > 35 && rect.height() >= 12) {
      p.setPen(color.lightness() > 150 ? Qt::black : Qt::white);
      p.drawText(rect.adjusted(3, 0, -2, 0), Qt::AlignVCenter,
                 noteName(row["pitch"].toInt()));
    }
  }
  if (_marquee) {
    p.setPen(QPen(accent, 1, Qt::DashLine));
    QColor fill = accent;
    fill.setAlpha(25);
    p.setBrush(fill);
    p.drawRect(QRectF(_press, _marqueeEnd).normalized());
  }
}
} // namespace Ms
