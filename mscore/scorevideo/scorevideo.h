// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "libmscore/score.h"
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QVector>
#include <memory>
namespace Ms
{
struct VideoClockPoint {
    double nativeSeconds = 0, seconds = 0, x = 0;
    int tick = 0;
};
struct VideoNoteInk {
    QRectF box;
    double from = 0, until = 0;
};
struct VideoPicture {
    QString id, file, label;
    QSize pixels;
    double firstStaffX = 0, firstStaffY = 0;
    int firstTick = 0, endTick = 0;
};
struct VideoCue {
    QString id, label, clockSource;
    int picture = 0, start = 0, end = 0, fromUtick = 0, untilUtick = 0, occurrence = 1;
    double automaticFrom = 0, automaticUntil = 0, from = 0, until = 0;
    QVector<VideoClockPoint> clock;
    QVector<VideoNoteInk> notes;
};
struct VideoSnapshot {
    QString title, audioPath, audioKind, audioId;
    QVariantMap settings, audioIdentity;
    QVector<VideoPicture> pictures;
    QVector<VideoCue> cues;
    QVariantList audioPieces;
    double duration = 0, preRoll = 0;
    int sampleRate = 48000;
    std::shared_ptr<QTemporaryDir> assets;
};
struct VideoFrameCache {
    int picture = -1;
    QString file;
    QImage ink;
    bool inverted = false;
};
QVariantMap defaultScoreVideoSettings();
int scoreVideoCueAt(const VideoSnapshot&, double seconds);
QRectF scoreVideoPictureRect(const VideoSnapshot&, int picture, const QSize& size);
QImage scoreVideoFrame(const VideoSnapshot&, double seconds, VideoFrameCache*, QSize size = {});
QString scoreVideoEncoder();

// Score ownership stays in this GUI-thread document. Export receives the
// frozen pictures and values above; source and copy never cross into a worker.
class ScoreVideoDocument final : public QObject
{
    Q_OBJECT
    QPointer<Score> _source;
    std::unique_ptr<MasterScore> _copy;
    ScoreContentState _sourceState;
    QVariantMap _settings, _reference;
    VideoSnapshot _snapshot, _native;
    QString _stored;
    quint64 _generation = 0;
    bool _busy = false, _cancelled = false;
    QString _status;
    struct Build;
    std::unique_ptr<Build> _build;
    void buildNext();
    void saveSettings();
    void rebuildTimeline();

  public:
    explicit ScoreVideoDocument(QObject* parent = nullptr);
    ~ScoreVideoDocument() override;
    Score* source() const { return _source; }
    MasterScore* preparedScore() const { return _copy.get(); }
    const VideoSnapshot& snapshot() const { return _snapshot; }
    bool busy() const { return _busy; }
    bool ready() const
    {
        return !_snapshot.cues.isEmpty() && !_busy &&
               (_settings.value("audioKind").toString() != "reference" ||
                !_snapshot.audioPath.isEmpty());
    }
    bool current() const;
    QString status() const { return _status; }
    QVariantMap settings() const { return _settings; }
    QVariantList audioSources() const;
    void setScore(Score*);
    Q_INVOKABLE QVariantMap state() const;
    Q_INVOKABLE void prepare(const QVariantMap& settings = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool changeSettings(const QVariantMap& fields);
    Q_INVOKABLE bool moveBoundary(int index, double seconds);
    Q_INVOKABLE bool movePicture(int picture, double xPercent, double yPercent);
    Q_INVOKABLE void restoreTiming();
    Q_INVOKABLE void restorePicture(int picture);
    void restoreStoredSettings();
  signals:
    void changed();
    void prepared();
};
} // namespace Ms
