// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <array>
#include <atomic>
#include <cstring>
namespace Ms {
namespace VstHost {
template <class Interface> class BorrowedInterface : public Interface {
  std::atomic<Steinberg::uint32> refs{1};

public:
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID id,
                                               void **value) override {
    if (!value)
      return Steinberg::kInvalidArgument;
    *value = nullptr;
    if (!std::memcmp(id, Interface::iid, 16) ||
        !std::memcmp(id, Steinberg::FUnknown::iid, 16)) {
      *value = static_cast<Interface *>(this);
      addRef();
      return Steinberg::kResultOk;
    }
    return Steinberg::kNoInterface;
  }
  Steinberg::uint32 PLUGIN_API addRef() override { return ++refs; }
  Steinberg::uint32 PLUGIN_API release() override { return --refs; }
  // The process owner keeps the initial reference for the complete instance
  // lifetime.
};
class FixedValueQueue final
    : public BorrowedInterface<Steinberg::Vst::IParamValueQueue> {
  struct Point {
    int offset;
    double value;
  };
  std::array<Point, 64> points;
  int count = 0;
  Steinberg::Vst::ParamID id = 0;

public:
  void reset(Steinberg::Vst::ParamID value) {
    count = 0;
    id = value;
  }
  Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id; }
  Steinberg::int32 PLUGIN_API getPointCount() override { return count; }
  Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index,
                                         Steinberg::int32 &offset,
                                         double &value) override {
    if (index < 0 || index >= count)
      return Steinberg::kInvalidArgument;
    offset = points[index].offset;
    value = points[index].value;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 offset, double value,
                                         Steinberg::int32 &index) override {
    if (count && points[count - 1].offset == offset) {
      index = count - 1;
      points[index].value = value;
      return Steinberg::kResultOk;
    }
    if (count == int(points.size()))
      return Steinberg::kOutOfMemory;
    index = count;
    points[count++] = {offset, value};
    return Steinberg::kResultOk;
  }
};
class FixedChanges final
    : public BorrowedInterface<Steinberg::Vst::IParameterChanges> {
  std::array<FixedValueQueue, 256> queues;
  int count = 0;

public:
  void clear() { count = 0; }
  Steinberg::int32 PLUGIN_API getParameterCount() override { return count; }
  Steinberg::Vst::IParamValueQueue *PLUGIN_API
  getParameterData(Steinberg::int32 index) override {
    return index >= 0 && index < count ? &queues[index] : nullptr;
  }
  Steinberg::Vst::IParamValueQueue *PLUGIN_API addParameterData(
      const Steinberg::Vst::ParamID &id, Steinberg::int32 &index) override {
    for (int i = 0; i < count; ++i)
      if (queues[i].getParameterId() == id) {
        index = i;
        return &queues[i];
      }
    if (count == int(queues.size()))
      return nullptr;
    index = count;
    queues[count].reset(id);
    return &queues[count++];
  }
  bool add(Steinberg::Vst::ParamID id, int offset, double value) {
    Steinberg::int32 index;
    auto queue = addParameterData(id, index);
    return queue &&
           queue->addPoint(offset, value, index) == Steinberg::kResultOk;
  }
};
} // namespace VstHost
} // namespace Ms
