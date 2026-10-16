// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "markvelocity.h"
#include "libmscore/score.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/dynamic.h"
#include "libmscore/part.h"
#include <algorithm>
namespace Ms {
MarkVelocityText readMarkVelocityText(Score* score){
      MarkVelocityText marks(score->nstaves());
      for(auto segment=score->firstMeasure()?score->firstMeasure()->first():nullptr;segment;segment=segment->next1()){
            QList<Dynamic*> dynamics;for(auto element:segment->annotations())if(element->isDynamic()&&toDynamic(element)->velocity()>0)dynamics.append(toDynamic(element));
            std::stable_sort(dynamics.begin(),dynamics.end(),[](const Dynamic* a,const Dynamic* b){return a->staffIdx()<b->staffIdx();});
            for(auto dynamic:dynamics){QString name=dynamic->dynamicType()==Dynamic::Type::OTHER?dynamic->plainText():dynamic->dynamicTypeName();
                  int first=dynamic->staffIdx(),last=first+1;
                  if(dynamic->dynRange()==Dynamic::Range::PART){first=dynamic->part()->startTrack()/VOICES;last=dynamic->part()->endTrack()/VOICES;}
                  else if(dynamic->dynRange()==Dynamic::Range::SYSTEM){first=0;last=score->nstaves();}
                  for(int staff=qMax(0,first);staff<qMin(last,marks.size());++staff)marks[staff].insert(segment->tick().ticks(),name);
                  }
            }
      return marks;
}
QString markVelocityAt(const MarkVelocityText& marks,int track,int tick){
      const int staff=track/VOICES;if(staff<0||staff>=marks.size())return {};
      const auto& names=marks[staff];auto next=names.upperBound(tick);return next==names.cbegin()?QString():(--next).value();
}
}
