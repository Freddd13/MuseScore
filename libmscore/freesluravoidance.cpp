// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "freeslur.h"
#include "freeslurrouting.h"
#include <QLineF>
#include <QSet>
#include <QCache>
#include <QDataStream>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
namespace Ms { namespace FreeSlur {
namespace {
CurvePoint at(const QVector<CurvePoint>& points,double t)
      {
      auto next=std::lower_bound(points.cbegin()+1,points.cend(),t,[](const CurvePoint& point,double value){return point.t<value;});
      if(next==points.cend())--next;
      const auto& a=*(next-1);const auto& b=*next;const double span=b.t-a.t;
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
      // Split where a straight sample chord would cut across the curved path.
      // The bound keeps this local calculation predictable on dense systems.
      double left=0;
      QVector<double> refined;
      std::function<void(double,double,int)> refine=[&](double a,double b,int depth) {
            const double mid=(a+b)/2;
            const QPointF first=at(points,a).p,last=at(points,b).p;
            const double error=QLineF(at(points,mid).p,(first+last)/2).length();
            if(error>sp*.01 && depth<4 && refined.size()<512) {refine(a,mid,depth+1);refine(mid,b,depth+1);}
            else refined.append(b);
            };
      for(double right:samples){if(right-left>1e-9)refine(left,right,0);left=right;}
      for(double t:refined) {
            if(t-previousT<1e-9)continue;
            const auto point=at(points,t);const QPointF midpoint=(previous+point.p)/2;
            ++sample;
            for(int b=obstacles.bucket(qMin(previous.x(),point.p.x()));b<=obstacles.bucket(qMax(previous.x(),point.p.x()));++b)for(int i:obstacles.buckets[b]) {
                  if(visited[i]==sample)continue;visited[i]=sample;
                  const auto r=obstacles.rectangles[i].adjusted(-.01*sp,-.01*sp,.01*sp,.01*sp);
                  if(!crosses(previous,point.p,r))continue;
                  collided.insert(i);
                  const double depth=qMax(.01,(up ? midpoint.y()-r.top() : r.bottom()-midpoint.y())/sp);
                  result.depth+=depth;
                  if(depth>result.worstDepth){result.worstDepth=depth;result.worstT=(t+previousT)/2;}
                  }
            // Penalize repeated changes of slope instead of rewarding arbitrary S curves.
            const auto before=at(points,previousT);
            const double angle=std::atan2(point.d.y(),point.d.x()),oldAngle=std::atan2(before.d.y(),before.d.x());
            const double turn=std::atan2(std::sin(angle-oldAngle),std::cos(angle-oldAngle));
            const double dt=t-previousT;
            const double baseline=points.front().p.y()+(points.back().p.y()-points.front().p.y())*t;
            const double height=(point.p.y()-baseline)/sp;
            result.shapeCost+=std::abs(turn)+.01*turn*turn/dt+.03*height*height*dt;
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
      {return points.size()<2?CurvePoint{t,{}, {}}:at(points,qBound(0.0,t,1.0));}
QPointF curvePosition(const QVector<CurvePoint>& points,double t)
      {return points.size()<2 ? QPointF() : at(points,qBound(0.0,t,1.0)).p;}
static AvoidanceResult autoAvoid(const QVector<CurvePoint>& original,const QVector<QRectF>& obstacles,bool up,double sp)
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
            }
      if(!best.collisions){result.collisions=0;return result;}
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
                  }
            if(!best.collisions)break;
            if(!improved)break;
            }
      result.collisions=best.collisions;return result;
      }
namespace {
struct RoutedEvaluation {Evaluation geometry;int wrong=0;double depth=0;};
RoutedEvaluation routedEvaluation(const QVector<CurvePoint>& points,const ObstacleIndex& obstacles,bool up,double sp,const QVector<Route>& routes,const QVector<QRectF>& guides)
      {
      RoutedEvaluation result;result.geometry=evaluate(points,obstacles,up,sp);QSet<int> wrong;
      for(const auto& route:routes){
            const double left=at(points,route.from).p.x(),right=at(points,route.until).p.x();if(right<=left)continue;
            for(int i=0;i<guides.size();++i){
                  const auto r=guides[i];const double x=r.center().x();if(x<=left+.01*sp||x>=right-.01*sp)continue;
                  double a=route.from,b=route.until;for(int n=0;n<10;++n){const double t=(a+b)/2;if(at(points,t).p.x()<x)a=t;else b=t;}
                  const double y=at(points,(a+b)/2).p.y(),depth=route.direction<0?y-r.top():r.bottom()-y;
                  if(depth>.01*sp){wrong.insert(i);result.depth+=depth/sp;}
                  }
            }
      result.wrong=wrong.size();return result;
      }
bool betterRouted(const RoutedEvaluation& a,const RoutedEvaluation& b)
      {
      if(a.wrong!=b.wrong)return a.wrong<b.wrong;
      if(a.wrong&&std::abs(a.depth-b.depth)>1e-7)return a.depth<b.depth;
      return better(a.geometry,b.geometry);
      }
int knot(QVector<CurvePoint>* points,double t)
      {
      auto it=std::lower_bound(points->begin(),points->end(),t,[](const CurvePoint& point,double value){return point.t<value;});
      const int index=int(it-points->begin());
      if(it!=points->end()&&std::abs(it->t-t)<1e-7)return index;
      if(index&&std::abs((*points)[index-1].t-t)<1e-7)return index-1;
      const auto p=at(*points,t);points->insert(index,p);return index;
      }
}
AvoidanceResult avoid(const QVector<CurvePoint>& original,const QVector<QRectF>& obstacles,bool up,double sp,const QVector<Route>& routes,bool automatic,const QVector<QRectF>* routeShapes)
      {
      AvoidanceResult result;result.points=original;
      if(original.size()<2||original.size()>maximumNodes+2||obstacles.size()>2048||!std::isfinite(sp)||sp<=0){result.limited=true;return result;}
      for(const auto& route:routes)if(route.from<0||route.until>1||route.until<=route.from||(route.direction!=-1&&route.direction!=1)){result.limited=true;return result;}
      const auto& guideShapes=routeShapes?*routeShapes:obstacles;
      QByteArray key;QDataStream stream(&key,QIODevice::WriteOnly);stream<<sp<<up<<automatic<<quint32(original.size());
      for(const auto& p:original)stream<<p.t<<p.p<<p.d<<p.locked;
      stream<<quint32(obstacles.size());for(const auto& r:obstacles)stream<<r;
      stream<<quint32(routes.size());for(const auto& route:routes)stream<<route.from<<route.until<<route.direction;
      stream<<bool(routeShapes);if(routeShapes){stream<<quint32(guideShapes.size());for(const auto& r:guideShapes)stream<<r;}
      static thread_local QCache<QByteArray,AvoidanceResult> cache(24);
      if(auto found=cache.object(key))return *found;
      auto finish=[&](){cache.insert(key,new AvoidanceResult(result));return result;};
      if(routes.isEmpty()){if(automatic)result=autoAvoid(original,obstacles,up,sp);return finish();}
      const ObstacleIndex index(obstacles);auto best=routedEvaluation(original,index,up,sp,routes,guideShapes);
      if(!automatic&&!best.wrong&&!best.geometry.collisions)return finish();
      const auto smooth=smoothRouting(original,obstacles,guideShapes,routes,sp,automatic);
      if(!smooth.points.isEmpty() && bounded(smooth.points,original,sp)) {
            const auto score=routedEvaluation(smooth.points,index,up,sp,routes,guideShapes);++result.candidates;
            if(betterRouted(score,best)){best=score;result.points=smooth.points;result.limited=smooth.limited;}
            if(!best.wrong&&!best.geometry.collisions)return finish();
            }

      if(automatic){auto candidate=autoAvoid(original,obstacles,up,sp);const auto score=routedEvaluation(candidate.points,index,up,sp,routes,guideShapes);result.candidates+=candidate.candidates;if(betterRouted(score,best)){result.points=candidate.points;best=score;}}
      for(const auto& route:routes){
            if(!best.wrong&&!best.geometry.collisions)break;
            auto base=result.points;for(int i=0;i<=6;++i)knot(&base,route.from+(route.until-route.from)*i/6.);
            for(double height:{.5,1.,1.5,2.,3.,4.,6.}){
                  if(result.candidates>=96){result.limited=true;break;}
                  auto candidate=base;const double span=route.until-route.from;
                  for(auto& point:candidate){if(point.locked||point.t<route.from||point.t>route.until)continue;
                        const double u=qBound(0.,(point.t-route.from)/span,1.);
                        point.p.ry()+=route.direction*sp*height*16*u*u*(1-u)*(1-u);
                        point.d.ry()+=route.direction*sp*height*32*u*(1-u)*(1-2*u)/span;
                        }
                  if(!bounded(candidate,original,sp))continue;++result.candidates;
                  const auto score=routedEvaluation(candidate,index,up,sp,routes,guideShapes);if(betterRouted(score,best)){result.points=candidate;best=score;}
                  }
            }
      for(int pass=0;pass<4&&best.geometry.collisions&&result.candidates<96;++pass){
            auto base=result.points;const double t=qBound(.02,best.geometry.worstT,.98);const int atIndex=knot(&base,t);if(base[atIndex].locked)break;
            int direction=up?-1:1;for(const auto& route:routes)if(t>=route.from&&t<=route.until)direction=route.direction;
            bool improved=false;for(double height:{.5,1.,2.,3.}){
                  auto candidate=base;candidate[atIndex].p.ry()+=direction*sp*height;if(!bounded(candidate,original,sp))continue;++result.candidates;
                  const auto score=routedEvaluation(candidate,index,up,sp,routes,guideShapes);if(betterRouted(score,best)){best=score;result.points=candidate;improved=true;}
                  }if(!improved)break;
            }
      result.collisions=best.geometry.collisions+best.wrong;return finish();
      }
} }
