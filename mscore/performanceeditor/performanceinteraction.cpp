// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "performanceselection.h"
#include "mscore/seq.h"
#include "mscore/scoreview.h"
#include "mscore/musescore.h"
#include "libmscore/chord.h"
#include "libmscore/synthesizerstate.h"
#include <QSignalBlocker>
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
QVector<int> PerformanceEditor::hits(QPointF point, bool parameter, bool onScore) const
      {
      QVector<int> result;
      if (!onScore && point.x() < PerformanceViewport::gutter) return result;
      const QRectF lane = onScore ? _scoreLane : laneRect();
      if (!parameter) {
            for (const auto& g : _noteGeometry) if (_notes[g.index].enabled && g.rect.adjusted(g.rect.width() < 4 ? -2 : 0, 0, g.rect.width() < 4 ? 2 : 0, 0).contains(point)) result.append(g.index);
            }
      else {
            const int from = tickForX(point.x() - 8, onScore), until = tickForX(point.x() + 8, onScore);
            auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
            for (auto it = first; it != _notes.cend() && it->tick <= until; ++it) {
                  const int i = int(it - _notes.cbegin());
                  if (noteEditable(*it) && (!onScore || it->system == _system) && std::abs(xForTick(it->tick, onScore) - point.x()) < 7
                        && point.y() >= qMax(lane.top(), yForValue(noteValue(i), lane)) - 7 && point.y() <= lane.bottom() + 5) result.append(i);
                  }
            }
      std::stable_sort(result.begin(), result.end(), [this, point, parameter, lane](int a, int b) {
            if (parameter) {
                  const int da = qRound(std::abs(yForValue(noteValue(a), lane) - point.y())), db = qRound(std::abs(yForValue(noteValue(b), lane) - point.y()));
                  if ((da <= 7) != (db <= 7)) return da <= 7;
                  if (da != db && (da <= 7 || db <= 7)) return da < db;
                  if (_notes[a].selected != _notes[b].selected) return _notes[a].selected;
                  if (da != db) return da < db;
                  }
            else if (_notes[a].selected != _notes[b].selected) return _notes[a].selected;
            return _notes[a].track != _notes[b].track ? _notes[a].track < _notes[b].track : _notes[a].pitch < _notes[b].pitch;
            });
      return result;
      }
void PerformanceEditor::cycleHit(const QVector<int>& candidates, int step)
      {
      if (!_score || _dirty || candidates.isEmpty()) return;
      auto order = candidates;
      const int tick = _notes[candidates.front()].tick;
      order.erase(std::remove_if(order.begin(), order.end(), [this, tick](int i) { return _notes[i].tick != tick; }), order.end());
      std::sort(order.begin(), order.end());
      int current = -1;
      for (int i = 0; i < order.size(); ++i) if (_notes[order[i]].selected) { current = i; break; }
      const int target = order[(current + step + order.size()) % order.size()];
      selectIndices({target}, Qt::NoModifier); updateHover(target); audition(target);
      }
void PerformanceEditor::audition(int index)
      {
      if (!_score || _dirty || !_auditionButton->isChecked() || !seq || !seq->isRunning() || seq->isPlaying() || seq->score() != _score->masterScore() || index < 0 || index >= _notes.size()) return;
      const auto& info = _notes[index];
      if (!info.enabled || !info.audible) return;
      const Note* master = info.note;
      if (master->score() != _score->masterScore()) for (auto linked : master->linkList())
            if (linked->isNote() && linked->score() == _score->masterScore()) { master = toNote(linked); break; }
      const auto instrument = master->part()->instrument(master->tick());
      const int channel = instrument->channel(master->subchannel())->channel();
      const int velocity = info.base >= 0 ? NoteVelocity::effective(info.base, info.type, info.raw) : (info.type == Note::ValueType::USER_VAL ? qBound(1, info.raw, 127) : 80);
      const int cc = mscore ? mscore->synthesizerState().ccToUse() : -1;
      if (cc != -1) seq->sendEvent(NPlayEvent(ME_CONTROLLER, channel, cc, 80));
      seq->startNote(channel, info.pitch, velocity, MScore::defaultPlayDuration, info.note->tuning());
      }
void PerformanceEditor::auditionPitch(int pitch)
      {
      if (!_score || !_score->nstaves() || !_auditionButton->isChecked() || !seq || !seq->isRunning() || seq->isPlaying() || seq->score() != _score->masterScore()) return;
      const auto instrument = _score->staff(_contextTrack / VOICES)->part()->instrument(Fraction::fromTicks(qMax(0, _playTick)));
      const int channel = instrument->channel(0)->channel(), cc = mscore ? mscore->synthesizerState().ccToUse() : -1;
      if (cc != -1) seq->sendEvent(NPlayEvent(ME_CONTROLLER, channel, cc, 80));
      seq->startNote(channel, pitch, 80, MScore::defaultPlayDuration, 0);
      }
void PerformanceEditor::togglePlayback(bool startOnly)
      {
      if (!_view || !_score || !seq || !seq->isRunning()) { status(tr("播放不可用，请检查音频设备。")); return; }
      if (startOnly && seq->isPlaying()) return;
      _view->cmd("play"); syncTransport();
      }
void PerformanceEditor::locateSelection()
      {
      if (!_score) return;
      const auto selected = _score->selection().noteList();
      if (!selected.empty()) seek(selected.front()->tick().ticks());
      }
void PerformanceEditor::zoomVertical(bool notes, double factor)
      {
      pauseFollow(); cancelGesture();
      if (notes) {
            const double middle = _viewport.topPitch - _noteCanvas->height() / (2 * _viewport.rowHeight);
            _viewport.rowHeight = qBound(2.0, _viewport.rowHeight * factor, 36.0);
            _viewport.topPitch = middle + _noteCanvas->height() / (2 * _viewport.rowHeight);
            }
      else {
            const auto range = _viewport.ranges[rangeKind()];
            const int focus = _hover >= 0 ? _hover : (_selectedIndices.isEmpty() ? -1 : _selectedIndices.front());
            const double anchor = rangeKind() < 2 && focus >= 0 ? noteValue(focus) : (range.minimum + range.maximum) / 2;
            _viewport.zoomRange(rangeKind(), factor, qBound(range.minimum, anchor, range.maximum));
            }
      surfaceResized(PerformanceSurface::Notes); invalidateVisual();
      }
void PerformanceEditor::syncValueScroll()
      {
      if (!_valueScroll) return;
      const auto range = _viewport.ranges[rangeKind()], limits = PerformanceViewport::limits(rangeKind());
      const double span = range.maximum - range.minimum, movement = limits.maximum - limits.minimum - span;
      QSignalBlocker blocker(_valueScroll);
      _valueScroll->setRange(0, movement > 0.01 && rangeKind() != 3 ? 10000 : 0);
      _valueScroll->setPageStep(qBound(1, qRound(span / qMax(0.01, movement) * 10000), 10000));
      _valueScroll->setSingleStep(100);
      _valueScroll->setValue(movement > 0.01 ? qRound((limits.maximum - range.maximum) / movement * 10000) : 0);
      _valueScroll->setEnabled(movement > 0.01 && rangeKind() != 3);
      }
void PerformanceEditor::applyAppearance()
      {
      setAttribute(Qt::WA_StyledBackground, true);
      setStyleSheet(QString("QWidget#performanceEditor {background:%1;} QLabel,QCheckBox {color:%2;} QToolButton,QPushButton,QComboBox,QDoubleSpinBox {background:#484848;color:%2;border:1px solid #696969;border-radius:2px;padding:3px;} QToolButton:hover,QPushButton:hover {background:#595959;} QToolButton:checked {background:#626d5c;} QScrollBar {background:#303030;} QScrollBar::handle {background:#797979;min-height:12px;min-width:12px;} QScrollBar::add-line,QScrollBar::sub-line {width:0;height:0;} QScrollBar::add-page,QScrollBar::sub-page {background:#383838;} QScrollBar::handle:disabled {background:#454545;} QSplitter::handle {background:#686868;}").arg(_appearance.colors[PerformanceAppearance::Background].name(), _appearance.colors[PerformanceAppearance::Text].name()));
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
            else {
                  const auto indices = _selectedIndices.isEmpty() ? _intervals.query(_scroll->value(), _scroll->value() + _scroll->pageStep()) : _selectedIndices;
                  for (int i : indices) if (noteEditable(_notes[i])) { low = qMin(low, noteValue(i)); high = qMax(high, noteValue(i)); }
                  }
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
      _playButton->setText(playing ? tr("■ 停止") : tr("▶ 播放"));
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
      if (_view && _band->isChecked()) _view->update(_scoreLane.adjusted(-4, -24, 4, 4).toAlignedRect());
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
      if (_dirty || index >= _notes.size()) index = -1;
      if (index == _hover) return;
      const int before = _hover; _hover = index; buildGeometry();
      if (index >= 0 && _notes[index].system != _system) updateSelection();
      if (_view && (_handles->isChecked() || _band->isChecked())) {
            invalidateOverlay();
            for (int i : {before, index}) if (i >= 0 && i < _notes.size()) _view->update(_view->matrix().mapRect(_notes[i].bounds).adjusted(-5, -35, 95, 5).toAlignedRect());
            }
      for (const auto& geometry : _noteGeometry) if (geometry.index == before || geometry.index == index) _noteCanvas->update(geometry.rect.adjusted(-3, -24, 3, 3).toAlignedRect());
      for (int i : {before, index}) if (i >= 0 && i < _notes.size()) { const int x = qRound(xForTick(_notes[i].tick, false)); _canvas->update(QRect(x - 8, 0, 17, _canvas->height())); }
      }
void PerformanceEditor::seek(int tick)
      {
      if (!_score) return;
      if (seq && seq->isPlaying() && seq->score() == _score->masterScore()) seq->seek(_score->repeatList().tick2utick(tick));
      else _score->setPlayPos(Fraction::fromTicks(tick));
      if (_view && (!seq || !seq->isPlaying())) _view->moveCursor(Fraction::fromTicks(tick));
      updatePlayhead();
      }
void PerformanceEditor::cancelSurfaceGesture()
      {
      if (!_marquee && !_seeking && !_rangePanning) return;
      _marquee = _seeking = _rangePanning = false; _selectionBefore.clear(); if (_canvas) _canvas->unsetCursor();
      qApp->removeEventFilter(this); if (_noteCanvas) _noteCanvas->releaseMouse(); if (_ruler) _ruler->releaseMouse(); if (_canvas) _canvas->releaseMouse(); updateSurfaces();
      }
void PerformanceEditor::queueGesture(QPointF point)
      { _queuedPoint = point; _moveQueued = true; if (!_moveTimer.isActive()) _moveTimer.start(); }
void PerformanceEditor::drainGesture()
      { _moveTimer.stop(); if (!_moveQueued) return; _moveQueued = false; moveGesture(_queuedPoint); }
bool PerformanceEditor::surfaceEvent(QObject* object, QEvent* event)
      {
      if ((_marquee || _seeking || _rangePanning) && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept(); if (event->type() == QEvent::KeyPress) cancelSurfaceGesture(); return true;
            }
      const bool notes = object == _noteCanvas, ruler = object == _ruler, parameter = object == _canvas;
      if (!notes && !ruler && !parameter) return false;
      if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Space) {
            event->accept(); if (event->type() == QEvent::KeyPress) togglePlayback(); return true;
            }
      if (event->type() == QEvent::Show) { scheduleRefresh(); syncTransport(); syncOverlayTracking(); }
      if (event->type() == QEvent::Hide) { finishWheel(); cancelSurfaceGesture(); cancelGesture(); _playTimer.stop(); _playingNotes.clear(); invalidateOverlay(); syncOverlayTracking(); }
      if (event->type() == QEvent::Leave && !_dragging && !_marquee) updateHover(-1);
      if (event->type() == QEvent::Wheel) {
            auto wheel = static_cast<QWheelEvent*>(event);
            if (!ruler && wheelVelocity(wheel, notes)) return true;
            finishWheel(); pauseFollow(); cancelSurfaceGesture(); cancelGesture();
            const double factor = wheel->angleDelta().y() > 0 ? 1.2 : 1 / 1.2;
            if (wheel->modifiers() & Qt::ControlModifier) {
                  if ((wheel->modifiers() & Qt::ShiftModifier) || wheel->position().x() < PerformanceViewport::gutter) {
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
            else if (wheel->position().x() < PerformanceViewport::gutter) _viewport.zoomRange(rangeKind(), factor, valueForY(wheel->position().y(), laneRect()));
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
            if (_dragging && notes) { queueGesture(mouse->pos()); return true; }
            if (_rangePanning) {
                  const auto range = _viewport.ranges[rangeKind()];
                  _viewport.panRange(rangeKind(), (mouse->y() - _last.y()) * (range.maximum - range.minimum) / laneRect().height());
                  _last = mouse->pos(); invalidateVisual(); return true;
                  }
            if (_seeking) { _seekTick = tickForX(mouse->x(), false); updateSurfaces(); return true; }
            if (_marquee) { _marqueeEnd = mouse->pos(); _noteCanvas->update(); return true; }
            if (!_dragging && !ruler) { buildGeometry(); const auto candidates = hits(mouse->pos(), parameter); const int hover = candidates.isEmpty() ? -1 : candidates.front(); if (_hover != hover) updateHover(hover); }
            }
      if (event->type() == QEvent::MouseButtonPress) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (!_score || _dirty) return false;
            if (parameter && mouse->button() == Qt::LeftButton && mouse->x() < PerformanceViewport::gutter && rangeKind() != 3) {
                  pauseFollow(); _rangePanning = true; _last = mouse->pos(); _canvas->setCursor(Qt::ClosedHandCursor); _canvas->grabMouse(); qApp->installEventFilter(this); return true;
                  }
            if (ruler && mouse->button() == Qt::LeftButton && mouse->x() >= PerformanceViewport::gutter) {
                  pauseFollow(); _seeking = true; _seekTick = tickForX(mouse->x(), false); _ruler->grabMouse(); qApp->installEventFilter(this); updateSurfaces(); return true;
                  }
            if (notes || (parameter && _parameter->currentIndex() == 0)) {
                  buildGeometry(); const auto candidates = hits(mouse->pos(), parameter);
                  if (mouse->button() == Qt::RightButton && !candidates.isEmpty()) {
                        QMenu menu; for (int i : candidates) { auto action = menu.addAction(tr("%1 · 谱表 %2 · 声部 %3 · %4").arg(_notes[i].name).arg(_notes[i].track / VOICES + 1).arg(_notes[i].track % VOICES + 1).arg(_notes[i].base < 0 && _notes[i].type == Note::ValueType::OFFSET_VAL ? tr("相对 %1%").arg(_notes[i].raw) : tr("MIDI %1").arg(NoteVelocity::effective(_notes[i].base, _notes[i].type, _notes[i].raw)))); connect(action, &QAction::triggered, this, [this, i, modifiers = mouse->modifiers()] { selectIndices({i}, modifiers); updateHover(i); audition(i); }); }
                        menu.exec(mouse->globalPos()); return true;
                        }
                  if (mouse->button() != Qt::LeftButton) return false;
                  if (notes && mouse->x() < 32) { auditionPitch(qBound(0, int(std::ceil(_viewport.topPitch - mouse->y() / _viewport.rowHeight)), 127)); return true; }
                  if (mouse->modifiers() & Qt::AltModifier && !candidates.isEmpty() && parameter) { cycleHit(candidates); return true; }
                  if (notes) {
                        pauseFollow();
                        if (!candidates.isEmpty()) {
                              if (mouse->modifiers() & Qt::AltModifier && _parameter->currentIndex() == 0) {
                                    selectIndices({candidates.front()}, mouse->modifiers() & ~Qt::AltModifier, true);
                                    beginGesture(mouse->pos(), laneRect(), false, candidates.front());
                                    if (_dragging) { _noteGesture = true; _canvas->releaseMouse(); _noteCanvas->grabMouse(); }
                                    }
                              else if (candidates.size() > 1 && _notes[candidates.front()].selected && _selectedIndices.size() == 1 && mouse->modifiers() == Qt::NoModifier) cycleHit(candidates);
                              else { selectIndices({candidates.front()}, mouse->modifiers()); updateHover(candidates.front()); audition(candidates.front()); }
                              }
                        else if (mouse->x() >= PerformanceViewport::gutter) { _marquee = true; _marqueeStart = _marqueeEnd = mouse->pos(); _selectionModifiers = mouse->modifiers(); _selectionBefore.clear(); for (auto note : _score->selection().noteList()) _selectionBefore.append(note); _noteCanvas->grabMouse(); qApp->installEventFilter(this); }
                        return true;
                        }
                  if (!candidates.isEmpty()) {
                        const int target = candidates.front(); const bool cycle = _notes[target].selected && _selectedIndices.size() == 1 && candidates.size() > 1
                              && std::abs(yForValue(noteValue(candidates[1]), laneRect()) - yForValue(noteValue(target), laneRect())) < 3 && mouse->modifiers() == Qt::NoModifier;
                        selectIndices({target}, mouse->modifiers(), !(mouse->modifiers() & Qt::ControlModifier)); updateHover(target); audition(target);
                        if (mouse->modifiers() & Qt::ControlModifier) return true;
                        beginGesture(mouse->pos(), laneRect(), false, target); _cycleOnClick = cycle; _pressedCandidates = candidates; return _dragging;
                        }
                  if (parameter && _tool->currentIndex() == 0 && laneRect().contains(mouse->pos())) { pauseFollow(); _seeking = true; _seekTick = tickForX(mouse->x(), false); _canvas->grabMouse(); qApp->installEventFilter(this); return true; }
                  }
            }
      if (event->type() == QEvent::MouseButtonDblClick) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && mouse->x() >= PerformanceViewport::gutter) {
                  cancelSurfaceGesture(); cancelGesture(); buildGeometry(); const auto candidates = hits(mouse->pos(), parameter);
                  seek(candidates.isEmpty() ? tickForX(mouse->x(), false) : _notes[candidates.front()].tick); togglePlayback(true); return true;
                  }
            }
      if (event->type() == QEvent::MouseButtonRelease) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton) return false;
            if (_rangePanning) { cancelSurfaceGesture(); return true; }
            if (_seeking) { const int tick = tickForX(mouse->x(), false); cancelSurfaceGesture(); seek(tick); return true; }
            if (_marquee) {
                  _marqueeEnd = mouse->pos(); const QRectF box = QRectF(_marqueeStart, _marqueeEnd).normalized(); QVector<int> indices;
                  buildGeometry(); for (const auto& g : _noteGeometry) if (g.rect.intersects(box) && _notes[g.index].enabled) indices.append(g.index);
                  const bool click = QLineF(_marqueeStart, _marqueeEnd).length() < 3 && _selectionModifiers == Qt::NoModifier;
                  selectIndices(indices, _selectionModifiers); cancelSurfaceGesture(); if (click) seek(tickForX(mouse->x(), false)); return true;
                  }
            if (_dragging) { queueGesture(mouse->pos()); finishGesture(); updateSurfaces(); return true; }
            }
      return false;
      }
}
