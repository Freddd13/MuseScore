// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "performancetiming.h"
#include "note.h"
#include "chord.h"
#include "segment.h"
#include "score.h"
#include "undo.h"
#include "xml.h"
#include <QGlobalStatic>
#include <QHash>
#include <QReadWriteLock>
#include <QSet>
#include <atomic>
#include <algorithm>
#include <limits>

namespace Ms
{
namespace PerformanceTiming
{
namespace
{
struct Storage {
    QReadWriteLock lock;
    QHash<const Note*, Adjustments> rows;
    std::atomic_int count{0};
};
Q_GLOBAL_STATIC(Storage, data)
void store(const Note* note, const Adjustments& next)
{
    if (data.isDestroyed())
        return;
    QWriteLocker locker(&data->lock);
    if (next.isEmpty())
        data->rows.remove(note);
    else
        data->rows.insert(note, next);
    data->count.store(data->rows.size(), std::memory_order_release);
}
class Change final : public UndoCommand
{
    Note* _note;
    Adjustments _next;
    void flip(EditData*) override
    {
        auto before = values(_note);
        store(_note, _next);
        _next = before;
        _note->score()->setPlaylistDirty();
    }

  public:
    Change(Note* note, const Adjustments& next) : _note(note), _next(next) {}
    const char* name() const override { return "ChangePerformanceTiming"; }
};
} // namespace
bool Adjustment::operator==(const Adjustment& o) const
{
    return index == o.index && count == o.count && pitch == o.pitch && onset == o.onset &&
           release == o.release;
}
Adjustments values(const Note* note)
{
    if (!data.exists() || data.isDestroyed() || !data->count.load(std::memory_order_acquire))
        return {};
    QReadLocker locker(&data->lock);
    return data->rows.value(note);
}
bool matches(const Note* note, const Adjustment& a)
{
    return note && a.index >= 0 && a.index < note->playEvents().size() &&
           a.count == note->playEvents().size() && a.pitch == note->playEvents()[a.index].pitch();
}
Adjustment value(const Note* note, int index)
{
    Adjustment empty;
    empty.index = index;
    if (note && index >= 0 && index < note->playEvents().size()) {
        empty.count = note->playEvents().size();
        empty.pitch = note->playEvents()[index].pitch();
    }
    for (const auto& a : values(note))
        if (a.index == index && matches(note, a))
            return a;
    return empty;
}
void apply(const Note* note, int index, int& onset, int& release)
{
    if (!data.exists() || data.isDestroyed() || !data->count.load(std::memory_order_acquire))
        return;
    const auto a = value(note, index);
    constexpr qint64 limit = std::numeric_limits<int>::max() / 2;
    onset = int(qBound(-limit, qint64(onset) + a.onset, limit));
    release = int(qBound(qint64(onset), qint64(release) + a.release, limit));
}
int lookahead(Score* score)
{
    if (!score || !data.exists() || data.isDestroyed() ||
        !data->count.load(std::memory_order_acquire))
        return 0;
    int advance = 0;
    // Start from live notes; removed notes can still belong to the undo history.
    for (auto segment = score->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest))
        for (int track = 0; track < score->ntracks(); ++track) {
            const auto element = segment->element(track);
            if (!element || !element->isChord())
                continue;
            const auto chord = toChord(element);
            auto chords = chord->graceNotes();
            chords.append(chord);
            for (const auto current : chords)
                for (const auto note : current->notes())
                    for (const auto& edit : values(note))
                        advance = qMax(advance, -edit.onset);
        }
    return advance;
}
bool change(Score* score, Note* note, const Adjustments& next, bool linked)
{
    if (!score || !note)
        return false;
    auto normalized = next;
    normalized.erase(std::remove_if(normalized.begin(), normalized.end(),
                                    [](const Adjustment& a) { return !a.onset && !a.release; }),
                     normalized.end());
    std::sort(normalized.begin(), normalized.end(),
              [](const Adjustment& a, const Adjustment& b) { return a.index < b.index; });
    bool changed = false;
    const auto links = linked ? note->linkList() : QList<ScoreElement*>{note};
    for (auto item : links)
        if (item->type() == ElementType::NOTE) {
            auto target = static_cast<Note*>(item);
            if (values(target) != normalized) {
                score->undo(new Change(target, normalized));
                changed = true;
            }
        }
    return changed;
}
void copy(const Note* source, const Note* destination) { store(destination, values(source)); }
void forget(const Note* note)
{
    if (data.exists() && !data.isDestroyed() && data->count.load(std::memory_order_acquire))
        store(note, {});
}
void write(const Note* note, XmlWriter& xml)
{
    QStringList rows;
    for (const auto& a : values(note))
        rows.append(QString("%1,%2,%3,%4,%5")
                        .arg(a.index)
                        .arg(a.count)
                        .arg(a.pitch)
                        .arg(a.onset)
                        .arg(a.release));
    if (!rows.isEmpty())
        xml.tag("kumoPerformanceTiming", rows.join(';'));
}
bool read(Note* note, XmlReader& xml)
{
    if (xml.name() != "kumoPerformanceTiming")
        return false;
    const auto text = xml.readElementText();
    if (text.size() > 2 * 1024 * 1024)
        return true;
    Adjustments next;
    QSet<int> seen;
    for (const auto& line : text.split(';')) {
        const auto fields = line.split(',');
        if (fields.size() != 5 || next.size() >= 65536)
            continue;
        int v[5];
        bool valid = true;
        for (int i = 0; i < 5; ++i) {
            bool ok = false;
            v[i] = fields[i].toInt(&ok);
            valid &= ok;
        }
        if (!valid || v[0] < 0 || v[0] >= v[1] || v[1] > 65536 || v[2] < -127 || v[2] > 127 ||
            qAbs(qint64(v[3])) > 16000000 || qAbs(qint64(v[4])) > 16000000 || seen.contains(v[0]))
            continue;
        seen.insert(v[0]);
        next.append({v[0], v[1], v[2], v[3], v[4]});
    }
    store(note, next);
    return true;
}
} // namespace PerformanceTiming
} // namespace Ms
