// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumprojection.h"
#include <QVector>
#include <cmath>
#include <algorithm>

namespace KumoAudio
{
SpectrumRequest spectrumBlock(const SpectrumRequest& view, int index)
{
    auto block = view;
    block.fromMs = index * 4000.;
    block.untilMs = block.fromMs + 4000.;
    block.columns = view.fftSize == 4096 ? 400 : 200;
    block.rows = 1024;
    block.lowPitch = 0.;
    block.highPitch = 128.;
    block.tuning = 440.;
    return block;
}
void projectSpectrumBlock(const SpectrumRequest& view, const SpectrumTile& block, QImage& image,
                          const std::atomic_bool& cancel)
{
    if (block.image.isNull() || image.format() != QImage::Format_Grayscale8 ||
        image.width() != view.columns || image.height() != view.rows || view.untilMs <= view.fromMs)
        return;
    const int left = qMax(0, int(std::floor((block.fromMs - view.fromMs) /
                                            (view.untilMs - view.fromMs) * view.columns)));
    const int right =
        qMin(view.columns, int(std::ceil((block.untilMs - view.fromMs) /
                                         (view.untilMs - view.fromMs) * view.columns)));
    const double tuningShift = 12. * std::log2(view.tuning / 440.);
    struct PitchRow {
        int first, last;
        double blend;
        bool interpolate;
    };
    QVector<PitchRow> rows;
    rows.reserve(view.rows);
    const double rawRowsPerPitch = block.image.height() / (block.highPitch - block.lowPitch);
    const double band = (view.highPitch - view.lowPitch) / view.rows * rawRowsPerPitch;
    for (int y = 0; y < view.rows; ++y) {
        const double pitch =
            view.highPitch - (y + .5) / view.rows * (view.highPitch - view.lowPitch) + tuningShift;
        const double at = (block.highPitch - pitch) * rawRowsPerPitch - .5;
        if (band <= 1.) {
            const int a = int(std::floor(at));
            rows.append({a, a + 1, at - a, true});
        } else
            rows.append({int(std::floor(at - band / 2. + .5)), int(std::ceil(at + band / 2. - .5)),
                         0, false});
    }
    const double msPerColumn = (view.untilMs - view.fromMs) / view.columns;
    const double rawPerMs = block.image.width() / (block.untilMs - block.fromMs);
    const bool interpolateTime = msPerColumn * rawPerMs <= 1.;
    for (int x = left; x < right && !cancel; ++x) {
        const double rawStart = (view.fromMs + x * msPerColumn - block.fromMs) * rawPerMs;
        const double rawEnd = rawStart + msPerColumn * rawPerMs;
        const double centre = (rawStart + rawEnd) / 2.;
        if (interpolateTime && (centre < 0. || centre >= block.image.width()))
            continue;
        const double sample = qBound(0., centre - .5, double(block.image.width() - 1));
        const int a = interpolateTime ? int(sample)
                                      : qBound(0, int(std::floor(rawStart)), block.image.width() - 1);
        const int b = interpolateTime ? qMin(a + 1, block.image.width() - 1)
                                      : qBound(a + 1, int(std::ceil(rawEnd)), block.image.width());
        const auto columnValue = [&](const uchar* row) {
            return qRound(row[a] + (row[b] - row[a]) * (sample - a));
        };
        for (int y = 0; y < view.rows; ++y) {
            const auto row = rows[y];
            int value = 0;
            if (row.interpolate) {
                const auto low = row.first >= 0 && row.first < block.image.height()
                                     ? block.image.constScanLine(row.first) : nullptr;
                const auto high = row.last >= 0 && row.last < block.image.height()
                                      ? block.image.constScanLine(row.last) : nullptr;
                if (interpolateTime)
                    value = qRound((low ? columnValue(low) : 0) * (1. - row.blend) +
                                   (high ? columnValue(high) : 0) * row.blend);
                else
                    for (int i = a; i < b; ++i)
                        value = qMax(value, qRound((low ? low[i] : 0) * (1. - row.blend) +
                                                   (high ? high[i] : 0) * row.blend));
            } else {
                for (int r = qMax(0, row.first); r <= qMin(block.image.height() - 1, row.last); ++r) {
                    const auto line = block.image.constScanLine(r);
                    value = qMax(value, interpolateTime ? columnValue(line)
                                                       : int(*std::max_element(line + a, line + b)));
                }
            }
            image.scanLine(y)[x] = uchar(qMax(int(image.constScanLine(y)[x]), value));
        }
    }
}
} // namespace KumoAudio
