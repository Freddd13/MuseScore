// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "regionalcomparison.h"
#include "libmscore/chord.h"
#include "libmscore/beam.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/regionalvariants.h"
#include "libmscore/system.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "referencefactory.h"
#include "referencebus.h"
#include "playbackpreview.h"
#include <QWidget>
#include <QApplication>
#include <QPointer>
#include <QPainter>
#include <QEvent>
#include <QSettings>
#include <QSet>
#include <QMap>
#include <QHash>
namespace Ms {
namespace {
double tickX(Score* score,int tick){
      auto measure=score->tick2measure(Fraction::fromTicks(tick));if(!measure)return 0;
      double x=measure->canvasPos().x(),until=x+measure->width();int left=measure->tick().ticks(),right=measure->endTick().ticks();
      for(auto seg=measure->first(SegmentType::ChordRest);seg;seg=seg->next(SegmentType::ChordRest)){
            const int at=seg->tick().ticks();if(at==tick)return seg->canvasPos().x();if(at>tick){until=seg->canvasPos().x();right=at;break;}x=seg->canvasPos().x();left=at;
            }
      return x+(until-x)*double(tick-left)/qMax(1,right-left);
      }
class DifferenceLayer final:public QWidget {
      ScoreView* _view;QPointer<Score> _source,_other;ScoreContentState _state;
      struct Item{Element* element;int tick,track;};QVector<Item> _items;QVector<QPair<int,int>> _current;QMap<int,QVector<int>> _itemTicks,_currentTicks;
      QHash<quint64,QPointF> _paintOffsets;QTransform _matrix;
      QPointF shift(int tick,int track){
            const quint64 key=(quint64(quint32(tick))<<32)|quint32(track);const auto known=_paintOffsets.constFind(key);if(known!=_paintOffsets.cend())return known.value();
            auto target=_source->tick2measure(Fraction::fromTicks(tick)),origin=_other->tick2measure(Fraction::fromTicks(tick));
            if(!target||!origin||!target->system()||!origin->system())return {};
            auto segment=_other->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);auto e=segment?segment->element(track):nullptr;
            const int visual=e&&e->isChordRest()?toChordRest(e)->vStaffIdx():track/VOICES;
            const double y=target->system()->canvasPos().y()+target->system()->staffYpage(visual)-target->system()->pagePos().y();
            const double oldY=origin->system()->canvasPos().y()+origin->system()->staffYpage(visual)-origin->system()->pagePos().y();
            const QPointF result(tickX(_source,tick)-tickX(_other,tick),y-oldY);_paintOffsets.insert(key,result);return result;
            }
      void drawBeam(QPainter& painter,Beam* beam,int track){
            struct Anchor{double oldX,newX;double dy;};QVector<Anchor> anchors;
            for(auto cr:beam->elements()){auto d=shift(cr->tick().ticks(),track);const double oldX=cr->segment()->canvasPos().x();anchors.append({oldX,oldX+d.x(),d.y()});}
            if(anchors.isEmpty())return;
            // Each span follows the same onset positions as its notes. Reflow and
            // changed rhythmic spacing therefore keep the ghost stems attached.
            auto span=[&](double from,double until,const Anchor& a,const Anchor& b){
                  if(until<=from)return;const double scale=qAbs(b.oldX-a.oldX)<.001?1.:(b.newX-a.newX)/(b.oldX-a.oldX);if(scale<=0)return;
                  painter.save();painter.translate(a.newX,a.dy);painter.scale(scale,1);painter.translate(-a.oldX,0);
                  painter.setClipRect(QRectF(from,-100000,until-from,200000),Qt::IntersectClip);painter.translate(beam->canvasPos());beam->draw(&painter);painter.restore();
                  };
            const auto first=anchors.front(),last=anchors.back();span(first.oldX-10000,first.oldX,first,first);
            for(int i=1;i<anchors.size();++i)span(anchors[i-1].oldX,anchors[i].oldX,anchors[i-1],anchors[i]);
            span(last.oldX,last.oldX+10000,last,last);
            }
      bool eventFilter(QObject* object,QEvent* event)override{if(object==_view&&(event->type()==QEvent::Paint||event->type()==QEvent::Resize)){if(geometry()!=_view->rect())setGeometry(_view->rect());if(_matrix!=_view->matrix()){_matrix=_view->matrix();update();}}return false;}
      void paintEvent(QPaintEvent*)override{
            if(!_source||!_other||_view->score()!=_source||_source->masterScore()->state()!=_state){hide();return;}if(_source->printing()||_view->fotoMode())return;
            QVector<int> items,current;_paintOffsets.clear();const auto visible=_view->toLogical(QRectF(rect()));
            for(auto system:_source->systems()){
                  if(!system->canvasBoundingRect().adjusted(-12*_source->spatium(),-12*_source->spatium(),12*_source->spatium(),12*_source->spatium()).intersects(visible))continue;
                  auto first=system->firstMeasure(),last=system->lastMeasure();if(!first||!last)continue;const int from=first->tick().ticks(),until=last->endTick().ticks();
                  for(auto i=_itemTicks.lowerBound(from);i!=_itemTicks.end()&&i.key()<until;++i)items+=i.value();
                  for(auto i=_currentTicks.lowerBound(from);i!=_currentTicks.end()&&i.key()<until;++i)current+=i.value();
                  }
            QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setWorldTransform(_view->matrix());
            const auto accent=QApplication::palette().color(QPalette::Active,QPalette::Highlight);p.setPen(QPen(accent,0));p.setBrush(Qt::NoBrush);
            for(int index:current){const auto& item=_current[index];auto segment=_source->tick2segment(Fraction::fromTicks(item.first),false,SegmentType::ChordRest);auto e=segment&&segment->tick().ticks()==item.first?segment->element(item.second):nullptr;if(!e)continue;const auto box=e->canvasBoundingRect();if(!_view->matrix().mapRect(box).intersects(rect()))continue;p.drawRoundedRect(box.adjusted(-1,-1,1,1),1,1);}
            p.setOpacity(.4);
            for(int index:items){const auto& item=_items[index];auto e=item.element;if(e->isBeam()){drawBeam(p,toBeam(e),item.track);continue;}const auto point=e->canvasPos()+shift(item.tick,item.track);if(!_view->matrix().mapRect(e->bbox().translated(point)).intersects(rect()))continue;p.save();p.translate(point);if(e->isNote())toNote(e)->draw(&p,accent);else e->draw(&p);p.restore();}
            }
public:
      explicit DifferenceLayer(ScoreView* view):QWidget(view),_view(view){setObjectName("regionalDifferenceLayer");setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);view->installEventFilter(this);hide();}
      void display(Score* other,const QVariantList& changes){
            _source=_view->score();_other=other;_items.clear();_current.clear();_itemTicks.clear();_currentTicks.clear();if(!other||!_source){hide();return;}_state=_source->masterScore()->state();QSet<Element*> seen;const auto accent=QApplication::palette().color(QPalette::Active,QPalette::Highlight);
            for(auto value:changes){const auto row=value.toMap();const int tick=row.value("tick").toInt(),track=row.value("track").toInt();
                  auto target=_source->tick2measure(Fraction::fromTicks(tick));auto origin=other->tick2measure(Fraction::fromTicks(tick));if(!target||!origin||!target->system()||!origin->system())continue;
                  auto a=_source->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest),b=other->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);
                  auto current=a&&a->tick().ticks()==tick?a->element(track):nullptr,alternative=b&&b->tick().ticks()==tick?b->element(track):nullptr;
                  if(current)_current.append({tick,track});
                  if(!alternative)continue;
                  QList<Element*> elements;alternative->scanElements(&elements,[](void* list,Element* element){static_cast<QList<Element*>*>(list)->append(element);},false);
                  if(alternative->isChord()&&toChord(alternative)->beam())elements.append(toChord(alternative)->beam());
                  for(auto e:elements)if(e->visible()&&!seen.contains(e)){seen.insert(e);e->setSelected(false);e->setColor(accent);_items.append({e,tick,track});}
                  }
            for(int i=0;i<_items.size();++i)_itemTicks[_items[i].tick].append(i);for(int i=0;i<_current.size();++i)_currentTicks[_current[i].first].append(i);
            setGeometry(_view->rect());setProperty("differenceCount",changes.size());show();raise();update();
            }
};
}
void showRegionalDifference(ScoreView* view,Score* other,const QVariantList& changes){if(!view)return;DifferenceLayer* layer=nullptr;for(auto child:view->children())if((layer=dynamic_cast<DifferenceLayer*>(child)))break;if(!layer&&other)layer=new DifferenceLayer(view);if(layer)layer->display(other,changes);}
bool regionalAuditionEnabled(){return QSettings().value("regionalVariants/auditionCompare",false).toBool();}
void setRegionalAuditionEnabled(bool value){QSettings().setValue("regionalVariants/auditionCompare",value);}
QVariantMap switchRegionalWriting(ScoreView* view,const QString& group,const QString& variant){
      if(!view||!view->score()||!view->score()->isMaster())return {{"error",QObject::tr("请回到可编辑的总谱。")}};
      if (mscore->currentScoreView() != view) mscore->setCurrentScoreView(view);
      for (auto value : RegionalVariants::state(view->score()).value("groups").toList()) {
            const auto g=value.toMap();
            if (g.value("id").toString()==group && g.value("active").toString()==variant)
                  return {{"ok",true},{"unchanged",true}};
      }
      stopPlaybackPreview(true);auto media=referenceProject();if(media->property("playing").toBool())QMetaObject::invokeMethod(media,"stop");
      if(seq&&seq->isPlaying())seq->stopWait();if(seq&&!seq->backgroundRenderingIdle())return {{"error",QObject::tr("声音正在准备，请稍后切换。")}};
      setReferenceClock({});setReferenceEndTick(-1);auto score=view->score();auto result=RegionalVariants::select(score,group,variant);if(result.contains("error"))return result;mscore->endCmd();view->update();
      if(regionalAuditionEnabled())for(auto value:RegionalVariants::state(score).value("groups").toList()){
            auto g=value.toMap();if(g.value("id").toString()!=group)continue;
            auto observer=view->findChild<PluginAPI::ScoreObserver*>("regionalAuditionObserver");if(!observer){observer=new PluginAPI::ScoreObserver(view);observer->setObjectName("regionalAuditionObserver");}
            PluginAPI::Score wrapped(score);observer->setScore(&wrapped);const auto play=observer->navigateWorkflow({{"start",g.value("start")},{"end",g.value("end")},{"firstTrack",g.value("tracks").toList().front()},{"occurrence",1}},true);
            if(play.contains("error"))result["playbackError"]=play.value("error");break;
            }
      return result;
      }
}
