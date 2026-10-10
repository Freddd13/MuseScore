// SPDX-License-Identifier: GPL-2.0-or-later
#include "midigatefile.h"
#include <QHash>

namespace Ms {
static QString sourceKey(int track,int on,int channel,int pitch) {
      return QString("%1:%2:%3:%4").arg(track).arg(on).arg(channel).arg(pitch);
      }
MidiGateData midiGateSnapshot(const MidiFile& file,const QVector<MidiGateSource>& sources) {
      MidiGateData data;data.division=file.division();
      QHash<QString,QVector<MidiGateSource>> tags;
      for(const auto& s:sources)tags[sourceKey(s.track,s.on,s.channel,s.pitch)].append(s);
      struct Control {int tick,value,controller;};
      QMap<QString,QVector<Control>> controls;
      int trackIndex=0,lastTick=0;
      for(const auto& track:file.tracks()) {
            QMap<int,QVector<int>> pending;
            int ordinal=0;
            for(const auto& pair:track.events()) {
                  const int tick=pair.first;const auto& e=pair.second;lastTick=qMax(lastTick,tick);
                  if(e.type()==ME_META && e.metaType()==META_TEMPO && e.len()==3) {
                        const auto bytes=e.edata();
                        data.tempos.append({tick,double((int(bytes[0])<<16)|(int(bytes[1])<<8)|bytes[2])});
                        }
                  if(e.type()==ME_CONTROLLER && (e.controller()==64 || e.controller()==66 || e.controller()==69 || e.controller()==121 || e.controller()==120 || e.controller()==123)) {
                        const QString key=QString("%1:%2").arg(track.outPort()).arg(e.channel());
                        controls[key].append({tick,e.value(),e.controller()});
                        }
                  if(e.type()==ME_PITCHBEND && (e.dataA()!=0 || e.dataB()!=64)) {
                        const QString controlKey=QString("%1:%2").arg(track.outPort()).arg(e.channel());
                        controls[controlKey].append({tick,0,-1});
                        }
                  const int key=e.channel()*128+e.pitch();
                  if(e.type()==ME_NOTEON && e.velo()>0) {
                        MidiGateNote n;n.track=trackIndex;n.port=track.outPort();n.channel=e.channel();n.pitch=e.pitch();n.on=tick;
                        n.id=sourceKey(trackIndex,tick,e.channel(),e.pitch())+":"+QString::number(ordinal);
                        const auto list=tags.value(sourceKey(trackIndex,tick,e.channel(),e.pitch()));
                        if(list.size()==1) {const auto& s=list.first();n.known=true;n.staff=s.staff;n.voice=s.voice;n.partName=s.partName;n.piano=s.piano;n.manual=s.manual;}
                        pending[key].append(data.notes.size());data.notes.append(n);
                        }
                  else if(e.type()==ME_NOTEOFF || (e.type()==ME_NOTEON && e.velo()==0)) {
                        auto& list=pending[key];
                        if(!list.isEmpty()) {
                              const int index=list.takeFirst();auto& n=data.notes[index];n.off=n.newOff=tick;n.offOrdinal=ordinal;
                              if(!list.isEmpty()) {n.ambiguous=true;for(int other:list)data.notes[other].ambiguous=true;}
                              }
                        }
                  ++ordinal;
                  }
            for(const auto& list:pending)for(int index:list) {data.notes[index].ambiguous=true;data.notes[index].off=lastTick;}
            ++trackIndex;
            }
      for(auto i=controls.begin();i!=controls.end();++i) {
            auto& stream=i.value();std::stable_sort(stream.begin(),stream.end(),[](const auto& a,const auto& b){return a.tick<b.tick;});
            const auto key=i.key().split(':');int down=-1;bool safe=true,unsupported=false;
            for(const auto& c:stream) {
                  if(c.controller!=64) {
                        // Hold semantics and reset/all-notes controls cannot be inferred safely.
                        if(c.controller!=121 || c.tick>0)unsupported=true;
                        if(down>=0)safe=false;
                        continue;
                        }
                  if(c.value>=64) {
                        if(down<0) {down=c.tick;safe=!unsupported;}
                        if(c.value!=127)safe=false;
                        }
                  else if(down>=0) {
                        safe=safe && c.value==0;
                        data.pedals.append({key[0].toInt(),key[1].toInt(),down,c.tick,safe});down=-1;
                        }
                  else if(c.value!=0)unsupported=true;
                  }
            // No fabricated pedal-up at EOF.
            if(down>=0)data.pedals.append({key[0].toInt(),key[1].toInt(),down,lastTick,false});
            }
      return data;
      }
void applyMidiGate(MidiFile& file,const QVector<MidiGateNote>& notes) {
      QMap<int,QMap<int,int>> moves;
      for(const auto& n:notes)if(n.newOff<n.off && n.newOff>n.on && n.offOrdinal>=0)moves[n.track][n.offOrdinal]=n.newOff;
      for(auto t=moves.cbegin();t!=moves.cend();++t) {
            if(t.key()<0 || t.key()>=file.tracks().size())continue;
            auto& events=file.tracks()[t.key()].events();std::multimap<int,MidiEvent> changed;int ordinal=0;
            for(const auto& pair:events) {changed.emplace(t.value().value(ordinal,pair.first),pair.second);++ordinal;}
            events.swap(changed);
            }
      }
}
