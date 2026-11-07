// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "midisources.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/score.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/instrument.h"
#ifdef KUMO_VST_HOST
#include "audio/vst/vstsynth.h"
#endif
#include <QSet>
#include <QPointer>
namespace Ms
{
namespace
{
QPointer<QObject> soundOwner;
}
void clearMidiTrackSounds(MasterSynthesizer* master, QObject* owner)
{
    if (owner && soundOwner != owner)
        return;
#ifdef KUMO_VST_HOST
    if (master)
        if (auto vst = static_cast<VstSynth*>(master->synthesizer("KumoVST")))
            vst->setReferenceSources({});
#else
    Q_UNUSED(master);
#endif
    soundOwner.clear();
}
MidiTrackSounds prepareMidiTrackSounds(MasterSynthesizer* master, Score* score,
                                       const QVariantList& tracks, QObject* owner)
{
    MidiTrackSounds result;
    if (!master || !score) {
        result.error = QObject::tr("打开乐谱并选择声音设备后可以播放MIDI。");
        return result;
    }
    QVariantList references;
    bool solo = false;
    for (const auto& value : tracks)
        solo |= value.toMap().value("solo").toBool();
#ifdef KUMO_VST_HOST
    auto vst = static_cast<VstSynth*>(master->synthesizer("KumoVST"));
    QVariantList sourceSlots;
    if (vst)
        sourceSlots = vst->runtimeConfiguration(true).value("slots").toList();
    QSet<int> used;
    for (const auto& value : sourceSlots)
        if (!value.toMap().contains("referenceMidi"))
            used.insert(value.toMap().value("id").toInt());
    int freeBank = 16383;
#endif
    for (const auto& value : tracks) {
        const auto track = value.toMap();
        if (track.value("mute", true).toBool() || (solo && !track.value("solo").toBool()))
            continue;
        MidiTrackSound sound;
        sound.id = track.value("id").toString();
        sound.synthesizer = master->index("Fluid");
        const auto source = track.value("source").toMap();
        if (source.value("kind").toString() == "score-piano" && score->nstaves()) {
            const int staff = source.value("staff").toInt();
            if (staff < 0 || staff >= score->nstaves()) {
                result.error = QObject::tr("原音源所在谱表已有变化，请为这条MIDI轨重新选择音源。");
                return result;
            }
            const auto instrument = score->staff(staff)->part()->instrument(Fraction());
            if (!instrument->channel().isEmpty()) {
                const auto channel = score->masterScore()->playbackChannel(instrument->channel(0));
                sound.bank = channel->bank();
                sound.program = channel->program();
                sound.volume = channel->volume();
                sound.pan = channel->pan();
                sound.reverb = channel->reverb();
                sound.synthesizer = master->index(channel->synti());
#ifdef KUMO_VST_HOST
                if (vst && channel->synti() == "KumoVST") {
                    const int native = vst->pianoSlot(channel->channel())
                                           ? vst->pianoSlot(channel->channel())
                                           : channel->bank();
                    QVariantMap found;
                    for (const auto& item : sourceSlots)
                        if (item.toMap().value("id").toInt() == native &&
                            !item.toMap().contains("referenceMidi")) {
                            found = item.toMap();
                            break;
                        }
                    if (found.isEmpty()) {
                        result.error =
                            QObject::tr("请先在音源面板载入谱表%1的乐器。也可选择SF2钢琴试听。")
                                .arg(staff + 1);
                        return result;
                    }
                    while (freeBank > 0 && used.contains(freeBank))
                        --freeBank;
                    if (!freeBank) {
                        result.error = QObject::tr("当前声音路由已满，请减少本次参加播放的音轨。");
                        return result;
                    }
                    found.remove("commonPiano");
                    found["id"] = freeBank;
                    found["referenceMidi"] = sound.id;
                    found["label"] = QObject::tr("MIDI：%1").arg(track.value("name").toString());
                    references.append(found);
                    sound.bank = freeBank;
                    sound.program = 0;
                    used.insert(freeBank--);
                }
#endif
            }
        }
        result.sounds.append(sound);
    }
#ifdef KUMO_VST_HOST
    if (vst) {
        vst->setReferenceSources(references);
        const auto ready = vst->rack().instruments(true);
        for (const auto& wanted : references) {
            bool available = false;
            for (const auto& row : ready)
                if (row.toMap().value("id") == wanted.toMap().value("id"))
                    available = row.toMap().value("available").toBool();
            if (!available) {
                result.error = QObject::tr("MIDI音源载入失败：%1").arg(vst->problems().join("；"));
                vst->setReferenceSources({});
                return result;
            }
        }
    }
#endif
    soundOwner = owner;
    return result;
}
} // namespace Ms
