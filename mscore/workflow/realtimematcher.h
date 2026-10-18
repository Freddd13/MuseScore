// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QSet>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace Ms {
// Values only. A bounded set of paths follows pitch sets and onset intervals.
// Skipped score groups and extra performed groups remain separate choices.
class RealtimeMatcher {
public:
  struct Group {
    int tick;
    double seconds;
    QSet<int> pitches;
  };

private:
  struct Path {
    int index = -1, paired = 0;
    double cost = 0, performance = -1, reference = 0, rate = 0;
  };
  QVector<Group> _score;
  QVector<Path> _paths;
  bool _tap = false, _uncertain = true;
  int _observed = 0;

public:
  void reset(const QVector<Group> &score, bool tap) {
    _score = score;
    _tap = tap;
    _paths = {Path{}};
    _observed = 0;
    _uncertain = true;
  }
  void observe(const QSet<int> &pitches, double seconds) {
    if (pitches.isEmpty() || _score.isEmpty() || !std::isfinite(seconds))
      return;
    ++_observed;
    QVector<Path> candidates;
    candidates.reserve(_paths.size() * 9);
    for (const auto &before : _paths) {
      auto extra = before;
      extra.cost += .8;
      candidates.append(extra);
      const int until = qMin(_score.size(), before.index + 9);
      for (int index = before.index + 1; index < until; ++index) {
        const auto &group = _score[index];
        int common = 0;
        for (int pitch : pitches)
          if (group.pitches.contains(pitch))
            ++common;
        const double distance =
            _tap ? 0.
                 : 1. - 2. * common /
                            qMax(1, pitches.size() + group.pitches.size());
        const double scoreInterval = group.seconds - before.reference;
        const double playedInterval = seconds - before.performance;
        auto after = before;
        after.cost += .6 * (index - before.index - 1) + 1.2 * distance;
        if (before.performance >= 0 && scoreInterval > .001 &&
            playedInterval > .015) {
          const double rate = qBound(.125, playedInterval / scoreInterval, 8.);
          if (before.rate > 0)
            after.cost += (_tap ? .85 : .22) *
                          qMin(2.5, qAbs(std::log2(rate / before.rate)));
          if (distance < .6)
            after.rate =
                before.rate > 0 ? .75 * before.rate + .25 * rate : rate;
        }
        after.index = index;
        after.paired++;
        after.performance = seconds;
        after.reference = group.seconds;
        candidates.append(after);
      }
    }
    std::stable_sort(
        candidates.begin(), candidates.end(),
        [](const Path &a, const Path &b) { return a.cost < b.cost; });
    _paths.clear();
    for (const auto &candidate : candidates) {
      bool duplicate = false;
      for (const auto &kept : _paths)
        if (candidate.index == kept.index &&
            (!(candidate.rate > 0 && kept.rate > 0) ||
             qAbs(std::log2(candidate.rate / kept.rate)) < .35)) {
          duplicate = true;
          break;
        }
      if (!duplicate)
        _paths.append(candidate);
      if (_paths.size() == 32)
        break;
    }
    _uncertain = _paths.isEmpty() || _paths.front().index < 0;
    if (!_paths.isEmpty())
      for (int i = 1; i < _paths.size(); ++i)
        if (_paths[i].index != _paths.front().index) {
          _uncertain |= _paths[i].cost - _paths.front().cost < .18;
          break;
        }
  }
  int index() const { return _paths.isEmpty() ? -1 : _paths.front().index; }
  int tick() const { return index() < 0 ? -1 : _score[index()].tick; }
  bool uncertain() const { return _uncertain; }
  bool atEnd() const {
    if (_paths.isEmpty() || _score.isEmpty() || _uncertain)
      return false;
    const auto &path = _paths.front();
    return path.index == _score.size() - 1 &&
           path.paired >= std::ceil(_score.size() * .7) &&
           path.cost <= qMax(.5, _observed * .4);
  }
};
} // namespace Ms
