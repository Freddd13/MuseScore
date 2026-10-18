// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "auditionfilter.h"
#include <cmath>
namespace KumoAudio {
// RBJ biquad equations: https://www.w3.org/TR/audio-eq-cookbook/
void AuditionFilter::add(int type, double frequency, double gain, double width, int rate) {
    frequency = qBound(10., frequency, rate * .45);
    gain = qBound(-18., gain, 18.);
    const double w = 2 * 3.14159265358979323846 * frequency / rate, c = std::cos(w), s = std::sin(w),
                 A = std::pow(10., gain / 40.);
    double alpha = s / std::sqrt(2.), b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    if (type == 0) {
        const double root = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * c + root);
        b1 = 2 * A * ((A - 1) - (A + 1) * c);
        b2 = A * ((A + 1) - (A - 1) * c - root);
        a0 = (A + 1) + (A - 1) * c + root;
        a1 = -2 * ((A - 1) + (A + 1) * c);
        a2 = (A + 1) + (A - 1) * c - root;
    } else if (type == 2) {
        const double root = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * c + root);
        b1 = -2 * A * ((A - 1) + (A + 1) * c);
        b2 = A * ((A + 1) + (A - 1) * c - root);
        a0 = (A + 1) - (A - 1) * c + root;
        a1 = 2 * ((A - 1) - (A + 1) * c);
        a2 = (A + 1) - (A - 1) * c - root;
    } else if (type == 1) {
        alpha = s / 2.;
        b0 = 1 + alpha * A;
        b1 = -2 * c;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * c;
        a2 = 1 - alpha / A;
    } else {
        alpha = s * std::sinh(std::log(2.) / 2 * qBound(.1, width, 4.) * w / s);
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
        a0 = 1 + alpha;
        a1 = -2 * c;
        a2 = 1 - alpha;
    }
    Stage stage;
    stage.b0 = b0 / a0;
    stage.b1 = b1 / a0;
    stage.b2 = b2 / a0;
    stage.a1 = a1 / a0;
    stage.a2 = a2 / a0;
    _stages.push_back(stage);
}
AuditionFilter::AuditionFilter(int rate, const QVariantMap &settings) {
    if (rate < 8000)
        return;
    if (settings.value("enabled", false).toBool()) {
        const char *names[] = {"low", "middle", "high"};
        const double frequencies[] = {180, 900, 4000};
        for (int band = 0; band < 3; ++band) {
            const double gain = settings.value(names[band], 0.).toDouble();
            if (std::isfinite(gain) && std::abs(gain) > .001)
                add(band, frequencies[band], gain, 1., rate);
        }
    }
    const double frequency = settings.value("frequency", 440.).toDouble(),
                 width = settings.value("width", .5).toDouble();
    if (settings.value("selectedFrequency", false).toBool() && std::isfinite(frequency) &&
        std::isfinite(width))
        add(3, frequency, 0, width, rate);
}
void AuditionFilter::process(float *stereo, int frames) {
    for (auto &stage : _stages)
        for (int f = 0; f < frames; ++f)
            for (int channel = 0; channel < 2; ++channel) {
                const double x = stereo[f * 2 + channel], y = stage.b0 * x + stage.z1[channel];
                stage.z1[channel] = stage.b1 * x - stage.a1 * y + stage.z2[channel];
                stage.z2[channel] = stage.b2 * x - stage.a2 * y;
                if (std::abs(stage.z1[channel]) < 1e-20)
                    stage.z1[channel] = 0;
                if (std::abs(stage.z2[channel]) < 1e-20)
                    stage.z2[channel] = 0;
                stereo[f * 2 + channel] = float(y);
            }
}
} // namespace KumoAudio
