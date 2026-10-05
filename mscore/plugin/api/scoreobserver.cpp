//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2026 Freddd13 and contributors
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "scoreobserver.h"
#include "score.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/sig.h"
#include "libmscore/staff.h"
#include <QElapsedTimer>
#include <QQuickItem>
#include <QQuickWindow>
#include <algorithm>

namespace Ms {
namespace PluginAPI {

ScoreObserver::ScoreObserver(QObject* parent) : QObject(parent)
      {
      // These signals are emitted by Seq's GUI-side heartbeat, never the audio callback.
      if (seq) {
            connect(seq, &Seq::heartBeat, this, [this](int tick, int, int) {
                  if (_score && seq->score() == _score->masterScore()) {
                        _tick = tick;
                        notifyPosition();
                        }
                  });
            connect(seq, &Seq::started, this, &ScoreObserver::notifyPosition);
            connect(seq, &Seq::stopped, this, &ScoreObserver::notifyPosition);
            }
      watchSurface();
      }

ScoreObserver::~ScoreObserver() { clearNotePreviewColors(); }

void ScoreObserver::watchSurface()
      {
      auto item = qobject_cast<QQuickItem*>(parent());
      if (!item) return;
      auto attach = [this](QQuickWindow* window) {
            if (!window) return;
            _surfaceVisible = window->isVisible();
            connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
                  _surfaceVisible = visible;
                  if (!visible) clearNotePreviewColors();
                  emit surfaceVisibleChanged();
                  });
            };
      connect(item, &QQuickItem::windowChanged, this, attach);
      attach(item->window());
      connect(item, &QQuickItem::visibleChanged, this, [this, item]() {
            if (!item->isVisible()) clearNotePreviewColors();
            });
      }

Score* ScoreObserver::wrappedScore() const
      { return _score ? wrap<Score>(_score.data(), Ownership::SCORE) : nullptr; }

void ScoreObserver::setScore(Score* wrapped)
      {
      Ms::Score* score = wrapped ? wrapped->score() : nullptr;
      if (_score == score) return;
      clearNotePreviewColors();
      for (const auto& connection : _scoreConnections) disconnect(connection);
      _scoreConnections.clear();
      _score = score;
      _index.clear();
      _firstTrack = _endTrack = -1;
      _tick = -1;
      if (_score) {
            _scoreConnections.append(connect(_score->masterScore(), &Ms::Score::posChanged,
                  this, [this](POS pos, unsigned tick) {
                        if (pos == POS::CURRENT) { _tick = int(tick); notifyPosition(); }
                        }));
            _scoreConnections.append(connect(_score.data(), &QObject::destroyed, this, [this]() {
                  clearNotePreviewColors();
                  _score = nullptr;
                  _index.clear();
                  emit scoreChanged();
                  notifyPosition();
                  }));
            }
      emit scoreChanged();
      notifyPosition();
      }

bool ScoreObserver::playing() const
      { return _score && seq && seq->isPlaying() && seq->score() == _score->masterScore(); }

int ScoreObserver::tick() const
      { return _score ? (_tick >= 0 ? _tick : _score->masterScore()->playPos().ticks()) : -1; }

void ScoreObserver::notifyPosition()
      { if (_enabled && _surfaceVisible) emit positionChanged(); }

void ScoreObserver::setEnabled(bool enabled)
      {
      if (_enabled == enabled) return;
      _enabled = enabled;
      if (!enabled) clearNotePreviewColors();
      emit enabledChanged();
      if (enabled) notifyPosition();
      }

QVariantMap ScoreObserver::describe(const Ms::Note* note, int pitch, bool eventPitch) const
      {
      const auto& notes = note->chord()->notes();
      const auto position = std::find(notes.begin(), notes.end(), note);
      QVariantMap result {
            {"tick", note->chord()->tick().ticks()}, {"track", note->track()},
            {"index", int(position - notes.begin())}, {"pitch", pitch},
            {"writtenPitch", note->pitch()}, {"writtenTpc", note->tpc()}
            };
      if (!eventPitch) result.insert("tpc", note->tpc1());
      return result;
      }

void ScoreObserver::ensureIndex(int firstTrack, int endTrack)
      {
      const auto state = _score->masterScore()->state();
      if (_firstTrack == firstTrack && _endTrack == endTrack && _indexState == state) return;
      QElapsedTimer timer;
      timer.start();
      _index.clear();
      _index.resize(endTrack - firstTrack);
      for (auto segment = _score->firstSegment(SegmentType::ChordRest); segment;
            segment = segment->next1(SegmentType::ChordRest)) {
            for (int track = firstTrack; track < endTrack; ++track) {
                  const auto element = segment->element(track);
                  if (!element || (!element->isChord() && !element->isRest())) continue;
                  const auto cr = toChordRest(element);
                  Event event {segment->tick().ticks(), cr->endTick().ticks(), {}};
                  if (element->isChord())
                        for (const auto note : toChord(element)->notes())
                              event.notes.append(describe(note, note->pitch()));
                  _index[track - firstTrack].append(event);
                  }
            }
      _firstTrack = firstTrack;
      _endTrack = endTrack;
      _indexState = state;
      _indexBuildMilliseconds = double(timer.nsecsElapsed()) / 1000000.0;
      ++_indexBuildCount;
      emit indexChanged();
      }

QVariantList ScoreObserver::soundingNotes(int firstTrack, int endTrack) const
      {
      QVariantList result;
      for (const auto& info : seq->activeNoteEvents()) {
            const auto owner = info.owner;
            if (!owner || !owner->chord() || info.noteEventIndex < 0 ||
                  info.noteEventIndex >= owner->playEvents().size()) continue;
            const Ms::Note* projected = nullptr;
            if (owner->score() == _score) projected = owner;
            else for (auto linked : owner->linkList())
                  if (linked->isNote() && linked->score() == _score) { projected = toNote(linked); break; }
            if (!projected || projected->track() < firstTrack || projected->track() >= endTrack) continue;
            const int offset = owner->playEvents()[info.noteEventIndex].pitch();
            result.append(describe(projected, owner->ppitch() + offset, offset != 0));
            }
      return result;
      }

QVariantMap ScoreObserver::snapshot(int tick, int firstTrack, int endTrack, bool sounding)
      {
      QVariantMap result {{"notes", QVariantList()}};
      if (!_score || tick < 0) return result;
      const int tracks = _score->nstaves() * VOICES;
      firstTrack = qBound(0, firstTrack, tracks);
      endTrack = qBound(firstTrack, endTrack, tracks);
      QVariantList notes;
      if (sounding && playing()) notes = soundingNotes(firstTrack, endTrack);
      else {
            ensureIndex(firstTrack, endTrack);
            for (const auto& voice : _index) {
                  auto next = std::upper_bound(voice.cbegin(), voice.cend(), tick,
                        [](int value, const Event& event) { return value < event.tick; });
                  if (next != voice.cbegin()) {
                        --next;
                        if (tick < next->end) notes.append(next->notes);
                        }
                  }
            }
      int bar = 0, beat = 0, offset = 0;
      _score->sigmap()->tickValues(tick, &bar, &beat, &offset);
      result.insert("notes", notes);
      result.insert("keySignature", firstTrack < tracks ? int(_score->staff(firstTrack / VOICES)->key(Fraction::fromTicks(tick))) : 0);
      result.insert("bar", bar + 1);
      result.insert("beat", beat + 1);
      result.insert("tick", tick);
      return result;
      }

void ScoreObserver::setNotePreviewColors(const QVariantList& notes)
      {
      if (!_score || !_enabled || !_surfaceVisible) { clearNotePreviewColors(); return; }
      NotePreviewColors colors;
      for (const auto& descriptor : notes) {
            const auto value = descriptor.toMap();
            const int track = value.value("track", -1).toInt();
            const int index = value.value("index", -1).toInt();
            const QColor color(value.value("color").toString());
            if (track < 0 || track >= _score->nstaves() * VOICES || index < 0 || !color.isValid()) continue;
            const auto segment = _score->tick2segment(Fraction::fromTicks(value.value("tick", -1).toInt()), false, SegmentType::ChordRest);
            if (!segment) continue;
            const auto element = segment->element(track);
            if (!element || !element->isChord()) continue;
            const auto& chordNotes = toChord(element)->notes();
            if (index >= int(chordNotes.size())) continue;
            const auto note = chordNotes[index];
            if (note->pitch() != value.value("writtenPitch", value.value("pitch")).toInt() ||
                  note->tpc() != value.value("writtenTpc", value.value("tpc")).toInt()) continue;
            colors.insert(note, {color, note->canvasBoundingRect().adjusted(-1, -1, 1, 1)});
            }
      for (auto viewer : _score->getViewer()) {
            auto view = dynamic_cast<Ms::ScoreView*>(viewer);
            if (!view) continue;
            view->setNotePreviewColors(this, colors);
            if (!_previewViews.contains(view)) _previewViews.append(view);
            }
      }

void ScoreObserver::clearNotePreviewColors()
      {
      // QPointer protects views closed before their plugin. Old dirty rectangles are cached.
      for (auto& view : _previewViews) if (view) view->setNotePreviewColors(this, {});
      _previewViews.clear();
      }
}
}
