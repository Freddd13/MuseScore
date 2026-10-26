// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QVector>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Ms { namespace PianoHands {
// Pitch is MIDI key position, not a claimed physical difficulty score.
// Values only: this module is also used on the MIDI export copy.
struct Limits { int comfort=14,maximum=15,white=16; };
struct Key {
      int id=0,pitch=60,hand=-1,hint=-1;
      bool attack=true,releasable=false;
      int finger=0;
      double endSeconds=std::numeric_limits<double>::quiet_NaN();
      double releaseSeconds=std::numeric_limits<double>::quiet_NaN();
      };
struct Hold { int pitch=60;double until=0,release=std::numeric_limits<double>::quiet_NaN(); };
struct Position { bool valid=false;int low=0,high=0;double seconds=0;QVector<Hold> holds; };
struct Hand {
      int low=0,high=0,count=0,maximum=15;
      int fingerConflicts=0;
      int crowdedBlackKeys=0;
      bool fits=true;
      };
struct Plan {
      QVector<int> hands,released;
      Hand before[2],after[2];
      bool possible=true,uncertain=false,limited=false;
      double cost=std::numeric_limits<double>::infinity();
      };
struct Movement {
      int distance=0,total=0,holdPitch=-1;
      double available=0,since=0;
      };
inline int distanceTo(const Hand& hand,int low,int high) {
      return std::max({0,hand.high-hand.maximum-low,high-hand.maximum-hand.low});
      }
inline Movement movement(const Hand& hand,const Position& previous,double seconds,bool pedal=false) {
      Movement out;
      if(!hand.count || !hand.fits || !previous.valid)return out;
      out.distance=out.total=distanceTo(hand,previous.low,previous.high);
      out.available=std::max(0.,seconds-previous.seconds);out.since=previous.seconds;
      // A held key constrains the hand until its release. Compare the remaining
      // distance/time at each constraint, rather than treating the entire IOI as free.
      for(const auto& hold:previous.holds) {
            const double until=pedal && std::isfinite(hold.release)?std::min(hold.until,hold.release):hold.until;
            if(!std::isfinite(until) || until<=previous.seconds)continue;
            const int distance=distanceTo(hand,hold.pitch,hold.pitch);if(!distance)continue;
            const double since=std::min(seconds,std::max(previous.seconds,until));
            const double available=std::max(0.,seconds-since);
            if(!out.distance || distance*std::max(1.e-9,out.available)>out.distance*std::max(1.e-9,available)) {
                  out.distance=distance;out.available=available;out.since=since;out.holdPitch=hold.pitch;
                  }
            }
      return out;
      }
inline Position positionFor(const QVector<Key>& keys,const Plan& plan,int side,double seconds) {
      const auto& hand=plan.after[side];if(!hand.count || !hand.fits)return {};
      Position out{true,hand.low,hand.high,seconds,{}};
      for(int i=0;i<keys.size();++i)if(plan.hands[i]==side && !plan.released.contains(i) && std::isfinite(keys[i].endSeconds))
            out.holds.append({keys[i].pitch,keys[i].endSeconds,keys[i].releaseSeconds});
      return out;
      }
inline bool white(int pitch) {
      const int pc=(pitch%12+12)%12;return pc==0 || pc==2 || pc==4 || pc==5 || pc==7 || pc==9 || pc==11;
      }
struct Group {
      int pitch=0,hand=-1,hint=-1;
      bool releasable=true,attack=false;
      QVector<int> members;
      int finger=0;
      };
inline Hand describe(const QVector<Group>& keys,const QVector<int>& hands,int side,const Limits& limits,const QVector<bool>& removed) {
      Hand out;out.maximum=limits.maximum;int used[6]={};int previous=-1;
      for(int i=0;i<keys.size();++i)if(hands[i]==side && !removed[i]) {
            if(!out.count)out.low=out.high=keys[i].pitch;
            else {out.low=std::min(out.low,keys[i].pitch);out.high=std::max(out.high,keys[i].pitch);}
            ++out.count;
            if(keys[i].finger>=1 && keys[i].finger<=5 && used[keys[i].finger]++)++out.fingerConflicts;
            if(previous>=0 && keys[i].pitch-keys[previous].pitch<=1 && (!white(keys[i].pitch) || !white(keys[previous].pitch)))++out.crowdedBlackKeys;
            previous=i;
            }
      if(out.count && white(out.low) && white(out.high))out.maximum=limits.white;
      out.fits=out.count<=5 && out.high-out.low<=out.maximum && !out.fingerConflicts;return out;
      }
inline double strain(const Hand& hand) {
      return std::max(0,hand.high-hand.low-hand.maximum)*4.+std::max(0,hand.count-5)*8.+hand.fingerConflicts*8.;
      }
inline bool releaseForHand(const QVector<Group>& keys,const QVector<int>& hands,int side,const Limits& limits,QVector<bool>* removed) {
      QVector<int> positions;for(int i=0;i<keys.size();++i)if(hands[i]==side)positions.append(i);
      if(positions.isEmpty())return true;
      double best=std::numeric_limits<double>::infinity();QVector<bool> bestRemoved;
      // Enumerate retained low/high keys. Inside that interval, at most five keys
      // can be retained with distinct specified fingers. This minimizes released
      // physical keys without an exponential subset search.
      for(int low=0;low<positions.size();++low)for(int high=low;high<positions.size();++high) {
            const int a=positions[low],b=positions[high];const int maximum=white(keys[a].pitch)&&white(keys[b].pitch)?limits.white:limits.maximum;
            if(keys[b].pitch-keys[a].pitch>maximum)continue;
            QVector<bool> kept(keys.size(),false);int used[6]={},count=0;bool valid=true;
            auto retain=[&](int i) {
                  if(kept[i])return;const int finger=keys[i].finger;
                  if(++count>5 || finger>0 && used[finger]++){valid=false;return;}kept[i]=true;
                  };
            retain(a);retain(b);
            for(int p=0;p<positions.size() && valid;++p)if(!keys[positions[p]].releasable) {
                  if(p<low || p>high)valid=false;else retain(positions[p]);
                  }
            if(!valid)continue;
            for(int p=low;p<=high && count<5;++p) {
                  const int i=positions[p],finger=keys[i].finger;
                  if(!kept[i] && keys[i].releasable && (!finger || !used[finger]))retain(i);
                  }
            auto candidate=*removed;int released=0;
            for(int i:positions){candidate[i]=!kept[i];released+=!kept[i];}
            const auto after=describe(keys,hands,side,limits,candidate);if(!after.fits)continue;
            const double cost=released*100.+std::max(0,after.high-after.low-limits.comfort)*.1+after.crowdedBlackKeys*.08;
            if(cost<best-1e-9){best=cost;bestRemoved=candidate;}
            }
      if(!std::isfinite(best))return false;*removed=bestRemoved;return true;
      }
inline QVector<Plan> candidates(const QVector<Key>& input,const Limits (&limits)[2],const Position (&previous)[2],double seconds,int width) {
      Plan best;QVector<Plan> choices;best.hands.fill(-1,input.size());if(input.isEmpty()){best.cost=0;return {best};}
      QVector<int> order;for(int i=0;i<input.size();++i)order.append(i);
      std::stable_sort(order.begin(),order.end(),[&](int a,int b){return input[a].pitch<input[b].pitch;});
      QVector<Group> groups;bool uncertain=false;
      for(int i:order) {
            const auto& key=input[i];if(groups.isEmpty() || groups.back().pitch!=key.pitch){Group g;g.pitch=key.pitch;groups.append(g);}
            if(key.finger<0)uncertain=true;
            auto& group=groups.back();group.members.append(i);group.attack|=key.attack;group.releasable&=!key.attack && key.releasable;
            if(key.hand>=0 && key.hand<2) {
                  if(group.hand>=0 && group.hand!=key.hand)uncertain=true;
                  else group.hand=key.hand;
                  }
            if(group.hint<0 && key.hint>=0 && key.hint<2)group.hint=key.hint;
            if(key.finger>=1 && key.finger<=5) {
                  if(group.finger && group.finger!=key.finger)uncertain=true;
                  else group.finger=key.finger;
                  }
            }
      // At most 25 split candidates even for dense material; no exponential search.
      if(groups.size()>24){best.limited=true;best.possible=false;best.uncertain=true;return {best};}
      for(int split=0;split<=groups.size();++split) {
            QVector<int> hands;QVector<bool> removed(groups.size(),false);double cost=0;
            for(int i=0;i<groups.size();++i) {
                  int hand=groups[i].hand>=0?groups[i].hand:i<split?0:1;hands.append(hand);
                  if(groups[i].hand<0 && groups[i].hint>=0 && hand!=groups[i].hint)cost+=.4;
                  }
            Plan candidate;candidate.uncertain=uncertain;candidate.hands.fill(-1,input.size());
            for(int side=0;side<2;++side) {
                  candidate.before[side]=describe(groups,hands,side,limits[side],removed);
                  Hand current=candidate.before[side];
                  if(!current.fits && releaseForHand(groups,hands,side,limits[side],&removed))current=describe(groups,hands,side,limits[side],removed);
                  for(int i=0;i<groups.size();++i)if(hands[i]==side && removed[i])cost+=10.;
                  candidate.after[side]=current;candidate.possible&=current.fits;
                  cost+=strain(current)*1000.;
                  if(current.count)cost+=std::max(0,current.high-current.low-limits[side].comfort)*.1;
                  cost+=current.crowdedBlackKeys*.08;
                  if(current.count && previous[side].valid) {
                        const auto motion=movement(current,previous[side],seconds,true);
                        cost+=std::min(5.,motion.distance*.03/std::max(.02,motion.available));
                        }
                  }
            for(int i=0;i<groups.size();++i)for(int member:groups[i].members) {
                  candidate.hands[member]=hands[i];if(removed[i])candidate.released.append(member);
                  }
            candidate.cost=cost;
            bool duplicate=false;for(const auto& old:choices)if(old.hands==candidate.hands && old.released==candidate.released){duplicate=true;break;}
            if(!duplicate)choices.append(candidate);
            }
      std::stable_sort(choices.begin(),choices.end(),[](const Plan& a,const Plan& b){return a.cost<b.cost;});
      if(choices.size()>width)choices.resize(width);
      return choices;
      }
inline Plan plan(const QVector<Key>& input,const Limits (&limits)[2],const Position (&previous)[2],double seconds) {
      return candidates(input,limits,previous,seconds,1).first();
      }
struct Future { QVector<Key> keys; double seconds=0; };
// Compare at most four present hand assignments across the next two attacks.
// Key ids persist across frames; a released key stays released until its next attack.
inline Plan planWithContext(const QVector<Key>& input,const Limits (&limits)[2],const Position (&previous)[2],double seconds,const QVector<Future>& future) {
      if(future.isEmpty())return plan(input,limits,previous,seconds);
      const auto choices=candidates(input,limits,previous,seconds,4);
      bool possible=false;for(const auto& choice:choices)possible|=choice.possible;
      Plan best=choices.first();double bestCost=std::numeric_limits<double>::infinity();
      for(const auto& choice:choices) {
            if(possible && !choice.possible)continue;
            QHash<int,int> assigned;QSet<int> released;Position positions[2]={previous[0],previous[1]};
            auto remember=[&](const QVector<Key>& keys,const Plan& value,double at) {
                  bool changed[2]={false,false};
                  for(int i=0;i<keys.size();++i) {
                        const int before=assigned.value(keys[i].id,-1),after=value.hands[i];
                        if(!keys[i].attack && before!=after){if(before>=0)changed[before]=true;if(after>=0)changed[after]=true;}
                        assigned[keys[i].id]=value.hands[i];
                        if(value.released.contains(i))released.insert(keys[i].id);
                        else if(keys[i].attack)released.remove(keys[i].id);
                        }
                  for(int side=0;side<2;++side) {
                        bool attacked=false;for(int i=0;i<keys.size();++i)attacked|=keys[i].attack && value.hands[i]==side;
                        if(attacked || changed[side])positions[side]=positionFor(keys,value,side,at);
                        }
                  };
            remember(input,choice,seconds);double cost=choice.cost,weight=.5;
            for(int f=0;f<std::min(2,future.size());++f) {
                  QVector<Key> keys;
                  for(auto key:future[f].keys) {
                        if(!key.attack && released.contains(key.id))continue;
                        if(!key.attack && key.hand<0)key.hand=assigned.value(key.id,-1);
                        keys.append(key);
                        }
                  const auto next=plan(keys,limits,positions,future[f].seconds);
                  cost+=weight*next.cost;weight*=.5;
                  if(next.limited || next.uncertain)break;
                  remember(keys,next,future[f].seconds);
                  }
            if(cost<bestCost-1e-9){bestCost=cost;best=choice;}
            }
      return best;
      }
} }
