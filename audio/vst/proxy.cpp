// SPDX-License-Identifier: GPL-2.0-or-later
#include "bridge.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QUuid>
#include <algorithm>
#include <cstring>
namespace Ms {
namespace VstHost {
namespace {
class Proxy final : public Plugin {
  Descriptor descriptor;
  QProcess child;
  HANDLE job = nullptr;
  QLocalSocket socket;
  BridgeHandles bridge;
  Queue<Parameter, 1024> parametersToAudio;
  bool offline = false;
  int pluginLatency = 0;
  qint64 pluginTail = 0, serial = 0;
  int fill = 0;
  BridgePacket stage;
  std::array<float, BridgeFrames> outputL{}, outputR{};
  QString preset;
  QVariantList parameterList;
  QByteArray replyBuffer;
  QVariantMap rpc(const QVariantMap &value, QString *error,
                  int timeout = 15000) {
    if (child.state() != QProcess::Running ||
        socket.state() != QLocalSocket::ConnectedState) {
      if (error)
        *error = QStringLiteral("音源进程已关闭，请重新载入音源。");
      return {};
    }
    QByteArray bytes =
        QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
    quint32 size = quint32(bytes.size());
    socket.write(reinterpret_cast<const char *>(&size), 4);
    socket.write(bytes);
    socket.flush();
    if (socket.bytesToWrite())
      socket.waitForBytesWritten(timeout);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout) {
      replyBuffer += socket.readAll();
      if (replyBuffer.size() >= 4) {
        quint32 length = 0;
        std::memcpy(&length, replyBuffer.constData(), 4);
        if (length > 96u * 1024 * 1024)
          break;
        if (replyBuffer.size() >= qint64(length) + 4) {
          const auto result =
              QJsonDocument::fromJson(replyBuffer.mid(4, length))
                  .toVariant()
                  .toMap();
          replyBuffer.remove(0, 4 + length);
          if (error && !result["error"].toString().isEmpty())
            *error = result["error"].toString();
          return result;
        }
      }
      if (!socket.waitForReadyRead(
              std::max(1, timeout - int(timer.elapsed())))) {
        if (qEnvironmentVariableIsSet("KUMO_VST_TRACE"))
          qWarning("VST control read: %s; bytes %d",
                   qPrintable(socket.errorString()), replyBuffer.size());
        break;
      }
    }
    if (error)
      *error = QStringLiteral("音源暂未回应，保留当前设置后可重新载入。");
    return {};
  }
  bool submit(BridgePacket &source, bool wait) {
    auto &packet = bridge.memory->packets[size_t(serial % BridgeSlots)];
    if (InterlockedCompareExchange(&packet.status, 0, 0) == 1)
      return false;
    packet.serial = serial;
    packet.count = source.count;
    packet.eventCount = source.eventCount;
    packet.parameterCount = 0;
    packet.clock = source.clock;
    std::copy_n(source.inL.data(), source.count, packet.inL.data());
    std::copy_n(source.inR.data(), source.count, packet.inR.data());
    std::copy_n(source.events.data(), source.eventCount, packet.events.data());
    Parameter parameter;
    while (packet.parameterCount < BridgeParameters &&
           parametersToAudio.pop(parameter))
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
    socket.connectToServer(QStringLiteral("KumoVst-") + key);
    QElapsedTimer timer;
    timer.start();
    while (!socket.waitForConnected(100) && timer.elapsed() < 10000) {
      socket.abort();
      socket.connectToServer(QStringLiteral("KumoVst-") + key);
    }
    if (socket.state() != QLocalSocket::ConnectedState) {
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
  bool restore(const QByteArray &value, QString *error) override {
    return rpc({{"op", "restore"},
                {"state", QString::fromLatin1(value.toBase64())}},
               error, 60000)["ok"]
        .toBool();
  }
  QByteArray state(QString *error) override {
    QVariantList values;
    Parameter p;
    while (parametersToAudio.pop(p))
      values.append(QVariantMap{{"id", p.id}, {"value", p.value}});
    return QByteArray::fromBase64(
        rpc({{"op", "state"}, {"parameters", values}}, error, 60000)["state"]
            .toByteArray());
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
    rpc({{"op", "closeEditor"}}, &error);
  }
  bool importPreset(const QString &path, QString *error) override {
    return rpc({{"op", "import"}, {"path", path}}, error, 60000)["ok"].toBool();
  }
  void setParameter(quint32 id, double value) override {
    parametersToAudio.push({id, value});
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
          auto &packet =
              bridge.memory->packets[size_t((serial - 2) % BridgeSlots)];
          if (InterlockedCompareExchange(&packet.status, 0, 0) == 2 &&
              packet.serial == serial - 2) {
            std::copy_n(packet.outL.data(), BridgeFrames, outputL.data());
            std::copy_n(packet.outR.data(), BridgeFrames, outputR.data());
            InterlockedExchange(&packet.status, 0);
          }
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
