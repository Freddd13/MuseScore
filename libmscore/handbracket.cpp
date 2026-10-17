// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "handbracket.h"
#include "symbol.h"
#include "chord.h"
#include "note.h"
#include "stem.h"
#include "hook.h"
#include "accidental.h"
#include "beam.h"
#include "ledgerline.h"
#include "stafflines.h"
#include "skyline.h"
#include <QSet>
#include "segment.h"
#include "measure.h"
#include "system.h"
#include <cmath>
#include <climits>
namespace Ms { namespace HandBracket {
namespace {
int role(SymId id) {
      switch(id) {case SymId::keyboardPlayWithLH:return 0;case SymId::keyboardPlayWithLHEnd:return 1;
      case SymId::keyboardPlayWithRH:return 2;case SymId::keyboardPlayWithRHEnd:return 3;default:return -1;}
      }
Style styles[4]={{.6,-2.5,.12,false},{-.6,-2.5,.12,false},{.6,-2.5,.12,false},{-.6,-2.5,.12,false}};
void chordRects(Chord* chord,const QPointF& origin,const QRectF& window,QVector<QRectF>* result) {
      auto add=[&](QRectF r){if(r.right()>=window.left()&&r.left()<=window.right())result->append(r);};
      for(auto note:chord->notes()) {
            add(note->bbox().translated(origin+note->pos()));
            if(note->accidental())add(note->accidental()->bbox().translated(origin+note->pos()+note->accidental()->pos()));
            }
      if(chord->stem())add(chord->stem()->bbox().translated(origin+chord->stem()->pos()));
      if(chord->hook())add(chord->hook()->bbox().translated(origin+chord->hook()->pos()));
      const double staffOffset=chord->measure()->staffLines(chord->vStaffIdx())->y();
      for(auto line=chord->ledgerLines();line;line=line->next())add(line->bbox().translated(origin+QPointF(line->x(),line->y()-chord->y()-staffOffset)));
      }
QVector<QRectF> inkRects(const Symbol* symbol) {
      const double sp=symbol->spatium()*symbol->mag();
      const double h=symbol->getProperty(Pid::HAND_BRACKET_H).toDouble()*sp,v=symbol->getProperty(Pid::HAND_BRACKET_V).toDouble()*sp;
      const double pad=symbol->getProperty(Pid::HAND_BRACKET_WIDTH).toDouble()*sp/2,y=symbol->handLeft()?v:0.;
      QVector<QRectF> result{QRectF(-pad,qMin(0.,v)-pad,2*pad,std::abs(v)+2*pad),QRectF(qMin(0.,h)-pad,y-pad,std::abs(h)+2*pad,2*pad)};
      if(symbol->getProperty(Pid::HAND_BRACKET_TEXT).toBool()&&symbol->getProperty(Pid::HAND_TEXT_AUTO).toBool()){auto shape=symbol->shape();if(shape.size()>2)result.append(shape.back());}
      return result;
      }
void beamRects(Beam* beam,const QPointF& origin,const QRectF& window,QVector<QRectF>* result) {
      Skyline outline;beam->addSkyline(outline);auto north=outline.north().begin(),south=outline.south().begin();
      while(north!=outline.north().end()&&south!=outline.south().end()) {
            const double first=qMax(north->x,south->x),last=qMin(north->x+north->w,south->x+south->w);
            if(last>first&&outline.north().valid(*north)&&outline.south().valid(*south)) {
                  auto r=QRectF(first,north->y,last-first,south->y-north->y).normalized().translated(origin);
                  if(r.right()>=window.left()&&r.left()<=window.right())result->append(r);
                  }
            if(north->x+north->w<south->x+south->w)++north;else if(south->x+south->w<north->x+north->w)++south;else{++north;++south;}
            }
      }
}
Style defaults(SymId id) {const int i=role(id);return i<0 ? Style{}:styles[i];}
bool setDefaults(SymId id,const Style& style) {
      const int i=role(id);if(i<0 || !std::isfinite(style.horizontal) || !std::isfinite(style.vertical) || !std::isfinite(style.width)
            || std::abs(style.horizontal)>40 || std::abs(style.vertical)>40 || style.width<.02 || style.width>2)return false;
      styles[i]=style;return true;
      }
QPointF position(const Symbol* symbol) {
      auto parent=symbol->parent();if(!parent||(!parent->isNote()&&!parent->isChord()))return {};
      auto chord=parent->isNote()?toNote(parent)->chord():toChord(parent);
      auto anchor=parent->isNote()?toNote(parent):symbol->handLeft()?chord->upNote():chord->downNote();
      const double sp=symbol->spatium()*symbol->mag(),grid=chord->spatium();if(sp<=0||grid<=0)return {};
      const QPointF anchorPosition=parent->isNote()?QPointF():anchor->pos();
      const auto head=anchor->bbox().translated(anchorPosition);auto measure=chord->measure();auto system=measure?measure->system():nullptr;
      const double staffTop=-chord->y()-(parent->isNote()?anchor->y():0.);
      const double h=symbol->getProperty(Pid::HAND_BRACKET_H).toDouble()*sp,v=symbol->getProperty(Pid::HAND_BRACKET_V).toDouble()*sp;
      const double edge=symbol->handLeft()?head.top()-.08*sp:head.bottom()+.08*sp;
      const double space=(edge-staffTop)/grid-.5;
      const double nearCorner=staffTop+((symbol->handLeft()?std::floor(space):std::ceil(space))+.5)*grid;
      int ordinal=0;const auto& list=parent->isNote()?toNote(parent)->el():toChord(parent)->el();
      for(auto other:list){if(other==symbol)break;if(other->isSymbol()&&toSymbol(other)->handBracket()&&toSymbol(other)->handLeft()==symbol->handLeft())++ordinal;}
      auto baseAt=[&](double gap,int move){
            const double corner=nearCorner+(symbol->handLeft()?-1:1)*(ordinal+move)*grid;
            const double x=symbol->handEnd()?head.right()+std::abs(h)+gap*sp:head.left()-std::abs(h)-gap*sp;
            return QPointF(x,corner-(symbol->handLeft()?v:0));
            };
      const auto initial=baseAt(.15,0);if(!symbol->autoplace()||!system||!chord->segment())return initial;
      const double reach=qMax(8.,std::abs(h)/sp+6)*sp;
      const auto window=head.adjusted(-reach,-6*sp,reach,6*sp);
      QVector<QRectF> obstacles;QSet<Beam*> beams;
      auto addChord=[&](Chord* c){
            chordRects(c,c->pagePos()-parent->pagePos(),window,&obstacles);
            if(c->beam()&&c->beam()->system()==system)beams.insert(c->beam());
            };
      addChord(chord);
      for(int side:{-1,1}){
            auto segment=chord->segment();
            for(int i=0;i<16;++i){
                  segment=side<0?segment->prev1(SegmentType::ChordRest):segment->next1(SegmentType::ChordRest);
                  if(!segment||segment->measure()->system()!=system)break;
                  if(std::abs(segment->pagePos().x()-chord->pagePos().x())>reach+2*sp)break;
                  for(int staff:QSet<int>{chord->staffIdx(),chord->vStaffIdx()})for(int track=staff*VOICES;track<(staff+1)*VOICES;++track){
                        auto element=segment->element(track);if(element&&element->isChord())addChord(toChord(element));
                        }
                  if(obstacles.size()>2048)break;
                  }
            }
      for(auto beam:beams)beamRects(beam,beam->pagePos()-parent->pagePos(),window,&obstacles);
      const auto ink=inkRects(symbol);QPointF best=initial;double bestCost=1e30;
      for(int move:{0,1,2,3})for(double gap:{.15,.25,.4,.6,.9}){
            const auto candidate=baseAt(gap,move);int hits=0;
            for(const auto& part:ink)for(const auto& obstacle:obstacles)if(part.translated(candidate).intersects(obstacle.adjusted(-.12*sp,-.12*sp,.12*sp,.12*sp)))++hits;
            const double cost=hits*1000.+move*.65+gap*.35;
            if(cost<bestCost){bestCost=cost;best=candidate;}
            }
      return best;
      }
} }
