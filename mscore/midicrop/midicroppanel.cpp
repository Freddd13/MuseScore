// SPDX-License-Identifier: GPL-2.0-or-later
#include "midicroppanel.h"
#include "mscore/workflow/toolicons.h"
#include "mscore/workflow/featuregate.h"
#include "gateaudition.h"
#include "mscore/shortcut.h"
#include "mscore/workflow/controlflowlayout.h"
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
            {"fingering-uncertain",MidiCropPanel::tr("这个音的指法互相冲突，保留原长度")},
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
class GateLegendSample final : public QWidget {
      int _kind;
public:
      GateLegendSample(int kind,QWidget* parent):QWidget(parent),_kind(kind){setFixedSize(32,16);}
protected:
      void paintEvent(QPaintEvent*) override {
            QPainter painter(this);const QRectF box(1,3,width()-3,height()-6);
            auto color=_kind==1?QApplication::palette().color(QPalette::Highlight):palette().text().color();
            if(_kind) {auto fill=color;fill.setAlpha(_kind==1?155:35);painter.fillRect(box,fill);}
            color.setAlpha(_kind==2?75:190);painter.setPen(QPen(color,1,_kind==1?Qt::SolidLine:Qt::DashLine));
            painter.drawRect(box);
            }
      };
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
            if(!index.isValid())return {};
            if(role==Qt::TextAlignmentRole)return int((index.column()>0&&index.column()<4?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter);
            if(role!=Qt::DisplayRole&&role!=Qt::ToolTipRole)return {};
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
      _writtenPedal=new QCheckBox(tr("导出谱上原始踏板"),this);_writtenPedal->setObjectName("midiWrittenPedal");
      _writtenPedal->setChecked(QSettings().value("export/midi/writtenPedal",false).toBool());
      _writtenPedal->setToolTip(tr("勾选后采用SF2原有踏板时机。关闭时采用乐谱中设置的钢琴换踏板。"));
      layout->addWidget(_writtenPedal);
      connect(_writtenPedal,&QCheckBox::toggled,this,[this]{savePreferences();});
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
bool MidiCropPanel::includeOriginal() const {return enabled() && _original->isChecked();}
bool MidiCropPanel::usesWrittenPedal() const {return personalFeaturesEnabled() && _writtenPedal->isChecked();}
MidiExportOptions MidiCropPanel::options(Score* score) const {
      MidiExportOptions result;result.crop=enabled();result.gate=_gate;result.writtenPedal=usesWrittenPedal();
      if(_fromMeasure>0){result.gate.scoreStart=INT_MAX;result.gate.scoreEnd=INT_MAX;int bar=1;for(auto m=score->firstMeasure();m;m=m->nextMeasure(),++bar){if(bar==_fromMeasure)result.gate.scoreStart=m->tick().ticks();if(bar==_untilMeasure)result.gate.scoreEnd=m->endTick().ticks();}if(_untilMeasure<_fromMeasure||_untilMeasure>score->nmeasures())result.gate.scoreStart=result.gate.scoreEnd=INT_MAX;}
      result.gate.tracks=_tracks.value(score);result.gate.protectedTracks=_protectedTracks.value(score);
      result.gate.excluded=_exclusions.value(score);if(_releaseStates.value(score)==score->masterScore()->state())result.gate.releaseOverrides=_releaseOverrides.value(score);result.gate.staves=_staves.value(score);return result;
      }
void MidiCropPanel::savePreferences() const {
      if (!personalFeaturesEnabled()) return;
      QSettings handSettings;handSettings.setValue("export/midi/writtenPedal",usesWrittenPedal());handSettings.setValue("export/midi/releaseLeadMs",_gate.releaseLeadMs);
      for(int side=0;side<2;++side) {const auto key=QString("export/midi/hand%1/").arg(side);handSettings.setValue(key+"comfort",_gate.hands[side].comfort);handSettings.setValue(key+"maximum",_gate.hands[side].maximum);handSettings.setValue(key+"white",_gate.hands[side].white);}handSettings.sync();
      preferences.setPreference("export/midi/cropFromMeasure",_fromMeasure);preferences.setPreference("export/midi/cropUntilMeasure",_untilMeasure);preferences.setPreference("export/midi/cropProtectMelody",_gate.protectMelody);
      auto text=[](const QSet<int>& values){QList<int> list=values.values();std::sort(list.begin(),list.end());QStringList result;for(int v:list)result<<QString::number(v);return result.join(',');};
      preferences.setPreference("export/midi/cropVoices",text(_gate.voices));preferences.setPreference("export/midi/cropProtectedVoices",text(_gate.protectedVoices));
      preferences.setPreference("export/midi/cropEnabled",enabled());preferences.setPreference("export/midi/cropOriginal",includeOriginal());
      preferences.setPreference("export/midi/cropLength",_gate.lengthRatio);preferences.setPreference("export/midi/cropNext",_gate.nextAttackRatio);
      preferences.setPreference("export/midi/cropMinimumMs",_gate.minimumMs);preferences.setPreference("export/midi/cropJitter",_gate.jitter);preferences.setPreference("export/midi/cropSeed",int(_gate.seed));
      }
bool MidiCropPanel::settings() {
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
      auto info=new QLabel(tr("先为下一组音安排松手，再为同时弹下的一组音加入接近的自然变化。踏板继续保持声音时，程序会按这里的最短音长和个人跨度安排松手。"),&dialog);info->setWordWrap(true);form->addRow(info);
      auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
      connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
      if(dialog.exec()==QDialog::Accepted) {
            QSet<int> selected,protectedSet;for(int i=0;i<4;++i){if(voices[i]->isChecked())selected.insert(i);if(protectedVoices[i]->isChecked())protectedSet.insert(i);}if(selected.isEmpty()||(from->value()>0&&until->value()<from->value())){QMessageBox::warning(this,tr("MIDI松手处理"),tr("请至少选择一个声部，并填写有效的小节范围。"));return false;}
            _fromMeasure=from->value();_untilMeasure=_fromMeasure?until->value():0;_gate.voices=selected;_gate.protectedVoices=protectedSet;_gate.protectMelody=melody->isChecked();
            _gate.lengthRatio=length->value()/100.;_gate.nextAttackRatio=next->value()/100.;_gate.minimumMs=minimum->value();_gate.releaseLeadMs=lead->value();_gate.hands[0]=editedHands[0];_gate.hands[1]=editedHands[1];_gate.jitter=jitter->value()/100.;_gate.seed=quint32(seed->value());savePreferences();return true;}
      return false;
      }
bool MidiCropPanel::write(Score* score,const QString& path,const QString& original) {
      if(seq && (seq->isPlaying()||!seq->backgroundRenderingIdle())) {QMessageBox::information(this,tr("MIDI松手处理"),tr("请先停播，等待乐谱准备完成后再生成MIDI。"));return false;}
      if(enabled()&&_fromMeasure>0&&(_untilMeasure>score->nmeasures()||_untilMeasure<_fromMeasure)){QMessageBox::warning(this,tr("MIDI松手处理"),tr("%1中不存在所填的小节范围，请调整后再导出。").arg(score->title()));return false;}
      if(enabled()&&!_releaseOverrides.value(score).isEmpty()&&_releaseStates.value(score)!=score->masterScore()->state()){QMessageBox::information(this,tr("MIDI松手处理"),tr("乐谱已改变，请打开“查看对比”更新手动松手位置，再导出。"));return false;}
      ExportMidi exporter(score);QBuffer buffer;buffer.open(QIODevice::WriteOnly);
      if(!exporter.write(&buffer,preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),preferences.getBool(PREF_IO_MIDI_EXPORTRPNS),mscore->synthesizerState(),options(score)))return false;
      auto store=[&](const QString& name,const QByteArray& bytes) {QSaveFile f(name);if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()) {QMessageBox::warning(this,tr("MIDI导出"),tr("无法写入%1：%2").arg(name,f.errorString()));return false;}return true;};
      if(!original.isEmpty()) {
            QBuffer bytes;bytes.open(QIODevice::WriteOnly);
            if(exporter.gateBaseline.write(&bytes) || !store(original,bytes.data()))return false;
            }
      return store(path,buffer.data());
      }
bool MidiCropPanel::voiceSelection(Score* score) {
      QDialog dialog(this);dialog.setObjectName("midiCropStaffVoices");dialog.setWindowTitle(tr("%1：声部选择与保护").arg(score->title()));dialog.resize(510,420);auto layout=new QVBoxLayout(&dialog);
      auto label=new QLabel(tr("可分别选择每个谱表的声部，以及需要保护的声部。这张表用于本次导出的当前谱子；通用范围设置单独保存。"),&dialog);label->setWordWrap(true);layout->addWidget(label);
      auto tree=new QTreeWidget(&dialog);tree->setObjectName("cropStaffVoicesTree");tree->setColumnCount(3);tree->setHeaderLabels({tr("谱表／声部"),tr("处理"),tr("保护")});layout->addWidget(tree);
      const auto selected=_tracks.value(score),protect=_protectedTracks.value(score);QVector<QTreeWidgetItem*> rows;
      for(int staff=0;staff<score->nstaves();++staff)for(int voice=0;voice<VOICES;++voice){int track=staff*VOICES+voice;auto row=new QTreeWidgetItem(tree,{tr("谱表%1 · 声部%2").arg(staff+1).arg(voice+1)});row->setData(0,Qt::UserRole,track);row->setCheckState(1,selected.isEmpty()||selected.contains(track)?Qt::Checked:Qt::Unchecked);row->setCheckState(2,protect.contains(track)?Qt::Checked:Qt::Unchecked);rows.append(row);}
      tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
      if(dialog.exec()!=QDialog::Accepted)return false;QSet<int> crop,protectedSet;for(auto row:rows){int track=row->data(0,Qt::UserRole).toInt();if(row->checkState(1)==Qt::Checked)crop.insert(track);if(row->checkState(2)==Qt::Checked)protectedSet.insert(track);}if(crop.isEmpty()){QMessageBox::warning(this,tr("MIDI松手处理"),tr("请至少选择一个声部。"));return false;}_tracks[score]=crop;_protectedTracks[score]=protectedSet;return true;
      }
void MidiCropPanel::compare() {
      if(seq && (seq->isPlaying()||!seq->backgroundRenderingIdle())) {QMessageBox::information(this,tr("MIDI松手处理"),tr("请先停播，等待乐谱准备完成后再生成MIDI。"));return;}
      const auto scores=_scores();if(scores.isEmpty())return;
      QDialog dialog(this);dialog.setObjectName("midiCropComparison");
      dialog.setWindowTitle(tr("MIDI松手前后对比"));dialog.setWindowFlag(Qt::WindowMaximizeButtonHint,true);
      dialog.resize(QSize(920,650).boundedTo(dialog.screen()->availableGeometry().size()-QSize(24,48)));
      auto layout=new QVBoxLayout(&dialog);layout->setContentsMargins(10,10,10,10);layout->setSpacing(6);
      auto scopeControls=new QWidget(&dialog);scopeControls->setObjectName("midiCropScopeControls");
      auto scopeFlow=new ControlFlowLayout(scopeControls);layout->addWidget(scopeControls);
      auto field=[&](ControlFlowLayout* flow,const QString& title,QWidget* control) {
            auto group=new QWidget(flow->parentWidget());auto row=new QHBoxLayout(group);
            row->setContentsMargins(0,0,0,0);row->setSpacing(5);
            row->addWidget(new QLabel(title,group));row->addWidget(control);
            group->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);flow->addWidget(group);
            };
      auto compactCombo=[](QComboBox* box,int characters,int maximum) {
            box->setMinimumContentsLength(characters);box->setMaximumWidth(maximum);
            box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            box->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
            };
      auto scoreBox=new QComboBox(scopeControls);scoreBox->setObjectName("midiCropScore");
      for(auto score:scores)scoreBox->addItem(score->title());compactCombo(scoreBox,12,220);
      field(scopeFlow,tr("乐谱"),scoreBox);
      auto targets=new QComboBox(scopeControls);targets->setObjectName("midiCropTargets");compactCombo(targets,10,210);
      field(scopeFlow,tr("处理钢琴"),targets);
      auto config=new QPushButton(tr("范围与松手…"),scopeControls);config->setObjectName("midiCropConfig");scopeFlow->addWidget(config);
      auto specific=new QPushButton(tr("谱表与保护…"),scopeControls);specific->setObjectName("midiCropStaffVoicesButton");scopeFlow->addWidget(specific);

      auto workspace=new QSplitter(Qt::Vertical,&dialog);workspace->setObjectName("midiCropWorkspace");
      workspace->setChildrenCollapsible(false);layout->addWidget(workspace,1);
      auto canvasPane=new QWidget(workspace);canvasPane->setObjectName("midiCropCanvasPane");
      auto canvasLayout=new QVBoxLayout(canvasPane);canvasLayout->setContentsMargins(0,0,0,0);canvasLayout->setSpacing(5);
      auto caption=new QLabel(tr("正在准备导出音符…"),canvasPane);caption->setObjectName("midiCropSummary");
      caption->setWordWrap(true);canvasLayout->addWidget(caption);
      auto legend=new QWidget(canvasPane);auto legendFlow=new ControlFlowLayout(legend);
      for(int kind=0;kind<3;++kind) {
            auto group=new QWidget(legend);auto row=new QHBoxLayout(group);row->setContentsMargins(0,0,0,0);row->setSpacing(4);
            row->addWidget(new GateLegendSample(kind,group));
            row->addWidget(new QLabel(QStringList{tr("原长度"),tr("新长度"),tr("保持原样")}[kind],group));
            group->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);legendFlow->addWidget(group);
            }
      canvasLayout->addWidget(legend);
      auto viewControls=new QWidget(canvasPane);viewControls->setObjectName("midiCropViewControls");
      auto viewFlow=new ControlFlowLayout(viewControls);canvasLayout->addWidget(viewControls);
      auto part=new QComboBox(viewControls);part->setObjectName("midiCropViewStaff");compactCombo(part,10,200);
      field(viewFlow,tr("图上显示"),part);
      auto filter=new QComboBox(viewControls);filter->setObjectName("midiCropFilter");filter->addItems({tr("全部"),tr("已缩短"),tr("保留")});compactCombo(filter,5,100);
      viewFlow->addWidget(filter);
      auto display=new QComboBox(viewControls);display->setObjectName("midiCropDisplay");display->addItems({tr("叠加"),tr("上下分列")});compactCombo(display,6,110);
      field(viewFlow,tr("对比"),display);
      auto zoomGroup=new QWidget(viewControls);auto zoomRow=new QHBoxLayout(zoomGroup);zoomRow->setContentsMargins(0,0,0,0);zoomRow->setSpacing(4);
      zoomRow->addWidget(new QLabel(tr("时间缩放"),zoomGroup));
      auto zoom=new QSlider(Qt::Horizontal,zoomGroup);zoom->setObjectName("midiCropZoom");zoom->setRange(0,1000);zoom->setFixedWidth(110);
      zoom->setToolTip(tr("拖动缩放时间；Ctrl+滚轮以鼠标位置缩放，Shift+滚轮调音高行。"));zoomRow->addWidget(zoom);
      auto zoomValue=new QLabel(zoomGroup);zoomValue->setObjectName("midiCropZoomValue");
      zoomValue->setAlignment(Qt::AlignRight|Qt::AlignVCenter);zoomValue->setFixedWidth(zoomValue->fontMetrics().horizontalAdvance("2000%")+5);zoomRow->addWidget(zoomValue);
      zoomGroup->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);viewFlow->addWidget(zoomGroup);
      auto fit=new QPushButton(tr("匹配内容"),viewControls);fit->setObjectName("midiCropFit");viewFlow->addWidget(fit);
      auto editorMode=new QPushButton(tr("在演奏编辑器调整…"),viewControls);editorMode->setObjectName("midiCropEditorMode");viewFlow->addWidget(editorMode);
      auto listToggle=new QToolButton(viewControls);listToggle->setObjectName("midiCropListToggle");listToggle->setText(tr("音符列表"));
      listToggle->setCheckable(true);listToggle->setChecked(QSettings().value("export/midi/showNoteList",true).toBool());viewFlow->addWidget(listToggle);
      auto fold=new QCheckBox(tr("只看有音的行"),viewControls);fold->setObjectName("midiCropFoldPitches");
      fold->setChecked(QSettings().value("export/midi/foldPitches",true).toBool());viewFlow->addWidget(fold);
      auto roll=new GateRoll(canvasPane);roll->foldUnused=fold->isChecked();roll->setObjectName("midiCropRoll");canvasLayout->addWidget(roll,1);
      auto detailPane=new QScrollArea(canvasPane);detailPane->setObjectName("midiCropDetailPane");
      detailPane->setWidgetResizable(true);detailPane->setFrameShape(QFrame::NoFrame);detailPane->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      detailPane->setFixedHeight(qMax(36,detailPane->fontMetrics().lineSpacing()*2+6));
      auto detail=new QLabel(tr("点选一个音，查看原长度、新长度和松手原因。"),detailPane);
      detail->setObjectName("midiCropDetail");detail->setWordWrap(true);detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
      detail->setAlignment(Qt::AlignLeft|Qt::AlignTop);detailPane->setWidget(detail);canvasLayout->addWidget(detailPane);
      auto actions=new QWidget(canvasPane);actions->setObjectName("midiCropActions");auto actionFlow=new ControlFlowLayout(actions);canvasLayout->addWidget(actions);
      auto playGroup=new QWidget(actions);auto transport=new QHBoxLayout(playGroup);transport->setContentsMargins(0,0,0,0);transport->setSpacing(5);
      auto originalPlay=new QPushButton(tr("听原长度"),playGroup),proposedPlay=new QPushButton(tr("听新长度"),playGroup),stopPlay=new QPushButton(tr("暂停"),playGroup);
      originalPlay->setObjectName("midiCropOriginalPlay");proposedPlay->setObjectName("midiCropProposedPlay");stopPlay->setObjectName("midiCropStop");
      originalPlay->setCheckable(true);proposedPlay->setCheckable(true);stopPlay->setEnabled(false);
      for(auto button:{originalPlay,proposedPlay,stopPlay})transport->addWidget(button);
      playGroup->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);actionFlow->addWidget(playGroup);
      auto editGroup=new QWidget(actions);auto historyButtons=new QHBoxLayout(editGroup);historyButtons->setContentsMargins(0,0,0,0);historyButtons->setSpacing(4);
      auto exclude=new QPushButton(tr("排除这个音"),editGroup);exclude->setEnabled(false);historyButtons->addWidget(exclude);
      editGroup->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);actionFlow->addWidget(editGroup);
      auto useForExport=new QCheckBox(tr("导出采用新长度"),actions);useForExport->setObjectName("midiCropUseProposed");useForExport->setChecked(enabled());actionFlow->addWidget(useForExport);
      connect(useForExport,&QCheckBox::toggled,_enabled,&QCheckBox::setChecked);connect(_enabled,&QCheckBox::toggled,useForExport,&QCheckBox::setChecked);
      auto playStatus=new QLabel(tr("拖动实心条尾端调整松手；图中按空格试听／暂停。"),canvasPane);playStatus->setObjectName("midiCropPlaybackStatus");
      playStatus->setWordWrap(true);playStatus->setFixedHeight(playStatus->fontMetrics().lineSpacing()*2+2);canvasLayout->addWidget(playStatus);
      auto table=new QTableView(workspace);table->setObjectName("midiCropNotes");auto model=new GateModel(table);table->setModel(model);
      table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);
      table->setMinimumHeight(80);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
      for(int column=0;column<7;++column)table->setColumnWidth(column,QVector<int>{65,95,100,100,130,200,145}[column]);
      table->horizontalHeader()->setStretchLastSection(true);
      workspace->setStretchFactor(0,3);workspace->setStretchFactor(1,2);workspace->setCollapsible(1,true);
      workspace->setSizes({360,240});workspace->restoreState(QSettings().value("export/midi/comparisonSplit").toByteArray());
      table->setVisible(listToggle->isChecked());
      connect(listToggle,&QToolButton::toggled,&dialog,[&](bool visible) {
            table->setVisible(visible);
            if(visible) {auto sizes=workspace->sizes();if(sizes.size()==2&&sizes[1]<20)workspace->setSizes({qMax(120,workspace->height()*3/5),qMax(80,workspace->height()*2/5)});}
            });
      auto footer=new QWidget(&dialog);auto footerFlow=new ControlFlowLayout(footer);layout->addWidget(footer);
      auto recompute=new QPushButton(tr("重新计算"),footer),reset=new QPushButton(tr("恢复默认设置"),footer),close=new QPushButton(tr("返回导出"),footer);
      recompute->setObjectName("midiCropRecompute");reset->setObjectName("midiCropReset");close->setObjectName("midiCropReturn");
      for(auto button:{recompute,reset,close})footerFlow->addWidget(button);
      auto scaleValue=[](int value){return .00005*std::pow(3./.00005,value/1000.);};
      auto sliderValue=[](double value){return qBound(0,qRound(std::log(value/.00005)/std::log(3./.00005)*1000),1000);};
      zoom->setValue(sliderValue(roll->scale));zoomValue->setText(QString::number(roll->scale/.15*100.,'f',0)+"%");
      MidiGateData data;QVector<MidiGateNote> rows;bool loading=false,queued=false,presenting=false,fitPending=true;
      QString auditionKey;bool proposedMode=true;double requestedMs=-1.;bool seekPending=false;
      QPointer<Score> targetScore=scores.front();
      auto histories=new QUndoGroup(&dialog);
      auto undo=histories->createUndoAction(workspace,tr("撤销导出调整"));
      auto redo=histories->createRedoAction(workspace,tr("重做导出调整"));
      undo->setObjectName("midiCropUndoAction");redo->setObjectName("midiCropRedoAction");
      undo->setShortcuts(QKeySequence::Undo);redo->setShortcuts(QKeySequence::Redo);
      for(auto action:{undo,redo}) {
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);dialog.addAction(action);workspace->addAction(action);
            }
      undo->setIcon(getAction("undo")->icon());redo->setIcon(getAction("redo")->icon());
      auto undoButton=new QToolButton(canvasPane),redoButton=new QToolButton(canvasPane);
      undoButton->setObjectName("midiCropUndo");redoButton->setObjectName("midiCropRedo");
      undoButton->setDefaultAction(undo);redoButton->setDefaultAction(redo);
      undoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);redoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
      historyButtons->insertWidget(0,undoButton);historyButtons->insertWidget(1,redoButton);
      exclude->setObjectName("midiCropExclude");
      for (const auto& item : QList<QPair<QAbstractButton*,QString>>{
                  {fit,"fit"},{editorMode,"panel"},{listToggle,"list"},
                  {originalPlay,"audition"},{proposedPlay,"audition"},{stopPlay,"pause"},
                  {exclude,"delete"},{recompute,"refresh"},{reset,"reset"},{close,"back"}})
            setComponentToolIcon(item.first,item.second);
      std::function<void(bool)> audition;std::function<void()> updateTransport;

      QFutureWatcher<QVector<MidiGateNote>> watcher;
      auto canceled=std::make_shared<std::atomic_bool>(false);
      auto present=[&] {
            QScopedValueRollback<bool> updating(presenting,true);
            const QString selected=roll->selected>=0&&roll->selected<roll->notes.size()?roll->notes[roll->selected].id:QString();
            model->setRows(rows,data,part->currentData().toInt(),filter->currentIndex());
            roll->notes.clear();for(int i:model->visible)roll->notes.append(rows[i]);
            roll->pedals=data.pedals;roll->selected=-1;roll->editingOptions=options(targetScore).gate;roll->indexNotes();
            for(int i=0;i<roll->notes.size();++i)if(roll->notes[i].id==selected){roll->selected=i;table->selectRow(i);break;}
            const bool selectedNote=roll->selected>=0;exclude->setEnabled(selectedNote&&!loading);
            exclude->setText(selectedNote&&_exclusions.value(targetScore).contains(selected)?tr("恢复这个音的处理"):tr("排除这个音"));
            if(!selectedNote)detail->setText(roll->notes.isEmpty()?tr("当前筛选没有音符，可选择“全部”查看。"):tr("点选一个音，查看原长度、新长度和松手原因。"));
            roll->refresh();
            };
      auto calculate=[&] {
            if(loading) {queued=true;return;}if(!targetScore)return;stopPlaybackPreview();auditionKey.clear();if(updateTransport)updateTransport();const auto snapshot=data;auto gate=options(scores[scoreBox->currentIndex()]).gate;loading=true;roll->setEnabled(false);originalPlay->setEnabled(false);proposedPlay->setEnabled(false);editorMode->setEnabled(false);
            caption->setText(tr("正在计算，关闭窗口可取消。"));exclude->setEnabled(false);recompute->setEnabled(false);scoreBox->setEnabled(false);targets->setEnabled(false);
            watcher.setFuture(QtConcurrent::run([snapshot,gate,canceled] {MidiGateProcessor p;return p.process(snapshot,gate,[canceled]{return canceled->load();});}));
            };
      connect(&watcher,&QFutureWatcher<QVector<MidiGateNote>>::finished,&dialog,[&] {rows=watcher.result();loading=false;if(queued){queued=false;calculate();return;}roll->setEnabled(true);originalPlay->setEnabled(true);proposedPlay->setEnabled(true);editorMode->setEnabled(true);exclude->setEnabled(true);recompute->setEnabled(true);scoreBox->setEnabled(true);targets->setEnabled(true);int needed=0,natural=0;for(const auto& note:rows){needed+=note.reason=="hand-release";natural+=note.reason=="pedal-release";}
            int manual=0,kept=0;for(const auto& note:rows){manual+=note.reason=="manual-release";kept+=note.newOff==note.off;}
            caption->setText(tr("%1个音 · 提前松手%2 · 自然松手%3 · 手动%4 · 保持%5").arg(rows.size()).arg(needed).arg(natural).arg(manual).arg(kept));present();if(fitPending){fitPending=false;roll->fitContent();}if(updateTransport)updateTransport();});
      connect(this,&MidiCropPanel::exportEditsChanged,&dialog,[&](Score* score) {
            if(score!=targetScore)return;
            playStatus->setText(tr("已更新这次导出的调整。"));calculate();
            });
      auto load=[&] {
            targetScore=scores[scoreBox->currentIndex()];if(!targetScore)return;fitPending=true;requestedMs=-1.;seekPending=false;
            auto history=editHistory(targetScore);histories->addStack(history);histories->setActiveStack(history);
            if(_releaseStates.value(targetScore)!=targetScore->masterScore()->state()) {
                  _releaseOverrides[targetScore].clear();history->clear();
                  }
            _releaseStates[targetScore]=targetScore->masterScore()->state();
            ExportMidi exporter(scores[scoreBox->currentIndex()]);exporter.collectGateData=true;QBuffer bytes;bytes.open(QIODevice::WriteOnly);
            if(!exporter.write(&bytes,preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),preferences.getBool(PREF_IO_MIDI_EXPORTRPNS),mscore->synthesizerState()))return;
            targets->blockSignals(true);targets->clear();targets->addItem(tr("全部钢琴部分"),-1);
            for(const auto piano:scores[scoreBox->currentIndex()]->parts())if(piano->instrumentId().contains("piano",Qt::CaseInsensitive))targets->addItem(piano->instrumentName(),piano->startTrack());
            const auto chosen=_staves.value(scores[scoreBox->currentIndex()]);for(int i=1;i<targets->count();++i)if(chosen.contains(targets->itemData(i).toInt()/VOICES))targets->setCurrentIndex(i);targets->blockSignals(false);
            data=exporter.gateData;roll->clock.setClock(data);part->blockSignals(true);part->clear();part->addItem(tr("全部谱表"),-1);QMap<int,QString> names;for(const auto& n:data.notes)if(n.staff>=0)names[n.staff]=n.partName+tr(" · 谱表%1").arg(n.staff+1);for(auto i=names.cbegin();i!=names.cend();++i)part->addItem(i.value(),i.key());part->blockSignals(false);calculate();
            };
      connect(targets,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int) {
            auto score=scores[scoreBox->currentIndex()];auto& selection=_staves[score];selection.clear();
            const int start=targets->currentData().toInt();if(start>=0)for(const auto p:score->parts())if(p->startTrack()==start)for(int track=p->startTrack();track<p->endTrack();track+=VOICES)selection.insert(track/VOICES);
            calculate();
            });
      connect(scoreBox,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){load();});
      connect(part,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){present();});connect(filter,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){present();});
      connect(display,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int i){roll->stacked=i==1;roll->fitPitches();});connect(zoom,&QSlider::valueChanged,&dialog,[&](int i){roll->setScale(scaleValue(i),roll->viewport()->width()/2.);});
      roll->selectRow=[table](int row){table->selectRow(row);};
      roll->scaleChanged=[&](double value){QSignalBlocker block(zoom);zoom->setValue(sliderValue(value));zoomValue->setText(QString::number(value/.15*100.,'f',value<.0015?1:0)+"%");};
      roll->editRelease=[&](const QString& id,int at){if(!targetScore||loading)return;changeRelease(targetScore,id,at);playStatus->setText(tr("已调整这次导出的松手位置，Ctrl+Z可撤销。"));};
      connect(fit,&QPushButton::clicked,&dialog,[&]{roll->fitContent();});
      connect(fold,&QCheckBox::toggled,&dialog,[&](bool value){roll->setFoldUnused(value);});
      auto ownPreview=[&]{return targetScore&&!auditionKey.isEmpty()&&playbackPreviewPrepared(targetScore,auditionKey);};
      auto occurrenceAt=[&](int tick,int utick) {
            int seen=0;for(auto repeat:targetScore->masterScore()->repeatList())
                  if(tick>=repeat->tick&&tick<repeat->tick+repeat->len()) {
                        ++seen;if(utick>=repeat->utick&&utick<repeat->utick+repeat->len())return seen;
                        }
            return 1;
            };
      auto seek=[&](double ms) {
            requestedMs=ms;seekPending=true;roll->setPlayhead(ms);
            const bool playing=ownPreview()&&(playbackPreviewActive(targetScore,false)||playbackPreviewActive(targetScore,true));
            if(ownPreview())stopPlaybackPreview(true);
            if(playing)audition(proposedMode);
            else playStatus->setText(tr("从 %1 秒开始试听。图中按空格播放。 ").arg(ms/1000.,0,'f',3));
            if(updateTransport)updateTransport();
            };
      roll->seekPosition=seek;
      audition=[&](bool proposed){
            if(!targetScore||loading)return;if(mscore->currentScore()!=targetScore){playStatus->setText(tr("请先在乐谱标签页打开%1，再试听这份谱。").arg(targetScore->title()));return;}
            if(_releaseStates.value(targetScore)!=targetScore->masterScore()->state()){playStatus->setText(tr("乐谱已改变，请返回对比并重新计算。"));return;}
            if(auditionKey.isEmpty()||!playbackPreviewPrepared(targetScore,auditionKey)){
                  playStatus->setText(tr("正在准备原长度与新长度的声音…"));auto prepared=prepareGateAudition(targetScore,data,rows);
                  if(prepared.unmatched){playStatus->setText(tr("%1个导出音还需要核对演奏对应。可先导出前后MIDI听这一段。").arg(prepared.unmatched));return;}
                  auditionKey="midi-gate-"+QString::number(quintptr(targetScore.data()))+"-"+QString::number(QDateTime::currentMSecsSinceEpoch());preparePlaybackPreview(targetScore,auditionKey,std::move(prepared.original),std::move(prepared.proposed));
                  }
            int start=0,occurrence=1;
            if(requestedMs>=0.) {
                  for(const auto& anchor:data.notes)if(anchor.known&&anchor.sourceUtick>=0) {
                        const double seconds=qMax(0.,PlaybackTiming::time(targetScore,anchor.sourceUtick)+(requestedMs-roll->clock.durationMs(0,anchor.on))/1000.);
                        int utick=targetScore->utime2utick(seconds);const auto& repeats=targetScore->masterScore()->repeatList();
                        if(!repeats.empty())utick=qBound(0,utick,repeats.back()->utick+repeats.back()->len()-1);
                        start=repeats.utick2tick(utick);occurrence=occurrenceAt(start,utick);break;
                        }
                  }
            else if(roll->selected>=0&&roll->selected<roll->notes.size()) {
                  const auto& note=roll->notes[roll->selected];start=qMax(0,note.sourceTick);occurrence=occurrenceAt(start,note.sourceUtick);
                  }
            if(seekPending){stopPlaybackPreview(true);seekPending=false;}
            auto result=startPlaybackPreview(targetScore,proposed,{{"start",start},{"end",targetScore->lastMeasure()->endTick().ticks()},{"occurrence",occurrence},{"exactStart",true}});
            if(result.contains("error")){playStatus->setText(result.value("error").toString());if(updateTransport)updateTransport();return;}
            proposedMode=proposed;if(updateTransport)updateTransport();
            };
      updateTransport=[&] {
            const bool original=ownPreview()&&playbackPreviewActive(targetScore,false);
            const bool proposed=ownPreview()&&playbackPreviewActive(targetScore,true);
            {QSignalBlocker one(originalPlay),two(proposedPlay);originalPlay->setChecked(original);proposedPlay->setChecked(proposed);}
            stopPlay->setEnabled(original||proposed);
            if(original||proposed)playStatus->setText(original?tr("正在听：原长度 · 切换到新长度将从当前拍位继续。"):tr("正在听：新长度 · 切换到原长度将从当前拍位继续。"));
            };
      auto stop=[&]{if(ownPreview())stopPlaybackPreview();updateTransport();playStatus->setText(tr("已暂停，可从当前拍位继续；点击图中时间尺可另选起点。"));};
      connect(originalPlay,&QPushButton::clicked,&dialog,[&]{if(ownPreview()&&playbackPreviewActive(targetScore,false))stop();else audition(false);});
      connect(proposedPlay,&QPushButton::clicked,&dialog,[&]{if(ownPreview()&&playbackPreviewActive(targetScore,true))stop();else audition(true);});
      connect(stopPlay,&QPushButton::clicked,&dialog,stop);
      roll->togglePlayback=[&]{if(ownPreview()&&(playbackPreviewActive(targetScore,false)||playbackPreviewActive(targetScore,true)))stop();else audition(proposedMode);};
      connect(workflowContext(),&WorkflowContext::auditionChanged,&dialog,updateTransport);
      bool wasPlaying=false;
      QTimer playhead;playhead.setInterval(40);connect(&playhead,&QTimer::timeout,&dialog,[&]{
            const bool playing=ownPreview()&&seq&&seq->isPlaying()&&(playbackPreviewActive(targetScore,false)||playbackPreviewActive(targetScore,true));
            if(playing)for(const auto& n:data.notes)if(n.known&&n.sourceUtick>=0){roll->setPlayhead(roll->clock.durationMs(0,n.on)+(PlaybackTiming::time(targetScore,seq->getCurTick())-PlaybackTiming::time(targetScore,n.sourceUtick))*1000.);break;}
            if(wasPlaying&&!playing){updateTransport();playStatus->setText(tr("播放已停。可继续试听，或在图中选择新的起点。"));}
            wasPlaying=playing;
            });playhead.start();
      connect(editorMode,&QPushButton::clicked,&dialog,[&] {
            if(loading||!targetScore)return;
            auto editor=mscore->performanceEditor();auto oldParent=editor->parentWidget();auto oldDock=qobject_cast<QDockWidget*>(oldParent);
            const bool oldHidden=editor->isHidden(),dockHidden=oldDock&&oldDock->isHidden();
            QList<QPointer<QWidget>> visible;
            for(auto child:editor->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))
                  if(!child->isHidden()){visible.append(child);child->hide();}
            QDialog mode(mscore);mode.setObjectName("midiCropEditorWindow");mode.setWindowTitle(tr("演奏编辑器 · 导出音长"));
            mode.setWindowFlag(Qt::WindowMaximizeButtonHint,true);
            mode.resize(QSize(1180,760).boundedTo(mode.screen()->availableGeometry().size()-QSize(24,48)));
            mode.setPalette(editor->palette());mode.setAutoFillBackground(true);
            mode.setStyleSheet(editor->styleSheet()+QString(" QPushButton:checked {background:palette(highlight);color:palette(highlighted-text);} QHeaderView::section {background:%1;color:%2;border:1px solid %3;padding:3px;}")
                  .arg(editor->palette().color(QPalette::Window).name(),editor->palette().color(QPalette::WindowText).name(),editor->property("workflowPanelTheme").toMap().value("line").value<QColor>().name()));
            auto box=new QVBoxLayout(&mode);
            auto header=new QLabel(tr("导出音长 · %1").arg(targetScore->title()),&mode);header->setWordWrap(true);box->addWidget(header);
            const auto previousSizes=workspace->sizes();
            QList<QPair<QWidget*,QPalette>> palettes;
            auto theme=editor->palette();theme.setColor(QPalette::Mid,editor->property("workflowPanelTheme").toMap().value("line").value<QColor>());
            theme.setColor(QPalette::Button,theme.color(QPalette::Window));theme.setColor(QPalette::ButtonText,theme.color(QPalette::WindowText));
            theme.setColor(QPalette::Highlight,QApplication::palette().color(QPalette::Highlight));theme.setColor(QPalette::HighlightedText,QApplication::palette().color(QPalette::HighlightedText));
            auto widgets=workspace->findChildren<QWidget*>();widgets.prepend(workspace);
            for(auto widget:widgets) {
                  palettes.append({widget,widget->palette()});widget->setPalette(theme);
                  }
            if(oldDock)oldDock->hide();dialog.hide();editor->setParent(&mode);box->addWidget(editor,1);
            editor->layout()->addWidget(workspace);workspace->show();editor->show();editorMode->hide();
            auto backRow=new QHBoxLayout;backRow->addStretch();
            auto back=new QPushButton(tr("完成调整，返回导出"),&mode);back->setObjectName("midiCropEditorReturn");
            backRow->addWidget(back);box->addLayout(backRow);connect(back,&QPushButton::clicked,&mode,&QDialog::accept);
            mode.exec();editor->layout()->removeWidget(workspace);workspace->setParent(&dialog);
            for(const auto& saved:palettes)saved.first->setPalette(saved.second);
            layout->insertWidget(1,workspace,1);workspace->setSizes(previousSizes);workspace->show();editorMode->show();
            editor->setParent(oldParent);if(oldDock)oldDock->setWidget(editor);
            for(auto child:visible)if(child)child->show();
            editor->setVisible(!oldHidden);if(oldDock)oldDock->setVisible(!dockHidden);dialog.show();dialog.raise();
            });
      connect(table->selectionModel(),&QItemSelectionModel::currentRowChanged,&dialog,[&](const QModelIndex& index,const QModelIndex&){roll->selected=index.row();exclude->setEnabled(index.isValid()&&!loading);
            if(index.isValid()) {
            const auto& note=roll->notes[index.row()];QString explanation=pitchText(note.pitch)+" · "+reasonText(note.reason);
            explanation+=tr("。原按键时长 %1 ms，导出 %2 ms。").arg(qRound(roll->clock.durationMs(note.on,note.off))).arg(qRound(roll->clock.durationMs(note.on,note.newOff)));
            if(note.releaseFor>=0) {QStringList pitches;for(int pitch:note.nextPitches)pitches.append(pitchText(pitch));explanation+=tr(" 下一组：%1，位于 %2 秒；图中以虚线轮廓标出。").arg(pitches.join("、")).arg(roll->clock.durationMs(0,note.releaseFor)/1000.,0,'f',3);}
            if(note.beforeKeys>=0)explanation+=tr(" 这只手由 %1 个按键减为 %2 个，跨度由 %3 减为 %4 半音。").arg(note.beforeKeys).arg(note.afterKeys).arg(note.beforeSpan).arg(note.afterSpan);
            if(!note.handWarning.isEmpty())explanation+=" "+note.handWarning;
            detail->setText(explanation);exclude->setText(_exclusions.value(targetScore).contains(note.id)?tr("恢复这个音的处理"):tr("排除这个音"));roll->revealSelected();
            if(!presenting&&(!seq||!seq->isPlaying()))seek(roll->clock.durationMs(0,note.on));
            if(!presenting&&targetScore&&targetScore==mscore->currentScore()&&(!seq||!seq->isPlaying())&&note.voice>=0&&note.sourceTick>=0){
                  auto segment=targetScore->tick2segment(Fraction::fromTicks(note.sourceTick),false,SegmentType::ChordRest);auto element=segment?segment->element(note.voice):nullptr;
                  if(element&&element->isChord())for(auto pitch:toChord(element)->notes())if(pitch->ppitch()==note.pitch){targetScore->select(pitch,SelectType::SINGLE);auto view=mscore->currentScoreView();view->adjustCanvasPosition(pitch,false);showWorkflowPositionHint(view,{{"start",note.sourceTick},{"end",note.sourceTick+toChord(element)->actualTicks().ticks()},{"firstTrack",note.voice}});break;}
                  }
            }roll->refresh();});
      connect(exclude,&QPushButton::clicked,&dialog,[&] {const auto index=table->currentIndex();if(!index.isValid())return;const auto& n=rows[model->visible[index.row()]];toggleExclusion(scores[scoreBox->currentIndex()],n.id);});
      connect(recompute,&QPushButton::clicked,&dialog,load);
      connect(config,&QPushButton::clicked,&dialog,[&]{if(settings())calculate();});
      connect(specific,&QPushButton::clicked,&dialog,[&]{if(voiceSelection(scores[scoreBox->currentIndex()]))calculate();});
      connect(reset,&QPushButton::clicked,&dialog,[&] {_gate=MidiGateOptions();_fromMeasure=_untilMeasure=0;_tracks[scores[scoreBox->currentIndex()]].clear();_protectedTracks[scores[scoreBox->currentIndex()]].clear();_exclusions[scores[scoreBox->currentIndex()]].clear();_releaseOverrides[scores[scoreBox->currentIndex()]].clear();_staves[scores[scoreBox->currentIndex()]].clear();editHistory(scores[scoreBox->currentIndex()])->clear();{QSignalBlocker block(targets);targets->setCurrentIndex(0);}savePreferences();calculate();});
      connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
      QTimer::singleShot(0,&dialog,load);dialog.exec();
      QSettings().setValue("export/midi/comparisonSplit",workspace->saveState());
      QSettings().setValue("export/midi/showNoteList",listToggle->isChecked());
      QSettings().setValue("export/midi/foldPitches",fold->isChecked());
      // Worker owns copies only; cancellation does not wait for audio or score state.
      stopPlaybackPreview(true);playhead.stop();canceled->store(true);watcher.disconnect();watcher.cancel();
      }
}
