// SPDX-License-Identifier: GPL-2.0-or-later
#include "bridge.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cstring>
namespace Ms {
namespace VstHost {
namespace {
// The control connection is prepared on one thread and later used by the
// score and retirement threads. Qt 5's Windows pipe reader delivers I/O
// completion to the submitting thread. An explicit event travels with the
// pipe, so a completed read stays observable after a handover.
class ControlPipe final {
  HANDLE handle = INVALID_HANDLE_VALUE;
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);

public:
  ControlPipe() = default;
  ControlPipe(const ControlPipe&) = delete;
  ControlPipe& operator=(const ControlPipe&) = delete;
  ~ControlPipe() {
    close();
    if (ready)
      CloseHandle(ready);
  }

  bool connected() const { return handle != INVALID_HANDLE_VALUE; }

  void close() {
    if (connected()) {
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
    }
  }

  bool connect(const QString& key) {
    if (!ready)
      return false;
    const auto name = QStringLiteral("\\\\.\\pipe\\KumoVst-") + key;
    const auto path = reinterpret_cast<LPCWSTR>(name.utf16());
    QElapsedTimer timer;
    timer.start();
    do {
      handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
      if (connected())
        return true;
      const auto code = GetLastError();
      if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PIPE_BUSY)
        return false;
      QThread::msleep(15);
    } while (timer.elapsed() < 10000);
    return false;
  }

  int transfer(char* data, int size, DWORD timeout, bool write = false) const {
    OVERLAPPED operation{};
    operation.hEvent = ready;
    ResetEvent(ready);
    DWORD transferred = 0;
    const BOOL done = write ? WriteFile(handle, data, DWORD(size), &transferred, &operation)
                            : ReadFile(handle, data, DWORD(size), &transferred, &operation);
    if (done)
      return int(transferred);
    if (GetLastError() != ERROR_IO_PENDING)
      return -1;
    if (WaitForSingleObject(ready, timeout) != WAIT_OBJECT_0)
      CancelIoEx(handle, &operation);
    // Wait for cancellation or completion before releasing the caller's buffer.
    return GetOverlappedResult(handle, &operation, &transferred, TRUE) ? int(transferred) : -1;
  }
};

class Proxy final : public Plugin {
  Descriptor descriptor;
  QProcess child;
  HANDLE job = nullptr;
  ControlPipe pipe;
  BridgeHandles bridge;
  Queue<Parameter, 1024> parametersToAudio;
  bool offline = false;
  double sampleRate = 44100;
  int pluginLatency = 0;
  qint64 pluginTail = 0, serial = 0;
  int fill = 0;
  BridgePacket stage;
  std::array<float, BridgeFrames> outputL{}, outputR{};
  QString preset;
  QVariantList parameterList;
  QByteArray replyBuffer;
  quint32 requestId = 0;
  void markChanged() {
    if (bridge.memory)
      InterlockedExchange(&bridge.memory->dirty, 1);
  }
  QVariantList pendingParameters() {
    QVariantList values;
    Parameter value;
    while (parametersToAudio.pop(value))
      values.append(QVariantMap{{"id", value.id}, {"value", value.value}});
    return values;
  }
  std::atomic<quint64> outputBuffers{0},missingBuffers{0},busyBuffers{0},failedBuffers{0};
  QVariantMap rpc(const QVariantMap &value, QString *error,
                  int timeout = 15000) {
    if (child.state() != QProcess::Running || !pipe.connected()) {
      if (error)
        *error = QStringLiteral("音源进程已关闭，请重新载入音源。");
      return {};
    }
    const auto id = ++requestId;
    auto request = value;
    request["requestId"] = id;
    const auto bytes = QJsonDocument::fromVariant(request).toJson(QJsonDocument::Compact);
    const quint32 size = quint32(bytes.size());
    QByteArray frame(reinterpret_cast<const char*>(&size), sizeof(size));
    frame.append(bytes);
    QElapsedTimer timer;
    timer.start();
    const auto remaining = [&] { return DWORD(qMax<qint64>(0, timeout - timer.elapsed())); };
    int written = 0;
    while (written < frame.size() && remaining()) {
      const int count = pipe.transfer(frame.data() + written, frame.size() - written, remaining(), true);
      if (count <= 0)
        break;
      written += count;
    }
    if (written != frame.size())
      pipe.close();
    std::array<char, 16384> chunk;
    while (written == frame.size() && remaining()) {
      if (replyBuffer.size() >= 4) {
        quint32 length = 0;
        std::memcpy(&length, replyBuffer.constData(), 4);
        if (length > 96u * 1024 * 1024) {
          pipe.close();
          break;
        }
        if (replyBuffer.size() >= qint64(length) + 4) {
          const auto result =
              QJsonDocument::fromJson(replyBuffer.mid(4, length))
                  .toVariant()
                  .toMap();
          replyBuffer.remove(0, 4 + length);
          if (result.value("requestId").toUInt() != id)
            continue;
          if (error && !result["error"].toString().isEmpty())
            *error = result["error"].toString();
          return result;
        }
      }
      const int count = pipe.transfer(chunk.data(), int(chunk.size()), remaining());
      if (count <= 0)
        break;
      replyBuffer.append(chunk.data(), count);
    }
    if (qEnvironmentVariableIsSet("KUMO_VST_TRACE"))
      qWarning("VST control response missing for %s (%lld ms)",
               qPrintable(value.value("op").toString()), timer.elapsed());
    if (error)
      *error = QStringLiteral("音源暂未回应，保留当前设置后可重新载入。");
    return {};
  }
  bool submit(BridgePacket &source, bool wait) {
    auto &packet = bridge.memory->packets[size_t(serial % BridgeSlots)];
    if (InterlockedCompareExchange(&packet.status, 0, 0) == 1) {
      if (!wait)busyBuffers.fetch_add(1,std::memory_order_relaxed);
      return false;
    }
    packet.serial = serial;
    packet.count = source.count;
    packet.eventCount = source.eventCount;
    packet.parameterCount = 0;
    packet.clock = source.clock;
    std::copy_n(source.inL.data(), source.count, packet.inL.data());
    std::copy_n(source.inR.data(), source.count, packet.inR.data());
    std::copy_n(source.events.data(), source.eventCount, packet.events.data());
    Parameter parameter;
    while (packet.parameterCount < BridgeParameters && parametersToAudio.pop(parameter))
      packet.parameters[packet.parameterCount++] = parameter;
    InterlockedExchange(&packet.status, 1);
    SetEvent(bridge.request);
    if (wait) {
      QElapsedTimer timer;
      timer.start();
      while (InterlockedCompareExchange(&packet.status, 0, 0) != 2) {
        if (timer.elapsed() > 15000 ||
            WaitForSingleObject(bridge.done, 1000) == WAIT_FAILED)
          return false;
      }
      std::copy_n(packet.outL.data(), source.count, outputL.data());
      std::copy_n(packet.outR.data(), source.count, outputR.data());
      const bool success = packet.success != 0;
      InterlockedExchange(&packet.status, 0);
      return success;
    }
    return true;
  }

public:
  explicit Proxy(Descriptor value) : descriptor(std::move(value)) {}
  void moveControlTo(QThread* thread) override {
    child.moveToThread(thread);
  }
  ~Proxy() override {
    if (bridge.memory) {
      InterlockedExchange(&bridge.memory->stop, 1);
      SetEvent(bridge.request);
    }
    if (child.state() == QProcess::Running) {
      QString error;
      rpc({{"op", "quit"}}, &error, 2000);
      if (!child.waitForFinished(3000)) {
        child.kill();
        child.waitForFinished(1000);
      }
    }
    if (job)
      CloseHandle(job);
  }
  bool prepare(double rate, int, QString *error, bool value) override {
    sampleRate = rate;
    offline = value;
    const auto key = QUuid::createUuid().toString(QUuid::Id128);
    if (!bridge.open(key, true)) {
      if (error)
        *error = QStringLiteral("无法建立音源音频缓冲。");
      return false;
    }
    const auto path = QCoreApplication::applicationDirPath() +
                      QStringLiteral("/MuseScoreVstHost.exe");
    if (!QFileInfo::exists(path)) {
      if (error)
        *error = QStringLiteral(
            "程序目录缺少 MuseScoreVstHost.exe，请重新安装随包工具。");
      return false;
    }
    child.setProgram(path);
    child.setArguments({"--worker", key});
    child.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    child.start();
    if (!child.waitForStarted(10000)) {
      if (error)
        *error = child.errorString();
      return false;
    }
    job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags =
          JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                              sizeof(limits));
      auto process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE |
                                     PROCESS_QUERY_INFORMATION,
                                 FALSE, DWORD(child.processId()));
      if (process) {
        AssignProcessToJobObject(job, process);
        CloseHandle(process);
      }
    }
    if (!pipe.connect(key)) {
      if (error)
        *error = QStringLiteral("音源进程启动后未建立连接。");
      return false;
    }
    const auto result = rpc({{"op", "prepare"},
                             {"plugin", descriptor.value()},
                             {"rate", rate},
                             {"offline", offline}},
                            error, 60000);
    pluginLatency = result["latency"].toInt();
    pluginTail = result["tail"].toLongLong();
    preset = result["preset"].toString();
    parameterList = result["parameters"].toList();
    return result["ok"].toBool();
  }
  bool resetAudio() override {
    QString error;
    const bool ok = rpc({{"op", "resetAudio"}, {"parameters", pendingParameters()},
                         {"rate", sampleRate}}, &error)["ok"].toBool();
    fill = 0;
    serial = 0;
    stage.count = stage.eventCount = stage.parameterCount = 0;
    outputL.fill(0);
    outputR.fill(0);
    return ok;
  }
  bool restore(const QByteArray &value, QString *error) override {
    const bool ok = rpc({{"op", "restore"},
                {"state", QString::fromLatin1(value.toBase64())}},
               error, 60000)["ok"]
        .toBool();
    if (ok)
      markChanged();
    return ok;
  }
  QByteArray state(QString *error) override {
    const bool cfx = (descriptor.name + descriptor.path).contains("CFX", Qt::CaseInsensitive);
    const auto result = rpc({{"op", "state"}, {"parameters", pendingParameters()}}, error,
                            cfx && !offline ? 1000 : 60000);
    return QByteArray::fromBase64(result.value("state").toByteArray());
  }
  int latency() const override {
    return pluginLatency + (offline ? 0 : BridgeFrames * 2);
  }
  qint64 tail() const override { return pluginTail; }
  QString presetName() const override { return preset; }
  bool openEditor(QWidget *, QString *error) override {
    const bool opened = rpc({{"op", "editor"}}, error)["ok"].toBool();
    if (opened && bridge.memory)
      InterlockedExchange(&bridge.memory->editorOpen, 1);
    return opened;
  }
  bool editorVisible() const override {
    return child.state() == QProcess::Running && bridge.memory &&
           InterlockedCompareExchange(&bridge.memory->editorOpen, 0, 0) != 0;
  }
  void closeEditor() override {
    QString error;
    if (rpc({{"op", "closeEditor"}}, &error).value("ok").toBool() && bridge.memory)
      InterlockedExchange(&bridge.memory->editorOpen, 0);
  }
  bool importPreset(const QString &path, QString *error) override {
    const bool ok = rpc({{"op", "import"}, {"path", path}}, error, 60000)["ok"].toBool();
    if (ok)
      markChanged();
    return ok;
  }
  void setParameter(quint32 id, double value) override {
    if (parametersToAudio.push({id, value}))
      markChanged();
  }
  QVariantList parameters() const override {
    QString error;
    return const_cast<Proxy *>(this)
        ->rpc({{"op", "parameters"}}, &error)["parameters"]
        .toList();
  }
  QString status() const override {
    return child.state() == QProcess::NotRunning
               ? QStringLiteral("音源进程已关闭，重新选择此音源即可载入。")
               : QString();
  }
  bool takeChanges() override {
    return bridge.memory && InterlockedExchange(&bridge.memory->dirty, 0) != 0;
  }
  QVariantMap processingDiagnostics() const override {
    return {{"outputBuffers",QVariant::fromValue(outputBuffers.load(std::memory_order_relaxed))},
            {"missingOutputBuffers",QVariant::fromValue(missingBuffers.load(std::memory_order_relaxed))},
            {"busyInputBuffers",QVariant::fromValue(busyBuffers.load(std::memory_order_relaxed))},
            {"failedOutputBuffers",QVariant::fromValue(failedBuffers.load(std::memory_order_relaxed))},
            {"bridgeLatencySamples",BridgeFrames*2}};
  }
  bool process(int count, const float *l, const float *r, float *outL,
               float *outR, const Midi *events, int eventCount,
               const Clock &clock) override {
    if (!bridge.memory)
      return false;
    if (offline) {
      for (int at = 0; at < count; at += BridgeFrames) {
        const int n = std::min(BridgeFrames, count - at);
        stage.count = n;
        stage.clock = clock;
        stage.clock.sample += at;
        stage.clock.quarter += at / clock.rate * clock.tempo / 60;
        stage.eventCount = 0;
        for (int e = 0; e < eventCount; ++e)
          if (events[e].offset >= at && events[e].offset < at + n &&
              stage.eventCount < BridgeMidi) {
            stage.events[stage.eventCount] = events[e];
            stage.events[stage.eventCount++].offset -= at;
          }
        if (l)
          std::copy_n(l + at, n, stage.inL.data());
        else
          std::fill_n(stage.inL.data(), n, 0);
        if (r)
          std::copy_n(r + at, n, stage.inR.data());
        else
          std::fill_n(stage.inR.data(), n, 0);
        if (!submit(stage, true))
          return false;
        std::copy_n(outputL.data(), n, outL + at);
        std::copy_n(outputR.data(), n, outR + at);
        ++serial;
      }
      return true;
    }
    int at = 0;
    while (at < count) {
      if (!fill) {
        stage.clock = clock;
        stage.clock.sample += at;
        stage.clock.quarter += at / clock.rate * clock.tempo / 60;
        stage.eventCount = 0;
        outputL.fill(0);
        outputR.fill(0);
        if (serial >= 2) {
          outputBuffers.fetch_add(1,std::memory_order_relaxed);
          auto &packet =
              bridge.memory->packets[size_t((serial - 2) % BridgeSlots)];
          if (InterlockedCompareExchange(&packet.status, 0, 0) == 2 &&
              packet.serial == serial - 2) {
            std::copy_n(packet.outL.data(), BridgeFrames, outputL.data());
            std::copy_n(packet.outR.data(), BridgeFrames, outputR.data());
            if(!packet.success)failedBuffers.fetch_add(1,std::memory_order_relaxed);
            InterlockedExchange(&packet.status, 0);
          } else missingBuffers.fetch_add(1,std::memory_order_relaxed);
        }
      }
      const int n = std::min(count - at, BridgeFrames - fill);
      if (l)
        std::copy_n(l + at, n, stage.inL.data() + fill);
      else
        std::fill_n(stage.inL.data() + fill, n, 0);
      if (r)
        std::copy_n(r + at, n, stage.inR.data() + fill);
      else
        std::fill_n(stage.inR.data() + fill, n, 0);
      std::copy_n(outputL.data() + fill, n, outL + at);
      std::copy_n(outputR.data() + fill, n, outR + at);
      for (int e = 0; e < eventCount; ++e)
        if (events[e].offset >= at && events[e].offset < at + n &&
            stage.eventCount < BridgeMidi) {
          stage.events[stage.eventCount] = events[e];
          stage.events[stage.eventCount++].offset += fill - at;
        }
      fill += n;
      at += n;
      if (fill == BridgeFrames) {
        stage.count = fill;
        submit(stage, false);
        fill = 0;
        ++serial;
      }
    }
    return true;
  }
};
} // namespace
std::unique_ptr<Plugin> createIsolated(const Descriptor &value) {
  return std::make_unique<Proxy>(value);
}
} // namespace VstHost
} // namespace Ms
