// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "plugin.h"
#include <windows.h>
namespace Ms {
namespace VstHost {
constexpr int BridgeFrames = 256, BridgeSlots = 4, BridgeMidi = 2048,
              BridgeParameters = 256;
struct BridgePacket {
  volatile LONG status = 0; // 0 free, 1 submitted, 2 completed
  qint64 serial = 0;
  int count = 0, eventCount = 0, parameterCount = 0, success = 0;
  Clock clock;
  std::array<float, BridgeFrames> inL{}, inR{}, outL{}, outR{};
  std::array<Midi, BridgeMidi> events{};
  std::array<Parameter, BridgeParameters> parameters{};
};
struct BridgeMemory {
  quint32 protocol = 1;
  volatile LONG stop = 0;
  volatile LONG dirty = 0;
  std::array<BridgePacket, BridgeSlots> packets;
};
struct BridgeHandles {
  HANDLE mapping = nullptr, request = nullptr, done = nullptr;
  BridgeMemory *memory = nullptr;
  bool open(const QString &key, bool create) {
    const auto name = [&](const QString &suffix) {
      return (QStringLiteral("Local\\KumoVst-") + key + suffix).toStdWString();
    };
    auto value = name("-data");
    mapping =
        create
            ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                 0, sizeof(BridgeMemory), value.c_str())
            : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, value.c_str());
    if (mapping)
      memory = static_cast<BridgeMemory *>(MapViewOfFile(
          mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BridgeMemory)));
    value = name("-request");
    request = create ? CreateEventW(nullptr, FALSE, FALSE, value.c_str())
                     : OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
                                  value.c_str());
    value = name("-done");
    done = create ? CreateEventW(nullptr, FALSE, FALSE, value.c_str())
                  : OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
                               value.c_str());
    if (create && memory)
      new (memory) BridgeMemory;
    return memory && request && done && memory->protocol == 1;
  }
  ~BridgeHandles() {
    if (memory)
      UnmapViewOfFile(memory);
    if (mapping)
      CloseHandle(mapping);
    if (request)
      CloseHandle(request);
    if (done)
      CloseHandle(done);
  }
};
} // namespace VstHost
} // namespace Ms
