// SPDX-License-Identifier: GPL-2.0-or-later
#include "fixedchanges.h"
#include "plugin.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "vst3editor.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <iostream>
#include <mutex>

namespace Ms {
namespace VstHost {
namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;
using Module = VST3::Hosting::Module;
class Host final : public HostApplication {
  tresult PLUGIN_API getName(String128 name) override {
    const auto value = QStringLiteral("MuseScore Evolution Kumo");
    std::fill_n(name, 128, 0);
    std::copy_n(reinterpret_cast<const TChar *>(value.utf16()),
                qMin(127, value.size()), name);
    return kResultOk;
  }
};
Host &host() {
  static Host instance;
  return instance;
}
std::mutex &initialization() {
  static std::mutex mutex;
  return mutex;
}
Descriptor descriptor(const QString &path,
                      const VST3::Hosting::ClassInfo &info) {
  return {QFileInfo(path).absoluteFilePath(),
          "VST3",
          QString::fromStdString(info.ID().toString()),
          QString::fromStdString(info.name()),
          QString::fromStdString(info.vendor()),
          QString::fromStdString(info.subCategoriesString())
              .contains("Instrument")};
}
class Vst3Plugin;
class Handler final : public BorrowedInterface<IComponentHandler> {
  Vst3Plugin *owner;

public:
  Handler(Vst3Plugin *value) : owner(value) {}
  tresult PLUGIN_API beginEdit(ParamID) override;
  tresult PLUGIN_API performEdit(ParamID, double) override;
  tresult PLUGIN_API endEdit(ParamID) override;
  tresult PLUGIN_API restartComponent(int32) override;
};
class Vst3Plugin final : public Plugin {
  Descriptor description;
  Module::Ptr module;
  std::unique_ptr<PlugProvider> provider;
  IPtr<IComponent> component;
  IPtr<IEditController> controller;
  FUnknownPtr<IAudioProcessor> processor;
  HostProcessData buffers;
  EventList events{2048};
  FixedChanges inputChanges, outputChanges;
  Queue<Parameter, 1024> fromEditor, toEditor;
  std::array<std::array<ParamID, kCountCtrlNumber>, 16> mappings;
  ProcessContext context{};
  Handler handler{this};
  QPointer<Vst3Editor> editor;
  std::unique_ptr<QTimer> uiTimer;
  int maximum = 0;
  bool active = false, processing = false;
  std::atomic<int> restart{0};
  std::atomic<int> editing{0};
  std::atomic<bool> dirty{false};
  void parameterChanges() {
    inputChanges.clear();
    outputChanges.clear();
    Parameter value;
    while (fromEditor.pop(value))
      inputChanges.add(value.id, 0, value.value);
  }
  void midi(const Midi &value) {
    const int channel = value.status & 15, kind = value.status & 0xf0;
    if (kind == 0x80 || kind == 0x90) {
      Event event{};
      event.busIndex = 0;
      event.sampleOffset = value.offset;
      event.ppqPosition = context.projectTimeMusic;
      if (kind == 0x90 && value.second) {
        event.type = Event::kNoteOnEvent;
        event.noteOn.channel = int16(channel);
        event.noteOn.pitch = value.first;
        event.noteOn.velocity = value.second / 127.f;
        event.noteOn.noteId = -1;
        event.noteOn.tuning = value.tuning;
      } else {
        event.type = Event::kNoteOffEvent;
        event.noteOff.channel = int16(channel);
        event.noteOff.pitch = value.first;
        event.noteOff.velocity = value.second / 127.f;
        event.noteOff.noteId = -1;
      }
      events.addEvent(event);
    } else if (kind == 0xa0) {
      Event event{};
      event.type = Event::kPolyPressureEvent;
      event.busIndex = 0;
      event.sampleOffset = value.offset;
      event.polyPressure.channel = int16(channel);
      event.polyPressure.pitch = value.first;
      event.polyPressure.pressure = value.second / 127.f;
      event.polyPressure.noteId = -1;
      events.addEvent(event);
    } else {
      const int code = kind == 0xb0   ? value.first
                       : kind == 0xe0 ? kPitchBend
                       : kind == 0xd0 ? kAfterTouch
                                      : -1;
      if (code >= 0 && mappings[channel][code] != kNoParamId) {
        const double normalized =
            kind == 0xe0 ? (value.first + 128 * value.second) / 16383.
                         : (kind == 0xd0 ? value.first : value.second) / 127.;
        inputChanges.add(mappings[channel][code], value.offset, normalized);
      }
    }
  }
  void pumpEditor() {
    if (!editor) {
      uiTimer->stop();
      return;
    }
    Parameter value;
    while (toEditor.pop(value))
      if (controller)
        controller->setParamNormalized(value.id, value.value);
    if (stateChanged && takeChanges())
      stateChanged();
  }

public:
  explicit Vst3Plugin(const Descriptor &value) : description(value) {
    for (auto &row : mappings)
      row.fill(kNoParamId);
  }
  bool load(QString *error) {
    std::string message;
    module = Module::create(description.path.toUtf8().constData(), message);
    if (!module) {
      if (error)
        *error = QString::fromStdString(message);
      return false;
    }
    const auto factory = module->getFactory();
    for (const auto &info : factory.classInfos())
      if (info.category() == kVstAudioEffectClass &&
          QString::fromStdString(info.ID().toString()) == description.uid) {
        std::lock_guard<std::mutex> lock(initialization());
        factory.setHostContext(&host());
        PluginContextFactory::instance().setPluginContext(&host());
        PlugProvider::setErrorStream(&std::cerr);
        provider.reset(new PlugProvider(factory, info, true));
        if (!provider->initialize()) {
          if (error)
            *error = QStringLiteral("音源初始化失败：") + description.name;
          return false;
        }
        component = provider->getComponentPtr();
        controller = provider->getControllerPtr();
        processor = FUnknownPtr<IAudioProcessor>(component);
        if (!processor) {
          if (error)
            *error = QStringLiteral("这个插件未提供音频处理接口。");
          return false;
        }
        if (controller)
          controller->setComponentHandler(&handler);
        FUnknownPtr<IMidiMapping> mapping(controller);
        if (mapping)
          for (int ch = 0; ch < 16; ++ch)
            for (int cc = 0; cc < kCountCtrlNumber; ++cc) {
              ParamID id;
              if (mapping->getMidiControllerAssignment(
                      0, int16(ch), CtrlNumber(cc), id) == kResultOk)
                mappings[ch][cc] = id;
            }
        return true;
      }
    if (error)
      *error =
          QStringLiteral("文件中没有找到原来选择的音源：") + description.name;
    return false;
  }
  ~Vst3Plugin() override {
    closeEditor();
    if (processing)
      processor->setProcessing(false);
    if (active)
      component->setActive(false);
    if (controller)
      controller->setComponentHandler(nullptr);
    buffers.unprepare();
    processor = nullptr;
    controller = nullptr;
    component = nullptr;
    provider.reset();
    module.reset();
  }
  bool prepare(double rate, int block, QString *error, bool offline) override {
    if (!processor || rate < 8000 || block < 1 || block > 8192)
      return false;
    if (processing) {
      processor->setProcessing(false);
      processing = false;
    }
    if (active) {
      component->setActive(false);
      active = false;
    }
    for (auto direction : {kInput, kOutput}) {
      for (int bus = 0; bus < component->getBusCount(kAudio, direction); ++bus)
        component->activateBus(kAudio, direction, bus, bus == 0);
      for (int bus = 0; bus < component->getBusCount(kEvent, direction); ++bus)
        component->activateBus(kEvent, direction, bus, bus == 0);
    }
    ProcessSetup setup{offline ? kOffline : kRealtime, kSample32, block, rate};
    if (processor->canProcessSampleSize(kSample32) != kResultOk ||
        processor->setupProcessing(setup) != kResultOk ||
        !buffers.prepare(*component, block, kSample32)) {
      if (error)
        *error = QStringLiteral("音源无法按当前采样率和缓冲长度播放。");
      return false;
    }
    maximum = block;
    buffers.inputEvents = &events;
    buffers.inputParameterChanges = &inputChanges;
    buffers.outputParameterChanges = &outputChanges;
    context = {};
    context.sampleRate = rate;
    buffers.processContext = &context;
    buffers.processMode = offline ? kOffline : kRealtime;
    active = component->setActive(true) == kResultOk;
    processing = active && processor->setProcessing(true) == kResultOk;
    if (!processing && error)
      *error = QStringLiteral("音源未能开始处理声音。");
    return processing;
  }
  bool resetAudio() override {
    if (!processing || !active)
      return false;
    processor->setProcessing(false);
    processing = processor->setProcessing(true) == kResultOk;
    return processing;
  }
  bool process(int count, const float *left, const float *right, float *outLeft,
               float *outRight, const Midi *input, int length,
               const Clock &clock) override {
    if (!processing || count < 0 || count > maximum)
      return false;
    context.sampleRate = clock.rate;
    context.projectTimeSamples = clock.sample;
    context.projectTimeMusic = clock.quarter;
    context.tempo = clock.tempo;
    context.timeSigNumerator = clock.numerator;
    context.timeSigDenominator = clock.denominator;
    context.state = ProcessContext::kTempoValid |
                    ProcessContext::kTimeSigValid |
                    ProcessContext::kProjectTimeMusicValid |
                    (clock.playing ? ProcessContext::kPlaying : 0);
    events.clear();
    parameterChanges();
    buffers.numSamples = count;
    for (int i = 0; i < length; ++i)
      midi(input[i]);
    for (int bus = 0; bus < buffers.numInputs; ++bus) {
      buffers.inputs[bus].silenceFlags =
          left ? 0 : HostProcessData::kAllChannelsSilent;
      for (int ch = 0; ch < buffers.inputs[bus].numChannels; ++ch) {
        const auto source = bus == 0 ? (ch == 0   ? left
                                        : ch == 1 ? right
                                                  : nullptr)
                                     : nullptr;
        if (bus == 0 && buffers.inputs[bus].numChannels == 1 && left && right)
          for (int i = 0; i < count; ++i)
            buffers.inputs[bus].channelBuffers32[ch][i] =
                (left[i] + right[i]) * .5f;
        else if (source)
          std::copy_n(source, count, buffers.inputs[bus].channelBuffers32[ch]);
        else
          std::fill_n(buffers.inputs[bus].channelBuffers32[ch], count, 0.f);
      }
    }
    for (int bus = 0; bus < buffers.numOutputs; ++bus) {
      buffers.outputs[bus].silenceFlags = 0;
      for (int ch = 0; ch < buffers.outputs[bus].numChannels; ++ch)
        std::fill_n(buffers.outputs[bus].channelBuffers32[ch], count, 0.f);
    }
    if (processor->process(buffers) != kResultOk)
      return false;
    if (count) {
      if (buffers.numOutputs && buffers.outputs[0].numChannels) {
        std::copy_n(buffers.outputs[0].channelBuffers32[0], count, outLeft);
        std::copy_n(
            buffers.outputs[0]
                .channelBuffers32[qMin(1, buffers.outputs[0].numChannels - 1)],
            count, outRight);
      } else {
        std::fill_n(outLeft, count, 0.f);
        std::fill_n(outRight, count, 0.f);
      }
    }
    for (int i = 0; i < outputChanges.getParameterCount(); ++i) {
      auto queue = outputChanges.getParameterData(i);
      int offset;
      double value;
      if (queue->getPointCount() && queue->getPoint(queue->getPointCount() - 1,
                                                    offset, value) == kResultOk)
        toEditor.push({queue->getParameterId(), value});
    }
    return true;
  }
  QByteArray state(QString *error) override {
    if (processing) {
      Clock clock;
      clock.rate = context.sampleRate;
      clock.sample = context.projectTimeSamples;
      clock.quarter = context.projectTimeMusic;
      clock.tempo = context.tempo;
      clock.numerator = context.timeSigNumerator;
      clock.denominator = context.timeSigDenominator;
      clock.playing = bool(context.state & ProcessContext::kPlaying);
      clock.offline = buffers.processMode == kOffline;
      process(0, nullptr, nullptr, nullptr, nullptr, nullptr, 0, clock);
    }
    MemoryStream componentState, controllerState;
    if (component->getState(&componentState) != kResultOk) {
      if (error)
        *error = QStringLiteral("音源未能保存当前预设。");
      return {};
    }
    if (controller)
      controller->getState(&controllerState);
    const QVariantMap value{
        {"component",
         QByteArray(componentState.getData(), int(componentState.getSize()))
             .toBase64()},
        {"controller",
         QByteArray(controllerState.getData(), int(controllerState.getSize()))
             .toBase64()}};
    return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
  }
  bool restore(const QByteArray &bytes, QString *error) override {
    if (bytes.isEmpty())
      return true;
    const auto value = QJsonDocument::fromJson(bytes).toVariant().toMap();
    auto data = QByteArray::fromBase64(value["component"].toByteArray());
    if (!value.contains("component")) {
      if (error)
        *error = QStringLiteral("这个预设没有音源状态。");
      return false;
    }
    const bool wasProcessing = processing, wasActive = active;
    if (processing) {
      processor->setProcessing(false);
      processing = false;
    }
    if (active) {
      component->setActive(false);
      active = false;
    }
    MemoryStream stream(data.data(), data.size());
    const bool accepted =
        data.isEmpty() || component->setState(&stream) == kResultOk;
    if (accepted && controller) {
      stream.seek(0, IBStream::kIBSeekSet, nullptr);
      controller->setComponentState(&stream);
      auto own = QByteArray::fromBase64(value["controller"].toByteArray());
      if (!own.isEmpty()) {
        MemoryStream viewState(own.data(), own.size());
        controller->setState(&viewState);
      }
    }
    if (wasActive)
      active = component->setActive(true) == kResultOk;
    if (wasProcessing && active)
      processing = processor->setProcessing(true) == kResultOk;
    if (!accepted && error)
      *error = QStringLiteral("音源无法读取这个预设。");
    return accepted;
  }
  int latency() const override {
    return processor ? int(processor->getLatencySamples()) : 0;
  }
  qint64 tail() const override {
    return processor ? processor->getTailSamples() : 0;
  }
  QString presetName() const override { return description.name; }
  bool openEditor(QWidget *parent, QString *error) override {
    if (editor) {
      editor->show();
      editor->raise();
      editor->activateWindow();
      return true;
    }
    auto view = controller ? controller->createView("editor") : nullptr;
    if (!view) {
      if (error)
        *error = QStringLiteral("这个音源没有独立界面。");
      return false;
    }
    editor = new Vst3Editor(view, parent);
    editor->setWindowTitle(description.name);
    if (!editor->attach(error)) {
      delete editor.data();
      return false;
    }
    if (!uiTimer) {
      uiTimer.reset(new QTimer);
      QObject::connect(uiTimer.get(), &QTimer::timeout, uiTimer.get(),
                       [this] { pumpEditor(); });
    }
    uiTimer->start(40);
    return true;
  }
  bool editorVisible() const override { return editor && editor->isVisible(); }
  void closeEditor() override {
    if (uiTimer)
      uiTimer->stop();
    if (editor)
      delete editor.data();
  }
  void setParameter(quint32 id, double value) override {
    fromEditor.push({id, qBound(0., value, 1.)});
  }
  QVariantList parameters() const override {
    QVariantList result;
    if (!controller)
      return result;
    for (int i = 0; i < qMin(16384, int(controller->getParameterCount()));
         ++i) {
      ParameterInfo info{};
      if (controller->getParameterInfo(i, info) != kResultOk)
        continue;
      const double value = controller->getParamNormalized(info.id);
      String128 display{};
      controller->getParamStringByValue(info.id, value, display);
      result.append(QVariantMap{
          {"id", info.id},
          {"name",
           QString::fromUtf16(reinterpret_cast<const ushort *>(info.title))},
          {"unit",
           QString::fromUtf16(reinterpret_cast<const ushort *>(info.units))},
          {"value", value},
          {"text",
           QString::fromUtf16(reinterpret_cast<const ushort *>(display))},
          {"steps", info.stepCount},
          {"readOnly", bool(info.flags & ParameterInfo::kIsReadOnly)}});
    }
    return result;
  }
  bool takeChanges() override {
    return editing.load() == 0 &&
           (dirty.exchange(false) | bool(restart.exchange(0)));
  }
  void begin() { ++editing; }
  void changed() {
    if (editing > 0)
      --editing;
    dirty = true;
  }
  void restarted(int flags) {
    restart.fetch_or(flags);
    dirty = true;
  }
};
tresult PLUGIN_API Handler::beginEdit(ParamID) {
  owner->begin();
  return kResultOk;
}
tresult PLUGIN_API Handler::performEdit(ParamID id, double value) {
  owner->setParameter(id, value);
  return kResultOk;
}
tresult PLUGIN_API Handler::endEdit(ParamID) {
  owner->changed();
  return kResultOk;
}
tresult PLUGIN_API Handler::restartComponent(int32 flags) {
  owner->restarted(flags);
  return kResultOk;
}
} // namespace
QVariantList scanVst3(const QString &path, QString *error) {
  std::string message;
  auto module = Module::create(path.toUtf8().constData(), message);
  if (!module) {
    if (error)
      *error = QString::fromStdString(message);
    return {};
  }
  QVariantList result;
  for (const auto &item : module->getFactory().classInfos())
    if (item.category() == kVstAudioEffectClass)
      result.append(descriptor(path, item).value());
  return result;
}
std::unique_ptr<Plugin> createVst3(const Descriptor &descriptor,
                                   QString *error) {
  std::unique_ptr<Vst3Plugin> result(new Vst3Plugin(descriptor));
  if (!result->load(error))
    return {};
  return result;
}
} // namespace VstHost
} // namespace Ms
