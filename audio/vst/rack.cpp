// SPDX-License-Identifier: GPL-2.0-or-later
#include "rack.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <thread>
#include <map>
namespace Ms {
namespace VstHost {
namespace {
constexpr int Block = 4096, MaxChannels = 256;
bool isCfx(const QVariantMap& source) {
  const auto plugin = Descriptor::fromValue(source.value("plugin").toMap());
  return plugin.format.startsWith("VST") && plugin.instrument
      && (plugin.name + plugin.path).contains("CFX", Qt::CaseInsensitive);
}

using Snapshot = std::function<void(const QByteArray&)>;

void retireIsolated(std::unique_ptr<Plugin> source, Snapshot snapshot = {}) {
  auto* processor = source.release();
  if (!processor)
    return;
  if (QCoreApplication::closingDown()) {
    delete processor;
    return;
  }
  auto* worker = QThread::create([processor, snapshot = std::move(snapshot)] {
    std::unique_ptr<Plugin> closing(processor);
    if (!snapshot)
      return;
    if (closing->editorVisible())
      closing->closeEditor();
    QString error;
    const auto bytes = closing->state(&error);
    if (auto app = QCoreApplication::instance()) {
      const auto result = error.isEmpty() ? bytes : QByteArray();
      QMetaObject::invokeMethod(app, [snapshot, result] { snapshot(result); }, Qt::QueuedConnection);
    }
  });
  processor->moveControlTo(worker);
  QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

// A slot owns its request. A superseded score releases it before another
// score can publish a result into the same rack.
struct DeferredCfx {
  QMutex mutex;
  bool done = false;
  bool abandoned = false;
  QString error;
  std::unique_ptr<Plugin> processor;
};

std::shared_ptr<DeferredCfx> prepareCfx(const QVariantMap& source, double rate) {
  auto job = std::make_shared<DeferredCfx>();
  auto* target = QThread::currentThread();
  static QThreadPool workers;
  workers.setMaxThreadCount(2);
  workers.start([job, source, rate, target] {
    {
      QMutexLocker lock(&job->mutex);
      if (job->abandoned)
        return;
    }
    const auto descriptor = Descriptor::fromValue(source.value("plugin").toMap());
    auto isolated = createIsolated(descriptor);
    QString error;
    const bool prepared = isolated && isolated->prepare(rate, Block, &error)
        && (!source.contains("state") ||
            isolated->restore(QByteArray::fromBase64(source.value("state").toByteArray()), &error));
    QMutexLocker lock(&job->mutex);
    if (job->abandoned)
      return;
    if (prepared) {
      isolated->takeChanges();
      isolated->moveControlTo(target);
      job->processor = std::move(isolated);
    } else {
      job->error = QObject::tr("%1 未能载入。当前可使用 SF2。%2").arg(descriptor.name, error);
    }
    job->done = true;
  });
  return job;
}

struct Delay {
  std::vector<float> left, right;
  size_t at = 0;
  void setup(int frames) {
    left.assign(std::max(0, frames), 0);
    right = left;
    at = 0;
  }
  void run(int count, float *l, float *r) {
    if (left.empty())
      return;
    for (int i = 0; i < count; ++i) {
      std::swap(l[i], left[at]);
      std::swap(r[i], right[at]);
      at = (at + 1) % left.size();
    }
  }
  void clear() {
    std::fill(left.begin(), left.end(), 0);
    std::fill(right.begin(), right.end(), 0);
    at = 0;
  }
};
struct Unit {
  QVariantMap config;
  std::unique_ptr<Plugin> processor;
  std::shared_ptr<DeferredCfx> pending;
  bool backgroundOwned = false;
  bool needsSnapshot = false;
  Snapshot onRetired;
  Delay dryDelay;
  bool bypass = false;
  std::array<float, Block> l{}, r{}, dryL{}, dryR{};
  Unit() = default;
  Unit(Unit&&) = default;
  Unit& operator=(Unit&&) = default;
  ~Unit() {
    if (pending) {
      QMutexLocker lock(&pending->mutex);
      pending->abandoned = true;
      retireIsolated(std::move(pending->processor));
    }
    if (backgroundOwned)
      retireIsolated(std::move(processor), std::move(onRetired));
  }
  void process(int count, float *inL, float *inR, const Midi *midi, int events,
               const Clock &clock) {
    if (!processor)
      return;
    std::copy_n(inL, count, dryL.data());
    std::copy_n(inR, count, dryR.data());
    dryDelay.run(count, dryL.data(), dryR.data());
    if (!processor->process(count, inL, inR, l.data(), r.data(), midi, events,
                            clock)) {
      std::fill_n(l.data(), count, 0);
      std::fill_n(r.data(), count, 0);
    }
    // Keep processing a bypassed unit so notes, pedal and tails remain current.
    std::copy_n(bypass ? dryL.data() : l.data(), count, inL);
    std::copy_n(bypass ? dryR.data() : r.data(), count, inR);
  }
  int latency() const {
    return processor ? std::max(0, processor->latency()) : 0;
  }
  bool resetAudio(const Clock &clock) {
    dryDelay.clear();
    if (!processor)
      return true;
    if (!processor->resetAudio())
      return false;
    // Also drain short fixed delays whose plugins keep history on suspend.
    // This bounded preparation runs once on the control thread at a switch.
    const std::array<float, Block> zero{};
    for (int at = 0, remaining = std::min(Block, latency()) + 64;
         at < remaining; at += Block) {
      const int count = std::min(Block, remaining - at);
      if (!processor->process(count, zero.data(), zero.data(), l.data(), r.data(),
                              nullptr, 0, clock))
        return false;
    }
    l.fill(0);
    r.fill(0);
    return true;
  }
  bool restoreSource(const QVariantMap& value,const Clock& clock) {
    if (backgroundOwned || !processor || !value.contains("state") || config.value("plugin")!=value["plugin"] ||
        config.value("isolated").toBool()!=value["isolated"].toBool()) return false;
    QString error;
    std::array<Midi,132> release{};
    release[0]={0,0xb0,64,0};release[1]={0,0xb0,66,0};release[2]={0,0xb0,123,0};release[3]={0,0xb0,120,0};
    for(int pitch=0;pitch<128;++pitch) release[pitch+4]={0,0x80,static_cast<unsigned char>(pitch),64};
    const std::array<float,64> zero{};
    if (!processor->process(64,zero.data(),zero.data(),l.data(),r.data(),release.data(),int(release.size()),clock) ||
        !resetAudio(clock)) return false;
    if (!processor->restore(QByteArray::fromBase64(value["state"].toByteArray()),&error)) return false;
    processor->takeChanges();
    config=value;bypass=value["bypass"].toBool();dryDelay.clear();return true;
  }
  bool takeChanges() {
    if (!processor || !processor->takeChanges())
      return false;
    needsSnapshot = true;
    return true;
  }
  bool needsSave() const {
    return processor && (!backgroundOwned || (!onRetired && !processor->editorVisible()))
        && (needsSnapshot || !config.contains("state") || processor->editorVisible());
  }
  QVariantMap save(bool changedOnly) {
    auto value = config;
    // CFX queries can wait on the vendor process. A saved CFX chunk already
    // represents an unchanged instance, even when a caller requests a full
    // snapshot of the rest of the rack.
    if (processor && ((backgroundOwned || changedOnly) ? needsSave() : true)) {
      QString error;
      const auto bytes = processor->state(&error);
      if (error.isEmpty() && (!backgroundOwned || !bytes.isEmpty())) {
        value["state"] = QString::fromLatin1(bytes.toBase64());
        needsSnapshot = false;
      }
    }
    return value;
  }
};
struct Slot {
  int id = 0;
  bool enabled = true, nativeControls = false;
  std::array<bool, 128> held{};
  QVariantMap config;
  // The requested preset stays stable when a live snapshot includes MIDI controls.
  QVariantMap loadedConfig;
  Unit source;
  std::vector<std::unique_ptr<Unit>> effects;
  Delay compensation, sendCompensation;
  float volume = 1, pan = 0;
  std::array<Midi, 2048> events{};
  int count = 0;
  int latency() const {
    int n = source.latency();
    for (const auto &e : effects)
      n += e->latency();
    return n;
  }
};
struct Message {
  int channel = 0, type = 0, first = 0, second = 0;
  float tuning = 0;
};
QVariantMap localConfiguration(QVariantMap value) {
  QVariantList local;
  for (const auto &row : value["slots"].toList())
    if (row.toMap()["commonPiano"].toString().isEmpty())
      local.append(row);
  value["slots"] = local;
  return value;
}
bool sameSlot(const QVariantMap& a,const QVariantMap& b) {
  return sameConfiguration({{"slots",QVariantList{a}}},{{"slots",QVariantList{b}}});
}
bool sameUnit(const QVariantMap& a,const QVariantMap& b) {
  return sameSlot({{"source",a}},{{"source",b}});
}
bool sameCommonPiano(QVariantMap a,QVariantMap b) {
  a.remove("id"); b.remove("id"); a.remove("label"); b.remove("label");
  return sameSlot(a,b);
}
} // namespace
struct Rack::Impl {
  bool offline = false;
  double rate = 44100;
  std::atomic<bool> enabled{true}, overflow{false};
  std::atomic<int> readers{0};
  int suspendDepth = 0;
  QVariantMap config;
  Rack::InternalFactory factory;
  std::vector<std::shared_ptr<Slot>> slots;
  std::map<QString, std::shared_ptr<Slot>> commonPianos;
  std::vector<std::unique_ptr<Unit>> master;
  struct Stored {
    QString label;
    QVariantMap config;
    double rate = 44100;
    std::vector<std::shared_ptr<Slot>> slots;
    std::vector<std::unique_ptr<Unit>> master;
  };
  std::map<QString, Stored> stored;
  QString session, sessionLabel, nextSession, nextLabel;
  bool retain = false, nextRetain = false;
  std::array<int, MaxChannels> banks{}, hi{}, lo{};
  std::array<float, MaxChannels> volumes{}, pans{};
  Queue<Message, 8192> queue;
  Delay existingDelay, existingSendDelay;
  int maxLatency = 0;
  int changedCommonPiano = 0;
  std::atomic<int> totalLatency{0};
  std::array<float, Block> l{}, r{}, sendL{}, sendR{};
  std::array<float, Block * 2> sends{};
  explicit Impl(bool value, Rack::InternalFactory internal)
      : offline(value), factory(std::move(internal)) {
    volumes.fill(1);
  }
  Slot *find(int id) const {
    for (const auto &s : slots)
      if (s->id == id)
        return s.get();
    return nullptr;
  }
  std::unique_ptr<Unit> make(QVariantMap value, QStringList *problems) {
    auto unit = std::make_unique<Unit>();
    unit->config = value;
    unit->bypass = value["bypass"].toBool();
    const auto descriptor = Descriptor::fromValue(value["plugin"].toMap());
    QString error;
    unit->processor = descriptor.format == "SF2" && factory
                          ? factory(descriptor, &error)
                      : value["isolated"].toBool() ? createIsolated(descriptor)
                                                   : create(descriptor, &error);
    if (unit->processor &&
        !unit->processor->prepare(rate, Block, &error, offline))
      unit->processor.reset();
    if (unit->processor && value.contains("state") &&
        !unit->processor->restore(
            QByteArray::fromBase64(value["state"].toByteArray()), &error))
      unit->processor.reset();
    if (unit->processor && value.contains("state"))
      unit->processor->takeChanges();
    if (!error.isEmpty() && problems)
      problems->append(descriptor.name + QStringLiteral(": ") + error);
    unit->dryDelay.setup(std::min(int(rate * 10), unit->latency()));
    return unit;
  }
  void recalculate() {
    auto delay = [this](Unit &unit) {
      const int frames = std::min(int(rate * 10), unit.latency());
      if (int(unit.dryDelay.left.size()) != frames)
        unit.dryDelay.setup(frames);
    };
    maxLatency = 0;
    for (const auto &s : slots)
      if (s->enabled)
        maxLatency = std::max(maxLatency, s->latency());
    for (auto &s : slots) {
      s->compensation.setup(s->enabled ? maxLatency - s->latency() : 0);
      s->sendCompensation.setup(s->enabled ? maxLatency - s->latency() : 0);
      delay(s->source);
      for (auto &e : s->effects)
        delay(*e);
    }
    existingDelay.setup(maxLatency);
    existingSendDelay.setup(maxLatency);
    totalLatency = maxLatency;
    for (auto &e : master) {
      totalLatency += e->latency();
      delay(*e);
    }
  }
  void add(Slot *slot, int type, int first, int second, float tuning = 0) {
    if (!slot)
      return;
    if (slot->count >= int(slot->events.size())) {
      overflow = true;
      return;
    }
    if ((type & 0xf0) == 0x90 && first < 128)
      slot->held[first] = second > 0;
    else if ((type & 0xf0) == 0x80 && first < 128)
      slot->held[first] = false;
    slot->events[slot->count++] = {0, static_cast<unsigned char>(type),
                                   static_cast<unsigned char>(first),
                                   static_cast<unsigned char>(second), tuning};
  }
  void release(int channel, bool immediate) {
    if (channel < 0) {
      for (const auto &s : slots) {
        for (int pitch = 0; pitch < 128; ++pitch)
          if (s->held[pitch])
            add(s.get(), 0x80, pitch, 64);
        add(s.get(), 0xb0, 64, 0);
        add(s.get(), 0xb0, 66, 0);
        add(s.get(), 0xb0, 123, 0);
        if (immediate)
          add(s.get(), 0xb0, 120, 0);
      }
      return;
    }
    if (channel >= MaxChannels)
      return;
    auto s = find(banks[channel]);
    if (s)
      for (int pitch = 0; pitch < 128; ++pitch)
        if (s->held[pitch])
          add(s, 0x80, pitch, 64);
    add(s, 0xb0, 64, 0);
    add(s, 0xb0, 66, 0);
    add(s, 0xb0, 123, 0);
    if (immediate)
      add(s, 0xb0, 120, 0);
  }
  void messages() {
    for (auto &s : slots)
      s->count = 0;
    Message m;
    while (queue.pop(m)) {
      if (m.type == -1) {
        release(m.channel, m.first != 0);
        continue;
      }
      if (m.channel < 0 || m.channel >= MaxChannels)
        continue;
      const int c = m.channel;
      if (m.type == 0xb0 && m.first == 0) {
        hi[c] = m.second;
        banks[c] = (hi[c] << 7) | lo[c];
        if (auto s = find(banks[c]))
          s->enabled = true;
        continue;
      }
      if (m.type == 0xb0 && m.first == 32) {
        lo[c] = m.second;
        banks[c] = (hi[c] << 7) | lo[c];
        if (auto s = find(banks[c]))
          s->enabled = true;
        continue;
      }
      if (m.type == 0xc0 || (m.type == 0xb0 && m.first == 129))
        continue;
      auto slot = find(banks[c]);
      if (!slot)
        continue;
      if (m.type == 0xb0 && m.first == 7) {
        volumes[c] = m.second / 100.f;
        slot->volume = slot->nativeControls ? 1 : volumes[c];
        if (slot->nativeControls)
          add(slot, 0xb0, 7, m.second);
        continue;
      }
      if (m.type == 0xb0 && m.first == 10) {
        pans[c] = (m.second - 64) / 63.f;
        slot->pan = slot->nativeControls ? 0 : pans[c];
        if (slot->nativeControls)
          add(slot, 0xb0, 10, m.second);
        continue;
      }
      int type = m.type, first = m.first, second = m.second;
      if (type == 0xb0 && first == 131) {
        type = 0xd0;
        first = second;
        second = 0;
      } else if (type == 0xb0 && first == 132)
        continue;
      if (type == 0x90 || type == 0x80 || type == 0xb0 || type == 0xa0 ||
          type == 0xd0 || type == 0xe0)
        add(slot, type, first, second, m.tuning);
    }
    if (overflow.exchange(false))
      release(-1, true);
  }
  bool enter() {
    if (!enabled.load(std::memory_order_acquire))
      return false;
    readers.fetch_add(1, std::memory_order_acq_rel);
    if (!enabled.load(std::memory_order_acquire)) {
      readers.fetch_sub(1, std::memory_order_release);
      return false;
    }
    return true;
  }
  void leave() { readers.fetch_sub(1, std::memory_order_release); }
  bool quietSlot(Slot& slot,bool closeEditors,int samples,const Clock& clock) {
    std::array<Midi,132> notes{};
    notes[0]={0,0xb0,64,0};notes[1]={0,0xb0,66,0};
    notes[2]={0,0xb0,123,0};notes[3]={0,0xb0,120,0};int count=4;
    for (int pitch=0;pitch<128;++pitch) if (slot.held[pitch])
      notes[count++]={0,0x80,static_cast<unsigned char>(pitch),64};
    slot.held.fill(false);slot.count=0;slot.enabled=false;
    std::fill_n(l.data(),samples,0);std::fill_n(r.data(),samples,0);
    slot.source.process(samples,l.data(),r.data(),notes.data(),count,clock);
    if (!closeEditors) return true;
    // Give an isolated instrument a complete release before retiring it.
    // Disposal, including closing its editor, takes place on its worker.
    bool reset = !slot.source.backgroundOwned;
    if (reset) {
      if (slot.source.processor) slot.source.processor->closeEditor();
      reset = slot.source.resetAudio(clock);
    }
    for (auto& unit:slot.effects) {
      if (unit->processor) unit->processor->closeEditor();
      reset=unit->resetAudio(clock) && reset;
    }
    slot.compensation.clear();slot.sendCompensation.clear();return reset;
  }
  bool quiet(bool closeEditors, int samples = 0) {
    // Drain messages before closing this material; no queued key press may
    // become a key press in the next score.
    messages();
    Clock clock; clock.rate = rate; clock.offline = offline;
    bool reset = true;
    for (auto& slot : slots) reset=quietSlot(*slot,closeEditors,samples,clock) && reset;
    if (closeEditors) for (auto& unit : master) if (unit->processor) unit->processor->closeEditor();
    if (closeEditors) {
      for (auto &unit : master)
        reset = unit->resetAudio(clock) && reset;
      existingDelay.clear();
      existingSendDelay.clear();
    }
    return reset;
  }
};
Rack::Rack(bool offline, InternalFactory factory)
    : d(new Impl(offline, std::move(factory))) {}
Rack::~Rack() { suspend(); }
void Rack::suspend() {
  if (d->suspendDepth++ > 0)
    return;
  d->enabled.store(false, std::memory_order_release);
  while (d->readers.load(std::memory_order_acquire))
    std::this_thread::yield();
}
void Rack::resume() {
  if (d->suspendDepth && !--d->suspendDepth)
    d->enabled.store(true, std::memory_order_release);
}
bool Rack::load(const QVariantMap &value, double rate, QStringList *problems, bool deferCfx) {
  suspend();
  if (d->rate != rate) { d->stored.clear(); d->commonPianos.clear(); }
  if (d->session != d->nextSession) {
    const bool reset = d->quiet(true,64);
    if (reset && d->retain && !d->offline && d->rate==rate && !d->session.isEmpty()
        && (!d->slots.empty() || !d->master.empty())) {
      Impl::Stored previous;
      previous.label=d->sessionLabel; previous.config=localConfiguration(d->config); previous.rate=d->rate;
      previous.slots=std::move(d->slots); previous.master=std::move(d->master);
      previous.slots.erase(std::remove_if(previous.slots.begin(),previous.slots.end(),
          [](const auto& slot) { return !slot->config.value("commonPiano").toString().isEmpty(); }),previous.slots.end());
      if (!previous.slots.empty() || !previous.master.empty()) d->stored[d->session]=std::move(previous);
    } else { d->slots.clear(); d->master.clear(); }
    d->session=d->nextSession; d->sessionLabel=d->nextLabel; d->retain=d->nextRetain;
    d->banks.fill(0); d->hi.fill(0); d->lo.fill(0); d->volumes.fill(1); d->pans.fill(0);
    auto previous=d->stored.find(d->session);
    if (previous!=d->stored.end()) {
      const bool compatible=previous->second.rate==rate && sameConfiguration(previous->second.config,localConfiguration(value));
      if (compatible) {
        d->slots=std::move(previous->second.slots); d->master=std::move(previous->second.master);
        d->config=std::move(previous->second.config); d->rate=rate; d->stored.erase(previous);
        // Continue assembling this score's routing; common pianos are owned by
        // their pool and can also be used by a score with no retained graph.
      }
      else d->stored.erase(previous);
    }
  }
  if (d->rate != rate) { d->quiet(true,64); d->slots.clear(); d->master.clear(); }
  d->rate = rate;
  d->config = value;
  Clock preparation;preparation.rate=rate;preparation.offline=d->offline;
  // A score can switch from the common piano back to its own source without
  // changing its session. Retire pooled voices before removing their routing.
  QSet<QString> incoming;
  for (const auto& row:value["slots"].toList()) incoming.insert(row.toMap()["commonPiano"].toString());
  bool retiring=false;
  for (auto& slot:d->slots) {
    const auto key=slot->config.value("commonPiano").toString();
    retiring|=!key.isEmpty() && !incoming.contains(key);
  }
  if (retiring) {
    d->messages();
    Clock stopped;stopped.rate=rate;stopped.offline=d->offline;
    for (auto& slot:d->slots) {
      const auto key=slot->config.value("commonPiano").toString();
      d->quietSlot(*slot,!key.isEmpty() && !incoming.contains(key),64,stopped);
    }
  }
  // Reuse unchanged instances; changing a different effect does not reload a
  // sample library.
  auto old = std::move(d->slots);
  auto oldMaster = std::move(d->master);
  for (const auto &v : value["slots"].toList()) {
    const auto c = v.toMap();
    auto slot = std::make_shared<Slot>();
    slot->config = c;
    slot->loadedConfig = c;
    slot->id = c["id"].toInt();
    if (slot->id < 1 || slot->id > 16383 || d->find(slot->id))
      continue;
    const auto commonKey = c["commonPiano"].toString();
    const auto common = d->commonPianos.find(commonKey);
    if (!commonKey.isEmpty() && common != d->commonPianos.end() && common->second->source.processor &&
        common->second->source.processor->status().isEmpty() &&
        (sameCommonPiano(common->second->config, c) ||
         sameCommonPiano(common->second->loadedConfig, c))) {
      common->second->id = slot->id;
      common->second->config = c;
      common->second->loadedConfig = c;
      d->slots.push_back(common->second);
      continue;
    }
    Slot *previous = nullptr;
    for (auto &s : old)
      if (s->id == slot->id) {
        previous = s.get();
        break;
      }
    const auto source = c["source"].toMap();
    slot->nativeControls =
        source["plugin"].toMap()["format"].toString() == "SF2";
    const bool delayed = !d->offline && deferCfx && isCfx(source);
    if (previous && (sameUnit(previous->source.config, source) ||
                     (!delayed && previous->source.restoreSource(source,preparation)))
        && (previous->source.processor || previous->source.pending))
      slot->source = std::move(previous->source);
    else if (delayed) {
      slot->source.config = source;
      slot->source.pending = prepareCfx(source, rate);
      slot->source.backgroundOwned = true;
      slot->source.bypass = source.value("bypass").toBool();
    }
    else {
      auto unit = d->make(source, problems);
      slot->source = std::move(*unit);
    }
    for (const auto &e : c["effects"].toList()) {
      auto config = e.toMap();
      std::unique_ptr<Unit> unit;
      if (previous)
        for (auto &p : previous->effects)
          if (p && sameUnit(p->config, config)) {
            unit = std::move(p);
            break;
          }
      if (!unit)
        unit = d->make(config, problems);
      slot->effects.push_back(std::move(unit));
    }
    if (previous) {
      slot->volume = previous->volume;
      slot->pan = previous->pan;
      slot->enabled = previous->enabled;
      slot->held = previous->held;
    }
    if (!commonKey.isEmpty()) {
      if (slot->source.processor) d->commonPianos[commonKey] = slot;
      else d->commonPianos.erase(commonKey);
    }
    d->slots.push_back(std::move(slot));
  }
  for (const auto &e : value["masterEffects"].toList()) {
    const auto config = e.toMap();
    std::unique_ptr<Unit> unit;
    for (auto &p : oldMaster)
      if (p && p->config == config) {
        unit = std::move(p);
        break;
      }
    if (!unit)
      unit = d->make(config, problems);
    d->master.push_back(std::move(unit));
  }
  d->recalculate();
  resume();
  return true;
}
bool Rack::preparingSources() const {
  return std::any_of(d->slots.begin(), d->slots.end(),
                     [](const auto& slot) { return bool(slot->source.pending); });
}
bool Rack::backgroundSource(int id) const {
  const auto* slot = d->find(id);
  return slot && slot->source.backgroundOwned;
}
bool Rack::adoptPreparedSources(QStringList* problems) {
  bool changed = false;
  suspend();
  for (auto& slot : d->slots) {
    const auto job = slot->source.pending;
    if (!job)
      continue;
    QMutexLocker lock(&job->mutex);
    if (!job->done)
      continue;
    slot->source.processor = std::move(job->processor);
    slot->source.pending.reset();
    if (slot->source.processor)
      slot->source.processor->takeChanges(); // Preparation notifications precede user edits.
    if (!job->error.isEmpty() && problems)
      problems->append(job->error);
    const auto commonKey = slot->config.value("commonPiano").toString();
    if (!commonKey.isEmpty() && slot->source.processor)
      d->commonPianos[commonKey] = slot;
    changed = true;
  }
  if (changed)
    d->recalculate();
  resume();
  return changed;
}
void Rack::selectSession(const QString& id,const QString& label,bool retain) {
  if (d->session.isEmpty() && d->nextSession.isEmpty() && !id.isEmpty()) d->session=id;
  d->nextSession=id; d->nextLabel=label; d->nextRetain=retain && !d->offline;
  if (d->session==id) { d->retain=d->nextRetain; d->sessionLabel=label; }
}
void Rack::releaseSession(const QString& id) {
  d->stored.erase(id);
  if (d->session==id) d->retain=false;
  if (d->nextSession==id) d->nextRetain=false;
}
void Rack::releaseInactiveSessions() {
  d->stored.clear();
  for (auto it=d->commonPianos.begin(); it!=d->commonPianos.end();)
    if (std::find(d->slots.begin(),d->slots.end(),it->second)==d->slots.end())
      it=d->commonPianos.erase(it);
    else ++it;
}
void Rack::releaseCommonPianos() { d->commonPianos.clear(); }
QVariantList Rack::retainedSessions() const {
  QVariantList result;
  for (const auto& row : d->stored) {
    int instances=int(row.second.master.size());
    for (const auto& slot : row.second.slots) instances+=1+int(slot->effects.size());
    result.append(QVariantMap{{"id",row.first},{"label",row.second.label},{"instances",instances}});
  }
  for (const auto& row : d->commonPianos)
    if (std::find(d->slots.begin(),d->slots.end(),row.second)==d->slots.end())
      result.append(QVariantMap{{"id",row.first},{"label",row.second->config.value("label")},
                               {"instances",1+int(row.second->effects.size())},{"commonPiano",true}});
  return result;
}
QVariantMap Rack::state(bool changedOnly) {
  takeChanges();
  if (changedOnly) {
    auto dirty = [](const auto& units) {
      return std::any_of(units.begin(), units.end(), [](const auto& unit) { return unit->needsSave(); });
    };
    if (!dirty(d->master) &&
        std::none_of(d->slots.begin(), d->slots.end(), [&](const auto& slot) {
          return slot->source.needsSave() || dirty(slot->effects);
        }))
      return d->config;
  }
  suspend();
  auto value = d->config;
  QVariantList slots, master;
  for (auto &s : d->slots) {
    auto c = s->config;
    c["source"] = s->source.save(changedOnly);
    s->source.config = c["source"].toMap();
    QVariantList effects;
    for (auto &e : s->effects) {
      e->config = e->save(changedOnly);
      effects.append(e->config);
    }
    c["effects"] = effects;
    s->config = c;
    slots.append(c);
  }
  for (auto &e : d->master) {
    e->config = e->save(changedOnly);
    master.append(e->config);
  }
  value["slots"] = slots;
  value["masterEffects"] = master;
  value["schema"] = 1;
  d->config = value;
  resume();
  return value;
}
QVariantList Rack::instruments(bool includeReference) const {
  QVariantList result;
  for (const auto &s : d->slots) {
    if(!includeReference && s->config.contains("referenceMidi"))continue;
    auto v = s->config;
    v["available"] = bool(s->source.processor);
    v["latency"] = s->latency();
    result.append(v);
  }
  return result;
}
QVariantList Rack::effects(int id) const {
  QVariantList r;
  if (id) {
    if (auto s = d->find(id))
      for (const auto &e : s->effects)
        r.append(e->config);
  } else
    for (const auto &e : d->master)
      r.append(e->config);
  return r;
}
Plugin *Rack::plugin(int id, int effect) const {
  if (!id)
    return effect >= 0 && effect < int(d->master.size())
               ? d->master[effect]->processor.get()
               : nullptr;
  auto slot = d->find(id);
  return !slot        ? nullptr
         : effect < 0 ? slot->source.processor.get()
         : effect < int(slot->effects.size())
             ? slot->effects[effect]->processor.get()
             : nullptr;
}
bool Rack::takeChanges() {
  bool changed = false;
  for (auto &s : d->slots) {
    bool instrumentChanged = s->source.takeChanges();
    for (auto &e : s->effects)
      instrumentChanged |= e->takeChanges();
    if (instrumentChanged && !s->config.value("commonPiano").toString().isEmpty())
      d->changedCommonPiano = s->id;
    changed |= instrumentChanged;
  }
  for (auto &e : d->master)
    changed |= e->takeChanges();
  return changed;
}
bool Rack::hasOpenCfxEdits() const {
  return std::any_of(d->slots.begin(), d->slots.end(), [](const auto& slot) {
    const auto& source = slot->source;
    return source.backgroundOwned && source.processor && source.processor->editorVisible()
        && (source.needsSnapshot || !source.config.contains("state"));
  });
}
QList<int> Rack::captureCfxOnRetirement(std::function<void(int, const QVariantMap&, const QByteArray&)> snapshot) {
  QList<int> captured;
  takeChanges();
  for (auto& slot : d->slots) {
    auto& source = slot->source;
    if (!source.backgroundOwned || !source.processor || source.onRetired
        || (!source.needsSnapshot && source.config.contains("state") && !source.processor->editorVisible())
        || !slot->config.value("commonPiano").toString().isEmpty())
      continue;
    const auto id = slot->id;
    const auto before = source.config;
    source.onRetired = [id, before, snapshot](const QByteArray& bytes) {
      snapshot(id, before, bytes);
    };
    captured.append(id);
  }
  return captured;
}
void Rack::rememberRetiredCfxState(int id, const QVariantMap& expected, const QString& encoded) {
  auto* slot = d->find(id);
  if (!slot || slot->source.config != expected)
    return;
  // Preserve the saved chunk for the next restore. The running instrument
  // remains untouched, including notes already sounding.
  suspend();
  slot->source.config["state"] = encoded;
  slot->config["source"] = slot->source.config;
  auto rows = d->config.value("slots").toList();
  for (auto& row : rows) {
    auto value = row.toMap();
    if (value.value("id").toInt() == id && value.value("source").toMap() == expected) {
      value["source"] = slot->source.config;
      row = value;
      d->config["slots"] = rows;
      break;
    }
  }
  resume();
}
int Rack::takeChangedCommonPiano() {
  const int id=d->changedCommonPiano; d->changedCommonPiano=0; return id;
}
bool Rack::hasOpenEditors() const {
  for (const auto &slot : d->slots) {
    if (slot->source.processor && slot->source.processor->editorVisible())
      return true;
    for (const auto &unit : slot->effects)
      if (unit->processor && unit->processor->editorVisible())
        return true;
  }
  for (const auto &unit : d->master)
    if (unit->processor && unit->processor->editorVisible())
      return true;
  return false;
}
int Rack::latency() const { return d->totalLatency; }
qint64 Rack::tail() const {
  qint64 result = 0;
  for (const auto &s : d->slots) {
    qint64 tail = s->source.processor ? s->source.processor->tail() : 0;
    for (const auto &e : s->effects)
      if (e->processor)
        tail += e->processor->tail();
    result = std::max(result, tail);
  }
  for (const auto &e : d->master)
    if (e->processor)
      result += e->processor->tail();
  return result;
}
void Rack::midi(int c, int type, int a, int b, float tuning) {
  if (!d->queue.push({c, type, a, b, tuning}))
    d->overflow = true;
}
void Rack::silence(int c, bool immediate) {
  if (!d->queue.push({c, -1, immediate ? 1 : 0, 0, 0}))
    d->overflow = true;
}
void Rack::bindChannels(const QVector<int> &banks) {
  suspend();
  d->quiet(false);
  d->banks.fill(0);
  d->hi.fill(0);
  d->lo.fill(0);
  for (int i = 0; i < std::min(int(banks.size()), MaxChannels); ++i) {
    d->banks[i] = banks[i];
    d->hi[i] = banks[i] >> 7;
    d->lo[i] = banks[i] & 127;
    if (auto s = d->find(banks[i]))
      s->enabled = true;
  }
  d->recalculate();
  resume();
}
void Rack::mix(int count, float *interleaved, const Clock &original, float *reverbSend) {
  if (!d->enter())
    return;
  d->messages();
  for (int offset = 0; offset < count; offset += Block) {
    const int n = std::min(Block, count - offset);
    auto clock = original;
    clock.sample += offset;
    clock.quarter += offset / clock.rate * clock.tempo / 60;
    for (int i = 0; i < n; ++i) {
      d->l[i] = interleaved[(i + offset) * 2];
      d->r[i] = interleaved[(i + offset) * 2 + 1];
    }
    d->existingDelay.run(n, d->l.data(), d->r.data());
    if (reverbSend) {
      for (int i = 0; i < n; ++i) { d->sendL[i] = reverbSend[(i + offset) * 2]; d->sendR[i] = reverbSend[(i + offset) * 2 + 1]; }
      d->existingSendDelay.run(n, d->sendL.data(), d->sendR.data());
      for (int i = 0; i < n; ++i) { reverbSend[(i + offset) * 2] = d->sendL[i]; reverbSend[(i + offset) * 2 + 1] = d->sendR[i]; }
    }
    for (int i = 0; i < n; ++i) {
      interleaved[(i + offset) * 2] = d->l[i];
      interleaved[(i + offset) * 2 + 1] = d->r[i];
    }
    for (auto &s : d->slots) {
      // A background source can already have a MIDI route while SF2 is
      // carrying its audio. Only mix this slot once its plugin exists.
      if (!s->source.processor || (!s->enabled && !s->count))
        continue;
      std::fill_n(d->l.data(), n, 0);
      std::fill_n(d->r.data(), n, 0);
      s->source.process(n, d->l.data(), d->r.data(), s->events.data(),
                        offset ? 0 : s->count, clock);
      for (auto &e : s->effects)
        e->process(n, d->l.data(), d->r.data(), nullptr, 0, clock);
      if (reverbSend) {
        std::fill_n(d->sends.data(), n * 2, 0);
        s->source.processor->addReverbSend(n, d->l.data(), d->r.data(), d->sends.data(), 1, 1);
        for (int i = 0; i < n; ++i) { d->sendL[i] = d->sends[i * 2]; d->sendR[i] = d->sends[i * 2 + 1]; }
        s->sendCompensation.run(n, d->sendL.data(), d->sendR.data());
      }
      s->compensation.run(n, d->l.data(), d->r.data());
      const float l = s->volume * std::min(1.f, 1 - s->pan),
                  r = s->volume * std::min(1.f, 1 + s->pan);
      for (int i = 0; i < n; ++i) {
        interleaved[(i + offset) * 2] += d->l[i] * l;
        interleaved[(i + offset) * 2 + 1] += d->r[i] * r;
        if (reverbSend) { reverbSend[(i + offset) * 2] += d->sendL[i] * l; reverbSend[(i + offset) * 2 + 1] += d->sendR[i] * r; }
      }
    }
  }
  d->leave();
}
void Rack::master(int count, float *interleaved, const Clock &original) {
  if (!d->enter())
    return;
  for (int offset = 0; offset < count; offset += Block) {
    const int n = std::min(Block, count - offset);
    auto clock = original;
    clock.sample += offset;
    clock.quarter += offset / clock.rate * clock.tempo / 60;
    for (int i = 0; i < n; ++i) {
      d->l[i] = interleaved[(i + offset) * 2];
      d->r[i] = interleaved[(i + offset) * 2 + 1];
    }
    for (auto &e : d->master)
      e->process(n, d->l.data(), d->r.data(), nullptr, 0, clock);
    for (int i = 0; i < n; ++i) {
      interleaved[(i + offset) * 2] = d->l[i];
      interleaved[(i + offset) * 2 + 1] = d->r[i];
    }
  }
  d->leave();
}
} // namespace VstHost
} // namespace Ms

QStringList Ms::VstHost::Rack::problems() const {
  QStringList result;
  auto check = [&](const auto &unit) {
    if (unit.processor) {
      auto text = unit.processor->status();
      if (!text.isEmpty())
        result.append(unit.config["plugin"].toMap()["name"].toString() + ": " +
                      text);
    }
  };
  for (const auto &slot : d->slots) {
    check(slot->source);
    for (const auto &e : slot->effects)
      check(*e);
  }
  for (const auto &e : d->master)
    check(*e);
  return result;
}
