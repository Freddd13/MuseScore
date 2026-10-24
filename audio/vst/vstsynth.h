// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "audio/midi/synthesizer.h"
#include "rack.h"
#include <QSet>
namespace Ms {
class VstSynth final : public Synthesizer {
  VstHost::Rack _rack;
  VstHost::Clock _clock;
  QList<MidiPatch *> patches;
  QVariantMap config;
  QStringList _problems;
  Synthesizer *fallback = nullptr;
  bool enabled = true;
  std::atomic<bool> hasGraph{false};
  std::atomic<double> tuning{440.};
  std::atomic<float> tuningCents{0};
  quint64 _revision = 0;
  std::array<int, 256> bankHi{}, bankLo{};
  std::array<int, 16384> originalBank{}, originalProgram{};
  std::array<bool, 16384> available{};
  QVariantList pianoInstances;
  QSet<int> suppressedSlots;
  std::array<std::atomic<int>,256> pianoBanks;
  QVector<int> configuredPianoBanks = QVector<int>(256,0);
  QVariantMap playbackConfiguration(const QVariantMap&) const;

public:
  explicit VstSynth(bool offline = false, Synthesizer *fallback = nullptr,
                    bool enabled = true);
  ~VstSynth() override;
  const char *name() const override { return "KumoVST"; }
  void init(float) override;
  void setMasterTuning(double value) override;
  double masterTuning() const override {
    return tuning.load(std::memory_order_acquire);
  }
  bool loadSoundFonts(const QStringList &) override { return true; }
  std::vector<SoundFontInfo> soundFontsInfo() const override;
  void process(unsigned, float *, float *, float *) override {}
  void mixMaster(unsigned, float *) override;
  void processMaster(unsigned, float *) override;
  void setPlaybackContext(qint64, double, double, bool, int, int) override;
  int processingLatency() const override { return _rack.latency(); }
  qint64 processingTail() const override { return _rack.tail(); }
  void play(const PlayEvent &) override;
  void allSoundsOff(int channel) override { _rack.silence(channel, true); }
  void allNotesOff(int channel) override { _rack.silence(channel, false); }
  const QList<MidiPatch *> &getPatchInfo() const override { return patches; }
  bool hasStateToStore() const override { return !config.isEmpty(); }
  void clearState() override;
  SynthesizerGroup state() const override;
  bool setState(const SynthesizerGroup &) override;
  bool setConfiguration(const QVariantMap &);
  QVariantMap configuration(bool snapshot = true);
  QVariantMap runtimeConfiguration(bool snapshot = true);
  void rememberPianoSnapshot(const QVariantMap& value) {
    config["commonPianoSnapshot"]=value; config["schema"]=1;
  }
  void setPianoRouting(const QVariantList&,const QVector<int>&,const QSet<int>&,bool refresh=true);
  int pianoSlot(int channel) const {
    return channel>=0 && channel<int(pianoBanks.size())
        ? pianoBanks[channel].load(std::memory_order_acquire):0;
  }
  VstHost::Rack &rack() { return _rack; }
  void setRouting(const QVector<int> &banks) {
    bankHi.fill(0);
    bankLo.fill(0);
    for (int i = 0; i < qMin(256, banks.size()); ++i) {
      bankHi[i] = (banks[i] >> 7) & 127;
      bankLo[i] = banks[i] & 127;
    }
    if (enabled)
      _rack.bindChannels(banks);
  }
  QStringList problems() const { return _problems + _rack.problems(); }
  quint64 revision() const { return _revision; }
};
} // namespace Ms
