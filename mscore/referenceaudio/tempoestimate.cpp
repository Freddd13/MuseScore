// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "tempoestimate.h"
#include "audioreader.h"
#include "vendor/kissfft/kiss_fftr.h"
#include <QObject>
#include <QUrl>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
namespace KumoAudio {
QVariantMap estimateTempo(const QVariantMap &request, const std::atomic_bool &cancel) {
    const double from = request.value("from", 0.).toDouble(),
                 until = request.value("until", 30000.).toDouble();
    if (!std::isfinite(from) || !std::isfinite(until) || from < 0 || until - from < 4000 ||
        until - from > 120000)
        return {{"error", QObject::tr("选择 4 秒至 2 分钟的片段后估计速度。")}};
    auto path = request.value("path").toString();
    if (QUrl(path).isLocalFile())
        path = QUrl(path).toLocalFile();
    QString error;
    Reader reader;
    if (!reader.open(path, &error, &cancel))
        return {{"error", error}};
    const int rate = reader.rate(), channels = reader.channels(), n = 2048,
              hop = qMax(1, qMin(n / 2, qRound(rate * .01)));
    if (from >= reader.frames() * 1000. / rate)
        return {{"error", QObject::tr("所选位置在音频结尾之后，请重新选片段。")}};
    const int frames =
        qMax(0, qMin(qRound((until - from) * rate / 1000),
                     int(qMin<qint64>(reader.frames() - qint64(from * rate / 1000), 120LL * rate))));
    if (frames < rate * 4)
        return {{"error", QObject::tr("这段音频不足 4 秒，请扩大选段。")}};
    if (!reader.seek(qint64(from * rate / 1000)))
        return {{"error", QObject::tr("无法读取这段音频。")}};
    std::unique_ptr<kiss_fftr_state, decltype(&kiss_fft_free)> fft(kiss_fftr_alloc(n, 0, nullptr, nullptr),
                                                                   &kiss_fft_free);
    if (!fft)
        return {{"error", QObject::tr("内存不足，稍后可以重新估计速度。")}};
    std::vector<float> samples(size_t(n) * channels), window(n), input(n);
    std::vector<kiss_fft_cpx> bins(n / 2 + 1);
    QVector<double> previous(n / 2 + 1), powers(n / 2 + 1), novelty;
    for (int i = 0; i < n; ++i)
        window[i] = float(.5 - .5 * std::cos(2 * 3.14159265358979323846 * i / (n - 1)));
    auto count = reader.read(samples.data(), n);
    std::fill(samples.begin() + size_t(count) * channels, samples.end(), 0.f);
    const int firstBin = qMax(1, qRound(60. * n / rate)), lastBin = qMin(n / 2, qRound(8000. * n / rate));
    double previousEnergy = 0;
    for (int at = 0; at < frames && !cancel; at += hop) {
        powers.fill(0);
        for (int channel = 0; channel < channels; ++channel) {
            for (int i = 0; i < n; ++i)
                input[i] = samples[size_t(i) * channels + channel] * window[i];
            kiss_fftr(fft.get(), input.data(), bins.data());
            for (int i = firstBin; i <= lastBin; ++i)
                powers[i] += (double(bins[i].r) * bins[i].r + double(bins[i].i) * bins[i].i) / channels;
        }
        double frameEnergy = 0;
        for (int i = firstBin; i <= lastBin; ++i)
            frameEnergy += powers[i];
        double flux = 0;
        for (int i = firstBin; i <= lastBin; ++i) {
            const double value = std::log1p(1000 * std::sqrt(powers[i]) / n);
            flux += qMax(0., value - previous[i]);
            previous[i] = value;
        }
        const double growth = qMax(0., frameEnergy - previousEnergy) / qMax(1e-15, frameEnergy);
        previousEnergy = frameEnergy;
        novelty.append(flux * (.15 + .85 * std::sqrt(growth)));
        std::move(samples.begin() + size_t(hop) * channels, samples.end(), samples.begin());
        count = reader.read(samples.data() + size_t(n - hop) * channels, hop);
        std::fill(samples.begin() + size_t(n - hop + count) * channels, samples.end(), 0.f);
    }
    if (cancel)
        return {};
    QVector<double> prefix(novelty.size() + 1);
    for (int i = 0; i < novelty.size(); ++i)
        prefix[i + 1] = prefix[i] + novelty[i];
    double energy = 0;
    for (int i = 0; i < novelty.size(); ++i) {
        const int a = qMax(0, i - 50), b = qMin(novelty.size(), i + 51);
        novelty[i] = qMax(0., novelty[i] - .5 * (prefix[b] - prefix[a]) / qMax(1, b - a));
        energy += novelty[i] * novelty[i];
    }
    if (energy < 1e-8)
        return {{"error", QObject::tr("这段的起音变化较少，换一段节奏清楚的音频，或用 Tap 设置速度。")}};
    const double step = double(hop) / rate;
    const int minLag = qMax(1, int(std::floor(60 / (240 * step)))), maxLag = int(std::ceil(60 / (40 * step)));
    QVector<double> correlation(maxLag * 3 + 3), scores(maxLag + 2);
    for (int lag = 1; lag < correlation.size() && !cancel; ++lag) {
        double cross = 0, a = 0, b = 0;
        for (int i = lag; i < novelty.size(); ++i) {
            cross += novelty[i] * novelty[i - lag];
            a += novelty[i] * novelty[i];
            b += novelty[i - lag] * novelty[i - lag];
        }
        correlation[lag] = cross / std::sqrt(qMax(1e-20, a * b));
    }
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double bpm = 60 / (lag * step),
                     prior = .65 + .35 * std::exp(-std::pow(std::log2(bpm / 110.), 2) / .8);
        scores[lag] = (correlation[lag] + .35 * correlation[lag * 2] + .15 * correlation[lag * 3]) * prior;
    }
    QVector<int> peaks;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (scores[lag] >= scores[lag - 1] && scores[lag] >= scores[lag + 1])
            peaks.append(lag);
    std::sort(peaks.begin(), peaks.end(), [&](int a, int b) { return scores[a] > scores[b]; });
    QVariantList candidates;
    QVector<double> accepted;
    for (int lag : peaks) {
        if (cancel)
            return {};
        const double denominator = scores[lag - 1] - 2 * scores[lag] + scores[lag + 1],
                     delta = std::abs(denominator) > 1e-10
                                 ? qBound(-.5, .5 * (scores[lag - 1] - scores[lag + 1]) / denominator, .5)
                                 : 0.;
        const double period = lag + delta, bpm = 60 / (period * step);
        if (bpm < 40 || bpm > 240 || std::any_of(accepted.begin(), accepted.end(), [&](double v) {
                return std::abs(std::log2(v / bpm)) < .06;
            }))
            continue;
        double best = -1, phase = 0;
        for (double offset = 0; offset < period; offset += .5) {
            double sum = 0;
            for (double index = offset; index + 1 < novelty.size(); index += period) {
                const int i = int(index);
                sum += novelty[i] * (1 - (index - i)) + novelty[i + 1] * (index - i);
            }
            if (sum > best) {
                best = sum;
                phase = offset;
            }
        }
        candidates.append(QVariantMap{{"bpm", bpm},
                                      {"firstBeat", from + (phase * hop + n / 2) * 1000. / rate},
                                      {"support", scores[lag]}});
        accepted.append(bpm);
        if (candidates.size() == 3)
            break;
    }
    if (candidates.isEmpty())
        return {{"error", QObject::tr("这段的速度候选较分散，试着用 Tap 跟着音乐敲几拍。")}};
    return {{"candidates", candidates},
            {"from", from},
            {"until", qMin(until, from + frames * 1000. / rate)},
            {"status", scores[peaks.front()] > 1 ? QObject::tr("节拍较规律，选择候选后对照听一遍。")
                                                 : QObject::tr("可以比较几个速度候选，再调整第一拍。")},
            {"method", "positive log-spectral flux and normalized autocorrelation"}};
}
} // namespace KumoAudio
