// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVector>
#include <QSet>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QtMath>
#include <algorithm>
#include <limits>
#include <functional>
#include <climits>
#include "libmscore/pianohands.h"
#include "libmscore/pianofingering.h"
#include "libmscore/pianosubstitution.h"

namespace Ms {
struct MidiGateOptions {
      double lengthRatio = .85;
      double nextAttackRatio = .90;
      double minimumMs = 120.;
      double jitter = .02;
      quint32 seed = 13013;
      QSet<int> staves; // empty = all recognized piano staves
      QSet<QString> excluded;
      QMap<QString,int> releaseOverrides; // Exact exported occurrence, this export only.
      int scoreStart=-1,scoreEnd=-1; // physical score ticks; unrolled MIDI time stays untouched
      QSet<int> tracks,protectedTracks; // explicit staff+voice; empty selection = all
      QSet<int> voices,protectedVoices; // local voice numbers 0..3
      bool protectMelody=false;
      bool naturalRelease=true;
      double releaseLeadMs=60.;
      PianoHands::Limits hands[2];
      };
struct MidiExportOptions {
      bool crop = false; MidiGateOptions gate; bool writtenPedal = false;
      bool splitHands = false;
      QMap<QString,int> handOverrides; // Export occurrence: 0 left, 1 right.
      };
// An instruction on a tied continuation, mapped to this exported occurrence.
// tick includes the normal export pause compensation; sourceTick is written time.
struct MidiGateInstruction {
      int tick=0,sourceTick=-1;
      int measure=-1;
      double beat=0;
      bool handSet=false,fingeringSet=false;
      int hand=-1,finger=0,fingerMask=0;
      QStringList fingerings;
      };
struct MidiGateNote {
      QString id, partName;
      int track = 0, onOrdinal = -1, offOrdinal = -1, staff = -1, voice = -1;
      int displayStaff=-1,exportHand=-1;
      QString handSource,exportHandSource;
      bool handReview=false;
      int port = 0, channel = 0, pitch = 60, on = 0, off = 0, newOff = 0;
      int sourceTick=-1,sourceMeasure=-1,sourceUtick=-1;
      int partId=-1,hand=-1,handHint=-1,finger=0;
      int fingerMask=0;
      QStringList fingerings;
      QVector<MidiGateInstruction> instructions;
      int requiredUntil=-1;
      struct FingerChange {int from=0,to=0,tick=0;QVector<int> releasedPitches,afterPitches;};
      QVector<FingerChange> fingerChanges;
      bool fingeringUnresolved=false;
      bool melody=false,handInferred=false;
      bool piano = false, known = false, manual = false, ambiguous = false;
      QString reason,handWarning;
      int beforeKeys=-1,afterKeys=-1,beforeSpan=-1,afterSpan=-1;
      int releaseFor=-1;
      QVector<int> nextPitches;
      };
struct MidiGatePedal { int port=0, channel=0, down=0, up=0; bool supported=true; };
struct MidiGateTempo { int tick=0; double micros=500000.; };
struct MidiGateData {
      int division = 480;
      bool writtenPedal = false;
      bool expandRepeats = true, exportRPNs = true;
      QVector<MidiGateNote> notes;
      QVector<MidiGatePedal> pedals;
      QVector<MidiGateTempo> tempos;
      };

// Only value data. No Score/Note/QObject, audio state or file IO is consulted.
class MidiGateProcessor {
      struct ClockPoint { int tick; double seconds, secondsPerTick; };
      QVector<ClockPoint> _clock;
      static QString key(int port,int channel) { return QString::number(port)+":"+QString::number(channel); }
      static quint32 random(QString id, quint32 seed) {
            quint32 value = seed ? seed : 1;
            for (QChar ch:id) value = (value ^ ch.unicode())*16777619u;
            value ^= value << 13; value ^= value >> 17; value ^= value << 5;
            return value;
            }
      static QString preserved(const MidiGateNote& n,const MidiGateOptions& option) {
            if(!n.piano || (!option.staves.isEmpty()&&!option.staves.contains(n.staff)))return "unselected";
            if(!n.known || n.offOrdinal<0)return "unknown-source";
            if(option.scoreStart>=0&&(n.sourceTick<option.scoreStart||n.sourceTick>=option.scoreEnd))return "outside-range";
            if(!option.tracks.isEmpty()&&!option.tracks.contains(n.voice))return "unselected-voice";
            if(option.protectedTracks.contains(n.voice))return "protected-melody";
            if(!option.voices.isEmpty()&&(n.voice<0||!option.voices.contains(n.voice%4)))return "unselected-voice";
            if((option.protectMelody&&n.melody)||(n.voice>=0&&option.protectedVoices.contains(n.voice%4)))return "protected-melody";
            if(n.manual)return "manual-performance";
            if(n.ambiguous || n.off<=n.on)return "ambiguous-retrigger";
            if(option.excluded.contains(n.id))return "excluded";
            if(n.hand==-2)return "hand-conflict";
            if(n.finger<0)return "fingering-uncertain";
            if(n.fingeringUnresolved)return "fingering-unresolved";
            for(const auto& p:n.instructions) {
                  if(p.handSet&&p.hand==-2)return "hand-conflict";
                  if(p.fingeringSet&&p.finger<0)return "fingering-uncertain";
                  }
            return {};
            }
      static QString partKey(const MidiGateNote& n) {
            return n.partId>=0?QString("part:%1").arg(n.partId):QString("port:%1:channel:%2").arg(n.port).arg(n.channel);
            }
      static bool physicalRelease(const QString& reason) {return reason=="hand-release" || reason=="finger-release";}
      double seconds(int tick) const {
            auto i=std::upper_bound(_clock.cbegin(),_clock.cend(),tick,[](int t,const ClockPoint& p){return t<p.tick;});
            if(i!=_clock.cbegin())--i;
            return i->seconds+(tick-i->tick)*i->secondsPerTick;
            }
      int tickAt(double time) const {
            auto i=std::upper_bound(_clock.cbegin(),_clock.cend(),time,[](double t,const ClockPoint& p){return t<p.seconds;});
            if(i!=_clock.cbegin())--i;
            return int(qCeil(i->tick+(time-i->seconds)/i->secondsPerTick));
            }
   public:
      QPair<int,int> releaseBounds(const MidiGateNote& note,const QVector<MidiGatePedal>& pedals,const MidiGateOptions& option) const {
            if(!preserved(note,option).isEmpty())return {-1,-1};
            const int minimum=tickAt(seconds(note.on)+qBound(1.,option.minimumMs,5000.)/1000.);
            for(const auto& pedal:pedals)if(pedal.port==note.port&&pedal.channel==note.channel&&pedal.supported&&note.off<=pedal.up&&note.off>pedal.down){
                  const int first=qMax(qMax(qMax(note.on+1,minimum),pedal.down+1),note.requiredUntil+1);if(first<note.off)return {first,note.off};}
            return {-1,-1};
            }
      int tickForMilliseconds(double milliseconds) const {return tickAt(milliseconds/1000.);}
      double durationMs(int from,int until) const { return (seconds(until)-seconds(from))*1000.; }
      void setClock(const MidiGateData& input) {
            _clock.clear();
            _clock.append({0,0.,.5/qMax(1,input.division)});
            auto tempos=input.tempos;
            std::stable_sort(tempos.begin(),tempos.end(),[](const auto& a,const auto& b){return a.tick<b.tick;});
            for(const auto& tempo:tempos) {
                  if(tempo.tick<0 || !qIsFinite(tempo.micros) || tempo.micros<=0.)continue;
                  double time=seconds(tempo.tick);
                  if(_clock.last().tick==tempo.tick)_clock.removeLast();
                  _clock.append({tempo.tick,time,tempo.micros/1000000./qMax(1,input.division)});
                  }
            }
      QVector<MidiGateNote> process(const MidiGateData& input,const MidiGateOptions& option,const std::function<bool()>& canceled = {}) {
            QSet<QString> unresolved;
            for(;;) {
                  if(canceled&&canceled())return {};
                  auto result=processPass(input,option,canceled,unresolved);bool retry=false;
                  for(const auto& note:result)if(note.fingeringUnresolved&&!unresolved.contains(note.id)){unresolved.insert(note.id);retry=true;}
                  if(!retry)return result;
                  }
            }
   private:
      QVector<MidiGateNote> processPass(const MidiGateData& input,const MidiGateOptions& option,const std::function<bool()>& canceled,const QSet<QString>& unresolved) {
            setClock(input);
            auto result=input.notes;
            QMap<QString,QVector<int>> samePitch, attacks;
            for(int i=0;i<result.size();++i) {
                  if(canceled && canceled())return {};
                  auto& n=result[i];n.newOff=n.off;n.handInferred=n.hand<0;n.reason.clear();n.handWarning.clear();n.releaseFor=-1;n.nextPitches.clear();n.beforeKeys=n.afterKeys=n.beforeSpan=n.afterSpan=-1;
                  if(!n.fingerChanges.isEmpty()){n.requiredUntil=-1;for(const auto& point:n.instructions)n.requiredUntil=qMax(n.requiredUntil,point.tick);}
                  n.fingerChanges.clear();n.fingeringUnresolved=unresolved.contains(n.id);
                  samePitch[key(n.port,n.channel)+":"+QString::number(n.pitch)].append(i);
                  if(n.voice>=0)attacks[key(n.port,n.channel)+":"+QString::number(n.voice)].append(n.on);
                  }
            for(auto& group:samePitch) {
                  if(canceled && canceled())return {};
                  std::sort(group.begin(),group.end(),[&](int a,int b){return result[a].on<result[b].on;});
                  // Connected overlap components; each interval is visited only twice.
                  for(int begin=0;begin<group.size();) {
                        int end=begin+1,maxOff=result[group[begin]].off;
                        while(end<group.size() && result[group[end]].on<maxOff) {
                              maxOff=qMax(maxOff,result[group[end]].off);++end;
                              }
                        if(end-begin>1)for(int i=begin;i<end;++i)result[group[i]].ambiguous=true;
                        begin=end;
                        }
                  }
            for(auto& group:attacks) {std::sort(group.begin(),group.end());group.erase(std::unique(group.begin(),group.end()),group.end());}
            QMap<QString,QVector<MidiGatePedal>> pedals;
            for(const auto& p:input.pedals)pedals[key(p.port,p.channel)].append(p);
            for(auto& group:pedals)std::sort(group.begin(),group.end(),[](const auto& a,const auto& b){return a.down<b.down;});
            auto releaseWindow=[&](const MidiGateNote& n,int at) {
                  if(at<=n.requiredUntil)return false;
                  const auto& windows=pedals[key(n.port,n.channel)];
                  auto p=std::upper_bound(windows.cbegin(),windows.cend(),at,[](int t,const MidiGatePedal& w){return t<w.down;});
                  if(p==windows.cbegin())return false;--p;
                  return p->supported && p->down<at && at<n.off && n.off<=p->up;
                  };
            QSet<QString> fixedReleases;
            for(auto& n:result) {
                  auto found=option.releaseOverrides.constFind(n.id);if(found==option.releaseOverrides.cend())continue;
                  const int at=found.value();
                  if(at==n.off){fixedReleases.insert(n.id);n.reason="manual-original";}
                  else if(preserved(n,option).isEmpty()&&at>n.on&&at<n.off&&durationMs(n.on,at)>=qBound(1.,option.minimumMs,5000.)&&releaseWindow(n,at)) {
                        fixedReleases.insert(n.id);n.newOff=at;n.reason="manual-release";
                        }
                  }
            struct Substitution {int epoch=-1;QVector<int> fingers,times;bool complete()const{return times.size()+1==fingers.size();}};
            QHash<int,Substitution> substitutions;
            auto sequenceAt=[&](int i,int tick,int* epoch) {
                  const auto& n=result[i];QStringList texts=n.fingerings;*epoch=n.on;
                  for(const auto& p:n.instructions)if(p.tick<=tick&&p.fingeringSet){texts=p.fingerings;*epoch=p.tick;}
                  QVector<int> sequence;
                  for(const auto& text:texts){const auto steps=PianoFingering::substitution(text);if(steps.isEmpty())continue;if(!sequence.isEmpty()&&sequence!=steps)return QVector<int>{-1};sequence=steps;}
                  return sequence;
                  };
            QMap<QString,QMap<int,QVector<int>>> parts;
            QVector<int> assignedHands;for(const auto& n:result)assignedHands.append(n.hand);
            auto keyAt=[&](int i,int tick) {
                  const auto& n=result[i];const bool attack=n.on==tick;
                  int hand=assignedHands[i],finger=n.finger,mask=n.fingerMask;
                  for(const auto& point:n.instructions) {
                        if(point.tick>tick)break;
                        if(point.handSet)hand=point.hand;
                        if(point.fingeringSet){finger=point.finger;mask=point.fingerMask;}
                        }
                  int epoch=0;const auto sequence=sequenceAt(i,tick,&epoch);const auto state=substitutions.constFind(i);
                  if(finger>=0&&sequence.size()>1) {
                        int stage=state!=substitutions.cend()&&state->epoch==epoch?int(std::upper_bound(state->times.cbegin(),state->times.cend(),tick)-state->times.cbegin()):0;
                        finger=sequence[qMin(stage,sequence.size()-1)];mask=1<<(finger-1);
                        } else if(sequence.size()==1){finger=-1;mask=0;}
                  else if(mask)for(auto sub=substitutions.cbegin();sub!=substitutions.cend();++sub) {
                        const auto& owner=result[sub.key()];if(partKey(owner)!=partKey(n)||owner.pitch!=n.pitch||owner.on!=n.on||epoch>sub->epoch||!(mask&(1<<(sub->fingers.first()-1))))continue;
                        const int stage=int(std::upper_bound(sub->times.cbegin(),sub->times.cend(),tick)-sub->times.cbegin());if(stage>0){finger=sub->fingers[qMin(stage,sub->fingers.size()-1)];mask=1<<(finger-1);}
                        }
                  const int candidate=tickAt(seconds(tick)-qBound(0.,option.releaseLeadMs,500.)/1000.);
                  const bool pending=sequence.size()>1&&(state==substitutions.cend()||state->epoch!=epoch||!state->complete()||state->times.last()>candidate);
                  const bool legal=!attack && !pending && !fixedReleases.contains(n.id) && preserved(n,option).isEmpty() && candidate>n.on && durationMs(n.on,candidate)>=qBound(1.,option.minimumMs,5000.) && releaseWindow(n,candidate);
                  PianoHands::Key key{i,n.pitch,hand,n.handHint,attack,legal,qMax(0,finger)};
                  key.fingerMask=finger<0?0:mask;key.endSeconds=seconds(n.newOff);
                  if(legal)key.releaseSeconds=seconds(candidate);
                  return key;
                  };
            for(int i=0;i<result.size();++i)if(result[i].piano && result[i].off>result[i].on) {
                  auto& frames=parts[partKey(result[i])];frames[result[i].on].append(i);
                  for(const auto& point:result[i].instructions)if(point.tick>result[i].on && point.tick<result[i].off)frames[point.tick];
                  }
            for(auto& frames:parts) {
                  QVector<int> active;PianoHands::Position previous[2];
                  for(auto frame=frames.cbegin();frame!=frames.cend();++frame) {
                        if(canceled && canceled())return {};
                        const int tick=frame.key();const double now=seconds(tick);
                        for(auto s=substitutions.begin();s!=substitutions.end();) {
                              const auto& n=result[s.key()];bool follower=false;
                              if(n.newOff<=tick)for(int i:active)if(result[i].pitch==n.pitch&&result[i].on==n.on&&result[i].newOff>tick){follower=true;break;}
                              if(n.newOff<=tick&&!follower){if(!s->complete())result[s.key()].fingeringUnresolved=true;s=substitutions.erase(s);}else ++s;
                              }
                        active.erase(std::remove_if(active.begin(),active.end(),[&](int i){return result[i].newOff<=tick;}),active.end());active+=frame.value();
                        for(int i:active) {
                              if(preserved(result[i],option)=="hand-conflict")continue;
                              int epoch=0;const auto sequence=sequenceAt(i,tick,&epoch);auto old=substitutions.find(i);
                              if(old!=substitutions.end()&&old->epoch!=epoch) {
                                    if(!old->complete())result[i].fingeringUnresolved=true;
                                    substitutions.erase(old);old=substitutions.end();
                                    }
                              if(!unresolved.contains(result[i].id)&&sequence.size()>1&&old==substitutions.end())substitutions.insert(i,{epoch,sequence,{}});
                              }
                        QVector<PianoHands::Key> keys;QVector<int> releaseAt;
                        for(int i:active) {
                              const int candidate=tickAt(now-qBound(0.,option.releaseLeadMs,500.)/1000.);
                              keys.append(keyAt(i,tick));releaseAt.append(candidate);
                              }
                        QVector<PianoHands::Future> future;QVector<int> futureTicks;
                        auto nextFrame=frame;
                        for(int f=0;f<2 && ++nextFrame!=frames.cend();++f) {
                              const int nextTick=nextFrame.key();const double nextTime=seconds(nextTick);
                              PianoHands::Future next;next.seconds=nextTime;
                              QVector<int> upcoming=active;
                              for(auto added=frame; ++added!=frames.cend() && added.key()<=nextTick;)upcoming+=added.value();
                              for(int i:upcoming) {
                                    const auto& n=result[i];if(n.newOff<=nextTick)continue;
                                    next.keys.append(keyAt(i,nextTick));
                                    }
                              future.append(next);
                              futureTicks.append(nextTick);
                              }
                        auto plan=PianoHands::planWithContext(keys,option.hands,previous,now,future);
                        if(plan.limited || plan.uncertain)continue;
                        auto refresh=[&] {
                              active.erase(std::remove_if(active.begin(),active.end(),[&](int i){return result[i].newOff<=tick;}),active.end());
                              keys.clear();releaseAt.clear();for(int i:active){keys.append(keyAt(i,tick));releaseAt.append(tickAt(now-qBound(0.,option.releaseLeadMs,500.)/1000.));}
                              for(int f=0;f<future.size();++f){QVector<PianoHands::Key> next;for(auto k:future[f].keys)if(result[k.id].newOff>futureTicks[f])next.append(keyAt(k.id,futureTicks[f]));future[f].keys=next;}
                              plan=PianoHands::planWithContext(keys,option.hands,previous,now,future);
                              };
                        auto attemptSwap=[&](int selected,int lower,int upper,bool before) {
                              const int owner=active[selected],side=plan.hands[selected];auto sub=substitutions.find(owner);
                              if(sub==substitutions.end()||sub->complete()||result[owner].fingeringUnresolved||side<0||lower>=upper||plan.limited||plan.uncertain)return false;
                              if(!sub->times.isEmpty())lower=qMax(lower,sub->times.last()+1);
                              if(lower>=upper)return false;
                              const int from=sub->fingers[sub->times.size()],to=sub->fingers[sub->times.size()+1],epoch=sub->epoch;
                              QVector<double> changes;for(const auto& s:substitutions)for(int at:s.times)changes.append(seconds(at));
                              QSet<int> followers;
                              for(int j=0;j<keys.size();++j)if(j!=selected&&result[active[j]].pitch==result[owner].pitch&&result[active[j]].on==result[owner].on) {
                                    int otherEpoch=0;const auto sequence=sequenceAt(active[j],tick,&otherEpoch);
                                    if(otherEpoch<=epoch&&(sequence==sub->fingers||sequence.isEmpty()&&(keys[j].fingerMask&(1<<(sub->fingers.first()-1)))))followers.insert(j);
                                    }
                              PianoSubstitution::Frame search{keys,plan,option.hands,changes,followers,
                                    [&](int j,double when){const int i=active[j];auto key=keyAt(i,tickAt(when));
                                          if(!fixedReleases.contains(result[i].id)) {
                                                auto pending=substitutions.constFind(i);const auto range=releaseBounds(result[i],input.pedals,option);
                                                if(range.first>=0&&(pending==substitutions.cend()||pending->complete()))key.releaseSeconds=seconds(range.first);
                                                }
                                          return key;},
                                    [&](int j,double when){return seconds(result[active[j]].newOff)<=when;}};
                              const auto proposal=PianoSubstitution::choose(search,selected,to,side,seconds(lower),seconds(upper),before);
                              if(!proposal.valid)return false;
                              const int at=tickAt(proposal.seconds);if(at<lower||at>=upper||at>=result[owner].newOff)return false;
                              for(int j:proposal.released) {
                                    const auto& n=result[active[j]];
                                    if(fixedReleases.contains(n.id)||!preserved(n,option).isEmpty()||!releaseWindow(n,at)||durationMs(n.on,at)<qBound(1.,option.minimumMs,5000.))return false;
                                    }
                              const auto saved=sub->times;sub->times.append(at);
                              bool compatible=true;
                              for(int f=0;f<future.size()&&compatible;++f) {
                                    if(futureTicks[f]<=at)continue;
                                    QVector<PianoHands::Key> beforeKeys,afterKeys;
                                    sub->times=saved;for(const auto& k:future[f].keys)beforeKeys.append(keyAt(k.id,futureTicks[f]));
                                    sub->times.append(at);for(const auto& k:future[f].keys) {
                                          bool released=false;for(int j:proposal.released)released|=active[j]==k.id;
                                          if(!released)afterKeys.append(keyAt(k.id,futureTicks[f]));
                                          }
                                    PianoHands::Position empty[2];const auto a=PianoHands::plan(beforeKeys,option.hands,empty,future[f].seconds),b=PianoHands::plan(afterKeys,option.hands,empty,future[f].seconds);
                                    compatible=b.after[side].fingerConflicts<=a.after[side].fingerConflicts&&(!a.after[side].fits||b.after[side].fits);
                                    }
                              sub->times=saved;if(!compatible)return false;sub->times.append(at);
                              for(auto s=substitutions.begin();s!=substitutions.end();++s)if(s.key()!=owner&&result[s.key()].pitch==result[owner].pitch&&result[s.key()].on==result[owner].on&&s->epoch==epoch&&s->fingers==sub->fingers)s->times=sub->times;
                              MidiGateNote::FingerChange change;change.from=from;change.to=to;change.tick=at;
                              for(int j:proposal.after)if(!change.afterPitches.contains(keys[j].pitch))change.afterPitches.append(keys[j].pitch);
                              for(int j:proposal.released) {
                                    auto& n=result[active[j]];n.newOff=at;n.reason="finger-release";n.releaseFor=result[owner].on;n.nextPitches={result[owner].pitch};
                                    n.handWarning=QString::fromUtf8("先松开这个音，再完成%1→%2指换指；踏板继续保持声音").arg(from).arg(to);
                                    if(!change.releasedPitches.contains(n.pitch))change.releasedPitches.append(n.pitch);
                                    }
                              result[owner].fingerChanges.append(change);result[owner].requiredUntil=qMax(result[owner].requiredUntil,at);
                              for(int j:followers)result[active[j]].requiredUntil=qMax(result[active[j]].requiredUntil,at);
                              return true;
                              };
                        // A held key may exchange fingers before this new attack.
                        for(int pass=0;pass<5;++pass) {
                              bool changed=false;
                              for(int k=0;k<active.size();++k){auto s=substitutions.constFind(active[k]);if(s==substitutions.cend()||s->epoch>=tick||s->complete()||plan.hands[k]<0||plan.after[plan.hands[k]].fits)continue;
                                    if(attemptSwap(k,qMax(s->epoch+1,tick-1),tick,true)){changed=true;break;}}
                              if(!changed)break;refresh();
                              }
                        for(int k=0;k<active.size();++k){auto s=substitutions.constFind(active[k]);const int hand=plan.hands[k];
                              if(s!=substitutions.cend()&&!s->complete()&&s->epoch<tick&&hand>=0&&!plan.after[hand].fits&&plan.before[hand].fingerConflicts>0)
                                    result[active[k]].fingeringUnresolved=true;
                              }
                        // Search the interval before the next musical change, including
                        // the times at which another key or finger actually becomes free.
                        const int boundary=std::next(frame)==frames.cend()?INT_MAX:std::next(frame).key();
                        for(int pass=0;pass<5;++pass) {
                              bool changed=false;
                              for(int k=0;k<active.size();++k){auto s=substitutions.constFind(active[k]);if(s==substitutions.cend()||s->complete()||plan.hands[k]<0||!plan.after[plan.hands[k]].fits||plan.released.contains(k))continue;
                                    const int lower=qMax(tick+1,s->times.isEmpty()?s->epoch+1:s->times.last()+1);
                                    if(attemptSwap(k,lower,qMin(boundary,result[active[k]].newOff),false)){changed=true;refresh();break;}}
                              if(!changed)break;
                              }
                        bool changedHand[2]={false,false};
                        for(int k=0;k<active.size();++k) {
                              if(keys[k].hand==-2)continue;
                              const int index=active[k];auto& n=result[index];const int side=plan.hands[k];if(side<0)continue;
                              const int before=assignedHands[index];if(before!=side){if(before>=0)changedHand[before]=true;changedHand[side]=true;}
                              assignedHands[index]=side;if(n.on==tick)n.hand=side;
                              if(n.on==tick && !plan.after[side].fits) {
                                    QVector<PianoHands::Key> attacksOnly;for(int j=0;j<keys.size();++j)if(keys[j].attack && plan.hands[j]==side) {auto key=keys[j];key.hand=side;key.releasable=false;attacksOnly.append(key);}
                                    const auto fresh=PianoHands::plan(attacksOnly,option.hands,previous,now);
                                    n.handWarning=fresh.possible ? QString::fromUtf8("需要保持的旧音占用了这只手，可检查保护设置和最短按键时长") : QString::fromUtf8("这组新音仍需要调整跨度、指法或分手");
                                    }
                              if(plan.after[side].fits && plan.released.contains(k)) {
                                    n.newOff=releaseAt[k];n.reason="hand-release";n.releaseFor=tick;
                                    if(plan.before[side].fingerConflicts>plan.after[side].fingerConflicts)n.handWarning=QString::fromUtf8("这组音标注的指法有重叠，松手后可分别使用手指");
                                    for(int j=0;j<keys.size();++j)if(keys[j].attack && plan.hands[j]==side && !n.nextPitches.contains(keys[j].pitch))n.nextPitches.append(keys[j].pitch);
                                    std::sort(n.nextPitches.begin(),n.nextPitches.end());n.beforeKeys=plan.before[side].count;n.afterKeys=plan.after[side].count;n.beforeSpan=plan.before[side].high-plan.before[side].low;n.afterSpan=plan.after[side].high-plan.after[side].low;
                                    }
                              }
                        for(int side=0;side<2;++side) {
                              bool attacked=false;for(int j=0;j<keys.size();++j)attacked|=keys[j].attack && plan.hands[j]==side;
                              if(attacked || changedHand[side])previous[side]=PianoHands::positionFor(keys,plan,side,now);
                              }
                        }
                  }
            for(auto sub=substitutions.cbegin();sub!=substitutions.cend();++sub)if(!sub->complete())result[sub.key()].fingeringUnresolved=true;
            QMap<QString,QVector<double>> groupLengths;
            QMap<QString,QVector<int>> handAttacks;
            auto handKey=[&](const MidiGateNote& n){return partKey(n)+":"+QString::number(n.hand>=0?n.hand:n.handHint);};
            for(const auto& n:result) {
                  if(n.piano)handAttacks[handKey(n)].append(n.on);
                  if(preserved(n,option).isEmpty() && !physicalRelease(n.reason) && durationMs(n.on,n.off)>qBound(1.,option.minimumMs,5000.))groupLengths[handKey(n)+":"+QString::number(n.on)].append(seconds(n.off)-seconds(n.on));
                  }
            for(auto& group:handAttacks){std::sort(group.begin(),group.end());group.erase(std::unique(group.begin(),group.end()),group.end());}
            for(auto& group:groupLengths)std::sort(group.begin(),group.end());
            for(auto& n:result) {
                  if(canceled && canceled())return {};
                  auto preserve=[&](const char* reason){n.reason=QString::fromLatin1(reason);};
                  const auto protection=preserved(n,option);if(!protection.isEmpty()){n.newOff=n.off;n.reason=protection;continue;}
                  if(physicalRelease(n.reason)||fixedReleases.contains(n.id))continue;
                  if(!option.naturalRelease){preserve("natural-disabled");continue;}
                  const double start=seconds(n.on), length=seconds(n.off)-start;
                  const double minimum=qBound(1.,option.minimumMs,5000.)/1000.;
                  if(length<=minimum) {preserve("short-note");continue;}
                  const auto& lengths=groupLengths[handKey(n)+":"+QString::number(n.on)];
                  const double commonLength=lengths.isEmpty()?length:lengths.first();
                  double desired=std::min(length,commonLength)*qBound(.1,option.lengthRatio,1.);
                  const auto& handGroup=handAttacks[handKey(n)];auto handNext=std::upper_bound(handGroup.cbegin(),handGroup.cend(),n.on);
                  if(handNext!=handGroup.cend())desired=qMin(desired,(seconds(*handNext)-start)*qBound(.1,option.nextAttackRatio,1.));
                  const auto& group=attacks[key(n.port,n.channel)+":"+QString::number(n.voice)];
                  auto next=std::upper_bound(group.cbegin(),group.cend(),n.on);
                  if(next!=group.cend())desired=qMin(desired,(seconds(*next)-start)*qBound(.1,option.nextAttackRatio,1.));
                  const QString releaseGroup=partKey(n)+":"+QString::number(n.hand)+":"+QString::number(n.on);
                  const double common=double(random(releaseGroup,option.seed))/std::numeric_limits<quint32>::max()*2.-1.;
                  const double individual=double(random(n.id,option.seed))/std::numeric_limits<quint32>::max()*2.-1.;
                  const double noise=(.9*common+.1*individual)*qBound(0.,option.jitter,.05);
                  desired=qMax(minimum,desired*(1.+noise));
                  const int candidate=qMax(tickAt(start+desired),n.requiredUntil+1);
                  if(candidate<=n.on || candidate>=n.off) {preserve("no-shortening");continue;}
                  const auto& windows=pedals[key(n.port,n.channel)];
                  auto p=std::upper_bound(windows.cbegin(),windows.cend(),candidate,[](int t,const MidiGatePedal& w){return t<w.down;});
                  if(p==windows.cbegin()) {preserve("no-pedal");continue;}
                  --p;
                  if(!p->supported) {preserve("unsupported-pedal");continue;}
                  if(!(p->down<candidate && candidate<n.off && n.off<=p->up)) {preserve("pedal-boundary");continue;}
                  n.newOff=candidate;n.reason="pedal-release";
                  }
            for(auto& note:result){
                  const auto override=option.releaseOverrides.constFind(note.id);if(override==option.releaseOverrides.cend())continue;
                  const int at=override.value();
                  if(at==note.off){if(note.reason=="hand-release")note.handWarning=QString::fromUtf8("继续按住这个旧音会影响下一组音，可再拖动尾端安排松手");note.newOff=note.off;note.reason="manual-original";note.beforeKeys=note.afterKeys=note.beforeSpan=note.afterSpan=-1;continue;}
                  if(preserved(note,option).isEmpty()&&at>note.on&&at<note.off&&durationMs(note.on,at)>=qBound(1.,option.minimumMs,5000.)&&releaseWindow(note,at)){
                        if(note.reason=="hand-release"&&at>note.newOff)note.handWarning=QString::fromUtf8("松手晚于自动建议，可试听并检查下一组音的手位");note.newOff=at;note.reason="manual-release";note.beforeKeys=note.afterKeys=note.beforeSpan=note.afterSpan=-1;
                        }
                  }
            for(auto& n:result)if(n.requiredUntil>=0 && n.handWarning.isEmpty()) {
                  for(const auto& point:n.instructions)if(point.tick==n.requiredUntil) {
                        const QString kind=point.handSet && !point.fingerings.isEmpty()?QString::fromUtf8("分手和指法"):point.handSet?QString::fromUtf8("分手"):QString::fromUtf8("指法");
                        n.handWarning=point.measure>0?QString::fromUtf8("按延音后面第%1小节第%2拍的%3，保留到该位置之后").arg(point.measure).arg(point.beat,0,'g',5).arg(kind)
                              :QString::fromUtf8("按延音后面的%1，保留到该记号之后").arg(kind);
                        break;
                        }
                  }
            for(auto& n:result) {
                  if(n.fingeringUnresolved)n.handWarning=QString::fromUtf8("换指所需的手指仍被占用，当前区间需要调整指法或分手");
                  else if(!n.fingerChanges.isEmpty()) {
                        QStringList steps;for(const auto& change:n.fingerChanges)steps.append(QString::fromUtf8("%1→%2指：%3秒").arg(change.from).arg(change.to).arg(seconds(change.tick),0,'f',3));
                        const auto text=QString::fromUtf8("安排换指 ")+steps.join(QString::fromUtf8("；"));n.handWarning=n.handWarning.isEmpty()?text:n.handWarning+QString::fromUtf8("；")+text;
                        }
                  }
            return result;
            }
      };
}
