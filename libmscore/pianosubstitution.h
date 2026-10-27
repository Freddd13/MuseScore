// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include "pianohands.h"
#include <functional>

namespace Ms { namespace PianoSubstitution {

struct Change {
      bool valid=false;
      double seconds=0;
      int physicalReleases=0;
      QVector<int> released;
      QVector<int> after;
      };

// Values only. The reader resolves a key's fingering at a candidate time;
// no score or playback object belongs to this search.
struct Frame {
      const QVector<PianoHands::Key>& keys;
      const PianoHands::Plan& plan;
      const PianoHands::Limits (&limits)[2];
      const QVector<double>& changes;
      QSet<int> followers;
      std::function<PianoHands::Key(int,double)> keyAt;
      std::function<bool(int,double)> releasedAt;
      };

inline Change choose(const Frame& frame,int index,int target,int hand,
                     double lower,double upper,bool before)
      {
      Change best;
      if(hand<0 || hand>1 || !std::isfinite(lower) || lower>=upper)return best;
      QVector<double> times{lower};
      if(!before) {
            for(const auto& key:frame.keys) {
                  if(std::isfinite(key.endSeconds))times.append(std::nextafter(key.endSeconds,INFINITY));
                  if(std::isfinite(key.releaseSeconds))times.append(std::nextafter(key.releaseSeconds,INFINITY));
                  }
            for(double time:frame.changes)times.append(std::nextafter(time,INFINITY));
            }
      std::sort(times.begin(),times.end());
      times.erase(std::unique(times.begin(),times.end()),times.end());

      for(double when:times) {
            if(when<lower || when>=upper || frame.keys[index].endSeconds<=when)continue;
            QVector<PianoHands::Key> trial;
            QVector<int> source;
            int selected=-1;
            for(int j=0;j<frame.keys.size();++j) {
                  if(frame.plan.hands[j]!=hand || (before&&frame.keys[j].attack)
                     || (!before&&frame.plan.released.contains(j)) || frame.releasedAt(j,when))continue;
                  auto key=frame.keyAt(j,when);
                  if(std::isfinite(key.endSeconds) && key.endSeconds<=when)continue;
                  key.hand=key.hint=hand;
                  key.attack=false;
                  key.releasable=std::isfinite(key.releaseSeconds)&&key.releaseSeconds<=when;
                  if(j==index || frame.followers.contains(j)) {
                        key.finger=0;
                        key.fingerMask=1<<(target-1);
                        key.releasable=false;
                        if(j==index)selected=trial.size();
                        }
                  trial.append(key);
                  source.append(j);
                  }
            if(selected<0)continue;
            PianoHands::Position empty[2];
            const auto proposal=PianoHands::plan(trial,frame.limits,empty,when);
            if(!proposal.after[hand].fits || proposal.uncertain)continue;
            QSet<int> releasedPitches;
            for(int i:proposal.released)releasedPitches.insert(trial[i].pitch);
            const int releaseCount=releasedPitches.size();
            if(best.valid && (releaseCount>best.physicalReleases
               || (releaseCount==best.physicalReleases&&when>=best.seconds)))continue;

            Change change;
            change.valid=true;
            change.seconds=when;
            change.physicalReleases=releaseCount;
            for(int i:proposal.released)change.released.append(source[i]);
            for(int j=0;j<frame.keys.size();++j) {
                  const auto& key=frame.keys[j];
                  if(j!=index && frame.plan.hands[j]==hand && (key.fingerMask&(1<<(target-1)))
                     && std::isfinite(key.endSeconds) && key.endSeconds<=when)change.after.append(j);
                  }
            best=change;
            }
      return best;
      }
}}
