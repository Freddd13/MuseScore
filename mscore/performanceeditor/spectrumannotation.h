// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "libmscore/durationtype.h"
#include <QImage>
#include <QVariantMap>
namespace Ms {
QVariantMap estimateSpectrumExtent(const QImage &, const QVariantMap &, double ms, double frequency);
TDuration spectrumInputDuration(double quarters);
} // namespace Ms
