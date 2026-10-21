// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "editorsettings.h"
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
      connect(&_wheelFeedbackTimer,&QTimer::timeout,this,[this]{
            ++_wheelFeedbackTicks;if(_wheelFeedbackTicks>=30){_wheelFeedbackTimer.stop();}
            if(_wheelFeedbackSurface){auto damage=wheelBadge(_wheelFeedbackSurface).adjusted(-3,-3,3,3);if(_wheelFeedbackSurface==_view)damage=damage.united(_view->matrix().mapRect(_wheelFeedbackHead).adjusted(-6,-6,6,6));_wheelFeedbackSurface->update(damage.toAlignedRect());}
            });
      auto add=[this](QWidget* w){_topRow->addWidget(w);};
      auto filters=new QToolButton(this); filters->setText(tr("谱表／声部")); filters->setObjectName("performanceTrackFilter");
      filters->setToolTip(tr("一张表选择可编辑的谱表和声部；编辑器、谱行带、音旁手柄同步使用。"));
      connect(filters,&QToolButton::clicked,this,&PerformanceEditor::showTrackFilter); add(filters);
      auto keyMode=new QComboBox(this); keyMode->setObjectName("performanceKeyboardMode");
      keyMode->addItems({tr("完整键盘"),tr("淡化无关音高"),tr("只显示相关音高")}); keyMode->setCurrentIndex(_keyboardMode);
      keyMode->setToolTip(tr("相关音高来自当前筛选允许显示的音符；收起音高时网格和选音一起调整。"));
      connect(keyMode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int mode){finishWheel();cancelGesture();_keyboardMode=mode;QSettings().setValue("performanceEditor/keyboardMode",mode);rebuildPitchRows();centerPitch(false,true);invalidateVisual();}); add(keyMode);
      auto actual=new QCheckBox(tr("显示力度参考线"),this);actual->setObjectName("performanceActualVelocity");
      actual->setToolTip(tr("MIDI力度视图中，虚线显示各谱表由力度记号给出的力度。显示方式由上方的MIDI力度／相对%选择；音符继续按原来的相对或固定方式保存。"));
      connect(actual,&QCheckBox::toggled,this,[this](bool on){finishWheel();cancelGesture();_relativeActual=on;_visibleScoreRect=QRect();_number->setSuffix(rangeKind()==1?" %":QString());_number->setRange(rangeKind()==1?NoteVelocity::minOffset:1,rangeKind()==1?NoteVelocity::maxOffset:127);invalidateVisual();invalidateOverlay();updateSelection();});add(actual);
      actual->setChecked(settings.value("performanceEditor/markVelocityLines",true).toBool());
      connect(actual,&QCheckBox::toggled,this,[this](bool on){QSettings().setValue(_comparisonGuides?"performanceEditor/comparisonVelocityLines":"performanceEditor/markVelocityLines",on);});
      auto arrangement=new QComboBox(this);arrangement->setObjectName("performanceVelocityArrangement");arrangement->addItems({tr("力度同图"),tr("按谱表分区")});arrangement->setCurrentIndex(qBound(0,settings.value("performanceEditor/velocityArrangement",0).toInt(),1));_velocityArrangement=arrangement->currentIndex();
      connect(arrangement,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int i){finishWheel();cancelGesture();_velocityArrangement=i;QSettings().setValue("performanceEditor/velocityArrangement",i);invalidateVisual();});add(arrangement);
      auto together=new QComboBox(this);together->setObjectName("performanceGroupVelocity");together->addItems({tr("一起增减百分比"),tr("相同MIDI差值")});_groupRelative=settings.value("performanceEditor/groupRelative",true).toBool();together->setCurrentIndex(_groupRelative?0:1);
      together->setToolTip(tr("百分比方式分别按各音的力度参考线换算；例如80和40同时增加10%，得到88和44。"));
      connect(together,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int i){finishWheel();cancelGesture();_groupRelative=i==0;QSettings().setValue("performanceEditor/groupRelative",_groupRelative);});add(together);
      connect(_parameter,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[actual,arrangement,together](int i){actual->setVisible(i==0);arrangement->setVisible(i==0);together->setVisible(i==0);});
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
      connect(operations,&QToolButton::clicked,this,[this]{showPerformanceSettings(_score,this,[this]{
            const auto key=QKeySequence(QSettings().value("performanceEditor/peekKey").toString());
            _peekKey=key.isEmpty()?0:key[0];
            });});add(operations);
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
      for(const auto& row:_scoreNoteRows) {
            if(!logicalViewport.intersects(row.bounds))continue;
            for(int i:row.indices) {const auto& note=_notes[i];if(!noteEditable(note)||!logicalViewport.intersects(note.bounds))continue;
                  QRectF bounds=matrix.mapRect(note.bounds);if(_view->rect().intersects(bounds.toAlignedRect()))_visibleScoreNotes.append({i,bounds});}
            }
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
      QHash<int,QPointF> previous;QHash<int,QColor> lineColors;
      QSet<QString> labelled;
      QVector<QRectF> labels;
      const int from=tickForX(rect.left(),onScore),until=tickForX(rect.right(),onScore);
      auto candidates=onScore?_systemIndices:_intervals.query(from,until);
      std::sort(candidates.begin(),candidates.end());
      for(int i:candidates){
            const auto& n=_notes[i];
            if(!n.inScope||(!n.enabled&&!_ghostVoices)||n.base<0)continue;
            const double x=xForNote(i,onScore);
            if(x<rect.left()||x>rect.right())continue;
            const auto lane=velocityLane(i,rect,onScore);if(lane.isEmpty())continue;
            const int reference=n.markBase>=0?n.markBase:n.base;
            const QPointF point(x,yForValue(reference,lane,onScore));
            const int staff=n.track/VOICES,line=n.track;
            QColor color=_appearance.staves[staff%8];if(!n.enabled)color.setAlpha(45);
            painter.setPen(QPen(color,1,Qt::DashLine));
            lineColors[line]=color;
            if(previous.contains(line)){auto old=previous.value(line);if(qAbs(old.y()-point.y())>.001){painter.drawLine(old,QPointF(point.x(),old.y()));painter.drawLine(QPointF(point.x(),old.y()),point);previous[line]=point;}}
            else previous[line]=point;
            const QString key=QString("%1:%2:%3").arg(staff).arg(reference).arg(n.dynamicText);
            if(labelled.contains(key))continue;
            const QString text=tr("谱表 %1 · %2 %3").arg(staff+1).arg(n.dynamicText.isEmpty()?tr("默认"):n.dynamicText).arg(reference);
            const double width=painter.fontMetrics().horizontalAdvance(text)+8,height=painter.fontMetrics().height()+2;
            const double offsets[4]={-height-3,3,-2*height-5,height+5};
            for(int attempt=0;attempt<12;++attempt){
                  const double left=qBound(lane.left(),point.x()+4+(attempt/4)*(width+6),qMax(lane.left(),lane.right()-width));
                  QRectF box(left,point.y()+offsets[attempt%4],width,height);
                  if(!lane.contains(box))continue;
                  bool overlaps=false;for(const auto& used:labels)if(box.intersects(used)){overlaps=true;break;}
                  if(overlaps)continue;
                  painter.setPen(QPen(onScore?color.darker(160):color,1));
                  painter.fillRect(box,onScore?QColor("#f1f2ef"):_appearance.colors[PerformanceAppearance::Background]);
                  painter.drawText(box.adjusted(4,0,-4,0),Qt::AlignVCenter,text);
                  labels.append(box);labelled.insert(key);break;
                  }
            }
      for(auto it=previous.cbegin();it!=previous.cend();++it){painter.setPen(QPen(lineColors.value(it.key()),1,Qt::DashLine));painter.drawLine(it.value(),QPointF(rect.right(),it.value().y()));}
      }
void PerformanceEditor::rebuildScoreNoteRows()
      {
      _scoreNoteRows.clear();_visibleScoreRect=QRect();QHash<System*,int> rows;
      for(int i=0;i<_notes.size();++i){const auto& n=_notes[i];if(n.bounds.isEmpty())continue;
            int row=rows.value(n.system,-1);if(row<0){row=_scoreNoteRows.size();rows.insert(n.system,row);_scoreNoteRows.append({{}, {}});}
            auto& r=_scoreNoteRows[row];r.bounds=r.bounds.united(n.bounds);r.indices.append(i);}
      }
QRectF PerformanceEditor::velocityLane(int index,const QRectF& rect,bool onScore) const
      {
      if(onScore || _velocityArrangement==0 || _parameter->currentIndex()!=0)return rect;
      if(index<0 || index>=_notes.size())return {};
      const int staff=_notes[index].track/VOICES;const int row=_velocityStaves.indexOf(staff);if(row<0)return {};
      int expanded=0;for(int s:_velocityStaves)if(!_collapsedVelocityStaves.contains(s))++expanded;
      const double header=17.,body=qMax(12.,(rect.height()-header*_velocityStaves.size())/qMax(1,expanded));double top=rect.top();
      for(int s:_velocityStaves){const bool closed=_collapsedVelocityStaves.contains(s);if(s==staff)return closed?QRectF():QRectF(rect.left(),top+header,rect.width(),body);top+=header+(closed?0:body);}
      return {};
      }
double PerformanceEditor::velocityY(int index,const QRectF& rect,bool onScore) const
      {const auto lane=velocityLane(index,rect,onScore);return lane.isEmpty()?-1e6:yForValue(noteValue(index),lane,onScore);}
bool PerformanceEditor::velocityHeaderClick(QPointF point)
      {
      if(_parameter->currentIndex()!=0 || !_velocityArrangement)return false;
      const auto rect=laneRect();int expanded=0;for(int s:_velocityStaves)if(!_collapsedVelocityStaves.contains(s))++expanded;
      const double body=qMax(12.,(rect.height()-17*_velocityStaves.size())/qMax(1,expanded));double top=rect.top();
      for(int s:_velocityStaves){if(point.y()>=top&&point.y()<top+17){finishWheel();cancelGesture();if(_collapsedVelocityStaves.contains(s))_collapsedVelocityStaves.remove(s);else _collapsedVelocityStaves.insert(s);invalidateVisual();return true;}top+=17+(_collapsedVelocityStaves.contains(s)?0:body);}
      return false;
      }
void PerformanceEditor::paintVelocityHeaders(QPainter& painter,const QRectF& rect)
      {
      if(_parameter->currentIndex()!=0 || !_velocityArrangement)return;
      int expanded=0;for(int s:_velocityStaves)if(!_collapsedVelocityStaves.contains(s))++expanded;
      const double body=qMax(12.,(rect.height()-17*_velocityStaves.size())/qMax(1,expanded));double top=rect.top();
      for(int s:_velocityStaves){const bool closed=_collapsedVelocityStaves.contains(s);painter.fillRect(QRectF(rect.left(),top,rect.width(),17),_appearance.colors[PerformanceAppearance::NaturalRow]);painter.setPen(_appearance.colors[PerformanceAppearance::Text]);
            painter.drawText(QRectF(rect.left()+4,top,rect.width()-8,17),Qt::AlignVCenter,(closed?QString::fromUtf8("▸ "):QString::fromUtf8("▾ "))+tr("谱表 %1 · 参考线／谱中力度").arg(s+1));top+=17+(closed?0:body);}
      }

}
