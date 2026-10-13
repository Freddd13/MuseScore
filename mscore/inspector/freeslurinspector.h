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
namespace Ms {
class FreeSlurInspector : public QWidget {
      Inspector* _inspector;
      Slur* _owner = nullptr;
      QVector<FreeSlurNode> _nodes;
      QComboBox* _choice;
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
            _choice->setCurrentIndex(qBound(-1,selected,_nodes.size()-1)); showNode();
            }
      void showNode()
            {
            const int i=_choice->currentIndex();
            QSignalBlocker lockBlock(_locked);_locked->setEnabled(i>=0);_locked->setChecked(i>=0 && _nodes[i].locked);
            for (int k=0;k<_values.size();++k) {
                  QSignalBlocker block(_values[k]); _values[k]->setEnabled(i>=0);
                  if(i>=0) _values[k]->setValue(k==0 ? _nodes[i].offset.x() : k==1 ? _nodes[i].offset.y() : k==2 ? _nodes[i].tangent.x() : _nodes[i].tangent.y());
                  }
            }
      QToolButton* buttonForRestore(QWidget* host,QGridLayout* grid)
            {
            auto button=new QToolButton(host);button->setText(tr("恢复自动形状"));button->setObjectName("freeSlurRestoreAuto");grid->addWidget(button,8,0,1,3);
            button->setToolTip(tr("清除这条线的中间点并重新自动避让；可撤销。起点、终点和原有手柄偏移保留。"));return button;
            }
   public:
      QCheckBox* mode;
      FreeSlurInspector(QWidget* host,Inspector* inspector) : QWidget(host),_inspector(inspector)
            {
            setObjectName("freeSlurPanel"); host->layout()->addWidget(this);
            auto layout=new QGridLayout(this); mode=new QCheckBox(tr("自由曲线"),this); mode->setObjectName("freeSlurMode"); layout->addWidget(mode,0,0,1,3);
            _details=new QWidget(this); layout->addWidget(_details,1,0,1,3); auto grid=new QGridLayout(_details);
            _automatic=new QCheckBox(tr("自动避让音符"),this);_automatic->setObjectName("freeSlurAuto");layout->addWidget(_automatic,3,0,1,3);
            _result=new QLabel(this);_result->setWordWrap(true);layout->addWidget(_result,4,0,1,3);
            connect(_automatic,&QCheckBox::clicked,this,[this](bool value) {
                  if(_owner)mscore->performanceEditor()->queueProperty(_owner,Pid::FREE_SLUR_AUTO,value);
                  });
            _locked=new QCheckBox(tr("锁定这个点和切线"),_details);_locked->setObjectName("freeSlurLock");grid->addWidget(_locked,7,0,1,3);
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
                  nodes.append(node); QVector<FreeSlurNode> valid;
                  if(!FreeSlur::decode(FreeSlur::encode(nodes),&valid)) {_position->setToolTip(tr("该位置已有节点；请调整百分比。"));return;}
                  _nodes=valid; int i=0;while(i<_nodes.size() && _nodes[i].position<_position->value()/100) ++i; commit(i);
                  });
            QObject::connect(remove,&QToolButton::clicked,this,[this] {int i=_choice->currentIndex();if(i<0)return;_nodes.removeAt(i);commit(qMin(i,_nodes.size()-1));});
            QObject::connect(smooth,&QToolButton::clicked,this,[this] {int i=_choice->currentIndex();if(i<0)return;_nodes[i].tangent={};commit(i);});
            QObject::connect(reset,&QToolButton::clicked,this,[this] {_nodes.clear();commit(-1);});
            const QStringList labels={tr("水平偏移"),tr("竖直偏移"),tr("水平切线"),tr("竖直切线")};
            for(int k=0;k<4;++k) {
                  auto spin=new QDoubleSpinBox(_details);spin->setObjectName(QString("freeSlurValue%1").arg(k));spin->setRange(k<2 ? -100:-400,k<2 ? 100:400);spin->setSingleStep(.1);spin->setDecimals(2);spin->setSuffix(tr(" sp"));spin->setKeyboardTracking(false);_values.append(spin);
                  auto label=new ScrubPropertyLabel(labels[k],spin,_details);label->setObjectName(spin->objectName()+"Label");grid->addWidget(label,3+k,0);grid->addWidget(spin,3+k,1,1,2);
                  QObject::connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,k](double value) {int i=_choice->currentIndex();if(i<0)return; _nodes[i].locked=true;if(k<2) {if(k==0)_nodes[i].offset.setX(value);else _nodes[i].offset.setY(value);} else {if(k==2)_nodes[i].tangent.setX(value);else _nodes[i].tangent.setY(value);}commit(i);});
                  }
            auto hint=new QLabel(tr("双击圆滑线进入编辑，拖动中间节点或两侧切线。节点位置按整条线的时间百分比保存；偏移以谱间距为单位。数字名称可拖动，Shift精调、Esc取消。手动调整会锁定该点；取消锁定后自动避让才会移动它。多选时只共同设置模式。"),this);hint->setWordWrap(true);layout->addWidget(hint,2,0,1,3);
            }
      void sync()
            {
            const bool single=_inspector->el()->size()==1;
            auto element=_inspector->element(); if(!element->isSlurSegment()){hide();return;}
            auto owner=toSlurSegment(element)->slur();
            if(_owner!=owner || !mscore->performanceEditor()->hasPending()) {_owner=owner;_nodes=owner->freeNodes();}
            _details->setEnabled(single && owner->freeMode());_automatic->setEnabled(single && owner->freeMode());
            if(!mscore->performanceEditor()->hasPending()) {QSignalBlocker block(_automatic);_automatic->setChecked(owner->freeAuto());}
            int collisions=0;bool limited=false;for(auto s:owner->spannerSegments()) {auto segment=toSlurSegment(s);collisions+=segment->freeCollisions();limited|=segment->freeLimited();}
            _result->setText(!owner->freeMode() ? tr("开启自由曲线后可使用自动避让。") : !owner->freeAuto() ? tr("手动形状：不会自动移动中间点。") : !owner->autoplace() ? tr("请先开启圆滑线的“自动布局”。") : limited ? tr("内容过密，保留形状；请手动调整。") : collisions ? tr("仍有拥挤位置：锁定点或高度限制可能使避让无法完成。") : tr("已自动避让；锁定点和两端位置保留。"));
            refresh(_choice->currentIndex()<0 ? 0:_choice->currentIndex());
            }
      };
}
#endif
