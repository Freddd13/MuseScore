// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumannotation.h"
#include "libmscore/mscore.h"
#include <cmath>
namespace Ms {
QVariantMap estimateSpectrumExtent(const QImage &raw, const QVariantMap &info, double ms, double frequency) {
    const double from = info.value("from").toDouble(), until = info.value("until").toDouble(),
                 low = info.value("lowPitch").toDouble(), high = info.value("highPitch").toDouble();
    if (raw.isNull() || until <= from || high <= low || frequency <= 0)
        return {};
    const double pitch = 69 + 12 * std::log2(frequency / info.value("tuning", 440.).toDouble());
    const int row = qRound((high - pitch) / (high - low) * raw.height()),
              at = qBound(0, int((ms - from) / (until - from) * raw.width()), raw.width() - 1);
    if (row < 0 || row >= raw.height())
        return {};
    auto value = [&](int x) {
        int v = 0;
        for (int y = qMax(0, row - 1); y <= qMin(raw.height() - 1, row + 1); ++y)
            v = qMax(v, int(raw.constScanLine(y)[x]));
        return v;
    };
    const int peak = value(at);
    if (peak < 48)
        return {};
    const int threshold = qMax(32, peak - 36);
    int left = at, right = at;
    while (left > 0 && value(left - 1) >= threshold)
        --left;
    while (right + 1 < raw.width() && value(right + 1) >= threshold)
        ++right;
    const double step = (until - from) / raw.width(),
                 correction = .15 * info.value("fftSize", 16384.).toDouble() /
                              qMax(1., info.value("sampleRate", 44100.).toDouble()) * 1000;
    double start = from + left * step, end = from + (right + 1) * step;
    if (left > 0)
        start += qMin(correction, (end - start) * .2);
    if (right + 1 < raw.width())
        end -= qMin(correction, (end - start) * .2);
    if (end - start < 20)
        return {};
    return {{"ms", start},
            {"until", end},
            {"openStart", left == 0},
            {"openEnd", right + 1 == raw.width()},
            {"durationSource", "spectral-estimate"}};
}
TDuration spectrumInputDuration(double quarters) {
    if (!std::isfinite(quarters) || quarters <= 0)
        return TDuration(TDuration::DurationType::V_QUARTER);
    TDuration result(TDuration::DurationType::V_QUARTER);
    double distance = 1e99;
    for (int power = -4; power <= 3; ++power)
        for (int dots = 0; dots <= 1; ++dots) {
            const double beats = std::pow(2., power) * (dots ? 1.5 : 1.);
            const double delta = std::abs(quarters - beats);
            if (delta < distance) {
                TDuration next(Fraction::fromTicks(qRound(beats * DIVISION)));
                if (next.type() != TDuration::DurationType::V_INVALID) {
                    result = next;
                    distance = delta;
                }
            }
        }
    return result;
}
} // namespace Ms
