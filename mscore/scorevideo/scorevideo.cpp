// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "scorevideo.h"
#include "mscore/musescore.h"
#include "libmscore/chord.h"
#include "libmscore/bracket.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/page.h"
#include "libmscore/part.h"
#include "libmscore/playbacktiming.h"
#include "libmscore/repeatlist.h"
#include "libmscore/rendermidi.h"
#include "libmscore/segment.h"
#include "libmscore/staff.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/system.h"
#include "libmscore/tempo.h"
#include "libmscore/tie.h"
#include "libmscore/undo.h"
#include "audio/midi/event.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QPainter>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
namespace Ms
{
namespace
{
QString stored(Score* score)
{
    return score ? score->masterScore()->metaTag("kumo.scoreVideo") : QString();
}
QVariantMap decode(const QString& text)
{
    return QJsonDocument::fromJson(text.toUtf8()).toVariant().toMap();
}
class ChangeVideoSettings final : public UndoCommand
{
    MasterScore* _score;
    QString _value;
    void flip(EditData*) override
    {
        const auto previous = _score->metaTag("kumo.scoreVideo");
        _score->setMetaTag("kumo.scoreVideo", _value);
        _value = previous;
    }

  public:
    ChangeVideoSettings(MasterScore* score, QString value) : _score(score), _value(std::move(value))
    {
    }
    bool affectsScoreContent() const override { return false; }
    UNDO_NAME("ChangeVideoSettings")
};
System* systemFor(const Element* element)
{
    for (auto parent = element; parent; parent = parent->parent())
        if (parent->isSystem())
            return const_cast<System*>(toSystem(parent));
    return nullptr;
}
const Note* rootNote(const Note* note)
{
    QSet<const Note*> visited;
    while (note && note->tieBack() && note->tieBack()->startNote() && !visited.contains(note)) {
        visited.insert(note);
        note = note->tieBack()->startNote();
    }
    return note;
}
int inkStaff(const Element* element)
{
    for (auto parent = element; parent; parent = parent->parent())
        if (parent->isChord())
            return toChord(parent)->vStaffIdx();
    return element->staffIdx();
}
double tickX(Measure* measure, int tick)
{
    double left = 0, right = measure->width();
    int from = measure->tick().ticks(), until = measure->endTick().ticks();
    for (auto segment = measure->first(SegmentType::ChordRest); segment;
         segment = segment->next(SegmentType::ChordRest)) {
        if (!segment->enabled())
            continue;
        const int at = segment->tick().ticks();
        if (at == tick)
            return segment->canvasPos().x();
        if (at > tick) {
            right = segment->x();
            until = at;
            break;
        }
        left = segment->x();
        from = at;
    }
    return measure->canvasPos().x() + left +
           (right - left) * qBound(0., double(tick - from) / qMax(1, until - from), 1.);
}
QString validation(const QVariantMap& settings, Score* score)
{
    const int width = settings.value("width", 1920).toInt(),
              height = settings.value("height", 1080).toInt(),
              fps = settings.value("fps", 30).toInt();
    if (width < 64 || height < 64 || width > 7680 || height > 7680 || fps < 1 || fps > 120)
        return QObject::tr("请设置64—7680像素的画面和1—120帧的帧率。");
    if (settings.value("staves").toList().isEmpty())
        return QObject::tr("选择要显示的谱表后即可准备画面。");
    for (auto staff : settings.value("staves").toList())
        if (!score || staff.toInt() < 0 || staff.toInt() >= score->nstaves())
            return QObject::tr("谱表已经改变，请重新选择显示范围。");
    return {};
}
QString sourceKey(const VideoSnapshot& value)
{
    return value.audioKind == "reference" ? "reference/" + value.audioId : QString("score");
}
} // namespace
struct ScoreVideoDocument::Build {
    struct NoteTime {
        int on = 0, off = 0;
    };
    struct Group {
        System* system = nullptr;
        QRectF crop;
        QPointF staffOrigin;
        QList<Element*> elements;
        QHash<Element*, QRectF> clips;
        QHash<Element*, QPointF> offsets;
        double scale = 1;
    };
    std::unique_ptr<MasterScore> score;
    ScoreContentState sourceState;
    VideoSnapshot snapshot;
    QSet<int> staves;
    QList<System*> systems;
    QHash<Page*, QList<Element*>> pageElements;
    QVector<Group> groups;
    QHash<const Note*, QVector<NoteTime>> times;
    int phase = 0, systemIndex = 0, elementIndex = 0, pictureIndex = 0, repeatIndex = 0,
        groupIndex = 0;
    Group current;
    QImage image;
    bool writing = false;
    qint64 readMs = 0, maxGuiNs = 0;
    QList<RepeatSegment*> repeats;
};
ScoreVideoDocument::ScoreVideoDocument(QObject* parent)
    : QObject(parent), _settings(defaultScoreVideoSettings())
{
}
ScoreVideoDocument::~ScoreVideoDocument() { cancel(); }
bool ScoreVideoDocument::current() const
{
    return _source && _copy && _sourceState == _source->masterScore()->state() &&
           _native.settings.value("staves") == _settings.value("staves") &&
           _native.settings.value("expandRepeats", true) == _settings.value("expandRepeats", true);
}
void ScoreVideoDocument::setScore(Score* score)
{
    if (_source == score && (score || !_copy))
        return;
    cancel();
    _source = score;
    _copy.reset();
    _snapshot = {};
    _native = {};
    _reference = score ? decode(score->masterScore()->metaTag("kumo.reference")) : QVariantMap();
    _stored = stored(score);
    _settings = defaultScoreVideoSettings();
    if (score && score->nstaves()) {
        QVariantList staves;
        const auto part = score->staff(0)->part();
        for (int index = 0; index < score->nstaves(); ++index)
            if (score->staff(index)->part() == part)
                staves.append(index);
        _settings["staves"] = staves;
        const auto saved = decode(_stored);
        for (auto it = saved.cbegin(); it != saved.cend(); ++it)
            _settings[it.key()] = it.value();
    }
    _status =
        score ? tr("选择谱表，点“准备画面”即可预览动态乐谱。") : tr("打开乐谱后可以制作动态乐谱。");
    emit changed();
}
void ScoreVideoDocument::cancel()
{
    ++_generation;
    _cancelled = true;
    _build.reset();
    if (_busy) {
        _busy = false;
        _status = tr("画面准备已取消，上次画面继续保留。");
        emit changed();
    }
}
void ScoreVideoDocument::prepare(const QVariantMap& fields)
{
    cancel();
    if (!_source) {
        _status = tr("先打开一份乐谱。");
        emit changed();
        return;
    }
    auto settings = _settings;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        settings[it.key()] = it.value();
    const auto error = validation(settings, _source);
    if (!error.isEmpty()) {
        _status = error;
        emit changed();
        return;
    }
    _settings = settings;
    _busy = true;
    _cancelled = false;
    _status = tr("正在读取版面与演奏时间…");
    emit changed();
    const auto generation = _generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != _generation || !_source)
            return;
        QElapsedTimer clock;
        clock.start();
        _build = std::make_unique<Build>();
        auto& build = *_build;
        build.sourceState = _source->masterScore()->state();
        build.score.reset(_source->isMaster() ? _source->masterScore()->clone()
                                              : static_cast<MasterScore*>(_source->clone()));
        if (!build.score) {
            _busy = false;
            _status = tr("这份乐谱的画面暂未准备好，请再试一次。");
            emit changed();
            return;
        }
        auto copy = build.score.get();
        copy->setExpandRepeats(_settings.value("expandRepeats", true).toBool());
        copy->tempomap()->setRelTempo(_source->tempomap()->relTempo());
        copy->updateVelo();
        for (auto value : _settings.value("staves").toList())
            build.staves.insert(value.toInt());
        auto cache =
            QSettings()
                .value("spectrum/cacheDirectory",
                       QDir("E:/").exists() ? QString("E:/MuseScoreCache") : QDir::tempPath())
                .toString() +
            "/score-video";
        QDir().mkpath(cache);
        build.snapshot.assets = std::make_shared<QTemporaryDir>(cache + "/session-XXXXXX");
        if (!build.snapshot.assets->isValid()) {
            _busy = false;
            _status = tr("无法建立视频画面目录，请检查缓存位置和空间。");
            _build.reset();
            emit changed();
            return;
        }
        build.snapshot.title = _source->name();
        build.snapshot.settings = _settings;
        build.systems = copy->systems();
        EventMap events;
        if (mscore)
            copy->renderMidi(&events, mscore->synthesizerState());
        else
            copy->renderMidi(&events, copy->synthesizerState());
        const int offset = PlaybackTiming::exportOffset(events);
        build.snapshot.preRoll = -PlaybackTiming::time(copy, -offset);
        for (auto repeat : copy->repeatList())
            build.repeats.append(repeat);
        build.snapshot.duration =
            PlaybackTiming::time(copy, copy->repeatList().ticks()) + build.snapshot.preRoll;
        struct Active {
            const Note* note;
            int index;
        };
        QHash<QString, QVector<Active>> active;
        for (const auto& pair : events) {
            const auto& event = pair.second;
            if (event.type() != ME_NOTEON && event.type() != ME_NOTEOFF)
                continue;
            const auto note =
                rootNote(event.noteEventOwner() ? event.noteEventOwner() : event.note());
            if (!note)
                continue;
            const auto key = QString::number(quintptr(note), 16) + ":" +
                             QString::number(event.channel()) + ":" +
                             QString::number(event.pitch());
            if (event.type() == ME_NOTEON && event.velo() > 0) {
                auto& times = build.times[note];
                const int index = times.size();
                times.append({pair.first, copy->repeatList().ticks()});
                active[key].append({note, index});
            } else if (active.contains(key) && !active[key].isEmpty()) {
                const auto value = active[key].takeFirst();
                build.times[value.note][value.index].off = pair.first;
                build.snapshot.duration =
                    qMax(build.snapshot.duration,
                         PlaybackTiming::time(copy, pair.first) + build.snapshot.preRoll);
            }
        }
        build.readMs = clock.elapsed();
        _reference = decode(_source->masterScore()->metaTag("kumo.reference"));
        _status = tr("正在准备谱行画面…");
        emit changed();
        buildNext();
    });
}
void ScoreVideoDocument::buildNext()
{
    if (!_build || _cancelled)
        return;
    auto& build = *_build;
    auto score = build.score.get();
    QElapsedTimer budget;
    budget.start();
    const auto generation = _generation;
    while (budget.nsecsElapsed() < 2000000) {
        if (build.phase == 0) {
            if (build.systemIndex >= build.systems.size()) {
                build.phase = 1;
                build.pictureIndex = 0;
                build.elementIndex = 0;
                continue;
            }
            auto system = build.systems[build.systemIndex];
            if (!system->page() || !system->firstMeasure() || !system->lastMeasure()) {
                ++build.systemIndex;
                continue;
            }
            if (build.elementIndex == 0 && build.current.system != system) {
                build.current = {};
                build.current.system = system;
                double top = 1e20, bottom = -1e20;
                int first = score->nstaves();
                for (int staff : build.staves)
                    if (system->staff(staff)->show()) {
                        const auto box =
                            system->staff(staff)->bbox().translated(system->canvasPos());
                        top = qMin(top, box.top());
                        bottom = qMax(bottom, box.bottom());
                        first = qMin(first, staff);
                    }
                if (top > bottom) {
                    ++build.systemIndex;
                    continue;
                }
                build.current.crop =
                    QRectF(system->canvasPos().x(), top, system->width(), bottom - top);
                build.current.staffOrigin =
                    system->staff(first)->bbox().topLeft() + system->canvasPos();
                if (!build.pageElements.contains(system->page())) {
                    auto elements = system->page()->elements();
                    std::stable_sort(elements.begin(), elements.end(), elementLessThan);
                    build.pageElements[system->page()] = elements;
                }
            }
            const auto& elements = build.pageElements[system->page()];
            if (build.elementIndex < elements.size()) {
                auto element = elements[build.elementIndex++];
                if (!element->visible() || element->isPage() || element->isSystem() ||
                    element->isMeasure() || element->isBox() || systemFor(element) != system)
                    continue;
                const int staff = inkStaff(element);
                if (staff >= 0 && !build.staves.contains(staff))
                    continue;
                if (element->isBracket()) {
                    const auto bracket = toBracket(element);
                    bool complete = true;
                    for (int i = bracket->firstStaff(); i <= bracket->lastStaff(); ++i)
                        complete = complete && build.staves.contains(i);
                    if (!complete)
                        continue;
                }
                auto box = element->bbox().translated(element->canvasPos());
                if (element->isBarLine() && staff >= 0) {
                    int last = staff;
                    while (last + 1 < score->nstaves() && build.staves.contains(last + 1))
                        ++last;
                    if (last + 1 < score->nstaves()) {
                        const double bottom = system->staff(last)->bbox().bottom() +
                                              system->canvasPos().y() + score->spatium() * .08;
                        if (box.bottom() > bottom) {
                            box.setBottom(bottom);
                            build.current.clips[element] =
                                box.adjusted(-score->spatium(), 0, score->spatium(), 0);
                        }
                    }
                }
                if (element->isInstrumentName() && staff >= 0) {
                    const auto part = score->staff(staff)->part();
                    double top = 1e20, bottom = -1e20, selectedTop = 1e20, selectedBottom = -1e20;
                    for (int i = 0; i < score->nstaves(); ++i)
                        if (score->staff(i)->part() == part && system->staff(i)->show()) {
                            const auto bounds = system->staff(i)->bbox();
                            top = qMin(top, bounds.top());
                            bottom = qMax(bottom, bounds.bottom());
                            if (build.staves.contains(i)) {
                                selectedTop = qMin(selectedTop, bounds.top());
                                selectedBottom = qMax(selectedBottom, bounds.bottom());
                            }
                        }
                    if (selectedTop <= selectedBottom) {
                        const QPointF offset(0, (selectedTop + selectedBottom - top - bottom) / 2);
                        build.current.offsets[element] = offset;
                        box.translate(offset);
                    }
                }
                if (!box.isEmpty())
                    build.current.crop |= box;
                build.current.elements.append(element);
                continue;
            }
            auto group = build.current;
            group.crop.adjust(-score->spatium() * .5, -score->spatium(), score->spatium() * .5,
                              score->spatium());
            if (group.crop.width() > 0 && group.crop.height() > 0) {
                group.scale =
                    qMin(4., qMin(3000. / group.crop.width(), 2400. / group.crop.height()));
                auto first = system->firstMeasure(), last = system->lastMeasure();
                if (first->isMMRest())
                    first = first->mmRestFirst();
                if (last->isMMRest())
                    last = last->mmRestLast();
                VideoPicture picture;
                picture.firstTick = first->tick().ticks();
                picture.endTick = last->endTick().ticks();
                picture.id =
                    QString("%1:%2:%3")
                        .arg(picture.firstTick)
                        .arg(picture.endTick)
                        .arg(QString::fromUtf8(QJsonDocument::fromVariant(_settings.value("staves"))
                                                   .toJson(QJsonDocument::Compact)));
                picture.label = tr("第%1行 · 第%2—%3小节")
                                    .arg(build.groups.size() + 1)
                                    .arg(first->no() + 1)
                                    .arg(last->no() + 1);
                picture.pixels = QSize(qMax(1, int(std::ceil(group.crop.width() * group.scale))),
                                       qMax(1, int(std::ceil(group.crop.height() * group.scale))));
                picture.sourceRect = group.crop;
                picture.rasterScale = group.scale;
                picture.firstStaffX = (group.staffOrigin.x() - group.crop.x()) * group.scale;
                picture.firstStaffY = (group.staffOrigin.y() - group.crop.y()) * group.scale;
                picture.file = build.snapshot.assets->filePath(
                    QString("picture-%1.png").arg(build.groups.size(), 5, 10, QChar('0')));
                build.groups.append(group);
                build.snapshot.pictures.append(picture);
            }
            build.current = {};
            build.elementIndex = 0;
            ++build.systemIndex;
        } else if (build.phase == 1) {
            if (build.pictureIndex >= build.groups.size()) {
                build.phase = 2;
                build.repeatIndex = 0;
                build.groupIndex = 0;
                continue;
            }
            if (build.writing)
                return;
            auto& group = build.groups[build.pictureIndex];
            const auto& picture = build.snapshot.pictures[build.pictureIndex];
            if (build.image.isNull()) {
                build.image = QImage(picture.pixels, QImage::Format_ARGB32_Premultiplied);
                // TextFragment uses the image device DPI as well as pixelRatio.
                // Match native PNG / foto output so text and music share one scale.
                const int dotsPerMeter = qRound(DPI * group.scale * 1000 / INCH);
                build.image.setDotsPerMeterX(dotsPerMeter);
                build.image.setDotsPerMeterY(dotsPerMeter);
                build.image.fill(Qt::transparent);
                build.elementIndex = 0;
                if (build.image.isNull()) {
                    _busy = false;
                    _status = tr("谱行图像未能建立，请缩小显示范围。");
                    _build.reset();
                    emit changed();
                    return;
                }
            }
            {
                QScopedValueRollback<qreal> pixels(MScore::pixelRatio, 1 / group.scale);
                const bool old = score->printing();
                score->setPrinting(true);
                QPainter painter(&build.image);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setRenderHint(QPainter::TextAntialiasing);
                painter.scale(group.scale, group.scale);
                painter.translate(-group.crop.topLeft());
                while (build.elementIndex < group.elements.size() &&
                       budget.nsecsElapsed() < 2000000) {
                    auto element = group.elements[build.elementIndex++];
                    painter.save();
                    if (group.clips.contains(element))
                        painter.setClipRect(group.clips.value(element), Qt::IntersectClip);
                    painter.translate(element->canvasPos() + group.offsets.value(element));
                    element->draw(&painter);
                    painter.restore();
                }
                painter.end();
                score->setPrinting(old);
            }
            if (build.elementIndex < group.elements.size())
                break;
            build.maxGuiNs = qMax(build.maxGuiNs, budget.nsecsElapsed());
            build.writing = true;
            const auto image = build.image;
            const auto file = picture.file;
            const auto lease = build.snapshot.assets;
            auto watcher = new QFutureWatcher<bool>(this);
            connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, generation] {
                const bool ok = watcher->result();
                watcher->deleteLater();
                if (generation != _generation || !_build)
                    return;
                _build->writing = false;
                if (!ok) {
                    _busy = false;
                    _status = tr("谱行图未能保存，请检查缓存空间。");
                    _build.reset();
                    emit changed();
                    return;
                }
                ++_build->pictureIndex;
                _build->image = {};
                _status =
                    tr("谱行画面 %1 / %2").arg(_build->pictureIndex).arg(_build->groups.size());
                emit changed();
                QTimer::singleShot(0, this, [this] { buildNext(); });
            });
            watcher->setFuture(
                QtConcurrent::run([image, file, lease] { return image.save(file, "PNG"); }));
            return;
        } else {
            if (build.repeatIndex >= build.repeats.size()) {
                if (build.snapshot.cues.isEmpty()) {
                    _busy = false;
                    _status = tr("所选谱表没有可显示的谱行，请选择其它谱表。");
                    _build.reset();
                    emit changed();
                    return;
                }
                std::stable_sort(
                    build.snapshot.cues.begin(), build.snapshot.cues.end(),
                    [](const VideoCue& a, const VideoCue& b) { return a.fromUtick < b.fromUtick; });
                build.snapshot.cues.front().from = build.snapshot.cues.front().automaticFrom = 0;
                build.snapshot.cues.back().until = build.snapshot.cues.back().automaticUntil =
                    build.snapshot.duration;
                _sourceState = build.sourceState;
                _copy = std::move(build.score);
                _native = std::move(build.snapshot);
                const auto read = build.readMs;
                const auto longest = build.maxGuiNs;
                _build.reset();
                _busy = false;
                _status = tr("画面已准备好 · %1组谱表，%2段播放")
                              .arg(_native.pictures.size())
                              .arg(_native.cues.size());
                setProperty("preparationReadMs", read);
                setProperty("maximumGuiBatchMs", longest / 1e6);
                rebuildTimeline();
                emit changed();
                emit prepared();
                return;
            }
            if (build.groupIndex >= build.groups.size()) {
                ++build.repeatIndex;
                build.groupIndex = 0;
                continue;
            }
            const auto repeat = build.repeats[build.repeatIndex];
            const int pictureIndex = build.groupIndex++;
            const auto& picture = build.snapshot.pictures[pictureIndex];
            const auto& group = build.groups[pictureIndex];
            const int from = qMax(picture.firstTick, repeat->tick),
                      until = qMin(picture.endTick, repeat->tick + repeat->len());
            if (until <= from)
                continue;
            VideoCue cue;
            cue.picture = pictureIndex;
            cue.start = from;
            cue.end = until;
            cue.fromUtick = repeat->utick + from - repeat->tick;
            cue.untilUtick = repeat->utick + until - repeat->tick;
            cue.occurrence = repeat->playbackCount;
            cue.clockSource = "score";
            cue.id = picture.id + QString(":%1:%2").arg(cue.fromUtick).arg(cue.untilUtick);
            cue.label = picture.label + tr(" · 第%1遍").arg(cue.occurrence);
            cue.from = cue.automaticFrom =
                PlaybackTiming::time(score, cue.fromUtick) + build.snapshot.preRoll;
            cue.until = cue.automaticUntil =
                PlaybackTiming::time(score, cue.untilUtick) + build.snapshot.preRoll;
            QVector<int> ticks{from, until};
            for (int tick = from + 60; tick < until; tick += 60)
                ticks.append(tick);
            for (auto measure = score->tick2measure(Fraction::fromTicks(from));
                 measure && measure->tick().ticks() < until; measure = measure->nextMeasure())
                for (auto segment = measure->first(SegmentType::ChordRest); segment;
                     segment = segment->next(SegmentType::ChordRest)) {
                    const int tick = segment->tick().ticks();
                    if (tick > from && tick < until)
                        ticks.append(tick);
                }
            std::sort(ticks.begin(), ticks.end());
            ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
            auto point = [&](int tick) {
                auto measure = score->tick2measure(Fraction::fromTicks(qMin(
                    tick == until ? tick - 1 : tick, score->lastMeasure()->endTick().ticks() - 1)));
                const int utick = cue.fromUtick + tick - from;
                const double time = PlaybackTiming::time(score, utick) + build.snapshot.preRoll;
                return VideoClockPoint{
                    time, time,
                    measure ? (tickX(measure, tick) - group.crop.x()) * group.scale : 0., tick};
            };
            for (int tick : ticks) {
                if (tick > from) {
                    const auto before = point(tick - 1), now = point(tick);
                    if (now.seconds - before.seconds > .02)
                        cue.clock.append(before);
                }
                cue.clock.append(point(tick));
            }
            QSet<const Note*> seen;
            for (auto element : group.elements)
                if (element->isNote()) {
                    const auto note = toNote(element);
                    if (seen.contains(note))
                        continue;
                    seen.insert(note);
                    const auto root = rootNote(note);
                    for (const auto& time : build.times.value(root))
                        if (time.on < cue.untilUtick &&
                            (time.off > cue.fromUtick || (cue.fromUtick == 0 && time.off <= 0))) {
                            auto box =
                                note->bbox().translated(note->canvasPos() - group.crop.topLeft());
                            box = QRectF(box.topLeft() * group.scale, box.size() * group.scale);
                            cue.notes.append(
                                {box, PlaybackTiming::time(score, time.on) + build.snapshot.preRoll,
                                 PlaybackTiming::time(score, time.off) + build.snapshot.preRoll});
                        }
                }
            build.snapshot.cues.append(cue);
        }
    }
    if (_build)
        _build->maxGuiNs = qMax(_build->maxGuiNs, budget.nsecsElapsed());
    QTimer::singleShot(0, this, [this] { buildNext(); });
}
void ScoreVideoDocument::saveSettings()
{
    if (!_source || _source->masterScore()->readOnly())
        return;
    const auto next =
        QString::fromUtf8(QJsonDocument::fromVariant(_settings).toJson(QJsonDocument::Compact));
    if (stored(_source) == next)
        return;
    auto master = _source->masterScore();
    master->startCmd();
    master->undo(new ChangeVideoSettings(master, next));
    master->endCmd();
    _stored = next;
    if (mscore && mscore->currentScore() && mscore->currentScore()->masterScore() == master)
        mscore->endCmd();
}
void ScoreVideoDocument::restoreStoredSettings()
{
    if (!_source || stored(_source) == _stored)
        return;
    _stored = stored(_source);
    const auto saved = decode(_stored);
    const auto staffDefault = _settings.value("staves");
    _settings = defaultScoreVideoSettings();
    _settings["staves"] = staffDefault;
    for (auto it = saved.cbegin(); it != saved.cend(); ++it)
        _settings[it.key()] = it.value();
    _reference = decode(_source->masterScore()->metaTag("kumo.reference"));
    rebuildTimeline();
    emit changed();
}
bool ScoreVideoDocument::changeSettings(const QVariantMap& fields)
{
    if (!_source || _busy)
        return false;
    auto next = _settings;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        next[it.key()] = it.value();
    const auto error = validation(next, _source);
    if (!error.isEmpty()) {
        _status = error;
        emit changed();
        return false;
    }
    if (next == _settings)
        return true;
    _settings = next;
    saveSettings();
    rebuildTimeline();
    if (!current())
        _status = tr("显示范围已改变，点“准备画面”更新。");
    emit changed();
    return true;
}
bool ScoreVideoDocument::moveBoundary(int index, double seconds)
{
    if (_busy || index <= 0 || index >= _snapshot.cues.size() || !std::isfinite(seconds))
        return false;
    const double minimum = 1. / qMax(1, _settings.value("fps", 30).toInt());
    const double previous = _snapshot.cues[index - 1].from,
                 next = index + 1 < _snapshot.cues.size() ? _snapshot.cues[index + 1].from
                                                          : _snapshot.duration;
    if (seconds <= previous + minimum || seconds >= next - minimum) {
        _status = tr("换行时间应位于相邻两次换行之间。");
        emit changed();
        return false;
    }
    auto all = _settings.value("timingOverrides").toMap(),
         values = all.value(sourceKey(_snapshot)).toMap();
    values[_snapshot.cues[index].id] = seconds;
    all[sourceKey(_snapshot)] = values;
    return changeSettings({{"timingOverrides", all}});
}
bool ScoreVideoDocument::movePicture(int picture, double x, double y)
{
    if (picture < 0 || picture >= _snapshot.pictures.size() || !std::isfinite(x) ||
        !std::isfinite(y))
        return false;
    auto values = _settings.value("pictureOffsets").toMap();
    values[_snapshot.pictures[picture].id] =
        QVariantMap{{"x", qBound(-100., x, 100.)}, {"y", qBound(-100., y, 100.)}};
    return changeSettings({{"pictureOffsets", values}});
}
void ScoreVideoDocument::restoreTiming()
{
    auto values = _settings.value("timingOverrides").toMap();
    values.remove(sourceKey(_snapshot));
    changeSettings({{"timingOverrides", values}});
}
void ScoreVideoDocument::restorePicture(int picture)
{
    if (picture < 0 || picture >= _snapshot.pictures.size())
        return;
    auto values = _settings.value("pictureOffsets").toMap();
    values.remove(_snapshot.pictures[picture].id);
    changeSettings({{"pictureOffsets", values}});
}
QVariantMap ScoreVideoDocument::state() const
{
    QVariantList pictures, cues, staves;
    for (const auto& p : _snapshot.pictures)
        pictures.append(QVariantMap{{"id", p.id},
                                    {"label", p.label},
                                    {"file", p.file},
                                    {"pixels", p.pixels},
                                    {"firstStaffX", p.firstStaffX},
                                    {"firstStaffY", p.firstStaffY}});
    for (const auto& c : _snapshot.cues)
        cues.append(QVariantMap{{"id", c.id},
                                {"label", c.label},
                                {"picture", c.picture},
                                {"from", c.from},
                                {"until", c.until},
                                {"start", c.start},
                                {"end", c.end},
                                {"fromUtick", c.fromUtick},
                                {"untilUtick", c.untilUtick},
                                {"occurrence", c.occurrence},
                                {"clockSource", c.clockSource},
                                {"notes", c.notes.size()}});
    if (_source)
        for (int i = 0; i < _source->nstaves(); ++i)
            staves.append(QVariantMap{
                {"index", i},
                {"name", tr("%1 · 谱表%2").arg(_source->staff(i)->part()->partName()).arg(i + 1)}});
    return {{"busy", _busy},
            {"ready", ready()},
            {"current", current()},
            {"status", _status},
            {"source", _source ? _source->name() : QString()},
            {"settings", _settings},
            {"pictures", pictures},
            {"cues", cues},
            {"duration", _snapshot.duration},
            {"preRoll", _snapshot.preRoll},
            {"staves", staves},
            {"audioSources", audioSources()},
            {"audioPieces", _snapshot.audioPieces},
            {"readMilliseconds", property("preparationReadMs")},
            {"maximumGuiBatchMs", property("maximumGuiBatchMs")}};
}
} // namespace Ms
