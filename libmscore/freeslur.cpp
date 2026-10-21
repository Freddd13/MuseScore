// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "freeslur.h"
#include "slur.h"
#include "score.h"
#include "system.h"
#include "measure.h"
#include "xml.h"
#include "segment.h"
#include "beam.h"
#include "chord.h"
#include "note.h"
#include "skyline.h"
#include <QSet>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
namespace Ms {
namespace FreeSlur {
bool decode(const QString& data, QVector<FreeSlurNode>* result)
      {
      if(data.size()>16384) return false;
      QJsonParseError error; const auto document=QJsonDocument::fromJson(data.toUtf8(),&error);
      if(error.error!=QJsonParseError::NoError || !document.isArray() || document.array().size()>maximumNodes) return false;
      QVector<FreeSlurNode> nodes;
      for(const auto item:document.array()) {
            if(!item.isArray() || (item.toArray().size()!=5 && item.toArray().size()!=6 && item.toArray().size()!=8)) return false;
            const auto values=item.toArray();
            for(int i=0;i<5;++i) if(!values[i].isDouble() || !std::isfinite(values[i].toDouble())) return false;
            if(values[0].toDouble()<=0 || values[0].toDouble()>=1) return false;
            for(int i=1;i<5;++i) if(std::abs(values[i].toDouble())>(i<3 ? 100 : 400)) return false;
            if(values.size()>=6 && !values[5].isBool()) return false;
            int before=0,after=0;
            if(values.size()==8){
                  for(int i=6;i<8;++i)if(!values[i].isDouble()||values[i].toDouble()!=values[i].toInt()||values[i].toInt()<-1||values[i].toInt()>1)return false;
                  before=values[6].toInt();after=values[7].toInt();
                  }
            nodes.append({values[0].toDouble(),{values[1].toDouble(),values[2].toDouble()},{values[3].toDouble(),values[4].toDouble()},values.size()==5 || values[5].toBool(),before,after});
            }
      std::sort(nodes.begin(),nodes.end(),[](const FreeSlurNode& a,const FreeSlurNode& b){return a.position<b.position;});
      for(int i=1;i<nodes.size();++i) if(nodes[i].position-nodes[i-1].position<.00001) return false;
      *result=nodes; return true;
      }
QString encode(const QVector<FreeSlurNode>& nodes)
      {
      QJsonArray values;
      for(const auto& n:nodes) {
            QJsonArray row{n.position,n.offset.x(),n.offset.y(),n.tangent.x(),n.tangent.y()};
            if(n.before||n.after){row.append(n.locked);row.append(n.before);row.append(n.after);}
            else if(!n.locked)row.append(false);
            values.append(row);
            }
      return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));
      }
bool removeNode(QVector<FreeSlurNode>* nodes,int index)
      {
      if(index<0||index>=nodes->size())return false;
      const int before=(*nodes)[index].before,after=index+1<nodes->size()?(*nodes)[index+1].before:(*nodes)[index].after;
      const int joined=before==after?before:0;
      if(index+1<nodes->size())(*nodes)[index+1].before=joined;else if(index)(*nodes)[index-1].after=joined;
      nodes->removeAt(index);return true;
      }
QVector<Route> routing(const QVector<FreeSlurNode>& nodes,double from,double until)
      {
      QVector<Route> result;if(nodes.isEmpty()||until<=from)return result;
      for(int i=0;i<=nodes.size();++i){
            const double left=i?nodes[i-1].position:0.,right=i<nodes.size()?nodes[i].position:1.;
            const int direction=i<nodes.size()?nodes[i].before:nodes.back().after;
            const double a=qMax(left,from),b=qMin(right,until);
            if(direction&&b>a){
                  const double begin=(a-from)/(until-from),end=(b-from)/(until-from);
                  if(!result.isEmpty()&&result.back().direction==direction&&std::abs(result.back().until-begin)<1e-8)result.back().until=end;
                  else result.append({begin,end,direction});
                  }
            }
      return result;
      }
}
QVariant Slur::getProperty(Pid id) const
      {
      if(id==Pid::FREE_SLUR_START_SIDE)return _freeStartSide;
      if(id==Pid::FREE_SLUR_END_SIDE)return _freeEndSide;
      if(id==Pid::FREE_SLUR_MODE) return _freeMode;
      if(id==Pid::FREE_SLUR_AUTO) return _freeAuto;
      if(id==Pid::FREE_SLUR_NODES) return _freeData;
      if(id==Pid::FREE_SLUR_ENDPOINTS) return _freeEndpointData;
      return SlurTie::getProperty(id);
      }
QVariant Slur::propertyDefault(Pid id) const
      {
      if(id==Pid::FREE_SLUR_MODE || id==Pid::FREE_SLUR_AUTO) return false;
      if(id==Pid::FREE_SLUR_START_SIDE||id==Pid::FREE_SLUR_END_SIDE)return 0;
      if(id==Pid::FREE_SLUR_NODES) return QString("[]");
      if(id==Pid::FREE_SLUR_ENDPOINTS) return QString("{}");
      return SlurTie::propertyDefault(id);
      }
bool Slur::setProperty(Pid id,const QVariant& value)
      {
      if(id==Pid::FREE_SLUR_START_SIDE||id==Pid::FREE_SLUR_END_SIDE){int side=value.toInt();if(side<-1||side>1)return false;if(id==Pid::FREE_SLUR_START_SIDE)_freeStartSide=side;else _freeEndSide=side;}
      else if(id==Pid::FREE_SLUR_MODE) _freeMode=value.toBool();
      else if(id==Pid::FREE_SLUR_AUTO) _freeAuto=value.toBool();
      else if(id==Pid::FREE_SLUR_ENDPOINTS) {
            std::array<FreeSlurEndpoint,2> endpoints;
            if(!FreeSlur::decodeEndpoints(value.toString(),&endpoints))return false;
            _freeEndpoints=endpoints;_freeEndpointData=FreeSlur::encodeEndpoints(endpoints);
            }
      else if(id==Pid::FREE_SLUR_NODES) {
            QVector<FreeSlurNode> nodes; if(!FreeSlur::decode(value.toString(),&nodes)) return false;
            _freeNodes=nodes; _freeData=FreeSlur::encode(nodes);
            }
      else return SlurTie::setProperty(id,value);
      triggerLayout(); return true;
      }
namespace {
QPointF derivative(const QPointF& p0,const QPointF& p1,const QPointF& p2,const QPointF& p3,double t)
      { const double r=1-t; return 3*(r*r*(p1-p0)+2*r*t*(p2-p1)+t*t*(p3-p2)); }
void appendBeamObstacles(Beam* beam,const QPointF& origin,double left,double right,double padding,QVector<QRectF>* result)
      {
      Skyline outline;beam->addSkyline(outline);
      auto north=outline.north().begin(),south=outline.south().begin();
      while(north!=outline.north().end() && south!=outline.south().end()) {
            const double first=qMax(north->x,south->x),last=qMin(north->x+north->w,south->x+south->w);
            if(last>first && outline.north().valid(*north) && outline.south().valid(*south)) {
                  auto rect=QRectF(first,north->y,last-first,south->y-north->y).normalized().translated(origin).adjusted(-padding,-padding,padding,padding);
                  rect.setLeft(qMax(rect.left(),left));rect.setRight(qMin(rect.right(),right));
                  if(rect.width()>0 && rect.height()>0)result->append(rect);
                  if(result->size()>2048)return;
                  }
            if(north->x+north->w<south->x+south->w)++north;
            else if(south->x+south->w<north->x+north->w)++south;
            else{++north;++south;}
            }
      }
using FreeSlur::CurvePoint;
QPainterPath curvePath(const QVector<CurvePoint>& points,QPointF thickness)
      {
      auto position=[&](const CurvePoint& p,double sign){return p.p+sign*thickness*(3*p.t*(1-p.t));};
      auto direction=[&](const CurvePoint& p,double sign){return p.d+sign*thickness*(3*(1-2*p.t));};
      QPainterPath path; path.moveTo(position(points.front(),-1));
      for(int i=1;i<points.size();++i) {
            const auto& a=points[i-1];const auto& b=points[i]; const double span=(b.t-a.t)/3;
            path.cubicTo(position(a,-1)+direction(a,-1)*span,position(b,-1)-direction(b,-1)*span,position(b,-1));
            }
      if(!thickness.isNull()) {
            for(int i=points.size()-1;i>0;--i) {
                  const auto& a=points[i];const auto& b=points[i-1];const double span=(a.t-b.t)/3;
                  path.cubicTo(position(a,1)-direction(a,1)*span,position(b,1)+direction(b,1)*span,position(b,1));
                  }
            path.closeSubpath();
            }
      return path;
      }
}
bool FreeSlur::smoothNode(Slur* owner,int index,FreeSlurNode* result)
      {
      if(!owner||index<0||index>=owner->freeNodes().size()||!result)return false;
      for(auto native:owner->spannerSegments()){
            auto segment=toSlurSegment(native);FreeSlurNode shown;
            if(!segment->system()||!segment->displayedFreeNode(index,&shown))continue;
            const auto reference=segment->freeBaseControls();
            const auto p0=reference[0],p1=reference[1],p2=reference[2],p3=reference[3];
            const int first=qMax(owner->tick().ticks(),segment->system()->firstMeasure()->tick().ticks());
            const int last=qMin(owner->tick2().ticks(),segment->system()->lastMeasure()->endTick().ticks());if(last<=first)continue;
            const double sp=segment->spatium();
            const double tick=owner->tick().ticks()+shown.position*owner->ticks().ticks();
            if(tick<first||tick>=last)continue;
            const double t=qBound(.00000001,(tick-first)/(last-first),.99999999);
            const auto& points=segment->freeCurvePoints();
            const auto found=std::lower_bound(points.cbegin(),points.cend(),t,[](const CurvePoint& point,double value){return point.t<value-1e-8;});
            const int selected=int(found-points.cbegin());
            if(selected<1||selected>=points.size()-1)continue;
            const auto& a=points[selected-1];const auto& n=points[selected];const auto& b=points[selected+1];
            const double left=n.t-a.t,right=b.t-n.t;if(left<1e-8||right<1e-8)continue;
            const auto before=(n.p-a.p)/left,after=(b.p-n.p)/right;
            auto harmonic=[&](double x,double y){return x*y<=0?0.:(3*(left+right))/((2*right+left)/x+(right+2*left)/y);};
            const auto nativeTangent=derivative(p0,p1,p2,p3,n.t);
            // Shape-preserving derivatives follow the visible neighbors in both axes.
            const QPointF desired(harmonic(before.x(),after.x()),harmonic(before.y(),after.y()));
            *result=shown;result->locked=true;result->tangent=(desired-nativeTangent)/sp;return true;
            }
      return false;
      }
int SlurSegment::freeEndpointSide(bool start) const
      {
      const auto element=start?slur()->startElement():slur()->endElement();
      const auto chord=!element?nullptr:element->isChord()?toChord(element):element->isNote()?toNote(element)->chord():nullptr;
      if(!chord||chord->measure()->system()!=system())return 0;
      int side=start?slur()->freeStartSide():slur()->freeEndSide();
      const auto& nodes=slur()->freeNodes();
      if(!side&&ups(start?Grip::START:Grip::END).off.isNull()&&!nodes.isEmpty())
            side=start?nodes.front().before:nodes.back().after;
      return side;
      }
// Saved interior nodes retain this reference; independent endpoint edits use
// freeDrawingControls() without changing existing point positions.
std::array<QPointF,4> SlurSegment::freeBaseControls() const
      {
      auto p0=ups(Grip::START).pos(),p1=ups(Grip::BEZIER1).pos(),p2=ups(Grip::BEZIER2).pos(),p3=ups(Grip::END).pos();
      auto chordFor=[](Element* e)->Chord*{return !e?nullptr:e->isChord()?toChord(e):e->isNote()?toNote(e)->chord():nullptr;};
      auto startChord=chordFor(slur()->startElement()),endChord=chordFor(slur()->endElement());
      auto endpoint=[&](bool start,int side,QPointF old){
            const auto grip=start?Grip::START:Grip::END;
            if(!side)return old;
            auto chord=start?startChord:endChord;if(!chord||chord->measure()->system()!=system())return old;
            auto e=start?slur()->startElement():slur()->endElement();
            auto note=e->isNote()?toNote(e):side<0?chord->upNote():chord->downNote();
            const auto box=note->bbox().translated(note->pagePos()-pagePos());
            return QPointF(box.center().x(),side<0?box.top()-.3*spatium():box.bottom()+.3*spatium())+ups(grip).off;
            };
      const int startSide=freeEndpointSide(true),endSide=freeEndpointSide(false);
      const auto newStart=endpoint(true,startSide,p0),newEnd=endpoint(false,endSide,p3);
      p1+=newStart-p0;p2+=newEnd-p3;p0=newStart;p3=newEnd;
      const double reach=qBound(.5*spatium(),std::abs(p3.x()-p0.x())*.12,2.*spatium());
      if(startSide&&ups(Grip::BEZIER1).off.isNull())p1.setY(p0.y()+startSide*reach);
      if(endSide&&ups(Grip::BEZIER2).off.isNull())p2.setY(p3.y()+endSide*reach);
      return {{p0,p1,p2,p3}};
      }
bool SlurSegment::computeFreeBezier(QPointF thickness)
      {
      _freeGrips.clear();_freeCurvePoints.clear();
      _freeCollisions=0; _freeLimited=false;
      if(!slur()->freeMode() || (slur()->freeNodes().isEmpty() && !slur()->freeAuto() && !slur()->freeStartSide() && !slur()->freeEndSide() && !slur()->freeEndpointsEdited()) || !system()) return false;
      const int first=qMax(slur()->tick().ticks(),system()->firstMeasure()->tick().ticks());
      const int last=qMin(slur()->tick2().ticks(),system()->lastMeasure()->endTick().ticks());
      if(last<=first) return false;
      const auto reference=freeBaseControls();
      const auto drawing=freeDrawingControls();
      const auto p0=drawing[0],p1=drawing[1],p2=drawing[2],p3=drawing[3];
      auto chordFor=[](Element* e)->Chord*{return !e?nullptr:e->isChord()?toChord(e):e->isNote()?toNote(e)->chord():nullptr;};
      auto startChord=chordFor(slur()->startElement()),endChord=chordFor(slur()->endElement());
      const int startSide=freeEndpointSide(true),endSide=freeEndpointSide(false);
      const CubicBezier base(reference[0],reference[1],reference[2],reference[3]); const double sp=spatium();
      QVector<CurvePoint> points {{0,p0,derivative(p0,p1,p2,p3,0)}};
      const auto& nodes=slur()->freeNodes();
      const auto routes=FreeSlur::routing(nodes,double(first-slur()->tick().ticks())/slur()->ticks().ticks(),double(last-slur()->tick().ticks())/slur()->ticks().ticks());
      for(int i=0;i<nodes.size();++i) {
            const auto& node=nodes[i]; const double tick=slur()->tick().ticks()+node.position*slur()->ticks().ticks();
            if(tick<first || tick>=last) continue;
            const double t=qBound(.00000001,(tick-first)/(last-first),.99999999);
            const QPointF p=base.pointAtPercent(t)+node.offset*sp;
            const QPointF d=derivative(reference[0],reference[1],reference[2],reference[3],t)+node.tangent*sp;
            points.append({t,p,d,node.locked}); _freeGrips.append({i,p,{}, {},0,0});
            }
      points.append({1,p3,derivative(p0,p1,p2,p3,1)});
      if((slur()->freeAuto() && autoplace()) || !routes.isEmpty()) {
            QVector<QRectF> obstacles,phraseGuides;QSet<Beam*> beams;
            const double padding=qMax(.2*sp,score()->styleP(Sid::slurMinDistance));
            auto ss=slur()->startSegment();auto es=slur()->endSegment();
            const int firstStaff=qMax(0,qMin(staffIdx(),qMin(startChord?startChord->vStaffIdx():staffIdx(),endChord?endChord->vStaffIdx():staffIdx())));
            const int lastStaff=qMin(score()->nstaves()-1,qMax(staffIdx(),qMax(startChord?startChord->vStaffIdx():staffIdx(),endChord?endChord->vStaffIdx():staffIdx())));
            // Shapes use the same local staff coordinates as native slur placement.
            for(auto segment=system()->firstMeasure()->first();segment;segment=segment->next1()) {
                  if(segment->measure()->system()!=system()) break;
                  if(!segment->enabled() || (segment->segmentType()&SegmentType::BarLineType)) continue;
                  const double x=segment->x()+segment->measure()->x();
                  if(x+segment->width()<qMin(p0.x(),p3.x()))continue;
                  if(x>qMax(p0.x(),p3.x()))break;
                  if(segment->segmentType()==SegmentType::ChordRest)for(int track=firstStaff*VOICES;track<(lastStaff+1)*VOICES;++track) {
                        auto element=segment->element(track);if(!element || !element->isChord())continue;
                        auto beam=toChord(element)->beam();if(!beam || beams.contains(beam) || beam->system()!=system())continue;
                        beams.insert(beam);appendBeamObstacles(beam,beam->pagePos()-pagePos(),qMin(p0.x(),p3.x())+.25*sp,qMax(p0.x(),p3.x())-.25*sp,padding,&obstacles);
                        }
                  if(obstacles.size()>2048)break;
                  if(segment==ss || segment==es)continue;
                  if(!routes.isEmpty() && segment->segmentType()==SegmentType::ChordRest) {
                        Chord* phrase=nullptr;double nearest=1e100;
                        const int startTrack=slur()->track(),endTrack=slur()->track2();
                        const double t=qBound(0.,double(segment->tick().ticks()-first)/(last-first),1.);
                        const double expected=base.pointAtPercent(t).y();
                        int direction=0;
                        for(const auto& route:routes)if(t>=route.from && (t<route.until || route.until==1)) {direction=route.direction;break;}
                        for(int track:{startTrack,endTrack}) {
                              auto element=segment->element(track);if(!element||!element->isChord())continue;
                              auto chord=toChord(element);
                              auto head=chord->upNote()->bbox().translated(chord->upNote()->pagePos()-pagePos());
                              head=head.united(chord->downNote()->bbox().translated(chord->downNote()->pagePos()-pagePos()));
                              const double detour=direction<0?qMax(0.,expected-head.top()):direction>0?qMax(0.,head.bottom()-expected):0.;
                              const double distance=std::abs(head.center().y()-expected)+2*detour;
                              if(distance<nearest){nearest=distance;phrase=chord;}
                              }
                        if(phrase)for(const auto& r:phrase->shape()) {
                              auto rect=QRectF(r).translated(phrase->pagePos()-pagePos()).adjusted(-padding,-padding,padding,padding);
                              if(rect.width()>0 && rect.height()>0)phraseGuides.append(rect);
                              }
                        }
                  for(int targetStaff=firstStaff;targetStaff<=lastStaff;++targetStaff){
                  const auto shape=segment->staffShape(targetStaff).translated(segment->pos()+segment->measure()->pos()-pos()+QPointF(0,system()->staffYpage(targetStaff)-system()->staffYpage(staffIdx())));
                  for(const auto& r:shape) {
                        if(r.width()<=0 || r.height()<=0)continue;
                        obstacles.append(QRectF(r).adjusted(-padding,-padding,padding,padding));
                        if(obstacles.size()>2048)break;
                        }
                  if(obstacles.size()>2048)break;
                  }
                  }
            // Native endpoint/control edits also constrain the automatic end tangents.
            points.front().locked=(isSingleBeginType()&&slur()->freeEndpoints()[0].manualTangent)||startSide||!ups(Grip::START).off.isNull() || !ups(Grip::BEZIER1).off.isNull();
            points.back().locked=(isSingleEndType()&&slur()->freeEndpoints()[1].manualTangent)||endSide||!ups(Grip::END).off.isNull() || !ups(Grip::BEZIER2).off.isNull();
            const bool preferredUp=startSide?startSide<0:endSide?endSide<0:slur()->up();
            const auto result=FreeSlur::avoid(points,obstacles,preferredUp,sp,routes,slur()->freeAuto()&&autoplace(),routes.isEmpty()?nullptr:&phraseGuides);
            points=result.points;_freeCollisions=result.collisions;_freeLimited=result.limited;
            }
      for(int i=0;i<_freeGrips.size();++i) {
            auto& grips=_freeGrips[i];const auto& node=nodes[grips.index];
            const double tick=slur()->tick().ticks()+node.position*slur()->ticks().ticks();
            const double t=qBound(.00000001,(tick-first)/(last-first),.99999999);
            int j=1;while(j<points.size()-1 && std::abs(points[j].t-t)>.00000001)++j;
            const auto& p=points[j];grips.point=p.p;
            const double before=i?nodes[_freeGrips[i-1].index].position:double(first-slur()->tick().ticks())/slur()->ticks().ticks();
            const double after=i+1<_freeGrips.size()?nodes[_freeGrips[i+1].index].position:double(last-slur()->tick().ticks())/slur()->ticks().ticks();
            const double span=double(last-first)/slur()->ticks().ticks();
            grips.leftScale=(node.position-before)/(3*span);grips.rightScale=(after-node.position)/(3*span);
            grips.left=p.p-p.d*grips.leftScale; grips.right=p.p+p.d*grips.rightScale;
            }
      _freeCurvePoints=points;
      path=curvePath(points,slur()->lineType()==0 ? thickness : QPointF());
      shapePath=curvePath(points,3*thickness);
      _shape.clear();
      for(int i=1;i<points.size();++i) {
            const auto& a=points[i-1];const auto& b=points[i];const double span=(b.t-a.t)/3;
            const CubicBezier piece(a.p,a.p+a.d*span,b.p-b.d*span,b.p); QPointF previous=a.p;
            for(int sample=1;sample<=16;++sample) {
                  const QPointF p=piece.pointAtPercent(sample/16.0); auto rectangle=QRectF(previous,p).normalized();
                  const double padding=qMax(score()->styleP(Sid::slurEndWidth),3*std::hypot(thickness.x(),thickness.y()));
                  rectangle.adjust(-padding,-padding,padding,padding); _shape.add(rectangle); previous=p;
                  }
            }
      return true;
      }
int SlurSegment::gripsCount() const { return int(Grip::GRIPS)+3*_freeGrips.size(); }
std::vector<QPointF> SlurSegment::gripsPositions(const EditData&) const
      {
      std::vector<QPointF> result; const QPointF origin=pagePos();
      for(int i=0;i<int(Grip::GRIPS);++i) result.push_back(_ups[i].pos()+origin);
      if(_freeCurvePoints.size()>=2){
            const auto& a=_freeCurvePoints.front();
            const auto& z=_freeCurvePoints.back();
            result[int(Grip::START)]=origin+a.p;result[int(Grip::END)]=origin+z.p;
            result[int(Grip::BEZIER1)]=origin+a.p+a.d*freeGripScale(true);
            result[int(Grip::BEZIER2)]=origin+z.p-z.d*freeGripScale(false);
            }
      for(const auto& n:_freeGrips) {result.push_back(n.point+origin);result.push_back(n.left+origin);result.push_back(n.right+origin);}
      return result;
      }
void Slur::reset()
      {
      if (!_freeNodes.isEmpty()) undoChangeProperty(Pid::FREE_SLUR_NODES,QString("[]"));
      if (_freeMode) undoResetProperty(Pid::FREE_SLUR_MODE);
      if (_freeAuto) undoResetProperty(Pid::FREE_SLUR_AUTO);
      undoResetProperty(Pid::FREE_SLUR_ENDPOINTS);
      undoResetProperty(Pid::FREE_SLUR_START_SIDE);undoResetProperty(Pid::FREE_SLUR_END_SIDE);
      SlurTie::reset();
      }
QVector<QLineF> SlurSegment::gripAnchorLines(Grip grip) const
      {
      const int index=int(grip)-int(Grip::GRIPS);
      if(index<0) {
            if(freeEndpointGrip(grip)) {
                  const auto points=gripsPositions();const auto origin=canvasPos()-pagePos();
                  const auto a=(grip==Grip::START||grip==Grip::BEZIER1)?Grip::START:Grip::END;
                  const auto b=a==Grip::START?Grip::BEZIER1:Grip::BEZIER2;
                  return {QLineF(points[int(a)]+origin,points[int(b)]+origin)};
                  }
            return SlurTieSegment::gripAnchorLines(grip);
            }
      if(index/3>=_freeGrips.size()) return {};
      const auto& n=_freeGrips[index/3]; const QPointF origin=canvasPos();
      return {QLineF(n.point+origin,n.left+origin),QLineF(n.point+origin,n.right+origin)};
      }
Element* SlurSegment::propertyDelegate(Pid id)
      { return id==Pid::FREE_SLUR_MODE || id==Pid::FREE_SLUR_AUTO || id==Pid::FREE_SLUR_NODES || id==Pid::FREE_SLUR_ENDPOINTS || id==Pid::FREE_SLUR_START_SIDE || id==Pid::FREE_SLUR_END_SIDE ? slur() : SlurTieSegment::propertyDelegate(id); }
QVariant SlurSegment::getProperty(Pid id) const
      { return id==Pid::FREE_SLUR_MODE || id==Pid::FREE_SLUR_AUTO || id==Pid::FREE_SLUR_NODES || id==Pid::FREE_SLUR_ENDPOINTS || id==Pid::FREE_SLUR_START_SIDE || id==Pid::FREE_SLUR_END_SIDE ? slur()->getProperty(id) : SlurTieSegment::getProperty(id); }
QVariant SlurSegment::propertyDefault(Pid id) const
      { return id==Pid::FREE_SLUR_MODE || id==Pid::FREE_SLUR_AUTO || id==Pid::FREE_SLUR_NODES || id==Pid::FREE_SLUR_ENDPOINTS || id==Pid::FREE_SLUR_START_SIDE || id==Pid::FREE_SLUR_END_SIDE ? slur()->propertyDefault(id) : SlurTieSegment::propertyDefault(id); }
bool SlurSegment::setProperty(Pid id,const QVariant& value)
      { return id==Pid::FREE_SLUR_MODE || id==Pid::FREE_SLUR_AUTO || id==Pid::FREE_SLUR_NODES || id==Pid::FREE_SLUR_ENDPOINTS || id==Pid::FREE_SLUR_START_SIDE || id==Pid::FREE_SLUR_END_SIDE ? slur()->setProperty(id,value) : SlurTieSegment::setProperty(id,value); }
bool SlurSegment::displayedFreeNode(double position,FreeSlurNode* result) const
      {
      if(_freeCurvePoints.size()<2 || !system())return false;
      const int first=qMax(slur()->tick().ticks(),system()->firstMeasure()->tick().ticks());
      const int last=qMin(slur()->tick2().ticks(),system()->lastMeasure()->endTick().ticks());
      const double tick=slur()->tick().ticks()+position*slur()->ticks().ticks();
      if(last<=first || tick<first || tick>=last)return false;
      const double t=qBound(.00000001,(tick-first)/(last-first),.99999999);
      const auto point=FreeSlur::curvePoint(_freeCurvePoints,t);
      const auto reference=freeBaseControls();
      const auto p0=reference[0],p1=reference[1],p2=reference[2],p3=reference[3];
      *result={position,(point.p-CubicBezier(p0,p1,p2,p3).pointAtPercent(t))/spatium(),
            (point.d-derivative(p0,p1,p2,p3,t))/spatium(),true};return true;
      }
bool SlurSegment::displayedFreeNode(int index,FreeSlurNode* result) const
      {
      const auto& nodes=slur()->freeNodes();if(index<0 || index>=nodes.size())return false;
      if(nodes[index].locked){*result=nodes[index];return true;}
      if(!displayedFreeNode(nodes[index].position,result))return false;
      result->before=nodes[index].before;result->after=nodes[index].after;return true;
      }
void SlurSegment::startEditDrag(EditData& ed)
      {
      if(freeEndpointGrip(ed.curGrip))_freeDragProperty=Pid::FREE_SLUR_ENDPOINTS;
      else if(int(ed.curGrip)>=int(Grip::GRIPS))_freeDragProperty=Pid::FREE_SLUR_NODES;
      else { _freeDragOriginal.clear(); SlurTieSegment::startEditDrag(ed); return; }
      _freeDragOriginal=slur()->getProperty(_freeDragProperty).toString(); _freeDragging=true;
      }
void SlurSegment::editDrag(EditData& ed)
      {
      const int index=int(ed.curGrip)-int(Grip::GRIPS);
      if(_freeDragging && _freeDragProperty==Pid::FREE_SLUR_ENDPOINTS) {
            const bool start=ed.curGrip==Grip::START||ed.curGrip==Grip::BEZIER1;
            const bool position=ed.curGrip==Grip::START||ed.curGrip==Grip::END;
            const int i=start?0:1;auto endpoints=slur()->freeEndpoints();auto& endpoint=endpoints[i];
            const auto shown=start?_freeCurvePoints.front():_freeCurvePoints.back();
            if(position) {
                  endpoint.position=(shown.p+ed.delta-freeAutoEndpoint(start))/spatium();
                  endpoint.manualPosition=true;
                  }
            const auto defaults=freeDrawingControls(false);
            const QPointF target=position?shown.d:shown.d+ed.delta/(start?freeGripScale(true):-freeGripScale(false));
            endpoint.tangent=(target-derivative(defaults[0],defaults[1],defaults[2],defaults[3],start?0:1))/spatium();
            endpoint.manualTangent=true;
            if(slur()->setProperty(Pid::FREE_SLUR_ENDPOINTS,FreeSlur::encodeEndpoints(endpoints)))computeBezier();
            return;
            }
      if(index<0) { SlurTieSegment::editDrag(ed); return; }
      if(!_freeDragging) return;
      if(index/3>=_freeGrips.size()) return;
      const auto grip=_freeGrips[index/3]; auto nodes=slur()->freeNodes(); auto& node=nodes[grip.index];
      if(!node.locked) displayedFreeNode(grip.index,&node);
      node.locked=true;
      if(index%3==0) node.offset+=ed.delta/spatium();
      else {
            const double scale=index%3==1 ? -grip.leftScale : grip.rightScale;
            if(std::abs(scale)<.00001) return;
            node.tangent+=ed.delta/(spatium()*scale);
            }
      if(slur()->setProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(nodes))) computeBezier();
      }
void SlurSegment::endEditDrag(EditData& ed)
      {
      if(_freeDragOriginal.isEmpty()) { SlurTieSegment::endEditDrag(ed); return; }
      if(_freeDragging) {
            const auto value=slur()->getProperty(_freeDragProperty); slur()->setProperty(_freeDragProperty,_freeDragOriginal);
            if(value.toString()!=_freeDragOriginal) slur()->undoChangeProperty(_freeDragProperty,value);
            }
      _freeDragging=false; _freeDragOriginal.clear(); triggerLayout();
      }
void SlurSegment::endEdit(EditData& ed)
      {
      if(_freeDragging) {slur()->setProperty(_freeDragProperty,_freeDragOriginal);_freeDragging=false;triggerLayout();}
      _freeDragOriginal.clear(); SlurTieSegment::endEdit(ed);
      }
bool SlurSegment::editFreeNode(EditData& ed)
      {
      if(freeEndpointGrip(ed.curGrip)) {
            if(ed.key==Qt::Key_Escape && _freeDragging) {
                  slur()->setProperty(_freeDragProperty,_freeDragOriginal);_freeDragging=false;computeBezier();return true;
                  }
            if(ed.key==Qt::Key_Home) {
                  auto endpoints=slur()->freeEndpoints();
                  const bool start=ed.curGrip==Grip::START||ed.curGrip==Grip::BEZIER1;
                  auto& endpoint=endpoints[start?0:1];
                  if(ed.curGrip==Grip::START||ed.curGrip==Grip::END)endpoint.manualPosition=false;
                  else endpoint.manualTangent=false;
                  slur()->undoChangeProperty(Pid::FREE_SLUR_ENDPOINTS,FreeSlur::encodeEndpoints(endpoints));computeBezier();return true;
                  }
            return false;
            }
      const int index=int(ed.curGrip)-int(Grip::GRIPS);
      if(index<0 || index/3>=_freeGrips.size()) return false;
      if(ed.key==Qt::Key_Delete || ed.key==Qt::Key_Backspace || ed.key==Qt::Key_Home) {
            auto nodes=slur()->freeNodes(); const int n=_freeGrips[index/3].index;
            if(ed.key==Qt::Key_Home) { if(index%3==0) nodes[n].offset={}; else nodes[n].tangent={}; }
            else FreeSlur::removeNode(&nodes,n);
            slur()->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(nodes)); computeBezier();
            ed.curGrip=Grip::DRAG; ed.grips=gripsCount(); return true;
            }
      if(ed.key==Qt::Key_Escape && _freeDragging) {slur()->setProperty(_freeDragProperty,_freeDragOriginal);_freeDragging=false;computeBezier();return true;}
      return false;
      }
void SlurSegment::drawEditMode(QPainter* painter,EditData& ed)
      {
      SlurTieSegment::drawEditMode(painter,ed);
      painter->setPen(QPen(MScore::frameMarginColor,0));
      for(int i=int(Grip::GRIPS);i+2<ed.grip.size();i+=3) {painter->drawLine(ed.grip[i+1].center(),ed.grip[i].center());painter->drawLine(ed.grip[i].center(),ed.grip[i+2].center());}
      }
}
