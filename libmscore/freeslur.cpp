// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "freeslur.h"
#include "slur.h"
#include "score.h"
#include "system.h"
#include "measure.h"
#include "xml.h"
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
            if(!item.isArray() || item.toArray().size()!=5) return false;
            const auto values=item.toArray();
            for(int i=0;i<5;++i) if(!values[i].isDouble() || !std::isfinite(values[i].toDouble())) return false;
            if(values[0].toDouble()<=0 || values[0].toDouble()>=1) return false;
            for(int i=1;i<5;++i) if(std::abs(values[i].toDouble())>(i<3 ? 100 : 400)) return false;
            nodes.append({values[0].toDouble(),{values[1].toDouble(),values[2].toDouble()},{values[3].toDouble(),values[4].toDouble()}});
            }
      std::sort(nodes.begin(),nodes.end(),[](const FreeSlurNode& a,const FreeSlurNode& b){return a.position<b.position;});
      for(int i=1;i<nodes.size();++i) if(nodes[i].position-nodes[i-1].position<.00001) return false;
      *result=nodes; return true;
      }
QString encode(const QVector<FreeSlurNode>& nodes)
      {
      QJsonArray values;
      for(const auto& n:nodes) values.append(QJsonArray{n.position,n.offset.x(),n.offset.y(),n.tangent.x(),n.tangent.y()});
      return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));
      }
}
QVariant Slur::getProperty(Pid id) const
      {
      if(id==Pid::FREE_SLUR_MODE) return _freeMode;
      if(id==Pid::FREE_SLUR_NODES) return _freeData;
      return SlurTie::getProperty(id);
      }
QVariant Slur::propertyDefault(Pid id) const
      {
      if(id==Pid::FREE_SLUR_MODE) return false;
      if(id==Pid::FREE_SLUR_NODES) return QString("[]");
      return SlurTie::propertyDefault(id);
      }
bool Slur::setProperty(Pid id,const QVariant& value)
      {
      if(id==Pid::FREE_SLUR_MODE) _freeMode=value.toBool();
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
struct CurvePoint { double t; QPointF p,d; };
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
bool SlurSegment::computeFreeBezier(QPointF thickness)
      {
      _freeGrips.clear();
      if(!slur()->freeMode() || slur()->freeNodes().isEmpty() || !system()) return false;
      const int first=qMax(slur()->tick().ticks(),system()->firstMeasure()->tick().ticks());
      const int last=qMin(slur()->tick2().ticks(),system()->lastMeasure()->endTick().ticks());
      if(last<=first) return false;
      const auto p0=ups(Grip::START).pos(),p1=ups(Grip::BEZIER1).pos(),p2=ups(Grip::BEZIER2).pos(),p3=ups(Grip::END).pos();
      const CubicBezier base(p0,p1,p2,p3); const double sp=spatium();
      QVector<CurvePoint> points {{0,p0,derivative(p0,p1,p2,p3,0)}};
      const auto& nodes=slur()->freeNodes();
      for(int i=0;i<nodes.size();++i) {
            const auto& node=nodes[i]; const double tick=slur()->tick().ticks()+node.position*slur()->ticks().ticks();
            if(tick<first || tick>=last) continue;
            const double t=qBound(.00000001,(tick-first)/(last-first),.99999999);
            const QPointF p=base.pointAtPercent(t)+node.offset*sp;
            const QPointF d=derivative(p0,p1,p2,p3,t)+node.tangent*sp;
            points.append({t,p,d}); _freeGrips.append({i,p,{}, {},0,0});
            }
      points.append({1,p3,derivative(p0,p1,p2,p3,1)});
      for(int i=0;i<_freeGrips.size();++i) {
            const auto& p=points[i+1]; auto& grips=_freeGrips[i];
            grips.leftScale=(p.t-points[i].t)/3; grips.rightScale=(points[i+2].t-p.t)/3;
            grips.left=p.p-p.d*grips.leftScale; grips.right=p.p+p.d*grips.rightScale;
            }
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
      for(const auto& n:_freeGrips) {result.push_back(n.point+origin);result.push_back(n.left+origin);result.push_back(n.right+origin);}
      return result;
      }
void Slur::reset()
      {
      if (!_freeNodes.isEmpty()) undoChangeProperty(Pid::FREE_SLUR_NODES,QString("[]"));
      if (_freeMode) undoResetProperty(Pid::FREE_SLUR_MODE);
      SlurTie::reset();
      }
QVector<QLineF> SlurSegment::gripAnchorLines(Grip grip) const
      {
      const int index=int(grip)-int(Grip::GRIPS);
      if(index<0) return SlurTieSegment::gripAnchorLines(grip);
      if(index/3>=_freeGrips.size()) return {};
      const auto& n=_freeGrips[index/3]; const QPointF origin=canvasPos();
      return {QLineF(n.point+origin,n.left+origin),QLineF(n.point+origin,n.right+origin)};
      }
Element* SlurSegment::propertyDelegate(Pid id)
      { return id==Pid::FREE_SLUR_MODE || id==Pid::FREE_SLUR_NODES ? slur() : SlurTieSegment::propertyDelegate(id); }
QVariant SlurSegment::getProperty(Pid id) const
      { return id==Pid::FREE_SLUR_MODE || id==Pid::FREE_SLUR_NODES ? slur()->getProperty(id) : SlurTieSegment::getProperty(id); }
QVariant SlurSegment::propertyDefault(Pid id) const
      { return id==Pid::FREE_SLUR_MODE || id==Pid::FREE_SLUR_NODES ? slur()->propertyDefault(id) : SlurTieSegment::propertyDefault(id); }
bool SlurSegment::setProperty(Pid id,const QVariant& value)
      { return id==Pid::FREE_SLUR_MODE || id==Pid::FREE_SLUR_NODES ? slur()->setProperty(id,value) : SlurTieSegment::setProperty(id,value); }
void SlurSegment::startEditDrag(EditData& ed)
      {
      if(int(ed.curGrip)<int(Grip::GRIPS)) { _freeDragOriginal.clear(); SlurTieSegment::startEditDrag(ed); return; }
      _freeDragOriginal=slur()->getProperty(Pid::FREE_SLUR_NODES).toString(); _freeDragging=true;
      }
void SlurSegment::editDrag(EditData& ed)
      {
      const int index=int(ed.curGrip)-int(Grip::GRIPS);
      if(index<0) { SlurTieSegment::editDrag(ed); return; }
      if(!_freeDragging) return;
      if(index/3>=_freeGrips.size()) return;
      const auto grip=_freeGrips[index/3]; auto nodes=slur()->freeNodes(); auto& node=nodes[grip.index];
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
            const auto value=slur()->getProperty(Pid::FREE_SLUR_NODES); slur()->setProperty(Pid::FREE_SLUR_NODES,_freeDragOriginal);
            if(value.toString()!=_freeDragOriginal) slur()->undoChangeProperty(Pid::FREE_SLUR_NODES,value);
            }
      _freeDragging=false; _freeDragOriginal.clear(); triggerLayout();
      }
void SlurSegment::endEdit(EditData& ed)
      {
      if(_freeDragging) {slur()->setProperty(Pid::FREE_SLUR_NODES,_freeDragOriginal);_freeDragging=false;triggerLayout();}
      _freeDragOriginal.clear(); SlurTieSegment::endEdit(ed);
      }
bool SlurSegment::editFreeNode(EditData& ed)
      {
      const int index=int(ed.curGrip)-int(Grip::GRIPS);
      if(index<0 || index/3>=_freeGrips.size()) return false;
      if(ed.key==Qt::Key_Delete || ed.key==Qt::Key_Backspace || ed.key==Qt::Key_Home) {
            auto nodes=slur()->freeNodes(); const int n=_freeGrips[index/3].index;
            if(ed.key==Qt::Key_Home) { if(index%3==0) nodes[n].offset={}; else nodes[n].tangent={}; }
            else nodes.removeAt(n);
            slur()->undoChangeProperty(Pid::FREE_SLUR_NODES,FreeSlur::encode(nodes)); computeBezier();
            ed.curGrip=Grip::DRAG; ed.grips=gripsCount(); return true;
            }
      if(ed.key==Qt::Key_Escape && _freeDragging) {slur()->setProperty(Pid::FREE_SLUR_NODES,_freeDragOriginal);_freeDragging=false;computeBezier();return true;}
      return false;
      }
void SlurSegment::drawEditMode(QPainter* painter,EditData& ed)
      {
      SlurTieSegment::drawEditMode(painter,ed);
      painter->setPen(QPen(MScore::frameMarginColor,0));
      for(int i=int(Grip::GRIPS);i+2<ed.grip.size();i+=3) {painter->drawLine(ed.grip[i+1].center(),ed.grip[i].center());painter->drawLine(ed.grip[i].center(),ed.grip[i+2].center());}
      }
}
