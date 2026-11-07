// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "miditransport.h"
class QObject;
namespace Ms
{
class Score;
struct MidiTrackSounds {
    QVector<MidiTrackSound> sounds;
    QString error;
};
MidiTrackSounds prepareMidiTrackSounds(MasterSynthesizer*, Score*, const QVariantList&,
                                       QObject* owner = nullptr);
void clearMidiTrackSounds(MasterSynthesizer*, QObject* owner = nullptr);
} // namespace Ms
