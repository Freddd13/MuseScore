// SPDX-License-Identifier: GPL-2.0-or-later
#include "bridge.h"
#include <QApplication>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
using namespace Ms::VstHost;
int main(int argc, char **argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);
  QApplication app(argc, argv);
  QApplication::setQuitOnLastWindowClosed(false);
  const auto args = app.arguments();
  if (args.contains("--scan")) {
    QString error;
    const auto index = args.indexOf("--scan");
    if (index + 1 >= args.size())
      return 2;
    const auto path = args[index + 1];
    const auto list = path.endsWith(".vst3", Qt::CaseInsensitive)
                          ? scanVst3(path, &error)
                          : scanVst2(path, &error);
    const auto result = QJsonDocument::fromVariant(
                            QVariantMap{{"plugins", list}, {"error", error}})
                            .toJson(QJsonDocument::Compact);
    std::fwrite(result.constData(), 1, result.size(), stdout);
    std::fflush(stdout);
    return error.isEmpty() ? 0 : 1;
  }
  const auto index = args.indexOf("--worker");
  if (index < 0 || index + 1 >= args.size())
    return 2;
  const auto key = args[index + 1];
  if (qEnvironmentVariableIsSet("KUMO_VST_TRACE")) {
    std::fprintf(stderr, "worker start %s\n", qPrintable(key));
    std::fflush(stderr);
  }
  BridgeHandles bridge;
  if (!bridge.open(key, false))
    return 3;
  std::unique_ptr<Plugin> plugin;
  std::atomic<bool> audioBusy{false};
  std::mutex pluginMutex;
  std::thread audio([&] {
    while (!InterlockedCompareExchange(&bridge.memory->stop, 0, 0)) {
      WaitForSingleObject(bridge.request, 500);
      if (InterlockedCompareExchange(&bridge.memory->stop, 0, 0))
        break;
      for (;;) {
        BridgePacket *packet = nullptr;
        for (auto &p : bridge.memory->packets)
          if (InterlockedCompareExchange(&p.status, 0, 0) == 1 &&
              (!packet || p.serial < packet->serial))
            packet = &p;
        if (!packet)
          break;
        std::lock_guard<std::mutex> guard(pluginMutex);
        // A control-thread reset may cancel a packet selected before this lock.
        if (InterlockedCompareExchange(&packet->status, 0, 0) != 1)
          continue;
        audioBusy = true;
        if (plugin && packet->count > 0 && packet->count <= BridgeFrames &&
            packet->eventCount >= 0 && packet->eventCount <= BridgeMidi &&
            packet->parameterCount >= 0 &&
            packet->parameterCount <= BridgeParameters) {
          for (int i = 0; i < packet->parameterCount; ++i)
            plugin->setParameter(packet->parameters[i].id,
                                 packet->parameters[i].value);
          packet->success = plugin->process(
              packet->count, packet->inL.data(), packet->inR.data(),
              packet->outL.data(), packet->outR.data(), packet->events.data(),
              packet->eventCount, packet->clock);
        } else
          packet->success = 0;
        if (!packet->success) {
          packet->outL.fill(0);
          packet->outR.fill(0);
        }
        audioBusy = false;
        InterlockedExchange(&packet->status, 2);
        SetEvent(bridge.done);
      }
    }
  });
  QLocalServer server;
  server.setSocketOptions(QLocalServer::UserAccessOption);
  QLocalSocket *client = nullptr;
  QByteArray buffer;
  QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
    if (client) {
      auto extra = server.nextPendingConnection();
      extra->disconnectFromServer();
      extra->deleteLater();
      return;
    }
    client = server.nextPendingConnection();
    if (qEnvironmentVariableIsSet("KUMO_VST_TRACE")) {
      std::fprintf(stderr, "client connected %lld\n", client->bytesAvailable());
      std::fflush(stderr);
    }
    QObject::connect(client, &QLocalSocket::disconnected, &app,
                     &QCoreApplication::quit);
    QObject::connect(client, &QLocalSocket::readyRead, &app, [&] {
      buffer += client->readAll();
      if (qEnvironmentVariableIsSet("KUMO_VST_TRACE")) {
        std::fprintf(stderr, "read %d\n", buffer.size());
        std::fflush(stderr);
      }
      while (buffer.size() >= 4) {
        quint32 length = 0;
        std::memcpy(&length, buffer.constData(), 4);
        if (length > 96u * 1024 * 1024) {
          app.quit();
          return;
        }
        if (buffer.size() < qint64(length) + 4)
          return;
        const auto command =
            QJsonDocument::fromJson(buffer.mid(4, length)).toVariant().toMap();
        buffer.remove(0, 4 + length);
        // The caller pauses submission before state and lifecycle commands.
        while (audioBusy.load(std::memory_order_acquire))
          std::this_thread::yield();
        std::lock_guard<std::mutex> guard(pluginMutex);
        QVariantMap result;
        QString error;
        const auto op = command["op"].toString();
        if (qEnvironmentVariableIsSet("KUMO_VST_TRACE")) {
          std::fprintf(stderr, "request %s\n", qPrintable(op));
          std::fflush(stderr);
        }
        if (op == "prepare") {
          if (qEnvironmentVariableIsSet("KUMO_VST_TRACE")) {
            std::fprintf(stderr, "create start\n");
            std::fflush(stderr);
          }
          plugin =
              create(Descriptor::fromValue(command["plugin"].toMap()), &error);
          if (qEnvironmentVariableIsSet("KUMO_VST_TRACE")) {
            std::fprintf(stderr, "create done\n");
            std::fflush(stderr);
          }
          const bool ok =
              plugin &&
              plugin->prepare(command["rate"].toDouble(), BridgeFrames, &error,
                              command["offline"].toBool());
          result = {{"ok", ok},
                    {"latency", ok ? plugin->latency() : 0},
                    {"tail", ok ? plugin->tail() : 0},
                    {"preset", ok ? plugin->presetName() : QString()}};
        } else if (op == "quit") {
          result["ok"] = true;
          app.quit();
        } else if (plugin) {
          if (op == "state") {
            for (const auto &v : command["parameters"].toList()) {
              const auto p = v.toMap();
              plugin->setParameter(p["id"].toUInt(), p["value"].toDouble());
            }
            result["state"] =
                QString::fromLatin1(plugin->state(&error).toBase64());
          } else if (op == "resetAudio") {
            for (auto &packet : bridge.memory->packets) {
              if (InterlockedCompareExchange(&packet.status, 0, 0) == 1)
                for (int i = 0; i < packet.parameterCount; ++i)
                  plugin->setParameter(packet.parameters[i].id, packet.parameters[i].value);
              InterlockedExchange(&packet.status, 0);
            }
            for (const auto &v : command["parameters"].toList()) {
              const auto p = v.toMap();
              plugin->setParameter(p["id"].toUInt(), p["value"].toDouble());
            }
            // The last partial bridge block can contain note/pedal releases.
            // Deliver a complete release here before clearing the pipeline.
            std::array<Midi, 132> releases{};
            releases[0] = {0, 0xb0, 64, 0};
            releases[1] = {0, 0xb0, 66, 0};
            releases[2] = {0, 0xb0, 123, 0};
            releases[3] = {0, 0xb0, 120, 0};
            for (int pitch = 0; pitch < 128; ++pitch)
              releases[pitch + 4] = {0, 0x80, static_cast<unsigned char>(pitch), 64};
            std::array<float, BridgeFrames> zero{}, left{}, right{};
            Clock clock;
            clock.rate = command.value("rate", 44100).toDouble();
            plugin->process(64, zero.data(), zero.data(), left.data(), right.data(),
                            releases.data(), int(releases.size()), clock);
            result["ok"] = plugin->resetAudio();
          } else if (op == "restore")
            result["ok"] = plugin->restore(
                QByteArray::fromBase64(command["state"].toByteArray()), &error);
          else if (op == "import")
            result["ok"] =
                plugin->importPreset(command["path"].toString(), &error);
          else if (op == "editor")
            result["ok"] = plugin->openEditor(nullptr, &error);
          else if (op == "closeEditor") {
            plugin->closeEditor();
            result["ok"] = true;
          } else if (op == "parameters")
            result["parameters"] = plugin->parameters();
          else if (op == "changed")
            result["changed"] = plugin->takeChanges();
        } else
          error = QStringLiteral("音源尚未载入。");
        result["error"] = error;
        const auto bytes =
            QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact);
        const quint32 size = quint32(bytes.size());
        client->write(reinterpret_cast<const char *>(&size), 4);
        client->write(bytes);
        client->flush();
        if (qEnvironmentVariableIsSet("KUMO_VST_TRACE"))
          qWarning("VST response %u", size);
      }
    });
    if (client->bytesAvailable())
      QMetaObject::invokeMethod(client, "readyRead", Qt::QueuedConnection);
  });
  if (!server.listen(QStringLiteral("KumoVst-") + key)) {
    InterlockedExchange(&bridge.memory->stop, 1);
    SetEvent(bridge.request);
    audio.join();
    return 4;
  }
  if (qEnvironmentVariableIsSet("KUMO_VST_TRACE")) {
    std::fprintf(stderr, "listen complete\n");
    std::fflush(stderr);
  }
  QTimer dirty;
  QObject::connect(&dirty, &QTimer::timeout, &app, [&] {
    InterlockedExchange(&bridge.memory->editorOpen, plugin && plugin->editorVisible() ? 1 : 0);
    if (plugin && plugin->takeChanges())
      InterlockedExchange(&bridge.memory->dirty, 1);
  });
  dirty.start(50);
  const int code = app.exec();
  InterlockedExchange(&bridge.memory->stop, 1);
  SetEvent(bridge.request);
  audio.join();
  if (plugin)
    plugin->closeEditor();
  return code;
}
