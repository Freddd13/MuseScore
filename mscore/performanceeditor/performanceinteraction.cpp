// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "performanceselection.h"
#include "mscore/seq.h"
#include "mscore/scoreview.h"
#include "libmscore/part.h"
#include "libmscore/instrument.h"
#include "libmscore/staff.h"
#include "libmscore/select.h"
#include "libmscore/repeatlist.h"
#include <QApplication>
#include <QComboBox>
#include <QScrollBar>
#include <QToolButton>
#include <QToolTip>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QMenu>
#include <algorithm>
#include <cmath>
namespace Ms {
bool PerformanceEditor::hasNoteValue(const NoteInfo& note) const
      { return note.base >= 0 || (_axis->currentIndex() ? note.type == Note::ValueType::OFFSET_VAL : note.type == Note::ValueType::USER_VAL); }
bool PerformanceEditor::noteEditable(const NoteInfo& note) const
      { return note.enabled && note.audible && hasNoteValue(note); }
void PerformanceEditor::rebuildFilter()
      {
      if (!_score || !_score->nstaves()) return;
      _contextTrack = qBound(0, _contextTrack, _score->nstaves() * VOICES - 1);
      const Part* part = _score->staff(_contextTrack / VOICES)->part();
      bool changed = false;
      for (auto& note : _notes) {
            const bool oldScope = note.inScope, oldEnabled = note.enabled;
            note.inScope = _scope->currentIndex() == 2 || (_scope->currentIndex() == 0 ? note.part == part : note.track / VOICES == _contextTrack / VOICES);
            note.enabled = note.inScope && (_voiceMask & (1 << (note.track % VOICES))) && (!_voice->currentIndex() || note.track % VOICES == _voice->currentIndex() - 1);
            changed |= oldScope != note.inScope || oldEnabled != note.enabled;
            }
      _hover = -1; updateSelection(); if (changed) invalidateVisual();
      }
void PerformanceEditor::selectIndices(const QVector<int>& indices, Qt::KeyboardModifiers modifiers, bool preserveGroup)
      {
      if (!_score || _selecting || _dirty) return;
      if (preserveGroup && !indices.isEmpty() && _notes[indices.front()].selected) return;
      QSet<Note*> chosen;
      if (modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier)) {
            if (_marquee) for (auto note : _selectionBefore) chosen.insert(note);
            else for (auto note : _score->selection().noteList()) chosen.insert(note);
            }
      Note* focus = nullptr;
      for (int i : indices) {
            if (i < 0 || i >= _notes.size() || !_notes[i].enabled) continue;
            Note* note = _notes[i].note;
            if ((modifiers & Qt::AltModifier) || ((modifiers & Qt::ControlModifier) && chosen.contains(note))) chosen.remove(note);
            else { chosen.insert(note); focus = note; }
            }
      QList<Note*> list; list.reserve(chosen.size());
      for (const auto& note : _notes) if (chosen.contains(note.note)) list.append(note.note);
      _selecting = true; PerformanceSelection::apply(_score, list, focus); _selecting = false;
      updateSelection();
      }
void PerformanceEditor::selectVoices(int scope)
      {
      if (!_score || _dirty) return;
      int from = _scroll->value(), until = from + _scroll->pageStep();
      if (scope == 2) { from = 0; until = _endTick; }
      else if (scope == 0 && _score->selection().isRange()) { from = _score->selection().tickStart().ticks(); until = _score->selection().tickEnd().ticks(); }
      QVector<int> indices;
      for (int index : _intervals.query(from, until)) if (_notes[index].enabled) indices.append(index);
      selectIndices(indices, Qt::NoModifier);
      }
QVector<int> PerformanceEditor::hits(QPointF point, bool parameter) const
      {
      QVector<int> result;
      if (point.x() < PerformanceViewport::gutter) return result;
      if (!parameter) {
            for (const auto& g : _noteGeometry) if (_notes[g.index].enabled && g.rect.contains(point)) result.append(g.index);
            }
      else {
            const int from = tickForX(point.x() - 8, false), until = tickForX(point.x() + 8, false);
            auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
            for (auto it = first; it != _notes.cend() && it->tick <= until; ++it) {
                  const int i = int(it - _notes.cbegin());
                  if (noteEditable(*it) && std::abs(xForTick(it->tick, false) - point.x()) < 7 && point.y() >= yForValue(noteValue(i), laneRect()) - 7 && point.y() <= laneRect().bottom() + 5) result.append(i);
                  }
            }
      std::sort(result.begin(), result.end(), [this](int a, int b) { return _notes[a].track < _notes[b].track; });
      return result;
      }
QString PerformanceEditor::noteTooltip(int i) const
      {
      const auto& note = _notes[i]; const auto edit = _pending.value(note.note, {note.type, note.raw});
      auto actual = [note](Note::ValueType type, int raw) { return note.base >= 0 ? QString::number(NoteVelocity::effective(note.base, type, raw)) : (type == Note::ValueType::USER_VAL ? QString::number(qBound(1, raw, 127)) : QObject::tr("基准待停播")); };
      return tr("记谱：%1\n实音：%2 · MIDI %3\n%4 · 谱表 %5 · 声部 %6\n小节拍位 %7 · 时值 %8\n已保存：%9%10 · MIDI %11\n当前预览：%12%13 · 实际 MIDI %14（整数舍入）")
            .arg(note.written, note.name).arg(note.pitch).arg(note.instrument).arg(note.track / VOICES + 1).arg(note.track % VOICES + 1)
            .arg(note.position, note.duration).arg(note.raw).arg(note.type == Note::ValueType::OFFSET_VAL ? " %" : "")
            .arg(actual(note.type, note.raw)).arg(edit.raw).arg(edit.type == Note::ValueType::OFFSET_VAL ? " %" : "")
            .arg(actual(edit.type, edit.raw));
      }
void PerformanceEditor::centerPitch(bool selection, bool full)
      {
      if (!_noteCanvas) return;
      pauseFollow(); int low = 127, high = 0; bool found = false;
      for (const auto& note : _notes) if (note.enabled && (!selection || note.selected)) { low = qMin(low, note.pitch); high = qMax(high, note.pitch); found = true; }
      if (!selection && _score && _score->nstaves() && _scope->currentIndex() != 2) {
            const auto instrument = _score->staff(_contextTrack / VOICES)->part()->instrument(Fraction::fromTicks(qMax(0, _playTick)));
            low = qBound(0, instrument->minPitchP(), 127); high = qBound(low, instrument->maxPitchP(), 127); found = true;
            }
      if (!found) { low = 48; high = 72; }
      if (full) _viewport.rowHeight = qBound(2.0, double(_noteCanvas->height()) / (high - low + 7), 24.0);
      _viewport.topPitch = (low + high) / 2.0 + _noteCanvas->height() / _viewport.rowHeight / 2 - 0.5;
      surfaceResized(PerformanceSurface::Notes);
      }
void PerformanceEditor::resetRange(bool dataRange)
      {
      pauseFollow(); PerformanceRange range = rangeKind() == 0 ? PerformanceRange{1, 127} : (rangeKind() == 1 ? PerformanceRange{-127, 12600} : (rangeKind() == 2 ? PerformanceRange{5, 999} : PerformanceRange{0, 127}));
      if (dataRange && rangeKind() < 3) {
            double low = range.maximum, high = range.minimum;
            if (rangeKind() == 2) for (const auto& segment : _segments) { low = qMin(low, segment.bpm); high = qMax(high, segment.bpm); }
            else for (int i = 0; i < _notes.size(); ++i) if (_notes[i].enabled && hasNoteValue(_notes[i])) { low = qMin(low, noteValue(i)); high = qMax(high, noteValue(i)); }
            if (low <= high) { const double pad = qMax(5.0, (high - low) * 0.1); range = {qMax(range.minimum, low - pad), qMin(range.maximum, high + pad)}; }
            }
      _viewport.ranges[rangeKind()] = range; invalidateVisual();
      }
void PerformanceEditor::pauseFollow()
      { _following = false; if (_followButton) { _followButton->setChecked(false); _followButton->setText(tr("跟随：暂停")); } }
void PerformanceEditor::syncTransport()
      {
      const bool playing = isVisible() && _score && seq && seq->isPlaying() && seq->score() == _score->masterScore();
      if (playing) { if (!_playTimer.isActive()) _playTimer.start(); }
      else { _playTimer.stop(); if (!_playingNotes.isEmpty()) { _playingNotes.clear(); updateSurfaces(); } }
      updatePlayhead();
      }
void PerformanceEditor::updatePlayhead()
      {
      if (!_score || !isVisible()) { _playTimer.stop(); return; }
      const bool playing = seq && seq->isPlaying() && seq->score() == _score->masterScore();
      if (!playing) _playTimer.stop();
      const int tick = playing ? _score->repeatList().utick2tick(seq->getCurTick()) : _score->playPos().ticks();
      const int previous = _playTick; _playTick = tick;
      if (_following && playing && !_dragging && !_marquee && !_seeking) {
            _followButton->setText(tr("跟随播放"));
            const int span = _scroll->pageStep();
            if (tick < _scroll->value() || tick > _scroll->value() + span * 0.85) {
                  _automaticScroll = true; _scroll->setValue(qMax(0, tick - int(span * 0.12))); _automaticScroll = false;
                  }
            }
      if (previous == tick) return;
      for (QWidget* canvas : {static_cast<QWidget*>(_noteCanvas), static_cast<QWidget*>(_ruler), _canvas}) {
            const int oldX = qRound(xForTick(previous, false)), newX = qRound(xForTick(tick, false));
            if (previous >= 0) canvas->update(QRect(oldX - 3, 0, 7, canvas->height()));
            canvas->update(QRect(newX - 3, 0, 7, canvas->height()));
            }
      }
void PerformanceEditor::playbackNotes()
      {
      if (!_playTimer.isActive() || !seq) return;
      QSet<int> active;
      for (const auto& event : seq->activeNoteEvents()) for (int index : _linkedIndex.values(event.owner)) active.insert(index);
      if (active == _playingNotes) return;
      QSet<int> changed = active; changed.unite(_playingNotes); _playingNotes = active; buildGeometry();
      for (const auto& g : _noteGeometry) if (changed.contains(g.index)) _noteCanvas->update(g.rect.adjusted(-3, -3, 3, 3).toAlignedRect());
      for (int i : changed) { if (i < 0 || i >= _notes.size()) continue; const int x = qRound(xForTick(_notes[i].tick, false)); _canvas->update(QRect(x - 8, 0, 17, _canvas->height())); }
      }
void PerformanceEditor::updateHover(int index)
      {
      if (index == _hover) return;
      const int before = _hover; _hover = index; buildGeometry();
      for (const auto& geometry : _noteGeometry) if (geometry.index == before || geometry.index == index) _noteCanvas->update(geometry.rect.adjusted(-3, -24, 3, 3).toAlignedRect());
      for (int i : {before, index}) if (i >= 0 && i < _notes.size()) { const int x = qRound(xForTick(_notes[i].tick, false)); _canvas->update(QRect(x - 8, 0, 17, _canvas->height())); }
      }
void PerformanceEditor::seek(int tick)
      {
      if (!_score) return;
      if (seq && seq->isPlaying() && seq->score() == _score->masterScore()) seq->seek(_score->repeatList().tick2utick(tick));
      else _score->setPlayPos(Fraction::fromTicks(tick));
      updatePlayhead();
      }
void PerformanceEditor::cancelSurfaceGesture()
      {
      if (!_marquee && !_seeking) return;
      _marquee = _seeking = false; _selectionBefore.clear();
      qApp->removeEventFilter(this); if (_noteCanvas) _noteCanvas->releaseMouse(); if (_ruler) _ruler->releaseMouse(); updateSurfaces();
      }
void PerformanceEditor::queueGesture(QPointF point)
      { _queuedPoint = point; _moveQueued = true; if (!_moveTimer.isActive()) _moveTimer.start(); }
void PerformanceEditor::drainGesture()
      { _moveTimer.stop(); if (!_moveQueued) return; _moveQueued = false; moveGesture(_queuedPoint); }
bool PerformanceEditor::surfaceEvent(QObject* object, QEvent* event)
      {
      if ((_marquee || _seeking) && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept(); if (event->type() == QEvent::KeyPress) cancelSurfaceGesture(); return true;
            }
      const bool notes = object == _noteCanvas, ruler = object == _ruler, parameter = object == _canvas;
      if (!notes && !ruler && !parameter) return false;
      if (event->type() == QEvent::Show) { scheduleRefresh(); syncTransport(); }
      if (event->type() == QEvent::Hide) { cancelSurfaceGesture(); cancelGesture(); _playTimer.stop(); _playingNotes.clear(); }
      if (event->type() == QEvent::Leave && !_dragging && !_marquee) updateHover(-1);
      if (event->type() == QEvent::Wheel) {
            auto wheel = static_cast<QWheelEvent*>(event); pauseFollow(); cancelGesture();
            const double factor = wheel->angleDelta().y() > 0 ? 1.2 : 1 / 1.2;
            if (wheel->modifiers() & Qt::ControlModifier) {
                  if (wheel->modifiers() & Qt::ShiftModifier) {
                        if (notes) { const double pitch = _viewport.topPitch - wheel->position().y() / _viewport.rowHeight; _viewport.rowHeight = qBound(2.0, _viewport.rowHeight * factor, 36.0); _viewport.topPitch = pitch + wheel->position().y() / _viewport.rowHeight; }
                        else _viewport.zoomRange(rangeKind(), factor, valueForY(wheel->position().y(), laneRect()));
                        }
                  else {
                        const int tick = tickForX(wheel->position().x(), false);
                        _viewport.pixelsPerQuarter = qBound(0.01, _viewport.pixelsPerQuarter * factor, 1000.0);
                        surfaceResized(PerformanceSurface::Parameter);
                        _scroll->setValue(qMax(0, tick - qRound((wheel->position().x() - PerformanceViewport::gutter) * DIVISION / _viewport.pixelsPerQuarter)));
                        }
                  }
            else if (wheel->modifiers() & Qt::ShiftModifier) _scroll->setValue(_scroll->value() - wheel->angleDelta().y() * DIVISION / 120);
            else if (notes) _viewport.topPitch += wheel->angleDelta().y() / 40.0;
            else { auto r = _viewport.ranges[rangeKind()]; _viewport.panRange(rangeKind(), wheel->angleDelta().y() / 120.0 * (r.maximum - r.minimum) / 8); }
            surfaceResized(PerformanceSurface::Notes); invalidateVisual(); return true;
            }
      if (event->type() == QEvent::ToolTip) {
            auto help = static_cast<QHelpEvent*>(event); buildGeometry();
            auto candidates = hits(help->pos(), parameter);
            if (!candidates.isEmpty()) QToolTip::showText(help->globalPos(), noteTooltip(candidates.front()), static_cast<QWidget*>(object));
            else if (notes && help->pos().x() < PerformanceViewport::gutter) {
                  const int pitch = qBound(0, int(std::ceil(_viewport.topPitch - help->pos().y() / _viewport.rowHeight)), 127);
                  static const char* names[] = {"C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"};
                  QToolTip::showText(help->globalPos(), tr("实音 %1%2 · MIDI %3").arg(QString::fromUtf8(names[pitch % 12])).arg(pitch / 12 - 1).arg(pitch), _noteCanvas);
                  }
            else QToolTip::hideText();
            return true;
            }
      if (event->type() == QEvent::MouseMove) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (_seeking) { _seekTick = tickForX(mouse->x(), false); updateSurfaces(); return true; }
            if (_marquee) { _marqueeEnd = mouse->pos(); _noteCanvas->update(); return true; }
            if (!_dragging && !ruler) { buildGeometry(); const auto candidates = hits(mouse->pos(), parameter); const int hover = candidates.isEmpty() ? -1 : candidates.front(); if (_hover != hover) updateHover(hover); }
            }
      if (event->type() == QEvent::MouseButtonPress) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (!_score || _dirty) return false;
            if (ruler && mouse->button() == Qt::LeftButton && mouse->x() >= PerformanceViewport::gutter) {
                  pauseFollow(); _seeking = true; _seekTick = tickForX(mouse->x(), false); _ruler->grabMouse(); qApp->installEventFilter(this); updateSurfaces(); return true;
                  }
            if (notes || (parameter && _parameter->currentIndex() == 0)) {
                  buildGeometry(); const auto candidates = hits(mouse->pos(), parameter);
                  if (mouse->button() == Qt::RightButton && !candidates.isEmpty()) {
                        QMenu menu; for (int i : candidates) { auto action = menu.addAction(tr("%1 · 谱表 %2 · 声部 %3 · %4").arg(_notes[i].name).arg(_notes[i].track / VOICES + 1).arg(_notes[i].track % VOICES + 1).arg(_notes[i].base < 0 && _notes[i].type == Note::ValueType::OFFSET_VAL ? tr("相对 %1%").arg(_notes[i].raw) : tr("MIDI %1").arg(NoteVelocity::effective(_notes[i].base, _notes[i].type, _notes[i].raw)))); connect(action, &QAction::triggered, this, [this, i] { selectIndices({i}, Qt::NoModifier); }); }
                        menu.exec(mouse->globalPos()); return true;
                        }
                  if (mouse->button() != Qt::LeftButton) return false;
                  if (notes) {
                        pauseFollow();
                        if (!candidates.isEmpty()) selectIndices({candidates.front()}, mouse->modifiers());
                        else if (mouse->x() >= PerformanceViewport::gutter) { _marquee = true; _marqueeStart = _marqueeEnd = mouse->pos(); _selectionModifiers = mouse->modifiers(); _selectionBefore.clear(); for (auto note : _score->selection().noteList()) _selectionBefore.append(note); _noteCanvas->grabMouse(); qApp->installEventFilter(this); }
                        return true;
                        }
                  if (!candidates.isEmpty()) { selectIndices({candidates.front()}, mouse->modifiers(), !(mouse->modifiers() & Qt::ControlModifier)); if (mouse->modifiers() & Qt::ControlModifier) return true; beginGesture(mouse->pos(), laneRect(), false, candidates.front()); return _dragging; }
                  }
            }
      if (event->type() == QEvent::MouseButtonRelease) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton) return false;
            if (_seeking) { const int tick = tickForX(mouse->x(), false); cancelSurfaceGesture(); seek(tick); return true; }
            if (_marquee) {
                  _marqueeEnd = mouse->pos(); const QRectF box = QRectF(_marqueeStart, _marqueeEnd).normalized(); QVector<int> indices;
                  buildGeometry(); for (const auto& g : _noteGeometry) if (g.rect.intersects(box) && _notes[g.index].enabled) indices.append(g.index);
                  selectIndices(indices, _selectionModifiers); cancelSurfaceGesture(); return true;
                  }
            if (_dragging) { queueGesture(mouse->pos()); finishGesture(); updateSurfaces(); return true; }
            }
      return false;
      }
}
