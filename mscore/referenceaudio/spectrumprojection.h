// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "spectrum.h"
namespace KumoAudio
{
SpectrumRequest spectrumBlock(const SpectrumRequest&, int index);
void projectSpectrumBlock(const SpectrumRequest&, const SpectrumTile&, QImage&,
                          const std::atomic_bool&);
} // namespace KumoAudio
