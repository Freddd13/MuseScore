// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/tempotext.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
namespace Ms { namespace ExpressionTempo {
inline QString key(){return QStringLiteral("kumo.workflow.expression-tempo");}
inline QMap<int,double> read(const Score* score){
    QMap<int,double> result;if(!score)return result;
    const auto root=QJsonDocument::fromJson(score->masterScore()->metaTag(key()).toUtf8()).object();
    if(root.value("version").toInt()!=1)return result;
    const auto points=root.value("points").toObject();
    for(auto it=points.begin();it!=points.end();++it){bool valid=false;const int tick=it.key().toInt(&valid);const double bpm=it.value().toDouble();if(valid&&tick>=0&&std::isfinite(bpm)&&bpm>=5&&bpm<=999)result.insert(tick,bpm);}
    return result;
}
inline bool owns(TempoText* text,const QMap<int,double>& saved){
    return text&&!text->visible()&&!text->isRelative()&&!text->restoreMode()&&saved.contains(text->tick().ticks())&&std::abs(saved.value(text->tick().ticks())-text->tempo()*60)<.001;
}
inline bool owns(const Score* score,int tick,const QMap<int,double>& saved){
    if(!saved.contains(tick))return false;auto segment=score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);if(!segment)return false;
    for(auto element:segment->annotations())if(element->isTempoText()&&owns(toTempoText(element),saved))return true;
    return false;
}
inline QString updated(const Score* score,const QMap<int,double>& changes){
    QJsonObject points;const auto previous=read(score);
    for(auto it=previous.begin();it!=previous.end();++it)if(owns(score,it.key(),previous))points.insert(QString::number(it.key()),it.value());
    for(auto it=changes.begin();it!=changes.end();++it)points.insert(QString::number(it.key()),it.value());
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"version",1},{"points",points}}).toJson(QJsonDocument::Compact));
}
} }
