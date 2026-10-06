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

// Generic GUI-thread score inspection for QML tools; musical interpretation stays in plugins.
#ifndef MS_PLUGIN_SCOREOBSERVER_H
#define MS_PLUGIN_SCOREOBSERVER_H

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QVector>
#include "libmscore/score.h"
#include "mscore/notepreview.h"

namespace Ms {
class ScoreView;
class Page;
namespace PluginAPI {
class Score;

class ScoreObserver : public QObject {
      Q_OBJECT
      Q_PROPERTY(Ms::PluginAPI::Score* score READ wrappedScore WRITE setScore NOTIFY scoreChanged)
      Q_PROPERTY(int tick READ tick NOTIFY positionChanged)
      Q_PROPERTY(bool playing READ playing NOTIFY positionChanged)
      Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
      Q_PROPERTY(bool surfaceVisible READ surfaceVisible NOTIFY surfaceVisibleChanged)
      Q_PROPERTY(int indexBuildCount READ indexBuildCount NOTIFY indexChanged)
      Q_PROPERTY(double indexBuildMilliseconds READ indexBuildMilliseconds NOTIFY indexChanged)

      struct Event { int tick; int end; QVariantList notes; };
      struct PedalWindow { int start; int end; int firstTrack; int endTrack; };
      QVector<PedalWindow> _pedals;
      QVector<int> _frameTicks;
      QString _fingerprint;
      QVector<int> _measureStarts;
      NotePreviewColors _baseColors;
      // Geometry only; page keys are opaque and cleared with the base layer.
      QHash<const Ms::Page*, QVector<QRectF>> _baseChordBoxes;
      QObject _baseOwner;
      int _activePreviewTick = -1;
      void applyPreview(QObject* owner, const QVariantList& notes);
      Ms::Note* resolve(const QVariantMap& value, const QHash<int, Ms::Segment*>* segments = nullptr) const;
      void clearPreview(QObject* owner);
      const PedalWindow* pedalWindow(int tick,int track) const;
      QVariantList contextNotes(int tick, int firstTrack, int endTrack, bool pedal, int windowTicks);

      QPointer<Ms::Score> _score;
      QVector<QMetaObject::Connection> _scoreConnections;
      QVector<QPointer<Ms::ScoreView>> _previewViews;
      QVector<QVector<Event>> _index;
      ScoreContentState _indexState;
      int _firstTrack = -1;
      int _endTrack = -1;
      int _tick = -1;
      int _indexBuildCount = 0;
      double _indexBuildMilliseconds = 0.0;
      bool _enabled = true;
      bool _surfaceVisible = true;
      void ensureIndex(int firstTrack, int endTrack);
      QVariantMap describe(const Ms::Note* note, int pitch, bool eventPitch = false) const;
      QVariantList soundingNotes(int firstTrack, int endTrack) const;
      void notifyPosition();
      void watchSurface();
   public:
      explicit ScoreObserver(QObject* parent = nullptr);
      ~ScoreObserver() override;
      Score* wrappedScore() const;
      void setScore(Score* score);
      int tick() const;
      bool playing() const;
      bool enabled() const { return _enabled; }
      bool surfaceVisible() const { return _surfaceVisible; }
      void setEnabled(bool enabled);
      int indexBuildCount() const { return _indexBuildCount; }
      double indexBuildMilliseconds() const { return _indexBuildMilliseconds; }
      Q_INVOKABLE QVariantMap snapshot(int tick, int firstTrack, int endTrack, bool sounding = false);
      Q_INVOKABLE void setNotePreviewColors(const QVariantList& notes);
      Q_INVOKABLE void clearNotePreviewColors();
      Q_INVOKABLE void clearAllPreviews();
      Q_INVOKABLE void setScorePreview(const QVariantList& notes);
      Q_INVOKABLE void setActiveScorePreview(int tick);
      Q_INVOKABLE QVariantMap previewStatus() const { return property("previewStatus").toMap(); }
      Q_INVOKABLE void activatePreview(int tick, int track) { emit previewActivated(tick, track); }
      Q_INVOKABLE QVariantMap contextSnapshot(int tick, int firstTrack, int endTrack, bool pedal, int windowTicks, bool sounding = false);
      Q_INVOKABLE QVariantMap analysisFrames(int fromTick, int limit, int firstTrack, int endTrack, bool pedal, int windowTicks);
      Q_INVOKABLE QVariantMap loadConfiguration(const QString& name) const;
      Q_INVOKABLE bool saveConfiguration(const QString& name, const QVariantMap& data) const;
      Q_INVOKABLE QString readTextFile(const QString& path) const;
      Q_INVOKABLE bool writeTextFile(const QString& path, const QString& text) const;
   signals:
      void scoreChanged();
      void positionChanged();
      void previewActivated(int tick, int track);
      void enabledChanged();
      void indexChanged();
      void surfaceVisibleChanged();
      };
}
}
#endif
