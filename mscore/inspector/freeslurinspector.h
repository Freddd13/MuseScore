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
            for (int k=0;k<_values.size();++k) {
                  QSignalBlocker block(_values[k]); _values[k]->setEnabled(i>=0);
                  if(i>=0) _values[k]->setValue(k==0 ? _nodes[i].offset.x() : k==1 ? _nodes[i].offset.y() : k==2 ? _nodes[i].tangent.x() : _nodes[i].tangent.y());
                  }
            }
   public:
      QCheckBox* mode;
      FreeSlurInspector(QWidget* host,Inspector* inspector) : QWidget(host),_inspector(inspector)
            {
            setObjectName("freeSlurPanel"); host->layout()->addWidget(this);
            auto layout=new QGridLayout(this); mode=new QCheckBox(tr("自由曲线"),this); mode->setObjectName("freeSlurMode"); layout->addWidget(mode,0,0,1,3);
            _details=new QWidget(this); layout->addWidget(_details,1,0,1,3); auto grid=new QGridLayout(_details);
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
                  auto nodes=_nodes; nodes.append({_position->value()/100,{}, {}}); QVector<FreeSlurNode> valid;
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
                  QObject::connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,k](double value) {int i=_choice->currentIndex();if(i<0)return; if(k<2) {if(k==0)_nodes[i].offset.setX(value);else _nodes[i].offset.setY(value);} else {if(k==2)_nodes[i].tangent.setX(value);else _nodes[i].tangent.setY(value);}commit(i);});
                  }
            auto hint=new QLabel(tr("双击圆滑线进入编辑，拖动中间节点或两侧切线。节点位置按整条线的时间百分比保存；偏移以谱间距为单位。数字名称可拖动，Shift精调、Esc取消。多选时只共同设置模式。"),this);hint->setWordWrap(true);layout->addWidget(hint,2,0,1,3);
            }
      void sync()
            {
            const bool single=_inspector->el()->size()==1;
            auto element=_inspector->element(); if(!element->isSlurSegment()){hide();return;}
            auto owner=toSlurSegment(element)->slur();
            if(_owner!=owner || !mscore->performanceEditor()->hasPending()) {_owner=owner;_nodes=owner->freeNodes();}
            _details->setEnabled(single && owner->freeMode()); refresh(_choice->currentIndex()<0 ? 0:_choice->currentIndex());
            }
      };
}
#endif
