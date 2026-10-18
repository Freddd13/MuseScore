// SPDX-License-Identifier: GPL-2.0-or-later
#include "midicroppanel.h"
#include "mscore/workflow/featuregate.h"
#include "gateaudition.h"
#include "libmscore/playbacktiming.h"
#include "libmscore/repeatlist.h"
#include "mscore/performanceeditor/performanceeditor.h"
#include "mscore/scoreview.h"
#include "mscore/workflow/workflowcontext.h"
#include <QCryptographicHash>
#include "audio/exports/exportmidi.h"
#include "libmscore/score.h"
#include "libmscore/part.h"
#include "libmscore/staff.h"
#include "libmscore/measure.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "mscore/workflow/positionhint.h"
#include "libmscore/synthesizerstate.h"
#include "mscore/musescore.h"
#include "mscore/preferences.h"
#include "mscore/seq.h"
#include <QtWidgets>
#include <QBuffer>
#include <QSaveFile>
#include <QtConcurrent>
#include <atomic>
#include <memory>

namespace Ms {
namespace {
QString reasonText(const QString& code) {
      static const QMap<QString,QString> names {
            {"manual-release",MidiCropPanel::tr("已手动调整这次导出的松手位置")},
            {"manual-original",MidiCropPanel::tr("已手动保留原按键长度")},
            {"pedal-release",MidiCropPanel::tr("同组音自然松手，踏板继续保持声音")},
            {"hand-release",MidiCropPanel::tr("为弹下一组音提前松手")},
            {"hand-conflict",MidiCropPanel::tr("分手说明有冲突或未确认，保留原长度")},
            {"fingering-uncertain",MidiCropPanel::tr("指法有多个解释，保留原长度")},
            {"unselected",MidiCropPanel::tr("未选择这个乐器")},
            {"outside-range",MidiCropPanel::tr("不在选定小节内，或无法对应原音")},
            {"unselected-voice",MidiCropPanel::tr("未选择这个声部")},
            {"protected-melody",MidiCropPanel::tr("按旋律或声部保护设置保留")},
            {"unknown-source",MidiCropPanel::tr("不能唯一对应来源音，保留")},
            {"manual-performance",MidiCropPanel::tr("手调演奏、小音符或特殊奏法，保留")},
            {"ambiguous-retrigger",MidiCropPanel::tr("同音重触关系不清楚，保留")},
            {"excluded",MidiCropPanel::tr("已手动排除")},
            {"short-note",MidiCropPanel::tr("原音已短于最小长度")},
            {"no-shortening",MidiCropPanel::tr("没有合适的提前松手位置")},
            {"no-pedal",MidiCropPanel::tr("没有可保持发声的踏板")},
            {"unsupported-pedal",MidiCropPanel::tr("踏板或控制事件不能可靠判断，保留")},
            {"pedal-boundary",MidiCropPanel::tr("跨过抬踏板位置，保留")}
            };
      return names.value(code,code);
      }
QString pitchText(int pitch) {
      static const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
      return QString::fromLatin1(names[qBound(0,pitch,127)%12])+QString::number(pitch/12-1);
      }
#include "gateroll.inc"
// QTableView requests only visible cells; no widget is allocated per note.
class GateModel : public QAbstractTableModel {
   public:
      QVector<MidiGateNote> rows;
      QVector<int> visible;
      MidiGateProcessor clock;
      GateModel(QObject* parent):QAbstractTableModel(parent) {}
      int rowCount(const QModelIndex& p={}) const override {return p.isValid()?0:visible.size();}
      int columnCount(const QModelIndex& p={}) const override {return p.isValid()?0:8;}
      QVariant headerData(int section,Qt::Orientation orientation,int role) const override {
            if(role!=Qt::DisplayRole || orientation!=Qt::Horizontal)return {};
            return QStringList{MidiCropPanel::tr("音高"),MidiCropPanel::tr("播放时间"),MidiCropPanel::tr("原长度（ms）"),MidiCropPanel::tr("新长度（ms）"),MidiCropPanel::tr("小节／谱表／声部"),MidiCropPanel::tr("处理原因"),MidiCropPanel::tr("原先手指占用"),MidiCropPanel::tr("提前松手后")}.value(section);
            }
      QVariant data(const QModelIndex& index,int role) const override {
            if(!index.isValid() || (role!=Qt::DisplayRole && role!=Qt::ToolTipRole))return {};
            const auto& n=rows[visible[index.row()]];
            if(role==Qt::ToolTipRole) {
                  QString side=n.hand<0?tr("分手待确认"):n.hand==0?tr("左手"):tr("右手");if(n.hand>=0 && n.handInferred)side+=tr("（自动建议）");
                  return pitchText(n.pitch)+" · "+side+"\n"+reasonText(n.reason)+(n.handWarning.isEmpty()?QString():"\n"+n.handWarning)+(n.beforeKeys<0?QString():QString::fromUtf8("\n%1键 / %2半音 → %3键 / %4半音").arg(n.beforeKeys).arg(n.beforeSpan).arg(n.afterKeys).arg(n.afterSpan));
                  }
            switch(index.column()) {
                  case 0:return pitchText(n.pitch);
                  case 1:return QString::number(clock.durationMs(0,n.on)/1000.,'f',3)+" s";
                  case 2:return qRound(clock.durationMs(n.on,n.off));
                  case 3:return qRound(clock.durationMs(n.on,n.newOff));
                  case 4:return n.sourceMeasure<0?QString("?"):QString("%1 / %2 / %3").arg(n.sourceMeasure).arg(n.staff+1).arg(n.voice%4+1);
                  case 5:return reasonText(n.reason)+(n.handWarning.isEmpty()?QString():QString(" · ")+n.handWarning);
                  case 6:return n.beforeKeys<0?QString("—"):(n.hand==0?tr("左手"):tr("右手"))+(n.handInferred?tr("（自动建议）"):QString())+QString::fromUtf8(" · %1键 / %2半音").arg(n.beforeKeys).arg(n.beforeSpan);
                  default:return n.afterKeys<0?QString("—"):QString::fromUtf8("%1键 / %2半音").arg(n.afterKeys).arg(n.afterSpan);
                  }
            }
      void setRows(const QVector<MidiGateNote>& value,const MidiGateData& source,int staff,int filter) {
            beginResetModel();rows=value;clock.setClock(source);visible.clear();
            for(int i=0;i<rows.size();++i)if((staff<0 || rows[i].staff==staff) && (filter==0 || (filter==1)==(rows[i].newOff<rows[i].off)))visible.append(i);
            endResetModel();
            }
      };
}
MidiCropPanel::MidiCropPanel(QWidget* parent,std::function<QList<Score*>()> scores):QWidget(parent),_scores(std::move(scores)) {
      auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,6,0,0);
      setVisible(personalFeaturesEnabled());
      _enabled=new QCheckBox(tr("人性化裁剪：保守踏板模式"),this);
      _enabled->setChecked(preferences.getBool("export/midi/cropEnabled"));layout->addWidget(_enabled);
      auto buttons=new QHBoxLayout;
      auto config=new QPushButton(tr("裁剪设置…"),this);config->setObjectName("midiCropSettingsButton");auto preview=new QPushButton(tr("查看对比…"),this);preview->setObjectName("midiCropCompare");
      buttons->addWidget(config);buttons->addWidget(preview);buttons->addStretch();layout->addLayout(buttons);
      _original=new QCheckBox(tr("同时导出未裁剪版本"),this);_original->setChecked(preferences.getBool("export/midi/cropOriginal"));layout->addWidget(_original);
      _gate.lengthRatio=preferences.getDouble("export/midi/cropLength");_gate.nextAttackRatio=preferences.getDouble("export/midi/cropNext");
      _gate.minimumMs=preferences.getDouble("export/midi/cropMinimumMs");_gate.jitter=preferences.getDouble("export/midi/cropJitter");_gate.seed=preferences.getInt("export/midi/cropSeed");
      QSettings handSettings;_gate.releaseLeadMs=handSettings.value("export/midi/releaseLeadMs",60.).toDouble();
      for(int side=0;side<2;++side) {const auto key=QString("export/midi/hand%1/").arg(side);_gate.hands[side].comfort=handSettings.value(key+"comfort",14).toInt();_gate.hands[side].maximum=handSettings.value(key+"maximum",15).toInt();_gate.hands[side].white=handSettings.value(key+"white",16).toInt();}
      _fromMeasure=preferences.getInt("export/midi/cropFromMeasure");_untilMeasure=preferences.getInt("export/midi/cropUntilMeasure");_gate.protectMelody=preferences.getBool("export/midi/cropProtectMelody");
      const QString voiceText=preferences.getString("export/midi/cropVoices"),protectText=preferences.getString("export/midi/cropProtectedVoices");
      for(auto v:voiceText.split(',',Qt::SkipEmptyParts))if(v.toInt()>=0&&v.toInt()<4)_gate.voices.insert(v.toInt());
      for(auto v:protectText.split(',',Qt::SkipEmptyParts))if(v.toInt()>=0&&v.toInt()<4)_gate.protectedVoices.insert(v.toInt());
      connect(_enabled,&QCheckBox::toggled,this,[this](bool){savePreferences();});
      connect(_original,&QCheckBox::toggled,this,[this](bool){savePreferences();});
      connect(config,&QPushButton::clicked,this,&MidiCropPanel::settings);
      connect(preview,&QPushButton::clicked,this,&MidiCropPanel::compare);
      }
bool MidiCropPanel::enabled() const {return personalFeaturesEnabled() && _enabled->isChecked();}
bool MidiCropPanel::includeOriginal() const {return personalFeaturesEnabled() && _original->isChecked();}
MidiExportOptions MidiCropPanel::options(Score* score) const {
      MidiExportOptions result;result.crop=enabled();result.gate=_gate;
      if(_fromMeasure>0){result.gate.scoreStart=INT_MAX;result.gate.scoreEnd=INT_MAX;int bar=1;for(auto m=score->firstMeasure();m;m=m->nextMeasure(),++bar){if(bar==_fromMeasure)result.gate.scoreStart=m->tick().ticks();if(bar==_untilMeasure)result.gate.scoreEnd=m->endTick().ticks();}if(_untilMeasure<_fromMeasure||_untilMeasure>score->nmeasures())result.gate.scoreStart=result.gate.scoreEnd=INT_MAX;}
      result.gate.tracks=_tracks.value(score);result.gate.protectedTracks=_protectedTracks.value(score);
      result.gate.excluded=_exclusions.value(score);if(_releaseStates.value(score)==score->masterScore()->state())result.gate.releaseOverrides=_releaseOverrides.value(score);result.gate.staves=_staves.value(score);return result;
      }
void MidiCropPanel::savePreferences() const {
      if (!personalFeaturesEnabled()) return;
      QSettings handSettings;handSettings.setValue("export/midi/releaseLeadMs",_gate.releaseLeadMs);
      for(int side=0;side<2;++side) {const auto key=QString("export/midi/hand%1/").arg(side);handSettings.setValue(key+"comfort",_gate.hands[side].comfort);handSettings.setValue(key+"maximum",_gate.hands[side].maximum);handSettings.setValue(key+"white",_gate.hands[side].white);}handSettings.sync();
      preferences.setPreference("export/midi/cropFromMeasure",_fromMeasure);preferences.setPreference("export/midi/cropUntilMeasure",_untilMeasure);preferences.setPreference("export/midi/cropProtectMelody",_gate.protectMelody);
      auto text=[](const QSet<int>& values){QList<int> list=values.values();std::sort(list.begin(),list.end());QStringList result;for(int v:list)result<<QString::number(v);return result.join(',');};
      preferences.setPreference("export/midi/cropVoices",text(_gate.voices));preferences.setPreference("export/midi/cropProtectedVoices",text(_gate.protectedVoices));
      preferences.setPreference("export/midi/cropEnabled",enabled());preferences.setPreference("export/midi/cropOriginal",includeOriginal());
      preferences.setPreference("export/midi/cropLength",_gate.lengthRatio);preferences.setPreference("export/midi/cropNext",_gate.nextAttackRatio);
      preferences.setPreference("export/midi/cropMinimumMs",_gate.minimumMs);preferences.setPreference("export/midi/cropJitter",_gate.jitter);preferences.setPreference("export/midi/cropSeed",int(_gate.seed));
      }
void MidiCropPanel::settings() {
      QDialog dialog(this);dialog.setObjectName("midiCropSettings");dialog.setWindowTitle(tr("MIDI松手设置"));auto form=new QFormLayout(&dialog);
      auto length=new QDoubleSpinBox(&dialog);length->setRange(10,100);length->setValue(_gate.lengthRatio*100);length->setSuffix(" %");form->addRow(tr("自然松手长度比例"),length);
      auto next=new QDoubleSpinBox(&dialog);next->setRange(10,100);next->setValue(_gate.nextAttackRatio*100);next->setSuffix(" %");form->addRow(tr("相邻起音间隔比例"),next);
      auto minimum=new QSpinBox(&dialog);minimum->setRange(1,5000);minimum->setValue(int(_gate.minimumMs));minimum->setSuffix(" ms");form->addRow(tr("导出最短音长"),minimum);
      auto lead=new QSpinBox(&dialog);lead->setObjectName("cropReleaseLead");lead->setRange(0,500);lead->setSuffix(" ms");lead->setValue(qRound(_gate.releaseLeadMs));form->addRow(tr("为下一次按键提前松手"),lead);
      PianoHands::Limits editedHands[2]={_gate.hands[0],_gate.hands[1]};
      auto hands=new QPushButton(tr("设置左右手的个人跨度…"),&dialog);hands->setObjectName("cropHandLimitsButton");form->addRow(hands);
      connect(hands,&QPushButton::clicked,&dialog,[&] {
            QDialog setup(&dialog);setup.setObjectName("cropHandLimits");setup.setWindowTitle(tr("个人跨度"));auto layout=new QFormLayout(&setup);QSpinBox* values[2][3];
            for(int side=0;side<2;++side)for(int k=0;k<3;++k) {auto spin=new QSpinBox(&setup);values[side][k]=spin;spin->setObjectName(QString("cropHand%1Value%2").arg(side).arg(k));spin->setRange(1,36);spin->setSuffix(tr(" 半音"));spin->setValue(k==0?editedHands[side].comfort:k==1?editedHands[side].maximum:editedHands[side].white);layout->addRow((side==0?tr("左手 · "):tr("右手 · "))+QStringList{tr("舒适跨度"),tr("最大跨度"),tr("两端白键最大跨度")}[k],spin);}
            auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&setup);layout->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&setup,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&setup,&QDialog::reject);
            if(setup.exec()==QDialog::Accepted)for(int side=0;side<2;++side)editedHands[side]={values[side][0]->value(),values[side][1]->value(),values[side][2]->value()};
            });
      auto jitter=new QDoubleSpinBox(&dialog);jitter->setRange(0,5);jitter->setValue(_gate.jitter*100);jitter->setSuffix(" %");form->addRow(tr("成组随机变化 ±（0关闭）"),jitter);
      auto seed=new QSpinBox(&dialog);seed->setRange(0,INT_MAX);seed->setValue(int(_gate.seed));form->addRow(tr("随机种子（相同设置可复现）"),seed);
      auto from=new QSpinBox(&dialog),until=new QSpinBox(&dialog);from->setObjectName("cropFromMeasure");until->setObjectName("cropUntilMeasure");from->setRange(0,99999);until->setRange(0,99999);from->setSpecialValueText(tr("全谱"));until->setSpecialValueText(tr("全谱"));from->setValue(_fromMeasure);until->setValue(_untilMeasure);form->addRow(tr("开始小节（按谱面顺序）"),from);form->addRow(tr("结束小节"),until);
      auto voiceRow=new QWidget(&dialog),protectRow=new QWidget(&dialog);auto voiceLayout=new QHBoxLayout(voiceRow),protectLayout=new QHBoxLayout(protectRow);voiceLayout->setContentsMargins(0,0,0,0);protectLayout->setContentsMargins(0,0,0,0);QVector<QCheckBox*> voices,protectedVoices;
      for(int i=0;i<4;++i){auto v=new QCheckBox(QString::number(i+1),voiceRow),p=new QCheckBox(QString::number(i+1),protectRow);v->setChecked(_gate.voices.isEmpty()||_gate.voices.contains(i));p->setChecked(_gate.protectedVoices.contains(i));voiceLayout->addWidget(v);protectLayout->addWidget(p);voices.append(v);protectedVoices.append(p);}
      form->addRow(tr("处理声部"),voiceRow);form->addRow(tr("保护声部"),protectRow);auto melody=new QCheckBox(tr("保护各钢琴部分的最高起音线"),&dialog);melody->setObjectName("cropProtectMelody");melody->setChecked(_gate.protectMelody);form->addRow(melody);
      auto info=new QLabel(tr("先处理旧音占手，再对其余音加入成组松手变化。提前时间只是你的操作设置，不代表精确的演奏难度。只有同一受支持踏板区间内的释放点可改变；起音和控制器不变。"),&dialog);info->setWordWrap(true);form->addRow(info);
      auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
      connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
      if(dialog.exec()==QDialog::Accepted) {
            QSet<int> selected,protectedSet;for(int i=0;i<4;++i){if(voices[i]->isChecked())selected.insert(i);if(protectedVoices[i]->isChecked())protectedSet.insert(i);}if(selected.isEmpty()||(from->value()>0&&until->value()<from->value())){QMessageBox::warning(this,tr("MIDI松手处理"),tr("请至少选择一个声部，并填写有效的小节范围。"));return;}
            _fromMeasure=from->value();_untilMeasure=_fromMeasure?until->value():0;_gate.voices=selected;_gate.protectedVoices=protectedSet;_gate.protectMelody=melody->isChecked();
            _gate.lengthRatio=length->value()/100.;_gate.nextAttackRatio=next->value()/100.;_gate.minimumMs=minimum->value();_gate.releaseLeadMs=lead->value();_gate.hands[0]=editedHands[0];_gate.hands[1]=editedHands[1];_gate.jitter=jitter->value()/100.;_gate.seed=quint32(seed->value());savePreferences();}
      }
bool MidiCropPanel::write(Score* score,const QString& path,const QString& original) {
      if(seq && (seq->isPlaying()||!seq->backgroundRenderingIdle())) {QMessageBox::information(this,tr("MIDI松手处理"),tr("请先停播，等待乐谱准备完成后再生成MIDI。"));return false;}
      if(_fromMeasure>0&&(_untilMeasure>score->nmeasures()||_untilMeasure<_fromMeasure)){QMessageBox::warning(this,tr("MIDI松手处理"),tr("%1中不存在所填的小节范围，请调整后再导出。").arg(score->title()));return false;}
      if(!_releaseOverrides.value(score).isEmpty()&&_releaseStates.value(score)!=score->masterScore()->state()){QMessageBox::information(this,tr("MIDI松手处理"),tr("乐谱已改变，请打开“查看对比”更新手动松手位置，再导出。"));return false;}
      ExportMidi exporter(score);QBuffer buffer;buffer.open(QIODevice::WriteOnly);
      if(!exporter.write(&buffer,preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),preferences.getBool(PREF_IO_MIDI_EXPORTRPNS),mscore->synthesizerState(),options(score)))return false;
      auto store=[&](const QString& name,const QByteArray& bytes) {QSaveFile f(name);if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()) {QMessageBox::warning(this,tr("MIDI导出"),tr("无法写入%1：%2").arg(name,f.errorString()));return false;}return true;};
      if(!original.isEmpty()) {
            QBuffer bytes;bytes.open(QIODevice::WriteOnly);
            if(exporter.gateBaseline.write(&bytes) || !store(original,bytes.data()))return false;
            }
      return store(path,buffer.data());
      }
void MidiCropPanel::voiceSelection(Score* score) {
      QDialog dialog(this);dialog.setObjectName("midiCropStaffVoices");dialog.setWindowTitle(tr("%1：声部选择与保护").arg(score->title()));dialog.resize(510,420);auto layout=new QVBoxLayout(&dialog);
      auto label=new QLabel(tr("可分别选择每个谱表的声部，以及需要保护的声部。这张表用于本次导出的当前谱子；通用范围设置单独保存。"),&dialog);label->setWordWrap(true);layout->addWidget(label);
      auto tree=new QTreeWidget(&dialog);tree->setObjectName("cropStaffVoicesTree");tree->setColumnCount(3);tree->setHeaderLabels({tr("谱表／声部"),tr("处理"),tr("保护")});layout->addWidget(tree);
      const auto selected=_tracks.value(score),protect=_protectedTracks.value(score);QVector<QTreeWidgetItem*> rows;
      for(int staff=0;staff<score->nstaves();++staff)for(int voice=0;voice<VOICES;++voice){int track=staff*VOICES+voice;auto row=new QTreeWidgetItem(tree,{tr("谱表%1 · 声部%2").arg(staff+1).arg(voice+1)});row->setData(0,Qt::UserRole,track);row->setCheckState(1,selected.isEmpty()||selected.contains(track)?Qt::Checked:Qt::Unchecked);row->setCheckState(2,protect.contains(track)?Qt::Checked:Qt::Unchecked);rows.append(row);}
      tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
      if(dialog.exec()!=QDialog::Accepted)return;QSet<int> crop,protectedSet;for(auto row:rows){int track=row->data(0,Qt::UserRole).toInt();if(row->checkState(1)==Qt::Checked)crop.insert(track);if(row->checkState(2)==Qt::Checked)protectedSet.insert(track);}if(crop.isEmpty()){QMessageBox::warning(this,tr("MIDI松手处理"),tr("请至少选择一个声部。"));return;}_tracks[score]=crop;_protectedTracks[score]=protectedSet;
      }
void MidiCropPanel::compare() {
      if(seq && (seq->isPlaying()||!seq->backgroundRenderingIdle())) {QMessageBox::information(this,tr("MIDI松手处理"),tr("请先停播，等待乐谱准备完成后再生成MIDI。"));return;}
      const auto scores=_scores();if(scores.isEmpty())return;
      QDialog dialog(this);dialog.setObjectName("midiCropComparison");dialog.setWindowTitle(tr("MIDI松手前后对比"));dialog.resize(920,650);
      auto layout=new QVBoxLayout(&dialog);auto bar=new QHBoxLayout;
      auto specific=new QPushButton(tr("选择谱表声部…"),&dialog);specific->setObjectName("midiCropStaffVoicesButton");bar->addWidget(specific);
      auto config=new QPushButton(tr("小节、声部与保护…"),&dialog);bar->addWidget(config);
      auto scoreBox=new QComboBox(&dialog);for(auto score:scores)scoreBox->addItem(score->title());bar->addWidget(scoreBox);
      auto part=new QComboBox(&dialog);bar->addWidget(part);auto filter=new QComboBox(&dialog);filter->addItems({tr("全部"),tr("已缩短"),tr("保留")});bar->addWidget(filter);
      auto display=new QComboBox(&dialog);display->addItems({tr("叠加"),tr("上下分列")});bar->addWidget(display);
      auto zoom=new QSlider(Qt::Horizontal,&dialog);zoom->setObjectName("midiCropZoom");zoom->setRange(1,400);zoom->setValue(100);zoom->setToolTip(tr("水平缩放；也可在图上按Ctrl+滚轮，以鼠标位置缩放。Shift+滚轮调整音高行高度。"));bar->addWidget(zoom);layout->addLayout(bar);
      auto canvasPane=new QWidget(&dialog);canvasPane->setObjectName("midiCropCanvasPane");auto canvasLayout=new QVBoxLayout(canvasPane);canvasLayout->setContentsMargins(0,0,0,0);layout->addWidget(canvasPane,3);
      auto caption=new QLabel(tr("细轮廓是原长度，实心条是新长度；下方是踏板。时间尺包含展开的反复和暂停。"),&dialog);caption->setWordWrap(true);canvasLayout->addWidget(caption);
      auto roll=new GateRoll(&dialog);roll->setObjectName("midiCropRoll");canvasLayout->addWidget(roll,1);
      auto detail=new QLabel(tr("点一个音查看松手原因。细轮廓为原长度，强调色实心条为缩短后的长度，灰色为保留的音。"),&dialog);
      detail->setObjectName("midiCropDetail");detail->setWordWrap(true);detail->setMinimumHeight(48);canvasLayout->addWidget(detail);
      auto table=new QTableView(&dialog);auto model=new GateModel(table);table->setModel(model);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table->setColumnWidth(0,65);table->setColumnWidth(1,120);table->setColumnWidth(2,100);table->setColumnWidth(3,100);table->setColumnWidth(4,120);table->setColumnWidth(5,140);table->setColumnWidth(6,145);table->horizontalHeader()->setStretchLastSection(true);layout->addWidget(table,2);
      auto buttons=new QHBoxLayout;auto exclude=new QPushButton(tr("排除／恢复这个音"),&dialog);auto recompute=new QPushButton(tr("重新计算"),&dialog);auto reset=new QPushButton(tr("恢复默认设置"),&dialog);auto close=new QPushButton(tr("返回导出"),&dialog);
      buttons->addWidget(exclude);buttons->addWidget(recompute);buttons->addWidget(reset);buttons->addStretch();buttons->addWidget(close);layout->addLayout(buttons);
      auto targets=new QComboBox(&dialog);auto targetRow=new QHBoxLayout;targetRow->addWidget(new QLabel(tr("处理的钢琴部分："),&dialog));targetRow->addWidget(targets);targetRow->addStretch();layout->insertLayout(1,targetRow);
      auto useForExport=new QCheckBox(tr("导出采用图中的新长度"),canvasPane);useForExport->setObjectName("midiCropUseProposed");useForExport->setChecked(enabled());canvasLayout->addWidget(useForExport);
      connect(useForExport,&QCheckBox::toggled,_enabled,&QCheckBox::setChecked);connect(_enabled,&QCheckBox::toggled,useForExport,&QCheckBox::setChecked);
      auto transport=new QHBoxLayout;canvasLayout->addLayout(transport);
      auto originalPlay=new QPushButton(tr("听原长度"),canvasPane),proposedPlay=new QPushButton(tr("听新长度"),canvasPane),stopPlay=new QPushButton(tr("停止"),canvasPane),fit=new QPushButton(tr("匹配内容"),canvasPane),editorMode=new QPushButton(tr("在演奏编辑器调整…"),canvasPane);
      originalPlay->setObjectName("midiCropOriginalPlay");proposedPlay->setObjectName("midiCropProposedPlay");stopPlay->setObjectName("midiCropStop");fit->setObjectName("midiCropFit");editorMode->setObjectName("midiCropEditorMode");
      for(auto button:{originalPlay,proposedPlay,stopPlay,fit,editorMode})transport->addWidget(button);
      auto playStatus=new QLabel(tr("选择一个音，可以拖实心条的尾端调整松手；右键可恢复自动计算。"),canvasPane);playStatus->setObjectName("midiCropPlaybackStatus");playStatus->setWordWrap(true);canvasLayout->addWidget(playStatus);
      MidiGateData data;QVector<MidiGateNote> rows;bool loading=false,queued=false;QString auditionKey;bool proposedMode=false;
      QPointer<Score> targetScore=scores.front();
      std::function<void(bool)> audition;

      QFutureWatcher<QVector<MidiGateNote>> watcher;
      auto canceled=std::make_shared<std::atomic_bool>(false);
      auto present=[&] {const QString selected=roll->selected>=0&&roll->selected<roll->notes.size()?roll->notes[roll->selected].id:QString();model->setRows(rows,data,part->currentData().toInt(),filter->currentIndex());roll->notes.clear();for(int i:model->visible)roll->notes.append(rows[i]);roll->pedals=data.pedals;roll->selected=-1;roll->editingOptions=options(targetScore).gate;roll->indexNotes();for(int i=0;i<roll->notes.size();++i)if(roll->notes[i].id==selected){roll->selected=i;table->selectRow(i);break;}roll->refresh();};
      auto calculate=[&] {
            if(loading) {queued=true;return;}if(!targetScore)return;stopPlaybackPreview();auditionKey.clear();const auto snapshot=data;auto gate=options(scores[scoreBox->currentIndex()]).gate;loading=true;roll->setEnabled(false);originalPlay->setEnabled(false);proposedPlay->setEnabled(false);editorMode->setEnabled(false);
            caption->setText(tr("正在计算，关闭窗口可取消。"));exclude->setEnabled(false);recompute->setEnabled(false);scoreBox->setEnabled(false);targets->setEnabled(false);
            watcher.setFuture(QtConcurrent::run([snapshot,gate,canceled] {MidiGateProcessor p;return p.process(snapshot,gate,[canceled]{return canceled->load();});}));
            };
      connect(&watcher,&QFutureWatcher<QVector<MidiGateNote>>::finished,&dialog,[&] {rows=watcher.result();loading=false;roll->setEnabled(true);originalPlay->setEnabled(true);proposedPlay->setEnabled(true);editorMode->setEnabled(true);exclude->setEnabled(true);recompute->setEnabled(true);scoreBox->setEnabled(true);targets->setEnabled(true);int needed=0,natural=0;for(const auto& note:rows){needed+=note.reason=="hand-release";natural+=note.reason=="pedal-release";}
            caption->setText(tr("为下一组音安排松手：%1个音；自然松手：%2个音。\n细轮廓：原按键长度；彩色实心：缩短后；灰色实心：保持原样；虚线：下一组音。下方一行显示踏板。").arg(needed).arg(natural));present();if(queued) {queued=false;calculate();}});
      auto load=[&] {
            targetScore=scores[scoreBox->currentIndex()];if(!targetScore)return;
            if(_releaseStates.value(targetScore)!=targetScore->masterScore()->state())_releaseOverrides[targetScore].clear();
            _releaseStates[targetScore]=targetScore->masterScore()->state();
            ExportMidi exporter(scores[scoreBox->currentIndex()]);exporter.collectGateData=true;QBuffer bytes;bytes.open(QIODevice::WriteOnly);
            if(!exporter.write(&bytes,preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),preferences.getBool(PREF_IO_MIDI_EXPORTRPNS),mscore->synthesizerState()))return;
            targets->blockSignals(true);targets->clear();targets->addItem(tr("全部钢琴部分"),-1);
            for(const auto piano:scores[scoreBox->currentIndex()]->parts())if(piano->instrumentId().contains("piano",Qt::CaseInsensitive))targets->addItem(piano->instrumentName(),piano->startTrack());
            const auto chosen=_staves.value(scores[scoreBox->currentIndex()]);for(int i=1;i<targets->count();++i)if(chosen.contains(targets->itemData(i).toInt()/VOICES))targets->setCurrentIndex(i);targets->blockSignals(false);
            data=exporter.gateData;roll->clock.setClock(data);part->blockSignals(true);part->clear();part->addItem(tr("全部乐器"),-1);QMap<int,QString> names;for(const auto& n:data.notes)if(n.staff>=0)names[n.staff]=n.partName+tr(" · 谱表%1").arg(n.staff+1);for(auto i=names.cbegin();i!=names.cend();++i)part->addItem(i.value(),i.key());part->blockSignals(false);calculate();
            };
      connect(targets,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int) {
            auto score=scores[scoreBox->currentIndex()];auto& selection=_staves[score];selection.clear();
            const int start=targets->currentData().toInt();if(start>=0)for(const auto p:score->parts())if(p->startTrack()==start)for(int track=p->startTrack();track<p->endTrack();track+=VOICES)selection.insert(track/VOICES);
            calculate();
            });
      connect(scoreBox,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){load();});
      connect(part,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){present();});connect(filter,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){present();});
      connect(display,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int i){roll->stacked=i==1;roll->refresh();});connect(zoom,&QSlider::valueChanged,&dialog,[&](int i){roll->setScale(i*.0015,roll->viewport()->width()/2.);});
      roll->selectRow=[table](int row){table->selectRow(row);};
      roll->scaleChanged=[zoom](double value){QSignalBlocker block(zoom);zoom->setValue(qBound(1,qRound(value/.0015),400));};
      roll->editRelease=[&](const QString& id,int at){if(!targetScore||loading)return;_enabled->setChecked(true);stopPlaybackPreview();auto& values=_releaseOverrides[targetScore];if(at<0)values.remove(id);else values[id]=at;_releaseStates[targetScore]=targetScore->masterScore()->state();playStatus->setText(tr("已调整这一次出现的音；最终MIDI采用图中的松手位置。"));calculate();};
      connect(fit,&QPushButton::clicked,&dialog,[&]{roll->fitContent();});
      audition=[&](bool proposed){
            if(!targetScore||loading)return;if(mscore->currentScore()!=targetScore){playStatus->setText(tr("请先在乐谱标签页打开%1，再试听这份谱。").arg(targetScore->title()));return;}
            if(_releaseStates.value(targetScore)!=targetScore->masterScore()->state()){playStatus->setText(tr("乐谱已改变，请返回对比并重新计算。"));return;}
            if(auditionKey.isEmpty()||!playbackPreviewPrepared(targetScore,auditionKey)){
                  playStatus->setText(tr("正在准备原长度与新长度的声音…"));auto prepared=prepareGateAudition(targetScore,data,rows);
                  if(prepared.unmatched){playStatus->setText(tr("%1个导出音还需要核对演奏对应。可先导出前后MIDI听这一段。").arg(prepared.unmatched));return;}
                  auditionKey="midi-gate-"+QString::number(quintptr(targetScore.data()))+"-"+QString::number(QDateTime::currentMSecsSinceEpoch());preparePlaybackPreview(targetScore,auditionKey,std::move(prepared.original),std::move(prepared.proposed));
                  }
            int start=0,occurrence=1;if(roll->selected>=0&&roll->selected<roll->notes.size()){
                  const auto& note=roll->notes[roll->selected];start=qMax(0,note.sourceTick);int seen=0;for(auto repeat:targetScore->masterScore()->repeatList())if(start>=repeat->tick&&start<repeat->tick+repeat->len()){++seen;if(note.sourceUtick>=repeat->utick&&note.sourceUtick<repeat->utick+repeat->len()){occurrence=seen;break;}}
                  }
            auto result=startPlaybackPreview(targetScore,proposed,{{"start",start},{"end",targetScore->lastMeasure()->endTick().ticks()},{"occurrence",occurrence}});
            if(result.contains("error")){playStatus->setText(result.value("error").toString());return;}proposedMode=proposed;playStatus->setText(proposed?tr("正在听：新长度 · 可直接切换到原长度，从当前拍位继续。"):tr("正在听：原长度 · 可直接切换到新长度，从当前拍位继续。"));
            };
      connect(originalPlay,&QPushButton::clicked,&dialog,[&]{audition(false);});connect(proposedPlay,&QPushButton::clicked,&dialog,[&]{audition(true);});connect(stopPlay,&QPushButton::clicked,&dialog,[&]{stopPlaybackPreview();playStatus->setText(tr("已停止，切换原长度／新长度可从这里继续。"));});
      QTimer playhead;playhead.setInterval(40);connect(&playhead,&QTimer::timeout,&dialog,[&]{
            if(!targetScore||!seq||!seq->isPlaying()||!playbackPreviewActive(targetScore,proposedMode))return;
            for(const auto& n:data.notes)if(n.known){roll->setPlayhead(roll->clock.durationMs(0,n.on)+(PlaybackTiming::time(targetScore,seq->getCurTick())-PlaybackTiming::time(targetScore,n.sourceUtick))*1000.);break;}
            });playhead.start();
      connect(editorMode,&QPushButton::clicked,&dialog,[&]{
            if(loading||!targetScore)return;auto editor=mscore->performanceEditor();auto oldParent=editor->parentWidget();auto oldDock=qobject_cast<QDockWidget*>(oldParent);const bool oldHidden=editor->isHidden(),dockHidden=oldDock&&oldDock->isHidden();
            QList<QPointer<QWidget>> visible;for(auto child:editor->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(!child->isHidden()){visible.append(child);child->hide();}
            QDialog mode(mscore);mode.setObjectName("midiCropEditorWindow");mode.setWindowTitle(tr("演奏编辑器 · 导出音长"));mode.resize(1180,760);auto box=new QVBoxLayout(&mode);
            auto header=new QLabel(tr("导出音长 · %1\n虚线框：原长度；实心条：新长度；灰色：保持原样。Ctrl+滚轮缩放时间，Shift+滚轮调整行高，中键拖动浏览。尾端只改变这次导出。 ").arg(targetScore->title()),&mode);header->setWordWrap(true);box->addWidget(header);
            if(oldDock)oldDock->hide();dialog.hide();editor->setParent(&mode);box->addWidget(editor,1);editor->layout()->addWidget(canvasPane);canvasPane->show();editor->show();editorMode->hide();
            auto back=new QPushButton(tr("完成调整，返回导出"),&mode);back->setObjectName("midiCropEditorReturn");box->addWidget(back);connect(back,&QPushButton::clicked,&mode,&QDialog::accept);
            mode.exec();editor->layout()->removeWidget(canvasPane);canvasPane->setParent(&dialog);layout->insertWidget(2,canvasPane,3);canvasPane->show();editorMode->show();
            editor->setParent(oldParent);if(oldDock)oldDock->setWidget(editor);for(auto child:visible)if(child)child->show();editor->setVisible(!oldHidden);if(oldDock)oldDock->setVisible(!dockHidden);dialog.show();dialog.raise();
            });
      connect(table->selectionModel(),&QItemSelectionModel::currentRowChanged,&dialog,[&](const QModelIndex& index,const QModelIndex&){roll->selected=index.row();if(index.isValid()) {
            const auto& note=roll->notes[index.row()];QString explanation=pitchText(note.pitch)+" · "+reasonText(note.reason);
            explanation+=tr("。原按键时长 %1 ms，导出 %2 ms。").arg(qRound(roll->clock.durationMs(note.on,note.off))).arg(qRound(roll->clock.durationMs(note.on,note.newOff)));
            if(note.releaseFor>=0) {QStringList pitches;for(int pitch:note.nextPitches)pitches.append(pitchText(pitch));explanation+=tr(" 下一组：%1，位于 %2 秒；图中以虚线轮廓标出。").arg(pitches.join("、")).arg(roll->clock.durationMs(0,note.releaseFor)/1000.,0,'f',3);}
            if(note.beforeKeys>=0)explanation+=tr(" 这只手由 %1 个按键减为 %2 个，跨度由 %3 减为 %4 半音。").arg(note.beforeKeys).arg(note.afterKeys).arg(note.beforeSpan).arg(note.afterSpan);
            if(!note.handWarning.isEmpty())explanation+=" "+note.handWarning;
            detail->setText(explanation);roll->revealSelected();
            if(targetScore&&targetScore==mscore->currentScore()&&(!seq||!seq->isPlaying())&&note.voice>=0&&note.sourceTick>=0){
                  auto segment=targetScore->tick2segment(Fraction::fromTicks(note.sourceTick),false,SegmentType::ChordRest);auto element=segment?segment->element(note.voice):nullptr;
                  if(element&&element->isChord())for(auto pitch:toChord(element)->notes())if(pitch->ppitch()==note.pitch){targetScore->select(pitch,SelectType::SINGLE);auto view=mscore->currentScoreView();view->adjustCanvasPosition(pitch,false);showWorkflowPositionHint(view,{{"start",note.sourceTick},{"end",note.sourceTick+toChord(element)->actualTicks().ticks()},{"firstTrack",note.voice}});break;}
                  }
            }roll->refresh();});
      connect(exclude,&QPushButton::clicked,&dialog,[&] {const auto index=table->currentIndex();if(!index.isValid())return;const auto& n=rows[model->visible[index.row()]];auto& set=_exclusions[scores[scoreBox->currentIndex()]];if(set.contains(n.id))set.remove(n.id);else set.insert(n.id);calculate();});
      connect(recompute,&QPushButton::clicked,&dialog,calculate);
      connect(config,&QPushButton::clicked,&dialog,[&]{settings();calculate();});
      connect(specific,&QPushButton::clicked,&dialog,[&]{voiceSelection(scores[scoreBox->currentIndex()]);calculate();});
      connect(reset,&QPushButton::clicked,&dialog,[&] {_gate=MidiGateOptions();_fromMeasure=_untilMeasure=0;_tracks[scores[scoreBox->currentIndex()]].clear();_protectedTracks[scores[scoreBox->currentIndex()]].clear();_exclusions[scores[scoreBox->currentIndex()]].clear();_releaseOverrides[scores[scoreBox->currentIndex()]].clear();_staves[scores[scoreBox->currentIndex()]].clear();{QSignalBlocker block(targets);targets->setCurrentIndex(0);}savePreferences();calculate();});
      connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
      QTimer::singleShot(0,&dialog,load);dialog.exec();
      // Worker owns copies only; cancellation does not wait for audio or score state.
      stopPlaybackPreview(true);playhead.stop();canceled->store(true);watcher.disconnect();watcher.cancel();
      }
}
