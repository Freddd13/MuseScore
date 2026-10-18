// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin.h"
#include "thirdparty/kumo-vestige/aeffectx.h"
#include "vst2editor.h"
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <windows.h>

namespace Ms {
namespace VstHost {
namespace {
class Vst2Plugin;
thread_local Vst2Plugin *creating = nullptr;
class Vst2Plugin final : public Plugin {
  Descriptor description;
  HMODULE module = nullptr;
  AEffect *effect = nullptr;
  QByteArray folder;
  int maximum = 0;
  bool opened = false, active = false, editorAttached = false, closing = false;
  std::vector<std::vector<float>> inputBuffers, outputBuffers;
  std::vector<float *> inputs, outputs;
  Queue<Parameter, 1024> changes;
  std::array<VstMidiEvent, 2048> midi;
  struct Packet {
    int32_t count = 0;
    void *reserved = nullptr;
    VstEvent *events[2048];
  } packet;
  QPointer<Vst2Editor> editor;
  std::atomic<double> rate{44100}, quarter{0}, tempo{120};
  std::atomic<qint64> sample{0};
  std::atomic<int> numerator{4}, denominator{4}, editing{0};
  std::atomic<bool> playing{false}, dirty{false}, offline{false};
  intptr_t dispatch(int code, int index = 0, intptr_t value = 0,
                    void *pointer = nullptr, float number = 0.f) const {
    return effect->dispatcher(effect, code, index, value, pointer, number);
  }
  static intptr_t VST_CALL_CONV callback(AEffect *plugin, int32_t code,
                                         int32_t index, intptr_t value,
                                         void *pointer, float number) {
    auto owner = creating ? creating
                 : plugin ? static_cast<Vst2Plugin *>(plugin->user)
                          : nullptr;
    if (code == audioMasterVersion)
      return 2400;
    if (!owner)
      return 0;
    switch (code) {
    case audioMasterCurrentId:
      return owner->description.uid.toUInt(nullptr, 16);
    case audioMasterGetSampleRate:
      return intptr_t(owner->rate.load());
    case audioMasterGetBlockSize:
      return owner->maximum ? owner->maximum : 256;
    case audioMasterGetDirectory:
      return reinterpret_cast<intptr_t>(owner->folder.constData());
    case audioMasterGetTime: {
      thread_local VstTimeInfo clock{};
      clock.samplePos = double(owner->sample.load());
      clock.sampleRate = owner->rate.load();
      clock.tempo = owner->tempo.load();
      clock.ppqPos = owner->quarter.load();
      clock.timeSigNumerator = owner->numerator.load();
      clock.timeSigDenominator = owner->denominator.load();
      clock.barStartPos =
          std::floor(clock.ppqPos /
                     (clock.timeSigNumerator * 4. / clock.timeSigDenominator)) *
          (clock.timeSigNumerator * 4. / clock.timeSigDenominator);
      clock.flags = kVstTempoValid | kVstPpqPosValid | kVstTimeSigValid |
                    kVstBarsValid |
                    (owner->playing.load() ? kVstTransportPlaying : 0);
      return reinterpret_cast<intptr_t>(&clock);
    }
    case audioMasterGetVendorString:
      if (pointer)
        std::strcpy(static_cast<char *>(pointer), "Freddd13");
      return 1;
    case audioMasterGetProductString:
      if (pointer)
        std::strcpy(static_cast<char *>(pointer), "MuseScore Evolution Kumo");
      return 1;
    case audioMasterGetVendorVersion:
      return 1;
    case audioMasterGetLanguage:
      return kVstLangEnglish;
    case audioMasterGetCurrentProcessLevel:
      return owner->offline.load() ? 4 : 2;
    case audioMasterCanDo: {
      const auto request = pointer ? static_cast<const char *>(pointer) : "";
      return !std::strcmp(request, "sendVstEvents") ||
                     !std::strcmp(request, "sendVstMidiEvent") ||
                     !std::strcmp(request, "sendVstTimeInfo") ||
                     !std::strcmp(request, "sizeWindow")
                 ? 1
                 : 0;
    }
    case audioMasterWantMidi:
      return 1;
    case audioMasterIOChanged:
      owner->dirty = true;
      return 1;
    case audioMasterSizeWindow:
      if (owner->editor)
        owner->editor->setPluginSize(QSize(index, int(value)));
      return 1;
    case audioMasterBeginEdit:
      ++owner->editing;
      return 1;
    case audioMasterEndEdit:
      if (owner->editing > 0)
        --owner->editing;
      owner->dirty = true;
      return 1;
    case audioMasterAutomate:
      owner->dirty = true;
      return 1;
    case audioMasterUpdateDisplay:
      owner->dirty = true;
      return 1;
    default:
      return 0;
    }
  }
  void notify() {
    if (!closing && stateChanged && takeChanges())
      stateChanged();
  }
  void detachEditor() {
    if (editorAttached) {
      editorAttached = false;
      dispatch(effEditClose);
    }
    notify();
  }

public:
  explicit Vst2Plugin(const Descriptor &value)
      : description(value),
        folder(QFileInfo(value.path).absolutePath().toUtf8()) {}
  bool load(QString *error) {
    auto trace = [](const char *text) {
      if (qEnvironmentVariableIsSet("KUMO_VST_TRACE")) {
        std::fprintf(stderr, "VST2 %s\n", text);
        std::fflush(stderr);
      }
    };
    trace("load");
    const auto path = QFileInfo(description.path).absoluteFilePath();
    module = LoadLibraryExW(reinterpret_cast<const wchar_t *>(path.utf16()),
                            nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    trace("library loaded");
    if (!module) {
      if (error)
        *error =
            GetLastError() == ERROR_BAD_EXE_FORMAT
                ? QStringLiteral("请选择 64 位的 VST 插件。")
                : QStringLiteral("音源文件或它的运行文件无法加载：") + path;
      return false;
    }
    using Entry = AEffect *(VST_CALL_CONV *)(audioMasterCallback);
    auto entry =
        reinterpret_cast<Entry>(GetProcAddress(module, "VSTPluginMain"));
    if (!entry)
      entry = reinterpret_cast<Entry>(GetProcAddress(module, "main"));
    if (!entry) {
      if (error)
        *error = QStringLiteral("这个文件未提供 VST2 入口。");
      return false;
    }
    trace("factory entry");
    creating = this;
    effect = entry(callback);
    creating = nullptr;
    trace("factory done");
    if (!effect || effect->magic != kEffectMagic || !effect->dispatcher) {
      effect = nullptr;
      if (error)
        *error = QStringLiteral("这个文件未提供有效的 VST2 音源。");
      return false;
    }
    effect->user = this;
    trace("open");
    dispatch(effOpen);
    opened = true;
    trace("opened");
    if (!description.uid.isEmpty() &&
        description.uid.toUInt(nullptr, 16) != quint32(effect->uniqueID)) {
      if (error)
        *error = QStringLiteral("这个文件与原来选择的音源不同，请重新选择。");
      return false;
    }
    if (description.name.isEmpty()) {
      char text[256]{};
      dispatch(effGetEffectName, 0, 0, text);
      description.name = QString::fromUtf8(text);
    }
    return true;
  }
  ~Vst2Plugin() override {
    closing = true;
    closeEditor();
    if (effect) {
      if (active) {
        dispatch(72);
        dispatch(effMainsChanged, 0, 0);
      }
      if (opened)
        dispatch(effClose);
      effect = nullptr;
    }
    if (module)
      FreeLibrary(module);
  }
  Descriptor identity() const {
    auto value = description;
    char vendor[256]{};
    dispatch(effGetVendorString, 0, 0, vendor);
    value.vendor = QString::fromUtf8(vendor);
    value.uid = QString::number(quint32(effect->uniqueID), 16);
    value.format = "VST2";
    value.instrument = bool(effect->flags & effFlagsIsSynth);
    return value;
  }
  bool prepare(double sampleRate, int block, QString *error,
               bool exporting) override {
    if (!effect || block < 1 || block > 8192 || sampleRate < 8000)
      return false;
    if (active) {
      dispatch(72);
      dispatch(effMainsChanged, 0, 0);
      active = false;
    }
    if (effect->numInputs < 0 || effect->numInputs > 32 ||
        effect->numOutputs < 1 || effect->numOutputs > 32 ||
        (!effect->processReplacing && !effect->process)) {
      if (error)
        *error = QStringLiteral("这个插件的声音通道暂时无法用于当前输出。");
      return false;
    }
    maximum = block;
    rate = sampleRate;
    offline = exporting;
    inputBuffers.assign(effect->numInputs, std::vector<float>(block));
    outputBuffers.assign(effect->numOutputs, std::vector<float>(block));
    inputs.clear();
    outputs.clear();
    for (auto &row : inputBuffers)
      inputs.push_back(row.data());
    for (auto &row : outputBuffers)
      outputs.push_back(row.data());
    dispatch(effSetSampleRate, 0, 0, nullptr, float(sampleRate));
    dispatch(effSetBlockSize, 0, block);
    dispatch(77, 0, 0);
    dispatch(effMainsChanged, 0, 1);
    dispatch(71);
    active = true;
    return true;
  }
  bool process(int count, const float *left, const float *right, float *outLeft,
               float *outRight, const Midi *events, int length,
               const Clock &clock) override {
    if (!active || count < 0 || count > maximum || length > int(midi.size()))
      return false;
    rate = clock.rate;
    sample = clock.sample;
    quarter = clock.quarter;
    tempo = clock.tempo;
    numerator = clock.numerator;
    denominator = clock.denominator;
    playing = clock.playing;
    offline = clock.offline;
    Parameter parameter;
    while (changes.pop(parameter))
      if (parameter.id < quint32(effect->numParams))
        effect->setParameter(effect, int(parameter.id), float(parameter.value));
    for (int channel = 0; channel < int(inputBuffers.size()); ++channel) {
      if (inputBuffers.size() == 1 && left && right)
        for (int i = 0; i < count; ++i)
          inputBuffers[channel][i] = (left[i] + right[i]) * .5f;
      else {
        const auto source = channel == 0   ? left
                            : channel == 1 ? right
                                           : nullptr;
        if (source)
          std::copy_n(source, count, inputBuffers[channel].data());
        else
          std::fill_n(inputBuffers[channel].data(), count, 0.f);
      }
    }
    for (auto &row : outputBuffers)
      std::fill_n(row.data(), count, 0.f);
    packet.count = length;
    for (int i = 0; i < length; ++i) {
      auto &value = midi[i];
      value = {};
      value.type = kVstMidiType;
      value.byteSize = sizeof(VstMidiEvent);
      value.deltaFrames = events[i].offset;
      value.midiData[0] = char(events[i].status);
      value.midiData[1] = char(events[i].first);
      value.midiData[2] = char(events[i].second);
      value.detune = char(qBound(-64, qRound(events[i].tuning), 63));
      packet.events[i] = reinterpret_cast<VstEvent *>(&value);
    }
    if (length)
      dispatch(effProcessEvents, 0, 0, &packet);
    if (count) {
      if (effect->processReplacing)
        effect->processReplacing(effect, inputs.data(), outputs.data(), count);
      else
        effect->process(effect, inputs.data(), outputs.data(), count);
      std::copy_n(outputs[0], count, outLeft);
      std::copy_n(outputs[qMin(1, effect->numOutputs - 1)], count, outRight);
    }
    return true;
  }
  QByteArray state(QString *error) override {
    Parameter change;
    while (changes.pop(change))
      if (change.id < quint32(effect->numParams))
        effect->setParameter(effect, int(change.id), float(change.value));
    QVariantMap value;
    void *chunk = nullptr;
    const auto size = dispatch(effGetChunk, 0, 0, &chunk);
    if (size > 64 * 1024 * 1024) {
      if (error)
        *error = QStringLiteral("音源状态大于 64 MiB，请在插件中另存预设。");
      return {};
    }
    if (chunk && size > 0)
      value["chunk"] =
          QByteArray(static_cast<const char *>(chunk), int(size)).toBase64();
    else {
      QVariantList values;
      for (int i = 0; i < effect->numParams; ++i)
        values.append(effect->getParameter(effect, i));
      value["parameters"] = values;
      if (effect->numPrograms > 0)
        value["program"] = int(dispatch(effGetProgram));
    }
    return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
  }
  bool restore(const QByteArray &bytes, QString *error) override {
    if (bytes.isEmpty())
      return true;
    const auto value = QJsonDocument::fromJson(bytes).toVariant().toMap();
    if (value.contains("chunk")) {
      const auto chunk = QByteArray::fromBase64(value["chunk"].toByteArray());
      if (chunk.isEmpty()) {
        if (error)
          *error = QStringLiteral("预设的声音数据为空。");
        return false;
      }
      dispatch(effSetChunk, 0, chunk.size(),
               const_cast<char *>(chunk.constData()));
    } else if (value.contains("parameters")) {
      if (value.contains("program"))
        dispatch(effSetProgram, 0, value["program"].toInt());
      const auto values = value["parameters"].toList();
      for (int i = 0; i < qMin(effect->numParams, values.size()); ++i)
        effect->setParameter(effect, i, float(values[i].toDouble()));
    } else {
      if (error)
        *error = QStringLiteral("这个文件没有可用的 VST2 预设。");
      return false;
    }
    return true;
  }
  bool importPreset(const QString &path, QString *error) override {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      if (error)
        *error = QStringLiteral("预设文件无法读取：") + path;
      return false;
    }
    if (file.size() > 64 * 1024 * 1024) {
      if (error)
        *error = QStringLiteral("请在插件界面中打开这个大预设。");
      return false;
    }
    const auto bytes = file.readAll();
    if (QFileInfo(path).suffix().compare("ariax", Qt::CaseInsensitive) == 0 &&
        description.vendor.contains("Garritan", Qt::CaseInsensitive)) {
      if (!bytes.contains("<AriaSave")) {
        if (error)
          *error = QStringLiteral("请选择 Garritan 的 ARIA 预设。");
        return false;
      }
      void *original = nullptr;
      const auto length = dispatch(effGetChunk, 0, 0, &original);
      if (!original || length < 8 ||
          QByteArray(static_cast<const char *>(original), 4) != "CEGP") {
        if (error)
          *error = QStringLiteral("请从 CFX 自带界面打开这个预设。");
        return false;
      }
      QByteArray chunk("CEGP", 4);
      QDataStream stream(&chunk, QIODevice::Append);
      stream.setByteOrder(QDataStream::LittleEndian);
      stream << quint32(bytes.size());
      chunk += qCompress(bytes).mid(4);
      dispatch(effSetChunk, 0, chunk.size(), chunk.data());
      return true;
    }
    if (bytes.left(4) == "CcnK" &&
        (bytes.mid(8, 4) == "FPCh" || bytes.mid(8, 4) == "FBCh")) {
      const bool bank = bytes.mid(8, 4) == "FBCh";
      const int header = bank ? 160 : 60;
      if (bytes.size() < header) {
        if (error)
          *error = QStringLiteral("这个 VST2 预设尚未保存完整。");
        return false;
      }
      QDataStream stream(bytes.mid(header - 4, 4));
      quint32 size;
      stream >> size;
      if (size > quint32(bytes.size() - header)) {
        if (error)
          *error = QStringLiteral("预设声音数据不完整。");
        return false;
      }
      dispatch(effSetChunk, bank ? 0 : 1, size,
               const_cast<char *>(bytes.constData() + header));
      return true;
    }
    return restore(bytes, error);
  }
  int latency() const override {
    int32_t value = 0;
    if (effect)
      std::memcpy(&value, effect->empty3, 4);
    return qMax(0, int(value));
  }
  qint64 tail() const override {
    return effect ? qMax<qint64>(0, dispatch(52)) : 0;
  }
  QString presetName() const override {
    if (effect && effect->numPrograms > 0) {
      char name[256]{};
      dispatch(effGetProgramName, 0, 0, name);
      return QString::fromUtf8(name);
    }
    return description.name;
  }
  bool openEditor(QWidget *parent, QString *error) override {
    if (editor) {
      editor->show();
      editor->raise();
      editor->activateWindow();
      return true;
    }
    if (!(effect->flags & effFlagsHasEditor)) {
      if (error)
        *error = QStringLiteral("这个音源没有独立界面。");
      return false;
    }
    editor = new Vst2Editor(
        parent,
        [this] {
          if (editorAttached) {
            dispatch(effEditIdle);
            notify();
          }
        },
        [this] { detachEditor(); });
    editor->setWindowTitle(description.name);
    dispatch(effEditOpen, 0, 0, reinterpret_cast<void *>(editor->winId()));
    editorAttached = true;
    struct Rect {
      int16_t top, left, bottom, right;
    };
    Rect *rect = nullptr;
    dispatch(effEditGetRect, 0, 0, &rect);
    if (!rect || rect->right <= rect->left || rect->bottom <= rect->top) {
      closeEditor();
      if (error)
        *error = QStringLiteral("插件界面尚未提供尺寸，请重试。");
      return false;
    }
    editor->setPluginSize(
        QSize(rect->right - rect->left, rect->bottom - rect->top));
    editor->show();
    return true;
  }
  void closeEditor() override {
    if (editor) {
      auto window = editor.data();
      editor = nullptr;
      window->disableCallbacks();
      detachEditor();
      delete window;
    }
  }
  void setParameter(quint32 id, double value) override {
    changes.push({id, qBound(0., value, 1.)});
  }
  QVariantList parameters() const override {
    QVariantList result;
    for (int i = 0; i < qMin(16384, effect->numParams); ++i) {
      char name[256]{}, unit[256]{}, display[256]{};
      dispatch(effGetParamName, i, 0, name);
      dispatch(effGetParamLabel, i, 0, unit);
      dispatch(effGetParamDisplay, i, 0, display);
      result.append(QVariantMap{{"id", i},
                                {"name", QString::fromUtf8(name)},
                                {"unit", QString::fromUtf8(unit)},
                                {"text", QString::fromUtf8(display)},
                                {"value", effect->getParameter(effect, i)},
                                {"readOnly", false}});
    }
    return result;
  }
  bool takeChanges() override {
    return editing.load() == 0 && dirty.exchange(false);
  }
};
} // namespace
QVariantList scanVst2(const QString &path, QString *error) {
  Descriptor descriptor;
  descriptor.path = path;
  descriptor.format = "VST2";
  Vst2Plugin plugin(descriptor);
  if (!plugin.load(error))
    return {};
  return {plugin.identity().value()};
}
std::unique_ptr<Plugin> createVst2(const Descriptor &descriptor,
                                   QString *error) {
  std::unique_ptr<Vst2Plugin> result(new Vst2Plugin(descriptor));
  if (!result->load(error))
    return {};
  return result;
}
} // namespace VstHost
} // namespace Ms
