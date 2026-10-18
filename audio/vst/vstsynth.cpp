// SPDX-License-Identifier: GPL-2.0-or-later
#include "vstsynth.h"
#include "audio/midi/event.h"
#include "audio/midi/midipatch.h"
#include "fluidsource.h"
#include <QJsonDocument>
#include <cmath>
namespace Ms {
VstSynth::VstSynth(bool offline, Synthesizer *source, bool on)
    : _rack(offline, VstHost::createFluidSource), fallback(source),
      enabled(on) {
  _clock.offline = offline;
}
VstSynth::~VstSynth() { qDeleteAll(patches); }
void VstSynth::init(float rate) {
  const auto previous = _sampleRate;
  Synthesizer::init(rate);
  _clock.rate = rate;
  if (previous != rate && !config.isEmpty()) {
    auto value = configuration();
    _rack.load({}, rate);
    setConfiguration(value);
  }
}
std::vector<SoundFontInfo> VstSynth::soundFontsInfo() const {
  std::vector<SoundFontInfo> result;
  for (const auto &v : _rack.instruments()) {
    const auto c = v.toMap();
    if (c["available"].toBool())
      result.emplace_back(QStringLiteral("VST"), c["label"].toString());
  }
  return result;
}
void VstSynth::mixMaster(unsigned count, float *p) {
  if (enabled && hasGraph.load(std::memory_order_acquire))
    _rack.mix(int(count), p, _clock);
}
void VstSynth::processMaster(unsigned count, float *p) {
  if (enabled && hasGraph.load(std::memory_order_acquire))
    _rack.master(int(count), p, _clock);
  _clock.sample += count;
  if (_clock.playing)
    _clock.quarter += count / _clock.rate * _clock.tempo / 60;
}
void VstSynth::setPlaybackContext(qint64 sample, double quarter, double tempo,
                                  bool playing, int numerator,
                                  int denominator) {
  _clock.sample = sample;
  _clock.quarter = quarter;
  _clock.tempo = tempo;
  _clock.playing = playing;
  _clock.numerator = numerator;
  _clock.denominator = denominator;
}
void VstSynth::setMasterTuning(double value) {
  tuning.store(value, std::memory_order_release);
  tuningCents.store(float(1200 * std::log2(value / 440.)),
                    std::memory_order_release);
}
void VstSynth::play(const PlayEvent &event) {
  const int c = event.channel();
  if (event.type() == ME_CONTROLLER && event.controller() == CTRL_HBANK)
    bankHi[c] = event.value() & 127;
  if (event.type() == ME_CONTROLLER && event.controller() == CTRL_LBANK)
    bankLo[c] = event.value() & 127;
  const int id = (bankHi[c] << 7) | bankLo[c];
  const bool bankChange =
      event.type() == ME_CONTROLLER &&
      (event.controller() == CTRL_HBANK || event.controller() == CTRL_LBANK);
  if (enabled && bankChange)
    _rack.midi(c, event.type(), event.dataA(), event.dataB(),
               event.tuning() + tuningCents.load(std::memory_order_acquire));
  if (enabled && available[id]) {
    if (!bankChange)
      _rack.midi(c, event.type(), event.dataA(), event.dataB(),
                 event.tuning() + tuningCents.load(std::memory_order_acquire));
    return;
  }
  if (!fallback)
    return;
  fallback->setActive(true);
  auto output = event;
  if (event.type() == ME_CONTROLLER && event.controller() == CTRL_HBANK)
    return;
  if (event.type() == ME_CONTROLLER && event.controller() == CTRL_LBANK) {
    output.setDataA(CTRL_HBANK);
    output.setDataB((originalBank[id] >> 7) & 127);
    fallback->play(output);
    output.setDataA(CTRL_LBANK);
    output.setDataB(originalBank[id] & 127);
  } else if (event.type() == ME_CONTROLLER &&
             event.controller() == CTRL_PROGRAM)
    output.setDataB(originalProgram[id]);
  else if (event.type() == ME_PROGRAM)
    output.setDataA(originalProgram[id]);
  fallback->play(output);
}
void VstSynth::clearState() {
  if (!config.isEmpty())
    setConfiguration({});
}
SynthesizerGroup VstSynth::state() const {
  SynthesizerGroup group;
  group.setName(name());
  if (!config.isEmpty())
    group.push_back(IdValue(
        0, QString::fromUtf8(QJsonDocument::fromVariant(
                                 const_cast<VstSynth *>(this)->configuration())
                                 .toJson(QJsonDocument::Compact))));
  return group;
}
bool VstSynth::setState(const SynthesizerGroup &group) {
  for (const auto &value : group)
    if (value.id == 0) {
      QJsonParseError error;
      const auto doc = QJsonDocument::fromJson(value.data.toUtf8(), &error);
      if (error.error != QJsonParseError::NoError)
        return false;
      return setConfiguration(doc.toVariant().toMap());
    }
  clearState();
  return true;
}
bool VstSynth::setConfiguration(const QVariantMap &value) {
  ++_revision;
  config = value;
  _problems.clear();
  available.fill(false);
  originalBank.fill(0);
  originalProgram.fill(0);
  if (enabled)
    _rack.load(value, _sampleRate, &_problems);
  auto oldPatches = patches;
  patches.clear();
  const auto instruments =
      enabled ? _rack.instruments() : value["slots"].toList();
  for (const auto &v : instruments) {
    const auto c = v.toMap();
    const int id = c["id"].toInt();
    if (id < 1 || id > 16383)
      continue;
    const auto original = c["originalPatch"].toMap();
    originalBank[id] = original["bank"].toInt();
    originalProgram[id] = qBound(0, original["program"].toInt(), 127);
    available[id] = enabled && c["available"].toBool();
    MidiPatch *patch = nullptr;
    for (int i = 0; i < oldPatches.size(); ++i)
      if (oldPatches[i]->bank == id) {
        patch = oldPatches.takeAt(i);
        break;
      }
    if (!patch)
      patch = new MidiPatch;
    patch->drum = false;
    patch->synti = name();
    patch->bank = id;
    patch->prog = 0;
    patch->sfid = 0;
    patch->name = c["label"].toString() +
                  (available[id] ? QString() : QStringLiteral(" · SF2"));
    patches.append(patch);
  }
  qDeleteAll(oldPatches);
  hasGraph.store(!value["slots"].toList().isEmpty() ||
                     !value["masterEffects"].toList().isEmpty(),
                 std::memory_order_release);
  return true;
}
QVariantMap VstSynth::configuration(bool snapshot) {
  if (enabled && snapshot && !config.isEmpty())
    config = _rack.state();
  return config;
}
} // namespace Ms
