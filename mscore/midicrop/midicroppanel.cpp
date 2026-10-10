// SPDX-License-Identifier: GPL-2.0-or-later
#include "midicroppanel.h"
#include "audio/exports/exportmidi.h"
#include "libmscore/score.h"
#include "libmscore/part.h"
#include "libmscore/staff.h"
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
            {"pedal-release",MidiCropPanel::tr("Earlier release within the same pedal")},
            {"unselected",MidiCropPanel::tr("Instrument not selected")},
            {"unknown-source",MidiCropPanel::tr("Source not uniquely identified")},
            {"manual-performance",MidiCropPanel::tr("Manual performance / grace / ornament")},
            {"ambiguous-retrigger",MidiCropPanel::tr("Ambiguous same-pitch retrigger")},
            {"excluded",MidiCropPanel::tr("Excluded by user")},
            {"short-note",MidiCropPanel::tr("Already shorter than minimum")},
            {"no-shortening",MidiCropPanel::tr("No safe shortening")},
            {"no-pedal",MidiCropPanel::tr("No holding pedal")},
            {"unsupported-pedal",MidiCropPanel::tr("Unsupported pedal/control semantics")},
            {"pedal-boundary",MidiCropPanel::tr("Crosses pedal-up boundary")}
            };
      return names.value(code,code);
      }
QString pitchText(int pitch) {
      static const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
      return QString::fromLatin1(names[qBound(0,pitch,127)%12])+QString::number(pitch/12-1);
      }
class GateRoll : public QAbstractScrollArea {
   public:
      QVector<MidiGateNote> notes;
      QVector<MidiGatePedal> pedals;
      double scale=.15;
      bool stacked=false;
      int selected=-1,low=127,high=0,lastTick=0;
      QMap<int,QVector<int>> buckets;
      void indexNotes() {buckets.clear();low=127;high=0;lastTick=0;for(int i=0;i<notes.size();++i) {low=qMin(low,notes[i].pitch);high=qMax(high,notes[i].pitch);lastTick=qMax(lastTick,notes[i].off);for(int b=notes[i].on/4800;b<=notes[i].off/4800;++b)buckets[b].append(i);}}
      GateRoll(QWidget* p):QAbstractScrollArea(p) {setMinimumHeight(210);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
      void refresh() {
            int end=lastTick;
            horizontalScrollBar()->setRange(0,qMax(0,int(end*scale)-viewport()->width()+30));
            horizontalScrollBar()->setPageStep(viewport()->width());viewport()->update();
            }
      void resizeEvent(QResizeEvent* e) override {QAbstractScrollArea::resizeEvent(e);refresh();}
      void paintEvent(QPaintEvent*) override {
            QPainter p(viewport());p.fillRect(viewport()->rect(),palette().base());
            if(notes.isEmpty())return;
            const int scroll=horizontalScrollBar()->value(),bands=stacked?2:1;
            const double row=qMin(10.,double(viewport()->height()-55)/qMax(1,(high-low+1)*bands));
            const int height=qMax(1,int((high-low+1)*row));
            const int firstTick=qMax(0,int(scroll/scale)/960*960),lastTick=int((scroll+viewport()->width())/scale);
            p.setPen(palette().mid().color());
            for(int tick=firstTick;tick<=lastTick;tick+=960) {const int x=qRound(tick*scale)-scroll;p.drawLine(x,18,x,viewport()->height()-18);p.drawText(x+3,13,QString::number(tick));}
            QSet<int> visible;for(int b=int(scroll/scale)/4800;b<=int((scroll+viewport()->width())/scale)/4800;++b)for(int index:buckets.value(b))visible.insert(index);
            for(int i:visible) {
                  const auto& n=notes[i];const double x=n.on*scale-scroll,w=(n.off-n.on)*scale;
                  if(x+w<0 || x>viewport()->width())continue;
                  const double y=(high-n.pitch)*row+22;
                  const bool changed=n.newOff<n.off;
                  const QColor color=i==selected?palette().highlight().color():palette().text().color();
                  p.setPen(color);p.setBrush(Qt::NoBrush);p.drawRect(QRectF(x,y,qMax(1.,w),qMax(2.,row-1)));
                  QColor fill=changed?palette().highlight().color():palette().mid().color();fill.setAlpha(changed?185:65);
                  p.setPen(Qt::NoPen);p.setBrush(fill);p.drawRect(QRectF(x,y+(stacked?height+8:0),qMax(1.,(n.newOff-n.on)*scale),qMax(2.,row-1)));
                  }
            const int base=viewport()->height()-18;
            p.setPen(palette().mid().color());p.drawLine(0,base,viewport()->width(),base);
            for(const auto& pedal:pedals) {
                  double x=pedal.down*scale-scroll,w=(pedal.up-pedal.down)*scale;
                  if(x+w<0 || x>viewport()->width())continue;
                  p.fillRect(QRectF(x,base+2,w,8),palette().mid());
                  }
            }
      void scrollContentsBy(int,int) override {viewport()->update();}
      };
// QTableView requests only visible cells; no widget is allocated per note.
class GateModel : public QAbstractTableModel {
   public:
      QVector<MidiGateNote> rows;
      QVector<int> visible;
      MidiGateProcessor clock;
      GateModel(QObject* parent):QAbstractTableModel(parent) {}
      int rowCount(const QModelIndex& p={}) const override {return p.isValid()?0:visible.size();}
      int columnCount(const QModelIndex& p={}) const override {return p.isValid()?0:5;}
      QVariant headerData(int section,Qt::Orientation orientation,int role) const override {
            if(role!=Qt::DisplayRole || orientation!=Qt::Horizontal)return {};
            return QStringList{MidiCropPanel::tr("Note"),MidiCropPanel::tr("Occurrence (ticks)"),MidiCropPanel::tr("Original ms"),MidiCropPanel::tr("New ms"),MidiCropPanel::tr("Reason")}.value(section);
            }
      QVariant data(const QModelIndex& index,int role) const override {
            if(!index.isValid() || role!=Qt::DisplayRole)return {};
            const auto& n=rows[visible[index.row()]];
            switch(index.column()) {
                  case 0:return pitchText(n.pitch);
                  case 1:return n.on;
                  case 2:return qRound(clock.durationMs(n.on,n.off));
                  case 3:return qRound(clock.durationMs(n.on,n.newOff));
                  default:return reasonText(n.reason);
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
      _enabled=new QCheckBox(tr("Humanized release: conservative pedal mode"),this);
      _enabled->setChecked(preferences.getBool("export/midi/cropEnabled"));layout->addWidget(_enabled);
      auto buttons=new QHBoxLayout;auto config=new QPushButton(tr("Release settings…"),this);auto preview=new QPushButton(tr("Compare…"),this);
      buttons->addWidget(config);buttons->addWidget(preview);buttons->addStretch();layout->addLayout(buttons);
      _original=new QCheckBox(tr("Also export original MIDI"),this);_original->setChecked(preferences.getBool("export/midi/cropOriginal"));layout->addWidget(_original);
      _gate.lengthRatio=preferences.getDouble("export/midi/cropLength");_gate.nextAttackRatio=preferences.getDouble("export/midi/cropNext");
      _gate.minimumMs=preferences.getDouble("export/midi/cropMinimumMs");_gate.jitter=preferences.getDouble("export/midi/cropJitter");_gate.seed=preferences.getInt("export/midi/cropSeed");
      connect(_enabled,&QCheckBox::toggled,this,[this](bool){savePreferences();});
      connect(_original,&QCheckBox::toggled,this,[this](bool){savePreferences();});
      connect(config,&QPushButton::clicked,this,&MidiCropPanel::settings);
      connect(preview,&QPushButton::clicked,this,&MidiCropPanel::compare);
      }
bool MidiCropPanel::enabled() const {return _enabled->isChecked();}
bool MidiCropPanel::includeOriginal() const {return _original->isChecked();}
MidiExportOptions MidiCropPanel::options(Score* score) const {
      MidiExportOptions result;result.crop=enabled();result.gate=_gate;
      result.gate.excluded=_exclusions.value(score);result.gate.staves=_staves.value(score);return result;
      }
void MidiCropPanel::savePreferences() const {
      preferences.setPreference("export/midi/cropEnabled",enabled());preferences.setPreference("export/midi/cropOriginal",includeOriginal());
      preferences.setPreference("export/midi/cropLength",_gate.lengthRatio);preferences.setPreference("export/midi/cropNext",_gate.nextAttackRatio);
      preferences.setPreference("export/midi/cropMinimumMs",_gate.minimumMs);preferences.setPreference("export/midi/cropJitter",_gate.jitter);preferences.setPreference("export/midi/cropSeed",int(_gate.seed));
      }
void MidiCropPanel::settings() {
      QDialog dialog(this);dialog.setWindowTitle(tr("Conservative MIDI release"));auto form=new QFormLayout(&dialog);
      auto length=new QDoubleSpinBox(&dialog);length->setRange(10,100);length->setValue(_gate.lengthRatio*100);length->setSuffix(" %");form->addRow(tr("Original length"),length);
      auto next=new QDoubleSpinBox(&dialog);next->setRange(10,100);next->setValue(_gate.nextAttackRatio*100);next->setSuffix(" %");form->addRow(tr("Next attack interval"),next);
      auto minimum=new QSpinBox(&dialog);minimum->setRange(1,5000);minimum->setValue(int(_gate.minimumMs));minimum->setSuffix(" ms");form->addRow(tr("Minimum gate"),minimum);
      auto jitter=new QDoubleSpinBox(&dialog);jitter->setRange(0,5);jitter->setValue(_gate.jitter*100);jitter->setSuffix(" %");form->addRow(tr("Release jitter ± (0 disables)"),jitter);
      auto seed=new QSpinBox(&dialog);seed->setRange(0,INT_MAX);seed->setValue(int(_gate.seed));form->addRow(tr("Repeatable seed"),seed);
      auto info=new QLabel(tr("Only piano notes with both release times in the same supported pedal window are shortened. Onsets and controllers remain unchanged."),&dialog);info->setWordWrap(true);form->addRow(info);
      auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
      connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
      if(dialog.exec()==QDialog::Accepted) {_gate.lengthRatio=length->value()/100.;_gate.nextAttackRatio=next->value()/100.;_gate.minimumMs=minimum->value();_gate.jitter=jitter->value()/100.;_gate.seed=quint32(seed->value());savePreferences();}
      }
bool MidiCropPanel::write(Score* score,const QString& path,const QString& original) {
      if(seq && seq->isPlaying()) {QMessageBox::information(this,tr("MIDI release"),tr("Stop playback before preparing MIDI."));return false;}
      ExportMidi exporter(score);QBuffer buffer;buffer.open(QIODevice::WriteOnly);
      if(!exporter.write(&buffer,preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),preferences.getBool(PREF_IO_MIDI_EXPORTRPNS),mscore->synthesizerState(),options(score)))return false;
      auto store=[&](const QString& name,const QByteArray& bytes) {QSaveFile f(name);if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()) {QMessageBox::warning(this,tr("MIDI export"),tr("Could not write %1: %2").arg(name,f.errorString()));return false;}return true;};
      if(!original.isEmpty()) {
            QBuffer bytes;bytes.open(QIODevice::WriteOnly);
            if(exporter.gateBaseline.write(&bytes) || !store(original,bytes.data()))return false;
            }
      return store(path,buffer.data());
      }
void MidiCropPanel::compare() {
      if(seq && seq->isPlaying()) {QMessageBox::information(this,tr("MIDI release"),tr("Stop playback before preparing a comparison."));return;}
      const auto scores=_scores();if(scores.isEmpty())return;
      QDialog dialog(this);dialog.setWindowTitle(tr("MIDI release comparison"));dialog.resize(920,650);
      auto layout=new QVBoxLayout(&dialog);auto bar=new QHBoxLayout;
      auto scoreBox=new QComboBox(&dialog);for(auto score:scores)scoreBox->addItem(score->title());bar->addWidget(scoreBox);
      auto part=new QComboBox(&dialog);bar->addWidget(part);auto filter=new QComboBox(&dialog);filter->addItems({tr("All"),tr("Shortened"),tr("Preserved")});bar->addWidget(filter);
      auto display=new QComboBox(&dialog);display->addItems({tr("Overlay"),tr("Stacked")});bar->addWidget(display);
      auto zoom=new QSlider(Qt::Horizontal,&dialog);zoom->setRange(1,40);zoom->setValue(10);bar->addWidget(zoom);layout->addLayout(bar);
      auto caption=new QLabel(tr("Outline: original · Solid: export · Pedal: bottom lane · Positions include expanded repeats and pauses"),&dialog);caption->setWordWrap(true);layout->addWidget(caption);
      auto roll=new GateRoll(&dialog);layout->addWidget(roll,2);
      auto table=new QTableView(&dialog);auto model=new GateModel(table);table->setModel(model);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table->setColumnWidth(0,65);table->setColumnWidth(1,120);table->setColumnWidth(2,100);table->setColumnWidth(3,100);table->horizontalHeader()->setStretchLastSection(true);layout->addWidget(table,2);
      auto buttons=new QHBoxLayout;auto exclude=new QPushButton(tr("Toggle exclusion"),&dialog);auto recompute=new QPushButton(tr("Recompute"),&dialog);auto reset=new QPushButton(tr("Restore defaults"),&dialog);auto close=new QPushButton(tr("Return to export"),&dialog);
      buttons->addWidget(exclude);buttons->addWidget(recompute);buttons->addWidget(reset);buttons->addStretch();buttons->addWidget(close);layout->addLayout(buttons);
      auto targets=new QComboBox(&dialog);auto targetRow=new QHBoxLayout;targetRow->addWidget(new QLabel(tr("Crop piano part:"),&dialog));targetRow->addWidget(targets);targetRow->addStretch();layout->insertLayout(1,targetRow);
      MidiGateData data;QVector<MidiGateNote> rows;bool loading=false,queued=false;
      QFutureWatcher<QVector<MidiGateNote>> watcher;
      auto canceled=std::make_shared<std::atomic_bool>(false);
      auto present=[&] {model->setRows(rows,data,part->currentData().toInt(),filter->currentIndex());roll->notes.clear();for(int i:model->visible)roll->notes.append(rows[i]);roll->pedals=data.pedals;roll->selected=-1;roll->indexNotes();roll->refresh();};
      auto calculate=[&] {
            if(loading) {queued=true;return;}const auto snapshot=data;auto gate=options(scores[scoreBox->currentIndex()]).gate;loading=true;
            caption->setText(tr("Calculating… Closing cancels the task."));exclude->setEnabled(false);recompute->setEnabled(false);scoreBox->setEnabled(false);targets->setEnabled(false);
            watcher.setFuture(QtConcurrent::run([snapshot,gate,canceled] {MidiGateProcessor p;return p.process(snapshot,gate,[canceled]{return canceled->load();});}));
            };
      connect(&watcher,&QFutureWatcher<QVector<MidiGateNote>>::finished,&dialog,[&] {rows=watcher.result();loading=false;exclude->setEnabled(true);recompute->setEnabled(true);scoreBox->setEnabled(true);targets->setEnabled(true);caption->setText(tr("Outline: original · Solid: export · Pedal: bottom lane"));present();if(queued) {queued=false;calculate();}});
      auto load=[&] {
            ExportMidi exporter(scores[scoreBox->currentIndex()]);exporter.collectGateData=true;QBuffer bytes;bytes.open(QIODevice::WriteOnly);
            if(!exporter.write(&bytes,preferences.getBool(PREF_IO_MIDI_EXPANDREPEATS),preferences.getBool(PREF_IO_MIDI_EXPORTRPNS),mscore->synthesizerState()))return;
            targets->blockSignals(true);targets->clear();targets->addItem(tr("All piano parts"),-1);
            for(const auto piano:scores[scoreBox->currentIndex()]->parts())if(piano->instrumentId().contains("piano",Qt::CaseInsensitive))targets->addItem(piano->instrumentName(),piano->startTrack());
            const auto chosen=_staves.value(scores[scoreBox->currentIndex()]);for(int i=1;i<targets->count();++i)if(chosen.contains(targets->itemData(i).toInt()/VOICES))targets->setCurrentIndex(i);targets->blockSignals(false);
            data=exporter.gateData;part->blockSignals(true);part->clear();part->addItem(tr("All instruments"),-1);QMap<int,QString> names;for(const auto& n:data.notes)if(n.staff>=0)names[n.staff]=n.partName+tr(" · staff %1").arg(n.staff+1);for(auto i=names.cbegin();i!=names.cend();++i)part->addItem(i.value(),i.key());part->blockSignals(false);calculate();
            };
      connect(targets,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int) {
            auto score=scores[scoreBox->currentIndex()];auto& selection=_staves[score];selection.clear();
            const int start=targets->currentData().toInt();if(start>=0)for(const auto p:score->parts())if(p->startTrack()==start)for(int track=p->startTrack();track<p->endTrack();track+=VOICES)selection.insert(track/VOICES);
            calculate();
            });
      connect(scoreBox,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){load();});
      connect(part,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){present();});connect(filter,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){present();});
      connect(display,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int i){roll->stacked=i==1;roll->refresh();});connect(zoom,&QSlider::valueChanged,&dialog,[&](int i){roll->scale=i*.015;roll->refresh();});
      connect(table->selectionModel(),&QItemSelectionModel::currentRowChanged,&dialog,[&](const QModelIndex& index,const QModelIndex&){roll->selected=index.row();if(index.isValid())roll->horizontalScrollBar()->setValue(qMax(0,int(roll->notes[index.row()].on*roll->scale)-20));roll->refresh();});
      connect(exclude,&QPushButton::clicked,&dialog,[&] {const auto index=table->currentIndex();if(!index.isValid())return;const auto& n=rows[model->visible[index.row()]];auto& set=_exclusions[scores[scoreBox->currentIndex()]];if(set.contains(n.id))set.remove(n.id);else set.insert(n.id);calculate();});
      connect(recompute,&QPushButton::clicked,&dialog,calculate);
      connect(reset,&QPushButton::clicked,&dialog,[&] {_gate=MidiGateOptions();_exclusions[scores[scoreBox->currentIndex()]].clear();savePreferences();calculate();});
      connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
      QTimer::singleShot(0,&dialog,load);dialog.exec();
      // Worker owns copies only; cancellation does not wait for audio or score state.
      canceled->store(true);watcher.disconnect();watcher.cancel();
      }
}
