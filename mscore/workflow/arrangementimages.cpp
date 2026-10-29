// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mscore/plugin/api/scoreobserver.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/mscore.h"
#include "libmscore/note.h"
#include "libmscore/page.h"
#include "libmscore/segment.h"
#include "libmscore/system.h"
#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>

namespace Ms { namespace PluginAPI { namespace {
struct ImageSlice {
    Ms::System* system;
    QList<Ms::Measure*> measures;
    QRectF rect;
};
QVariantMap imageRect(const QRectF& r) {
    return {{"x",r.x()},{"y",r.y()},{"width",r.width()},{"height",r.height()}};
}
Ms::System* imageSystem(const Ms::Element* element) {
    for(auto parent=element;parent;parent=parent->parent())
        if(parent->isSystem())return const_cast<Ms::System*>(Ms::toSystem(parent));
    return nullptr;
}
// Values only; the shared budget also covers tools opened on several scores.
QCache<QString,QVariantMap>& imageCache() {
    static QCache<QString,QVariantMap> cache(16*1024*1024);return cache;
}
}}}

namespace Ms { namespace PluginAPI {
QVariantMap ScoreObserver::arrangementImage(const QVariantMap& request) {
    QElapsedTimer watch;watch.start();
    auto state=arrangementState(request);
    if(state.contains("error"))return state;
    const QString snapshot=state.value("snapshotId").toString();
    if(request.value("snapshotId").toString()!=snapshot)
        return {{"error",tr("乐谱已经更新，请重新读取这一段后取图。")}};
    if(playing())return {{"error",tr("请暂停乐谱播放后读取谱面图片。")}};
    if(_score->pages().isEmpty())return {{"error",tr("乐谱正在排版，稍后即可读取图片。")}};
    const auto scope=state.value("scope").toMap();
    const int start=scope.value("start").toInt(),end=scope.value("end").toInt();
    const int firstTrack=scope.value("firstTrack").toInt(),endTrack=scope.value("endTrack").toInt();
    const qreal targetScale=qMin(qreal(2),14/_score->spatium());
    QList<ImageSlice> slices;
    // Retain native positions. A long system is split between measures so the
    // returned images remain readable, including in continuous-score view.
    for(auto page:_score->pages())for(auto system:page->systems())for(auto base:system->measures()) {
        if(!base->isMeasure())continue;
        auto measure=Ms::toMeasure(base);
        auto first=measure->isMMRest()?measure->mmRestFirst():measure;
        auto last=measure->isMMRest()?measure->mmRestLast():measure;
        if(!first||!last||first->tick().ticks()>=end||last->endTick().ticks()<=start)continue;
        const auto box=measure->pageBoundingRect();
        if(slices.isEmpty()||slices.back().system!=system||slices.back().measures.size()>=4
           ||slices.back().rect.united(box).width()*targetScale>2200)
            slices.append({system,{},box});
        slices.back().measures.append(measure);slices.back().rect|=box;
    }
    const int index=request.value("imageIndex",0).toInt();
    if(index<0||index>=slices.size())return {{"error",tr("这个范围共有%1张谱图，请选择有效的图片序号。").arg(slices.size())}};
    const auto slice=slices[index];auto page=slice.system->page();
    if(!page)return {{"error",tr("这一行尚未完成排版，稍后即可读取。")}};
    auto elements=page->elements();
    QRectF crop(slice.rect.left(),slice.system->pageBoundingRect().top(),slice.rect.width(),slice.system->height());
    // Include the actual ink above and below the staves, such as slurs and
    // articulations. Keep the requested measure boundaries on the horizontal axis.
    QRectF ink;bool hasInk=false;
    QByteArray geometry;QDataStream stream(&geometry,QIODevice::WriteOnly);
    stream<<snapshot<<scope<<index<<slice.rect<<page->bbox();
    for(auto element:elements) {
        if(!element->visible()||imageSystem(element)!=slice.system)continue;
        if(element->isSystem()||element->isMeasure()||element->isBox()||element->isPage())continue;
        const auto box=element->pageBoundingRect();
        if(box.right()<crop.left()||box.left()>crop.right())continue;
        if(!box.isEmpty()){ink=hasInk?ink.united(box):box;hasInk=true;}
        stream<<int(element->type())<<element->pagePos()<<element->bbox();
    }
    if(hasInk){crop.setTop(ink.top());crop.setBottom(ink.bottom());}
    if(slice.measures.front()==slice.system->firstMeasure())crop.setLeft(qMin(crop.left(),slice.system->pageBoundingRect().left()));
    crop.adjust(-_score->spatium()/4,-_score->spatium(),_score->spatium()/4,_score->spatium());
    const QString geometryVersion=QString::fromLatin1(QCryptographicHash::hash(geometry,QCryptographicHash::Sha256).toHex());
    if(auto cached=imageCache().object(geometryVersion)) {
        auto result=*cached;result["cacheHit"]=true;result["milliseconds"]=watch.nsecsElapsed()/1e6;return result;
    }
    if(!std::isfinite(crop.width())||!std::isfinite(crop.height())||crop.width()<=0||crop.height()<=0)
        return {{"error",tr("这一行的谱面尺寸尚未确定，请等待排版完成。")}};
    const qreal scale=qMin(targetScale,qMin(2400/crop.width(),1800/crop.height()));
    const QSize size(qMax(1,int(std::ceil(crop.width()*scale))),qMax(1,int(std::ceil(crop.height()*scale))));
    QImage image(size,QImage::Format_ARGB32_Premultiplied);if(image.isNull())return {{"error",tr("图片未能建立，请缩小读取范围。")}};
    image.fill(Qt::white);
    struct PrintScope {
        Ms::Score* score;bool before;
        PrintScope(Ms::Score* value):score(value),before(value->printing()){score->setPrinting(true);}
        ~PrintScope(){score->setPrinting(before);}
    } printing(_score);
    QScopedValueRollback<qreal> pixels(MScore::pixelRatio,1/scale);
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(scale,scale);painter.translate(-crop.topLeft());
    std::stable_sort(elements.begin(),elements.end(),Ms::elementLessThan);
    for(auto element:elements)if(element->visible()&&element->pageBoundingRect().intersects(crop)) {
        painter.save();painter.translate(element->pagePos());element->draw(&painter);painter.restore();
    }
    painter.end();QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"PNG"))return {{"error",tr("谱图编码未完成，请重试。")}};
    QVariantList measures,notes;
    auto pixelsFor=[&](QRectF box){box.translate(-crop.topLeft());return imageRect(QRectF(box.topLeft()*scale,box.size()*scale));};
    auto chordNotes=[&](Ms::Chord* chord,int at,int grace){
        int noteIndex=0;for(auto note:chord->notes()) {
            QString id=QString("%1:%2:%3").arg(at).arg(chord->track()).arg(noteIndex++);
            if(grace>=0)id+=QString(":g%1").arg(grace);
            notes.append(QVariantMap{{"ref",QVariantMap{{"snapshotId",snapshot},{"id",id}}},
                {"tick",at},{"track",chord->track()},{"graceIndex",grace},{"visible",note->visible()},
                {"pixelRect",pixelsFor(note->pageBoundingRect())}});
        }
    };
    for(auto measure:slice.measures) {
        auto first=measure->isMMRest()?measure->mmRestFirst():measure;
        auto last=measure->isMMRest()?measure->mmRestLast():measure;
        measures.append(QVariantMap{{"firstMeasure",first->no()+1},{"lastMeasure",last->no()+1},
            {"startTick",first->tick().ticks()},{"endTick",last->endTick().ticks()},{"pixelRect",pixelsFor(measure->pageBoundingRect())}});
        for(auto segment=measure->first(SegmentType::ChordRest);segment;segment=segment->next(SegmentType::ChordRest)) {
            const int at=segment->tick().ticks();if(at<start||at>=end)continue;
            for(int track=firstTrack;track<endTrack;++track) {
                auto element=segment->element(track);if(!element||!element->isChord())continue;
                auto chord=Ms::toChord(element);chordNotes(chord,at,-1);
                for(int g=0;g<int(chord->graceNotes().size());++g)chordNotes(chord->graceNotes()[g],at,g);
            }
        }
    }
    QVariantMap result{{"schemaVersion","score-image/1"},{"snapshotId",snapshot},{"geometryVersion",geometryVersion},
        {"identity",state.value("identity")},{"scope",scope},{"imageIndex",index},{"imageCount",slices.size()},
        {"nextImage",index+1<slices.size()?QVariant(index+1):QVariant()},
        {"page",page->no()+1},{"width",size.width()},{"height",size.height()},{"scale",scale},
        {"pageRect",imageRect(crop)},{"measures",measures},{"notes",notes},
        {"mimeType","image/png"},{"pngBase64",QString::fromLatin1(png.toBase64())},
        {"cacheHit",false},{"milliseconds",watch.nsecsElapsed()/1e6}};
    const int cost=QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).size()*2;
    imageCache().insert(geometryVersion,new QVariantMap(result),cost);return result;
}
}}
