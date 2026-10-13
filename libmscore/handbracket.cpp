// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "handbracket.h"
#include "symbol.h"
#include "chord.h"
#include "note.h"
#include "stem.h"
#include "hook.h"
#include "accidental.h"
#include "beam.h"
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
Style styles[4]={{2,-1,.12,false},{-2,-1,.12,false},{2,-1,.12,false},{-2,-1,.12,false}};
void chordRects(const Chord* chord,const QPointF& origin,QVector<QRectF>* result) {
      for(auto note:chord->notes()) {
            result->append(note->bbox().translated(origin+note->pos()));
            if(note->accidental())result->append(note->accidental()->bbox().translated(origin+note->pos()+note->accidental()->pos()));
            }
      if(chord->stem())result->append(chord->stem()->bbox().translated(origin+chord->stem()->pos()));
      if(chord->hook())result->append(chord->hook()->bbox().translated(origin+chord->hook()->pos()));
      }
}
Style defaults(SymId id) {const int i=role(id);return i<0 ? Style{}:styles[i];}
bool setDefaults(SymId id,const Style& style) {
      const int i=role(id);if(i<0 || !std::isfinite(style.horizontal) || !std::isfinite(style.vertical) || !std::isfinite(style.width)
            || std::abs(style.horizontal)>40 || std::abs(style.vertical)>40 || style.width<.02 || style.width>2)return false;
      styles[i]=style;return true;
      }
QPointF position(const Symbol* symbol) {
      auto parent=symbol->parent();if(!parent || (!parent->isNote() && !parent->isChord()))return {};
      auto chord=parent->isNote() ? toNote(parent)->chord():toChord(parent);
      auto anchor=parent->isNote() ? toNote(parent):symbol->handLeft()?chord->downNote():chord->upNote();
      const double sp=symbol->spatium()*symbol->mag();
      const QPointF anchorPosition=parent->isNote() ? QPointF():anchor->pos();
      const auto head=anchor->bbox().translated(anchorPosition);
      QPointF base(head.left()-.6*sp,symbol->handLeft()?head.bottom()+.55*sp-symbol->getProperty(Pid::HAND_BRACKET_V).toDouble()*sp:head.top()-.55*sp);
      int ordinal=0;const auto& list=parent->isNote() ? toNote(parent)->el():toChord(parent)->el();
      for(auto other:list) {if(other==symbol)break;if(other->isSymbol() && toSymbol(other)->handBracket() && toSymbol(other)->handLeft()==symbol->handLeft())++ordinal;}
      base.ry()+=(symbol->handLeft()?1:-1)*ordinal*.65*sp;
      if(!symbol->autoplace())return base;
      QVector<QRectF> obstacles;
      const QPointF origin=chord->pagePos()-parent->pagePos();
      chordRects(chord,origin,&obstacles);
      auto segment=chord->segment();int count=0;
      // Only nearby chord groups, with a strict bound independent of score length.
      for(int side:{-1,1}) {
            auto s=segment;for(int i=0;i<16;++i) {
                  s=side<0 ? s->prev1(SegmentType::ChordRest):s->next1(SegmentType::ChordRest);
                  if(!s || s->measure()->system()!=chord->measure()->system())break;
                  for(int track=chord->staffIdx()*VOICES;track<(chord->staffIdx()+1)*VOICES;++track) {
                        auto element=s->element(track);if(element && element->isChord())chordRects(toChord(element),element->pagePos()-parent->pagePos(),&obstacles);
                        }
                  if(++count>=32)break;
                  }
            }
      if(chord->beam())obstacles.append(chord->beam()->bbox().translated(chord->beam()->pagePos()-parent->pagePos()));
      const auto shape=symbol->shape();QPointF best=base;int bestHits=INT_MAX;
      for(double move:{0.,.4,.8,1.2,1.6,2.4,3.2}) {
            const QPointF candidate=base+QPointF(0,(symbol->handLeft()?1:-1)*move*sp);
            int hits=0;for(const auto& part:shape)for(const auto& obstacle:obstacles)
                  if(part.translated(candidate).intersects(obstacle.adjusted(-.15*sp,-.15*sp,.15*sp,.15*sp)))++hits;
            if(hits<bestHits){bestHits=hits;best=candidate;}if(!hits)break;
            }
      return best;
      }
} }
