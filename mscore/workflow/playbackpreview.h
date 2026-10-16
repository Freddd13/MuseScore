// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include "audio/midi/event.h"
#include <QPair>
#include <QVariantMap>
#include <QVector>
namespace Ms {
class Score;
class MasterScore;
struct PlaybackPreviewMaterial {
      EventMap events;
      QVector<QPair<double,double>> clock;
      QVariantMap reference;
};
PlaybackPreviewMaterial renderPlaybackPreview(Score* original, Score* rendered);
EventMap resumePlaybackPreview(const EventMap&,int tick);
void alignPreviewReference(PlaybackPreviewMaterial&);
QVariantMap playbackPreviewReferencePlan(Score*,bool proposed,int resumeTick);
bool playbackPreviewPrepared(Score*, const QString& key);
bool playbackPreviewActive(Score*,bool proposed);
void preparePlaybackPreview(Score*, const QString& key, PlaybackPreviewMaterial original, PlaybackPreviewMaterial proposed);
QVariantMap startPlaybackPreview(Score*, bool proposed, const QVariantMap& range);
void stopPlaybackPreview(bool finish = false);
const EventMap* activePlaybackPreviewEvents(MasterScore*);
}
