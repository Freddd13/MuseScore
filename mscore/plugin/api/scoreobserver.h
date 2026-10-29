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
#include <memory>
#include <atomic>
#include "libmscore/score.h"
#include "mscore/notepreview.h"

namespace Ms {
class ScoreView;
class Page;
namespace PluginAPI {
class Score;
struct AnalysisPlaybackRead;

class ScoreObserver : public QObject {
      Q_OBJECT
      Q_PROPERTY(Ms::PluginAPI::Score* score READ wrappedScore WRITE setScore NOTIFY scoreChanged)
      Q_PROPERTY(int tick READ tick NOTIFY positionChanged)
      Q_PROPERTY(bool playing READ playing NOTIFY positionChanged)
      Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
      Q_PROPERTY(bool surfaceVisible READ surfaceVisible NOTIFY surfaceVisibleChanged)
      Q_PROPERTY(bool auxiliarySurfaceVisible READ auxiliarySurfaceVisible WRITE setAuxiliarySurfaceVisible NOTIFY surfaceVisibleChanged)
      Q_PROPERTY(int indexBuildCount READ indexBuildCount NOTIFY indexChanged)
      Q_PROPERTY(double indexBuildMilliseconds READ indexBuildMilliseconds NOTIFY indexChanged)

      int _indexUndoIndex=-1;
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

      std::shared_ptr<AnalysisPlaybackRead> _playbackRead;
      ScoreContentState _analysisState;
      quint64 _analysisRevision = 0;
      quint64 _analysisSession = 0;
      QObject* _readOnlyJob = nullptr;
      int _readOnlyJobToken = 0;
      int _midiToken = 0;
      std::shared_ptr<std::atomic_bool> _midiCancel;
      QPointer<Ms::Score> _workflowOriginal;
      bool _workflowApplyingPreview = false;
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
      QPointer<QObject> _chordEditor, _workflowRepeat;
      bool _surfaceVisible = true;
      bool _auxiliarySurfaceVisible = false;
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
      bool surfaceVisible() const { return _surfaceVisible || _auxiliarySurfaceVisible; }
      bool auxiliarySurfaceVisible() const { return _auxiliarySurfaceVisible; }
      void setAuxiliarySurfaceVisible(bool visible);
      void setEnabled(bool enabled);
      int indexBuildCount() const { return _indexBuildCount; }
      double indexBuildMilliseconds() const { return _indexBuildMilliseconds; }
      Q_INVOKABLE int startReadOnlyJob(const QString& source, const QVariantMap& input);
      Q_INVOKABLE void cancelReadOnlyJob();
      Q_INVOKABLE QString analysisRevision();
      Q_INVOKABLE void prepareLocalBridge();
      Q_INVOKABLE QVariantMap arrangementState(const QVariantMap& request = QVariantMap());
      Q_INVOKABLE QVariantMap arrangementRange(const QVariantMap& request);
      Q_INVOKABLE QVariantMap arrangementImage(const QVariantMap& request);
      Q_INVOKABLE QVariantMap arrangementRequest(const QString& action,const QVariantMap& request);
      Q_INVOKABLE void publishHarmonyAnalysis(const QVariantMap& result);
      Q_INVOKABLE void publishPianoSettings(const QVariantMap& settings);
      Q_INVOKABLE QVariantMap workflowStatus();
      Q_INVOKABLE QVariantMap stopWorkflowPlayback();
      Q_INVOKABLE QString referenceStructureKey() const;
      Q_INVOKABLE QVariantMap referenceFileLocation(const QString& original,const QString& relative=QString()) const;
      Q_INVOKABLE QVariantMap workflowData(const QString& section) const;
      Q_INVOKABLE QVariantMap setWorkflowData(const QString& section,const QVariantMap& value);
      Q_INVOKABLE void workflowUndo(bool redo=false);
      Q_INVOKABLE QVariantMap referencePlan(const QVariantMap&);
      Q_INVOKABLE QVariantMap startReferencePlayback(const QVariantMap&);
      Q_INVOKABLE QVariantMap stopReferencePlayback();
      Q_INVOKABLE QVariantMap setEditorComparison(const QVariantMap&);
      Q_INVOKABLE QVariantMap updateEditorComparisonState(const QVariantMap&);
      Q_INVOKABLE bool reuseHarmonyAfterVelocityEdit();
      Q_INVOKABLE QVariantMap setReferenceTracks(const QVariantList&);
      Q_INVOKABLE QVariantMap workflowFingerprint(int from,int limit,const QString& expected);
      Q_INVOKABLE QVariantMap returnWorkflowSource();
      Q_INVOKABLE QString temporaryWorkflowProject();
      Q_INVOKABLE QVariantMap showEditorComparison();
      Q_INVOKABLE QVariantMap showEditorCorrespondence();
      Q_INVOKABLE QString analysisScoreId() const;
      Q_INVOKABLE QString analysisContentRevision() const;
      Q_INVOKABLE QVariantMap scoreIdentity() const;
      static QString applicationInstanceId();
      static QVariantMap scoreIdentityFor(Ms::Score*);
      Q_INVOKABLE QVariantMap scoreLocation(int tick,int track) const;
      Q_INVOKABLE QVariantMap previewExpression(const QString& workspace,const QVariantMap& candidate,const QVariantMap& scope);
      Q_INVOKABLE QVariantMap openWorkflowScore(const QString& filename,bool baseline = false,const QString& expectedHash = QString());
      Q_INVOKABLE QVariantMap copyWorkflowScore(const QString& destination,const QString& expected,bool overwrite = false);
      Q_INVOKABLE QVariantMap saveWorkflowScore(const QString& expected);
      Q_INVOKABLE QVariantMap applyExpression(const QVariantMap& candidate);
      Q_INVOKABLE QVariantMap expressionVelocityHistory(const QVariantMap& range);
      Q_INVOKABLE QVariantMap expressionSelection(bool exactElements = false);
      Q_INVOKABLE QVariantMap selectExpressionNote(const QVariantMap& note);
      Q_INVOKABLE QVariantMap auditionExpression(const QString& workspace,const QVariantMap& candidate,const QVariantMap& scope,bool proposed);
      Q_INVOKABLE QVariantMap finishExpressionAudition();
      Q_INVOKABLE QVariantMap closeExpressionPreviewTabs();
      Q_INVOKABLE QVariantMap restoreExpressionVelocity(const QVariantMap& range);
      Q_INVOKABLE QVariantMap saveCurrentExpressionVelocity(const QVariantMap& range);
      Q_INVOKABLE int readPerformanceMidi(const QString& path);
      Q_INVOKABLE void cancelPerformanceMidi();
      Q_INVOKABLE QVariantMap savePerformanceSource(const QString& source,const QString& destination,const QString& expected);
      Q_INVOKABLE QVariantMap workflowInfo();
      Q_INVOKABLE QVariantMap performanceRange(int start,int end,int firstTrack,int endTrack,int fromTick,int limit,const QString& expected);
      Q_INVOKABLE QVariantMap navigateWorkflow(const QVariantMap& range,bool audition = false);
      Q_INVOKABLE QVariantMap analysisScope(bool whole = false) const;
      Q_INVOKABLE QVariantMap analysisRange(int start, int end, int firstTrack, int endTrack, int fromTick, int limit, const QString& expected = QString());
      Q_INVOKABLE QVariantMap analysisHarmonyContext(int before, int firstTrack, int endTrack, int fromTick, int limit, const QString& expected = QString());
      Q_INVOKABLE QString analysisPlaybackRevision();
      Q_INVOKABLE QVariantMap analysisPlaybackPrepare(int start,int end,const QString& expected);
      Q_INVOKABLE QVariantMap analysisPlaybackPage(int fromTick,int firstTrack,int endTrack,int limit,const QString& expected);
      Q_INVOKABLE QVariantMap snapshot(int tick, int firstTrack, int endTrack, bool sounding = false);
      Q_INVOKABLE void setNotePreviewColors(const QVariantList& notes);
      Q_INVOKABLE void clearNotePreviewColors();
      Q_INVOKABLE void clearAllPreviews();
      Q_INVOKABLE void setScorePreview(const QVariantList& notes);
      Q_INVOKABLE void setChordEditRegion(const QVariantMap& region);
      Q_INVOKABLE QVariantMap musicalPosition(int tick) const;
      Q_INVOKABLE void setExpressionAuditionLoop(bool enabled);
      Q_INVOKABLE QVariantMap harmonyLabel(const QString& chord, const QString& degree, const QVariantMap& style) const;
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
      void performanceMidiReady(int token,const QVariantMap& result);
      void readOnlyJobFinished(int token, const QVariantMap& result);
      void scoreChanged();
      void activeScoreChanged();
      void workflowFileChanged();
      void positionChanged();
      void workflowPlaybackError(const QString& message);
      void previewActivated(int tick, int track);
      void previewRangeEdited(int start,int end,int track);
      void previewPositionEdited(int tick,int track,qreal x,qreal y);
      void enabledChanged();
      void indexChanged();
      void surfaceVisibleChanged();
      };
}
}
#endif
