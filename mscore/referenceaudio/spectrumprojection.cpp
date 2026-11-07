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
    for (int x = left; x < right && !cancel; ++x) {
        const double start = view.fromMs + x * (view.untilMs - view.fromMs) / view.columns;
        const double end = view.fromMs + (x + 1) * (view.untilMs - view.fromMs) / view.columns;
        const int a = qBound(0,
                             int(std::floor((start - block.fromMs) /
                                            (block.untilMs - block.fromMs) * block.image.width())),
                             block.image.width() - 1);
        const int b = qBound(a + 1,
                             int(std::ceil((end - block.fromMs) / (block.untilMs - block.fromMs) *
                                           block.image.width())),
                             block.image.width());
        for (int y = 0; y < view.rows; ++y) {
            const auto row = rows[y];
            int value = 0;
            if (row.interpolate) {
                for (int i = a; i < b; ++i) {
                    const int low = row.first < 0 || row.first >= block.image.height()
                                        ? 0
                                        : block.image.constScanLine(row.first)[i];
                    const int high = row.last < 0 || row.last >= block.image.height()
                                         ? 0
                                         : block.image.constScanLine(row.last)[i];
                    value = qMax(value, qRound(low * (1. - row.blend) + high * row.blend));
                }
            } else
                for (int r = qMax(0, row.first); r <= qMin(block.image.height() - 1, row.last); ++r)
                    for (int i = a; i < b; ++i)
                        value = qMax(value, int(block.image.constScanLine(r)[i]));
            image.scanLine(y)[x] = uchar(qMax(int(image.constScanLine(y)[x]), value));
        }
    }
}
} // namespace KumoAudio
