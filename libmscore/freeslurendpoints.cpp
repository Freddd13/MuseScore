// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "slur.h"
#include "score.h"
#include "system.h"
#include "measure.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <cmath>

namespace Ms { namespace FreeSlur {
bool decodeEndpoints(const QString& data,std::array<FreeSlurEndpoint,2>* result)
      {
      if(data.size()>2048)return false;
      QJsonParseError error;
      const auto document=QJsonDocument::fromJson(data.toUtf8(),&error);
      if(error.error!=QJsonParseError::NoError || !document.isObject())return false;
      std::array<FreeSlurEndpoint,2> endpoints;
      for(int i=0;i<2;++i) {
            const QString name=i?"end":"start";
            const auto value=document.object().value(name);
            if(value.isUndefined())continue;
            if(!value.isObject())return false;
            const auto object=value.toObject();
            for(bool tangent:{false,true}) {
                  const QString key=tangent?"tangent":"position";
                  if(!object.contains(key))continue;
                  const auto pair=object.value(key).toArray();
                  if(pair.size()!=2)return false;
                  for(auto coordinate:pair)if(!coordinate.isDouble() || !std::isfinite(coordinate.toDouble())
                        || std::abs(coordinate.toDouble())>(tangent?400:100))return false;
                  const QPointF point(pair[0].toDouble(),pair[1].toDouble());
                  if(tangent) {endpoints[i].tangent=point;endpoints[i].manualTangent=true;}
                  else {endpoints[i].position=point;endpoints[i].manualPosition=true;}
                  }
            }
      *result=endpoints;return true;
      }
QString encodeEndpoints(const std::array<FreeSlurEndpoint,2>& endpoints)
      {
      QJsonObject result;
      for(int i=0;i<2;++i) {
            const auto& endpoint=endpoints[i];QJsonObject object;
            if(endpoint.manualPosition)object.insert("position",QJsonArray{endpoint.position.x(),endpoint.position.y()});
            if(endpoint.manualTangent)object.insert("tangent",QJsonArray{endpoint.tangent.x(),endpoint.tangent.y()});
            if(!object.isEmpty())result.insert(i?"end":"start",object);
            }
      return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
      }
} // namespace FreeSlur

SlurPos Slur::freeEndpointPositions(bool up)
      {
      QScopedValueRollback<bool> direction(_up,up);
      SlurPos positions{};slurPos(&positions);return positions;
      }
QPointF SlurSegment::freeAutoEndpoint(bool start) const
      {
      const auto current=slur()->freeEndpointPositions(slur()->up());
      const int side=freeEndpointSide(start);
      const auto desired=side?slur()->freeEndpointPositions(side<0):current;
      return start?ups(Grip::START).p+desired.p1-current.p1:ups(Grip::END).p+desired.p2-current.p2;
      }
std::array<QPointF,4> SlurSegment::freeDrawingControls(bool adjustments) const
      {
      auto controls=freeBaseControls();
      for(bool start:{true,false}) {
            if(start?!isSingleBeginType():!isSingleEndType())continue;
            const auto& edit=slur()->freeEndpoints()[start?0:1];
            const auto grip=start?Grip::START:Grip::END;
            const int endpoint=start?0:3,handle=start?1:2;
            // Existing manually placed endpoints keep their saved appearance.
            // New endpoint edits are independent of the saved interior nodes.
            if(edit.manualPosition || (freeEndpointSide(start) && ups(grip).off.isNull())) {
                  const auto position=freeAutoEndpoint(start)+(edit.manualPosition?edit.position*spatium():QPointF());
                  controls[handle]+=position-controls[endpoint];controls[endpoint]=position;
                  }
            if(adjustments && edit.manualTangent)controls[handle]+=(start?1.:-1.)*edit.tangent*spatium()/3;
            }
      return controls;
      }
bool SlurSegment::freeEndpointGrip(Grip grip) const
      {
      if(!slur()->freeMode() || _freeCurvePoints.size()<2)return false;
      if(grip==Grip::START || grip==Grip::BEZIER1)return isSingleBeginType();
      if(grip==Grip::END || grip==Grip::BEZIER2)return isSingleEndType();
      return false;
      }
double SlurSegment::freeGripScale(bool start) const
      {
      if(!system())return 1./3;
      const int first=qMax(slur()->tick().ticks(),system()->firstMeasure()->tick().ticks());
      const int last=qMin(slur()->tick2().ticks(),system()->lastMeasure()->endTick().ticks());
      double nearest=start?1:0;
      if(last>first)for(const auto& node:slur()->freeNodes()) {
            const double tick=slur()->tick().ticks()+node.position*slur()->ticks().ticks();
            if(tick<=first || tick>=last)continue;
            const double t=(tick-first)/(last-first);
            nearest=start?qMin(nearest,t):qMax(nearest,t);
            }
      return (start?nearest:1-nearest)/3;
      }
} // namespace Ms
