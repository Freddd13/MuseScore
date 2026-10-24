// SPDX-License-Identifier: GPL-2.0-or-later
#include "rack.h"
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <thread>
#include <map>
namespace Ms {
namespace VstHost {
namespace {
constexpr int Block = 4096, MaxChannels = 256;
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
  Delay dryDelay;
  bool bypass = false;
  std::array<float, Block> l{}, r{}, dryL{}, dryR{};
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
  QVariantMap save() {
    auto value = config;
    if (processor) {
      QString error;
      const auto bytes = processor->state(&error);
      if (error.isEmpty())
        value["state"] = QString::fromLatin1(bytes.toBase64());
    }
    return value;
  }
};
struct Slot {
  int id = 0;
  bool enabled = true, nativeControls = false;
  std::array<bool, 128> held{};
  QVariantMap config;
  Unit source;
  std::vector<std::unique_ptr<Unit>> effects;
  Delay compensation;
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
} // namespace
struct Rack::Impl {
  bool offline = false;
  double rate = 44100;
  std::atomic<bool> enabled{true}, overflow{false};
  std::atomic<int> readers{0};
  int suspendDepth = 0;
  QVariantMap config;
  Rack::InternalFactory factory;
  std::vector<std::unique_ptr<Slot>> slots;
  std::vector<std::unique_ptr<Unit>> master;
  struct Stored {
    QString label;
    QVariantMap config;
    double rate = 44100;
    std::vector<std::unique_ptr<Slot>> slots;
    std::vector<std::unique_ptr<Unit>> master;
  };
  std::map<QString, Stored> stored;
  QString session, sessionLabel, nextSession, nextLabel;
  bool retain = false, nextRetain = false;
  std::array<int, MaxChannels> banks{}, hi{}, lo{};
  std::array<float, MaxChannels> volumes{}, pans{};
  Queue<Message, 8192> queue;
  Delay existingDelay;
  int maxLatency = 0;
  std::atomic<int> totalLatency{0};
  std::array<float, Block> l{}, r{};
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
      delay(s->source);
      for (auto &e : s->effects)
        delay(*e);
    }
    existingDelay.setup(maxLatency);
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
  bool quiet(bool closeEditors, int samples = 0) {
    // Drain messages before closing this material; no queued key press may
    // become a key press in the next score.
    messages();
    const Midi release[] = {{0,0xb0,64,0},{0,0xb0,66,0},{0,0xb0,123,0},{0,0xb0,120,0}};
    Clock clock; clock.rate = rate; clock.offline = offline;
    bool reset = true;
    for (auto& slot : slots) {
      std::array<Midi,132> notes{}; std::copy_n(release,4,notes.data()); int count=4;
      for (int pitch=0; pitch<128; ++pitch) if (slot->held[pitch])
        notes[count++]={0,0x80,static_cast<unsigned char>(pitch),64};
      slot->held.fill(false); slot->count=0; slot->enabled=false;
      std::fill_n(l.data(),samples,0); std::fill_n(r.data(),samples,0);
      slot->source.process(samples,l.data(),r.data(),notes.data(),count,clock);
      if (closeEditors) {
        if (slot->source.processor) slot->source.processor->closeEditor();
        for (auto& unit : slot->effects) if (unit->processor) unit->processor->closeEditor();
        reset = slot->source.resetAudio(clock) && reset;
        for (auto &unit : slot->effects)
          reset = unit->resetAudio(clock) && reset;
        slot->compensation.clear();
      }
    }
    if (closeEditors) for (auto& unit : master) if (unit->processor) unit->processor->closeEditor();
    if (closeEditors) {
      for (auto &unit : master)
        reset = unit->resetAudio(clock) && reset;
      existingDelay.clear();
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
bool Rack::load(const QVariantMap &value, double rate, QStringList *problems) {
  suspend();
  if (d->rate != rate) d->stored.clear();
  if (d->session != d->nextSession) {
    const bool reset = d->quiet(true,64);
    if (reset && d->retain && !d->offline && d->rate==rate && !d->session.isEmpty()
        && (!d->slots.empty() || !d->master.empty())) {
      Impl::Stored previous;
      previous.label=d->sessionLabel; previous.config=d->config; previous.rate=d->rate;
      previous.slots=std::move(d->slots); previous.master=std::move(d->master);
      d->stored[d->session]=std::move(previous);
    } else { d->slots.clear(); d->master.clear(); }
    d->session=d->nextSession; d->sessionLabel=d->nextLabel; d->retain=d->nextRetain;
    d->banks.fill(0); d->hi.fill(0); d->lo.fill(0); d->volumes.fill(1); d->pans.fill(0);
    auto previous=d->stored.find(d->session);
    if (previous!=d->stored.end()) {
      const bool compatible=previous->second.rate==rate && sameConfiguration(previous->second.config,value);
      if (compatible) {
        d->slots=std::move(previous->second.slots); d->master=std::move(previous->second.master);
        d->config=std::move(previous->second.config); d->rate=rate; d->stored.erase(previous);
        d->recalculate(); resume(); return true;
      }
      d->stored.erase(previous);
    }
  }
  if (d->rate != rate) { d->quiet(true,64); d->slots.clear(); d->master.clear(); }
  d->rate = rate;
  d->config = value;
  // Reuse unchanged instances; changing a different effect does not reload a
  // sample library.
  auto old = std::move(d->slots);
  auto oldMaster = std::move(d->master);
  for (const auto &v : value["slots"].toList()) {
    const auto c = v.toMap();
    auto slot = std::make_unique<Slot>();
    slot->config = c;
    slot->id = c["id"].toInt();
    if (slot->id < 1 || slot->id > 16383 || d->find(slot->id))
      continue;
    Slot *previous = nullptr;
    for (auto &s : old)
      if (s->id == slot->id) {
        previous = s.get();
        break;
      }
    const auto source = c["source"].toMap();
    slot->nativeControls =
        source["plugin"].toMap()["format"].toString() == "SF2";
    if (previous && previous->source.config == source)
      slot->source = std::move(previous->source);
    else {
      auto unit = d->make(source, problems);
      slot->source = std::move(*unit);
    }
    for (const auto &e : c["effects"].toList()) {
      auto config = e.toMap();
      std::unique_ptr<Unit> unit;
      if (previous)
        for (auto &p : previous->effects)
          if (p && p->config == config) {
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
void Rack::releaseInactiveSessions() { d->stored.clear(); }
QVariantList Rack::retainedSessions() const {
  QVariantList result;
  for (const auto& row : d->stored) {
    int instances=int(row.second.master.size());
    for (const auto& slot : row.second.slots) instances+=1+int(slot->effects.size());
    result.append(QVariantMap{{"id",row.first},{"label",row.second.label},{"instances",instances}});
  }
  return result;
}
QVariantMap Rack::state() {
  suspend();
  auto value = d->config;
  QVariantList slots, master;
  for (auto &s : d->slots) {
    auto c = s->config;
    c["source"] = s->source.save();
    s->source.config = c["source"].toMap();
    QVariantList effects;
    for (auto &e : s->effects) {
      e->config = e->save();
      effects.append(e->config);
    }
    c["effects"] = effects;
    s->config = c;
    slots.append(c);
  }
  for (auto &e : d->master) {
    e->config = e->save();
    master.append(e->config);
  }
  value["slots"] = slots;
  value["masterEffects"] = master;
  value["schema"] = 1;
  d->config = value;
  resume();
  return value;
}
QVariantList Rack::instruments() const {
  QVariantList result;
  for (const auto &s : d->slots) {
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
    if (s->source.processor)
      changed |= s->source.processor->takeChanges();
    for (auto &e : s->effects)
      if (e->processor)
        changed |= e->processor->takeChanges();
  }
  for (auto &e : d->master)
    if (e->processor)
      changed |= e->processor->takeChanges();
  return changed;
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
void Rack::mix(int count, float *interleaved, const Clock &original) {
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
    for (int i = 0; i < n; ++i) {
      interleaved[(i + offset) * 2] = d->l[i];
      interleaved[(i + offset) * 2 + 1] = d->r[i];
    }
    for (auto &s : d->slots) {
      if (!s->enabled && !s->count)
        continue;
      std::fill_n(d->l.data(), n, 0);
      std::fill_n(d->r.data(), n, 0);
      s->source.process(n, d->l.data(), d->r.data(), s->events.data(),
                        offset ? 0 : s->count, clock);
      for (auto &e : s->effects)
        e->process(n, d->l.data(), d->r.data(), nullptr, 0, clock);
      s->compensation.run(n, d->l.data(), d->r.data());
      const float l = s->volume * std::min(1.f, 1 - s->pan),
                  r = s->volume * std::min(1.f, 1 + s->pan);
      for (int i = 0; i < n; ++i) {
        interleaved[(i + offset) * 2] += d->l[i] * l;
        interleaved[(i + offset) * 2 + 1] += d->r[i] * r;
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
