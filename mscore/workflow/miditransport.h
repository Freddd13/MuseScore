// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "audio/midi/event.h"
#include <QVariantMap>
#include <QVector>
#include <memory>

namespace Ms
{
class MasterSynthesizer;
struct MidiTrackSound {
    QString id;
    int synthesizer = 0, bank = 0, program = 0, volume = 100, pan = 64, reverb = 0;
};
struct MidiTrackPlaybackEvent {
    qint64 frame = 0;
    int synthesizer = 0;
    NPlayEvent event;
};
struct MidiTrackPlaybackPlan {
    struct Checkpoint {
        int cursor = 0;
        QVector<MidiTrackPlaybackEvent> state;
    };
    QVector<MidiTrackPlaybackEvent> events, initial;
    QVector<Checkpoint> checkpoints;
    QVector<QPair<int, int>> channels; // internal channel, synthesizer index
    QString error;
    int sampleRate = 44100;
    qint64 startFrame = 0, endFrame = 0;
    bool followScore = false;
    bool monitor = false;
};
MidiTrackPlaybackPlan buildMidiTrackPlayback(const QVariantList& tracks,
                                             const QVector<MidiTrackSound>& sounds,
                                             int firstFreeChannel, int sampleRate,
                                             double startSeconds, bool followScore);

// GUI publication; the sound thread switches and releases old keys at the next
// processing boundary. Retired plans are reclaimed by the GUI, never by audio.
quint64 startMidiTrackPlayback(MidiTrackPlaybackPlan);
quint64 startMidiTrackMonitor(const MidiTrackSound&, int channel, int sampleRate);
bool sendMidiTrackMonitor(quint64, const NPlayEvent&);
void stopMidiTrackPlayback();
QVariantMap midiTrackPlaybackState();
void collectMidiTrackPlayback();
void processMidiTrackPlayback(MasterSynthesizer*, unsigned frames, float* output, qint64 scoreFrame,
                              bool scorePlaying);
} // namespace Ms
