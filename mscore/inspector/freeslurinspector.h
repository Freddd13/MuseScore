// Optional intermediate slur nodes, using native linked properties and Undo. GPL v2.
#ifndef MS_FREESLURINSPECTOR_H
#define MS_FREESLURINSPECTOR_H
#include "inspector.h"
#include "scrubproperty.h"
#include "libmscore/slur.h"
#include "../musescore.h"
#include "../performanceeditor/performanceeditor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <cmath>
namespace Ms {
class FreeSlurInspector : public QWidget {
      Inspector* _inspector;
      Slur* _owner = nullptr;
      QVector<FreeSlurNode> _nodes;
      QComboBox* _choice;
      QComboBox* _span=nullptr;
      QComboBox* _startSide=nullptr;QComboBox* _endSide=nullptr;
      QComboBox* _route=nullptr;
      QToolButton* _smoothSpan=nullptr;
      QDoubleSpinBox* _position;
      QVector<QDoubleSpinBox*> _values;
      QWidget* _details;
      QCheckBox* _automatic;
      QCheckBox* _locked;
      QLabel* _result;
      void commit(int selected)
            {
            if (!_owner) return;
            mscore->performanceEditor()->queueProperty(_owner,Pid::FREE_SLUR_NODES,FreeSlur::encode(_nodes));
            refresh(selected);
            }
      void refresh(int selected)
            {
            QSignalBlocker block(_choice); _choice->clear();
            for (int i=0;i<_nodes.size();++i)
                  _choice->addItem(tr("节点 %1（%2%）").arg(i+1).arg(_nodes[i].position*100,0,'f',2));
            _choice->setCurrentIndex(qBound(-1,selected,_nodes.size()-1));
            if(_span){QSignalBlocker block(_span);const int previous=_span->currentIndex();_span->clear();
                  _span->addItem(tr("整条圆滑线"));
                  if(!_nodes.isEmpty())for(int i=0;i<=_nodes.size();++i)_span->addItem(tr("%1 → %2").arg(i?tr("点 %1").arg(i):tr("起点"),i<_nodes.size()?tr("点 %1").arg(i+1):tr("终点")));
                  _span->setCurrentIndex(qBound(0,previous,_span->count()-1));}
            showNode();
            }
      void showNode()
            {
            const int i=_choice->currentIndex();
            QSignalBlocker lockBlock(_locked);_locked->setEnabled(i>=0);_locked->setChecked(i>=0 && _nodes[i].locked);
            for (int k=0;k<_values.size();++k) {
                  QSignalBlocker block(_values[k]); _values[k]->setEnabled(i>=0);
                  if(i>=0) _values[k]->setValue(k==0 ? _nodes[i].offset.x() : k==1 ? _nodes[i].offset.y() : k==2 ? _nodes[i].tangent.x() : _nodes[i].tangent.y());
                  }
            showRoute();
            }
      void showRoute(){
            if(!_route||!_span)return;const int i=_span->currentIndex()-1;
            int direction=_nodes.isEmpty()?0:i<0?_nodes.front().before:i<_nodes.size()?_nodes[i].before:_nodes.back().after;bool mixed=false;
            if(i<0&&!_nodes.isEmpty()){for(const auto& node:_nodes)mixed|=node.before!=direction;mixed|=_nodes.back().after!=direction;}
            QSignalBlocker block(_route);_route->setCurrentIndex(mixed?3:direction<0?1:direction>0?2:0);
            if(_smoothSpan){_smoothSpan->setEnabled(!_nodes.isEmpty());_smoothSpan->setText(i<0?tr("整理整条线"):tr("整理这一段"));}
            }

      QToolButton* buttonForRestore(QWidget* host,QGridLayout* grid)
            {
            auto button=new QToolButton(host);button->setText(tr("恢复自动形状"));button->setObjectName("freeSlurRestoreAuto");grid->addWidget(button,11,0,1,3);
            button->setToolTip(tr("清除这条线的中间点并重新自动避让；可撤销。起点、终点和原有手柄偏移保留。"));return button;
            }
   public:
      QCheckBox* mode;
      FreeSlurInspector(QWidget* host,Inspector* inspector) : QWidget(host),_inspector(inspector)
            {
            setObjectName("freeSlurPanel"); host->layout()->addWidget(this);
            auto layout=new QGridLayout(this); mode=new QCheckBox(tr("自由曲线"),this); mode->setObjectName("freeSlurMode"); layout->addWidget(mode,0,0,1,3);
            _details=new QWidget(this); layout->addWidget(_details,3,0,1,3); auto grid=new QGridLayout(_details);
            _automatic=new QCheckBox(tr("自动避让音符"),this);_automatic->setObjectName("freeSlurAuto");layout->addWidget(_automatic,1,0,1,3);
            auto endpoints=new QWidget(this);auto endpointGrid=new QGridLayout(endpoints);endpointGrid->setContentsMargins(0,0,0,0);layout->addWidget(endpoints,5,0,1,3);
            for(int i=0;i<2;++i){auto box=new QComboBox(endpoints);box->addItems({tr("自动"),tr("音符上方"),tr("音符下方")});box->setObjectName(i?"freeSlurEndSide":"freeSlurStartSide");endpointGrid->addWidget(new QLabel(i?tr("终点位置"):tr("起点位置"),endpoints),i,0);endpointGrid->addWidget(box,i,1);if(i)_endSide=box;else _startSide=box;connect(box,QOverload<int>::of(&QComboBox::activated),this,[this,i](int value){if(_owner)mscore->performanceEditor()->queueProperty(_owner,i?Pid::FREE_SLUR_END_SIDE:Pid::FREE_SLUR_START_SIDE,value==1?-1:value==2?1:0);});}
            _result=new QLabel(this);_result->setWordWrap(true);layout->addWidget(_result,2,0,1,3);
            connect(_automatic,&QCheckBox::clicked,this,[this](bool value) {
                  if(_owner)mscore->performanceEditor()->queueProperty(_owner,Pid::FREE_SLUR_AUTO,value);
                  });
            _locked=new QCheckBox(tr("锁定这个点和切线"),_details);_locked->setObjectName("freeSlurLock");grid->addWidget(_locked,6,0,1,3);
            connect(_locked,&QCheckBox::clicked,this,[this](bool value) {int i=_choice->currentIndex();if(i<0)return;
                  if(value && _owner)for(auto segment:_owner->spannerSegments())if(toSlurSegment(segment)->displayedFreeNode(i,&_nodes[i]))break;
                  _nodes[i].locked=value;commit(i);});
            auto restore=buttonForRestore(_details,grid);
            connect(restore,&QToolButton::clicked,this,[this] {
                  if(!_owner)return;_nodes.clear();auto editor=mscore->performanceEditor();
                  editor->queueProperty(_owner,Pid::FREE_SLUR_NODES,QString("[]"));
                  editor->queueProperty(_owner,Pid::FREE_SLUR_AUTO,true);refresh(-1);
                  });
            _choice=new QComboBox(_details); _choice->setObjectName("freeSlurNode"); grid->addWidget(_choice,0,0,1,3);
            _position=new QDoubleSpinBox(_details); _position->setObjectName("freeSlurAddPosition"); _position->setRange(.01,99.99); _position->setDecimals(2); _position->setValue(50); _position->setSuffix(" %");
            grid->addWidget(new QLabel(tr("新增位置"),_details),1,0);grid->addWidget(_position,1,1);
            auto button=[&](const QString& name,const char* id,int row,int col) {auto b=new QToolButton(_details);b->setText(name);b->setObjectName(id);grid->addWidget(b,row,col);return b;};
            auto add=button(tr("添加"),"freeSlurAdd",1,2);
            auto remove=button(tr("删除节点"),"freeSlurRemove",2,0);
            auto smooth=button(tr("重置切线"),"freeSlurSmooth",2,1);
            auto reset=button(tr("重置节点"),"freeSlurReset",2,2);
            QObject::connect(_choice,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){showNode();});
            QObject::connect(add,&QToolButton::clicked,this,[this] {
                  if(!_owner || _nodes.size()>=FreeSlur::maximumNodes) return;
                  auto nodes=_nodes;FreeSlurNode node{_position->value()/100,{}, {}};
                  if(_owner->freeAuto())for(auto s:_owner->spannerSegments())if(toSlurSegment(s)->displayedFreeNode(node.position,&node))break;
                  int insertion=0;while(insertion<nodes.size()&&nodes[insertion].position<node.position)++insertion;
                  const int direction=nodes.isEmpty()?0:insertion<nodes.size()?nodes[insertion].before:nodes.back().after;
                  node.before=direction;node.after=insertion==nodes.size()?direction:0;if(insertion==nodes.size()&&!nodes.isEmpty())nodes.back().after=0;
                  nodes.append(node); QVector<FreeSlurNode> valid;
                  if(!FreeSlur::decode(FreeSlur::encode(nodes),&valid)) {_position->setToolTip(tr("该位置已有节点；请调整百分比。"));return;}
                  _nodes=valid; int i=0;while(i<_nodes.size() && _nodes[i].position<_position->value()/100) ++i; commit(i);
                  });
            QObject::connect(remove,&QToolButton::clicked,this,[this] {int i=_choice->currentIndex();if(i<0)return;FreeSlur::removeNode(&_nodes,i);commit(qMin(i,_nodes.size()-1));});
            QObject::connect(smooth,&QToolButton::clicked,this,[this] {int i=_choice->currentIndex();if(i<0)return;_nodes[i].tangent={};commit(i);});
            QObject::connect(reset,&QToolButton::clicked,this,[this] {_nodes.clear();commit(-1);});
            const QStringList labels={tr("水平偏移"),tr("竖直偏移"),tr("水平切线"),tr("竖直切线")};
            for(int k=0;k<4;++k) {
                  auto spin=new QDoubleSpinBox(_details);spin->setObjectName(QString("freeSlurValue%1").arg(k));spin->setRange(k<2 ? -100:-400,k<2 ? 100:400);spin->setSingleStep(.1);spin->setDecimals(2);spin->setSuffix(tr(" sp"));spin->setKeyboardTracking(false);_values.append(spin);
                  auto label=new ScrubPropertyLabel(labels[k],spin,_details);label->setObjectName(spin->objectName()+"Label");grid->addWidget(label,7+k,0);grid->addWidget(spin,7+k,1,1,2);
                  QObject::connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,k](double value) {int i=_choice->currentIndex();if(i<0)return; _nodes[i].locked=true;if(k<2) {if(k==0)_nodes[i].offset.setX(value);else _nodes[i].offset.setY(value);} else {if(k==2)_nodes[i].tangent.setX(value);else _nodes[i].tangent.setY(value);}commit(i);});
                  }
            _span=new QComboBox(_details);_span->setObjectName("freeSlurSpan");grid->addWidget(new QLabel(tr("调整区段"),_details),3,0);grid->addWidget(_span,3,1,1,2);
            _route=new QComboBox(_details);_route->setObjectName("freeSlurRoute");_route->addItems({tr("自动"),tr("从音符上方经过"),tr("从音符下方经过"),tr("各段分别设置")});
            if(auto model=qobject_cast<QStandardItemModel*>(_route->model()))model->item(3)->setEnabled(false);grid->addWidget(_route,4,0,1,3);
            connect(_span,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){showRoute();});
            connect(_route,QOverload<int>::of(&QComboBox::activated),this,[this](int selected){
                  if(!_owner||selected>2)return;const int direction=selected==1?-1:selected==2?1:0;
                  if(_span->currentIndex()==0)mscore->performanceEditor()->queueProperty(_owner,Pid::SLUR_DIRECTION,QVariant::fromValue(direction<0?Direction::UP:direction>0?Direction::DOWN:Direction::AUTO));
                  if(_nodes.isEmpty()){
                        if(!direction)return;FreeSlurNode node{.5,{}, {},false};for(auto s:_owner->spannerSegments())if(toSlurSegment(s)->displayedFreeNode(.5,&node))break;
                        node.locked=false;node.before=node.after=direction;_nodes.append(node);
                        }
                  else if(_span->currentIndex()==0){bool changed=_nodes.back().after!=direction;for(auto& node:_nodes){changed|=node.before!=direction;node.before=direction;}if(!changed)return;_nodes.back().after=direction;}
                  else {const int i=_span->currentIndex()-1;int& value=i<_nodes.size()?_nodes[qMax(0,i)].before:_nodes.back().after;if(value==direction)return;value=direction;}
                  commit(_choice->currentIndex());
                  });
            _smoothSpan=new QToolButton(_details);_smoothSpan->setObjectName("freeSlurSmoothSpan");_smoothSpan->setText(tr("整理这一段"));grid->addWidget(_smoothSpan,5,0,1,3);
            _smoothSpan->setToolTip(tr("保持这一段两端点的位置，整理共用切线，让相邻区段平滑相接。可撤销。"));
            connect(_smoothSpan,&QToolButton::clicked,this,[this]{
                  const int span=_span->currentIndex()-1;bool changed=false;const int first=span<0?0:span-1,last=span<0?_nodes.size()-1:span;
                  for(int i=first;i<=last;++i)if(i>=0&&i<_nodes.size()){
                        FreeSlurNode shown;if(!FreeSlur::smoothNode(_owner,i,&shown)||std::abs(shown.position-_nodes[i].position)>1e-7)continue;
                        _nodes[i].offset=shown.offset;_nodes[i].tangent=shown.tangent;_nodes[i].locked=true;changed=true;
                        }
                  if(changed)commit(_choice->currentIndex());
                  });
            auto hint=new QLabel(tr("双击圆滑线后，拖点改变位置，拖点两侧的手柄改变弯曲方向。在“调整区段”中选择两点之间的一段，再指定从上方或下方经过。“整理这一段”保留端点并整理切线。点的位置按整条线的时间百分比保存；数字名称可拖动，Shift精调、Esc取消。"),this);hint->setWordWrap(true);layout->addWidget(hint,4,0,1,3);
            }
      void sync()
            {
            const bool single=_inspector->el()->size()==1;
            auto element=_inspector->element(); if(!element->isSlurSegment()){hide();return;}
            auto owner=toSlurSegment(element)->slur();
            if(_owner!=owner || !mscore->performanceEditor()->hasPending()) {_owner=owner;_nodes=owner->freeNodes();}
            for(int i=0;i<2;++i){auto box=i?_endSide:_startSide;QSignalBlocker block(box);const int side=i?owner->freeEndSide():owner->freeStartSide();box->setCurrentIndex(side<0?1:side>0?2:0);box->setEnabled(single&&owner->freeMode());}
            _details->setEnabled(single && owner->freeMode());_automatic->setEnabled(single && owner->freeMode());
            if(!mscore->performanceEditor()->hasPending()) {QSignalBlocker block(_automatic);_automatic->setChecked(owner->freeAuto());}
            int collisions=0;bool limited=false;for(auto s:owner->spannerSegments()) {auto segment=toSlurSegment(s);collisions+=segment->freeCollisions();limited|=segment->freeLimited();}
            bool directed=false;for(const auto& node:_nodes)directed|=node.before||node.after;
            _result->setText(!owner->freeMode()?tr("开启自由曲线，可添加点和调整每段走向。"):(limited||collisions)?tr("此处仍拥挤，可调整区段方向，或拖动点和切线。")
                  :directed?tr("区段已按指定方向整理，可继续拖动手柄微调。")
                  :!owner->freeAuto()?tr("拖动点改变位置，拖动两侧手柄改变弯曲方向。")
                  :!owner->autoplace()?tr("开启顶部的“自动布局”，可按附近音符整理形状。")
                  :tr("曲线已整理，手动设置的点和切线保持当前位置。"));
            refresh(_choice->currentIndex()<0 ? 0:_choice->currentIndex());
            }
      };
}
#endif
