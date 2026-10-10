// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only, GUI-thread paging. All returned data are numbers/text, never native pointers.
#include "scoreobserver.h"
#include "readonlyanalysisjob.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/select.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/tie.h"
#include "libmscore/tempo.h"
#include "libmscore/harmony.h"
#include "libmscore/symbol.h"
#include "libmscore/textbase.h"
#include "libmscore/spanner.h"
#include "libmscore/spannermap.h"
#include <QElapsedTimer>
#include <QSet>
#include <algorithm>

namespace Ms { namespace PluginAPI {
int ScoreObserver::startReadOnlyJob(const QString& source,const QVariantMap& input) {
      cancelReadOnlyJob();
      const int token=++_readOnlyJobToken;
      auto job=new ReadOnlyAnalysisJob(this);_readOnlyJob=job;
      connect(job,&ReadOnlyAnalysisJob::finished,this,[this,job,token](const QVariantMap& result) {
            if(_readOnlyJob!=job)return;
            _readOnlyJob=nullptr;emit readOnlyJobFinished(token,result);job->deleteLater();
            });
      job->start(source,input);return token;
      }
void ScoreObserver::cancelReadOnlyJob() {
      if(!_readOnlyJob)return;
      auto job=static_cast<ReadOnlyAnalysisJob*>(_readOnlyJob);_readOnlyJob=nullptr;
      job->cancel();job->deleteLater();
      }
QString ScoreObserver::analysisRevision() {
      if(!_score)return QString();
      const auto state=_score->masterScore()->state();
      if(state!=_analysisState) {_analysisState=state;++_analysisRevision;}
      return QString("%1:%2").arg(_analysisSession).arg(_analysisRevision);
      }
QVariantMap ScoreObserver::analysisScope(bool whole) const {
      if(!_score || !_score->firstMeasure())return {};
      const auto& selection=_score->selection();
      int from=qMax(0,tick()),end=from+1,staff=0;bool noncontiguous=false;
      if(selection.isRange()) {from=selection.tickStart().ticks();end=selection.tickEnd().ticks();staff=selection.staffStart();}
      else if(!selection.elements().isEmpty()) {
            from=INT_MAX;end=0;int count=0;
            for(const auto e:selection.elements())if(e->isNote() || e->isChordRest()) {
                  const auto cr=e->isNote()?toNote(e)->chord():toChordRest(e);
                  from=qMin(from,cr->tick().ticks());end=qMax(end,cr->endTick().ticks());staff=cr->staffIdx();++count;
                  }
            noncontiguous=count>1;
            if(from==INT_MAX) {from=qMax(0,tick());end=from+1;}
            }
      const auto measure=_score->tick2measure(Fraction::fromTicks(from));
      const auto last=_score->tick2measure(Fraction::fromTicks(qMax(from,end-1)));
      from=measure?measure->tick().ticks():0;end=last?last->endTick().ticks():_score->lastMeasure()->endTick().ticks();
      if(whole) {from=0;end=_score->lastMeasure()->endTick().ticks();}
      staff=qBound(0,staff,_score->nstaves()-1);const auto part=_score->staff(staff)->part();
      return {{"start",from},{"end",end},{"firstTrack",part->startTrack()},{"endTrack",part->endTrack()},
            {"part",part->instrumentName()},{"piano",part->instrumentId().contains("piano",Qt::CaseInsensitive)},
            {"noncontiguous",noncontiguous},{"firstMeasure",whole?_score->firstMeasure()->no()+1:measure?measure->no()+1:1},{"lastMeasure",whole?_score->lastMeasure()->no()+1:last?last->no()+1:1}};
      }
QVariantMap ScoreObserver::analysisRange(int start,int end,int firstTrack,int endTrack,int fromTick,int limit,const QString& expected) {
      const QString revision=analysisRevision();
      if(!_score || revision.isEmpty())return {{"error","no-score"}};
      if(!expected.isEmpty() && expected!=revision)return {{"error","stale"},{"revision",revision}};
      if(playing())return {{"error","playing"},{"revision",revision}};
      firstTrack=qBound(0,firstTrack,_score->ntracks());endTrack=qBound(firstTrack,endTrack,_score->ntracks());
      const int scoreEnd=_score->lastMeasure()?_score->lastMeasure()->endTick().ticks():0;
      start=qBound(0,start,scoreEnd);end=qBound(start,end,scoreEnd);fromTick=qMax(start,fromTick);limit=qBound(1,limit,64);
      auto measure=_score->tick2measure(Fraction::fromTicks(fromTick));
      auto segment=measure?measure->first(SegmentType::ChordRest):nullptr;
      while(segment && segment->tick().ticks()<fromTick)segment=segment->next1(SegmentType::ChordRest);
      QVariantList notes,rests,meters,harmonies,hands,pedals,parts,tempos;
      QElapsedTimer timer;timer.start();int count=0,previousMeasure=-1;
      auto handData=[&](const ElementList& list,int tick,int track,int index) {
            for(const auto e:list)if(e->isSymbol()) {
                  const auto symbol=toSymbol(e);if(!symbol->handEligible())continue;
                  hands.append(QVariantMap{{"tick",tick},{"track",track},{"index",index},{"left",symbol->handLeft()},{"end",symbol->handEnd()},{"bracket",symbol->handBracket()}});
                  }
            };
      while(segment && segment->tick().ticks()<end) {
            const int at=segment->tick().ticks();const auto m=segment->measure();
            if(previousMeasure!=m->no()) {
                  meters.append(QVariantMap{{"tick",m->tick().ticks()},{"end",m->endTick().ticks()},{"measure",m->no()+1},{"numerator",m->timesig().numerator()},{"denominator",m->timesig().denominator()}});previousMeasure=m->no();
                  }
            for(int track=firstTrack;track<endTrack;++track) {
                  const auto e=segment->element(track);if(!e || !e->isChordRest())continue;
                  const auto cr=toChordRest(e);
                  if(e->isRest()) {rests.append(QVariantMap{{"tick",at},{"end",cr->endTick().ticks()},{"track",track}});continue;}
                  const auto chord=toChord(e);int index=0;
                  handData(chord->el(),at,track,-1);
                  for(const auto note:chord->notes()) {
                        auto row=describe(note,note->ppitch());const auto attack=row.value("attackTick").toInt();auto origin=note;QSet<const Ms::Note*> before;
                        while(origin->tieBack() && origin->tieBack()->startNote() && !before.contains(origin)) {before.insert(origin);origin=origin->tieBack()->startNote();}
                        const auto& originNotes=origin->chord()->notes();const int originIndex=int(std::find(originNotes.begin(),originNotes.end(),origin)-originNotes.begin());
                        row.insert("attackIndex",originIndex);row.insert("attackTrack",origin->track());row.insert("logicalId",QString("%1:%2:%3").arg(attack).arg(origin->track()).arg(originIndex));
                        auto tail=note;QSet<const Ms::Note*> visited;
                        while(tail->tieFor() && tail->tieFor()->endNote() && !visited.contains(tail)) {visited.insert(tail);tail=tail->tieFor()->endNote();}
                        const int logicalEnd=tail->chord()->endTick().ticks();
                        row.insert("id",QString("%1:%2:%3").arg(at).arg(track).arg(index));row.insert("end",cr->endTick().ticks());row.insert("logicalEnd",logicalEnd);
                        row.insert("tieBack",note->tieBack()!=nullptr);row.insert("tieForward",note->tieFor()!=nullptr);
                        row.insert("duration",cr->actualTicks().ticks());row.insert("performedTicks",note->playTicks());row.insert("onSeconds",_score->tempomap()->tick2time(attack));row.insert("endSeconds",_score->tempomap()->tick2time(logicalEnd));
                        row.insert("measure",m->no()+1);row.insert("beat",1.+double(at-m->tick().ticks())/(DIVISION*4./m->timesig().denominator()));
                        row.insert("grace",chord->isGrace());row.insert("rolled",chord->arpeggio()!=nullptr);
                        row.insert("key",int(_score->staff(track/VOICES)->key(Fraction::fromTicks(at))));
                        QVariantList fingering;
                        for(const auto annotation:note->el())if(annotation->isFingering())fingering.append(toTextBase(annotation)->plainText());
                        row.insert("fingerings",fingering);handData(note->el(),at,track,index++);notes.append(row);
                        }
                  }
            for(const auto e:segment->annotations())if(e->isHarmony() && e->track()>=firstTrack && e->track()<endTrack) {
                  const auto h=toHarmony(e);harmonies.append(QVariantMap{{"tick",at},{"track",e->track()},{"name",h->harmonyName()},{"rootTpc",h->rootTpc()},{"bassTpc",h->baseTpc()}});
                  }
            segment=segment->next1(SegmentType::ChordRest);++count;
            if(count>=limit || timer.nsecsElapsed()>=2000000)break;
            }
      // Small, range-filtered metadata only on the first page.
      if(fromTick==start) {
            for(const auto part:_score->parts())if(part->endTrack()>firstTrack && part->startTrack()<endTrack)parts.append(QVariantMap{{"firstTrack",part->startTrack()},{"endTrack",part->endTrack()},{"name",part->instrumentName()},{"instrumentId",part->instrumentId()}});
            for(const auto& entry:_score->spannerMap().map()) {
                  const auto s=entry.second;if(!s->isPedal() || s->tick2().ticks()<=start || s->tick().ticks()>=end)continue;
                  const auto part=_score->staff(s->staffIdx())->part();if(part->endTrack()<=firstTrack || part->startTrack()>=endTrack)continue;
                  pedals.append(QVariantMap{{"tick",s->tick().ticks()},{"end",s->tick2().ticks()},{"firstTrack",part->startTrack()},{"endTrack",part->endTrack()}});
                  }
            for(const auto& entry:*_score->tempomap())if(entry.first>=start && entry.first<end)tempos.append(QVariantMap{{"tick",entry.first},{"seconds",_score->tempomap()->tick2time(entry.first)}});
            }
      return {{"revision",revision},{"notes",notes},{"rests",rests},{"meters",meters},{"harmonies",harmonies},{"hands",hands},{"pedals",pedals},{"parts",parts},{"tempos",tempos},
            {"nextTick",segment?segment->tick().ticks():end},{"done",!segment || segment->tick().ticks()>=end},{"milliseconds",double(timer.nsecsElapsed())/1000000.},{"coverage",QString("%1–%2; tracks %3–%4").arg(start).arg(end).arg(firstTrack).arg(endTrack)}};
      }
}}
