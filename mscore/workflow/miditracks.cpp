// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "miditracks.h"
#include "midiinput.h"
#include "workflowcontext.h"
#include "mscore/musescore.h"
#include "libmscore/score.h"
#include "libmscore/undo.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <zlib.h>
#include <algorithm>
#include <cmath>

namespace Ms
{
namespace
{
constexpr int maximumDocument = 64 * 1024 * 1024;
class ChangeMidiTrackData final : public UndoCommand
{
    MasterScore* _score;
    QString _value;
    void flip(EditData*) override
    {
        const auto previous = _score->metaTag("kumo.midiTracks");
        _score->setMetaTag("kumo.midiTracks", _value);
        _value = previous;
        if (auto context = workflowContextIfCreated()) {
            const QPointer<Score> score = _score;
            QMetaObject::invokeMethod(
                context,
                [context, score] {
                    if (score)
                        emit context->midiTracksChanged(score);
                },
                Qt::QueuedConnection);
        }
    }

  public:
    ChangeMidiTrackData(MasterScore* score, const QString& value) : _score(score), _value(value) {}
    bool affectsScoreContent() const override { return false; }
    UNDO_NAME("ChangeMidiTrackData")
};
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QVariantMap emptyDocument()
{
    return {{"schema", 1},
            {"tracks", QVariantList()},
            {"deleted", QVariantList()},
            {"assets", QVariantMap()}};
}
QVariantMap error(const QString& text) { return {{"error", text}}; }
QVariantMap emptyTrack(const QString& name)
{
    return {{"id", freshId()},
            {"name", name},
            {"visible", true},
            {"mute", true},
            {"solo", false},
            {"volume", 1.},
            {"audioId", QString()},
            {"offsetMs", 0.},
            {"anchors", QVariantList()},
            {"source", QVariantMap{{"kind", "sf2"}}},
            {"notes", QVariantList()},
            {"events", QVariantList()}};
}
bool validNotes(const QVariantList& notes)
{
    QSet<QString> ids;
    for (const auto& value : notes) {
        const auto n = value.toMap();
        const auto id = n.value("id").toString();
        const double start = n.value("start").toDouble(), end = n.value("end").toDouble();
        if (id.isEmpty() || ids.contains(id) || !std::isfinite(start) || !std::isfinite(end) ||
            start < 0 || end < start || end > 86400 || n.value("pitch", -1).toInt() < 0 ||
            n.value("pitch").toInt() > 127 || n.value("velocity", 0).toInt() < 1 ||
            n.value("velocity").toInt() > 127 || n.value("channel", 0).toInt() < 0 ||
            n.value("channel").toInt() > 15)
            return false;
        ids.insert(id);
    }
    return true;
}
bool validAnchors(const QVariantList& points)
{
    double previousSource = -1, previousTarget = -1;
    for (const auto& value : points) {
        const auto p = value.toMap();
        const double a = p.value("sourceMs", -1).toDouble(), b = p.value("targetMs", -1).toDouble();
        if (!std::isfinite(a) || !std::isfinite(b) || a <= previousSource || b <= previousTarget ||
            a < 0 || b < 0)
            return false;
        previousSource = a;
        previousTarget = b;
    }
    return true;
}
void number(QByteArray& data, quint32 value, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        data.append(char(value >> (i * 8)));
}
void variable(QByteArray& data, quint32 value)
{
    char buffer[4];
    int used = 0;
    buffer[used++] = char(value & 127);
    while ((value >>= 7) != 0)
        buffer[used++] = char((value & 127) | 128);
    while (used)
        data.append(buffer[--used]);
}
} // namespace

QVariantMap prepareMidiTracks(const QByteArray& bytes, const QString& name,
                              const std::atomic_bool& canceled)
{
    const auto parsed = readPerformanceMidiData(bytes, canceled);
    if (parsed.contains("error"))
        return parsed;
    const auto assetId = parsed.value("sha256").toString();
    QVariantList tracks;
    QHash<int, QVariantList> notes, events, channelEvents;
    QHash<int, QSet<int>> usedChannels;
    QVariantList systemEvents;
    for (const auto& value : parsed.value("notes").toList()) {
        if (canceled)
            return error(QObject::tr("已取消导入。"));
        const auto n = value.toMap();
        usedChannels[n.value("track").toInt()].insert(n.value("port").toInt() * 16 +
                                                      n.value("channel").toInt());
        notes[n.value("track").toInt()].append(QVariantMap{{"id", n.value("id")},
                                                           {"pitch", n.value("pitch")},
                                                           {"velocity", n.value("velocity")},
                                                           {"channel", n.value("channel")},
                                                           {"port", n.value("port")},
                                                           {"start", n.value("seconds")},
                                                           {"end", n.value("endSeconds")},
                                                           {"unreleased", n.value("unreleased")},
                                                           {"ambiguous", n.value("ambiguous")}});
    }
    for (const auto& value : parsed.value("events").toList()) {
        const auto e = value.toMap();
        const int kind = e.value("type").toInt() & 240;
        if (kind == 128 || kind == 144)
            continue;
        const int type = e.value("type").toInt();
        if (type >= 128 && type < 240)
            channelEvents[e.value("port").toInt() * 16 + e.value("channel").toInt()].append(e);
        else if (type == 240 || type == 247)
            systemEvents.append(e);
        else
            events[e.value("track").toInt()].append(e);
    }
    for (const auto& value : parsed.value("tracks").toList()) {
        const auto info = value.toMap();
        const int index = info.value("index").toInt();
        if (notes.value(index).isEmpty())
            continue;
        const QString label = info.value("name").toString().trimmed();
        auto track = emptyTrack(
            label.isEmpty() ? QObject::tr("%1 · 轨道%2").arg(name).arg(index + 1) : label);
        track.insert("asset", assetId);
        track.insert("originalTrack", index);
        track.insert("notes", notes.value(index));
        auto controls = events.value(index);
        controls.append(systemEvents);
        for (int channel : usedChannels.value(index))
            controls.append(channelEvents.value(channel));
        std::stable_sort(controls.begin(), controls.end(),
                         [](const QVariant& a, const QVariant& b) {
                             return a.toMap().value("id").toInt() < b.toMap().value("id").toInt();
                         });
        track.insert("events", controls);
        tracks.append(track);
    }
    if (tracks.isEmpty())
        return error(QObject::tr("这份MIDI没有可显示的音符。"));
    QVariantMap baseline;
    for (const auto& value : tracks) {
        const auto t = value.toMap();
        baseline.insert(QString::number(t.value("originalTrack").toInt()),
                        QVariantMap{{"notes", t.value("notes")}, {"events", t.value("events")}});
    }
    // Track-zero tempo and controller data remain available even if that track
    // has no notes. The untouched original is saved with the document as well.
    return {{"tracks", tracks},
            {"assetId", assetId},
            {"asset", QVariantMap{{"name", name},
                                  {"data", QString::fromLatin1(bytes.toBase64())},
                                  {"division", parsed.value("division")},
                                  {"format", parsed.value("format")},
                                  {"_baseline", baseline}}}};
}

QVariantMap decodeMidiTracks(const QString& text, const QVariantMap& knownAssets)
{
    if (text.isEmpty())
        return emptyDocument();
    const auto packed = QByteArray::fromBase64(text.toLatin1());
    if (packed.size() < 4)
        return error(QObject::tr("MIDI轨道资料不完整。"));
    const quint32 size = (quint32(uchar(packed[0])) << 24) | (quint32(uchar(packed[1])) << 16) |
                         (quint32(uchar(packed[2])) << 8) | uchar(packed[3]);
    if (size > quint32(maximumDocument))
        return error(QObject::tr("这份MIDI轨道资料超过64MiB。"));
    QByteArray bytes(int(size), Qt::Uninitialized);
    uLongf actual = size;
    if (uncompress(reinterpret_cast<Bytef*>(bytes.data()), &actual,
                   reinterpret_cast<const Bytef*>(packed.constData() + 4),
                   uLong(packed.size() - 4)) != Z_OK ||
        actual != size)
        return error(QObject::tr("MIDI轨道资料不完整。"));
    QJsonParseError parse;
    auto result = QJsonDocument::fromJson(bytes, &parse).toVariant().toMap();
    if (parse.error != QJsonParseError::NoError || result.value("schema").toInt() != 1)
        return error(QObject::tr("无法读取MIDI轨道资料。"));
    if (!result.value("storage").toString().isEmpty() &&
        result.value("storage").toString() != "delta1")
        return error(QObject::tr("这份MIDI轨道使用了更新的保存格式，请更新程序后打开。"));
    auto assets = result.value("assets").toMap();
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        auto asset = it.value().toMap();
        const auto original = QByteArray::fromBase64(asset.value("data").toString().toLatin1());
        if (QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex() !=
            it.key().toLatin1())
            return error(QObject::tr("保存的原始MIDI与文件身份不一致。"));
        const auto known = knownAssets.value(it.key()).toMap();
        if (known.value("data") == asset.value("data") && known.contains("_baseline"))
            asset.insert("_baseline", known.value("_baseline"));
        else {
            const std::atomic_bool canceled{false};
            const auto prepared =
                prepareMidiTracks(original, asset.value("name").toString(), canceled);
            if (prepared.contains("error"))
                return prepared;
            asset.insert("_baseline", prepared.value("asset").toMap().value("_baseline"));
        }
        it.value() = asset;
    }
    result.insert("assets", assets);
    if (result.value("storage").toString() == "delta1") {
        for (const auto& collection : {QString("tracks"), QString("deleted")}) {
            auto list = result.value(collection).toList();
            for (auto& value : list) {
                auto track = value.toMap();
                if (track.value("asset").toString().isEmpty())
                    continue;
                const auto base = assets.value(track.value("asset").toString())
                                      .toMap()
                                      .value("_baseline")
                                      .toMap()
                                      .value(QString::number(track.value("originalTrack").toInt()))
                                      .toMap();
                if (base.isEmpty())
                    return error(QObject::tr("找不到这条轨道的原始MIDI内容。"));
                const auto overrides = track.value("overrides").toMap();
                QSet<QString> removed;
                for (const auto& id : track.value("removed").toList())
                    removed.insert(id.toString());
                QVariantList notes;
                for (const auto& n : base.value("notes").toList()) {
                    const auto id = n.toMap().value("id").toString();
                    if (!removed.contains(id))
                        notes.append(overrides.value(id, n));
                }
                notes.append(track.value("added").toList());
                track.insert("notes", notes);
                if (track.contains("noteOrder")) {
                    QHash<QString, QVariant> byId;
                    for (const auto& n : notes)
                        byId.insert(n.toMap().value("id").toString(), n);
                    QVariantList ordered;
                    for (const auto& id : track.value("noteOrder").toList()) {
                        const auto key = id.toString();
                        if (!byId.contains(key))
                            return error(QObject::tr("MIDI音符顺序资料不完整。"));
                        ordered.append(byId.take(key));
                    }
                    if (!byId.isEmpty())
                        return error(QObject::tr("MIDI音符顺序资料不完整。"));
                    track.insert("notes", ordered);
                }
                track.insert("events", track.contains("eventOverrides")
                                           ? track.value("eventOverrides")
                                           : base.value("events"));
                for (const auto& field :
                     {"overrides", "removed", "added", "eventOverrides", "noteOrder"})
                    track.remove(field);
                value = track;
            }
            result.insert(collection, list);
        }
        result.remove("storage");
    }
    QSet<QString> ids;
    for (const auto& collection :
         {result.value("tracks").toList(), result.value("deleted").toList()})
        for (const auto& value : collection) {
            const auto t = value.toMap();
            const auto id = t.value("id").toString();
            const double offset = t.value("offsetMs").toDouble();
            if (id.isEmpty() || ids.contains(id) || !std::isfinite(offset) ||
                qAbs(offset) > 86400000 || !validNotes(t.value("notes").toList()) ||
                !validAnchors(t.value("anchors").toList()))
                return error(QObject::tr("MIDI轨道包含无效音符或对齐点。"));
            ids.insert(id);
        }
    return result;
}
QString encodeMidiTracks(const QVariantMap& value, QString* message)
{
    auto stored = value;
    const auto assets = value.value("assets").toMap();
    for (const auto& collection : {QString("tracks"), QString("deleted")}) {
        auto list = value.value(collection).toList();
        for (auto& item : list) {
            auto track = item.toMap();
            if (track.value("asset").toString().isEmpty())
                continue;
            const auto base = assets.value(track.value("asset").toString())
                                  .toMap()
                                  .value("_baseline")
                                  .toMap()
                                  .value(QString::number(track.value("originalTrack").toInt()))
                                  .toMap();
            if (base.isEmpty()) {
                if (message)
                    *message = QObject::tr("找不到这条轨道的原始MIDI内容。");
                return {};
            }
            QHash<QString, QVariant> originals;
            for (const auto& n : base.value("notes").toList())
                originals.insert(n.toMap().value("id").toString(), n);
            QVariantMap overrides;
            QVariantList added, removed, currentOrder;
            for (const auto& n : track.value("notes").toList()) {
                const auto id = n.toMap().value("id").toString();
                const auto original = originals.find(id);
                currentOrder.append(id);
                if (original == originals.end())
                    added.append(n);
                else {
                    if (original.value() != n)
                        overrides.insert(id, n);
                    originals.erase(original);
                }
            }
            auto removedKeys = originals.keys();
            std::sort(removedKeys.begin(), removedKeys.end());
            for (const auto& key : removedKeys)
                removed.append(key);
            QVariantList ordinaryOrder;
            for (const auto& n : base.value("notes").toList()) {
                const auto id = n.toMap().value("id").toString();
                if (!originals.contains(id))
                    ordinaryOrder.append(id);
            }
            for (const auto& n : added)
                ordinaryOrder.append(n.toMap().value("id"));
            if (currentOrder != ordinaryOrder)
                track.insert("noteOrder", currentOrder);
            track.insert("overrides", overrides);
            track.insert("added", added);
            track.insert("removed", removed);
            if (track.value("events") != base.value("events"))
                track.insert("eventOverrides", track.value("events"));
            track.remove("notes");
            track.remove("events");
            item = track;
        }
        stored.insert(collection, list);
    }
    QVariantMap savedAssets = assets;
    for (auto it = savedAssets.begin(); it != savedAssets.end(); ++it) {
        auto asset = it.value().toMap();
        asset.remove("_baseline");
        it.value() = asset;
    }
    stored.insert("assets", savedAssets);
    stored.insert("storage", "delta1");
    const auto bytes = QJsonDocument::fromVariant(stored).toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumDocument) {
        if (message)
            *message = QObject::tr("轨道资料超过64MiB，请分开导入较短片段。");
        return {};
    }
    return QString::fromLatin1(qCompress(bytes, 6).toBase64());
}
double midiTrackTime(const QVariantMap& track, double value, bool inverse)
{
    double ms = value * 1000;
    const double offset = track.value("offsetMs").toDouble();
    if (inverse)
        ms -= offset;
    const auto points = track.value("anchors").toList();
    auto x = [inverse](const QVariant& p) {
        return p.toMap().value(inverse ? "targetMs" : "sourceMs").toDouble();
    };
    auto y = [inverse](const QVariant& p) {
        return p.toMap().value(inverse ? "sourceMs" : "targetMs").toDouble();
    };
    if (!points.isEmpty()) {
        auto next = std::upper_bound(points.cbegin(), points.cend(), ms,
                                     [&](double t, const QVariant& p) { return t < x(p); });
        if (next == points.cbegin())
            ms += y(points.first()) - x(points.first());
        else if (next == points.cend())
            ms += y(points.last()) - x(points.last());
        else {
            const auto a = *(next - 1), b = *next;
            ms = y(a) + (ms - x(a)) * (y(b) - y(a)) / (x(b) - x(a));
        }
    }
    return (ms + (inverse ? 0 : offset)) / 1000;
}

void MidiTrackDocument::setScore(Score* value)
{
    auto master = value ? value->masterScore() : nullptr;
    if (_score == master) {
        reload();
        return;
    }
    _score = master;
    _serialized.clear();
    _data.clear();
    _knownAssets.clear();
    _error.clear();
    _revision.clear();
    reload();
}
MasterScore* MidiTrackDocument::score() const { return _score.data(); }
bool MidiTrackDocument::reload()
{
    const auto text = _score ? _score->metaTag("kumo.midiTracks") : QString();
    if (text == _serialized && !_data.isEmpty())
        return false;
    const auto cache = _score ? _score->property("_kumoMidiTracksCache").toMap() : QVariantMap();
    if (_knownAssets.isEmpty())
        _knownAssets = cache.value("data").toMap().value("assets").toMap();
    const auto loaded = cache.value("serialized").toString() == text && cache.contains("data")
                            ? cache.value("data").toMap()
                            : decodeMidiTracks(text, _knownAssets);
    _serialized = text;
    _error = loaded.value("error").toString();
    _revision = QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
    _data = loaded;
    const auto assets = _data.value("assets").toMap();
    for (auto it = assets.cbegin(); it != assets.cend(); ++it)
        _knownAssets.insert(it.key(), it.value());
    if (_score && _error.isEmpty())
        _score->QObject::setProperty("_kumoMidiTracksCache",
                                     QVariantMap{{"serialized", text}, {"data", _data}});
    return true;
}
int MidiTrackDocument::indexOf(const QString& id) const
{
    const auto list = tracks();
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toMap().value("id").toString() == id)
            return i;
    return -1;
}
QVariantMap MidiTrackDocument::track(const QString& id) const
{
    const int i = indexOf(id);
    return i < 0 ? QVariantMap() : tracks()[i].toMap();
}
bool MidiTrackDocument::write(const QVariantMap& value)
{
    if (!_score || _score->readOnly()) {
        _error = QObject::tr("请打开可编辑的乐谱后保存MIDI轨道。");
        return false;
    }
    if (!_error.isEmpty() && _data.contains("error"))
        return false;
    QString message;
    const auto text = encodeMidiTracks(value, &message);
    if (text.isEmpty()) {
        _error = message;
        return false;
    }
    if (_score->metaTag("kumo.midiTracks") == text)
        return true;
    _score->startCmd();
    _score->undo(new ChangeMidiTrackData(_score, text));
    _score->endCmd();
    _data = value;
    _serialized = text;
    _error.clear();
    _revision = QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
    _score->QObject::setProperty("_kumoMidiTracksCache",
                                 QVariantMap{{"serialized", text}, {"data", value}});
    const auto assets = value.value("assets").toMap();
    for (auto it = assets.cbegin(); it != assets.cend(); ++it)
        _knownAssets.insert(it.key(), it.value());
    if (mscore && mscore->currentScore() && mscore->currentScore()->masterScore() == _score)
        mscore->endCmd();
    return true;
}
bool MidiTrackDocument::addPrepared(const QVariantMap& prepared)
{
    reload();
    if (prepared.contains("error")) {
        _error = prepared.value("error").toString();
        return false;
    }
    auto value = _data;
    auto list = tracks();
    for (auto t : prepared.value("tracks").toList())
        list.append(t);
    auto assets = value.value("assets").toMap();
    assets.insert(prepared.value("assetId").toString(), prepared.value("asset"));
    value.insert("tracks", list);
    value.insert("assets", assets);
    return write(value);
}
bool MidiTrackDocument::appendRecording(const QString& id, const QVariantMap& prepared,
                                        double start)
{
    reload();
    const int i = indexOf(id);
    if (i < 0 || prepared.contains("error") || !std::isfinite(start) || start < 0)
        return false;
    auto value = _data;
    auto list = tracks();
    auto row = list[i].toMap();
    auto notes = row.value("notes").toList(), events = row.value("events").toList();
    const QString prefix = freshId() + ":";
    for (const auto& source : prepared.value("tracks").toList()) {
        for (const auto& note : source.toMap().value("notes").toList()) {
            auto n = note.toMap();
            n.insert("id", prefix + n.value("id").toString());
            n.insert("start", n.value("start").toDouble() + start);
            n.insert("end", n.value("end").toDouble() + start);
            notes.append(n);
        }
        for (const auto& control : source.toMap().value("events").toList()) {
            auto e = control.toMap();
            e.insert("seconds", e.value("seconds").toDouble() + start);
            events.append(e);
        }
    }
    if (!validNotes(notes))
        return false;
    auto assets = value.value("assets").toMap();
    assets.insert(prepared.value("assetId").toString(), prepared.value("asset"));
    auto recordings = row.value("recordings").toList();
    recordings.append(QVariantMap{{"asset", prepared.value("assetId")}, {"start", start}});
    row.insert("notes", notes);
    row.insert("events", events);
    row.insert("recordings", recordings);
    list[i] = row;
    value.insert("tracks", list);
    value.insert("assets", assets);
    return write(value);
}
bool MidiTrackDocument::setScoreOptions(const QVariantMap& fields)
{
    reload();
    auto value = _data;
    auto options = value.value("scoreOptions").toMap();
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (it.key() != "visible" && it.key() != "mute" && it.key() != "solo")
            return false;
        options.insert(it.key(), it.value().toBool());
    }
    value.insert("scoreOptions", options);
    return write(value);
}
QString MidiTrackDocument::create(const QString& name)
{
    reload();
    auto value = _data;
    auto list = tracks();
    auto row = emptyTrack(name.trimmed().isEmpty() ? QObject::tr("临时MIDI") : name.trimmed());
    row["mute"] = false;
    list.append(row);
    value.insert("tracks", list);
    return write(value) ? row.value("id").toString() : QString();
}
QString MidiTrackDocument::duplicate(const QString& id)
{
    reload();
    auto row = track(id);
    if (row.isEmpty())
        return {};
    row.insert("id", freshId());
    row.insert("name", QObject::tr("%1 副本").arg(row.value("name").toString()));
    auto value = _data;
    auto list = tracks();
    list.insert(indexOf(id) + 1, row);
    value.insert("tracks", list);
    return write(value) ? row.value("id").toString() : QString();
}
bool MidiTrackDocument::remove(const QString& id)
{
    reload();
    const int i = indexOf(id);
    if (i < 0)
        return false;
    auto value = _data;
    auto list = tracks(), deleted = value.value("deleted").toList();
    deleted.append(list.takeAt(i));
    value.insert("tracks", list);
    value.insert("deleted", deleted);
    return write(value);
}
bool MidiTrackDocument::restore(const QString& id)
{
    reload();
    auto value = _data;
    auto deleted = value.value("deleted").toList();
    int i = deleted.size() - 1;
    if (!id.isEmpty())
        for (i = deleted.size() - 1; i >= 0; --i)
            if (deleted[i].toMap().value("id").toString() == id)
                break;
    if (i < 0)
        return false;
    auto list = tracks();
    list.append(deleted.takeAt(i));
    value.insert("tracks", list);
    value.insert("deleted", deleted);
    return write(value);
}
bool MidiTrackDocument::update(const QString& id, const QVariantMap& fields)
{
    reload();
    const int i = indexOf(id);
    if (i < 0)
        return false;
    auto value = _data;
    auto list = tracks();
    auto row = list[i].toMap();
    for (const auto& key : fields.keys()) {
        if (key == "name") {
            const auto name = fields.value(key).toString().trimmed();
            if (name.isEmpty())
                return false;
            row.insert(key, name);
        } else if (key == "visible" || key == "mute" || key == "solo" || key == "armed")
            row.insert(key, fields.value(key).toBool());
        else if (key == "volume") {
            const double level = fields.value(key).toDouble();
            if (!std::isfinite(level) || level < 0 || level > 2)
                return false;
            row.insert(key, level);
        } else if (key == "source")
            row.insert(key, fields.value(key).toMap());
        else
            return false;
    }
    list[i] = row;
    value.insert("tracks", list);
    return write(value);
}
bool MidiTrackDocument::replaceNotes(const QString& id, const QVariantList& notes)
{
    reload();
    const int i = indexOf(id);
    if (i < 0) {
        _error = QObject::tr("这条MIDI轨已被移除，可恢复后继续编辑。");
        return false;
    }
    if (!validNotes(notes)) {
        _error = QObject::tr("音符的起止、音高或力度超出可用范围，请检查本次修改。");
        return false;
    }
    auto value = _data;
    auto list = tracks();
    auto row = list[i].toMap();
    row.insert("notes", notes);
    list[i] = row;
    value.insert("tracks", list);
    return write(value);
}
bool MidiTrackDocument::setAlignment(const QString& id, const QString& audio, double offset,
                                     const QVariantList& anchors, const QString& association)
{
    reload();
    const int i = indexOf(id);
    if (i < 0) {
        _error = QObject::tr("这条MIDI轨已被移除，可恢复后继续对齐。");
        return false;
    }
    if (!std::isfinite(offset) || qAbs(offset) > 86400000 || !validAnchors(anchors)) {
        _error = QObject::tr("请按时间先后填写对齐点，两列的时间都需要逐项增加。");
        return false;
    }
    auto value = _data;
    auto list = tracks();
    auto row = list[i].toMap();
    row.insert("audioId", audio);
    row.insert("offsetMs", offset);
    row.insert("anchors", anchors);
    if (!association.isEmpty())
        row.insert("associationId", association);
    else
        row.remove("associationId");
    list[i] = row;
    value.insert("tracks", list);
    return write(value);
}

QVariantMap exportMidiTrack(const QVariantMap& document, const QString& id, const QString& path,
                            bool mapped)
{
    QVariantMap track;
    for (const auto& value : document.value("tracks").toList())
        if (value.toMap().value("id").toString() == id) {
            track = value.toMap();
            break;
        }
    if (track.isEmpty())
        return error(QObject::tr("请选择要导出的MIDI轨道。"));
    struct Event {
        int tick, order;
        QByteArray data;
    };
    QVector<Event> events;
    auto at = [&](double seconds) {
        return qMax(0, qRound((mapped ? midiTrackTime(track, seconds) : seconds) * 1000));
    };
    auto add = [&](double seconds, int order, const QByteArray& bytes) {
        events.append({at(seconds), order, bytes});
    };
    QByteArray name = track.value("name").toString().toUtf8(), title("\xff\x03", 2);
    variable(title, name.size());
    title += name;
    events.append({0, 0, title});
    events.append({0, 0, QByteArray("\xff\x51\x03\x0f\x42\x40", 6)});
    for (const auto& value : track.value("events").toList()) {
        const auto e = value.toMap();
        const int type = e.value("type").toInt();
        QByteArray bytes;
        if (type >= 128 && type < 240) {
            bytes.append(char(type));
            bytes.append(char(e.value("a").toInt()));
            if ((type & 240) != 192 && (type & 240) != 208)
                bytes.append(char(e.value("b").toInt()));
        } else if (type == 240 || type == 247) {
            const auto body = QByteArray::fromHex(e.value("data").toString().toLatin1());
            bytes.append(char(type));
            variable(bytes, body.size());
            bytes += body;
        } else if (type == 255) {
            const int meta = e.value("meta").toInt();
            if (meta == 81 || meta == 47 || meta == 3)
                continue;
            const auto body = QByteArray::fromHex(e.value("data").toString().toLatin1());
            bytes.append('\xff');
            bytes.append(char(meta));
            variable(bytes, body.size());
            bytes += body;
        }
        if (!bytes.isEmpty())
            add(e.value("seconds").toDouble(), 1, bytes);
    }
    for (const auto& value : track.value("notes").toList()) {
        const auto n = value.toMap();
        if (n.value("unreleased").toBool())
            return error(QObject::tr("有音符缺少松键，请先调整音长再导出。"));
        const int channel = n.value("channel").toInt(), pitch = n.value("pitch").toInt();
        const int start = at(n.value("start").toDouble()),
                  end = qMax(start + 1, at(n.value("end").toDouble()));
        events.append(
            {start, 3,
             QByteArray(1, char(144 | channel)) + char(pitch) + char(n.value("velocity").toInt())});
        events.append({end, 2, QByteArray(1, char(128 | channel)) + char(pitch) + char(0)});
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.tick == b.tick ? a.order < b.order : a.tick < b.tick;
    });
    QByteArray body;
    int previous = 0;
    for (const auto& e : events) {
        variable(body, quint32(e.tick - previous));
        body += e.data;
        previous = e.tick;
    }
    body.append("\0\xff\x2f\0", 4);
    QByteArray file("MThd", 4);
    number(file, 6, 4);
    number(file, 0, 2);
    number(file, 1, 2);
    number(file, 1000, 2);
    file.append("MTrk", 4);
    number(file, body.size(), 4);
    file += body;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(file) != file.size() || !output.commit())
        return error(QObject::tr("MIDI保存失败：%1").arg(output.errorString()));
    return {{"ok", true}, {"file", path}, {"notes", track.value("notes").toList().size()}};
}
} // namespace Ms
