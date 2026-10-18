// SPDX-License-Identifier: GPL-2.0-or-later
#include "fluidsource.h"
#include "audio/midi/event.h"
#include "audio/midi/fluid/fluid.h"
#include <QJsonDocument>
#include <algorithm>
namespace Ms {
namespace VstHost {
namespace {
// A private voice renderer is created only when an SF2 part needs its own
// effects.
class FluidSource final : public Plugin {
  Descriptor identity;
  std::unique_ptr<FluidS::Fluid> synth;
  std::vector<float> audio, fx1, fx2;
  int maximum = 0, bank = 0, program = 0;

public:
  explicit FluidSource(const Descriptor &descriptor) : identity(descriptor) {
    const auto value = descriptor.uid.split(':');
    bank = value.value(0).toInt();
    program = value.value(1).toInt();
  }
  bool prepare(double rate, int block, QString *error, bool) override {
    synth = std::make_unique<FluidS::Fluid>();
    synth->init(float(rate));
    if (!synth->loadSoundFonts(identity.path.split('\n', Qt::SkipEmptyParts))) {
      if (error)
        *error = QStringLiteral("请在合成器中重新选择这份 SF2 音色库。");
      return false;
    }
    maximum = block;
    audio.resize(block * 2);
    fx1.resize(block * 2);
    fx2.resize(block * 2);
    synth->play(PlayEvent(ME_CONTROLLER, 0, CTRL_HBANK, (bank >> 7) & 127));
    synth->play(PlayEvent(ME_CONTROLLER, 0, CTRL_LBANK, bank & 127));
    synth->play(PlayEvent(ME_CONTROLLER, 0, CTRL_PROGRAM, program));
    synth->play(PlayEvent(ME_CONTROLLER, 0, CTRL_VOLUME, 100));
    return true;
  }
  bool process(int count, const float *, const float *, float *l, float *r,
               const Midi *events, int used, const Clock &) override {
    if (!synth || count < 0 || count > maximum)
      return false;
    int at = 0;
    auto render = [&](int from, int to) {
      if (to <= from)
        return;
      std::fill_n(audio.data(), (to - from) * 2, 0);
      std::fill_n(fx1.data(), (to - from) * 2, 0);
      std::fill_n(fx2.data(), (to - from) * 2, 0);
      synth->process(to - from, audio.data(), fx1.data(), fx2.data());
      for (int i = from; i < to; ++i) {
        l[i] = audio[(i - from) * 2];
        r[i] = audio[(i - from) * 2 + 1];
      }
    };
    for (int i = 0; i < used; ++i) {
      const int next = qBound(at, events[i].offset, count);
      render(at, next);
      at = next;
      PlayEvent event(events[i].status & 0xf0, 0, events[i].first,
                      events[i].second);
      event.setTuning(events[i].tuning);
      synth->play(event);
    }
    render(at, count);
    return true;
  }
  bool restore(const QByteArray &, QString *) override { return true; }
  QByteArray state(QString *) override {
    return QJsonDocument::fromVariant(
               QVariantMap{{"bank", bank}, {"program", program}})
        .toJson(QJsonDocument::Compact);
  }
  int latency() const override { return 0; }
  qint64 tail() const override { return 0; }
  QString presetName() const override { return identity.name; }
  bool openEditor(QWidget *, QString *error) override {
    if (error)
      *error = QStringLiteral("这份 SF2 沿用添加效果前的音色。要换音色，可恢复 "
                              "SF2 后从混音器选择。");
    return false;
  }
  void closeEditor() override {}
  void setParameter(quint32, double) override {}
  QVariantList parameters() const override { return {}; }
  bool takeChanges() override { return false; }
};
} // namespace
std::unique_ptr<Plugin> createFluidSource(const Descriptor &descriptor,
                                          QString *) {
  return std::make_unique<FluidSource>(descriptor);
}
} // namespace VstHost
} // namespace Ms
