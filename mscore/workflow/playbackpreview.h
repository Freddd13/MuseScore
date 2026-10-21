// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include "audio/midi/event.h"
#include <QPair>
#include <QVariantMap>
#include <QVector>
#include <functional>
class QObject;
namespace Ms {
class Score;
class MasterScore;
struct PlaybackPreviewMaterial {
      EventMap events;
      QVector<QPair<double,double>> clock;
      QVariantMap reference;
};
PlaybackPreviewMaterial renderPlaybackPreview(Score* original, Score* rendered);
EventMap restoredPlaybackEvents(const EventMap&,int tick);
EventMap resumePlaybackPreview(const EventMap&,int tick);
void alignPreviewReference(PlaybackPreviewMaterial&);
QVariantMap playbackPreviewReferencePlan(Score*,bool proposed,int resumeTick);
bool playbackPreviewPrepared(Score*, const QString& key);
bool playbackPreviewActive(Score*,bool proposed);
void preparePlaybackPreview(Score*, const QString& key, PlaybackPreviewMaterial original, PlaybackPreviewMaterial proposed);
QVariantMap startPlaybackPreview(Score*, bool proposed, const QVariantMap& range);
void stopPlaybackPreview(bool finish = false);
void setPlaybackPreviewRepeat(QObject*,std::function<void()>);
std::function<void()> playbackPreviewRepeat();
void setPlaybackPreviewLoop(Score*,bool);
bool playbackPreviewLoop();
const EventMap* activePlaybackPreviewEvents(MasterScore*);
}
