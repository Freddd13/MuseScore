// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "miditransport.h"
#include "miditracks.h"
#include "audio/midi/msynthesizer.h"
#include <QObject>
#include <QHash>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include <array>
#include <limits>

namespace Ms
{
namespace
{
struct MonitorQueue {
    std::array<NPlayEvent, 512> events;
    std::atomic<unsigned> read{0}, write{0};
    std::atomic_bool overflow{false};
};
struct Packet {
    MidiTrackPlaybackPlan plan;
    quint64 id;
    int cursor = 0;
    qint64 frame = 0;
    bool initialized = false, finished = false, hasFollowed = false;
    std::atomic<qint64> publishedFrame{0};
    std::atomic_bool publishedPlaying{false};
    std::atomic_bool complete{false};
    std::unique_ptr<MonitorQueue> input;
    explicit Packet(MidiTrackPlaybackPlan value, quint64 serial)
        : plan(std::move(value)), id(serial), frame(plan.startFrame)
    {
        if (plan.monitor)
            input.reset(new MonitorQueue);
    }
};
std::atomic<Packet*> requested{nullptr}, current{nullptr};
std::atomic<unsigned> readers{0};
std::vector<std::unique_ptr<Packet>> owned;
quint64 serial = 0;
void send(MasterSynthesizer* synth, const MidiTrackPlaybackEvent& e)
{
    synth->play(e.event, unsigned(e.synthesizer));
}
void release(MasterSynthesizer* synth, Packet* packet, bool hard = true)
{
    if (!packet)
        return;
    for (const auto& pair : packet->plan.channels) {
        synth->play(NPlayEvent(ME_CONTROLLER, pair.first, 64, 0), unsigned(pair.second));
        synth->play(NPlayEvent(ME_CONTROLLER, pair.first, 123, 0), unsigned(pair.second));
        if (hard)
            synth->play(NPlayEvent(ME_CONTROLLER, pair.first, 120, 0), unsigned(pair.second));
    }
    packet->publishedPlaying.store(false, std::memory_order_release);
}
int order(const NPlayEvent& e)
{
    if (e.type() == ME_CONTROLLER)
        return 0;
    return e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0) ? 1 : 2;
}
struct HeldKey {
    MidiTrackPlaybackEvent onset;
    bool pressed = true;
};
void makeCheckpoints(MidiTrackPlaybackPlan& plan)
{
    constexpr int block = 512;
    QMap<quint64, QPair<int, MidiTrackPlaybackEvent>> controls;
    QMap<QPair<int, int>, HeldKey> keys;
    QHash<int, int> pedal;
    plan.checkpoints.append({0, {}});
    for (int index = 0; index < plan.events.size(); ++index) {
        const auto& e = plan.events[index];
        const auto& event = e.event;
        const int channel = event.channel();
        if (event.type() == ME_CONTROLLER) {
            const int controller = event.controller();
            if (controller == 121) {
                for (auto it = controls.begin(); it != controls.end();)
                    if (it.value().second.event.channel() == channel)
                        it = controls.erase(it);
                    else
                        ++it;
                pedal[channel] = 0;
            }
            controls.insert((quint64(channel) << 32) | (quint64(event.type()) << 16) |
                                quint64(controller),
                            {index, e});
            if (controller == 64)
                pedal[channel] = event.value();
            if (controller == 120 || controller == 123 || controller == 121 ||
                (controller == 64 && event.value() < 64)) {
                for (auto it = keys.begin(); it != keys.end();) {
                    if (it.key().first != channel) {
                        ++it;
                        continue;
                    }
                    if (controller == 120 ||
                        ((controller == 121 || controller == 64) && !it->pressed) ||
                        (controller == 123 && pedal.value(channel) < 64))
                        it = keys.erase(it);
                    else {
                        if (controller == 123)
                            it->pressed = false;
                        ++it;
                    }
                }
            }
        } else if (event.type() == ME_NOTEON) {
            const auto key = qMakePair(channel, event.pitch());
            if (event.velo())
                keys.insert(key, {e, true});
            else if (pedal.value(channel) < 64)
                keys.remove(key);
            else {
                auto held = keys.find(key);
                if (held != keys.end())
                    held->pressed = false;
            }
        } else {
            const int parameter = event.type() == ME_POLYAFTER ? event.dataA() : 0;
            controls.insert((quint64(channel) << 32) | (quint64(event.type()) << 16) |
                                quint64(parameter),
                            {index, e});
        }
        if ((index + 1) % block == 0) {
            MidiTrackPlaybackPlan::Checkpoint point;
            point.cursor = index + 1;
            auto ordered = controls.values();
            std::sort(
                ordered.begin(), ordered.end(),
                [](const QPair<int, MidiTrackPlaybackEvent>& a,
                   const QPair<int, MidiTrackPlaybackEvent>& b) { return a.first < b.first; });
            for (const auto& control : ordered)
                point.state.append(control.second);
            for (const auto& key : keys) {
                point.state.append(key.onset);
                if (!key.pressed) {
                    auto off = key.onset;
                    off.event.setVelo(0);
                    point.state.append(off);
                }
            }
            plan.checkpoints.append(std::move(point));
        }
    }
}
void initializeAt(MasterSynthesizer* synth, Packet& packet, qint64 frame)
{
    const auto& plan = packet.plan;
    const int cursor = int(
        std::lower_bound(plan.events.cbegin(), plan.events.cend(), frame,
                         [](const MidiTrackPlaybackEvent& e, qint64 f) { return e.frame < f; }) -
        plan.events.cbegin());
    release(synth, &packet);
    for (const auto& e : plan.initial)
        send(synth, e);
    if (!plan.checkpoints.isEmpty()) {
        const auto& point = plan.checkpoints[qMin(cursor / 512, plan.checkpoints.size() - 1)];
        for (const auto& e : point.state)
            send(synth, e);
        for (int index = point.cursor; index < cursor; ++index)
            send(synth, plan.events[index]);
    }
    packet.cursor = cursor;
    packet.initialized = true;
    packet.publishedPlaying.store(true, std::memory_order_release);
}
} // namespace

MidiTrackPlaybackPlan buildMidiTrackPlayback(const QVariantList& tracks,
                                             const QVector<MidiTrackSound>& sounds, int firstFree,
                                             int rate, double start, bool follow)
{
    MidiTrackPlaybackPlan plan;
    plan.sampleRate = rate;
    plan.followScore = follow;
    if (firstFree < 0 || firstFree > 255 || rate < 8000 || rate > 384000 || !std::isfinite(start) ||
        start < 0) {
        plan.error = QObject::tr("播放位置或声音设置无效。");
        return plan;
    }
    plan.startFrame = qint64(std::llround(start * rate));
    bool solo = false;
    for (const auto& value : tracks)
        solo = solo || value.toMap().value("solo").toBool();
    int next = firstFree;
    for (const auto& value : tracks) {
        const auto track = value.toMap();
        if (track.value("mute").toBool() || (solo && !track.value("solo").toBool()))
            continue;
        const double gain = track.value("volume", 1.).toDouble();
        if (!std::isfinite(gain) || gain < 0 || gain > 2) {
            plan.error = QObject::tr("音轨音量无效。");
            return plan;
        }
        const auto found =
            std::find_if(sounds.cbegin(), sounds.cend(), [&](const MidiTrackSound& sound) {
                return sound.id == track.value("id").toString();
            });
        if (found == sounds.cend()) {
            plan.error = QObject::tr("请选择%1的音源。").arg(track.value("name").toString());
            return plan;
        }
        QMap<int, int> mapping;
        for (const auto& note : track.value("notes").toList()) {
            const auto n = note.toMap();
            const int key = n.value("port").toInt() * 16 + n.value("channel").toInt();
            if (!mapping.contains(key)) {
                if (next >= 256) {
                    plan.error = QObject::tr(
                        "当前乐谱和MIDI轨使用了全部256个声音通道，请减少本次参加播放的轨道。");
                    return plan;
                }
                mapping.insert(key, next);
                plan.channels.append({next, found->synthesizer});
                ++next;
            }
        }
        auto frame = [&](double seconds) {
            return qMax<qint64>(0, qint64(std::llround(midiTrackTime(track, seconds) * rate)));
        };
        auto add = [&](qint64 at, const NPlayEvent& event) {
            plan.events.append({at, found->synthesizer, event});
            plan.endFrame = qMax(plan.endFrame, at);
        };
        for (int channel : mapping) {
            auto initial = [&](int control, int amount) {
                plan.initial.append({plan.startFrame, found->synthesizer,
                                     NPlayEvent(ME_CONTROLLER, channel, control, amount)});
            };
            initial(120, 0);
            initial(121, 0);
            initial(CTRL_HBANK, (found->bank >> 7) & 127);
            initial(CTRL_LBANK, found->bank & 127);
            initial(CTRL_PROGRAM, found->program);
            initial(CTRL_VOLUME, qBound(0, qRound(found->volume * gain), 127));
            initial(CTRL_PANPOT, found->pan);
            initial(CTRL_REVERB_SEND, found->reverb);
        }
        for (const auto& item : track.value("events").toList()) {
            const auto e = item.toMap();
            const int type = e.value("type").toInt(), kind = type & 240;
            if (type < 128 || type >= 240 || kind == 128 || kind == 144)
                continue;
            const int key = e.value("port").toInt() * 16 + e.value("channel").toInt();
            if (!mapping.contains(key))
                continue;
            const int a = e.value("a").toInt(), b = e.value("b").toInt();
            // The chosen source supplies program/bank; other playing controls
            // retain the imported performance, including sustain and expression.
            if (kind == 192 || (kind == 176 && (a == 0 || a == 32)))
                continue;
            NPlayEvent event(kind, mapping.value(key), a, b);
            if (kind == 176 && a == 7)
                event.setValue(qBound(0, qRound(b * gain), 127));
            add(frame(e.value("seconds").toDouble()), event);
        }
        for (const auto& item : track.value("notes").toList()) {
            const auto n = item.toMap();
            if (n.value("unreleased").toBool()) {
                plan.error = QObject::tr("%1有音符缺少松键，请先调整音长。")
                                 .arg(track.value("name").toString());
                return plan;
            }
            if (midiTrackTime(track, n.value("end").toDouble()) <= 0)
                continue;
            const int channel =
                mapping.value(n.value("port").toInt() * 16 + n.value("channel").toInt());
            const int pitch = n.value("pitch").toInt(), velocity = n.value("velocity").toInt();
            const qint64 on = frame(n.value("start").toDouble()),
                         off = qMax(on + 1, frame(n.value("end").toDouble()));
            add(on, NPlayEvent(ME_NOTEON, channel, pitch, velocity));
            add(off, NPlayEvent(ME_NOTEON, channel, pitch, 0));
        }
    }
    std::stable_sort(plan.events.begin(), plan.events.end(),
                     [](const MidiTrackPlaybackEvent& a, const MidiTrackPlaybackEvent& b) {
                         return a.frame == b.frame ? order(a.event) < order(b.event)
                                                   : a.frame < b.frame;
                     });
    if (plan.events.isEmpty()) {
        plan.error = QObject::tr("当前没有参加播放的音符。取消音轨静音后可以试听。");
        return plan;
    }
    makeCheckpoints(plan);
    return plan;
}
quint64 startMidiTrackPlayback(MidiTrackPlaybackPlan plan)
{
    if (!plan.error.isEmpty())
        return 0;
    std::unique_ptr<Packet> packet(new Packet(std::move(plan), ++serial));
    auto pointer = packet.get();
    owned.push_back(std::move(packet));
    requested.store(pointer, std::memory_order_release);
    return pointer->id;
}
quint64 startMidiTrackMonitor(const MidiTrackSound& sound, int channel, int rate)
{
    if (channel < 0 || channel > 255 || rate < 8000 || rate > 384000)
        return 0;
    MidiTrackPlaybackPlan plan;
    plan.monitor = true;
    plan.sampleRate = rate;
    plan.endFrame = std::numeric_limits<qint64>::max();
    plan.channels.append({channel, sound.synthesizer});
    const QPair<int, int> controls[] = {{120, 0},
                                        {121, 0},
                                        {CTRL_HBANK, (sound.bank >> 7) & 127},
                                        {CTRL_LBANK, sound.bank & 127},
                                        {CTRL_PROGRAM, sound.program},
                                        {CTRL_VOLUME, sound.volume},
                                        {CTRL_PANPOT, sound.pan},
                                        {CTRL_REVERB_SEND, sound.reverb}};
    for (const auto& pair : controls)
        plan.initial.append(
            {0, sound.synthesizer, NPlayEvent(ME_CONTROLLER, channel, pair.first, pair.second)});
    return startMidiTrackPlayback(std::move(plan));
}
bool sendMidiTrackMonitor(quint64 id, const NPlayEvent& event)
{
    const auto packet = requested.load(std::memory_order_acquire);
    if (!packet || packet->id != id || !packet->input)
        return false;
    auto& queue = *packet->input;
    const auto at = queue.write.load(std::memory_order_relaxed);
    if (at - queue.read.load(std::memory_order_acquire) >= queue.events.size()) {
        queue.overflow.store(true, std::memory_order_release);
        return false;
    }
    queue.events[at % queue.events.size()] = event;
    queue.write.store(at + 1, std::memory_order_release);
    return true;
}
void stopMidiTrackPlayback() { requested.store(nullptr, std::memory_order_release); }
void collectMidiTrackPlayback()
{
    if (readers.load(std::memory_order_acquire))
        return;
    const auto active = current.load(std::memory_order_acquire),
               waiting = requested.load(std::memory_order_acquire);
    if (readers.load(std::memory_order_acquire))
        return;
    owned.erase(std::remove_if(owned.begin(), owned.end(),
                               [&](const std::unique_ptr<Packet>& value) {
                                   return value.get() != active && value.get() != waiting;
                               }),
                owned.end());
}
QVariantMap midiTrackPlaybackState()
{
    const auto packet = requested.load(std::memory_order_acquire);
    if (!packet)
        return {{"playing", false}};
    return {{"playing", packet->publishedPlaying.load(std::memory_order_acquire)},
            {"requested", !packet->complete.load(std::memory_order_acquire)},
            {"id", QVariant::fromValue<qulonglong>(packet->id)},
            {"seconds", double(packet->publishedFrame.load(std::memory_order_relaxed)) /
                            packet->plan.sampleRate},
            {"duration", double(packet->plan.endFrame) / packet->plan.sampleRate}};
}
void processMidiTrackPlayback(MasterSynthesizer* synth, unsigned count, float* output,
                              qint64 scoreFrame, bool playing)
{
    if (!requested.load(std::memory_order_acquire) && !current.load(std::memory_order_acquire)) {
        synth->process(count, output);
        return;
    }
    readers.fetch_add(1, std::memory_order_acq_rel);
    auto packet = current.load(std::memory_order_relaxed);
    const auto wanted = requested.load(std::memory_order_acquire);
    if (packet != wanted) {
        release(synth, packet);
        packet = wanted;
        current.store(packet, std::memory_order_release);
    }
    if (packet && packet->finished && packet->plan.followScore) {
        if (!playing && packet->hasFollowed)
            packet->complete.store(true, std::memory_order_release);
        else if (playing && scoreFrame < packet->frame && scoreFrame < packet->plan.endFrame) {
            packet->finished = false;
            packet->initialized = false;
            packet->complete.store(false, std::memory_order_release);
        }
    }
    if (!packet || packet->finished) {
        synth->process(count, output);
        readers.fetch_sub(1, std::memory_order_release);
        return;
    }
    if (packet->plan.followScore && !playing) {
        if (packet->hasFollowed) {
            release(synth, packet);
            packet->finished = true;
            packet->complete.store(true, std::memory_order_release);
        }
        synth->process(count, output);
        readers.fetch_sub(1, std::memory_order_release);
        return;
    }
    if (packet->plan.followScore)
        packet->hasFollowed = true;
    const qint64 frame = packet->plan.followScore ? scoreFrame : packet->frame;
    if (!packet->initialized || (packet->plan.followScore && frame != packet->frame))
        initializeAt(synth, *packet, frame);
    if (packet->input) {
        auto& queue = *packet->input;
        const auto until = queue.write.load(std::memory_order_acquire);
        auto at = queue.read.load(std::memory_order_relaxed);
        if (queue.overflow.exchange(false, std::memory_order_acq_rel)) {
            release(synth, packet);
            at = until;
            packet->initialized = false;
        }
        for (; at != until; ++at) {
            auto event = queue.events[at % queue.events.size()];
            event.setChannel(packet->plan.channels.first().first);
            synth->play(event, unsigned(packet->plan.channels.first().second));
        }
        queue.read.store(at, std::memory_order_release);
        synth->process(count, output);
        packet->frame = frame + count;
        packet->publishedFrame.store(packet->frame, std::memory_order_relaxed);
        readers.fetch_sub(1, std::memory_order_release);
        return;
    }
    unsigned offset = 0;
    while (packet->cursor < packet->plan.events.size()) {
        const auto& event = packet->plan.events[packet->cursor];
        if (event.frame >= frame + count)
            break;
        const unsigned position = unsigned(qBound<qint64>(offset, event.frame - frame, count));
        if (position > offset)
            synth->process(position - offset, output + offset * 2);
        offset = position;
        send(synth, event);
        ++packet->cursor;
    }
    if (offset < count)
        synth->process(count - offset, output + offset * 2);
    packet->frame = frame + count;
    packet->publishedFrame.store(packet->frame, std::memory_order_relaxed);
    if (packet->frame > packet->plan.endFrame) {
        release(synth, packet, false);
        packet->finished = true;
        packet->complete.store(!packet->plan.followScore, std::memory_order_release);
    }
    readers.fetch_sub(1, std::memory_order_release);
}
} // namespace Ms
