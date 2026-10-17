// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "performanceeditor.h"
#include "../workflow/workflowcontext.h"
#include "libmscore/chord.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHelpEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QToolTip>
#include <QGridLayout>
#include <QSignalBlocker>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollBar>
#include <QSettings>
#include <algorithm>
namespace Ms {
void PerformanceEditor::setupExpressionComparison()
      {
      _comparisonButton=new QToolButton(this);_comparisonButton->setObjectName("performanceExpressionCompare");
      _comparisonButton->setText(tr("演奏前后对比"));_comparisonButton->setCheckable(true);_comparisonButton->setChecked(true);_comparisonButton->hide();
      _comparisonButton->setToolTip(tr("左侧中性色柱为谱中力度，右侧强调色柱为拟采用力度。拖动强调色柱调整；点击使用力度保存到乐谱。"));
      const int row=_actionsRow->rowCount();_actionsRow->addWidget(_comparisonButton,row,0);
      _comparisonView=new QComboBox(this);_comparisonView->setObjectName("performanceComparisonView");
      _comparisonView->addItems({tr("同音对比"),tr("真实播放时间对比")});_comparisonView->hide();
      _comparisonView->setCurrentIndex(qBound(0,QSettings().value("performanceEditor/comparisonView",0).toInt(),1));
      _comparisonTime=_comparisonView->currentIndex()==1;
      _comparisonView->setToolTip(tr("同音对比把原、新力度放在同一个谱位。真实播放时间按两种效果各自的秒数排列，用来查看速度变化。"));
      _actionsRow->addWidget(_comparisonView,row,1);
      connect(_comparisonView,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int mode){cancelComparisonDrag();const double center=_canvas->width()*.5;const int anchor=tickForX(center,false);_comparisonTime=mode==1;
            QSettings().setValue("performanceEditor/comparisonView",mode);surfaceResized(PerformanceSurface::Parameter);_scroll->setValue(qMax(0,leftTickForAnchor(anchor,center)));invalidateVisual();});
      connect(_comparisonButton,&QToolButton::toggled,this,[this]{cancelComparisonDrag();syncExpressionComparison();updateSelection();status(comparisonActive()?tr("调整拟采用效果 · 中性色为谱中值，强调色为预览值"):tr("继续编辑谱中效果"));});
      connect(workflowContext(),&WorkflowContext::changed,this,&PerformanceEditor::syncExpressionComparison);
      connect(workflowContext(),&WorkflowContext::auditionChanged,this,[this]{const auto mode=workflowContext()->audition().value("mode").toString();if(_comparisonButton->property("auditionMode").toString()!=mode){_comparisonButton->setProperty("auditionMode",mode);updateSurfaces();}});
      }
bool PerformanceEditor::comparisonTimeActive()const
      {return _score&&_comparisonView&&_comparisonTime&&comparisonActive();}
int PerformanceEditor::comparisonDisplayTick(int index)const
      {return comparisonTimeActive()?_comparisonValues.value(index).value("displayTick",_notes[index].tick).toInt():_notes[index].tick;}
int PerformanceEditor::leftTickForAnchor(int tick,double x)const
      {
      const double units=(x-PerformanceViewport::gutter)/_viewport.pixelsPerQuarter;
      return comparisonTimeActive()?_score->tempomap()->time2tick(_score->tempomap()->tick2time(tick)-units*.5):tick-qRound(units*DIVISION);
      }
int PerformanceEditor::comparisonPlayTick(int tick)const
      {
      if(!comparisonTimeActive()||workflowContext()->audition().value("mode").toString()!="candidate")return tick;
      return _score->tempomap()->time2tick(_comparisonClock.tick2time(tick));
      }
QVector<int> PerformanceEditor::comparisonVisible(int from,int until)const
      {
      auto indices=_intervals.query(from,until);
      if(comparisonTimeActive()){
            auto first=std::lower_bound(_comparisonPositions.cbegin(),_comparisonPositions.cend(),from,[](const QPair<int,int>& a,int t){return a.first<t;});
            for(auto it=first;it!=_comparisonPositions.cend()&&it->first<=until;++it)indices.append(it->second);
            std::sort(indices.begin(),indices.end());indices.erase(std::unique(indices.begin(),indices.end()),indices.end());
            }
      return indices;
      }
bool PerformanceEditor::comparisonActive()const
      {return _comparisonButton&&_comparisonButton->isChecked()&&workflowContext()->current(_score)&&((_parameter->currentIndex()==0&&!_comparisonValues.isEmpty())||(_parameter->currentIndex()==1&&!_comparisonTempos.isEmpty()));}
bool PerformanceEditor::comparisonWaiting()const {return _comparisonButton&&_comparisonButton->isChecked()&&!_comparisonButton->isHidden()&&workflowContext()->current(_score)&&workflowContext()->expression().value("busy").toBool();}
void PerformanceEditor::syncExpressionComparison()
      {
      if(!_comparisonButton)return;
      const auto data=workflowContext()->expression();const auto candidate=data.value("candidate").toMap();
      const bool available=workflowContext()->current(_score)&&(!candidate.value("velocity").toList().isEmpty()||!candidate.value("tempos").toList().isEmpty()||(data.value("busy").toBool()&&!_comparisonButton->isHidden()));
      const bool changed=_comparisonButton->isHidden()==available;_comparisonButton->setVisible(available);
      const QString version=data.value("revision").toString()+":"+QString::number(data.value("previewVersion").toLongLong());
      if(version!=_comparisonVersion||_comparisonState!=_state){
            cancelComparisonDrag();_comparisonVersion=version;_comparisonState=_state;_comparisonValues.clear();_comparisonPositions.clear();_comparisonEnd=_endTick;_comparisonTempos=candidate.value("tempos").toList();
            if(_score){_comparisonClock=*_score->tempomap();for(auto point:_comparisonTempos){auto t=point.toMap();_comparisonClock.setTempo(t.value("tick").toInt(),t.value("bpm").toDouble()/60.);}_comparisonEnd=qMax(_endTick,_score->tempomap()->time2tick(_comparisonClock.tick2time(_endTick)));}
            if(available)for(auto value:candidate.value("velocity").toList()){
                  auto target=value.toMap();const int tick=target.value("tick").toInt();
                  const int display=_score->tempomap()->time2tick(_comparisonClock.tick2time(tick));target["displayTick"]=display;
                  _comparisonEnd=qMax(_comparisonEnd,display+DIVISION);
                  auto first=std::lower_bound(_notes.cbegin(),_notes.cend(),tick,[](const NoteInfo& n,int t){return n.tick<t;});
                  for(auto it=first;it!=_notes.cend()&&it->tick==tick;++it){if(it->track!=target.value("track").toInt())continue;auto chord=it->note->chord();int grace=-1;if(chord->isGrace()&&chord->parent()&&chord->parent()->isChord())grace=toChord(chord->parent())->graceNotes().indexOf(chord);if(grace!=target.value("graceIndex",-1).toInt())continue;
                        const auto& notes=chord->notes();const int index=target.value("index").toInt();if(index>=0&&index<int(notes.size())&&notes[size_t(index)]==it->note){const int index=int(it-_notes.cbegin());_comparisonValues.insert(index,target);_comparisonPositions.append({display,index});break;}}
                  }
            std::sort(_comparisonPositions.begin(),_comparisonPositions.end());
            }
      const bool comparing=comparisonActive(),waiting=comparisonWaiting();
      _comparisonView->setVisible(available&&_comparisonButton->isChecked());
      const bool guides=comparing&&_parameter->currentIndex()==0;
      if(guides!=_comparisonGuides){_comparisonGuides=guides;if(auto check=findChild<QCheckBox*>("performanceActualVelocity")){
            QSignalBlocker block(check);_relativeActual=QSettings().value(guides?"performanceEditor/comparisonVelocityLines":"performanceEditor/markVelocityLines",!guides).toBool();check->setChecked(_relativeActual);}}

      _number->setEnabled(_parameter->currentIndex()!=2&&!waiting);
      if(comparing&&_parameter->currentIndex()==0&&!_selectedIndices.isEmpty()&&_comparisonValues.contains(_selectedIndices.front())){QSignalBlocker block(_number);_number->setValue(comparisonValue(_selectedIndices.front()));}
      for(const char* name:{"performanceConvertRelative","performanceConvertAbsolute"})if(auto button=findChild<QPushButton*>(name))button->setEnabled(!comparing&&!waiting&&_parameter->currentIndex()==0&&!_snapshotDegraded);
      if(auto button=findChild<QPushButton*>("performanceWriteValue")){button->setEnabled(_parameter->currentIndex()!=2&&!waiting);button->setText(comparing?tr("调整拟采用值"):_parameter->currentIndex()==0?tr("写入选中"):tr("写入速度节点"));}
      if(changed)resizeEvent(nullptr);updateSurfaces();
      }
double PerformanceEditor::comparisonValue(int index)const
      {
      const auto value=_comparisonValues.value(index);const double target=_comparisonDraft.value(value.value("id").toString(),value.value("target").toDouble());
      return rangeKind()==1?NoteVelocity::offsetFor(_notes[index].base,qRound(target)):target;
      }
void PerformanceEditor::paintExpressionComparison(QPainter& painter,const QRectF& rect)
      {
      painter.save();painter.setClipRect(rect,Qt::IntersectClip);
      const QColor original=_appearance.colors[PerformanceAppearance::Text],proposed=palette().color(QPalette::Highlight);
      if(_parameter->currentIndex()==0){
            const int from=tickForX(rect.left()-8,false),until=tickForX(rect.right()+8,false);
            for(int i:comparisonVisible(from,until)){
                  const auto& note=_notes[i];if(!note.inScope||(!note.enabled&&!_ghostVoices))continue;
                  const auto lane=velocityLane(i,rect,false);if(lane.isEmpty())continue;
                  double old=NoteVelocity::effective(note.base,note.type,note.raw);if(rangeKind()==1)old=note.type==Note::ValueType::OFFSET_VAL?note.raw:NoteVelocity::offsetFor(note.base,note.raw);
                  const double x=xForNote(i,false),y=yForValue(old,lane),base=qBound(lane.top(),yForValue(rangeKind()==1?0:1,lane),lane.bottom());
                  QColor neutral=original;neutral.setAlpha(note.enabled?(_comparisonValues.contains(i)?210:45):25);
                  QColor voice=MScore::selectColor[note.track%VOICES];voice.setAlpha(note.enabled?(_comparisonValues.contains(i)?180:45):25);
                  painter.setPen(QPen(voice,note.selected?2:1));painter.setBrush(neutral);
                  painter.drawRect(QRectF(x-9,qMin(base,y),7,qMax(2.,qAbs(base-y))));
                  if(!_comparisonValues.contains(i))continue;
                  const double nextX=xForTick(comparisonDisplayTick(i),false)+2,target=yForValue(comparisonValue(i),lane);
                  QColor color=proposed;if(!note.enabled)color.setAlpha(40);
                  painter.setPen(QPen(note.selected?_appearance.colors[PerformanceAppearance::Selection]:voice,note.selected?2:1));painter.setBrush(color);
                  painter.drawRect(QRectF(nextX,qMin(base,target),7,qMax(2.,qAbs(base-target))));
                  if(i==_comparisonAnchor||(!_selectedIndices.isEmpty()&&i==_selectedIndices.front())){
                        const QString text=QString("%1 → %2").arg(NoteVelocity::effective(note.base,note.type,note.raw)).arg(qRound(_comparisonDraft.value(_comparisonValues.value(i).value("id").toString(),_comparisonValues.value(i).value("target").toDouble())));
                        const double w=painter.fontMetrics().horizontalAdvance(text)+10;QRectF badge(qBound(lane.left(),nextX+8,qMax(lane.left(),lane.right()-w)),lane.bottom()-19,w,18);
                        painter.fillRect(badge,_appearance.colors[PerformanceAppearance::Background]);painter.setPen(QPen(color,1));painter.drawRect(badge);painter.setPen(original);painter.drawText(badge,Qt::AlignCenter,text);
                        }
                  }
            // End caps keep each simultaneous note visible when filled bars overlap.
            for(int i:comparisonVisible(from,until)){
                  const auto& note=_notes[i];if(!note.inScope||(!note.enabled&&!_ghostVoices)||!_comparisonValues.contains(i))continue;
                  const auto lane=velocityLane(i,rect,false);if(lane.isEmpty())continue;
                  const double x=xForTick(comparisonDisplayTick(i),false)+2,y=yForValue(comparisonValue(i),lane);
                  QColor edge=original;edge.setAlpha(note.enabled?220:40);painter.setPen(QPen(edge,note.selected?2:1));painter.drawLine(QPointF(x-1,y),QPointF(x+8,y));
                  if(note.selected){painter.setBrush(proposed);painter.drawEllipse(QPointF(x+3.5,y),3,3);}
                  }
            }
      else {
            QPolygonF old,now;double previousOld=0,previousNew=0;bool first=true;
            for(auto value:_comparisonTempos){auto point=value.toMap();const int tick=point.value("tick").toInt();double x=xForTick(tick,false),a=yForValue(point.value("oldBpm").toDouble(),rect),b=yForValue(_comparisonDraft.value(QString::number(tick),point.value("bpm").toDouble()),rect);
                  if(!first){old<<QPointF(x,previousOld);now<<QPointF(x,previousNew);}old<<QPointF(x,a);now<<QPointF(x,b);previousOld=a;previousNew=b;first=false;painter.setPen(QPen(proposed,2));painter.setBrush(proposed);painter.drawEllipse(QPointF(x,b),3,3);}
            painter.setBrush(Qt::NoBrush);painter.setPen(QPen(original,1,Qt::DashLine));painter.drawPolyline(old);painter.setPen(QPen(proposed,2));painter.drawPolyline(now);
            }
      const QString oldLabel=tr("谱中"),newLabel=tr("拟采用");const double a=painter.fontMetrics().horizontalAdvance(oldLabel),b=painter.fontMetrics().horizontalAdvance(newLabel),x=rect.right()-a-b-54,y=rect.top()+6;
      painter.fillRect(QRectF(x,y+2,8,8),original);painter.setPen(original);painter.drawText(QPointF(x+12,y+painter.fontMetrics().ascent()),oldLabel);
      painter.fillRect(QRectF(x+a+28,y+2,8,8),proposed);painter.drawText(QPointF(x+a+40,y+painter.fontMetrics().ascent()),newLabel);painter.restore();
      }
void PerformanceEditor::cancelComparisonDrag()
      {
      if(_comparisonDragging){_comparisonDragging=false;qApp->removeEventFilter(this);_canvas->releaseMouse();_canvas->unsetCursor();}
      _comparisonDraft.clear();_comparisonStarts.clear();_comparisonAnchor=-1;_comparisonTempoTick=-1;
      }
void PerformanceEditor::commitComparisonDrag()
      {
      const auto values=_comparisonDraft;const bool tempo=_parameter->currentIndex()==1;
      _comparisonDragging=false;qApp->removeEventFilter(this);_canvas->releaseMouse();_canvas->unsetCursor();
      QVariantList edits;for(auto it=values.cbegin();it!=values.cend();++it)edits.append(tempo?QVariantMap{{"tick",it.key().toInt()},{"bpm",it.value()}}:QVariantMap{{"id",it.key()},{"target",qRound(it.value())}});
      if(!edits.isEmpty())workflowContext()->expressionAction(tempo?"tempoTargets":"velocityTargets",QString(),QString::fromUtf8(QJsonDocument::fromVariant(edits).toJson(QJsonDocument::Compact)));
      _comparisonDraft.clear();_comparisonStarts.clear();_comparisonAnchor=-1;_comparisonTempoTick=-1;updateSurfaces();
      }
bool PerformanceEditor::comparisonEvent(QObject* object,QEvent* event)
      {
      if(_comparisonDragging&&(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress)&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){event->accept();if(event->type()==QEvent::KeyPress){cancelComparisonDrag();updateSurfaces();}return true;}
      if(_comparisonDragging&&(event->type()==QEvent::UngrabMouse||event->type()==QEvent::ApplicationDeactivate)){cancelComparisonDrag();updateSurfaces();return false;}
      if(object==_canvas&&comparisonWaiting()&&event->type()==QEvent::MouseButtonPress){status(tr("正在更新拟采用效果，完成后可以继续拖动。"));return true;}
      if(!comparisonActive()){if(_comparisonDragging)cancelComparisonDrag();return false;}
      if(object!=_canvas&&!_comparisonDragging)return false;
      if(event->type()==QEvent::Hide){cancelComparisonDrag();return false;}
      if(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress){auto key=static_cast<QKeyEvent*>(event);const auto state=workflowContext()->expression();const bool undo=key->matches(QKeySequence::Undo)&&state.value("previewCanUndo").toBool(),redo=key->matches(QKeySequence::Redo)&&state.value("previewCanRedo").toBool();if(undo||redo){event->accept();if(event->type()==QEvent::KeyPress)workflowContext()->expressionAction(undo?"undoPreview":"redoPreview",QString(),QString());return true;}}
      if(event->type()==QEvent::ToolTip&&_parameter->currentIndex()==0){auto help=static_cast<QHelpEvent*>(event);int nearest=-1;double distance=1e20;for(int index:comparisonVisible(tickForX(help->pos().x()-12,false),tickForX(help->pos().x()+12,false))){if(!_notes[index].enabled||!_comparisonValues.contains(index)||qAbs(xForTick(comparisonDisplayTick(index),false)+4-help->pos().x())>9)continue;auto row=velocityLane(index,laneRect(),false);if(row.isEmpty())continue;double d=qAbs(yForValue(comparisonValue(index),row)-help->pos().y());if(d<distance){distance=d;nearest=index;}}if(nearest>=0){const auto& n=_notes[nearest];QToolTip::showText(help->globalPos(),tr("%1 · 谱表 %2 · 声部 %3\n力度参考线 %4：%5\n谱中 %6 → 拟采用 %7\n重音等演奏处理后的基础力度：%8").arg(n.name).arg(n.track/VOICES+1).arg(n.track%VOICES+1).arg(n.dynamicText).arg(n.markBase).arg(NoteVelocity::effective(n.base,n.type,n.raw)).arg(qRound(_comparisonValues.value(nearest).value("target").toDouble())).arg(n.base),_canvas);return true;}}
      if(event->type()!=QEvent::MouseButtonPress&&event->type()!=QEvent::MouseMove&&event->type()!=QEvent::MouseButtonRelease&&event->type()!=QEvent::MouseButtonDblClick)return false;
      auto mouse=static_cast<QMouseEvent*>(event);QPointF point=object==_canvas?mouse->localPos():_canvas->mapFromGlobal(mouse->globalPos());
      const auto lane=laneRect();
      if(event->type()==QEvent::MouseButtonPress&&mouse->button()==Qt::LeftButton&&lane.contains(point)){
            if(velocityHeaderClick(point))return true;
            _comparisonPress=_comparisonLast=point;_comparisonDrawing=_tool->currentIndex()>0;_comparisonAnchor=-1;_comparisonTempoTick=-1;_comparisonDraft.clear();_comparisonStarts.clear();
            if(_comparisonDrawing){
                  if(_parameter->currentIndex()==0){const bool selected=!_selectedIndices.isEmpty();for(int index:comparisonVisible(tickForX(lane.left(),false),tickForX(lane.right(),false)))if(noteEditable(_notes[index])&&_comparisonValues.contains(index)&&(!selected||_notes[index].selected))_comparisonStarts.insert(index,comparisonValue(index));if(_comparisonStarts.isEmpty()){status(tr("选中有强调色预览条的音后，可以画一段新的力度。"));return true;}}
                  }
            else if(_parameter->currentIndex()==0){double nearest=1e20;
                  const auto nearby=comparisonVisible(tickForX(point.x()-12,false),tickForX(point.x()+12,false));
                  int originalIndex=-1;double originalDistance=1e20;
                  for(int index:nearby){const auto& n=_notes[index];if(!noteEditable(n))continue;const auto row=velocityLane(index,lane,false);if(!row.contains(point))continue;
                        const double sourceX=xForNote(index,false)-4.5;
                        double old=NoteVelocity::effective(n.base,n.type,n.raw);if(rangeKind()==1)old=n.type==Note::ValueType::OFFSET_VAL?n.raw:NoteVelocity::offsetFor(n.base,n.raw);
                        const double sourceDistance=qAbs(yForValue(old,row)-point.y());
                        if(qAbs(sourceX-point.x())<=4&&sourceDistance<originalDistance){originalDistance=sourceDistance;originalIndex=index;}
                        if(!_comparisonValues.contains(index))continue;
                        if(qAbs(xForTick(comparisonDisplayTick(index),false)+4.5-point.x())>5)continue;
                        const double distance=qAbs(yForValue(comparisonValue(index),row)-point.y())-(n.selected?100000.:0.);
                        if(distance<nearest){nearest=distance;_comparisonAnchor=index;}
                        }
                  if(originalIndex>=0&&_comparisonAnchor<0){selectIndices({originalIndex},mouse->modifiers(),true);status(tr("已选中谱中音符；拖动旁边的强调色柱调整拟采用力度。"));return true;}
                  if(_comparisonAnchor<0)return true;
                  selectIndices({_comparisonAnchor},mouse->modifiers(),true);
                  const auto selected=selectedNotes();for(int index:selected)if(_comparisonValues.contains(index))_comparisonStarts.insert(index,comparisonValue(index));
                  }
            else {double nearest=10.;for(auto v:_comparisonTempos){auto t=v.toMap();double d=qAbs(xForTick(t.value("tick").toInt(),false)-point.x());if(d<nearest){nearest=d;_comparisonTempoTick=t.value("tick").toInt();_comparisonTempoStart=t.value("bpm").toDouble();}}if(_comparisonTempoTick<0)return true;}
            _comparisonDragging=true;_canvas->grabMouse();qApp->installEventFilter(this);_canvas->setCursor(Qt::SizeVerCursor);status(tr("正在调整拟采用效果；使用力度或速度后写入乐谱。"));return true;
            }
      if(_comparisonDragging&&(event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonRelease)){
            if(_comparisonDrawing){
                  const bool line=_tool->currentIndex()==2;const QPointF from=line?_comparisonPress:_comparisonLast;if(line)_comparisonDraft.clear();
                  const double left=qMin(from.x(),point.x())-.5,right=qMax(from.x(),point.x())+.5;
                  auto yAt=[&](double x){return qFuzzyCompare(from.x()+1,point.x()+1)?point.y():from.y()+(point.y()-from.y())*(x-from.x())/(point.x()-from.x());};
                  if(_parameter->currentIndex()==0)for(auto it=_comparisonStarts.cbegin();it!=_comparisonStarts.cend();++it){const int index=it.key();const double x=xForTick(comparisonDisplayTick(index),false)+4.5;if(x<left||x>right)continue;const auto row=velocityLane(index,lane,false);if(row.isEmpty())continue;double target=valueForY(yAt(x),row);if(rangeKind()==1)target=NoteVelocity::effective(_notes[index].base,Note::ValueType::OFFSET_VAL,qRound(target));_comparisonDraft.insert(_comparisonValues.value(index).value("id").toString(),qBound(1.,target,127.));}
                  else for(auto value:_comparisonTempos){const auto t=value.toMap();const int tick=t.value("tick").toInt();const double x=xForTick(tick,false);if(x>=left&&x<=right)_comparisonDraft.insert(QString::number(tick),qBound(5.,valueForY(yAt(x),lane),999.));}
                  _comparisonLast=point;
                  }
            else if(_comparisonTempoTick>=0){const double delta=(valueForY(point.y(),lane)-valueForY(_comparisonPress.y(),lane))*_dragSensitivity;_comparisonDraft.insert(QString::number(_comparisonTempoTick),qBound(5.,_comparisonTempoStart+delta,999.));}
            else {const auto row=velocityLane(_comparisonAnchor,lane,false);if(!row.isEmpty()){const double delta=(valueForY(point.y(),row)-valueForY(_comparisonPress.y(),row))*_dragSensitivity;for(auto it=_comparisonStarts.cbegin();it!=_comparisonStarts.cend();++it){const auto& n=_notes[it.key()];const double scale=_groupRelative&&rangeKind()==0&&_comparisonStarts.size()>1&&_notes[_comparisonAnchor].base>0?double(n.base)/_notes[_comparisonAnchor].base:1.;double target=it.value()+delta*scale;if(rangeKind()==1)target=NoteVelocity::effective(n.base,Note::ValueType::OFFSET_VAL,qRound(target));_comparisonDraft.insert(_comparisonValues.value(it.key()).value("id").toString(),qBound(1.,target,127.));}}}
            updateSurfaces();if(event->type()==QEvent::MouseButtonRelease)commitComparisonDrag();return true;
            }
      if(event->type()==QEvent::MouseButtonDblClick&&lane.contains(point))return true;
      return false;
      }
}
