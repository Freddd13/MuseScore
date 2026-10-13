// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "freeslur.h"
#include <QLineF>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
namespace Ms { namespace FreeSlur {
namespace {
CurvePoint at(const QVector<CurvePoint>& points,double t)
      {
      int i=1;while(i<points.size()-1 && points[i].t<t)++i;
      const auto& a=points[i-1];const auto& b=points[i];const double span=b.t-a.t;
      const double u=qBound(0.0,(t-a.t)/span,1.0),u2=u*u,u3=u2*u;
      return {t,(2*u3-3*u2+1)*a.p+(u3-2*u2+u)*span*a.d+(-2*u3+3*u2)*b.p+(u3-u2)*span*b.d,
            ((6*u2-6*u)*a.p+(3*u2-4*u+1)*span*a.d+(-6*u2+6*u)*b.p+(3*u2-2*u)*span*b.d)/span};
      }
bool crosses(const QPointF& a,const QPointF& b,const QRectF& r)
      {
      if(r.contains(a)||r.contains(b))return true;
      if(!QRectF(a,b).normalized().adjusted(-.001,-.001,.001,.001).intersects(r))return false;
      QLineF line(a,b);QPointF hit;
      for(const QLineF edge:{QLineF(r.topLeft(),r.topRight()),QLineF(r.topRight(),r.bottomRight()),
            QLineF(r.bottomRight(),r.bottomLeft()),QLineF(r.bottomLeft(),r.topLeft())})
            if(line.intersect(edge,&hit)==QLineF::BoundedIntersection)return true;
      return false;
      }
struct ObstacleIndex {
      const QVector<QRectF>& rectangles;
      std::array<QVector<int>,64> buckets;
      double left=0,width=1;
      int bucket(double x) const {return qBound(0,int(std::floor((x-left)/width*64)),63);}
      explicit ObstacleIndex(const QVector<QRectF>& input):rectangles(input)
            {
            if(input.isEmpty())return;
            left=input.front().left();double right=input.front().right();
            for(const auto& r:input){left=qMin(left,r.left());right=qMax(right,r.right());}
            width=qMax(1.0,right-left);
            for(int i=0;i<input.size();++i)for(int b=bucket(input[i].left());b<=bucket(input[i].right());++b)buckets[b].append(i);
            }
      };
struct Evaluation { int collisions=0;double depth=0,worstT=.5,worstDepth=0,shapeCost=0; };
Evaluation evaluate(const QVector<CurvePoint>& points,const ObstacleIndex& obstacles,bool up,double sp)
      {
      Evaluation result;QSet<int> collided;QVector<int> visited(obstacles.rectangles.size(),-1);int sample=0;
      QPointF previous=points.front().p;double previousT=0;
      // Include every supplied knot even when it falls between regular samples.
      QVector<double> samples;for(int i=1;i<=128;++i)samples.append(i/128.0);
      for(const auto& point:points)if(point.t>0)samples.append(point.t);
      std::sort(samples.begin(),samples.end());
      for(double t:samples) {
            if(t-previousT<1e-9)continue;
            const auto point=at(points,t);const QPointF midpoint=(previous+point.p)/2;
            ++sample;
            for(int b=obstacles.bucket(qMin(previous.x(),point.p.x()));b<=obstacles.bucket(qMax(previous.x(),point.p.x()));++b)for(int i:obstacles.buckets[b]) {
                  if(visited[i]==sample)continue;visited[i]=sample;
                  const auto& r=obstacles.rectangles[i];
                  if(!crosses(previous,point.p,r))continue;
                  collided.insert(i);
                  const double depth=qMax(.01,(up ? midpoint.y()-r.top() : r.bottom()-midpoint.y())/sp);
                  result.depth+=depth;
                  if(depth>result.worstDepth){result.worstDepth=depth;result.worstT=(t+previousT)/2;}
                  }
            // Penalize repeated changes of slope instead of rewarding arbitrary S curves.
            const auto before=at(points,previousT);
            const double dx=qMax(sp*.05,std::abs(point.d.x()));
            result.shapeCost+=std::abs(point.d.y()/dx-before.d.y()/qMax(sp*.05,std::abs(before.d.x())));
            previous=point.p;previousT=t;
            }
      result.collisions=collided.size();return result;
      }
bool bounded(const QVector<CurvePoint>& candidate,const QVector<CurvePoint>& original,double sp)
      {
      for(int i=0;i<=128;++i)if(std::abs(at(candidate,i/128.0).p.y()-at(original,i/128.0).p.y())>6*sp+.001)return false;
      return true;
      }
bool better(const Evaluation& a,const Evaluation& b)
      {
      if(a.collisions!=b.collisions)return a.collisions<b.collisions;
      return a.depth+a.shapeCost*.1<b.depth+b.shapeCost*.1-1e-7;
      }
}
CurvePoint curvePoint(const QVector<CurvePoint>& points,double t)
      {return at(points,qBound(0.0,t,1.0));}
QPointF curvePosition(const QVector<CurvePoint>& points,double t)
      {return points.size()<2 ? QPointF() : at(points,qBound(0.0,t,1.0)).p;}
AvoidanceResult avoid(const QVector<CurvePoint>& original,const QVector<QRectF>& obstacles,bool up,double sp)
      {
      AvoidanceResult result;result.points=original;
      if(original.size()<2 || sp<=0 || obstacles.size()>2048 || original.size()>maximumNodes+2) {
            result.limited=true;return result;
            }
      const ObstacleIndex obstacleIndex(obstacles);
      auto best=evaluate(original,obstacleIndex,up,sp);result.collisions=best.collisions;
      if(!best.collisions)return result;
      const double sign=up ? -1:1;
      // First try the ordinary smooth bow without adding any intermediate points.
      for(double height:{.5,1.0,2.0,3.0,4.0,6.0}) {
            auto candidate=original;
            for(auto& p:candidate) {
                  if(p.locked)continue;
                  p.p.ry()+=sign*sp*height*4*p.t*(1-p.t);
                  const double delta=sign*sp*height*4*(1-2*p.t);
                  const double limit=qMax(std::abs(p.d.y()),std::abs(p.d.x())*1.732);
                  p.d.setY(qBound(-limit,p.d.y()+delta,limit));
                  }
            if(!bounded(candidate,original,sp))continue;
            const auto score=evaluate(candidate,obstacleIndex,up,sp);++result.candidates;
            if(better(score,best)){best=score;result.points=candidate;}
            if(!best.collisions){result.collisions=0;return result;}
            }
      // A few local knots handle an obstacle that a single bow cannot avoid.
      // They exist only for this layout; saved manual data is never changed.
      for(int pass=0;pass<4 && best.collisions;++pass) {
            auto base=result.points;double t=qBound(.02,best.worstT,.98);
            int index=1;while(index<base.size()-1 && base[index].t<t)++index;
            if(t-base[index-1].t<.01 || base[index].t-t<.01)t=(base[index-1].t+base[index].t)/2;
            if(base[index].t-base[index-1].t<.02)break;
            auto point=at(base,t);point.locked=false;base.insert(index,point);
            bool improved=false;
            for(double height:{.5,1.0,2.0,3.0,4.0,6.0}) {
                  auto candidate=base;candidate[index].p.ry()+=sign*sp*height;
                  // Overall displacement remains within six staff spaces.
                  if(!bounded(candidate,original,sp))continue;
                  const auto score=evaluate(candidate,obstacleIndex,up,sp);++result.candidates;
                  if(better(score,best)){best=score;result.points=candidate;improved=true;}
                  if(!best.collisions)break;
                  }
            if(!improved)break;
            }
      result.collisions=best.collisions;return result;
      }
} }
