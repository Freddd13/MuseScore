// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "positionhint.h"
#include "mscore/scoreview.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/score.h"
#include "libmscore/system.h"
#include <QElapsedTimer>
#include <QEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>

namespace Ms {
namespace {
class PositionHint final : public QWidget {
      ScoreView* _view;
      QPointer<Score> _score;
      ScoreContentState _state;
      QVector<QRectF> _boxes;
      QString _text;
      QElapsedTimer _elapsed;
      QTimer _timer;
      bool eventFilter(QObject* object, QEvent* event) override
            {
            if (object == _view && event->type() == QEvent::Resize) setGeometry(_view->rect());
            return false;
            }
      void paintEvent(QPaintEvent*) override
            {
            if (!_score || _view->score() != _score || _score->masterScore()->state() != _state) {
                  hide(); _timer.stop(); return;
                  }
            if (_score->printing() || _view->fotoMode()) return;
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setOpacity(qBound(0., (3000. - _elapsed.elapsed()) / 400., 1.));
            const auto color = palette().color(QPalette::Highlight);
            auto fill = color; fill.setAlpha(20);
            painter.setPen(QPen(color, 1.5)); painter.setBrush(fill);
            QRectF first;
            for (const auto& box : _boxes) {
                  const auto screen = _view->matrix().mapRect(box).adjusted(-3,-3,3,3);
                  if (!screen.intersects(rect())) continue;
                  painter.drawRoundedRect(screen, 4, 4);
                  if (first.isEmpty()) first = screen;
                  }
            if (first.isEmpty()) return;
            const auto size = painter.fontMetrics().size(Qt::TextSingleLine, _text) + QSize(16,10);
            const QVector<QPointF> candidates{{first.left(),first.top()-size.height()-8},{first.right()+8,first.top()},{first.left(),first.bottom()+8},{first.left()-size.width()-8,first.top()},{3.,3.},{qreal(width()-size.width()-3),3.}};
            QRect label;double best=1e30;
            for(auto at:candidates){QRect candidate(QPoint(qBound(3,qRound(at.x()),qMax(3,width()-size.width()-3)),qBound(3,qRound(at.y()),qMax(3,height()-size.height()-3))),size);double cost=QLineF(candidate.center(),first.center()).length();for(const auto& box:_boxes){const auto overlap=QRectF(candidate).intersected(_view->matrix().mapRect(box).adjusted(-4,-6,4,6));cost+=overlap.width()*overlap.height()*1000;}if(cost<best){best=cost;label=candidate;}}
            painter.setBrush(color); painter.setPen(Qt::NoPen); painter.drawRoundedRect(label,4,4);
            painter.setPen(palette().color(QPalette::HighlightedText)); painter.drawText(label,Qt::AlignCenter,_text);
            }
public:
      explicit PositionHint(ScoreView* view) : QWidget(view), _view(view)
            {
            setObjectName("workflowPositionHint");
            setAttribute(Qt::WA_TransparentForMouseEvents);
            setAttribute(Qt::WA_NoSystemBackground);
            view->installEventFilter(this);
            _timer.setInterval(50);
            connect(&_timer,&QTimer::timeout,this,[this] {
                  if (_elapsed.elapsed() >= 3000 || !_score || !_view->isVisible()) {hide();_timer.stop();}
                  else if (_elapsed.elapsed() >= 2600) update();
                  });
            hide();
            }
      void display(const QVariantMap& range)
            {
            _score = _view->score(); if (!_score || !_score->lastMeasure()) return;
            const int from = qMax(0,range.value("start").toInt());
            auto first = _score->tick2measure(Fraction::fromTicks(from)); if (!first) return;
            const int until = range.value("end",first->endTick().ticks()).toInt();
            const int firstStaff = qBound(0,range.value("firstTrack",0).toInt()/VOICES,_score->nstaves()-1);
            const int lastStaff = qBound(firstStaff,(range.value("endTrack",(firstStaff+1)*VOICES).toInt()-1)/VOICES,_score->nstaves()-1);
            _boxes.clear(); System* previous = nullptr; int lastNumber = first->no();
            for (auto measure=first; measure && measure->tick().ticks()<qMax(from+1,until); measure=measure->nextMeasure()) {
                  auto system=measure->system(); if (!system) continue;
                  QRectF box=measure->canvasBoundingRect();
                  auto upper=system->staff(firstStaff), lower=system->staff(lastStaff);
                  box.setTop(system->canvasPos().y()+upper->bbox().top());
                  box.setBottom(system->canvasPos().y()+lower->bbox().bottom());
                  if (box.height()<1) box=measure->canvasBoundingRect();
                  if (previous==system && !_boxes.isEmpty()) _boxes.last()=_boxes.last().united(box);
                  else _boxes.append(box);
                  previous=system;lastNumber=measure->no();
                  }
            _text = lastNumber==first->no() ? tr("第 %1 小节").arg(first->no()+1)
                    : tr("第 %1–%2 小节").arg(first->no()+1).arg(lastNumber+1);
            _state=_score->masterScore()->state();_elapsed.start();setGeometry(_view->rect());show();raise();_timer.start();update();
            }
      };
}

void showWorkflowPositionHint(ScoreView* view,const QVariantMap& range)
      {
      if (!view) return;
      PositionHint* hint=nullptr;
      for (auto child:view->children()) if ((hint=dynamic_cast<PositionHint*>(child))) break;
      if (!hint) hint=new PositionHint(view);
      hint->display(range);
      }

namespace PluginAPI {
QVariantMap ScoreObserver::musicalPosition(int tick) const
      {
      if(!_score||!_score->lastMeasure())return {};
      const int end=_score->lastMeasure()->endTick().ticks();auto measure=_score->tick2measure(Fraction::fromTicks(qBound(0,tick,qMax(0,end-1))));if(!measure)return {};
      const qreal beat=1.+(tick-measure->tick().ticks())/(480.*4/qMax(1,measure->timesig().denominator()));
      const QString text=tick>=end?tr("乐谱结尾"):tr("第 %1 小节 · 第 %2 拍").arg(measure->no()+1).arg(QString::number(beat,'g',4));
      return {{"tick",tick},{"measure",measure->no()+1},{"beat",beat},{"text",text}};
      }
QVariantMap ScoreObserver::scoreLocation(int tick,int track) const
      {
      auto view=mscore?mscore->currentScoreView():nullptr;
      if (!_score || !view || view->score()!=_score || track<0 || track>=_score->ntracks()) return {};
      auto segment=_score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);
      auto element=segment?segment->element(track):nullptr;
      if (!element && segment) for (int t=track;t<_score->ntracks() && !element;++t) element=segment->element(t);
      if (!element) return {};
      const auto local=view->matrix().mapRect(element->canvasBoundingRect()).toAlignedRect();
      const QRect global(view->mapToGlobal(local.topLeft()),local.size());
      auto screen=QGuiApplication::screenAt(global.center());const auto area=screen?screen->availableGeometry():QRect();
      auto measure=element->findMeasure();auto measureBounds=measure->canvasBoundingRect();
      if(auto system=measure->system()){auto first=system->staff(0),last=system->staff(_score->nstaves()-1);measureBounds.setTop(system->canvasPos().y()+first->bbox().top());measureBounds.setBottom(system->canvasPos().y()+last->bbox().bottom());}
      const auto localMeasure=view->matrix().mapRect(measureBounds).toAlignedRect();
      const QRect measureBox(view->mapToGlobal(localMeasure.topLeft()),localMeasure.size());
      const QRect viewport(view->mapToGlobal(QPoint()),view->size());
      return {{"measureX",measureBox.x()},{"measureY",measureBox.y()},{"measureWidth",measureBox.width()},{"measureHeight",measureBox.height()},
              {"viewX",viewport.x()},{"viewY",viewport.y()},{"viewWidth",viewport.width()},{"viewHeight",viewport.height()},{"x",global.x()},{"y",global.y()},{"width",global.width()},{"height",global.height()},
              {"screenX",area.x()},{"screenY",area.y()},{"screenWidth",area.width()},{"screenHeight",area.height()}};
      }
}
}
