// SPDX-License-Identifier: GPL-2.0-or-later
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/seq.h"
#include "libmscore/measure.h"
#include "libmscore/part.h"
#include "libmscore/xml.h"
#include "libmscore/spannermap.h"
#include "libmscore/spanner.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QCryptographicHash>
namespace Ms { namespace PluginAPI {
QVariantMap ScoreObserver::workflowFingerprint(int from,int limit,const QString& expected){
 if(!_score||playing()||(seq&&!seq->backgroundRenderingIdle())||expected!=analysisRevision())return {{"error","stale, playing or rendering"}};
 if(from<0||from>_score->nmeasures())return {{"error","invalid measure page"}};
 QElapsedTimer timer;timer.start();
 limit=qBound(1,limit,16);
 struct Index{QPointer<Ms::Score> score;ScoreContentState state;QVector<Measure*> measures;};static Index cache;
 const auto state=_score->masterScore()->state();if(cache.score!=_score||cache.state!=state){cache.score=_score;cache.state=state;cache.measures.clear();for(auto m=_score->firstMeasure();m;m=m->nextMeasure())cache.measures.append(m);}
 auto measure=from<cache.measures.size()?cache.measures[from]:nullptr;
 QVariantList rows;int index=from;
 while(measure&&rows.size()<limit){
  QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
  {XmlWriter xml(_score,&buffer);xml.setWritePosition(false);for(int staff=0;staff<_score->nstaves();++staff)measure->write(xml,staff,staff==0,false);}
  // Explicit spanner serialization catches both start and continuation changes.
  for(auto interval:_score->spannerMap().findOverlapping(measure->tick().ticks(),measure->endTick().ticks()-1)){auto s=interval.value;if(s->tick().ticks()<measure->endTick().ticks()&&s->tick2().ticks()>measure->tick().ticks()){
    QByteArray span;QBuffer b(&span);b.open(QIODevice::WriteOnly);{XmlWriter xml(_score,&b);xml.setWritePosition(false);s->write(xml);}bytes+=span;
   }}
  rows.append(QVariantMap{{"tick",measure->tick().ticks()},{"end",measure->endTick().ticks()},{"signature",measure->timesig().toString()},{"hash",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}});
  ++index;measure=measure->nextMeasure();if(timer.nsecsElapsed()>2000000)break;
 }
 QByteArray context;QBuffer config(&context);config.open(QIODevice::WriteOnly);if(from==0){XmlWriter xml(_score,&config);for(auto part:_score->parts())part->write(xml);_score->style().save(xml,false);}
 return {{"context",from==0?QString::fromLatin1(QCryptographicHash::hash(context,QCryptographicHash::Sha256).toHex()):QString()},{"measures",rows},{"staves",_score->nstaves()},{"next",index},{"done",measure==nullptr},{"milliseconds",double(timer.nsecsElapsed())/1000000}};
}
}}
