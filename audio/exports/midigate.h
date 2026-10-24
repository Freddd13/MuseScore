// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVector>
#include <QSet>
#include <QMap>
#include <QString>
#include <QtMath>
#include <algorithm>
#include <limits>
#include <functional>
#include "libmscore/pianohands.h"

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
      double releaseLeadMs=60.;
      PianoHands::Limits hands[2];
      };
struct MidiExportOptions { bool crop = false; MidiGateOptions gate; bool writtenPedal = false; };
struct MidiGateNote {
      QString id, partName;
      int track = 0, offOrdinal = -1, staff = -1, voice = -1;
      int port = 0, channel = 0, pitch = 60, on = 0, off = 0, newOff = 0;
      int sourceTick=-1,sourceMeasure=-1,sourceUtick=-1;
      int partId=-1,hand=-1,handHint=-1,finger=0;
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
            return {};
            }
      static QString partKey(const MidiGateNote& n) {
            return n.partId>=0?QString("part:%1").arg(n.partId):QString("port:%1:channel:%2").arg(n.port).arg(n.channel);
            }
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
                  const int first=qMax(qMax(note.on+1,minimum),pedal.down+1);if(first<note.off)return {first,note.off};}
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
            setClock(input);
            auto result=input.notes;
            QMap<QString,QVector<int>> samePitch, attacks;
            for(int i=0;i<result.size();++i) {
                  if(canceled && canceled())return {};
                  auto& n=result[i];n.newOff=n.off;n.handInferred=n.hand<0;n.reason.clear();n.handWarning.clear();n.releaseFor=-1;n.nextPitches.clear();n.beforeKeys=n.afterKeys=n.beforeSpan=n.afterSpan=-1;
                  samePitch[key(n.port,n.channel)+":"+QString::number(n.pitch)].append(i);
                  if(n.voice>=0)attacks[key(n.port,n.channel)+":"+QString::number(n.voice)].append(n.on);
                  }
            for(auto& group:samePitch) {
                  if(canceled && canceled())return {};
                  std::sort(group.begin(),group.end(),[&](int a,int b){return result[a].on<result[b].on;});
                  // Connected overlap components; each interval is visited only twice.
                  for(int begin=0;begin<group.size();) {
                        int end=begin+1,maxOff=result[group[begin]].off;
                        while(end<group.size() && result[group[end]].on<=maxOff) {
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
                  const auto& windows=pedals[key(n.port,n.channel)];
                  auto p=std::upper_bound(windows.cbegin(),windows.cend(),at,[](int t,const MidiGatePedal& w){return t<w.down;});
                  if(p==windows.cbegin())return false;--p;
                  return p->supported && p->down<at && at<n.off && n.off<=p->up;
                  };
            QMap<QString,QMap<int,QVector<int>>> parts;
            for(int i=0;i<result.size();++i)if(result[i].piano && result[i].off>result[i].on)parts[partKey(result[i])][result[i].on].append(i);
            for(auto& frames:parts) {
                  QVector<int> active;PianoHands::Position previous[2];
                  for(auto frame=frames.cbegin();frame!=frames.cend();++frame) {
                        if(canceled && canceled())return {};
                        const int tick=frame.key();const double now=seconds(tick);
                        active.erase(std::remove_if(active.begin(),active.end(),[&](int i){return result[i].newOff<=tick;}),active.end());active+=frame.value();
                        QVector<PianoHands::Key> keys;QVector<int> releaseAt;bool uncertain=false;
                        for(int i:active) {
                              const auto& n=result[i];const bool attack=n.on==tick;uncertain|=n.hand==-2;
                              const int candidate=tickAt(now-qBound(0.,option.releaseLeadMs,500.)/1000.);
                              const double minimum=qBound(1.,option.minimumMs,5000.)/1000.;
                              const bool legal=!attack && preserved(n,option).isEmpty() && candidate>n.on && seconds(candidate)-seconds(n.on)>=minimum && releaseWindow(n,candidate);
                              keys.append({i,n.pitch,n.hand,n.handHint,attack,legal,n.finger});releaseAt.append(candidate);
                              }
                        QVector<PianoHands::Future> future;
                        auto nextFrame=frame;
                        for(int f=0;f<2 && ++nextFrame!=frames.cend();++f) {
                              const int nextTick=nextFrame.key();const double nextTime=seconds(nextTick);
                              PianoHands::Future next;next.seconds=nextTime;
                              QVector<int> upcoming=active;
                              for(auto added=frame; ++added!=frames.cend() && added.key()<=nextTick;)upcoming+=added.value();
                              for(int i:upcoming) {
                                    const auto& n=result[i];if(n.newOff<=nextTick)continue;
                                    const bool attack=n.on==nextTick;const int at=tickAt(nextTime-qBound(0.,option.releaseLeadMs,500.)/1000.);
                                    const bool legal=!attack && preserved(n,option).isEmpty() && at>n.on && durationMs(n.on,at)>=qBound(1.,option.minimumMs,5000.) && releaseWindow(n,at);
                                    next.keys.append({i,n.pitch,n.hand,n.handHint,attack,legal,n.finger});
                                    }
                              future.append(next);
                              }
                        auto plan=PianoHands::planWithContext(keys,option.hands,previous,now,future);
                        if(plan.limited || plan.uncertain || uncertain)continue;
                        for(int k=0;k<active.size();++k) {
                              auto& n=result[active[k]];const int side=plan.hands[k];if(side<0)continue;n.hand=side;
                              if(n.on==tick && !plan.after[side].fits) {
                                    QVector<PianoHands::Key> attacksOnly;for(int j=0;j<keys.size();++j)if(keys[j].attack && plan.hands[j]==side) {auto key=keys[j];key.hand=side;key.releasable=false;attacksOnly.append(key);}
                                    const auto fresh=PianoHands::plan(attacksOnly,option.hands,previous,now);
                                    n.handWarning=fresh.possible ? QString::fromUtf8("需要保持的旧音占用了这只手，可检查保护设置和最短按键时长") : QString::fromUtf8("这组新音仍需要调整跨度、指法或分手");
                                    }
                              if(plan.after[side].fits && plan.released.contains(k)) {
                                    n.newOff=releaseAt[k];n.reason="hand-release";n.releaseFor=tick;
                                    for(int j=0;j<keys.size();++j)if(keys[j].attack && plan.hands[j]==side && !n.nextPitches.contains(keys[j].pitch))n.nextPitches.append(keys[j].pitch);
                                    std::sort(n.nextPitches.begin(),n.nextPitches.end());n.beforeKeys=plan.before[side].count;n.afterKeys=plan.after[side].count;n.beforeSpan=plan.before[side].high-plan.before[side].low;n.afterSpan=plan.after[side].high-plan.after[side].low;
                                    }
                              }
                        for(int side=0;side<2;++side) {
                              bool attacked=false;for(int j=0;j<keys.size();++j)attacked|=keys[j].attack && plan.hands[j]==side;
                              if(attacked && plan.after[side].count)previous[side]={true,plan.after[side].low,plan.after[side].high,now};
                              }
                        }
                  }
            QMap<QString,QVector<double>> groupLengths;
            QMap<QString,QVector<int>> handAttacks;
            auto handKey=[&](const MidiGateNote& n){return partKey(n)+":"+QString::number(n.hand>=0?n.hand:n.handHint);};
            for(const auto& n:result) {
                  if(n.piano)handAttacks[handKey(n)].append(n.on);
                  if(preserved(n,option).isEmpty() && n.reason!="hand-release" && durationMs(n.on,n.off)>qBound(1.,option.minimumMs,5000.))groupLengths[handKey(n)+":"+QString::number(n.on)].append(seconds(n.off)-seconds(n.on));
                  }
            for(auto& group:handAttacks){std::sort(group.begin(),group.end());group.erase(std::unique(group.begin(),group.end()),group.end());}
            for(auto& group:groupLengths)std::sort(group.begin(),group.end());
            for(auto& n:result) {
                  if(canceled && canceled())return {};
                  auto preserve=[&](const char* reason){n.reason=QString::fromLatin1(reason);};
                  const auto protection=preserved(n,option);if(!protection.isEmpty()){n.newOff=n.off;n.reason=protection;continue;}
                  if(n.reason=="hand-release")continue;
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
                  const int candidate=tickAt(start+desired);
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
            return result;
            }
      };
}
