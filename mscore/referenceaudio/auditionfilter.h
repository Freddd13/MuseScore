// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QVariantMap>
#include <vector>
namespace KumoAudio {
// Per-stream state, owned by the decoding worker. Stereo buffers remain bounded.
class AuditionFilter {
    struct Stage {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2]{}, z2[2]{};
    };
    std::vector<Stage> _stages;
    void add(int type, double frequency, double gain, double width, int rate);

  public:
    AuditionFilter(int rate, const QVariantMap &settings);
    void process(float *stereo, int frames);
};
} // namespace KumoAudio
