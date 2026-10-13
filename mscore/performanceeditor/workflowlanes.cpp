// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "workflowlanes.h"
#include "performanceeditor.h"
#include "../workflow/workflowcontext.h"
#include "../workflow/referencefactory.h"
#include "../scoreview.h"
#include "../shortcut.h"
#include "../musescore.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QTimer>
#include <QVBoxLayout>
#include <QToolButton>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QComboBox>
#include <QMenu>
#include <QLabel>
#include <algorithm>
#include <cmath>
namespace Ms {
namespace {
double audioAt(const QVariantMap& a,double tick){auto points=a.value("anchors").toList();points.prepend(QVariantMap{{"tick",a.value("start")},{"ms",a.value("from")}});points.append(QVariantMap{{"tick",a.value("end")},{"ms",a.value("until")}});for(int i=1;i<points.size();++i){auto p=points[i-1].toMap(),q=points[i].toMap();if(tick<=q.value("tick").toDouble())return p.value("ms").toDouble()+(tick-p.value("tick").toDouble())*(q.value("ms").toDouble()-p.value("ms").toDouble())/qMax(1.,q.value("tick").toDouble()-p.value("tick").toDouble());}return a.value("until").toDouble();}
}
WorkflowLanes::WorkflowLanes(PerformanceEditor* editor):QWidget(editor),_editor(editor){
      setObjectName("performanceWorkflowLanes");setMouseTracking(true);setToolTip(tr("上排是谱面起音，下排是演奏起音；演奏时间按首尾对应缩放到这段谱子的时间尺。线条表示对应关系，? 是漏弹，+ 是多弹。力度细框是原值，实心是拟采用值。滚轮上下浏览音轨。"));connect(workflowContext(),&WorkflowContext::changed,this,QOverload<>::of(&WorkflowLanes::update));
      connect(editor-> _scroll,&QScrollBar::valueChanged,this,QOverload<>::of(&WorkflowLanes::update));
      }
QObject* WorkflowLanes::wave(const QVariantMap& t){
      const QString key=t.value("path").toString();auto found=_waves.value(key);if(found)return found;
      auto source=createReferenceMedia(this);if(!source->property("available").toBool()){source->deleteLater();return nullptr;}
      _waves.insert(key,source);connect(source,SIGNAL(waveformChanged()),this,SLOT(waveChanged()));connect(source,SIGNAL(changed()),this,SLOT(waveChanged()));
      QMetaObject::invokeMethod(source,"open",Q_ARG(QString,key));return source;
      }
void WorkflowLanes::showEvent(QShowEvent*){if(!_media){_media=referenceProject();connect(_media,SIGNAL(tracksChanged()),this,SLOT(waveChanged()));}update();}
void WorkflowLanes::hideEvent(QHideEvent*){for(auto source:_waves)if(source)source->deleteLater();_waves.clear();}
void WorkflowLanes::paintEvent(QPaintEvent*){
      QPainter p(this);p.fillRect(rect(),palette().base());_hits.clear();const int gutter=PerformanceViewport::gutter;
      auto expression=workflowContext()->expression();bool hasExpression=!expression.isEmpty();const bool valid=workflowContext()->current(_editor->score());
      int y=-_vertical;auto x=[&](double tick){return _editor->xForTick(int(std::lround(tick)),false);};
      if(hasExpression){
            p.setPen(palette().text().color());p.drawText(QRect(4,y,70,30),Qt::AlignVCenter|Qt::TextWordWrap,tr("谱面\n演奏"));
            if(!valid){p.drawText(QRect(gutter,y,width()-gutter,40),Qt::AlignVCenter,tr("乐谱已经改变，请在演奏表达面板重新读取。"));y+=56;}
            else{
                  auto matches=expression.value("result").toMap().value("displayMatches").toList();if(matches.isEmpty())matches=expression.value("result").toMap().value("matches").toList();const QString selected=expression.value("selectedScoreId").toString();
                  const auto targets=expression.value("candidate").toMap().value("velocityById").toMap();
                  double firstTick=0,lastTick=0,firstSeconds=0,lastSeconds=0;if(!matches.isEmpty()){auto a=matches.front().toMap(),b=matches.back().toMap();firstTick=a.value("tick").toDouble();lastTick=b.value("tick").toDouble();firstSeconds=a.value("seconds").toDouble();lastSeconds=b.value("seconds").toDouble();}
                  auto performanceTick=[&](double seconds){return lastSeconds>firstSeconds?firstTick+(seconds-firstSeconds)*(lastTick-firstTick)/(lastSeconds-firstSeconds):firstTick;};
                  p.save();p.setClipRect(QRect(gutter,y,width()-gutter,136));
                  auto from=std::lower_bound(matches.cbegin(),matches.cend(),_editor->tickForX(gutter-8,false),[](const QVariant& v,int tick){return v.toMap().value("tick").toInt()<tick;});
                  const int until=_editor->tickForX(width()+8,false);
                  for(auto it=from;it!=matches.cend();++it){auto m=it->toMap();if(m.value("tick").toInt()>until)break;double px=x(m.value("tick").toDouble());
                        double mx=x(performanceTick(m.value("seconds").toDouble()));QColor c=m.value("confirmed").toBool()?palette().highlight().color():palette().text().color();if(m.value("scoreId").toString()==selected)c=palette().highlight().color();p.setPen(QPen(c,m.value("scoreId").toString()==selected?2:1));p.drawLine(QPointF(px,y+13),QPointF(mx,y+37));p.setBrush(palette().base());p.drawEllipse(QPointF(px,y+13),4,4);p.setBrush(c);p.drawEllipse(QPointF(mx,y+37),4,4);
                        _hits.append({QRectF(px-7,y+5,14,18),m.value("scoreId").toString(),QString(),-1});_hits.append({QRectF(mx-7,y+28,14,18),m.value("scoreId").toString(),m.value("midiId").toString(),-1});
                        if(!_tempo){const auto old=m.value("original").toMap();double oldValue=old.value("velocity",64).toDouble(),target=m.value("velocity",oldValue).toDouble();QString source=tr("演奏实测");
                        auto v=targets.value(m.value("scoreId").toString()).toMap();if(!v.isEmpty()){target=v.value("target").toDouble();source=v.value("source")=="inferred"?tr("伴奏推算"):tr("拟采用");}
                        p.setBrush(Qt::NoBrush);p.drawRect(QRectF(px-4,y+126-oldValue*.5,8,oldValue*.5));p.setBrush(c);p.drawRect(QRectF(px-2,y+126-target*.5,4,target*.5));
                        if(m.value("scoreId").toString()==selected){p.drawText(QPointF(px+7,y+65),tr("原 %1 → %2 · %3").arg(int(oldValue)).arg(int(target)).arg(source));}}
                        }
                  // Show missing and extra onsets without inventing a confirmed correspondence.
                  for(auto value:expression.value("result").toMap().value("missing").toList()){auto n=value.toMap();double px=x(n.value("tick").toDouble());p.setPen(palette().text().color());p.drawText(QPointF(px-4,y+18),"?");_hits.append({QRectF(px-7,y+5,14,18),n.value("id").toString(),QString(),-1});}
                  for(auto value:expression.value("result").toMap().value("extra").toList()){auto n=value.toMap();if(matches.isEmpty())continue;double px=x(performanceTick(n.value("seconds").toDouble()));p.drawText(QPointF(px-4,y+42),"+");_hits.append({QRectF(px-7,y+28,14,18),QString(),n.value("id").toString(),-1});}
                  if(_tempo){auto tempos=expression.value("candidate").toMap().value("tempos").toList();double low=999,high=5;for(auto value:tempos){auto t=value.toMap();low=qMin(low,qMin(t.value("oldBpm").toDouble(),t.value("bpm").toDouble()));high=qMax(high,qMax(t.value("oldBpm").toDouble(),t.value("bpm").toDouble()));}low=qMax(0.,low-5);high=qMax(low+10,high+5);QPolygonF original,candidate;
                        for(auto value:tempos){auto t=value.toMap();double px=x(t.value("tick").toDouble());original<<QPointF(px,y+127-(t.value("oldBpm").toDouble()-low)/(high-low)*68);candidate<<QPointF(px,y+127-(t.value("bpm").toDouble()-low)/(high-low)*68);}
                        p.setBrush(Qt::NoBrush);p.setPen(QPen(palette().text().color(),1,Qt::DashLine));p.drawPolyline(original);p.setPen(QPen(palette().highlight().color(),2));p.drawPolyline(candidate);p.drawText(QPointF(gutter+4,y+61),tempos.isEmpty()?tr("先在演奏表达生成 Rubato 结果。"):tr("速度：虚线是原值，实线是拟采用值。"));}
                  p.restore();p.setPen(palette().mid().color());p.drawLine(0,y+136,width(),y+136);y+=140;
                  }
            }
      const auto tracks=_media?_media->property("tracks").toList():QVariantList();int index=0;
      QSet<QString> paths;for(auto value:tracks)paths.insert(value.toMap().value("path").toString());for(auto it=_waves.begin();it!=_waves.end();)if(!paths.contains(it.key())){if(it.value())it.value()->deleteLater();it=_waves.erase(it);}else ++it;
      if(!tracks.isEmpty()&&!workflowContext()->referenceCurrent(_editor->score())){p.setPen(palette().text().color());p.drawText(rect().adjusted(gutter, y+4,-8,-4),Qt::AlignTop|Qt::TextWordWrap,tr("这些音频来自另一份乐谱。请在原曲参考中打开对应项目。"));return;}
      for(auto value:tracks){auto t=value.toMap();int row=y; y+=60;if(row+60<0||row>height()){++index;continue;}p.setPen(palette().text().color());p.drawText(QRect(4,row,70,56),Qt::AlignVCenter|Qt::TextWordWrap,t.value("name").toString());_hits.append({QRectF(0,row,gutter,60),QString(),QString(),index++});
            auto audio=wave(t);auto peaks=audio?audio->property("waveform").toList():QVariantList();double duration=audio?audio->property("duration").toDouble():0;
            p.save();p.setClipRect(QRect(gutter,row,width()-gutter,60));
            for(auto av:t.value("associations").toList()){auto a=av.toMap();double first=x(a.value("start").toDouble()),last=x(a.value("end").toDouble());if(last<gutter||first>width())continue;
                  QColor color=palette().highlight().color();color.setAlpha(a.value("active",true).toBool()?35:12);p.fillRect(QRectF(first,row+4,last-first,48),color);p.setPen(a.value("stale").toBool()?palette().mid().color():palette().text().color());
                  if(duration>0&&!peaks.isEmpty())for(int pixel=qMax(gutter,int(first));pixel<qMin(width(),int(last));pixel+=2){int tick=_editor->tickForX(pixel,false);double ms=audioAt(a,tick);int pi=qBound(0,int(ms/duration*peaks.size()),peaks.size()-1);double peak=qMin(1.,peaks[pi].toDouble())*19;p.drawLine(QPointF(pixel,row+29-peak),QPointF(pixel,row+29+peak));}
                  p.drawText(QRectF(first+3,row+3,last-first-6,16),a.value("name").toString());
                  }
            p.restore();p.setPen(palette().mid().color());p.drawLine(0,row+59,width(),row+59);
            }
      if(!hasExpression&&tracks.isEmpty()){p.setPen(palette().text().color());p.drawText(rect().adjusted(8,4,-8,-4),Qt::AlignCenter|Qt::TextWordWrap,tr("从“原曲参考”添加音轨，或从“演奏表达”读取对应。此区域与上方共用时间尺。"));}
      }
void WorkflowLanes::mousePressEvent(QMouseEvent* event){
      for(auto hit:_hits)if(hit.bounds.contains(event->pos())){
            if(hit.track>=0){QToolTip::showText(event->globalPos(),tr("参与播放、静音、独听和关联选择在原曲参考面板中设置。"),this);return;}
            if(!hit.scoreId.isEmpty())_scoreId=hit.scoreId;if(!hit.midiId.isEmpty())_midiId=hit.midiId;
            const auto matches=workflowContext()->expression().value("result").toMap().value("matches").toList();
            for(auto value:matches){auto m=value.toMap();if(m.value("scoreId").toString()!=hit.scoreId)continue;for(int i=0;i<_editor->_notes.size();++i){auto n=_editor->_notes[i];const auto& notes=n.note->chord()->notes();int ni=m.value("index").toInt();if(n.tick==m.value("tick").toInt()&&n.track==m.value("track").toInt()&&ni>=0&&ni<int(notes.size())&&notes[size_t(ni)]==n.note){_editor->selectIndices({i},Qt::NoModifier);break;}}break;}
            workflowContext()->expressionAction("select",_scoreId,hit.midiId);update();return;
            }
      }
void WorkflowLanes::operation(const QString& action){if(!workflowContext()->current(_editor->score()))return;workflowContext()->expressionAction(action,_scoreId,_midiId);}
void WorkflowLanes::wheelEvent(QWheelEvent* event){int rows=(_media?_media->property("tracks").toList().size():0)*60+(workflowContext()->expression().isEmpty()?0:140);_vertical=qBound(0,_vertical-event->angleDelta().y()/2,qMax(0,rows-height()));update();event->accept();}
void addWorkflowLanes(PerformanceEditor* editor,QWidget** result,QWidget* parent){
      auto panel=new QWidget(parent);panel->setObjectName("performanceWorkflowPanel");auto layout=new QVBoxLayout(panel);layout->setContentsMargins(0,0,0,0);auto row=new QHBoxLayout;layout->addLayout(row);auto lane=new WorkflowLanes(editor);layout->addWidget(lane,1);
      auto mode=new QComboBox(panel);mode->setObjectName("workflowComparisonKind");mode->addItems({QObject::tr("力度对比"),QObject::tr("速度对比")});row->addWidget(mode);QObject::connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),lane,[lane](int index){lane->setTempo(index==1);});
      for(auto item:QList<QPair<QString,QString>>{{QObject::tr("确认"),"confirm"},{QObject::tr("连接"),"connect"}}){auto b=new QToolButton(panel);b->setText(item.first);b->setObjectName("workflow"+item.second);QObject::connect(b,&QToolButton::clicked,lane,[lane,item]{lane->operation(item.second);});row->addWidget(b);}
      auto more=new QToolButton(panel);more->setText(QObject::tr("更多"));more->setPopupMode(QToolButton::InstantPopup);auto menu=new QMenu(more);more->setMenu(menu);row->addWidget(more);
      for(const char* name:{"reference-audio-play","reference-joint-play"})menu->addAction(getAction(name));
      for(auto item:QList<QPair<QString,QString>>{{QObject::tr("固定对齐点"),"anchor"},{QObject::tr("解除对应"),"unlink"}}){auto action=menu->addAction(item.first);QObject::connect(action,&QAction::triggered,lane,[lane,item]{lane->operation(item.second);});}
      row->addStretch();auto feedback=new QLabel(panel);feedback->setObjectName("workflowFeedback");feedback->setWordWrap(true);layout->addWidget(feedback);QObject::connect(workflowContext(),&WorkflowContext::feedbackChanged,panel,[feedback]{feedback->setText(workflowContext()->property("feedback").toString());});*result=panel;
      }
}
