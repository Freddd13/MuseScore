// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/scoreview.h"
#include "mscore/musescore.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/system.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QtMath>

namespace Ms { namespace {
qreal chordTickX(Measure* measure,int tick)
      {
      qreal x=0,right=measure->width();int from=measure->tick().ticks(),until=measure->endTick().ticks();
      for(auto segment=measure->first(SegmentType::ChordRest);segment;segment=segment->next(SegmentType::ChordRest)) {
            if(!segment->enabled())continue;const int at=segment->tick().ticks();
            if(at==tick)return segment->canvasPos().x();
            if(at>tick){right=segment->x();until=at;break;}x=segment->x();from=at;
            }
      return measure->canvasPos().x()+x+(right-x)*qBound(0.,qreal(tick-from)/qMax(1,until-from),1.);
      }
class ChordEditor final : public QWidget {
      QPointer<PluginAPI::ScoreObserver> observer;
      QPointer<Score> source;
      ScoreView* view;
      ScoreContentState state;
      int start=0,end=0,track=0,gesture=0;
      qreal offsetX=0,offsetY=0;
      QPoint origin,delta;
      QRect body;
      QPointF handles[2];
      bool valid() const {return observer&&source&&source==view->score()&&source->masterScore()->state()==state&&!observer->playing()&&!source->printing()&&!view->fotoMode();}
      QPointF pointFor(int tick) const {
            auto measure=source->tick2measure(Fraction::fromTicks(qMin(tick==end?qMax(0,tick-1):tick,source->lastMeasure()->endTick().ticks()-1)));if(!measure||!measure->system())return {};
            auto system=measure->system();const qreal sp=source->spatium();
            auto anchor=view->matrix().map(QPointF(chordTickX(measure,tick),system->canvasPos().y()+system->staffYpage(track/VOICES)-system->pagePos().y()-sp*2.5));
            auto marker=view->notePreviewMarkerBox(observer,start,track);
            if(!marker.isEmpty() && tick>=measure->tick().ticks() && source->tick2measure(Fraction::fromTicks(start))->system()==system)anchor.setY(view->matrix().mapRect(marker).bottom()+10);
            return anchor;
            }
      int tickFor(QPoint position,int fallback) const {
            auto canvas=view->matrix().inverted().map(QPointF(position));int staff=track/VOICES;Segment* segment=nullptr;
            auto measure=source->pos2measure(canvas,&staff,nullptr,&segment,nullptr);
            if(!measure)measure=source->tick2measure(Fraction::fromTicks(qMin(fallback,source->lastMeasure()->endTick().ticks()-1)));
            if(!measure)return fallback;
            int lo=measure->tick().ticks(),hi=measure->endTick().ticks();
            while(lo<hi){int mid=lo+(hi-lo)/2;if(chordTickX(measure,mid)<canvas.x())lo=mid+1;else hi=mid;}
            return qBound(0,qRound(lo/60.)*60,source->lastMeasure()->endTick().ticks());
            }
      void position() {
            if(!valid()){hide();return;}
            const auto previousBody=body;const auto previousStart=handles[0],previousEnd=handles[1];
            const bool geometryChanged=geometry()!=view->rect();if(geometryChanged)setGeometry(view->rect());
            body=view->matrix().mapRect(view->notePreviewMarkerBox(observer,start,track)).toAlignedRect();
            if(body.isEmpty()){hide();return;}
            body.adjust(-3,-3,3,3);handles[0]=pointFor(start);handles[1]=pointFor(end);
            QRegion region(body.adjusted(-2,-2,2,2));for(auto point:handles)region|=QRegion(QRect(qRound(point.x())-9,qRound(point.y())-10,18,20));
            if(mask()!=region)setMask(region);
            const bool changed=geometryChanged||body!=previousBody||handles[0]!=previousStart||handles[1]!=previousEnd;
            if(changed){setProperty("rangeStartHandle",handles[0]);setProperty("rangeEndHandle",handles[1]);}
            if(!isVisible()){show();raise();update();}else if(changed)update();
            }
      bool eventFilter(QObject* object,QEvent* event) override {
            if(object==view && (event->type()==QEvent::Paint||event->type()==QEvent::Resize))position();
            return false;
            }
      void paintEvent(QPaintEvent*) override {
            if(!valid())return;QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);auto accent=palette().color(QPalette::Highlight);
            painter.setPen(QPen(accent,1,Qt::DashLine));painter.setBrush(Qt::NoBrush);painter.drawRoundedRect(body.translated(gesture==1?delta:QPoint()),3,3);
            painter.setPen(QPen(accent,1.5));auto fill=palette().color(QPalette::Base);painter.setBrush(fill);
            for(int i=0;i<2;++i){auto point=handles[i];if(gesture==i+2)point+=delta;painter.drawRoundedRect(QRectF(point-QPointF(5,8),QSizeF(10,16)),3,3);painter.drawLine(point-QPointF(0,4),point+QPointF(0,4));}
            }
      void mousePressEvent(QMouseEvent* event) override {
            if(event->button()!=Qt::LeftButton || !valid()){event->ignore();return;}
            gesture=body.contains(event->pos())?1:0;for(int i=0;i<2;++i)if(QRectF(handles[i]-QPointF(9,10),QSizeF(18,20)).contains(event->pos()))gesture=i+2;
            if(!gesture){event->ignore();return;}origin=event->pos();delta={};setFocus(Qt::MouseFocusReason);grabMouse();event->accept();
            }
      void mouseMoveEvent(QMouseEvent* event) override {
            if(!gesture){bool handle=false;for(auto point:handles)if(QRectF(point-QPointF(9,10),QSizeF(18,20)).contains(event->pos()))handle=true;setCursor(handle?Qt::SizeHorCursor:Qt::SizeAllCursor);event->ignore();return;}delta=event->pos()-origin;update();event->accept();
            }
      void mouseReleaseEvent(QMouseEvent* event) override {
            if(!gesture){event->ignore();return;}const int mode=gesture;delta=event->pos()-origin;gesture=0;releaseMouse();view->setFocus();
            if(valid() && delta.manhattanLength()>=QApplication::startDragDistance()) {
                  if(mode==1){const qreal unit=qMax(.01,source->spatium()*view->matrix().m11());emit observer->previewPositionEdited(start,track,offsetX+delta.x()/unit,offsetY+delta.y()/unit);}
                  else {const int at=tickFor(event->pos(),mode==2?start:end);if(mode==2&&at<end)emit observer->previewRangeEdited(at,end,track);else if(mode==3&&at>start)emit observer->previewRangeEdited(start,at,track);}
                  }
            delta={};position();event->accept();
            }
      void mouseDoubleClickEvent(QMouseEvent* event) override {if(valid())observer->activatePreview(start,track);event->accept();}
      void keyPressEvent(QKeyEvent* event) override {if(event->key()==Qt::Key_Escape){gesture=0;delta={};releaseMouse();view->setFocus();position();event->accept();}else QWidget::keyPressEvent(event);}
public:
      ChordEditor(ScoreView* target,PluginAPI::ScoreObserver* owner):QWidget(target),observer(owner),view(target) {
            setObjectName("harmonyScoreRangeEditor");setAttribute(Qt::WA_NoSystemBackground);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);setToolTip(tr("拖记号调整位置，拖两端手柄修改作用范围。"));view->installEventFilter(this);hide();connect(owner,&QObject::destroyed,this,&QObject::deleteLater);
            connect(owner,&PluginAPI::ScoreObserver::positionChanged,this,[this]{if(observer->playing())hide();});
            }
      bool matches(ScoreView* target)const{return target==view;}
      void configure(Score* score,const QVariantMap& region) {
            if(region.isEmpty()||!score){source=nullptr;hide();return;}
            source=score;state=score->masterScore()->state();start=region.value("start").toInt();end=region.value("end").toInt();track=qBound(0,region.value("track").toInt(),score->ntracks()-1);
            offsetX=region.value("offsetX").toDouble();offsetY=region.value("offsetY").toDouble();position();
            }
      };
}
namespace PluginAPI {
void ScoreObserver::setChordEditRegion(const QVariantMap& region)
      {
      auto editor=dynamic_cast<ChordEditor*>(_chordEditor.data());
      if(region.isEmpty()||!_score||!enabled()||!surfaceVisible()){if(editor)editor->configure(nullptr,{});return;}
      auto view=mscore?mscore->currentScoreView():nullptr;if(!view||view->score()!=_score)return;
      if(editor&&!editor->matches(view)){editor->deleteLater();editor=nullptr;}
      if(!editor){editor=new ChordEditor(view,this);_chordEditor=editor;}
      editor->configure(_score,region);
      }
}}
