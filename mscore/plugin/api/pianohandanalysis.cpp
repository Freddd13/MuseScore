// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "pianohandanalysis.h"
#include <algorithm>
namespace Ms { namespace PluginAPI {
namespace {
int side(const QString& name){return name=="left"?0:name=="right"?1:-1;}
QVariantMap hand(const PianoHands::Hand& value){return {{"low",value.low},{"high",value.high},{"count",value.count},{"maximum",value.maximum},{"fingerConflicts",value.fingerConflicts},{"crowdedBlackKeys",value.crowdedBlackKeys},{"fits",value.fits}};}
}
QVariantMap PianoHandAnalysis::analyse(const QVariantList& notes,const QVariantList& pedals,const QVariantMap& options) {
      const int tick=options.value("tick").toInt();auto& state=_parts[options.value("part",0).toInt()];
      if(tick<=state.lastTick){state.assigned.clear();state.released.clear();state.previous[0]={};state.previous[1]={};}
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
      for(int i=0;i<notes.size();++i) {
            auto n=notes[i].toMap();const auto id=n.value("id").toString();
            if(n.value("logicalEnd",n.value("end")).toInt()<=tick || n.value("grace").toBool() || state.released.contains(id))continue;
            PianoHands::Key key;key.id=i;key.pitch=n.value("pitch").toInt();key.hint=side(n.value("analysisHand").toString());
            key.attack=n.value("attackTick",n.value("tick")).toInt()==tick;
            const bool known=n.value("handKnown").toBool();inferred|=!known;
            key.hand=known?key.hint:!key.attack?state.assigned.value(id,-1):-1;
            for(const auto& value:n.value("fingerings").toList()) {
                  const auto text=value.toString().trimmed();const int finger=text.toInt();
                  if(text.size()==1 && finger>=1 && finger<=5){if(!key.finger)key.finger=finger;else if(key.finger!=finger)key.finger=-1;}
                  }
            if(!key.attack && seconds-n.value("onSeconds").toDouble()>=minimum)for(const auto& value:pedals) {
                  const auto p=value.toMap();const int track=n.value("track").toInt();
                  if(p.value("tick").toInt()<tick && tick<p.value("end").toInt() && track>=p.value("firstTrack").toInt() && track<p.value("endTrack").toInt()
                        && n.value("logicalEnd",n.value("end")).toInt()<=p.value("end").toInt() && p.value("supported",true).toBool())key.releasable=true;
                  }
            keys.append(key);rows.append(n);original.append(i);
            }
      const auto result=PianoHands::plan(keys,limits,state.previous,seconds);QVariantList sides,assignments;
      for(int h=0;h<2;++h) {
            QVariantList involved,released,attacks;bool rolled=false;
            for(int i=0;i<keys.size();++i)if(result.hands[i]==h) {
                  involved.append(original[i]);if(keys[i].attack)attacks.append(original[i]);rolled|=rows[i].value("rolled").toBool();
                  const auto id=rows[i].value("id").toString();state.assigned[id]=h;
                  if(result.released.contains(i)){released.append(original[i]);state.released.insert(id);}
                  assignments.append(QVariantMap{{"index",original[i]},{"hand",h==0?"left":"right"},{"inferred",!rows[i].value("handKnown").toBool()}});
                  }
            int travel=0;double available=0;
            if(result.after[h].count && state.previous[h].valid) {
                  travel=std::max({0,result.after[h].high-result.after[h].maximum-state.previous[h].low,state.previous[h].high-result.after[h].maximum-result.after[h].low});
                  available=std::max(0.,seconds-state.previous[h].seconds);
                  }
            sides.append(QVariantMap{{"hand",h==0?"left":"right"},{"before",hand(result.before[h])},{"after",hand(result.after[h])},{"notes",involved},{"released",released},{"attacks",attacks},{"travel",travel},{"available",available},{"rolled",rolled}});
            if(!attacks.isEmpty() && result.after[h].count)state.previous[h]={true,result.after[h].low,result.after[h].high,seconds};
            }
      return {{"sides",sides},{"assignments",assignments},{"possible",result.possible},{"inferred",inferred},{"uncertain",result.uncertain},{"limited",result.limited}};
      }
} }
