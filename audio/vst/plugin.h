// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
class QWidget;
namespace Ms {
namespace VstHost {
struct Descriptor {
  QString path, format, uid, name, vendor;
  bool instrument = false;
  QVariantMap value() const;
  static Descriptor fromValue(const QVariantMap &);
};
struct Midi {
  int offset = 0;
  unsigned char status = 0, first = 0, second = 0;
  float tuning = 0;
};
struct Clock {
  qint64 sample = 0;
  double rate = 44100, quarter = 0, tempo = 120;
  int numerator = 4, denominator = 4;
  bool playing = false, offline = false;
};
struct Parameter {
  quint32 id = 0;
  double value = 0;
};
// Bounded control-to-audio queue. Its storage is allocated with the instance.
template <class T, size_t Size> class Queue {
  struct Cell {
    std::atomic<size_t> sequence;
    T value;
  };
  std::array<Cell, Size> cells;
  std::atomic<size_t> producer{0}, consumer{0};

public:
  Queue() {
    for (size_t i = 0; i < Size; ++i)
      cells[i].sequence.store(i, std::memory_order_relaxed);
  }
  bool push(const T &value) {
    size_t at = producer.load(std::memory_order_relaxed);
    for (;;) {
      auto &cell = cells[at % Size];
      const auto difference =
          intptr_t(cell.sequence.load(std::memory_order_acquire)) -
          intptr_t(at);
      if (difference < 0)
        return false;
      if (difference == 0) {
        if (producer.compare_exchange_weak(at, at + 1,
                                           std::memory_order_relaxed)) {
          cell.value = value;
          cell.sequence.store(at + 1, std::memory_order_release);
          return true;
        }
      } else
        at = producer.load(std::memory_order_relaxed);
    }
  }
  bool pop(T &value) {
    const auto at = consumer.load(std::memory_order_relaxed);
    auto &cell = cells[at % Size];
    if (intptr_t(cell.sequence.load(std::memory_order_acquire)) -
            intptr_t(at + 1) !=
        0)
      return false;
    value = cell.value;
    consumer.store(at + 1, std::memory_order_relaxed);
    cell.sequence.store(at + Size, std::memory_order_release);
    return true;
  }
};
class Plugin {
public:
  virtual ~Plugin() = default;
  // Lifecycle and state operations run on the control thread while processing
  // is stopped.
  virtual bool prepare(double sampleRate, int maximumBlock, QString *error,
                       bool offline = false) = 0;
  // Processing is stopped. Clear transient DSP history while retaining the
  // instance, loaded samples and user parameters.
  virtual bool resetAudio() { return true; }
  virtual bool restore(const QByteArray &, QString *error) = 0;
  virtual QByteArray state(QString *error) = 0;
  virtual int latency() const = 0;
  virtual qint64 tail() const = 0;
  virtual QString presetName() const = 0;
  virtual bool openEditor(QWidget *, QString *error) = 0;
  virtual void closeEditor() = 0;
  virtual bool editorVisible() const { return false; }
  virtual bool importPreset(const QString &, QString *error);
  // Buffers and events are borrowed for this call. No host allocation or
  // waiting here.
  virtual bool process(int samples, const float *left, const float *right,
                       float *outputLeft, float *outputRight,
                       const Midi *events, int eventCount, const Clock &) = 0;
  virtual void setParameter(quint32, double) = 0;
  virtual QVariantList parameters() const = 0;
  virtual bool takeChanges() = 0;
  virtual QString status() const { return {}; }
  virtual QVariantMap processingDiagnostics() const { return {}; }
  std::function<void()> stateChanged;
};
std::unique_ptr<Plugin> createIsolated(const Descriptor &);
bool sameConfiguration(const QVariantMap &, const QVariantMap &);
void placeEditorOnScreen(QWidget *);
QVariantList scanVst3(const QString &, QString *error);
QVariantList scanVst2(const QString &, QString *error);
std::unique_ptr<Plugin> createVst3(const Descriptor &, QString *error);
std::unique_ptr<Plugin> createVst2(const Descriptor &, QString *error);
inline std::unique_ptr<Plugin> create(const Descriptor &value, QString *error) {
  return value.format == "VST2" ? createVst2(value, error)
                                : createVst3(value, error);
}
} // namespace VstHost
} // namespace Ms
