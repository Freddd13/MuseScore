// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "pianohandanalysis.h"
#include "libmscore/pianofingering.h"
#include "libmscore/pianosubstitution.h"
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
      if(tick<=state.lastTick){state.assigned.clear();state.released.clear();state.substitutions.clear();state.scheduledReleases.clear();for(int h=0;h<2;++h){state.previous[h]={};state.previousNotes[h].clear();}}
      state.lastTick=tick;PianoHands::Limits limits[2];
      for(int h=0;h<2;++h) {
            auto settings=options.value(h==0?"left":"right").toMap();
            limits[h].comfort=qBound(1,settings.value("comfort",14).toInt(),36);
            limits[h].maximum=qBound(1,settings.value("maximum",15).toInt(),36);
            limits[h].white=qBound(1,settings.value("white",16).toInt(),36);
            }
      QVector<PianoHands::Key> keys;QVector<QVariantMap> rows;QVector<int> original;
      double seconds=options.value("seconds",tick/960.).toDouble();bool inferred=false;
      if(!state.substitutions.isEmpty()||!state.scheduledReleases.isEmpty()) {
            QSet<QString> activeIds;
            for(const auto& value:notes){const auto n=value.toMap();if(activeKey(n,tick))activeIds.insert(identityOf(n));}
            for(auto it=state.substitutions.begin();it!=state.substitutions.end();)if(!activeIds.contains(it.key()))it=state.substitutions.erase(it);else ++it;
            for(auto it=state.scheduledReleases.begin();it!=state.scheduledReleases.end();)if(!activeIds.contains(it.key()))it=state.scheduledReleases.erase(it);else ++it;
            }
      const double minimum=qBound(0.,options.value("minimumHoldMs",120.).toDouble(),5000.)/1000.;
      QHash<QString,int> identities;
      QVariantList fingeringChanges,blockedSubstitutions;
      auto sequenceFor=[](const QVariantMap& n){QVector<int> sequence;for(const auto& value:n.value("fingerings").toList()){
            const auto next=PianoFingering::substitution(value.toString());if(next.isEmpty())continue;
            if(!sequence.isEmpty()&&sequence!=next)return QVector<int>{-1};sequence=next;
            }return sequence;};
      auto signature=[](const QVariantMap& n){QString value=QString::number(n.value("fingeringAt",keyStart(n)).toInt());for(const auto& text:n.value("fingerings").toList())value+=':'+text.toString().simplified();return value;};
      for(const auto& value:notes){const auto n=value.toMap();const auto id=identityOf(n);if(!activeKey(n,tick)||state.released.contains(id))continue;
            const auto sequence=sequenceFor(n);if(sequence.size()<2&&state.substitutions.isEmpty())continue;
            const auto sig=signature(n);auto found=state.substitutions.find(id);
            if(found!=state.substitutions.end()&&found->signature!=sig){if(found->stage(seconds)+1<found->fingers.size())blockedSubstitutions.append(QVariantMap{{"note",found->note},{"from",found->fingers[found->stage(seconds)]},{"to",found->fingers.back()},{"replaced",true}});state.substitutions.erase(found);}
            if(sequence.size()>1&&!state.substitutions.contains(id)){Substitution s;s.signature=sig;s.fingers=sequence;s.note=n;s.epoch=n.value("fingeringAt",keyStart(n)).toInt();state.substitutions.insert(id,s);}
            }
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
            const auto sequence=sequenceFor(n);const auto sub=state.substitutions.constFind(identityOf(n));
            const bool matching=sub!=state.substitutions.cend()&&sub->signature==signature(n);
            if(sequence.size()==1&&sequence.front()<0)key.finger=-1;
            for(const auto& value:n.value("fingerings").toList()) {
                  const auto steps=PianoFingering::substitution(value.toString());
                  const auto mask=steps.isEmpty()?PianoFingering::choices(value.toString()):1<<(steps[matching?sub->stage(atSeconds):0]-1);if(!mask)continue;
                  if(key.finger<0)continue;
                  if(!key.fingerMask)key.fingerMask=mask;
                  else if(key.fingerMask&mask)key.fingerMask&=mask;
                  else key.finger=-1;
                  }
            if(sequence.isEmpty()&&key.finger>=0&&key.fingerMask)for(const auto& owner:state.substitutions) {
                  if(owner.note.value("pitch").toInt()!=key.pitch||keyStart(owner.note)!=keyStart(n)||n.value("fingeringAt",keyStart(n)).toInt()>owner.epoch)continue;
                  const int stage=owner.stage(atSeconds);
                  if(stage>0&&(key.fingerMask&(1<<(owner.fingers.front()-1))))key.fingerMask=1<<(owner.fingers[stage]-1);
                  }
            for(const auto& value:pedals) {
                  const auto p=value.toMap();const int track=n.value("track").toInt();
                  const bool matches=p.contains("channelKey") ? p.value("channelKey")==n.value("playbackChannel")
                        : track>=p.value("firstTrack").toInt() && track<p.value("endTrack").toInt();
                  if(!matches || p.value("tick").toInt()>=keyEnd(n) || keyEnd(n)>p.value("end").toInt() || !p.value("supported",true).toBool())continue;
                  const bool required=n.contains("requiredUntil")&&at<=n.value("requiredUntil").toInt();
                  if(!key.attack && !required && atSeconds-start>=minimum && p.value("tick").toInt()<at && at<p.value("end").toInt())key.releasable=true;
                  if(hasStart && hasEnd && p.contains("seconds") && (!n.contains("requiredUntil")||n.contains("requiredSeconds"))) {
                        double earliest=std::max(start+minimum,std::nextafter(p.value("seconds").toDouble(),std::numeric_limits<double>::infinity()));
                        if(n.contains("requiredSeconds"))earliest=std::max(earliest,std::nextafter(n.value("requiredSeconds").toDouble(),std::numeric_limits<double>::infinity()));
                        if(earliest<end && (!std::isfinite(key.releaseSeconds) || earliest<key.releaseSeconds))key.releaseSeconds=earliest;
                        }
                  }
            if(matching) {
                  const double completed=sub->completion();
                  if(!sub->scheduled()){key.releasable=false;key.releaseSeconds=std::numeric_limits<double>::quiet_NaN();}
                  else {if(atSeconds<completed)key.releasable=false;if(std::isfinite(key.releaseSeconds))key.releaseSeconds=std::max(key.releaseSeconds,completed);}
                  }
            return key;
            };
      for(int i=0;i<notes.size();++i) {
            auto n=notes[i].toMap();const auto id=identityOf(n);
            if(!activeKey(n,tick) || state.released.contains(id) || state.scheduledReleases.contains(id)&&state.scheduledReleases[id]<=seconds)continue;
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
            else if(key.fingerMask) {
                  if(pitchFingers.contains(key.pitch) && !(pitchFingers[key.pitch]&key.fingerMask))fingerConflicts.insert(key.pitch);
                  else pitchFingers[key.pitch]=pitchFingers.value(key.pitch,31)&key.fingerMask;
                  }
            }
      QVariantList uncertainHands,uncertainFingerings;
      QSet<QString> uncertainIds;
      for(int i=keys.size()-1;i>=0;--i) {
            if(fingerConflicts.contains(keys[i].pitch)){uncertainFingerings.prepend(original[i]);keys[i].finger=0;keys[i].fingerMask=0;}
            if(handConflicts.contains(keys[i].pitch)) {
                  uncertainHands.prepend(original[i]);uncertainIds.insert(identityOf(rows[i]));
                  keys.removeAt(i);rows.removeAt(i);original.removeAt(i);
                  }
            }
      // Trial one substitution at a time. Releases and held keys keep their
      // actual identities; only scalar plans are retained in this worker.
      struct Change {bool valid=false;double seconds=0;QVariantList released,after;};
      auto attempt=[&](int index,int target,int hand,double lower,double upper,bool before,const PianoHands::Plan& plan){
            QVector<double> changes;
            for(const auto& sub:state.substitutions)changes+=sub.changes;
            QSet<int> followers;const auto& owner=state.substitutions[identityOf(rows[index])];
            for(int j=0;j<keys.size();++j)if(j!=index&&keys[j].pitch==keys[index].pitch&&keyStart(rows[j])==keyStart(rows[index])&&rows[j].value("fingeringAt",keyStart(rows[j])).toInt()<=owner.epoch){
                  const auto sequence=sequenceFor(rows[j]);if(sequence==owner.fingers||sequence.isEmpty()&&(keys[j].fingerMask&(1<<(owner.fingers.front()-1))))followers.insert(j);
                  }
            PianoSubstitution::Frame frame{keys,plan,limits,changes,followers,
                  [&](int j,double when){return keyFor(rows[j],tick,when);},
                  [&](int j,double when){const auto id=identityOf(rows[j]);return state.scheduledReleases.contains(id)&&state.scheduledReleases[id]<=when;}};
            const auto candidate=PianoSubstitution::choose(frame,index,target,hand,lower,upper,before);
            Change result;result.valid=candidate.valid;result.seconds=candidate.seconds;
            for(int i:candidate.released)result.released.append(rows[i]);
            for(int i:candidate.after)result.after.append(rows[i]);
            return result;
            };
      auto accept=[&](int index,int hand,const Change& change,const QString& phase){auto& sub=state.substitutions[identityOf(rows[index])];const int from=sub.fingers[sub.changes.size()],to=sub.fingers[sub.changes.size()+1];sub.changes.append(change.seconds);
            for(auto& other:state.substitutions)if(&other!=&sub&&other.note.value("pitch")==sub.note.value("pitch")&&keyStart(other.note)==keyStart(sub.note)&&other.epoch==sub.epoch&&other.fingers==sub.fingers)other.changes=sub.changes;
            for(const auto& value:change.released){const auto id=identityOf(value.toMap());if(!state.scheduledReleases.contains(id)||change.seconds<state.scheduledReleases[id])state.scheduledReleases[id]=change.seconds;}
            fingeringChanges.append(QVariantMap{{"note",rows[index]},{"hand",hand==0?"left":"right"},{"from",from},{"to",to},{"seconds",change.seconds},{"phase",phase},{"released",change.released},{"after",change.after}});};
      // Held notes can change finger before the new attack. A fresh fingering
      // still uses its first finger for this attack.
      if(!state.substitutions.isEmpty()&&uncertainHands.isEmpty())for(int pass=0;pass<5;++pass){bool changed=false;const auto plan=PianoHands::plan(keys,limits,state.previous,seconds);
            for(int i=0;i<keys.size();++i){const auto id=identityOf(rows[i]);auto sub=state.substitutions.constFind(id);if(sub==state.substitutions.cend()||sub->scheduled()||sub->epoch>=tick||!sub->changes.isEmpty()&&sub->changes.back()>seconds)continue;
                  if(plan.limited||plan.hands[i]<0)continue;
                  if(plan.after[plan.hands[i]].fits)continue;
                  const auto change=attempt(i,sub->fingers[sub->changes.size()+1],plan.hands[i],std::nextafter(seconds,-INFINITY),seconds,true,plan);if(!change.valid)continue;
                  QVector<PianoHands::Key> trial;for(int j=0;j<keys.size();++j){bool removed=false;for(const auto& r:change.released)removed|=identityOf(r.toMap())==identityOf(rows[j]);if(removed)continue;auto key=keys[j];if(j==i){key.finger=0;key.fingerMask=1<<(sub->fingers[sub->changes.size()+1]-1);key.hand=plan.hands[i];}trial.append(key);}
                  const auto final=PianoHands::plan(trial,limits,state.previous,seconds);if(!final.after[plan.hands[i]].fits)continue;
                  accept(i,plan.hands[i],change,"before");state.assigned[id]=plan.hands[i];changed=true;}
            if(!changed)break;
            for(int i=keys.size()-1;i>=0;--i){const auto id=identityOf(rows[i]);if(state.scheduledReleases.contains(id)&&state.scheduledReleases[id]<=seconds){keys.removeAt(i);rows.removeAt(i);original.removeAt(i);}else keys[i]=keyFor(rows[i],tick,seconds);}
            }
      QVector<PianoHands::Future> future;
      PianoHands::Plan fingeringPreview;
      if(!state.substitutions.isEmpty())fingeringPreview=PianoHands::plan(keys,limits,state.previous,seconds);
      for(const auto& value:options.value("lookahead").toList()) {
            if(future.size()>=2)break;
            const auto frame=value.toMap();const int at=frame.value("tick").toInt();const double atSeconds=frame.value("seconds",at/960.).toDouble();
            if(at<=tick || atSeconds<seconds)continue;
            PianoHands::Future next;next.seconds=atSeconds;
            for(const auto& note:frame.value("notes").toList()) {
                  auto n=note.toMap();const auto id=identityOf(n);if(!activeKey(n,at) || state.released.contains(id) || uncertainIds.contains(id)||state.scheduledReleases.contains(id)&&state.scheduledReleases[id]<=atSeconds)continue;
                  auto key=keyFor(n,at,atSeconds);
                  const auto sub=state.substitutions.constFind(id);
                  if(!key.attack&&sub!=state.substitutions.cend()&&!sub->scheduled()&&sub->signature==signature(n)&&!fingeringPreview.limited){
                        for(int i=0;i<keys.size();++i)if(keys[i].id==key.id&&fingeringPreview.hands[i]>=0&&fingeringPreview.after[fingeringPreview.hands[i]].fits){
                              const double lower=std::nextafter(sub->changes.isEmpty()?seconds:std::max(seconds,sub->changes.back()),INFINITY);
                              const double upper=std::min({atSeconds,options.value("nextChangeSeconds",INFINITY).toDouble(),options.value("endSeconds",INFINITY).toDouble(),keys[i].endSeconds});
                              const int target=sub->fingers[sub->changes.size()+1];
                              // A future hand choice can use either the present
                              // finger or a reachable next finger. The actual
                              // substitution is scheduled and checked below.
                              if(attempt(i,target,fingeringPreview.hands[i],lower,upper,false,fingeringPreview).valid)key.fingerMask|=1<<(target-1);
                              break;
                              }
                        }
                  next.keys.append(key);
                  }
            future.append(next);
            }
      const auto result=PianoHands::planWithContext(keys,limits,state.previous,seconds,future);QVariantList sides,assignments;bool changed[2]={false,false};
      double boundary=options.value("nextChangeSeconds",INFINITY).toDouble();boundary=std::min(boundary,options.value("endSeconds",INFINITY).toDouble());
      for(const auto& frame:options.value("lookahead").toList()){const double at=frame.toMap().value("seconds",INFINITY).toDouble();if(at>seconds)boundary=std::min(boundary,at);}
      auto compatibleFuture=[&](int index,const Change& change){
            const auto id=identityOf(rows[index]);auto& sub=state.substitutions[id];const auto saved=sub.changes;bool valid=true;
            for(const auto& value:options.value("lookahead").toList()){
                  const auto frame=value.toMap();const int at=frame.value("tick").toInt();const double when=frame.value("seconds",at/960.).toDouble();if(when<=change.seconds)continue;
                  auto futureKeys=[&](bool apply){sub.changes=saved;if(apply)sub.changes.append(change.seconds);QVector<PianoHands::Key> out;
                        for(const auto& n:frame.value("notes").toList()){auto note=n.toMap();const auto identity=identityOf(note);if(!activeKey(note,at)||state.released.contains(identity)||state.scheduledReleases.contains(identity)&&state.scheduledReleases[identity]<=when)continue;
                              bool removed=false;if(apply)for(const auto& r:change.released)removed|=identityOf(r.toMap())==identity;if(removed)continue;
                              auto key=keyFor(note,at,when);for(int j=0;j<keys.size();++j)if(keys[j].id==key.id&&!key.attack)key.hand=result.hands[j];out.append(key);
                              }return out;};
                  PianoHands::Position empty[2];const auto before=PianoHands::plan(futureKeys(false),limits,empty,when),after=PianoHands::plan(futureKeys(true),limits,empty,when);const int hand=result.hands[index];
                  if(after.after[hand].fingerConflicts>before.after[hand].fingerConflicts||!after.after[hand].fits&&before.after[hand].fits){valid=false;break;}
                  }
            sub.changes=saved;return valid;};
      if(!state.substitutions.isEmpty()&&uncertainHands.isEmpty())for(int pass=0;pass<5;++pass){bool scheduled=false;
            for(int i=0;i<keys.size();++i){const auto id=identityOf(rows[i]);const auto sub=state.substitutions.constFind(id);if(sub==state.substitutions.cend()||sub->scheduled()||result.limited||result.hands[i]<0||result.released.contains(i)||!result.after[result.hands[i]].fits)continue;
                  const double lower=std::nextafter(sub->changes.isEmpty()?seconds:std::max(seconds,sub->changes.back()),INFINITY);
                  const auto change=attempt(i,sub->fingers[sub->changes.size()+1],result.hands[i],lower,std::min(boundary,keys[i].endSeconds),false,result);
                  if(change.valid&&compatibleFuture(i,change)){accept(i,result.hands[i],change,"after");scheduled=true;}
                  }if(!scheduled)break;
            }
      QVariantMap effectiveFingers;bool usesPlannedRelease=false,usesPlannedFingering=false,substitutionUncertain=false;
      if(!state.scheduledReleases.isEmpty())for(const auto& value:notes){const auto n=value.toMap();const auto id=identityOf(n);usesPlannedRelease|=activeKey(n,tick)&&state.scheduledReleases.contains(id)&&state.scheduledReleases[id]<=seconds;}
      for(int i=0;i<keys.size();++i){const auto refreshed=keyFor(rows[i],tick,seconds);keys[i].releaseSeconds=refreshed.releaseSeconds;
            if(std::isfinite(keys[i].releaseSeconds))rows[i].insert("analysisReleaseSeconds",keys[i].releaseSeconds);
            if(keys[i].fingerMask&&!(keys[i].fingerMask&(keys[i].fingerMask-1))){int f=1;while(!(keys[i].fingerMask&(1<<(f-1))))++f;effectiveFingers.insert(QString::number(original[i]),f);}
            const auto sub=state.substitutions.constFind(identityOf(rows[i]));usesPlannedFingering|=sub!=state.substitutions.cend()&&sub->stage(seconds)>0;
            substitutionUncertain|=!uncertainHands.isEmpty()&&!keys[i].attack&&sub!=state.substitutions.cend()&&!sub->scheduled();
            if(!result.limited&&uncertainHands.isEmpty()&&sub!=state.substitutions.cend()&&!sub->scheduled()&&std::isfinite(keys[i].endSeconds)&&keys[i].endSeconds<=boundary){
                  const int target=sub->fingers[sub->changes.size()+1];QVariantList blockers;for(int j=0;j<keys.size();++j)if(j!=i&&result.hands[j]==result.hands[i]&&keys[j].pitch!=keys[i].pitch&&keys[j].fingerMask==(1<<(target-1)))blockers.append(rows[j]);
                  blockedSubstitutions.append(QVariantMap{{"note",rows[i]},{"from",sub->fingers[sub->changes.size()]},{"to",target},{"blockers",blockers}});
                  }
            }
      for(int h=0;h<2;++h)for(auto& value:state.previousNotes[h])for(int i=0;i<rows.size();++i)if(result.hands[i]==h && identityOf(value.toMap())==identityOf(rows[i])) {
            value=rows[i];
            for(auto& hold:state.previous[h].holds)if(hold.pitch==keys[i].pitch && hold.until==keys[i].endSeconds && std::isfinite(keys[i].releaseSeconds))hold.release=keys[i].releaseSeconds;
            break;
            }
      for(int i=0;i<keys.size();++i)if(!keys[i].attack) {
            const int before=state.assigned.value(identityOf(rows[i]),-1),after=result.hands[i];
            if(before!=after){if(before>=0)changed[before]=true;if(after>=0)changed[after]=true;}
            }
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
            if(!attacks.isEmpty() || changed[h]) {
                  state.previous[h]=PianoHands::positionFor(keys,result,h,seconds);state.previousNotes[h].clear();
                  if(state.previous[h].valid)for(int i=0;i<keys.size();++i)if(result.hands[i]==h && !result.released.contains(i)) {
                        auto row=rows[i];row.insert("analysisHand",h==0?"left":"right");state.previousNotes[h].append(row);
                        }
                  }
            }
      return {{"sides",sides},{"assignments",assignments},{"possible",result.possible && uncertainHands.isEmpty() && uncertainFingerings.isEmpty()},{"inferred",inferred},{"uncertain",result.uncertain || !uncertainHands.isEmpty() || !uncertainFingerings.isEmpty()},{"uncertainHands",uncertainHands},{"uncertainFingerings",uncertainFingerings},{"limited",result.limited},{"effectiveFingers",effectiveFingers},{"fingeringChanges",fingeringChanges},{"blockedSubstitutions",blockedSubstitutions},{"usesPlannedRelease",usesPlannedRelease},{"usesPlannedFingering",usesPlannedFingering},{"substitutionUncertain",substitutionUncertain}};
      }
} }
