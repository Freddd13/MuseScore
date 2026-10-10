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

namespace Ms {
struct MidiGateOptions {
      double lengthRatio = .85;
      double nextAttackRatio = .90;
      double minimumMs = 120.;
      double jitter = .02;
      quint32 seed = 13013;
      QSet<int> staves; // empty = all recognized piano staves
      QSet<QString> excluded;
      };
struct MidiExportOptions { bool crop = false; MidiGateOptions gate; };
struct MidiGateNote {
      QString id, partName;
      int track = 0, offOrdinal = -1, staff = -1, voice = -1;
      int port = 0, channel = 0, pitch = 60, on = 0, off = 0, newOff = 0;
      bool piano = false, known = false, manual = false, ambiguous = false;
      QString reason;
      };
struct MidiGatePedal { int port=0, channel=0, down=0, up=0; bool supported=true; };
struct MidiGateTempo { int tick=0; double micros=500000.; };
struct MidiGateData {
      int division = 480;
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
                  auto& n=result[i];n.newOff=n.off;
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
            for(auto& n:result) {
                  if(canceled && canceled())return {};
                  auto preserve=[&](const char* reason){n.reason=QString::fromLatin1(reason);};
                  if(!n.piano || (!option.staves.isEmpty()&&!option.staves.contains(n.staff))) {preserve("unselected");continue;}
                  if(!n.known || n.offOrdinal<0) {preserve("unknown-source");continue;}
                  if(n.manual) {preserve("manual-performance");continue;}
                  if(n.ambiguous || n.off<=n.on) {preserve("ambiguous-retrigger");continue;}
                  if(option.excluded.contains(n.id)) {preserve("excluded");continue;}
                  const double start=seconds(n.on), length=seconds(n.off)-start;
                  const double minimum=qBound(1.,option.minimumMs,5000.)/1000.;
                  if(length<=minimum) {preserve("short-note");continue;}
                  double desired=length*qBound(.1,option.lengthRatio,1.);
                  const auto& group=attacks[key(n.port,n.channel)+":"+QString::number(n.voice)];
                  auto next=std::upper_bound(group.cbegin(),group.cend(),n.on);
                  if(next!=group.cend())desired=qMin(desired,(seconds(*next)-start)*qBound(.1,option.nextAttackRatio,1.));
                  const double noise=(double(random(n.id,option.seed))/std::numeric_limits<quint32>::max()*2.-1.)*qBound(0.,option.jitter,.05);
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
            return result;
            }
      };
}
