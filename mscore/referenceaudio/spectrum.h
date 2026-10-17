// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QImage>
#include <QVariantMap>
#include <atomic>

namespace KumoAudio {
// Values only. Independent readers keep analysis away from the playback callback.
struct SpectrumRequest {
    QString path;
    double fromMs = 0, untilMs = 8000;
    // fftSize is the Hann window length; the low preset uses a 2x padded transform.
    int columns = 256, rows = 352, fftSize = 16384;
    double lowPitch = 20.5, highPitch = 108.5, tuning = 440;
};
struct SpectrumTile {
    QImage image;
    double fromMs = 0, untilMs = 0, durationMs = 0;
    double lowPitch = 0, highPitch = 0;
    int sampleRate = 0;
    QString error;
};
SpectrumTile spectrum(const SpectrumRequest &, const std::atomic_bool &cancel);
QString spectrumKey(const SpectrumRequest &);
} // namespace KumoAudio
