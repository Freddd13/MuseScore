namespace {
class HandExportCommand final:public QUndoCommand {
      std::function<void(bool)> apply;
public:
      HandExportCommand(const QString& title,std::function<void(bool)> callback):QUndoCommand(title),apply(std::move(callback)){}
      void undo()override{apply(false);}void redo()override{apply(true);}
      };
class HandExportModel final:public QAbstractTableModel {
public:
      QVector<MidiGateNote> rows;MidiGateProcessor clock;
      HandExportModel(QObject* parent):QAbstractTableModel(parent){}
      int rowCount(const QModelIndex& p={})const override{return p.isValid()?0:rows.size();}
      int columnCount(const QModelIndex& p={})const override{return p.isValid()?0:7;}
      QVariant headerData(int col,Qt::Orientation orientation,int role)const override {
            if(orientation!=Qt::Horizontal||role!=Qt::DisplayRole)return {};
            return QStringList{tr("音高"),tr("小节／原谱表／声部"),tr("导出到"),tr("播放时间"),tr("判断依据"),tr("核对"),tr("延音中的接手")}.value(col);
            }
      QVariant data(const QModelIndex& index,int role)const override {
            if(!index.isValid())return {};const auto& n=rows[index.row()];
            if(role==Qt::ForegroundRole&&index.column()==2)return n.exportHand==0?QColor(49,132,185):QColor(213,136,48);
            if(role!=Qt::DisplayRole&&role!=Qt::ToolTipRole)return {};
            switch(index.column()) {
                  case 0:return pitchText(n.pitch);
                  case 1:return QString("%1 / %2 / %3").arg(n.sourceMeasure).arg(n.staff+1).arg(n.voice%VOICES+1);
                  case 2:return n.exportHand==0?tr("左手"):n.exportHand==1?tr("右手"):tr("原音轨");
                  case 3:return QString::number(clock.durationMs(0,n.on)/1000.,'f',3)+" s";
                  case 4:return n.exportHandSource;
                  case 5:return n.handReview?tr("需核对"):QString();
                  default:{QStringList text;for(const auto& p:n.instructions)if(p.handSet)text.append(tr("第%1小节第%2拍：%3").arg(p.measure).arg(p.beat,0,'g',4).arg(p.hand==0?tr("左手"):p.hand==1?tr("右手"):tr("待核对")));return text.join(tr("；"));}
                  }
            }
      void replace(const QVector<MidiGateNote>& value){beginResetModel();rows=value;endResetModel();}
      };
}
void MidiCropPanel::compareHands() {
      if(seq&&(seq->isPlaying()||!seq->backgroundRenderingIdle())){QMessageBox::information(this,tr("左右手分轨"),tr("请先停播，再准备左右手预览。"));return;}
      const auto scores=_scores();if(scores.isEmpty())return;
      QDialog dialog(this);dialog.setObjectName("midiHandComparison");dialog.setWindowTitle(tr("MIDI导出 · 检查左右手"));dialog.setWindowFlag(Qt::WindowMaximizeButtonHint,true);
      dialog.resize(QSize(1000,690).boundedTo(dialog.screen()->availableGeometry().size()-QSize(24,48)));
      auto layout=new QVBoxLayout(&dialog);layout->setSpacing(6);auto controls=new QWidget(&dialog);auto flow=new ControlFlowLayout(controls);layout->addWidget(controls);
      auto scoreBox=new QComboBox(controls);scoreBox->setObjectName("midiHandScore");for(auto score:scores)scoreBox->addItem(score->title());scoreBox->setMaximumWidth(240);flow->addWidget(scoreBox);
      auto filter=new QComboBox(controls);filter->setObjectName("midiHandFilter");filter->addItems({tr("全部钢琴音"),tr("与显示谱表不同"),tr("需要核对")});flow->addWidget(filter);
      auto use=new QCheckBox(tr("导出时按左右手分轨"),controls);use->setChecked(splitsHands());flow->addWidget(use);connect(use,&QCheckBox::toggled,_splitHands,&QCheckBox::setChecked);connect(_splitHands,&QCheckBox::toggled,use,&QCheckBox::setChecked);
      auto legend=new QLabel(tr("蓝色：左手    金色：右手    红色虚线：需要核对"),&dialog);layout->addWidget(legend);
      auto fold=new QCheckBox(tr("只看有音的行"),controls);fold->setChecked(true);flow->addWidget(fold);
      auto summary=new QLabel(tr("正在准备音符…"),&dialog);summary->setObjectName("midiHandSummary");summary->setWordWrap(true);layout->addWidget(summary);
      auto splitter=new QSplitter(Qt::Vertical,&dialog);splitter->setChildrenCollapsible(false);layout->addWidget(splitter,1);
      auto roll=new GateRoll(splitter);roll->setObjectName("midiHandRoll");roll->handsMode=true;roll->foldUnused=true;
      auto table=new QTableView(splitter);table->setObjectName("midiHandNotes");auto model=new HandExportModel(table);table->setModel(model);
      table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::ExtendedSelection);table->setMinimumHeight(100);
      table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);for(int i=0;i<7;++i)table->setColumnWidth(i,QVector<int>{60,155,70,85,190,70,210}[i]);table->horizontalHeader()->setStretchLastSection(true);splitter->setSizes({300,230});
      connect(fold,&QCheckBox::toggled,roll,[roll](bool on){roll->setFoldUnused(on);});
      auto detail=new QLabel(tr("点选音符查看分手；在表格中按住Ctrl或Shift可选多个音。"),&dialog);detail->setWordWrap(true);detail->setMinimumHeight(detail->fontMetrics().lineSpacing()*2);layout->addWidget(detail);
      auto editRow=new QWidget(&dialog);auto editFlow=new ControlFlowLayout(editRow);layout->addWidget(editRow);
      auto left=new QPushButton(tr("选中音用左手"),editRow),right=new QPushButton(tr("选中音用右手"),editRow),automatic=new QPushButton(tr("选中音恢复自动"),editRow),range=new QPushButton(tr("选中谱段中的音"),editRow),fit=new QPushButton(tr("匹配画面"),editRow);
      left->setObjectName("midiHandLeft");right->setObjectName("midiHandRight");automatic->setObjectName("midiHandAutomatic");range->setObjectName("midiHandSelectRange");
      for(auto button:{left,right,automatic,range,fit})editFlow->addWidget(button);
      auto histories=new QUndoGroup(&dialog);auto undo=histories->createUndoAction(&dialog,tr("撤销分手修改")),redo=histories->createRedoAction(&dialog,tr("重做分手修改"));
      undo->setObjectName("midiHandUndo");redo->setObjectName("midiHandRedo");undo->setShortcut(QKeySequence::Undo);redo->setShortcut(QKeySequence::Redo);undo->setIcon(getAction("undo")->icon());redo->setIcon(getAction("redo")->icon());
      for(auto action:{undo,redo}){dialog.addAction(action);auto button=new QToolButton(editRow);button->setDefaultAction(action);button->setToolButtonStyle(Qt::ToolButtonIconOnly);editFlow->addWidget(button);}
      auto close=new QPushButton(tr("返回导出"),editRow);close->setObjectName("midiHandReturn");editFlow->addWidget(close);connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
      QPointer<Score> target=scores.front();MidiGateData data;QVector<MidiGateNote> rows;bool busy=false,again=false,fitPending=true,presenting=false;
      QSet<QString> selected;auto canceled=std::make_shared<std::atomic_bool>(false);QFutureWatcher<QVector<MidiGateNote>> watcher;
      auto editEnabled=[&]{bool manual=false;for(const auto& id:selected)manual|=_handOverrides.value(target).contains(id);left->setEnabled(!busy&&!selected.isEmpty());right->setEnabled(!busy&&!selected.isEmpty());automatic->setEnabled(!busy&&manual);range->setEnabled(!busy&&target&&target->selection().isRange());};
      std::function<void()> updateDetail;
      auto present=[&]{
            QScopedValueRollback<bool> guard(presenting,true);const auto remembered=selected;
            QVector<MidiGateNote> shown;int reviewed=0,changed=0,leftCount=0,rightCount=0;
            for(const auto& n:rows)if(n.piano){reviewed+=n.handReview;changed+=n.exportHand!=n.handHint;leftCount+=n.exportHand==0;rightCount+=n.exportHand==1;if(filter->currentIndex()==1&&n.exportHand==n.handHint||filter->currentIndex()==2&&!n.handReview)continue;shown.append(n);}
            QSet<QString> visibleIds;for(const auto& n:shown)visibleIds.insert(n.id);selected=remembered;selected.intersect(visibleIds);
            QSignalBlocker block(table->selectionModel());model->clock.setClock(data);model->replace(shown);roll->notes=shown;roll->selectedIds=selected;roll->selected=-1;roll->pedals=data.pedals;roll->clock.setClock(data);roll->indexNotes();
            for(int i=0;i<shown.size();++i)if(selected.contains(shown[i].id)){table->selectionModel()->select(model->index(i,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);table->selectionModel()->setCurrentIndex(model->index(i,0),QItemSelectionModel::NoUpdate);roll->selected=i;}
            summary->setText(tr("%1 · 左手%2音，右手%3音 · 与显示谱表不同%4音 · 需核对%5音").arg(target?target->title():QString()).arg(leftCount).arg(rightCount).arg(changed).arg(reviewed));roll->refresh();if(fitPending){roll->fitContent();fitPending=false;}editEnabled();if(updateDetail)updateDetail();
            };
      std::function<void()> calculate=[&]{if(busy){again=true;return;}if(!target)return;busy=true;roll->setEnabled(false);auto opts=options(target);opts.splitHands=true;const auto snapshot=data;summary->setText(tr("正在根据分手记号与前后手位计算…"));for(auto button:{left,right,automatic,range})button->setEnabled(false);scoreBox->setEnabled(false);
            watcher.setFuture(QtConcurrent::run([snapshot,opts,canceled]{return prepareMidiPostprocess(snapshot,opts,[canceled]{return canceled->load();});}));};
      connect(&watcher,&QFutureWatcher<QVector<MidiGateNote>>::finished,&dialog,[&]{rows=watcher.result();busy=false;if(again){again=false;calculate();return;}scoreBox->setEnabled(true);roll->setEnabled(true);present();});
      auto load=[&]{target=scores[scoreBox->currentIndex()];selected.clear();fitPending=true;if(!target)return;
            if(!_handHistories.contains(target)){auto history=new QUndoStack(this);_handHistories[target]=history;connect(target,&QObject::destroyed,this,[this,score=target.data()]{auto stack=_handHistories.take(score);if(stack)stack->deleteLater();_handOverrides.remove(score);_handStates.remove(score);});}
            auto history=_handHistories.value(target);histories->addStack(history);histories->setActiveStack(history);
            if(_handStates.value(target)!=target->masterScore()->state()){_handOverrides[target].clear();history->clear();}_handStates[target]=target->masterScore()->state();
            ExportMidi exporter(target);exporter.collectGateData=true;MidiExportOptions opts;opts.writtenPedal=usesWrittenPedal();QBuffer bytes;bytes.open(QIODevice::WriteOnly);
            const auto render=renderSettings();
            if(!exporter.write(&bytes,render.expandRepeats,render.exportRPNs,mscore->synthesizerState(),opts)){summary->setText(tr("无法准备这份乐谱的MIDI。"));return;}data=exporter.gateData;calculate();};
      connect(scoreBox,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&]{load();});connect(filter,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&]{present();});
      connect(table->selectionModel(),&QItemSelectionModel::selectionChanged,&dialog,[&]{if(presenting)return;selected.clear();for(const auto& index:table->selectionModel()->selectedRows())selected.insert(model->rows[index.row()].id);roll->selectedIds=selected;roll->refresh();editEnabled();if(updateDetail)updateDetail();});
      updateDetail=[&]{const auto index=table->currentIndex();if(!index.isValid()){detail->setText(tr("已选%1音。在表格中按Ctrl或Shift可选多个音，再指定左手或右手。").arg(selected.size()));return;}roll->selected=index.row();roll->revealSelected();const auto& n=model->rows[index.row()];detail->setText(tr("%1 · 第%2小节 · %3 · %4。导出手按起音时刻安排；延音接手见列表。已选%5音。").arg(pitchText(n.pitch)).arg(n.sourceMeasure).arg(n.exportHand==0?tr("左手"):tr("右手")).arg(n.exportHandSource).arg(selected.size()));};
      connect(table->selectionModel(),&QItemSelectionModel::currentRowChanged,&dialog,[&]{updateDetail();});
      roll->selectRow=[&](int row){table->setCurrentIndex(model->index(row,0));table->selectRow(row);};
      auto setHand=[&](int hand){if(busy||!target||selected.isEmpty())return;auto before=_handOverrides.value(target),after=before;for(const auto& id:selected){if(hand<0)after.remove(id);else after[id]=hand;}if(before==after)return;
            QPointer<Score> score=target;const bool wasEnabled=splitsHands();auto apply=[this,score,before,after,wasEnabled](bool forward){if(!score)return;_handOverrides[score]=forward?after:before;_handStates[score]=score->masterScore()->state();_splitHands->setChecked(forward?true:wasEnabled);emit exportEditsChanged(score);};_handHistories[target]->push(new HandExportCommand(hand<0?tr("恢复自动分手"):tr("指定导出分手"),apply));};
      connect(left,&QPushButton::clicked,&dialog,[&]{setHand(0);});connect(right,&QPushButton::clicked,&dialog,[&]{setHand(1);});connect(automatic,&QPushButton::clicked,&dialog,[&]{setHand(-1);});
      connect(range,&QPushButton::clicked,&dialog,[&]{if(!target||!target->selection().isRange()){detail->setText(tr("先在谱面框选要处理的一段，再点击这里。"));return;}const int first=target->selection().tickStart().ticks(),end=target->selection().tickEnd().ticks();const int staff=target->selection().staffStart(),until=target->selection().staffEnd();selected.clear();for(const auto& n:rows)if(n.piano&&n.sourceTick>=first&&n.sourceTick<end&&n.staff>=staff&&n.staff<until)selected.insert(n.id);present();});
      connect(fit,&QPushButton::clicked,roll,[&]{roll->fitContent();});connect(this,&MidiCropPanel::exportEditsChanged,&dialog,[&](Score* score){if(score==target)calculate();});
      QTimer::singleShot(0,&dialog,load);dialog.exec();canceled->store(true);watcher.disconnect();watcher.cancel();
      }
