// SPDX-License-Identifier: GPL-2.0-or-later
#include "rollnotation.h"
#include <QMap>
#include <QObject>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace Ms {
namespace {
constexpr int quarter = 480;
QVariantMap quarters(int ticks) {
  return {{"numerator", ticks}, {"denominator", quarter}};
}
QVariantMap problem(const QString &text) { return {{"error", text}}; }
bool overlaps(const QVariantMap &a, const QVariantMap &b) {
  return a["start"].toInt() < b["end"].toInt() &&
         b["start"].toInt() < a["end"].toInt();
}
bool incompatible(const QVariantMap &a, const QVariantMap &b) {
  return overlaps(a, b) &&
         (b.value("contextOnly").toBool() || a["start"] != b["start"] ||
          a["end"] != b["end"] || a["pitch"] == b["pitch"]);
}
} // namespace
int rollQuantizeTick(int tick, int origin, int quantum) {
  if (quantum <= 0)
    return tick;
  return origin + qRound(double(tick - origin) / quantum) * quantum;
}
int rollGridQuantum(int tick, int quantum, bool triplet, int track,
                    const QVariantList &tuplets) {
  if (!triplet)
    for (auto value : tuplets) {
      const auto t = value.toMap();
      if (t["track"].toInt() != track || tick < t["start"].toInt() ||
          tick >= t["start"].toInt() + t["duration"].toInt())
        continue;
      const auto ratio = t["ratio"].toString().split('/');
      if (ratio.size() == 2 && ratio[0].toInt() > 0 && ratio[1].toInt() > 0)
        return qMax(
            1, qRound(double(quantum) * ratio[1].toInt() / ratio[0].toInt()));
    }
  return quantum;
}
int rollSnapInScore(int tick, int quantum, bool triplet, int track,
                    int fallback, const QVariantList &measures,
                    const QVariantList &tuplets) {
  int origin = fallback;
  for (auto value : measures) {
    const auto m = value.toMap();
    if (tick >= m["start"].toInt() && tick < m["end"].toInt()) {
      origin = m["start"].toInt();
      break;
    }
  }
  if (!triplet)
    for (auto value : tuplets) {
      const auto t = value.toMap();
      if (t["track"].toInt() != track || tick < t["start"].toInt() ||
          tick >= t["start"].toInt() + t["duration"].toInt())
        continue;
      const auto ratio = t["ratio"].toString().split('/');
      if (ratio.size() == 2 && ratio[0].toInt() > 0 && ratio[1].toInt() > 0) {
        origin = t["start"].toInt();
        quantum = qMax(
            1, qRound(double(quantum) * ratio[1].toInt() / ratio[0].toInt()));
      }
      break;
    }
  return rollQuantizeTick(tick, origin, quantum);
}
QVariantMap
rollAffectedScope(const QVariantList &current, const QVariantList &previous,
                  const QVariantMap &scope, const QVariantList &measures,
                  const QVariantList &tuplets, const QVariantList &tracks) {
  QMap<QString, QVariantMap> before, after;
  QSet<int> wanted;
  for (auto t : tracks)
    wanted.insert(t.toInt());
  for (auto value : previous) {
    const auto row = value.toMap();
    before[row["id"].toString()] = row;
  }
  for (auto value : current) {
    const auto row = value.toMap();
    after[row["id"].toString()] = row;
  }
  int from = scope["end"].toInt(), until = scope["start"].toInt();
  auto include = [&](const QVariantMap &n) {
    from = qMin(from, n["start"].toInt());
    until = qMax(until, n["end"].toInt());
  };
  for (auto i = before.cbegin(); i != before.cend(); ++i)
    if (after.value(i.key()) != i.value())
      include(i.value());
  for (auto i = after.cbegin(); i != after.cend(); ++i)
    if (before.value(i.key()) != i.value())
      include(i.value());
  if (until <= from)
    return scope;
  bool expanded;
  do {
    expanded = false;
    const int oldFrom = from, oldUntil = until;
    for (auto value : measures) {
      const auto m = value.toMap();
      if (m["start"].toInt() < until && m["end"].toInt() > from) {
        from = qMin(from, m["start"].toInt());
        until = qMax(until, m["end"].toInt());
      }
    }
    for (const auto &rows : {current, previous})
      for (auto value : rows) {
        const auto n = value.toMap();
        if (wanted.contains(n["track"].toInt()) && n["start"].toInt() < until &&
            n["end"].toInt() > from)
          include(n);
      }
    for (auto value : tuplets) {
      const auto t = value.toMap();
      if (wanted.contains(t["track"].toInt()) && t["start"].toInt() < until &&
          t["start"].toInt() + t["duration"].toInt() > from) {
        from = qMin(from, t["start"].toInt());
        until = qMax(until, t["start"].toInt() + t["duration"].toInt());
      }
    }
    expanded = from != oldFrom || until != oldUntil;
  } while (expanded);
  auto result = scope;
  result["start"] = qMax(scope["start"].toInt(), from);
  result["end"] = qMin(scope["end"].toInt(), until);
  return result;
}
QVariantMap buildRollEdit(const QVariantList &current,
                          const QVariantList &previous,
                          const QVariantMap &fullScope,
                          const QVariantList &measures,
                          const QVariantList &originalTuplets,
                          const QVariantMap &baseSettings, bool local,
                          const std::atomic_bool *cancel) {
  auto cancelled = [&] {
    return cancel && cancel->load(std::memory_order_relaxed);
  };
  QMap<QString, QVariantMap> before, after;
  for (auto value : previous) {
    if (cancelled())
      return {{"cancelled", true}};
    auto row = value.toMap();
    before[row["id"].toString()] = row;
  }
  for (auto value : current) {
    if (cancelled())
      return {{"cancelled", true}};
    auto row = value.toMap();
    after[row["id"].toString()] = row;
  }
  QSet<int> changed;
  for (auto i = before.cbegin(); i != before.cend(); ++i)
    if (after.value(i.key()) != i.value())
      changed.insert(i.value()["track"].toInt());
  for (auto i = after.cbegin(); i != after.cend(); ++i)
    if (before.value(i.key()) != i.value())
      changed.insert(i.value()["track"].toInt());
  QVariantList tracks;
  for (int track : changed)
    tracks.append(track);
  if (changed.isEmpty())
    return {{"unchanged", true},
            {"notes", current},
            {"scope", fullScope},
            {"changedTracks", tracks},
            {"baseSettings", baseSettings}};
  auto settings = baseSettings;
  settings["tracks"] = tracks;
  auto scope = local ? rollAffectedScope(current, previous, fullScope, measures,
                                         originalTuplets, tracks)
                     : fullScope;
  if (cancelled())
    return {{"cancelled", true}};
  QVariantList notes = current, tuplets = originalTuplets;
  if (local) {
    QVariantList inside;
    for (auto value : notes) {
      auto row = value.toMap();
      if (row["start"].toInt() >= scope["end"].toInt() ||
          row["end"].toInt() <= scope["start"].toInt())
        continue;
      if (row["start"].toInt() < scope["start"].toInt() ||
          row["end"].toInt() > scope["end"].toInt()) {
        row["start"] = qMax(row["start"].toInt(), scope["start"].toInt());
        row["end"] = qMin(row["end"].toInt(), scope["end"].toInt());
        row["contextOnly"] = true;
        row["edited"] = false;
        row["new"] = false;
        row.remove("sourceId");
        row.remove("copySourceId");
      }
      inside.append(row);
    }
    notes = inside;
    QVariantList insideTuplets;
    for (auto value : tuplets) {
      const auto t = value.toMap();
      if (t["start"].toInt() >= scope["start"].toInt() &&
          t["start"].toInt() + t["duration"].toInt() <= scope["end"].toInt())
        insideTuplets.append(t);
    }
    tuplets = insideTuplets;
  }
  auto result =
      buildRollNotation(notes, scope, measures, tuplets, settings, cancel);
  if (!result.contains("error") && !result.value("cancelled").toBool()) {
    // Automatic voice allocation can move every new note away from the requested
    // voice. Rebuild only the voices that actually changed after allocation.
    for (auto value : result["notes"].toList()) {
      if (cancelled())
        return {{"cancelled", true}};
      const auto row = value.toMap();
      if (!row["contextOnly"].toBool())
        after[row["id"].toString()] = row;
    }
    changed.clear();
    for (auto i = before.cbegin(); i != before.cend(); ++i)
      if (after.value(i.key()) != i.value())
        changed.insert(i.value()["track"].toInt());
    for (auto i = after.cbegin(); i != after.cend(); ++i)
      if (before.value(i.key()) != i.value())
        changed.insert(i.value()["track"].toInt());
    tracks.clear();
    for (int track : changed)
      tracks.append(track);
    QVariantList voices;
    int eventCount = 0;
    for (auto value : result["voices"].toList())
      if (changed.contains(value.toMap()["track"].toInt())) {
        voices.append(value);
        eventCount += value.toMap()["events"].toList().size();
      }
    result["voices"] = voices;
    result["eventCount"] = eventCount;
    settings["tracks"] = tracks;
  }
  result["changedTracks"] = tracks;
  result["baseSettings"] = baseSettings;
  result["settings"] = settings;
  return result;
}
QVariantMap buildRollNotation(const QVariantList &input,
                              const QVariantMap &scope,
                              const QVariantList &measures,
                              const QVariantList &originalTuplets,
                              const QVariantMap &settings,
                              const std::atomic_bool *cancel) {
  const int from = scope.value("start", -1).toInt(),
            until = scope.value("end", -1).toInt();
  const int first = scope.value("firstTrack", -1).toInt(),
            last = scope.value("endTrack", -1).toInt();
  if (from < 0 || until <= from || first < 0 || last <= first ||
      input.size() > 16000)
    return problem(QObject::tr("请选择有小节和谱表的写音范围。"));
  const int quantum = settings.value("quantum", 120).toInt();
  if (quantum < 15 || quantum > 1920)
    return problem(QObject::tr("选择四分、八分、十六分等网格后写音。"));
  auto cancelled = [&] {
    return cancel && cancel->load(std::memory_order_relaxed);
  };
  QSet<int> touched;
  for (auto value : settings.value("tracks").toList())
    touched.insert(value.toInt());
  const bool limitTracks = settings.contains("tracks");
  QSet<int> locked;
  for (auto track : settings.value("lockedTracks").toList())
    locked.insert(track.toInt());
  auto origin = [&](int at) {
    for (auto value : measures) {
      const auto m = value.toMap();
      if (at >= m["start"].toInt() && at < m["end"].toInt())
        return m["start"].toInt();
    }
    return from;
  };
  for (auto value : settings.value("tracks").toList())
    if (locked.contains(value.toInt()))
      return problem(
          QObject::tr("谱表 %1 的声部 %2 "
                      "有待采用的改动，请解锁或在上方筛选中显示这个声部。")
              .arg(value.toInt() / 4 + 1)
              .arg(value.toInt() % 4 + 1));
  QVariantList notes;
  QSet<QString> ids;
  for (auto value : input) {
    if (cancelled())
      return {{"cancelled", true}};
    auto row = value.toMap();
    const int track = row.value("track", -1).toInt(),
              pitch = row.value("pitch", -1).toInt();
    if (track < first || track >= last || pitch < 0 || pitch > 127 ||
        row["id"].toString().isEmpty() || ids.contains(row["id"].toString()))
      return problem(
          QObject::tr("有一个音的位置或声部已经变化，请重新读取这段。"));
    ids.insert(row["id"].toString());
    if (row.value("edited").toBool() && !row.value("quantized", false).toBool()) {
      if (locked.contains(track))
        return problem(
            QObject::tr("谱表 %1 的声部 %2 已锁定，可在“声部锁定”中解锁。 ")
                .arg(track / 4 + 1)
                .arg(track % 4 + 1));
      const int rawStart = row.value("rawStart", row["start"]).toInt(),
                rawEnd = row.value("rawEnd", row["end"]).toInt();
      row["start"] = qBound(
          from,
          rollSnapInScore(rawStart, quantum, settings.value("triplet").toBool(),
                          track, from, measures, originalTuplets),
          qMax(from, until - quantum));
      int minimum = quantum;
      for (auto value : originalTuplets) {
        const auto t = value.toMap();
        if (t["track"].toInt() != track || rawStart < t["start"].toInt() ||
            rawStart >= t["start"].toInt() + t["duration"].toInt() ||
            settings.value("triplet").toBool())
          continue;
        const auto ratio = t["ratio"].toString().split('/');
        if (ratio.size() == 2 && ratio[0].toInt() > 0)
          minimum = qMax(
              1, qRound(double(quantum) * ratio[1].toInt() / ratio[0].toInt()));
      }
      row["end"] = qBound(
          qMin(until, row["start"].toInt() + minimum),
          rollSnapInScore(rawEnd, quantum, settings.value("triplet").toBool(),
                          track, from, measures, originalTuplets),
          until);
    }
    if (row["start"].toInt() < from || row["end"].toInt() > until ||
        row["end"].toInt() <= row["start"].toInt())
      return problem(
          QObject::tr("这个音伸出了写音范围，请扩大范围或拖短音符。"));
    notes.append(row);
  }
  // Existing material keeps its voice. A newly drawn overlap can use another
  // unlocked voice on the same staff, with the requested voice tried first.
  if (settings.value("suggestVoices", true).toBool()) {
    for (int index = 0; index < notes.size(); ++index) {
      if (cancelled())
        return {{"cancelled", true}};
      auto row = notes[index].toMap();
      if (!row["new"].toBool())
        continue;
      const int requested = row["track"].toInt(), staff = requested / 4;
      auto free = [&](int track) {
        if (track < first || track >= last || locked.contains(track))
          return false;
        for (int j = 0; j < notes.size(); ++j) {
          if (j == index)
            continue;
          const auto other = notes[j].toMap();
          if (other["track"].toInt() != track)
            continue;
          if (other["new"].toBool() && j > index)
            continue;
          if (incompatible(row, other))
            return false;
        }
        return true;
      };
      int chosen = free(requested) ? requested : -1;
      for (int voice = 0; voice < 4 && chosen < 0; ++voice)
        if (free(staff * 4 + voice))
          chosen = staff * 4 + voice;
      if (chosen < 0)
        return problem(QObject::tr("这组音需要另一个声部。四个声部已有内容或被"
                                   "锁定，请调整音长或解锁声部。"));
      row["track"] = chosen;
      touched.insert(chosen);
      notes[index] = row;
    }
  }
  QMap<int, QList<QVariantMap>> byTrack;
  for (auto value : notes) {
    auto n = value.toMap();
    byTrack[n["track"].toInt()].append(n);
  }
  QVariantList voices;
  int eventCount = 0;
  for (int track = first; track < last; ++track) {
    if (cancelled())
      return {{"cancelled", true}};
    if (locked.contains(track) || (limitTracks && !touched.contains(track)))
      continue;
    const auto rows = byTrack.value(track);
    QMap<int, QVector<int>> begins, ends;
    QSet<int> cutSet{from, until};
    for (auto value : measures) {
      auto m = value.toMap();
      for (auto key : {"start", "end"}) {
        const int at = m[key].toInt();
        if (at > from && at < until)
          cutSet.insert(at);
      }
    }
    for (int i = 0; i < rows.size(); ++i) {
      const auto &n = rows[i];
      begins[n["start"].toInt()].append(i);
      ends[n["end"].toInt()].append(i);
      cutSet.insert(n["start"].toInt());
      cutSet.insert(n["end"].toInt());
    }
    QVariantList tuplets;
    for (auto value : originalTuplets) {
      auto t = value.toMap();
      if (t["track"].toInt() != track)
        continue;
      const int begin = t["start"].toInt(), duration = t["duration"].toInt();
      if (begin < from || begin + duration > until)
        return problem(QObject::tr("范围碰到连音组，请把整组包含进来。"));
      const auto ratio = t["ratio"].toString().split('/');
      if (ratio.size() != 2)
        return problem(QObject::tr("请连同完整连音组一起读取。"));
      tuplets.append(QVariantMap{{"quarterTime", quarters(begin)},
                                 {"quarterDuration", quarters(duration)},
                                 {"actual", ratio[0].toInt()},
                                 {"normal", ratio[1].toInt()}});
      cutSet.insert(begin);
      cutSet.insert(begin + duration);
    }
    {
      QMap<int, int> groups;
      const QVariantList bars =
          measures.isEmpty()
              ? QVariantList{QVariantMap{{"start", from}, {"end", until}}}
              : measures;
      for (const auto &n : rows)
        if (n["edited"].toBool() &&
            (n.contains("tripletGrid") ? n["tripletGrid"].toBool()
                                       : settings.value("triplet").toBool()))
          for (auto barValue : bars) {
            const auto bar = barValue.toMap();
            const int barStart = qMax(from, bar["start"].toInt()),
                      barEnd = qMin(until, bar["end"].toInt());
            if (n["start"].toInt() >= barEnd || n["end"].toInt() <= barStart)
              continue;
            for (int at = barStart; at < barEnd;) {
              int group = 3 * n.value("rasterQuantum", quantum).toInt();
              while (group > barEnd - at && group >= 6)
                group /= 2;
              if (group > barEnd - at || group < 3 || group % 3)
                return problem(QObject::tr("这里的剩余拍数适合更细的连音网格，"
                                           "请选择十六分三连音或扩大范围。"));
              bool covered = false;
              for (auto value : tuplets) {
                const auto t = value.toMap(), start = t["quarterTime"].toMap(),
                           length = t["quarterDuration"].toMap();
                const double begin = double(start["numerator"].toInt()) /
                                     start["denominator"].toInt() * quarter,
                             end = begin + double(length["numerator"].toInt()) /
                                               length["denominator"].toInt() *
                                               quarter;
                covered |= at < end && at + group > begin;
              }
              if (at < n["end"].toInt() && at + group > n["start"].toInt() &&
                  !covered)
                groups[at] = group;
              at += group;
            }
          }
      for (auto i = groups.cbegin(); i != groups.cend(); ++i) {
        tuplets.append(QVariantMap{{"quarterTime", quarters(i.key())},
                                   {"quarterDuration", quarters(i.value())},
                                   {"actual", 3},
                                   {"normal", 2}});
        cutSet.insert(i.key());
        cutSet.insert(i.key() + i.value());
      }
    }
    std::sort(
        tuplets.begin(), tuplets.end(),
        [](const QVariant &a, const QVariant &b) {
          const auto x = a.toMap()["quarterTime"].toMap(),
                     y = b.toMap()["quarterTime"].toMap();
          return double(x["numerator"].toInt()) / x["denominator"].toInt() <
                 double(y["numerator"].toInt()) / y["denominator"].toInt();
        });
    auto cuts = cutSet.values();
    std::sort(cuts.begin(), cuts.end());
    QSet<int> active;
    QVariantList events;
    for (int at = 0; at + 1 < cuts.size(); ++at) {
      if (cancelled())
        return {{"cancelled", true}};
      const int start = cuts[at], end = cuts[at + 1];
      for (int i : ends.value(start))
        active.remove(i);
      for (int i : begins.value(start))
        active.insert(i);
      QVariantList pitches;
      QSet<int> distinct;
      auto ordered = active.values();
      std::sort(ordered.begin(), ordered.end(), [&](int a, int b) {
        return rows[a]["pitch"].toInt() < rows[b]["pitch"].toInt();
      });
      for (int i : ordered) {
        const auto &n = rows[i];
        const int pitch = n["pitch"].toInt();
        if (distinct.contains(pitch))
          return problem(QObject::tr(
              "同一声部的同音重叠了，请移到另一个声部或调整起止。"));
        distinct.insert(pitch);
        QVariantMap p{{"editId", n["id"]},
                      {"pitch", pitch},
                      {"tpc1", n["tpc1"]},
                      {"tpc2", n["tpc2"]},
                      {"tieNext", n["end"].toInt() > end}};
        if (!n["sourceId"].toString().isEmpty())
          p["sourceId"] = n["sourceId"];
        if (!n["copySourceId"].toString().isEmpty())
          p["copySourceId"] = n["copySourceId"];
        if (n.contains("velocity") &&
            ((n["new"].toBool() && n["copySourceId"].toString().isEmpty() &&
              !n["followDynamics"].toBool()) ||
             n["velocityEdited"].toBool()))
          p["velocity"] = n["velocity"];
        if (settings.value("preserveTiming").toBool() && n["edited"].toBool() &&
            n.value("customEventCount", 1).toInt() <= 1)
          p["performance"] = QVariantMap{
              {"offset",
               n.value("rawStart", n["start"]).toInt() - n["start"].toInt()},
              {"duration", n.value("rawEnd", n["end"]).toInt() -
                               n.value("rawStart", n["start"]).toInt()}};
        pitches.append(p);
      }
      events.append(QVariantMap{{"quarterTime", quarters(start)},
                                {"quarterDuration", quarters(end - start)},
                                {"pitches", pitches}});
      ++eventCount;
    }
    voices.append(QVariantMap{
        {"track", track}, {"events", events}, {"tuplets", tuplets}});
  }
  if (voices.isEmpty())
    return problem(QObject::tr("当前声部都已锁定，请选择一个可写声部。"));
  return {{"voices", voices},
          {"notes", notes},
          {"eventCount", eventCount},
          {"scope", scope}};
}
} // namespace Ms
