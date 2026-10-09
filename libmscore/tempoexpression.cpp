#include "tempoexpression.h"
#include "score.h"
#include "textline.h"
#include "tempotext.h"
#include "tempo.h"
#include "measure.h"
#include "segment.h"
#include "fermata.h"
#include "breath.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace Ms {
namespace TempoExpression {
QVector<TextLine*> curves(Score* score)
      {
      QVector<TextLine*> result;
      for (const auto& pair : score->masterScore()->spannerMap().map())
            if (pair.second->isTextLine()) {
                  auto line = toTextLine(pair.second);
                  if (line->ritPlay() && line->tick2() > line->tick()) result.append(line);
                  }
      std::stable_sort(result.begin(), result.end(), [](const TextLine* a, const TextLine* b) {
            if (a->tick() != b->tick()) return a->tick() < b->tick();
            if (a->tick2() != b->tick2()) return a->tick2() < b->tick2();
            return a->track() < b->track();
            });
      return result;
      }

QString conflict(Score* score, int from, int until, const TextLine* ignore)
      {
      if (ignore && ignore->score() != score->masterScore())
            for (auto linked : ignore->linkList()) if (linked->isTextLine() && linked->score() == score->masterScore()) { ignore = static_cast<TextLine*>(linked); break; }
      if (from < 0 || until <= from || !score->lastMeasure() || until > score->lastMeasure()->endTick().ticks())
            return QObject::tr("渐变速度的范围必须在谱内，且终点晚于起点。");
      for (auto line : curves(score))
            if (line != ignore && line->tick().ticks() < until && line->tick2().ticks() > from)
                  return QObject::tr("与已有渐变速度重叠；请编辑原曲线或明确替换该范围。");
      for (auto measure = score->masterScore()->firstMeasure(); measure; measure = measure->nextMeasure()) {
            if (measure->endTick().ticks() <= from) continue;
            if (measure->tick().ticks() > until) break;
            for (auto& segment : measure->segments()) {
                  if (segment.tick().ticks() <= from || segment.tick().ticks() >= until) continue;
                  for (auto annotation : segment.annotations()) if (annotation->isTempoText())
                        return QObject::tr("该范围已有速度节点；请明确选择替换该范围速度。");
                  }
            }
      return {};
      }

qreal target(const TextLine* line, qreal startBps)
      {
      return qBound(5.0 / 60, line->ritTargetMode() ? line->ritTarget() / 60 : startBps * line->ritTarget() / 100, 999.0 / 60);
      }
qreal value(const TextLine* line, qreal startBps, double tick)
      {
      double fraction = qBound(0.0, (tick - line->tick().ticks()) / line->ticks().ticks(), 1.0);
      return startBps + (target(line, startBps) - startBps) * std::pow(fraction, line->ritCurve());
      }

namespace {
// Integrate seconds per tick while preparing the map. Harmonic interval tempos
// preserve cumulative time without changing TempoMap's piecewise-constant engine.
double integral(const std::function<double(double)>& f, double a, double b)
      {
      auto simpson = [](double x, double y, double fx, double fm, double fy) { return (y - x) * (fx + 4 * fm + fy) / 6; };
      std::function<double(double, double, double, double, double, double, int)> recurse;
      recurse = [&](double x, double y, double fx, double fm, double fy, double estimate, int depth) {
            double mid = (x + y) / 2, leftMid = f((x + mid) / 2), rightMid = f((mid + y) / 2);
            double left = simpson(x, mid, fx, leftMid, fm), right = simpson(mid, y, fm, rightMid, fy);
            double correction = left + right - estimate;
            if (!depth || std::abs(correction) < 1e-10 * (1 + std::abs(left + right))) return left + right + correction / 15;
            return recurse(x, mid, fx, leftMid, fm, left, depth - 1) + recurse(mid, y, fm, rightMid, fy, right, depth - 1);
            };
      double fa = f(a), fm = f((a + b) / 2), fb = f(b);
      return recurse(a, b, fa, fm, fb, simpson(a, b, fa, fm, fb), 18);
      }
struct Action { int kind; TempoText* text; TextLine* line; }; // end, text, start
struct Stretch { int from, until; double factor; };
}

void rebuild(Score* input)
      {
      auto score = input->masterScore();
      if (!score->_tempoExpressionsPresent || !score->lastMeasure()) return;
      std::map<int, std::vector<Action>> actions;
      std::map<int, double> pauses;
      // Includes breaths and section-break pauses from the original rebuild.
      for (const auto& event : *score->tempomap())
            if (event.second.pause > 0) pauses[event.first] = event.second.pause;
      std::vector<Stretch> stretches;
      std::set<int> boundaries {0, score->lastMeasure()->endTick().ticks()};
      bool active = false;
      for (auto measure = score->firstMeasure(); measure; measure = measure->nextMeasure())
            for (auto& segment : measure->segments()) {
                  int tick = segment.tick().ticks();
                  if (segment.isBreathType()) {
                        for (auto e : segment.elist()) if (e && e->isBreath()) pauses[tick] = qMax(pauses[tick], toBreath(e)->pause());
                        boundaries.insert(tick);
                        }
                  if (!segment.isChordRestType()) continue;
                  double stretch = 0;
                  for (auto annotation : segment.annotations()) {
                        if (annotation->isTempoText()) {
                              auto text = toTempoText(annotation);
                              actions[tick].push_back({1, text, nullptr}); boundaries.insert(tick);
                              active |= text->restoreMode() != 0;
                              }
                        else if (annotation->isFermata() && toFermata(annotation)->play()) stretch = qMax(stretch, double(toFermata(annotation)->timeStretch()));
                        }
                  if (stretch > 0 && !qFuzzyCompare(stretch, 1.0) && segment.ticks().ticks() > 1) {
                        int until = tick + segment.ticks().ticks() - 1;
                        stretches.push_back({tick, until, stretch}); boundaries.insert(tick); boundaries.insert(until);
                        }
                  }
      for (auto line : curves(score)) {
            active = true;
            if (!conflict(score, line->tick().ticks(), line->tick2().ticks(), line).isEmpty()) continue;
            actions[line->tick().ticks()].push_back({2, nullptr, line});
            actions[line->tick2().ticks()].push_back({0, nullptr, line});
            boundaries.insert(line->tick().ticks()); boundaries.insert(line->tick2().ticks());
            }
      for (const auto& pause : pauses) boundaries.insert(pause.first);
      if (!active) { score->_tempoExpressionsPresent = false; return; }
      for (auto& pair : actions) std::stable_sort(pair.second.begin(), pair.second.end(), [](const Action& a, const Action& b) { return a.kind < b.kind; });
      std::map<int, TEvent> events;
      double current = Score::defaultTempo(), initial = current, previousRamp = current, rampStart = current;
      TextLine* ramp = nullptr;
      for (auto point = boundaries.begin(); point != boundaries.end(); ++point) {
            int tick = *point;
            for (const auto& action : actions[tick]) {
                  if (action.kind == 0 && ramp == action.line) { current = target(ramp, rampStart); ramp = nullptr; }
                  else if (action.kind == 1) {
                        auto text = action.text;
                        switch (text->restoreMode()) {
                              case 1: current = previousRamp; break;
                              case 2: current = initial; break;
                              case 3: current = text->tempo(); break;
                              default: current = text->isRelative() ? current * text->relativeTempo() : text->tempo(); break;
                              }
                        current = qBound(5.0 / 60, current, 999.0 / 60);
                        if (tick == 0) initial = current;
                        }
                  else if (action.kind == 2) {
                        ramp = action.line; previousRamp = current;
                        rampStart = ramp->ritStartBpm() > 0 ? ramp->ritStartBpm() / 60 : current;
                        ramp->setRitDerivedStart(rampStart);
                        for (auto linked : ramp->linkList())
                              if (linked->isTextLine()) static_cast<TextLine*>(linked)->setRitDerivedStart(rampStart);
                        }
                  }
            auto next = std::next(point);
            int until = next == boundaries.end() ? tick : *next;
            double stretch = 0;
            for (const auto& s : stretches) if (s.from <= tick && s.until > tick) stretch = qMax(stretch, s.factor);
            if (stretch <= 0) stretch = 1;
            int step = ramp ? qMax(1, qMin(15, ramp->ticks().ticks() / 256)) : qMax(1, until - tick);
            if (ramp) step = qMax(step, (ramp->ticks().ticks() + 199999) / 200000);
            auto put = [&](int when, double bps) {
                  TEvent e(bps, pauses[when], TempoType::FIX);
                  if (pauses[when]) e.type |= TempoType::PAUSE;
                  if (ramp) e.type |= TempoType::RAMP;
                  events[when] = e;
                  };
            if (until <= tick) { put(tick, current / stretch); continue; }
            for (int from = tick; from < until;) {
                  int to = from + qMin(step, until - from);
                  double bps = current / stretch;
                  if (ramp) {
                        const auto secondsPerTick = [&](double t) { return stretch / (DIVISION * value(ramp, rampStart, t)); };
                        double seconds = integral(secondsPerTick, from, to);
                        // The interval integral is exact; also bound the prefix
                        // deviation at its sole extremum for this monotone curve.
                        // Refine only during map preparation, never in audio code.
                        while (to - from > 1) {
                              const double average = seconds / (to - from);
                              double lo = from, hi = to;
                              const bool increasing = secondsPerTick(to) > secondsPerTick(from);
                              for (int i = 0; i < 24; ++i) {
                                    const double mid = (lo + hi) / 2;
                                    if ((secondsPerTick(mid) < average) == increasing) lo = mid; else hi = mid;
                                    }
                              const double extremum = (lo + hi) / 2;
                              if (std::abs(integral(secondsPerTick, from, extremum) - (extremum - from) * average) < .00005) break;
                              to = from + (to - from) / 2;
                              seconds = integral(secondsPerTick, from, to);
                              }
                        bps = (to - from) / (DIVISION * seconds);
                        }
                  put(from, bps); from = to;
                  }
            }
      score->tempomap()->replaceEvents(events);
      }
}
}
