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
#include "mscore/musescore.h"
#include "libmscore/chord.h"
#include "libmscore/harmony.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/sig.h"
#include "libmscore/staff.h"
#include "libmscore/tie.h"
#include "libmscore/part.h"
#include "libmscore/spanner.h"
#include "libmscore/spannermap.h"
#include "libmscore/system.h"
#include "libmscore/page.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFontMetricsF>
#include <QFontInfo>
#include <QPainter>
#include <memory>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QtMath>
#include <climits>
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

ScoreObserver::~ScoreObserver() { clearAllPreviews(); }

void ScoreObserver::watchSurface()
      {
      auto item = qobject_cast<QQuickItem*>(parent());
      if (!item) return;
      auto attach = [this](QQuickWindow* window) {
            if (!window) return;
            _surfaceVisible = window->isVisible();
            connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
                  _surfaceVisible = visible;
                  if (!visible) clearAllPreviews();
                  emit surfaceVisibleChanged();
                  });
            };
      connect(item, &QQuickItem::windowChanged, this, attach);
      attach(item->window());
      connect(item, &QQuickItem::visibleChanged, this, [this, item]() {
            if (!item->isVisible()) clearAllPreviews();
            });
      }

Score* ScoreObserver::wrappedScore() const
      { return _score ? wrap<Score>(_score.data(), Ownership::SCORE) : nullptr; }

void ScoreObserver::setScore(Score* wrapped)
      {
      Ms::Score* score = wrapped ? wrapped->score() : nullptr;
      if (_score == score) return;
      clearAllPreviews();
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
                  clearAllPreviews();
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
      if (!enabled) clearAllPreviews();
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
      auto attack=note;
      QSet<const Ms::Note*> visited;
      while (attack->tieBack() && attack->tieBack()->startNote() && !visited.contains(attack)) {
            visited.insert(attack);attack=attack->tieBack()->startNote();
            }
      result.insert("attackTick",attack->chord()->tick().ticks());
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
      _pedals.clear();
      _frameTicks.clear();
      _measureStarts.clear();
      QCryptographicHash hash(QCryptographicHash::Sha256);
      hash.addData(QByteArray::number(firstTrack) + ":scope:" + QByteArray::number(endTrack) + ";");
      _index.resize(endTrack - firstTrack);
      for (auto segment = _score->firstSegment(SegmentType::ChordRest); segment;
            segment = segment->next1(SegmentType::ChordRest)) {
            for (int track = firstTrack; track < endTrack; ++track) {
                  const auto element = segment->element(track);
                  if (!element || (!element->isChord() && !element->isRest())) continue;
                  const auto cr = toChordRest(element);
                  Event event {segment->tick().ticks(), cr->endTick().ticks(), {}};
                  if (element->isChord())
                        for (const auto note : toChord(element)->notes()) {
                              auto descriptor = describe(note, note->pitch());
                              auto tail = note;
                              QSet<const Ms::Note*> visited;
                              while (tail->tieFor() && tail->tieFor()->endNote() && !visited.contains(tail)) {
                                    visited.insert(tail);
                                    tail = tail->tieFor()->endNote();
                                    }
                              descriptor.insert("end", tail->chord()->endTick().ticks());
                              event.notes.append(descriptor);
                              hash.addData(QByteArray::number(event.tick) + ":" + QByteArray::number(track) + ":"
                                    + QByteArray::number(note->pitch()) + ":" + QByteArray::number(note->tpc()) + ":"
                                    + QByteArray::number(tail->chord()->endTick().ticks()) + ";");
                              }
                  hash.addData(QByteArray::number(event.tick) + ":voice:" + QByteArray::number(track) + ":"
                        + QByteArray::number(event.end) + ":" + QByteArray::number(int(element->type())) + ":"
                        + QByteArray::number(int(_score->staff(track/VOICES)->key(segment->tick()))) + ";");
                  _frameTicks.append(event.tick);
                  _frameTicks.append(event.end);
                  _index[track - firstTrack].append(event);
                  }
            }
      for (const auto& entry : _score->spannerMap().map()) {
            const auto spanner = entry.second;
            if (!spanner->isPedal()) continue;
            // A piano pedal normally spans the whole part, including both staves.
            auto part = _score->staff(spanner->staffIdx())->part();
            PedalWindow window {spanner->tick().ticks(), spanner->tick2().ticks(), part->startTrack(), part->endTrack()};
            _pedals.append(window);
            _frameTicks.append(window.start); _frameTicks.append(window.end);
            hash.addData(QByteArray::number(window.start) + ":pedal:" + QByteArray::number(window.end) + ";");
            }
      std::sort(_pedals.begin(),_pedals.end(),[](const auto& a,const auto& b) {
            return a.firstTrack<b.firstTrack || (a.firstTrack==b.firstTrack && a.start<b.start);
            });
      QVector<PedalWindow> continuous;
      for (const auto& window:_pedals) {
            if (!continuous.isEmpty() && continuous.back().firstTrack==window.firstTrack && window.start<continuous.back().end)
                  continuous.back().end=qMax(continuous.back().end,window.end);
            else continuous.append(window);
            }
      _pedals=continuous;
      for (auto measure = _score->firstMeasure(); measure; measure = measure->nextMeasure()) {
            _frameTicks.append(measure->tick().ticks());
            _measureStarts.append(measure->tick().ticks());
            }
      std::sort(_frameTicks.begin(), _frameTicks.end());
      _frameTicks.erase(std::unique(_frameTicks.begin(), _frameTicks.end()), _frameTicks.end());
      _fingerprint = QString::fromLatin1(hash.result().toHex());
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

const ScoreObserver::PedalWindow* ScoreObserver::pedalWindow(int tick,int track) const
      {
      const int part=_score->staff(track/VOICES)->part()->startTrack();
      const auto after=std::upper_bound(_pedals.cbegin(),_pedals.cend(),std::make_pair(part,tick),
            [](const auto& key,const auto& window) {
                  return key.first<window.firstTrack || (key.first==window.firstTrack && key.second<window.start);
                  });
      if (after==_pedals.cbegin())return nullptr;
      const auto& window=*(after-1);
      return window.firstTrack==part && tick<window.end ? &window : nullptr;
      }

QVariantList ScoreObserver::contextNotes(int tick, int firstTrack, int endTrack, bool pedal, int windowTicks)
      {
      ensureIndex(firstTrack, endTrack);
      const auto measure = std::upper_bound(_measureStarts.cbegin(), _measureStarts.cend(), tick);
      const int barStart = measure == _measureStarts.cbegin() ? 0 : *(measure-1);
      QMap<int, QVariant> notes; // Retain the most recent spelling/anchor for each MIDI pitch.
      for (int track = firstTrack; track < endTrack; ++track) {
            const auto& voice = _index[track - firstTrack];
            int start = qMax(barStart, tick - qBound(0, windowTicks, 1920));
            bool heldByPedal = false;
            if (pedal) if (const auto window=pedalWindow(tick,track)) {
                  // Include a note already held when the pedal goes down.
                  start=qMin(start,window->start);heldByPedal=true;
                  }
            auto next = std::upper_bound(voice.cbegin(), voice.cend(), tick,
                  [](int value, const Event& event) { return value < event.tick; });
            while (next != voice.cbegin()) {
                  --next;
                  // Explicit rests terminate a non-pedal arpeggio window in this voice.
                  if (!heldByPedal && next->notes.isEmpty() && tick >= next->tick) break;
                  bool eligible = next->tick >= start || next->end > tick || (heldByPedal && next->end > start);
                  for (const auto& note : next->notes) {
                        const auto descriptor = note.toMap();
                        if (!eligible && descriptor.value("end").toInt() <= tick) continue;
                        const int pitch = descriptor.value("pitch").toInt();
                        if (!notes.contains(pitch)) notes.insert(pitch, descriptor);
                        }
                  if (next->tick < start && next->end <= start) break;
                  }
            }
      return notes.values();
      }

QVariantMap ScoreObserver::contextSnapshot(int tick, int firstTrack, int endTrack, bool pedal, int windowTicks, bool sounding)
      {
      auto result = snapshot(tick, firstTrack, endTrack, sounding);
      if (!_score || tick < 0) return result;
      firstTrack = qBound(0, firstTrack, _score->nstaves() * VOICES);
      endTrack = qBound(firstTrack, endTrack, _score->nstaves() * VOICES);
      auto context = contextNotes(tick, firstTrack, endTrack, pedal, windowTicks);
      // Actual playback events take precedence for instruments/transposed playback events.
      if (sounding && playing()) {
            QMap<int, QVariant> merged;
            for (const auto& note : context) merged.insert(note.toMap().value("pitch").toInt(), note);
            for (const auto& note : result.value("notes").toList()) merged.insert(note.toMap().value("pitch").toInt(), note);
            context = merged.values();
            }
      result.insert("analysisNotes", context);
      QVariantList parts,windows;
      for (const auto part:_score->parts()) if (part->startTrack()<endTrack && part->endTrack()>firstTrack)
            parts.append(QVariantMap{{"startTrack",part->startTrack()},{"endTrack",part->endTrack()}});
      for (const auto part:_score->parts()) if (part->startTrack()<endTrack && part->endTrack()>firstTrack)
            if (const auto window=pedalWindow(tick,part->startTrack()))
                  windows.append(QVariantMap{{"start",window->start},{"end",window->end},{"firstTrack",window->firstTrack},{"endTrack",window->endTrack}});
      result.insert("parts",parts);result.insert("pedalWindows",windows);
      result.insert("scoreEnd",_score->lastMeasure() ? _score->lastMeasure()->endTick().ticks() : 0);
      result.insert("fingerprint", _fingerprint);
      return result;
      }

QVariantMap ScoreObserver::analysisFrames(int fromTick, int limit, int firstTrack, int endTrack, bool pedal, int windowTicks)
      {
      QVariantMap result {{"frames", QVariantList()}, {"nextTick", -1}};
      if (!_score) return result;
      firstTrack = qBound(0, firstTrack, _score->nstaves() * VOICES);
      endTrack = qBound(firstTrack, endTrack, _score->nstaves() * VOICES);
      ensureIndex(firstTrack, endTrack);
      auto it = std::lower_bound(_frameTicks.cbegin(), _frameTicks.cend(), fromTick);
      QVariantList frames;
      for (int count = 0; it != _frameTicks.cend() && count < qBound(1, limit, 128); ++count, ++it)
            frames.append(contextSnapshot(*it, firstTrack, endTrack, pedal, windowTicks));
      result.insert("frames", frames);
      result.insert("nextTick", it == _frameTicks.cend() ? -1 : *it);
      result.insert("fingerprint", _fingerprint);
      return result;
      }

Ms::Note* ScoreObserver::resolve(const QVariantMap& value, const QHash<int, Ms::Segment*>* segments) const
      {
      const int track = value.value("track", -1).toInt(), index = value.value("index", -1).toInt();
      if (!_score || track < 0 || track >= _score->nstaves()*VOICES || index < 0) return nullptr;
      const int tick=value.value("tick",-1).toInt();
      auto segment = segments ? segments->value(tick,nullptr) :
            _score->tick2segment(Fraction::fromTicks(tick), false, SegmentType::ChordRest);
      if (!segment) return nullptr;
      auto element = segment->element(track);
      if (!element || !element->isChord()) return nullptr;
      const auto& notes = toChord(element)->notes();
      if (index >= int(notes.size())) return nullptr;
      auto note = notes[index];
      if (note->pitch() != value.value("writtenPitch", value.value("pitch")).toInt() ||
            note->tpc() != value.value("writtenTpc", value.value("tpc")).toInt()) return nullptr;
      return note;
      }

// A tick without a written onset still has a stable horizontal location in its measure.
// Prefer actual segment positions; interpolate only between neighboring rhythmic positions.
static qreal previewTickX(const Ms::Measure* measure, int tick, const Ms::Segment** exact)
      {
      qreal left=0,right=measure->width();
      int leftTick=measure->tick().ticks(),rightTick=measure->endTick().ticks();
      *exact=nullptr;
      for (auto segment=measure->first(SegmentType::ChordRest);segment;segment=segment->next(SegmentType::ChordRest)) {
            if (!segment->enabled()) continue;
            const int position=segment->tick().ticks();
            if (position==tick) {*exact=segment;return segment->pagePos().x();}
            if (position>tick) {right=segment->x();rightTick=position;break;}
            left=segment->x();leftTick=position;
            }
      const qreal amount=rightTick>leftTick ? qBound(qreal(0),qreal(tick-leftTick)/(rightTick-leftTick),qreal(1)) : 0;
      return measure->pagePos().x()+left+(right-left)*amount;
      }

void ScoreObserver::applyPreview(QObject* owner, const QVariantList& descriptors)
      {
      if (!_score || !_enabled || !_surfaceVisible) { clearPreview(owner); return; }
      NotePreviewColors colors;
      NotePreviewMarkers markers;
      QVector<QPointF> positions;
      QHash<int,Ms::Segment*> segments;
      QMap<int,Ms::Measure*> measures;
      if (descriptors.size()>64) {
            for (auto segment=_score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))
                  segments.insert(segment->tick().ticks(),segment);
            for (auto measure=_score->firstMeasure();measure;measure=measure->nextMeasure())
                  measures.insert(measure->tick().ticks(),measure);
            }
      QHash<const Page*,QVector<QRectF>> occupied,chordBoxes;
      QHash<const System*,QMap<int,QVector<int>>> groups;
      // Private style/chord list: parsing unknown symbols must not modify the user's score.
      std::unique_ptr<Ms::MasterScore> scratch;
      QHash<QString,NotePreviewEntry> rendered;
      int hidden=0;QString fontFallback;
      for (const auto& descriptor : descriptors) {
            const auto value=descriptor.toMap();
            auto note=resolve(value,descriptors.size()>64 ? &segments : nullptr);
            if (!note || !note->visible()) continue;
            const qreal sp=note->spatium();
            auto system=note->chord()->measure()->system();
            auto page=system ? system->page() : nullptr;
            if (!page) continue;
            NotePreviewEntry entry {QColor(value.value("color").toString()),note->canvasBoundingRect().adjusted(-sp,-sp,sp,sp)};
            entry.label=value.value("label").toString().left(24);
            entry.active=value.value("active").toBool();
            entry.anchor=note->canvasPos();entry.spatium=sp;
            const QPointF anchor=note->pagePos();
            auto collides=[&](const QRectF& candidate) {
                  if (!page->bbox().contains(candidate)) return true;
                  if (owner==this) for (const auto& box : _baseChordBoxes.value(page))
                        if (box.adjusted(-sp*.12,-sp*.12,sp*.12,sp*.12).intersects(candidate)) return true;
                  for (const auto& box : occupied[page])
                        if (box.adjusted(-sp*.12,-sp*.12,sp*.12,sp*.12).intersects(candidate)) return true;
                  for (const auto element : page->items(candidate)) {
                        if (!element->visible() || element->isStaffLines() || element->isPage() || element->isSystem() || element->isMeasure()) continue;
                        if (element->pageBoundingRect().adjusted(-sp*.12,-sp*.12,sp*.12,sp*.12).intersects(candidate)) return true;
                        }
                  return false;
                  };
            const auto base=_baseColors.constFind(note);
            if (!entry.label.isEmpty()) {
                  if (owner==this && base!=_baseColors.constEnd() && base->anchor==entry.anchor && base->spatium==sp && base->label==entry.label)
                        entry.labelBox=base->labelBox;
                  else {
                        QFontMetricsF metrics(notePreviewFont(sp,false));
                        const QSizeF size(metrics.horizontalAdvance(entry.label)+sp*.5,metrics.height()+sp*.2);
                        for (int lane=0;lane<7;++lane) {
                              const qreal y=anchor.y()-size.height()/2+(lane==0 ? 0 : ((lane+1)/2)*(lane%2 ? -1 : 1)*sp*1.2);
                              const QRectF candidate(anchor.x()+note->bbox().right()+sp*.45,y,size.width(),size.height());
                              if (collides(candidate)) continue;
                              occupied[page].append(candidate);entry.labelBox=candidate.translated(-anchor);break;
                              }
                        }
                  }
            entry.bounds |= entry.labelBox.translated(entry.anchor);
            if (entry.color.isValid() || !entry.label.isEmpty() || entry.active) colors.insert(note,entry);
            entry.chord=value.value("chord").toString().left(64);entry.degree=value.value("degree").toString().left(24);
            if (entry.chord.isEmpty() && entry.degree.isEmpty()) continue;
            entry.chordTick=qMax(0,value.value("chordTick",note->chord()->tick().ticks()).toInt());
            entry.chordUntil=qMax(entry.chordTick,value.value("chordUntil",INT_MAX).toInt());
            Ms::Measure* measure=nullptr;
            if (descriptors.size()>64) {
                  auto it=measures.upperBound(entry.chordTick);
                  if (it!=measures.begin()) measure=*--it;
                  }
            else measure=_score->tick2measure(Fraction::fromTicks(entry.chordTick));
            if (!measure || entry.chordTick>=measure->endTick().ticks() || !measure->system()) continue;
            system=measure->system();page=system->page();if (!page) continue;
            const Ms::Segment* exact=nullptr;
            const qreal x=previewTickX(measure,entry.chordTick,&exact);
            if (exact && value.value("preferExistingHarmony").toBool())
                  for (const auto annotation : exact->annotations()) {
                        if (!annotation->visible() || !annotation->isHarmony() || !annotation->staff()
                              || annotation->staff()->part()!=note->staff()->part()) continue;
                        const auto harmony=toHarmony(annotation);
                        if (harmony->harmonyName().trimmed().isEmpty()) continue;
                        if (harmony->harmonyType()==HarmonyType::ROMAN) entry.degree.clear();else entry.chord.clear();
                        }
            if (entry.chord.isEmpty() && entry.degree.isEmpty()) continue;
            entry.annotationTrack=note->staff()->part()->startTrack();entry.activationTarget=this;
            entry.sourceAnchor=note;entry.chordMask=value.value("chordMask",true).toBool();
            entry.label.clear();entry.labelBox={};entry.color={};entry.active=false;entry.bounds={};
            // This anchor belongs to the change's system, even if its sustained source note is on a prior line.
            const QPointF position(x,system->staffYpage(entry.annotationTrack/VOICES));
            entry.anchor=position+page->pos();
            QString family=value.value("chordFont").toString().left(64);
            if (family.isEmpty()) family=_score->styleSt(Sid::chordSymbolAFontFace);
            qreal scale=value.value("chordScale",1.0).toDouble();
            if (!qIsFinite(scale) || scale<.6 || scale>2.) scale=1.;
            entry.chordFont=QFont(family);entry.chordFont.setPointSizeF(_score->styleD(Sid::chordSymbolAFontSize)*scale);
            QString degreeFamily=value.value("degreeFont","Arial").toString().left(64);
            entry.degreeFont=QFont(degreeFamily);entry.degreeFont.setPixelSize(qMax(5,qRound(sp*1.45*scale)));
            const QFontInfo resolved(entry.chordFont);
            if (resolved.family().compare(family,Qt::CaseInsensitive)!=0) fontFallback=resolved.family();
            auto readColor=[&](const char* key,const QColor& fallback) {
                  const QColor color(value.value(key).toString());return color.isValid() ? color : fallback;
                  };
            entry.chordColor=readColor("chordColor",entry.chordColor);
            entry.highlightColor=readColor("highlightColor",entry.highlightColor);
            entry.highlightBackground=readColor("highlightBackground",entry.highlightBackground);
            if (!entry.chord.isEmpty()) {
                  const QString renderKey=entry.chord+"\t"+family+"\t"+QString::number(scale,'g',12)+"\t"
                        +entry.chordColor.name()+entry.highlightColor.name();
                  if (!rendered.contains(renderKey)) {
                        if (!scratch) {
                              scratch=std::make_unique<Ms::MasterScore>(_score->style());
                              // Scores without written Harmony may not yet have loaded their symbol list.
                              scratch->style().checkChordList();
                              }
                        Harmony harmony(scratch.get());
                        if (!value.value("chordFont").toString().isEmpty()) harmony.setProperty(Pid::FONT_FACE,family);
                        harmony.setProperty(Pid::FONT_SIZE,_score->styleD(Sid::chordSymbolAFontSize)*scale);
                        harmony.setHarmony(entry.chord);harmony.calculateBoundingRect();
                        const auto nativeBox=harmony.bbox();
                        NotePreviewEntry glyphs;glyphs.renderedChordSize=nativeBox.size();glyphs.renderedChordKey=renderKey;
                        auto record=[&](const QColor& color,QPicture& picture) {
                              harmony.setColor(color);QPainter painter(&picture);painter.translate(-nativeBox.topLeft());
                              static_cast<const Element&>(harmony).draw(&painter);painter.end();
                              };
                        record(entry.chordColor,glyphs.chordPicture);record(entry.highlightColor,glyphs.activeChordPicture);
                        // Include rendered bytes: score style/font/chord-list edits must invalidate equal-sized glyphs.
                        glyphs.renderedChordKey += QCryptographicHash::hash(
                              QByteArray(glyphs.chordPicture.data(),glyphs.chordPicture.size()),QCryptographicHash::Sha256).toHex();
                        rendered.insert(renderKey,glyphs);
                        }
                  const auto& glyphs=rendered[renderKey];entry.chordPicture=glyphs.chordPicture;
                  entry.activeChordPicture=glyphs.activeChordPicture;entry.renderedChordSize=glyphs.renderedChordSize;
                  entry.renderedChordKey=glyphs.renderedChordKey;
                  }
            entry.chordBox=QRectF(QPointF(),layoutPreviewChord(entry,qBound(0,value.value("chordOrder").toInt(),3)));
            groups[system][entry.annotationTrack].append(markers.size());markers.append(entry);positions.append(position);
            }
      // Prefer one row per system/part; lift colliding markers while keeping their tick x.
      for (auto systemIt=groups.cbegin();systemIt!=groups.cend();++systemIt) {
            const auto system=systemIt.key();const auto page=system->page();
            for (auto groupIt=systemIt->cbegin();groupIt!=systemIt->cend();++groupIt) {
                  auto group=groupIt.value();
                  std::sort(group.begin(),group.end(),[&](int a,int b){return markers[a].chordTick<markers[b].chordTick;});
                  const qreal sp=markers[group.front()].spatium;
                  qreal height=0;for (int index:group) height=qMax(height,markers[index].chordBox.height());
                  const qreal top=system->staffYpage(groupIt.key()/VOICES)-sp*1.5-height;
                  for (int index:group) {
                        auto& entry=markers[index];QRectF placed;
                        for (int lane=0;lane<6;++lane) {
                              const QRectF candidate(positions[index].x(),top-lane*(height+sp*.5),entry.chordBox.width(),height);
                              bool collision=!page->bbox().contains(candidate);
                              for (const auto& box:occupied[page])
                                    if (box.adjusted(-sp*.15,-sp*.12,sp*.15,sp*.12).intersects(candidate)){collision=true;break;}
                              if (!collision) for (const auto element:page->items(candidate)) {
                                    if (!element->visible() || element->isStaffLines() || element->isPage() || element->isSystem() || element->isMeasure()) continue;
                                    if (element->pageBoundingRect().adjusted(-sp*.12,-sp*.12,sp*.12,sp*.12).intersects(candidate)){collision=true;break;}
                                    }
                              if (!collision){placed=candidate;break;}
                              }
                        entry.chordBox=placed.isEmpty() ? QRectF() : placed.translated(-positions[index]);
                        if (placed.isEmpty()) ++hidden;
                        else {occupied[page].append(placed);chordBoxes[page].append(placed);}
                        entry.bounds=entry.chordBox.isEmpty() ? QRectF() : entry.chordBox.translated(entry.anchor);
                        }

                  }
            }
      if (owner==&_baseOwner){_baseColors=colors;_baseChordBoxes=chordBoxes;
            setProperty("previewStatus",QVariantMap{{"hidden",hidden},{"fontFallback",fontFallback}});}
      for (auto viewer:_score->getViewer()) {
            auto view=dynamic_cast<Ms::ScoreView*>(viewer);if (!view) continue;
            view->setNotePreviewColors(owner,colors,markers);
            if (owner==&_baseOwner) view->setActiveNotePreview(owner,_activePreviewTick);
            if (!_previewViews.contains(view)) _previewViews.append(view);
            }
      }


void ScoreObserver::setActiveScorePreview(int tick)
      {
      _activePreviewTick=tick;
      for (auto& view : _previewViews) if (view) view->setActiveNotePreview(&_baseOwner,tick);
      }

void ScoreObserver::setNotePreviewColors(const QVariantList& notes) { applyPreview(this, notes); }
void ScoreObserver::setScorePreview(const QVariantList& notes) { applyPreview(&_baseOwner, notes); }
void ScoreObserver::clearPreview(QObject* owner)
      { for (auto& view : _previewViews) if (view) view->setNotePreviewColors(owner, {}); }
void ScoreObserver::clearNotePreviewColors() { clearPreview(this); }
void ScoreObserver::clearAllPreviews()
      { clearPreview(this); clearPreview(&_baseOwner); _baseColors.clear(); _baseChordBoxes.clear(); _previewViews.clear(); _activePreviewTick=-1; }

static QString localPath(const QString& path)
      { const QUrl url(path); return url.isLocalFile() ? url.toLocalFile() : path; }
QString ScoreObserver::readTextFile(const QString& path) const
      {
      QFile file(localPath(path));
      if (!file.open(QIODevice::ReadOnly) || file.size() > 16*1024*1024) return QString();
      return QString::fromUtf8(file.readAll());
      }
bool ScoreObserver::writeTextFile(const QString& path, const QString& text) const
      {
      QSaveFile file(localPath(path));
      if (!file.open(QIODevice::WriteOnly)) return false;
      const auto bytes = text.toUtf8();
      return file.write(bytes) == bytes.size() && file.commit();
      }
static QString configurationPath(const QString& name)
      {
      if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains("..")) return QString();
      const auto directory = (dataPath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : dataPath) + "/plugin-settings";
      if (!QDir().mkpath(directory)) return QString();
      return directory + "/" + name + ".json";
      }
QVariantMap ScoreObserver::loadConfiguration(const QString& name) const
      { return QJsonDocument::fromJson(readTextFile(configurationPath(name)).toUtf8()).toVariant().toMap(); }
bool ScoreObserver::saveConfiguration(const QString& name, const QVariantMap& data) const
      { return writeTextFile(configurationPath(name), QString::fromUtf8(QJsonDocument::fromVariant(data).toJson())); }

}
}
