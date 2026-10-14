// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "mscore/scoreview.h"
#include "libmscore/select.h"
#include "libmscore/part.h"
#include "libmscore/staff.h"
#include <QApplication>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace Ms {
// This filter only listens for the held display key. Gesture filters can be
// removed on mouse release without disabling this independent shortcut.
class PerformancePeekKeys : public QObject {
      PerformanceEditor* _editor;
   public:
      explicit PerformancePeekKeys(PerformanceEditor* editor):QObject(editor),_editor(editor) { qApp->installEventFilter(this); }
      bool eventFilter(QObject* object,QEvent* event) override { return _editor->peekKeyEvent(object,event); }
      };
void PerformanceEditor::setupUsabilityControls()
      {
      QSettings settings;
      _dragSensitivity=settings.value("performanceEditor/dragSensitivity",1.).toDouble();
      if(!std::isfinite(_dragSensitivity)||_dragSensitivity<.25||_dragSensitivity>4)_dragSensitivity=1;
      _keyboardMode=qBound(0,settings.value("performanceEditor/keyboardMode",1).toInt(),2);
      _peekKey=QKeySequence(settings.value("performanceEditor/peekKey","Ctrl+Alt+V").toString())[0];
      new PerformancePeekKeys(this);
      auto add=[this](QWidget* w){_topRow->addWidget(w,0,_topRow->count());};
      auto filters=new QToolButton(this); filters->setText(tr("谱表／声部")); filters->setObjectName("performanceTrackFilter");
      filters->setToolTip(tr("一张表选择可编辑的谱表和声部；编辑器、谱行带、音旁手柄同步使用。"));
      connect(filters,&QToolButton::clicked,this,&PerformanceEditor::showTrackFilter); add(filters);
      auto keyMode=new QComboBox(this); keyMode->setObjectName("performanceKeyboardMode");
      keyMode->addItems({tr("完整键盘"),tr("淡化无关音高"),tr("只显示相关音高")}); keyMode->setCurrentIndex(_keyboardMode);
      keyMode->setToolTip(tr("相关音高来自当前筛选允许显示的音符；收起音高时网格和选音一起调整。"));
      connect(keyMode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int mode){finishWheel();cancelGesture();_keyboardMode=mode;QSettings().setValue("performanceEditor/keyboardMode",mode);rebuildPitchRows();centerPitch(false,true);invalidateVisual();}); add(keyMode);
      auto actual=new QCheckBox(tr("实际力度＋原力度线"),this);actual->setObjectName("performanceActualVelocity");
      actual->setToolTip(tr("显示每个音最终的 MIDI 力度，并用虚线连接各谱表起音处的原力度；不改变音符原本的保存方式。百分比视图的 0% 表示不增减。"));
      connect(actual,&QCheckBox::toggled,this,[this](bool on){finishWheel();cancelGesture();_relativeActual=on;_visibleScoreRect=QRect();_number->setSuffix(rangeKind()==1?" %":QString());_number->setRange(rangeKind()==1?NoteVelocity::minOffset:1,rangeKind()==1?NoteVelocity::maxOffset:127);invalidateVisual();invalidateOverlay();updateSelection();});add(actual);
      connect(_parameter,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this,actual](int i){actual->setVisible(i==0);});
      auto sensitivity=new QComboBox(this);sensitivity->setObjectName("performanceDragSensitivity");
      const QList<double> factors{.25,.5,1,2,4};for(double factor:factors)sensitivity->addItem(tr("拖动 %1 倍").arg(factor),factor);
      sensitivity->setCurrentIndex(qMax(0,sensitivity->findData(_dragSensitivity)));
      sensitivity->setToolTip(tr("只改变鼠标上下拖动调整力度的速度；不改变滚轮步长或显示缩放。"));
      connect(sensitivity,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this,sensitivity](int i){finishWheel();cancelGesture();_dragSensitivity=sensitivity->itemData(i).toDouble();QSettings().setValue("performanceEditor/dragSensitivity",_dragSensitivity);});add(sensitivity);
      auto tools=new QWidget(this);auto bar=new QGridLayout(tools);bar->setContentsMargins(0,0,0,0);bar->setSpacing(1);
      const QStringList labels{tr("选择"),tr("铅笔"),tr("直线")};
      for(int i=0;i<3;++i){auto button=new QToolButton(tools);button->setText(labels[i]);
            QPixmap icon(16,16);icon.fill(Qt::transparent);QPainter ink(&icon);ink.setRenderHint(QPainter::Antialiasing);ink.setPen(QPen(_appearance.colors[PerformanceAppearance::Text],1.4));
            if(i==0){QPolygonF arrow;arrow<<QPointF(3,1)<<QPointF(3,13)<<QPointF(6,10)<<QPointF(9,15)<<QPointF(11,14)<<QPointF(8,9)<<QPointF(13,9);ink.drawPolygon(arrow);}
            else if(i==1){ink.drawLine(QPointF(4,11),QPointF(12,3));ink.drawLine(QPointF(6,13),QPointF(14,5));ink.drawLine(QPointF(12,3),QPointF(14,5));ink.drawLine(QPointF(4,11),QPointF(3,14));ink.drawLine(QPointF(3,14),QPointF(6,13));}
            else {ink.drawLine(QPointF(3,13),QPointF(13,3));ink.drawRect(QRectF(1,11,4,4));ink.drawRect(QRectF(11,1,4,4));}ink.end();button->setIcon(QIcon(icon));button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setCheckable(true);button->setChecked(i==_tool->currentIndex());button->setObjectName(QString("performanceDrawTool%1").arg(i));
            button->setToolTip(i==0?tr("拖动已选音；速度模式下点按添加速度节点。"):(i==1?tr("拖过一段力度柱进行绘制；有选音时只改选音，没有选音时只改筛选允许的音。松开后可一次撤销。速度模式绘制速度。") : tr("从起点到终点画一条直线，调整经过的力度或速度。")));
            connect(button,&QToolButton::clicked,this,[this,i]{_tool->setCurrentIndex(i);});
            connect(_tool,QOverload<int>::of(&QComboBox::currentIndexChanged),button,[button,i](int index){button->setChecked(index==i);});
            connect(_parameter,QOverload<int>::of(&QComboBox::currentIndexChanged),button,[button](int index){button->setEnabled(index!=2);});bar->addWidget(button,0,i);}
      add(tools);
      auto operations=new QToolButton(this);operations->setText(tr("操作设置…"));operations->setObjectName("performanceOperationSettings");
      connect(operations,&QToolButton::clicked,this,[this]{QDialog dialog(this);dialog.setWindowTitle(tr("临时查看音旁力度"));auto layout=new QVBoxLayout(&dialog);
            auto text=new QLabel(tr("按住快捷键：显示鼠标附近的力度；再按住 Shift：显示当前视口全部力度。松开恢复。文字输入、输入音符或编辑标记时不接管按键。"));text->setWordWrap(true);layout->addWidget(text);
            auto key=new QKeySequenceEdit(QKeySequence(_peekKey));layout->addWidget(key);auto box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(box);
            connect(box,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(box,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
            if(dialog.exec()==QDialog::Accepted){_peekKey=key->keySequence().isEmpty()?0:key->keySequence()[0];QSettings().setValue("performanceEditor/peekKey",QKeySequence(_peekKey).toString());}});add(operations);
      _wheelButton->setToolTip(tr("开启后指向音符滚轮调力度。Alt+Shift+滚轮临时选择视口内最近的音并细调 1；本次滚动中固定目标。普通 Shift 粗调 8，Ctrl 仍缩放。停转 300 ms 后写入，Esc 取消。"));
      _axis->setToolTip(tr("MIDI 力度：显示最终力度；相对 %：在力度记号和渐强渐弱给出的力度上增减百分比。切换显示不会转换音符的保存方式。"));
      }
void PerformanceEditor::quickFilter(int mode)
      {
      if(!_score)return; finishWheel();cancelGesture();
      QSet<int> tracks,staves;for(auto note:_score->selection().noteList()){tracks.insert(note->track());staves.insert(note->staffIdx());}
      if(mode&&tracks.isEmpty()){status(tr("请先在谱面选择一个音。"));return;}
      _disabledTracks.clear();_excludedStaves.clear();_voiceMask=15;
      {QSignalBlocker a(_voice),b(_scope);_voice->setCurrentIndex(0);_scope->setCurrentIndex(2);}
      for(int track=0;track<_score->nstaves()*VOICES;++track)if(mode==1?!tracks.contains(track):(mode==2&&!staves.contains(track/VOICES)))_disabledTracks.insert(track);
      rebuildFilter();invalidateVisual();invalidateOverlay();
      }
void PerformanceEditor::showTrackFilter()
      {
      if(!_score){status(tr("请先打开乐谱。"));return;}
      QDialog dialog(this);dialog.setWindowTitle(tr("选择谱表和声部"));dialog.resize(500,360);auto layout=new QVBoxLayout(&dialog);
      auto message=new QLabel(tr("勾选的音可以编辑。未勾选的音可隐藏或淡化为参考，原来在谱面选中的音仍保留选择。跨谱表音按所属谱表筛选。"));message->setWordWrap(true);layout->addWidget(message);
      auto table=new QTableWidget(_score->nstaves(),4);table->setObjectName("performanceTrackTable");table->setHorizontalHeaderLabels({tr("声部 1"),tr("声部 2"),tr("声部 3"),tr("声部 4")});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
      for(int staff=0;staff<_score->nstaves();++staff){table->setVerticalHeaderItem(staff,new QTableWidgetItem(tr("%1 · %2").arg(staff+1).arg(_score->staff(staff)->part()->partName())));
            for(int v=0;v<4;++v){auto item=new QTableWidgetItem;item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsUserCheckable);bool enabled=staffEnabled(staff*VOICES)&&!_disabledTracks.contains(staff*VOICES+v)&&(_voiceMask&(1<<v))&&(!_voice->currentIndex()||v==_voice->currentIndex()-1);
                  const bool scope=_scope->currentIndex()==2||(_scope->currentIndex()==1?staff==_contextTrack/VOICES:_score->staff(staff)->part()==_score->staff(_contextTrack/VOICES)->part());item->setCheckState(enabled&&scope?Qt::Checked:Qt::Unchecked);table->setItem(staff,v,item);}}
      layout->addWidget(table,1);
      auto quick=new QGridLayout;const QStringList choices{tr("全部"),tr("只看选音所在声部"),tr("只看选中谱表")};
      for(int mode=0;mode<3;++mode){auto button=new QPushButton(choices[mode]);button->setObjectName(QString("performanceFilterQuick%1").arg(mode));quick->addWidget(button,0,mode);
            connect(button,&QPushButton::clicked,&dialog,[this,table,mode]{QSet<int> tracks,staves;for(auto note:_score->selection().noteList()){tracks.insert(note->track());staves.insert(note->staffIdx());}if(mode&&tracks.isEmpty())return;
                  for(int staff=0;staff<table->rowCount();++staff)for(int v=0;v<4;++v)table->item(staff,v)->setCheckState(!mode||(mode==1?tracks.contains(staff*VOICES+v):staves.contains(staff))?Qt::Checked:Qt::Unchecked);});}layout->addLayout(quick);
      auto ghost=new QCheckBox(tr("淡化其它音符为参考（取消勾选则隐藏）"));ghost->setChecked(_ghostVoices);layout->addWidget(ghost);
      auto box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(box);connect(box,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(box,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
      if(dialog.exec()!=QDialog::Accepted)return;
      quickFilter(0);for(int staff=0;staff<table->rowCount();++staff)for(int v=0;v<4;++v)if(table->item(staff,v)->checkState()!=Qt::Checked)_disabledTracks.insert(staff*VOICES+v);
      _ghostVoices=ghost->isChecked();rebuildFilter();invalidateVisual();invalidateOverlay();
      if(std::any_of(_notes.cbegin(),_notes.cend(),[](const NoteInfo& n){return n.selected&&!n.enabled;}))status(tr("部分选音已被筛掉，因此不会修改。点击‘谱表／声部’→‘只看选音所在声部’即可显示。"));
      }
void PerformanceEditor::rebuildPitchRows()
      {
      _usedPitches.clear();for(const auto& n:_notes)if(n.enabled)_usedPitches.insert(n.pitch);
      const auto old=_viewport.pitchRows;_viewport.pitchRows.clear();if(_keyboardMode==2){for(int p:_usedPitches)_viewport.pitchRows.append(p);std::sort(_viewport.pitchRows.begin(),_viewport.pitchRows.end());}
      if(old!=_viewport.pitchRows)invalidateVisual();
      }
int PerformanceEditor::keyboardPitch(QPointF point) const
      {
      if(_viewport.pitchRows.isEmpty())return PerformanceKeyboard::pitchAt(point,_viewport.topPitch,_viewport.rowHeight,PerformanceViewport::gutter);
      double row=std::ceil(_viewport.pitchIndex(_viewport.topPitch)-point.y()/_viewport.rowHeight);if(row<0||row>_viewport.rowMaximum())return -1;
      int pitch=qRound(_viewport.pitchAtIndex(row));if(PerformanceKeyboard::isBlack(pitch)&&point.x()>PerformanceViewport::gutter*.58)return -1;return pitch;
      }
void PerformanceEditor::rebuildVisibleScoreNotes()
      {
      if(!_view||!_score||_dirty||_geometryDirty)return;
      auto matrix=_view->matrix();if(_visibleScoreMatrix==matrix&&_visibleScoreRect==_view->rect()&&_visibleScoreState==_state)return;
      _visibleScoreNotes.clear();_visibleScoreMatrix=matrix;_visibleScoreRect=_view->rect();_visibleScoreState=_state;
      bool invertible=false;const auto logicalViewport=matrix.inverted(&invertible).mapRect(QRectF(_view->rect()));if(!invertible)return;
      // Layout already cached every head's bounds. Pointer operations must not
      // walk native parent objects across the whole score again.
      for(int i=0;i<_notes.size();++i){const auto& note=_notes[i];if(!noteEditable(note)||!logicalViewport.intersects(note.bounds))continue;
            QRectF bounds=matrix.mapRect(note.bounds);if(_view->rect().intersects(bounds.toAlignedRect()))_visibleScoreNotes.append({i,bounds});}
      }
bool PerformanceEditor::peekKeyEvent(QObject* object,QEvent* event)
      {
      if(event->type()==QEvent::ApplicationDeactivate){if(_peekHeld){_peekHeld=false;invalidateOverlay();updateSurfaces();syncOverlayTracking();}return false;}
      if(event->type()!=QEvent::KeyPress&&event->type()!=QEvent::KeyRelease&&event->type()!=QEvent::ShortcutOverride)return false;
      auto key=static_cast<QKeyEvent*>(event);if(!_peekKey)return false;
      const int mainKey=_peekKey&~int(Qt::KeyboardModifierMask);
      if(_peekHeld&&key->key()==Qt::Key_Shift&&event->type()!=QEvent::ShortcutOverride){_peekAll=event->type()==QEvent::KeyPress;invalidateOverlay();updateSurfaces();if(_view)_view->update();event->accept();return true;}
      if(_peekHeld&&event->type()==QEvent::KeyRelease&&(key->key()==mainKey||key->key()==Qt::Key_Control||key->key()==Qt::Key_Alt)){
            _peekHeld=false;invalidateOverlay();updateSurfaces();syncOverlayTracking();event->accept();return true;}
      auto widget=qobject_cast<QWidget*>(object);
      if(!widget||(!(widget==_view)&&!isAncestorOf(widget))||!_score||!_view||_view->noteEntryMode()||_view->editMode()||_view->fotoMode())return false;
      if(qobject_cast<QLineEdit*>(widget)||qobject_cast<QAbstractSpinBox*>(widget)||qobject_cast<QTextEdit*>(widget)||qobject_cast<QPlainTextEdit*>(widget))return false;
      const int required=_peekKey&int(Qt::KeyboardModifierMask);
      if(key->key()!=mainKey||(int(key->modifiers())&required)!=required||(int(key->modifiers())&~int(Qt::ShiftModifier))!=(required&~int(Qt::ShiftModifier)))return false;
      if(event->type()==QEvent::KeyRelease)return false;
      event->accept();if(event->type()==QEvent::KeyPress&&!key->isAutoRepeat()){
            _peekHeld=true;_peekAll=bool(key->modifiers()&Qt::ShiftModifier);_overlayPointer=_view->mapFromGlobal(QCursor::pos());
            if(_dirty)refresh();syncOverlayTracking();updateSurfaces();_view->update();}return true;
      }
void PerformanceEditor::paintOriginalVelocity(QPainter& painter,const QRectF& rect,bool onScore)
      {
      if(rangeKind()!=0)return;
      QHash<int,QPointF> previous;QHash<int,bool> labelled;
      const int from=tickForX(rect.left(),onScore),until=tickForX(rect.right(),onScore);
      auto candidates=onScore?_systemIndices:_intervals.query(from,until);std::sort(candidates.begin(),candidates.end());
      for(int i:candidates){const auto& n=_notes[i];if(!n.inScope||(!n.enabled&&!_ghostVoices)||n.base<0)continue;
            double x=xForNote(i,onScore);if(x<rect.left()||x>rect.right())continue;const QPointF p(x,yForValue(n.base,rect,onScore));int staff=n.track/VOICES, line=n.track;
            QColor color=MScore::selectColor[n.track%VOICES];if(!n.enabled)color.setAlpha(45);painter.setPen(QPen(color,1,Qt::DashLine));
            if(previous.contains(line)){auto old=previous.value(line);painter.drawLine(old,QPointF(p.x(),old.y()));painter.drawLine(QPointF(p.x(),old.y()),p);}else painter.drawLine(QPointF(p.x()-4,p.y()),QPointF(p.x()+4,p.y()));
            if(!labelled.value(line)){painter.drawText(QPointF(p.x()+3,p.y()-4),tr("谱表 %1 · 声部 %2 · 原力度 %3").arg(staff+1).arg(n.track%VOICES+1).arg(n.base));labelled[line]=true;}previous[line]=p;}
      }
}
