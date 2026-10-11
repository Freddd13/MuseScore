// SPDX-License-Identifier: GPL-2.0-or-later
#include "tapwrite.h"
#include "mscore/musescore.h"
#include "mscore/seq.h"
#include "mscore/performanceeditor/parameteredit.h"
#include "libmscore/score.h"
#include "libmscore/input.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/select.h"
#include "libmscore/tempotext.h"
#include <QDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMessageBox>
namespace Ms {
void showTapWriteDialog(Score* score,double bpm){
 if(!score||score->readOnly()||score->isPlaying()||(seq&&(!seq->backgroundRenderingIdle()||seq->isPlaying())))return;
 QDialog dialog(mscore);dialog.setWindowTitle(QObject::tr("Tap 写入指定速度位置"));QFormLayout layout(&dialog);
 QLabel value(QObject::tr("四分音符 BPM = %1").arg(bpm,0,'f',2));layout.addRow(&value);
 QSpinBox measure;measure.setRange(1,score->nmeasures());QComboBox positions;
 int selectedTick=score->inputState().tick().ticks();TempoText* selected=nullptr;
 const auto elements=score->selection().elements();if(elements.size()==1&&elements.front()->isTempoText()){selected=toTempoText(elements.front());selectedTick=selected->tick().ticks();}
 auto current=score->tick2measure(Fraction::fromTicks(selectedTick));measure.setValue(current?current->no()+1:1);
 auto update=[&]{
  positions.clear();auto m=score->firstMeasure();for(int i=1;m&&i<measure.value();++i)m=m->nextMeasure();if(!m)return;
  for(auto seg=m->first(SegmentType::ChordRest);seg&&seg->measure()==m;seg=seg->next(SegmentType::ChordRest)){
   int tick=seg->tick().ticks();double beat=1.+double(tick-m->tick().ticks())/(DIVISION*4./m->timesig().denominator());
   QString label=QObject::tr("第 %1 拍").arg(beat,0,'f',3);
   for(auto e:seg->annotations())if(e->isTempoText())label+=QObject::tr(" · 已有速度");
   positions.addItem(label,tick);
  }
  int index=positions.findData(selectedTick);if(index>=0)positions.setCurrentIndex(index);
 };
 update();QObject::connect(&measure,QOverload<int>::of(&QSpinBox::valueChanged),&dialog,[&](int){update();});
 layout.addRow(QObject::tr("小节"),&measure);layout.addRow(QObject::tr("位置"),&positions);
 QLabel protection(selected?QObject::tr("仅在选中原记号的位置修改；保留其拍单位和说明：%1").arg(selected->plainText()):QObject::tr("新建可见速度记号；已有记号须先直接选中。"));protection.setWordWrap(true);layout.addRow(&protection);
 QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addRow(&buttons);
 QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&]{
  if(positions.currentIndex()<0)return;QString error;int tick=positions.currentData().toInt();
  if(!ParameterEdit::writeTapTempo(score,tick,bpm,selected&&selected->tick().ticks()==tick,&error)){QMessageBox::information(&dialog,QObject::tr("Tap 写谱"),error.isEmpty()?QObject::tr("无变化；请核对测量与目标。"):error);return;}
  dialog.accept();
 });QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.exec();
}
}
