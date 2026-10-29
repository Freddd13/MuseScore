// Value-only routing of an editable slur through a smooth vertical corridor. GPL v2.
#include "freeslurrouting.h"
#include <QLineF>
#include <algorithm>
#include <array>
#include <cmath>

namespace Ms { namespace FreeSlur {
namespace {
struct Sample {
      double t, x, reference, value, low, high;
      int original = -1;
      bool fixed = false;
      };
double harmonic(double a, double b, double left, double right)
      {
      return a*b<=0 ? 0. : 3*(left+right)/((2*right+left)/a+(right+2*left)/b);
      }
QVector<CurvePoint> fit(const QVector<Sample>& samples, const QVector<int>& chosen,
                        const QVector<CurvePoint>& original, double sp)
      {
      QVector<CurvePoint> points;
      for(int i:chosen)points.append({samples[i].t,{samples[i].x,samples[i].value*sp},{},samples[i].fixed});
      for(int i=0;i<points.size();++i) {
            const auto& sample=samples[chosen[i]];
            if(sample.original>=0 && (sample.fixed || i==0 || i==points.size()-1)) {
                  points[i].d=original[sample.original].d;
                  continue;
                  }
            if(i==0 || i==points.size()-1) {
                  const int a=i?i-1:i,b=a+1;
                  points[i].d=(points[b].p-points[a].p)/(points[b].t-points[a].t);
                  continue;
                  }
            const double left=points[i].t-points[i-1].t,right=points[i+1].t-points[i].t;
            const auto before=(points[i].p-points[i-1].p)/left,after=(points[i+1].p-points[i].p)/right;
            points[i].d={harmonic(before.x(),after.x(),left,right),harmonic(before.y(),after.y(),left,right)};
            }
      return points;
      }
}

RoutingCandidate smoothRouting(const QVector<CurvePoint>& original, const QVector<QRectF>& obstacles,
                                const QVector<QRectF>& guides, const QVector<Route>& routes,
                                double sp, bool automatic, double detourSpaces)
      {
      RoutingCandidate result;
      if(original.size()<2 || routes.isEmpty() || sp<=0)return result;
      constexpr int divisions=48;
      QVector<double> times;
      for(const auto& point:original)times.append(point.t);
      for(int i=1;i<divisions;++i) {
            const double t=double(i)/divisions;
            if(std::none_of(original.cbegin(),original.cend(),[t](const CurvePoint& p){return std::abs(p.t-t)<1e-8;}))
                  times.append(t);
            }
      std::sort(times.begin(),times.end());
      QVector<Sample> samples;
      for(double t:times) {
            const auto point=curvePoint(original,t);
            Sample sample{t,point.p.x(),point.p.y()/sp,point.p.y()/sp,point.p.y()/sp-detourSpaces+.1,point.p.y()/sp+detourSpaces-.1};
            for(int i=0;i<original.size();++i)if(original[i].t==t) {
                  sample.original=i;
                  sample.fixed=original[i].locked || i==0 || i==original.size()-1;
                  break;
                  }
            if(!samples.isEmpty() && sample.x<=samples.back().x+1e-8) {
                  result.limited=true;return result;
                  }
            samples.append(sample);
            }
      double widest=0;
      for(int i=1;i<samples.size();++i)widest=qMax(widest,samples[i].x-samples[i-1].x);
      const double horizontalPad=widest*.55;
      auto routeAtX=[&](double x) {
            double a=0,b=1;
            for(int step=0;step<18;++step) {
                  const double mid=(a+b)/2;
                  if(curvePoint(original,mid).p.x()<x)a=mid;else b=mid;
                  }
            const double t=(a+b)/2;
            for(const auto& route:routes)if(t>=route.from && (t<route.until || route.until==1))return route.direction;
            return 0;
            };
      auto constrain=[&](Sample& sample,const QRectF& rect,int direction,bool mayReverse) {
            const double limit=(direction<0?rect.top()-.08*sp:rect.bottom()+.08*sp)/sp;
            if(sample.fixed) {
                  if(direction<0?sample.reference>limit:sample.reference<limit)result.limited=true;
                  return true;
                  }
            if(direction<0 && limit>=sample.low) {sample.high=qMin(sample.high,limit);return true;}
            if(direction>0 && limit<=sample.high) {sample.low=qMax(sample.low,limit);return true;}
            if(mayReverse) {
                  const double other=(direction<0?rect.bottom()+.08*sp:rect.top()-.08*sp)/sp;
                  if(direction<0 && other<=sample.high) {sample.low=qMax(sample.low,other);return true;}
                  if(direction>0 && other>=sample.low) {sample.high=qMin(sample.high,other);return true;}
                  }
            return false;
            };
      for(const auto& rect:guides) {
            const int direction=routeAtX(rect.center().x());if(!direction)continue;
            for(auto& sample:samples) {
                  if(sample.x<rect.left()-horizontalPad || sample.x>rect.right()+horizontalPad)continue;
                  if(!constrain(sample,rect,direction,false)) result.limited=true;
                  }
            }
      if(automatic)for(const auto& rect:obstacles)for(auto& sample:samples) {
            if(sample.x<rect.left()-horizontalPad || sample.x>rect.right()+horizontalPad)continue;
            const double y=sample.reference*sp;
            if(y<rect.top() || y>rect.bottom())continue;
            if(!constrain(sample,rect,y<rect.center().y()?-1:1,true)) result.limited=true;
            }
      for(auto& sample:samples) {
            if(sample.fixed) {
                  if(sample.reference<sample.low-1e-7 || sample.reference>sample.high+1e-7) {
                        result.limited=true;return result;
                        }
                  sample.low=sample.high=sample.reference;
                  }
            sample.value=qBound(sample.low,sample.reference,sample.high);
            }

      // Banded least squares: stay near the visible curve while reducing changes
      // of slope. Projection keeps note clearance and user-fixed points intact.
      const int count=samples.size();
      QVector<std::array<double,5>> matrix(count);
      QVector<double> rhs(count);
      for(int i=0;i<count;++i) {
            matrix[i].fill(0);
            const double weight=samples[i].original>=0?.12:.03;
            matrix[i][2]=weight;rhs[i]=weight*samples[i].reference;
            }
      const double mean=1./divisions;
      for(int i=0;i<count;++i)if(samples[i].fixed && samples[i].original>=0) {
            const int a=i?i-1:0,b=i+1<count?i+1:count-1;
            const double scale=mean/(samples[b].t-samples[a].t),weight=40;
            const double target=mean*original[samples[i].original].d.y()/sp;
            matrix[a][2]+=weight*scale*scale;matrix[b][2]+=weight*scale*scale;
            matrix[a][b-a+2]-=weight*scale*scale;matrix[b][a-b+2]-=weight*scale*scale;
            rhs[a]-=weight*scale*target;rhs[b]+=weight*scale*target;
            }
      for(int i=1;i<count-1;++i) {
            const double left=samples[i].t-samples[i-1].t,right=samples[i+1].t-samples[i].t;
            const std::array<double,3> c{{mean/left,-mean/left-mean/right,mean/right}};
            const double weight=80*2*mean/(left+right);
            for(int a=0;a<3;++a)for(int b=0;b<3;++b)
                  matrix[i+a-1][b-a+2]+=weight*c[a]*c[b];
            }
      for(int pass=0;pass<400;++pass) {
            double change=0;
            for(int step=0;step<count;++step) {
                  const int i=pass%2?count-step-1:step;
                  if(samples[i].fixed)continue;
                  double value=rhs[i];
                  for(int d=-2;d<=2;++d)if(d && i+d>=0 && i+d<count)value-=matrix[i][d+2]*samples[i+d].value;
                  value=qBound(samples[i].low,value/matrix[i][2],samples[i].high);
                  change=qMax(change,std::abs(value-samples[i].value));samples[i].value=value;
                  }
            if(change<.0002)break;
            }

      // Retain every editable point, then add only the extra points needed to
      // reproduce the smooth corridor. These layout points stay out of the file.
      QVector<int> chosen;
      for(int i=0;i<count;++i)if(samples[i].original>=0)chosen.append(i);
      for(;;) {
            std::sort(chosen.begin(),chosen.end());
            auto candidate=fit(samples,chosen,original,sp);
            double worst=.05;int index=-1;
            for(int i=1;i<count-1;++i) {
                  if(chosen.contains(i))continue;
                  const auto point=curvePoint(candidate,samples[i].t).p;
                  const double error=QLineF(point,{samples[i].x,samples[i].value*sp}).length()/sp;
                  if(error>worst) {worst=error;index=i;}
                  }
            if(index<0) {result.points=candidate;return result;}
            if(chosen.size()>=maximumNodes+2) {result.points=candidate;result.limited=true;return result;}
            chosen.append(index);
            }
      }
} }
