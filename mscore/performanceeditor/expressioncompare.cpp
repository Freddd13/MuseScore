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
#include <algorithm>
namespace Ms {
void PerformanceEditor::setupExpressionComparison()
      {
      _comparisonButton=new QToolButton(this);_comparisonButton->setObjectName("performanceExpressionCompare");
      _comparisonButton->setText(tr("演奏前后对比"));_comparisonButton->setCheckable(true);_comparisonButton->setChecked(true);_comparisonButton->hide();
      _comparisonButton->setToolTip(tr("空心显示谱中效果，实心显示拟采用效果；拖动实心条调整预览。使用力度后写入乐谱。"));
      _actionsRow->addWidget(_comparisonButton,_actionsRow->rowCount(),0);
      connect(_comparisonButton,&QToolButton::toggled,this,[this]{cancelComparisonDrag();updateSelection();status(comparisonActive()?tr("调整拟采用效果 · 空心为谱中值，实心为预览值"):tr("继续编辑谱中效果"));});
      connect(workflowContext(),&WorkflowContext::changed,this,&PerformanceEditor::syncExpressionComparison);
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
            cancelComparisonDrag();_comparisonVersion=version;_comparisonState=_state;_comparisonValues.clear();_comparisonTempos=candidate.value("tempos").toList();
            if(available)for(auto value:candidate.value("velocity").toList()){
                  const auto target=value.toMap();const int tick=target.value("tick").toInt();
                  auto first=std::lower_bound(_notes.cbegin(),_notes.cend(),tick,[](const NoteInfo& n,int t){return n.tick<t;});
                  for(auto it=first;it!=_notes.cend()&&it->tick==tick;++it){if(it->track!=target.value("track").toInt())continue;auto chord=it->note->chord();int grace=-1;if(chord->isGrace()&&chord->parent()&&chord->parent()->isChord())grace=toChord(chord->parent())->graceNotes().indexOf(chord);if(grace!=target.value("graceIndex",-1).toInt())continue;
                        const auto& notes=chord->notes();const int index=target.value("index").toInt();if(index>=0&&index<int(notes.size())&&notes[size_t(index)]==it->note){_comparisonValues.insert(int(it-_notes.cbegin()),target);break;}}
                  }
            }
      const bool comparing=comparisonActive(),waiting=comparisonWaiting();
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
      const QColor original=_appearance.colors[PerformanceAppearance::Text],proposed=_appearance.colors[PerformanceAppearance::Preview];
      if(_parameter->currentIndex()==0){
            const int from=tickForX(rect.left()-8,false),until=tickForX(rect.right()+8,false);
            for(int i:_intervals.query(from,until)){const auto& note=_notes[i];if(!note.inScope||(!note.enabled&&!_ghostVoices))continue;const auto lane=velocityLane(i,rect,false);if(lane.isEmpty())continue;
                  double old=NoteVelocity::effective(note.base,note.type,note.raw);if(rangeKind()==1)old=note.type==Note::ValueType::OFFSET_VAL?note.raw:NoteVelocity::offsetFor(note.base,note.raw);
                  const double x=xForNote(i,false),y=yForValue(old,lane),base=qBound(lane.top(),yForValue(rangeKind()==1?0:1,lane),lane.bottom());
                  QColor edge=original;edge.setAlpha(note.enabled?(_comparisonValues.contains(i)?130:50):25);painter.setPen(QPen(edge,1));painter.setBrush(Qt::NoBrush);painter.drawRect(QRectF(x-4,qMin(base,y),8,qMax(1.,qAbs(base-y))));
                  if(!_comparisonValues.contains(i))continue;
                  const double target=yForValue(comparisonValue(i),lane);QColor color=_appearance.noteColor(qRound(_comparisonValues.value(i).value("target").toDouble()),note.track);if(!note.enabled)color.setAlpha(40);
                  painter.setPen(QPen(color,3));painter.drawLine(QPointF(x,base),QPointF(x,target));painter.setBrush(color);painter.setPen(QPen(note.selected?_appearance.colors[PerformanceAppearance::Selection]:color,note.selected?2:1));painter.drawEllipse(QPointF(x,target),note.selected?4:3,note.selected?4:3);
                  if(i==_comparisonAnchor||(!_selectedIndices.isEmpty()&&i==_selectedIndices.front())){const QString text=QString("%1 → %2").arg(NoteVelocity::effective(note.base,note.type,note.raw)).arg(qRound(_comparisonDraft.value(_comparisonValues.value(i).value("id").toString(),_comparisonValues.value(i).value("target").toDouble())));const double w=painter.fontMetrics().horizontalAdvance(text)+10;QRectF badge(qBound(lane.left(),x+8,qMax(lane.left(),lane.right()-w)),lane.bottom()-19,w,18);painter.fillRect(badge,_appearance.colors[PerformanceAppearance::Background]);painter.setPen(QPen(color,1));painter.drawRect(badge);painter.setPen(original);painter.drawText(badge,Qt::AlignCenter,text);}
                  }
            }
      else {
            QPolygonF old,now;double previousOld=0,previousNew=0;bool first=true;
            for(auto value:_comparisonTempos){auto point=value.toMap();const int tick=point.value("tick").toInt();double x=xForTick(tick,false),a=yForValue(point.value("oldBpm").toDouble(),rect),b=yForValue(_comparisonDraft.value(QString::number(tick),point.value("bpm").toDouble()),rect);
                  if(!first){old<<QPointF(x,previousOld);now<<QPointF(x,previousNew);}old<<QPointF(x,a);now<<QPointF(x,b);previousOld=a;previousNew=b;first=false;painter.setPen(QPen(proposed,2));painter.setBrush(proposed);painter.drawEllipse(QPointF(x,b),3,3);}
            painter.setBrush(Qt::NoBrush);painter.setPen(QPen(original,1,Qt::DashLine));painter.drawPolyline(old);painter.setPen(QPen(proposed,2));painter.drawPolyline(now);
            }
      painter.setPen(original);painter.drawText(rect.adjusted(6,3,-6,-3),Qt::AlignTop|Qt::AlignRight,tr("谱中：空心　拟采用：实心"));painter.restore();
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
      if(event->type()==QEvent::ToolTip&&_parameter->currentIndex()==0){auto help=static_cast<QHelpEvent*>(event);int nearest=-1;double distance=1e20;for(int index:_intervals.query(tickForX(help->pos().x()-8,false),tickForX(help->pos().x()+8,false))){if(!_notes[index].enabled||!_comparisonValues.contains(index)||qAbs(xForNote(index,false)-help->pos().x())>8)continue;auto row=velocityLane(index,laneRect(),false);if(row.isEmpty())continue;double d=qAbs(yForValue(comparisonValue(index),row)-help->pos().y());if(d<distance){distance=d;nearest=index;}}if(nearest>=0){const auto& n=_notes[nearest];QToolTip::showText(help->globalPos(),tr("%1 · 谱表 %2 · 声部 %3\n记号力度 %4 %5\n谱中 %6 → 拟采用 %7").arg(n.name).arg(n.track/VOICES+1).arg(n.track%VOICES+1).arg(n.dynamicText).arg(n.base).arg(NoteVelocity::effective(n.base,n.type,n.raw)).arg(qRound(_comparisonValues.value(nearest).value("target").toDouble())),_canvas);return true;}}
      if(event->type()!=QEvent::MouseButtonPress&&event->type()!=QEvent::MouseMove&&event->type()!=QEvent::MouseButtonRelease&&event->type()!=QEvent::MouseButtonDblClick)return false;
      auto mouse=static_cast<QMouseEvent*>(event);QPointF point=object==_canvas?mouse->localPos():_canvas->mapFromGlobal(mouse->globalPos());
      const auto lane=laneRect();
      if(event->type()==QEvent::MouseButtonPress&&mouse->button()==Qt::LeftButton&&lane.contains(point)){
            if(velocityHeaderClick(point))return true;
            _comparisonPress=_comparisonLast=point;_comparisonDrawing=_tool->currentIndex()>0;_comparisonAnchor=-1;_comparisonTempoTick=-1;_comparisonDraft.clear();_comparisonStarts.clear();
            if(_comparisonDrawing){
                  if(_parameter->currentIndex()==0){const bool selected=!_selectedIndices.isEmpty();for(int index:_intervals.query(tickForX(lane.left(),false),tickForX(lane.right(),false)))if(noteEditable(_notes[index])&&_comparisonValues.contains(index)&&(!selected||_notes[index].selected))_comparisonStarts.insert(index,comparisonValue(index));if(_comparisonStarts.isEmpty()){status(tr("选中有实心预览条的音后，可以画一段新的力度。"));return true;}}
                  }
            else if(_parameter->currentIndex()==0){double nearest=1e20;
                  const auto nearby=_intervals.query(tickForX(point.x()-8,false),tickForX(point.x()+8,false));int closestTick=-1;double closestX=9.;
                  for(int index:nearby){if(!noteEditable(_notes[index])||!_comparisonValues.contains(index))continue;const double d=qAbs(xForNote(index,false)-point.x());if(d<closestX){closestX=d;closestTick=_notes[index].tick;}}
                  for(int index:nearby){const auto& n=_notes[index];if(n.tick!=closestTick||!noteEditable(n)||!_comparisonValues.contains(index))continue;const auto row=velocityLane(index,lane,false);if(row.isEmpty()||!row.contains(point))continue;const double distance=qAbs(yForValue(comparisonValue(index),row)-point.y())-(n.selected?100000.:0.);if(distance<nearest){nearest=distance;_comparisonAnchor=index;}}
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
                  if(_parameter->currentIndex()==0)for(auto it=_comparisonStarts.cbegin();it!=_comparisonStarts.cend();++it){const int index=it.key();const double x=xForNote(index,false);if(x<left||x>right)continue;const auto row=velocityLane(index,lane,false);if(row.isEmpty())continue;double target=valueForY(yAt(x),row);if(rangeKind()==1)target=NoteVelocity::effective(_notes[index].base,Note::ValueType::OFFSET_VAL,qRound(target));_comparisonDraft.insert(_comparisonValues.value(index).value("id").toString(),qBound(1.,target,127.));}
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
