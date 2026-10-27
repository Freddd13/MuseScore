// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "handbracket.h"
#include "score.h"
#include "symbol.h"
#include "chord.h"
#include "note.h"
#include "segment.h"
#include "tie.h"
#include <QMap>
#include <QSet>
#include <algorithm>
namespace Ms { namespace HandBracket {
namespace {
struct Mark {int tick=0,index=-1,side=0;bool end=false;};
struct Range {Mark start,end;};
struct Track {QVector<Mark> marks;QVector<const Note*> notes;QVector<Range> ranges[2];QMap<int,QVector<Mark>> singles;};
void addMarks(Track& track,const ElementList& elements,int tick,int index) {
      QList<const Symbol*> symbols;for(auto element:elements)if(element->isSymbol())symbols.append(toSymbol(element));
      for(int i=0;i<symbols.size();++i) {
            auto symbol=symbols[i];for(auto child:symbol->leafs())if(child->isSymbol())symbols.append(toSymbol(child));
            if(symbol->handEligible())track.marks.append({tick,index,symbol->handLeft()?0:1,symbol->handEnd()});
            }
      }
void build(Track& track) {
      std::stable_sort(track.marks.begin(),track.marks.end(),[](const Mark& a,const Mark& b){return a.tick<b.tick || (a.tick==b.tick && a.end<b.end);});
      int next[2]={-1,-1};QSet<int> matched;
      for(int i=track.marks.size()-1;i>=0;--i) {
            const auto& m=track.marks[i];if(m.end){next[m.side]=i;continue;}
            if(next[m.side]>=0){track.ranges[m.side].append({m,track.marks[next[m.side]]});matched.insert(next[m.side]);}
            else track.singles[m.tick].append(m);
            next[m.side]=-1;
            }
      for(int i=0;i<track.marks.size();++i)if(track.marks[i].end && !matched.contains(i))track.singles[track.marks[i].tick].append(track.marks[i]);
      for(auto& ranges:track.ranges)std::reverse(ranges.begin(),ranges.end());
      }
int resolve(const Track& track,const Note* note) {
      const int tick=note->chord()->tick().ticks();const auto& notes=note->chord()->notes();
      const int index=int(std::find(notes.begin(),notes.end(),note)-notes.begin());
      int found=-1;bool uncertain=false;
      auto accept=[&](int side){if(found>=0 && found!=side)uncertain=true;else found=side;};
      for(const auto& m:track.singles.value(tick))if(m.index<0 || m.index==index)accept(m.side);
      for(int side=0;side<2;++side) {
            const auto& ranges=track.ranges[side];auto i=std::upper_bound(ranges.cbegin(),ranges.cend(),tick,[](int tick,const Range& range){return tick<range.start.tick;});
            if(i==ranges.cbegin())continue;--i;if(tick>i->end.tick)continue;
            if(tick==i->start.tick && i->start.index>=0 && index!=i->start.index || tick==i->end.tick && i->end.index>=0 && index!=i->end.index)continue;
            if(i->start.index>=0 && i->end.index>=0 && tick>i->start.tick && tick<i->end.tick && notes.size()>1)uncertain=true;
            else accept(side);
            }
      return uncertain?-2:found;
      }
}
QHash<const Note*,int> assignments(const Score* score) {
      QHash<const Note*,int> result;if(!score)return result;QMap<int,Track> tracks;
      for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int number=0;number<score->nstaves()*VOICES;++number) {
            auto element=segment->element(number);if(!element || !element->isChord())continue;
            auto chord=toChord(element);auto& track=tracks[number];const int tick=segment->tick().ticks();addMarks(track,chord->el(),tick,-1);int index=0;
            for(auto note:chord->notes()){addMarks(track,note->el(),tick,index++);track.notes.append(note);}
            }
      for(auto& track:tracks) {
            build(track);for(auto note:track.notes){const int hand=resolve(track,note);if(hand!=-1)result[note]=hand;}
            // A hand change inside a tie needs a richer timed explanation; do not guess it in export.
            for(auto note:track.notes)if(note->tieBack() && result.contains(note)) {
                  auto origin=note;QSet<const Note*> visited;
                  while(origin->tieBack() && origin->tieBack()->startNote() && !visited.contains(origin)){visited.insert(origin);origin=origin->tieBack()->startNote();}
                  if(result.value(origin,-1)!=result[note])result[origin]=-2;
                  }
            }
      return result;
      }
} }
