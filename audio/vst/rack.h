// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "plugin.h"
#include <QVector>
#include <vector>
namespace Ms {
namespace VstHost {
// The rack owns plug-ins; score, undo and widgets remain in the application
// layer.
class Rack {
public:
  using InternalFactory =
      std::function<std::unique_ptr<Plugin>(const Descriptor &, QString *)>;
  explicit Rack(bool offline = false, InternalFactory factory = {});
  ~Rack();
  bool load(const QVariantMap &, double rate, QStringList *problems = nullptr);
  QVariantMap state();
  QVariantList instruments() const;
  QVariantList effects(int slot) const;
  Plugin *plugin(int slot, int effect = -1) const;
  bool takeChanges();
  bool hasOpenEditors() const;
  QStringList problems() const;
  int latency() const;
  qint64 tail() const;
  void midi(int channel, int type, int first, int second, float tuning = 0);
  void silence(int channel = -1, bool immediate = false);
  void bindChannels(const QVector<int> &banks);
  void mix(int count, float *interleaved, const Clock &);
  void master(int count, float *interleaved, const Clock &);
  // A control-thread lease prevents audio access during state/editor
  // operations.
  void suspend();
  void resume();

private:
  struct Impl;
  std::unique_ptr<Impl> d;
};
} // namespace VstHost
} // namespace Ms
