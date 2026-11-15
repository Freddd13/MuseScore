// SPDX-License-Identifier: GPL-2.0-or-later
#include "audioreader.h"
#include "audiofileidentity.h"
#include "ffmpegtool.h"
#include "referencecache.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QProcess>
#include <QTemporaryFile>
#ifdef Q_OS_WIN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#endif
#include <sndfile.h>
#define DR_MP3_IMPLEMENTATION
#include "vendor/dr_mp3.h"
#include "SoundTouch.h"
#include <cmath>
#include <mutex>

namespace KumoAudio {
namespace {

QString message(const char* value)
{
    return QCoreApplication::translate("ReferenceAudio", value);
}

SNDFILE* openSndfile(const QString& path, SF_INFO* info)
{
    *info = {};
#ifdef Q_OS_WIN
    return sf_wchar_open(reinterpret_cast<const wchar_t*>(path.utf16()), SFM_READ, info);
#else
    return sf_open(path.toUtf8().constData(), SFM_READ, info);
#endif
}

QString fallbackDemuxer(const QString& suffix)
{
    if (suffix == "m4a")
        return "mov";
    if (suffix == "aac")
        return "aac";
    if (suffix == "ogg" || suffix == "oga" || suffix == "opus")
        return "ogg";
    if (suffix == "aif" || suffix == "aiff")
        return "aiff";
    return {};
}

bool cancelled(const std::atomic_bool* flag)
{
    return flag && flag->load(std::memory_order_relaxed);
}

bool decodeAudio(const QString& executable, const QString& source, const QString& demuxer,
                 const QString& destination, qint64 sizeLimit, const std::atomic_bool* cancel,
                 QString* error)
{
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.setStandardOutputFile(QProcess::nullDevice());
    process.start(executable,
                  {"-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                   "-protocol_whitelist", "file", "-f", demuxer, "-i", source,
                   "-map", "0:a:0", "-vn", "-sn", "-dn", "-c:a", "pcm_f32le",
                   "-rf64", "auto", "-f", "wav", destination},
                  QIODevice::ReadOnly);
    QElapsedTimer activity;
    activity.start();
    qint64 bytes = 0;
    QByteArray diagnostics;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(40);
        diagnostics = (diagnostics + process.readAllStandardError()).right(4096);
        const auto written = QFileInfo(destination).size();
        if (written > bytes) {
            bytes = written;
            activity.restart();
        }
        if (cancelled(cancel) || written > sizeLimit || activity.elapsed() > 30000) {
            process.kill();
            process.waitForFinished(1000);
            if (error && !cancelled(cancel)) {
                *error = written > sizeLimit
                    ? message("解码后的音频超出磁盘缓存上限。请提高频谱设置中的磁盘缓存容量。")
                    : message("读取音频超时。请检查录音文件后重试。");
            }
            return false;
        }
    }
    if (cancelled(cancel))
        return false;
    if (process.error() == QProcess::FailedToStart || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0 || bytes == 0) {
        if (error) {
            const auto reason = QString::fromUtf8(diagnostics).trimmed();
            *error = message("无法解码这份音频。");
            if (!reason.isEmpty())
                *error += " " + reason.section('\n', 0, 0);
        }
        return false;
    }
    return true;
}

SNDFILE* openConvertedAudio(const QFileInfo& source, const QString& demuxer,
                            SF_INFO* info, std::unique_ptr<QTemporaryFile>* lease,
                            const std::atomic_bool* cancel, QString* error)
{
    QSettings settings;
    const auto root = QDir(settings.value("spectrum/cacheDirectory", referenceCacheDirectory()).toString());
    const auto limitMiB = qBound(0, settings.value("spectrum/diskMiB", 1024).toInt(), 16384);
    const auto maxBytes = qint64(limitMiB ? limitMiB : 1024) * 1024 * 1024;
    const auto cache = QDir(root.filePath("kumo-reference-audio-v1"));
    const auto sourcePath = source.absoluteFilePath();
    const auto fingerprint = audioFingerprint(sourcePath);
    const QString cachedFile = limitMiB && !fingerprint.isEmpty()
        ? cache.filePath(fingerprint + ".wav")
        : QString();

    auto cached = [&]() -> SNDFILE* {
        if (cachedFile.isEmpty() || !QFileInfo(cachedFile).isFile())
            return nullptr;
        return openSndfile(cachedFile, info);
    };
    if (auto ready = cached())
        return ready;

    // Simultaneous open, waveform and spectrum requests should use a single decoder process.
    static QMutex conversion;
    while (!conversion.tryLock(40)) {
        if (cancelled(cancel))
            return nullptr;
        if (auto ready = cached())
            return ready;
    }
    std::unique_lock<QMutex> lock(conversion, std::adopt_lock);
    if (cancelled(cancel))
        return nullptr;
    if (auto ready = cached())
        return ready;

    const auto executable = ffmpegExecutable();
    if (executable.isEmpty()) {
        if (error)
            *error = message("找不到音频解码工具。请检查程序目录中的 tools/video/ffmpeg.exe。");
        return nullptr;
    }
    if (!QDir().mkpath(cache.absolutePath())) {
        if (error)
            *error = message("无法建立音频缓存目录。请检查频谱设置中的缓存位置。");
        return nullptr;
    }
    auto temporary = std::make_unique<QTemporaryFile>(cache.filePath(".decode-XXXXXX.wav"));
    if (!temporary->open()) {
        if (error)
            *error = message("无法写入音频缓存。请检查磁盘空间和权限。");
        return nullptr;
    }
    temporary->close();
    if (!decodeAudio(executable, sourcePath, demuxer, temporary->fileName(), maxBytes, cancel, error))
        return nullptr;

    if (!cachedFile.isEmpty() &&
        QFileInfo(sourcePath).size() == source.size() &&
        audioFingerprint(sourcePath) == fingerprint && temporary->rename(cachedFile)) {
        temporary->setAutoRemove(false);
        auto result = openSndfile(cachedFile, info);
        pruneReferenceCache(root.absolutePath(), maxBytes, cachedFile);
        return result;
    }

    const auto result = openSndfile(temporary->fileName(), info);
    if (result)
        *lease = std::move(temporary);
    return result;
}

} // namespace

struct Reader::State {
    SNDFILE* file = nullptr;
    SF_INFO info{};
    drmp3 mp3{};
    bool compressed = false;
    qint64 count = 0;
    QFile input;
    const std::atomic_bool* cancel = nullptr;
    std::unique_ptr<QTemporaryFile> audioLease;
};

Reader::Reader() : s(new State) {}

Reader::~Reader()
{
    if (s->file)
        sf_close(s->file);
    if (s->compressed)
        drmp3_uninit(&s->mp3);
}

bool Reader::open(const QString& path, QString* error, const std::atomic_bool* cancel)
{
    if (s->file || s->compressed || cancelled(cancel))
        return false;

    const QFileInfo source(path);
    const QString suffix = source.suffix().toLower();
    if (!source.isFile() || !source.isReadable()) {
        if (error)
            *error = message("找不到音频或无法读取：%1").arg(path);
        return false;
    }
    if (suffix == "mp3") {
        s->cancel = cancel;
        s->input.setFileName(path);
        if (!s->input.open(QIODevice::ReadOnly))
            return false;
        auto read = [](void* ctx, void* out, size_t amount) -> size_t {
            auto state = static_cast<State*>(ctx);
            if (cancelled(state->cancel))
                return 0;
            return size_t(qMax<qint64>(0, state->input.read(static_cast<char*>(out), qint64(amount))));
        };
        auto seek = [](void* ctx, int offset, drmp3_seek_origin origin) -> drmp3_bool32 {
            auto state = static_cast<State*>(ctx);
            if (cancelled(state->cancel))
                return DRMP3_FALSE;
            const qint64 base = origin == DRMP3_SEEK_SET ? 0
                               : origin == DRMP3_SEEK_END ? state->input.size() : state->input.pos();
            return state->input.seek(base + offset) ? DRMP3_TRUE : DRMP3_FALSE;
        };
        auto tell = [](void* ctx, drmp3_int64* out) -> drmp3_bool32 {
            *out = static_cast<State*>(ctx)->input.pos();
            return DRMP3_TRUE;
        };
        s->compressed = drmp3_init(&s->mp3, read, seek, tell, nullptr, s.get(), nullptr);
        if (s->compressed) {
            static QMutex mutex;
            static QHash<QString, qint64> counts;
            const QString key = source.absoluteFilePath() + ':' + QString::number(source.size()) +
                                ':' + QString::number(source.lastModified().toMSecsSinceEpoch());
            {
                QMutexLocker lock(&mutex);
                s->count = counts.value(key, 0);
            }
            if (!s->count) {
                s->count = drmp3_get_pcm_frame_count(&s->mp3);
                if (!cancelled(cancel)) {
                    QMutexLocker lock(&mutex);
                    if (counts.size() >= 16)
                        counts.clear();
                    counts.insert(key, s->count);
                }
            }
            if (cancelled(cancel))
                return false;
            drmp3_seek_to_pcm_frame(&s->mp3, 0);
        }
    } else {
        s->file = openSndfile(path, &s->info);
        if (!s->file) {
            const auto demuxer = fallbackDemuxer(suffix);
            if (!demuxer.isEmpty())
                s->file = openConvertedAudio(source, demuxer, &s->info, &s->audioLease, cancel, error);
        }
        if (s->file)
            s->count = s->info.frames;
    }
    if ((!s->file && !s->compressed) || channels() < 1 || channels() > 2 ||
        rate() < 8000 || rate() > 192000 || !s->count) {
        if (error && error->isEmpty() && !cancelled(cancel))
            *error = message("无法读取此音频。请选择单声道或双声道录音：%1").arg(path);
        return false;
    }
    return !cancelled(cancel);
}

int Reader::rate() const { return s->compressed ? s->mp3.sampleRate : s->info.samplerate; }
int Reader::channels() const { return s->compressed ? s->mp3.channels : s->info.channels; }
qint64 Reader::frames() const { return s->count; }
qint64 Reader::read(float* output, qint64 frames)
{
    return s->compressed ? drmp3_read_pcm_frames_f32(&s->mp3, frames, output)
                         : sf_readf_float(s->file, output, frames);
}
bool Reader::seek(qint64 frame)
{
    return s->compressed ? bool(drmp3_seek_to_pcm_frame(&s->mp3, frame))
                         : sf_seek(s->file, frame, SEEK_SET) >= 0;
}

bool streamSequence(const QString& path,const std::vector<Segment>& segments,int outputRate,
 const std::function<bool(const float*,int)>& sink,const std::atomic_bool& cancel,QString* error) {
 if(segments.empty())return false;Reader reader;if(!reader.open(path,error,&cancel))return false;
 const int rate=reader.rate(),channels=reader.channels();qint64 target=0,previous=-1;
 for(auto seg:segments){qint64 begin=std::llround(seg.start*rate/1000),end=std::llround(seg.end*rate/1000);if(begin<0||end>reader.frames()||end<=begin||(previous>=0&&begin!=previous)||seg.speed<.5||seg.speed>1.5)return false;previous=end;target+=std::llround(double(end-begin)*outputRate/(rate*seg.speed));}
 if(!reader.seek(std::llround(segments.front().start*rate/1000)))return false;
 soundtouch::SoundTouch stretch;stretch.setChannels(2);stretch.setSampleRate(outputRate);stretch.setRate(double(rate)/outputRate);stretch.setSetting(SETTING_USE_QUICKSEEK,0);
 std::vector<float> input(256*channels),stereo(512),out(8192);qint64 produced=0;
 auto drain=[&](){while(!cancel){int count=stretch.receiveSamples(out.data(),4096);if(!count)break;int accepted=int(qMin<qint64>(count,target-produced));if(accepted<=0)break;
  if(segments.front().fade)for(int f=0;f<accepted;++f){double gain=qMin(1.,qMin(double(produced+f)/qMax(1,outputRate/200),double(target-produced-f)/qMax(1,outputRate/200)));out[f*2]*=float(gain);out[f*2+1]*=float(gain);}
  if(!sink(out.data(),accepted))return false;produced+=accepted;}return !cancel;};
 for(auto seg:segments){stretch.setTempo(seg.speed);qint64 remaining=std::llround(seg.end*rate/1000)-std::llround(seg.start*rate/1000);
  while(remaining>0&&!cancel){qint64 count=reader.read(input.data(),qMin<qint64>(256,remaining));if(count<=0)return false;remaining-=count;for(int f=0;f<count;++f)for(int c=0;c<2;++c)stereo[f*2+c]=input[f*channels+qMin(c,channels-1)];stretch.putSamples(stereo.data(),unsigned(count));if(!drain())return false;}
 }
 stretch.flush();return drain()&&!cancel;
}
bool stream(const QString& path,const Segment& seg,int outputRate,
 const std::function<bool(const float*,int)>& sink,const std::atomic_bool& cancel,QString* error) {
 Reader reader;if(!reader.open(path,error,&cancel))return false;
 const int rate=reader.rate(),channels=reader.channels();
 const qint64 begin=qBound<qint64>(0,std::llround(seg.start*rate/1000),reader.frames());
 const qint64 end=qBound<qint64>(begin,std::llround(seg.end*rate/1000),reader.frames());
 if(end<=begin||seg.speed<.5||seg.speed>1.5||outputRate<8000||!reader.seek(begin))return false;
 soundtouch::SoundTouch stretch;stretch.setChannels(2);stretch.setSampleRate(outputRate);
 const double ratio=double(rate)/outputRate;
 stretch.setRate(ratio);stretch.setTempo(seg.speed);
 stretch.setSetting(SETTING_USE_QUICKSEEK,0);
 std::vector<float> input(4096*channels),stereo(8192),out(8192);
 qint64 remaining=end-begin,produced=0;
 const qint64 target=std::llround(double(end-begin)*outputRate/(rate*seg.speed));
 auto drain=[&]() {
  while(!cancel) {
   const int count=stretch.receiveSamples(out.data(),4096);if(!count)break;
   int accepted=int(qMin<qint64>(count,target-produced));if(accepted<=0)break;
   if(seg.fade)for(int f=0;f<accepted;++f) {
    double gain=qMin(1.,qMin(double(produced+f)/qMax(1,outputRate/200),double(target-produced-f)/qMax(1,outputRate/200)));
    out[f*2]*=float(gain);out[f*2+1]*=float(gain);
   }
   if(!sink(out.data(),accepted))return false;produced+=accepted;
  }
  return !cancel;
 };
 while(remaining>0&&!cancel) {
  qint64 count=reader.read(input.data(),qMin<qint64>(4096,remaining));if(!count)break;remaining-=count;
  for(int f=0;f<count;++f)for(int c=0;c<2;++c)stereo[f*2+c]=input[f*channels+qMin(c,channels-1)];
  stretch.putSamples(stereo.data(),unsigned(count));if(!drain())return false;
 }
 stretch.flush();if(!drain())return false;
 return !cancel&&remaining==0;
}
}
