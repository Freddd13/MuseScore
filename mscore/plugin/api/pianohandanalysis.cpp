// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "pianohandanalysis.h"
#include <algorithm>
namespace Ms { namespace PluginAPI {
namespace {
int side(const QString& name){return name=="left"?0:name=="right"?1:-1;}
QVariantMap hand(const PianoHands::Hand& value){return {{"low",value.low},{"high",value.high},{"count",value.count},{"maximum",value.maximum},{"fingerConflicts",value.fingerConflicts},{"crowdedBlackKeys",value.crowdedBlackKeys},{"fits",value.fits}};}
QString identityOf(const QVariantMap& note) {
      auto id=note.value("analysisId",note.value("id")).toString();
      if(id.isEmpty())id=QString("%1:%2:%3:%4").arg(note.value("track").toInt()).arg(note.value("attackTick",note.value("tick")).toInt()).arg(note.value("index").toInt()).arg(note.value("pitch").toInt());
      return id;
      }
int keyStart(const QVariantMap& note){return note.value("physicalStart",note.value("attackTick",note.value("tick"))).toInt();}
int keyEnd(const QVariantMap& note){return note.value("physicalEnd",note.value("logicalEnd",note.value("end"))).toInt();}
bool activeKey(const QVariantMap& note,int tick){return keyStart(note)<=tick && keyEnd(note)>tick && !note.value("grace").toBool() && note.value("play",true).toBool() && !note.value("muted").toBool();}
}
QVariantMap PianoHandAnalysis::analyse(const QVariantList& notes,const QVariantList& pedals,const QVariantMap& options) {
      const int tick=options.value("tick").toInt();auto& state=_parts[options.value("part",0).toInt()];
      if(tick<=state.lastTick){state.assigned.clear();state.released.clear();for(int h=0;h<2;++h){state.previous[h]={};state.previousNotes[h].clear();}}
      state.lastTick=tick;PianoHands::Limits limits[2];
      for(int h=0;h<2;++h) {
            auto settings=options.value(h==0?"left":"right").toMap();
            limits[h].comfort=qBound(1,settings.value("comfort",14).toInt(),36);
            limits[h].maximum=qBound(1,settings.value("maximum",15).toInt(),36);
            limits[h].white=qBound(1,settings.value("white",16).toInt(),36);
            }
      QVector<PianoHands::Key> keys;QVector<QVariantMap> rows;QVector<int> original;
      double seconds=options.value("seconds",tick/960.).toDouble();bool inferred=false;
      const double minimum=qBound(0.,options.value("minimumHoldMs",120.).toDouble(),5000.)/1000.;
      QHash<QString,int> identities;
      auto identity=[&](const QVariantMap& n) {
            const auto id=identityOf(n);
            if(!identities.contains(id))identities[id]=identities.size();return identities.value(id);
            };
      auto keyFor=[&](const QVariantMap& n,int at,double atSeconds) {
            PianoHands::Key key;key.id=identity(n);key.pitch=n.value("pitch").toInt();key.hint=side(n.value("analysisHand").toString());
            key.attack=keyStart(n)==at;
            key.hand=n.value("handKnown").toBool()?key.hint:!key.attack?state.assigned.value(identityOf(n),-1):-1;
            bool hasEnd=false,hasStart=false;
            const double end=n.value("physicalEndSeconds",n.value("endSeconds")).toDouble(&hasEnd);
            const double start=n.value("physicalOnSeconds",n.value("onSeconds")).toDouble(&hasStart);
            if(hasEnd && std::isfinite(end))key.endSeconds=end;
            for(const auto& value:n.value("fingerings").toList()) {
                  const auto text=value.toString().trimmed();const int finger=text.toInt();
                  if(text.size()==1 && finger>=1 && finger<=5){if(!key.finger)key.finger=finger;else if(key.finger!=finger)key.finger=-1;}
                  }
            for(const auto& value:pedals) {
                  const auto p=value.toMap();const int track=n.value("track").toInt();
                  const bool matches=p.contains("channelKey") ? p.value("channelKey")==n.value("playbackChannel")
                        : track>=p.value("firstTrack").toInt() && track<p.value("endTrack").toInt();
                  if(!matches || p.value("tick").toInt()>=keyEnd(n) || keyEnd(n)>p.value("end").toInt() || !p.value("supported",true).toBool())continue;
                  if(!key.attack && atSeconds-start>=minimum && p.value("tick").toInt()<at && at<p.value("end").toInt())key.releasable=true;
                  if(hasStart && hasEnd && p.contains("seconds")) {
                        const double earliest=std::max(start+minimum,std::nextafter(p.value("seconds").toDouble(),std::numeric_limits<double>::infinity()));
                        if(earliest<end && (!std::isfinite(key.releaseSeconds) || earliest<key.releaseSeconds))key.releaseSeconds=earliest;
                        }
                  }
            return key;
            };
      for(int i=0;i<notes.size();++i) {
            auto n=notes[i].toMap();const auto id=identityOf(n);
            if(!activeKey(n,tick) || state.released.contains(id))continue;
            inferred|=!n.value("handKnown").toBool();const auto key=keyFor(n,tick,seconds);keys.append(key);
            if(std::isfinite(key.releaseSeconds))n.insert("analysisReleaseSeconds",key.releaseSeconds);
            rows.append(n);original.append(i);
            }
      QHash<int,int> pitchHands,pitchFingers;
      QSet<int> handConflicts,fingerConflicts;
      for(const auto& key:keys) {
            if(key.hand>=0) {
                  if(pitchHands.contains(key.pitch) && pitchHands[key.pitch]!=key.hand)handConflicts.insert(key.pitch);
                  else pitchHands[key.pitch]=key.hand;
                  }
            if(key.finger<0)fingerConflicts.insert(key.pitch);
            else if(key.finger>0) {
                  if(pitchFingers.contains(key.pitch) && pitchFingers[key.pitch]!=key.finger)fingerConflicts.insert(key.pitch);
                  else pitchFingers[key.pitch]=key.finger;
                  }
            }
      QVariantList uncertainHands,uncertainFingerings;
      QSet<QString> uncertainIds;
      for(int i=keys.size()-1;i>=0;--i) {
            if(fingerConflicts.contains(keys[i].pitch))uncertainFingerings.prepend(original[i]);
            if(handConflicts.contains(keys[i].pitch)) {
                  uncertainHands.prepend(original[i]);uncertainIds.insert(identityOf(rows[i]));
                  keys.removeAt(i);rows.removeAt(i);original.removeAt(i);
                  }
            }
      QVector<PianoHands::Future> future;
      for(const auto& value:options.value("lookahead").toList()) {
            if(future.size()>=2)break;
            const auto frame=value.toMap();const int at=frame.value("tick").toInt();const double atSeconds=frame.value("seconds",at/960.).toDouble();
            if(at<=tick || atSeconds<seconds)continue;
            PianoHands::Future next;next.seconds=atSeconds;
            for(const auto& note:frame.value("notes").toList()) {
                  auto n=note.toMap();if(!activeKey(n,at) || state.released.contains(identityOf(n)) || uncertainIds.contains(identityOf(n)))continue;
                  next.keys.append(keyFor(n,at,atSeconds));
                  }
            future.append(next);
            }
      const auto result=PianoHands::planWithContext(keys,limits,state.previous,seconds,future);QVariantList sides,assignments;
      for(int h=0;h<2;++h) {
            QVariantList involved,released,attacks;bool rolled=false;
            for(int i=0;i<keys.size();++i)if(result.hands[i]==h) {
                  involved.append(original[i]);if(keys[i].attack)attacks.append(original[i]);rolled|=rows[i].value("rolled").toBool();
                  const auto id=identityOf(rows[i]);state.assigned[id]=h;
                  if(result.released.contains(i)){released.append(original[i]);state.released.insert(id);}
                  assignments.append(QVariantMap{{"index",original[i]},{"hand",h==0?"left":"right"},{"inferred",!rows[i].value("handKnown").toBool()}});
                  }
            const auto motion=PianoHands::movement(result.after[h],state.previous[h],seconds);
            const auto pedalMotion=PianoHands::movement(result.after[h],state.previous[h],seconds,true);
            QVariantList from,pedalRelease;
            for(const auto& value:state.previousNotes[h]) {
                  const auto n=value.toMap();if(n.value("pitch").toInt()==motion.holdPitch)from.append(value);
                  if(n.contains("analysisReleaseSeconds") && PianoHands::distanceTo(result.after[h],n.value("pitch").toInt(),n.value("pitch").toInt())>0)pedalRelease.append(value);
                  }
            sides.append(QVariantMap{{"hand",h==0?"left":"right"},{"before",hand(result.before[h])},{"after",hand(result.after[h])},{"notes",involved},{"released",released},{"attacks",attacks},
                  {"travel",motion.distance},{"available",motion.available},{"travelTotal",motion.total},{"movementSince",motion.since},{"movementFrom",from},
                  {"pedalTravel",pedalMotion.distance},{"pedalAvailable",pedalMotion.available},{"pedalReleaseNotes",pedalRelease},{"rolled",rolled},{"partial",!uncertainHands.isEmpty()}});
            if(!attacks.isEmpty()) {
                  state.previous[h]=PianoHands::positionFor(keys,result,h,seconds);state.previousNotes[h].clear();
                  if(state.previous[h].valid)for(int i=0;i<keys.size();++i)if(result.hands[i]==h && !result.released.contains(i)) {
                        auto row=rows[i];row.insert("analysisHand",h==0?"left":"right");state.previousNotes[h].append(row);
                        }
                  }
            }
      return {{"sides",sides},{"assignments",assignments},{"possible",result.possible && uncertainHands.isEmpty() && uncertainFingerings.isEmpty()},{"inferred",inferred},{"uncertain",result.uncertain || !uncertainHands.isEmpty()},{"uncertainHands",uncertainHands},{"uncertainFingerings",uncertainFingerings},{"limited",result.limited}};
      }
} }
