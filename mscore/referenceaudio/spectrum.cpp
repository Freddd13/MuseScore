// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrum.h"
#include "audioreader.h"
#include <QObject>
#include "vendor/kissfft/kiss_fftr.h"
#include <QFileInfo>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDataStream>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace KumoAudio {
QString spectrumKey(const SpectrumRequest &r) {
    const QFileInfo file(r.path);
    QByteArray values;
    QDataStream stream(&values, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_9);
    stream << quint32(3) << file.absoluteFilePath() << file.size() << file.lastModified().toMSecsSinceEpoch()
           << r.fromMs << r.untilMs << r.columns << r.rows << r.fftSize << r.lowPitch << r.highPitch
           << r.tuning;
    return QString::fromLatin1(QCryptographicHash::hash(values, QCryptographicHash::Sha256).toHex());
}

QString validateSpectrumRequest(const SpectrumRequest &r) {
    if (!std::isfinite(r.fromMs) || !std::isfinite(r.untilMs) || r.fromMs < 0 || r.untilMs > 1e12 || r.untilMs <= r.fromMs ||
        r.untilMs - r.fromMs > 120000 || r.columns < 1 || r.columns > 1024 || r.rows < 1 || r.rows > 1024 ||
        (r.fftSize != 4096 && r.fftSize != 16384 && r.fftSize != 32768) || !std::isfinite(r.lowPitch) ||
        !std::isfinite(r.highPitch) || r.lowPitch < 0 || r.highPitch > 128 || r.highPitch <= r.lowPitch ||
        !std::isfinite(r.tuning) || r.tuning < 400 || r.tuning > 480) {
        return QObject::tr("频谱参数超出可用范围。请恢复显示预设后重试。");
    }
    return {};
}

SpectrumTile spectrum(const SpectrumRequest &r, const std::atomic_bool &cancel) {
    SpectrumTile tile;
    tile.fromMs = r.fromMs;
    tile.untilMs = r.untilMs;
    tile.lowPitch = r.lowPitch;
    tile.highPitch = r.highPitch;
    tile.error = validateSpectrumRequest(r);
    if (!tile.error.isEmpty()) return tile;
    if (cancel)
        return tile;
    Reader reader;
    if (!reader.open(r.path, &tile.error, &cancel)) {
        if (tile.error.isEmpty() && !cancel)
            tile.error = QObject::tr("无法读取音频：%1").arg(r.path);
        return tile;
    }
    const int rate = reader.rate(), channels = reader.channels(), n = r.fftSize;
    // Twofold zero padding in the low-pitch preset smooths frequency-bin positions.
    // The Hann window length still determines actual frequency resolution.
    const int fftLength = n == 32768 ? n * 2 : n;
    tile.sampleRate = rate;
    tile.durationMs = double(reader.frames()) * 1000. / rate;
    using Fft = std::unique_ptr<kiss_fftr_state, decltype(&free)>;
    Fft fft(kiss_fftr_alloc(fftLength, 0, nullptr, nullptr), &free);
    if (!fft) {
        tile.error = QObject::tr("频谱计算内存不足。");
        return tile;
    }
    std::vector<float> samples(size_t(n) * channels, 0), input(size_t(fftLength), 0);
    std::vector<kiss_fft_cpx> bins(size_t(fftLength / 2 + 1));
    QVector<double> window(n), energy(fftLength / 2 + 1), binPositions(r.rows);
    double windowSum = 0;
    for (int i = 0; i < n; ++i) {
        window[i] = .5 - .5 * std::cos(2. * 3.14159265358979323846 * i / (n - 1));
        windowSum += window[i];
    }
    for (int y = 0; y < r.rows; ++y) {
        const double pitch = r.highPitch - (y + .5) / r.rows * (r.highPitch - r.lowPitch);
        binPositions[y] = r.tuning * std::pow(2., (pitch - 69.) / 12.) * fftLength / rate;
    }
    tile.image = QImage(r.columns, r.rows, QImage::Format_Grayscale8);
    tile.image.fill(0);
    qint64 buffered = -1;
    for (int x = 0; x < r.columns && !cancel; ++x) {
        const double ms = r.fromMs + (x + .5) / r.columns * (r.untilMs - r.fromMs);
        const qint64 wanted = qint64(std::llround(ms * rate / 1000.)) - n / 2;
        // Reuse the overlap. A pan/zoom request never decodes an entire file into memory.
        const qint64 advance = wanted - buffered;
        int retained = 0;
        if (buffered >= 0 && advance >= 0 && advance < n) {
            retained = n - int(advance);
            std::move(samples.begin() + size_t(advance) * channels, samples.end(), samples.begin());
        } else {
            if (!reader.seek(qBound<qint64>(0, wanted, reader.frames()))) {
                tile.error = QObject::tr("无法定位音频的这一段。");
                tile.image = QImage();
                return tile;
            }
            retained = int(qMin<qint64>(n, qMax<qint64>(0, -wanted)));
            std::fill(samples.begin(), samples.begin() + size_t(retained) * channels, 0);
        }
        const auto count = reader.read(samples.data() + size_t(retained) * channels, n - retained);
        std::fill(samples.begin() + size_t(retained + count) * channels, samples.end(), 0);
        buffered = wanted;
        energy.fill(0);
        // Add channel powers; opposite stereo phases must not erase a real note.
        for (int channel = 0; channel < channels && !cancel; ++channel) {
            for (int i = 0; i < n; ++i)
                input[size_t(i)] = float(samples[size_t(i) * channels + channel] * window[i]);
            kiss_fftr(fft.get(), input.data(), bins.data());
            for (int i = 0; i <= fftLength / 2; ++i)
                energy[i] += (double(bins[size_t(i)].r) * bins[size_t(i)].r +
                              double(bins[size_t(i)].i) * bins[size_t(i)].i) /
                             channels;
        }
        const double normalization = 4. / (windowSum * windowSum);
        for (int y = 0; y < r.rows; ++y) {
            const double position = binPositions[y];
            if (position < 1 || position >= fftLength / 2)
                continue;
            const int left = int(position);
            double power = energy[left] * (1. - (position - left)) + energy[left + 1] * (position - left);
            // At high pitches, one display row spans several FFT bins.
            const double rowBand = position * (std::pow(2., (r.highPitch - r.lowPitch) / r.rows / 24.) - 1.);
            if (rowBand > .75)
                for (int i = qMax(1, int(std::ceil(position - rowBand)));
                     i <= qMin(fftLength / 2, int(std::floor(position + rowBand))); ++i)
                    power = qMax(power, energy[i]);
            const double db = 10. * std::log10(qMax(1e-14, power * normalization));
            tile.image.scanLine(y)[x] = uchar(qBound(0, qRound((db + 85.) / 85. * 255.), 255));
        }
    }
    if (cancel)
        tile.image = QImage();
    return tile;
}
// Harmonic-weighted peak support guides optional display attenuation. Raw blocks
// remain unchanged; the user can mix the result with the measured spectrum.
QImage emphasizeFundamental(const SpectrumTile& tile,double mix,const std::atomic_bool& cancel){
    if(tile.image.isNull()||!std::isfinite(mix)||mix<=0)return tile.image;
    mix=qBound(0.,mix,1.);const auto& raw=tile.image;QImage result=raw.copy();
    QVector<double> amplitude(raw.height()),support(raw.height());
    QVector<int> offsets;for(int h=1;h<=8;++h)offsets.append(qRound(12.*std::log2(h)*raw.height()/(tile.highPitch-tile.lowPitch)));
    double levels[256];for(int i=0;i<256;++i)levels[i]=std::pow(10.,(i/255.*85.-85.)/20.);
    for(int x=0;x<raw.width()&&!cancel;++x){
        double maximum=0;
        for(int y=0;y<raw.height();++y){amplitude[y]=levels[raw.constScanLine(y)[x]];maximum=qMax(maximum,amplitude[y]);}
        auto peak=[&](int at){double v=0;for(int dy=-1;dy<=1;++dy)if(at+dy>=0&&at+dy<amplitude.size())v=qMax(v,amplitude[at+dy]);return v;};
        for(int y=0;y<raw.height();++y){double value=0,weight=1;for(int h=0;h<offsets.size();++h){value+=peak(y-offsets[h])*weight;weight*=.8;}support[y]=value;}
        for(int y=0;y<raw.height();++y){
            double reduction=0;
            for(int h=1;h<offsets.size();++h){const int below=y+offsets[h];if(below>=amplitude.size())break;
                if(peak(below)<maximum*.01||peak(below)<amplitude[y]*.15)continue;
                reduction=qMax(reduction,qBound(0.,(support[below]/qMax(1e-7,support[y])-1.)*.5,.9));
            }
            const double db=20.*std::log10(qMax(1e-8,amplitude[y]*(1-mix*reduction)));
            result.scanLine(y)[x]=uchar(qBound(0,qRound((db+85.)/85.*255.),255));
        }
    }
    return cancel?QImage():result;
}
} // namespace KumoAudio
