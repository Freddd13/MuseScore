// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "workflowlanes.h"
#include "performanceeditor.h"
#include "../workflow/workflowcontext.h"
#include "../workflow/referencefactory.h"
#include "../workflow/toolicons.h"
#include "../scoreview.h"
#include "../shortcut.h"
#include "../musescore.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QTimer>
#include <QVBoxLayout>
#include <QToolButton>
#include <QToolBar>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QComboBox>
#include <QMenu>
#include <QLabel>
#include <algorithm>
#include <cmath>
namespace Ms {
namespace {
Note* displayedNote(Score* score,const QVariantMap& value){
      if(!score||!value.contains("tick")||!value.contains("track"))return nullptr;
      const int tick=value.value("tick").toInt(),track=value.value("track").toInt();
      if(tick<0||track<0||track>=score->ntracks())return nullptr;
      auto element=score->findCR(Fraction::fromTicks(tick),track);
      if(!element||!element->isChord())return nullptr;
      auto chord=toChord(element);
      if(value.contains("graceIndex")&&!value.value("graceIndex").isNull()){
            const int grace=value.value("graceIndex").toInt();
            if(grace<0||grace>=int(chord->graceNotes().size()))return nullptr;
            chord=chord->graceNotes()[size_t(grace)];
      }
      const int index=value.value("index",-1).toInt();
      if(index<0||index>=int(chord->notes().size()))return nullptr;
      auto note=chord->notes()[size_t(index)];
      return value.contains("writtenPitch")&&note->pitch()!=value.value("writtenPitch").toInt()?nullptr:note;
}
QString selectedPitch(const QVariantMap& note,bool performed=false){
      const int pitch=note.value(performed?"performedPitch":"writtenPitch",note.value("pitch",60)).toInt();
      const auto spelling=note.value("writtenTpc");
      if(!performed&&!spelling.isNull()&&spelling.isValid()&&spelling.toInt()>=-1&&spelling.toInt()<=33){
            const int tpc=spelling.toInt(),degree=(tpc%7+7)%7,accidental=int(std::floor((tpc-13)/7.));
            static const QStringList letters={"C","G","D","A","E","B","F"};static const int natural[]={0,7,2,9,4,11,5};
            return letters[degree]+(accidental<0?QString(-accidental,QChar(0x266d)):QString(accidental,QChar(0x266f)))+QString::number(int(std::floor((pitch-natural[degree]-accidental)/12.))-1);
      }
      static const QStringList names={"C","C♯","D","E♭","E","F","F♯","G","A♭","A","B♭","B"};
      return names[qBound(0,pitch,127)%12]+QString::number(pitch/12-1);
}
double audioAt(const QVariantMap& a,double tick){auto points=a.value("anchors").toList();points.prepend(QVariantMap{{"tick",a.value("start")},{"ms",a.value("from")}});points.append(QVariantMap{{"tick",a.value("end")},{"ms",a.value("until")}});for(int i=1;i<points.size();++i){auto p=points[i-1].toMap(),q=points[i].toMap();if(tick<=q.value("tick").toDouble())return p.value("ms").toDouble()+(tick-p.value("tick").toDouble())*(q.value("ms").toDouble()-p.value("ms").toDouble())/qMax(1.,q.value("tick").toDouble()-p.value("tick").toDouble());}return a.value("until").toDouble();}
}
WorkflowLanes::WorkflowLanes(PerformanceEditor* editor):QWidget(editor),_editor(editor){
      setObjectName("performanceWorkflowLanes");setMinimumHeight(100);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);connect(workflowContext(),&WorkflowContext::changed,this,&WorkflowLanes::refreshExpression);refreshExpression();
      connect(editor-> _scroll,&QScrollBar::valueChanged,this,QOverload<>::of(&WorkflowLanes::update));
      }
void WorkflowLanes::refreshExpression(){
      const auto expression=workflowContext()->expression();
      const QString version=expression.value("revision").toString()+":"+expression.value("resultVersion").toString();
      if(version!=_expressionVersion||!expression.contains("resultVersion")){
            _expressionVersion=version;const auto result=expression.value("result").toMap();
            _matches=result.value("displayMatches").toList();if(!result.contains("displayMatches"))_matches=result.value("matches").toList();
            _scoreNotes.clear();_scoreMatches.clear();_midiNotes.clear();
            for(auto value:_matches){auto m=value.toMap();_scoreMatches.insert(m.value("scoreId").toString(),m);_scoreNotes.insert(m.value("scoreId").toString(),m.value("original").toMap());_midiNotes.insert(m.value("midiId").toString(),m);}
            for(auto value:result.value("missing").toList()){auto n=value.toMap();_scoreNotes.insert(n.value("id").toString(),n);}
            for(auto value:result.value("extra").toList()){auto n=value.toMap();_midiNotes.insert(n.value("id").toString(),n);}
            _timing.clear();for(auto value:result.value("timing").toList()){auto p=value.toMap();_timing.append({p.value("seconds").toDouble(),p.value("tick").toDouble()});}
            // Older panels provide only the endpoints. New panels supply the local clock.
            if(_timing.isEmpty()&&!_matches.isEmpty()){auto a=_matches.front().toMap(),b=_matches.back().toMap();_timing.append({a.value("seconds").toDouble(),a.value("tick").toDouble()});if(b.value("seconds").toDouble()>_timing.front().first)_timing.append({b.value("seconds").toDouble(),b.value("tick").toDouble()});}
            }
      _scoreId=expression.value("selectedScoreId").toString();_midiId=expression.value("selectedMidiId").toString();
      emit selectionChanged();update();
      }
QObject* WorkflowLanes::wave(const QVariantMap& t){
      const QString key=t.value("path").toString();auto found=_waves.value(key);if(found)return found;
      auto source=createReferenceMedia(this);if(!source->property("available").toBool()){source->deleteLater();return nullptr;}
      _waves.insert(key,source);connect(source,SIGNAL(waveformChanged()),this,SLOT(waveChanged()));connect(source,SIGNAL(changed()),this,SLOT(waveChanged()));
      QMetaObject::invokeMethod(source,"open",Q_ARG(QString,key));return source;
      }
      bool WorkflowLanes::matchEnabled(const QVariantMap &value) const {
            auto note=displayedNote(_editor->score(),value);if(!note)return false;
            const int cached = _editor->_noteIndex.value(note, -1);
            return cached >= 0 && _editor->_notes[cached].enabled;
      }
void WorkflowLanes::showEvent(QShowEvent*){if(!_media){_media=referenceProject();connect(_media,SIGNAL(tracksChanged()),this,SLOT(waveChanged()));}update();}
void WorkflowLanes::hideEvent(QHideEvent*){for(auto source:_waves)if(source)source->deleteLater();_waves.clear();}
void WorkflowLanes::paintEvent(QPaintEvent*){
      QPalette theme=palette();
      const auto &colors=_editor->_appearance.colors;
      theme.setColor(QPalette::Base,colors[PerformanceAppearance::Background]);
      theme.setColor(QPalette::Text,colors[PerformanceAppearance::Text]);
      theme.setColor(QPalette::Highlight,colors[PerformanceAppearance::Selection]);
      theme.setColor(QPalette::Mid,colors[PerformanceAppearance::Grid]);
      QPainter p(this);p.fillRect(rect(),theme.base());_hits.clear();const int gutter=PerformanceViewport::gutter;
      auto expression=workflowContext()->expression();const auto result=expression.value("result").toMap(),candidate=expression.value("candidate").toMap();bool hasExpression=!result.value("matches").toList().isEmpty()||!result.value("missing").toList().isEmpty()||!result.value("extra").toList().isEmpty()||!candidate.value("velocity").toList().isEmpty()||!candidate.value("tempos").toList().isEmpty();const bool valid=workflowContext()->current(_editor->score());
      const int expressionHeight=qBound(100,height(),136);const double base=expressionHeight-6,scale=qMax(24.,base-64.);
      int y=-_vertical;auto x=[&](double tick){return _editor->xForTick(int(std::lround(tick)),false);};
      if(hasExpression){
            p.setPen(theme.text().color());p.drawText(QRect(4,y+3,70,20),Qt::AlignVCenter,tr("谱面"));p.drawText(QRect(4,y+30,70,20),Qt::AlignVCenter,tr("演奏"));p.drawText(QRect(4,y+64,70,qMax(20,expressionHeight-64)),Qt::AlignVCenter,_tempo?tr("速度"):tr("力度"));
            if(!valid){p.drawText(QRect(gutter,y,width()-gutter,40),Qt::AlignVCenter,tr("乐谱已经改变，请在演奏表达面板重新读取。"));y+=56;}
            else{
                  const auto& matches=_matches;const QString selected=expression.value("selectedScoreId").toString();
                  const auto targets=expression.value("candidate").toMap().value("velocityById").toMap();
                  auto performanceTick=[&](double seconds){
                        if(_timing.isEmpty())return expression.value("scope").toMap().value("start").toDouble();
                        if(_timing.size()<2)return _timing.front().second;
                        auto next=std::upper_bound(_timing.cbegin(),_timing.cend(),seconds,[](double s,const QPair<double,double>& p){return s<p.first;});if(next==_timing.cbegin())++next;if(next==_timing.cend())--next;auto before=next-1;return before->second+(seconds-before->first)*(next->second-before->second)/(next->first-before->first);
                        };
                  p.save();p.setClipRect(QRect(gutter,y,width()-gutter,expressionHeight));
                  auto from=std::lower_bound(matches.cbegin(),matches.cend(),_editor->tickForX(gutter-8,false),[](const QVariant& v,int tick){return v.toMap().value("tick").toInt()<tick;});
                  const int until=_editor->tickForX(width()+8,false);
                  QHash<int, int> onsetSlots;
                  for (auto it = from; it != matches.cend(); ++it) {
                        auto m = it->toMap();
                        if (m.value("tick").toInt() > until)
                              break;
                        const bool enabled = matchEnabled(m);
                        if (!enabled && !_editor->_ghostVoices)
                              continue;
                        p.setOpacity(enabled ? 1. : .25);
                        double px = x(m.value("tick").toDouble());
                        const int slot = onsetSlots[m.value("tick").toInt()]++;
                        const int offsets[8] = {0, -6, 6, -10, 10, -3, 3, 13};
                        const int offset = offsets[slot % 8];
                        const double sy = y + 13 + offset, my = y + 40 + offset;
                        double mx = x(performanceTick(m.value("seconds").toDouble()));
                        QColor c = theme.text().color();
                        if (m.value("scoreId").toString() == selected)
                              c = theme.highlight().color();
                        p.setPen(QPen(c, m.value("scoreId").toString() == selected ? 2 : 1));
                        p.drawLine(QPointF(px, sy), QPointF(mx, my));
                        p.setBrush(theme.base());
                        p.drawEllipse(QPointF(px, sy), 3, 3);
                        p.setBrush(c);
                        p.drawEllipse(QPointF(mx, my), 3, 3);
                        if(m.value("midiId").toString()==expression.value("selectedMidiId").toString()){
                              p.setPen(QPen(theme.highlight().color(),2));p.setBrush(Qt::NoBrush);p.drawEllipse(QPointF(mx,my),6,6);
                        }
                        const auto original = m.value("original").toMap();
                        auto target = targets.value(m.value("scoreId").toString()).toMap();
                        const auto tooltip=tr("谱面 %1 · 演奏 %2\n谱表%3／声部%4\n原力度 %5 → 新力度 %6\n演奏时间 %7 秒")
                            .arg(selectedPitch(original)).arg(selectedPitch(m,true)).arg(m.value("track").toInt()/4+1).arg(m.value("track").toInt()%4+1)
                            .arg(original.value("velocity",64).toInt()).arg(target.value("target",m.value("velocity",64)).toInt()).arg(m.value("seconds").toDouble(),0,'f',3);
                        if (enabled) {
                              _hits.append({QRectF(px - 6, sy - 6, 12, 12), m.value("scoreId").toString(),
                                            QString(), -1, tooltip});
                              _hits.append({QRectF(mx - 6, my - 6, 12, 12), m.value("scoreId").toString(),
                                            m.value("midiId").toString(), -1, tooltip});
                        }
                        if(!_tempo){const auto old=m.value("original").toMap();double oldValue=old.value("velocity",64).toDouble(),target=m.value("velocity",oldValue).toDouble();QString source=tr("演奏实测");
                        auto v=targets.value(m.value("scoreId").toString()).toMap();if(!v.isEmpty()){target=v.value("target").toDouble();source=v.value("source")=="inferred"?tr("伴奏推算"):tr("拟采用");}
                        const double vx = px + slot * 9;
                        p.setBrush(Qt::NoBrush);
                        p.drawRect(QRectF(vx - 4, y + base - oldValue / 127. * scale, 8, oldValue / 127. * scale));
                        p.setBrush(c);
                        p.drawRect(QRectF(vx - 2, y + base - target / 127. * scale, 4, target / 127. * scale));
                        if(m.value("scoreId").toString()==selected){p.drawText(QPointF(px+7,y+65),tr("原 %1 → %2 · %3").arg(int(oldValue)).arg(int(target)).arg(source));}}
                  }
                  p.setOpacity(1);
                  // Show missing and extra onsets without inventing a confirmed correspondence.
                  for (auto value : expression.value("result").toMap().value("missing").toList()) {
                        auto n = value.toMap();
                        const bool enabled = matchEnabled(n);
                        if (!enabled && !_editor->_ghostVoices)
                              continue;
                        p.setOpacity(enabled ? 1. : .25);
                        double px = x(n.value("tick").toDouble());
                        p.setPen(theme.text().color());
                        p.drawText(QPointF(px - 4, y + 18), "?");
                        if (enabled)
                              _hits.append(
                                  {QRectF(px - 7, y + 5, 14, 18), n.value("id").toString(), QString(), -1});
                  }
                  p.setOpacity(1);
                  for(auto value:result.value("extra").toList()){
                        auto n=value.toMap();if(_timing.isEmpty())continue;double px=x(performanceTick(n.value("seconds").toDouble()));
                        const bool selected=n.value("id").toString()==expression.value("selectedMidiId").toString();
                        p.setPen(QPen(selected?theme.highlight().color():theme.text().color(),selected?2:1));
                        if(selected){p.setBrush(Qt::NoBrush);p.drawRoundedRect(QRectF(px-8,y+26,16,21),3,3);}
                        p.drawText(QPointF(px-4,y+42),"+");_hits.append({QRectF(px-7,y+28,14,18),QString(),n.value("id").toString(),-1,tr("尚未对应 · MIDI %1 · %2 秒").arg(n.value("pitch").toInt()).arg(n.value("seconds").toDouble(),0,'f',3)});
                        }
                  if(_tempo){auto tempos=expression.value("candidate").toMap().value("tempos").toList();double low=999,high=5;for(auto value:tempos){auto t=value.toMap();low=qMin(low,qMin(t.value("oldBpm").toDouble(),t.value("bpm").toDouble()));high=qMax(high,qMax(t.value("oldBpm").toDouble(),t.value("bpm").toDouble()));}low=qMax(0.,low-5);high=qMax(low+10,high+5);QPolygonF original,candidate;
                        for(auto value:tempos){auto t=value.toMap();double px=x(t.value("tick").toDouble());original<<QPointF(px,y+base-(t.value("oldBpm").toDouble()-low)/(high-low)*scale);candidate<<QPointF(px,y+base-(t.value("bpm").toDouble()-low)/(high-low)*scale);}
                        p.setBrush(Qt::NoBrush);p.setPen(QPen(theme.text().color(),1,Qt::DashLine));p.drawPolyline(original);p.setPen(QPen(theme.highlight().color(),2));p.drawPolyline(candidate);p.drawText(QPointF(gutter+4,y+61),tempos.isEmpty()?tr("先在演奏表达生成 Rubato 结果。"):tr("速度：虚线是原值，实线是拟采用值。"));}
                  p.restore();p.setPen(theme.mid().color());p.drawLine(0,y+expressionHeight,width(),y+expressionHeight);y+=expressionHeight+4;
                  }
            }
      const auto tracks=_media?_media->property("tracks").toList():QVariantList();int index=0;
      QSet<QString> paths;for(auto value:tracks)paths.insert(value.toMap().value("path").toString());for(auto it=_waves.begin();it!=_waves.end();)if(!paths.contains(it.key())){if(it.value())it.value()->deleteLater();it=_waves.erase(it);}else ++it;
      if(!tracks.isEmpty()&&!workflowContext()->referenceCurrent(_editor->score())){p.setPen(theme.text().color());p.drawText(rect().adjusted(gutter, y+4,-8,-4),Qt::AlignTop|Qt::TextWordWrap,tr("这些音频来自另一份乐谱。请在原曲参考中打开对应项目。"));return;}
      for(auto value:tracks){auto t=value.toMap();int row=y; y+=60;if(row+60<0||row>height()){++index;continue;}p.setPen(theme.text().color());p.drawText(QRect(4,row,70,56),Qt::AlignVCenter|Qt::TextWordWrap,t.value("name").toString());_hits.append({QRectF(0,row,gutter,60),QString(),QString(),index++});
            auto audio=wave(t);auto peaks=audio?audio->property("waveform").toList():QVariantList();double duration=audio?audio->property("duration").toDouble():0;
            p.save();p.setClipRect(QRect(gutter,row,width()-gutter,60));
            for(auto av:t.value("associations").toList()){auto a=av.toMap();double first=x(a.value("start").toDouble()),last=x(a.value("end").toDouble());if(last<gutter||first>width())continue;
                  QColor color=theme.highlight().color();color.setAlpha(a.value("active",true).toBool()?35:12);p.fillRect(QRectF(first,row+4,last-first,48),color);p.setPen(a.value("stale").toBool()?theme.mid().color():theme.text().color());
                  if(duration>0&&!peaks.isEmpty())for(int pixel=qMax(gutter,int(first));pixel<qMin(width(),int(last));pixel+=2){int tick=_editor->tickForX(pixel,false);double ms=audioAt(a,tick);int pi=qBound(0,int(ms/duration*peaks.size()),peaks.size()-1);double peak=qMin(1.,peaks[pi].toDouble())*19;p.drawLine(QPointF(pixel,row+29-peak),QPointF(pixel,row+29+peak));}
                  p.drawText(QRectF(first+3,row+3,last-first-6,16),a.value("name").toString());
                  }
            p.restore();p.setPen(theme.mid().color());p.drawLine(0,row+59,width(),row+59);
            }
      if(_editor->_playTick>=0){const double px=x(_editor->_playTick);if(px>=gutter&&px<=width()){p.setPen(QPen(colors[PerformanceAppearance::Playhead],1.5));p.drawLine(QPointF(px,0),QPointF(px,height()));}}
      if(!hasExpression&&tracks.isEmpty()){p.setPen(theme.text().color());p.drawText(rect().adjusted(8,4,-8,-4),Qt::AlignCenter|Qt::TextWordWrap,tr("从“原曲参考”添加音轨，或从“演奏表达”读取对应。此区域与上方共用时间尺。"));}
      }
void WorkflowLanes::mousePressEvent(QMouseEvent* event){
      const Hit *nearest = nullptr;
      double distance = 1e12;
      for (const auto &candidate : _hits)
            if (candidate.bounds.contains(event->pos())) {
                  const auto delta = candidate.bounds.center() - event->pos();
                  const double cost = delta.x() * delta.x() + delta.y() * delta.y();
                  if (cost < distance) {
                        distance = cost;
                        nearest = &candidate;
                  }
            }
      if (nearest) {
            const auto hit = *nearest;
            if(hit.track>=0){QToolTip::showText(event->globalPos(),tr("参与播放、静音、独听和关联选择在原曲参考面板中设置。"),this);return;}
            if(!workflowContext()->current(_editor->score()))return;
            setFocus(Qt::MouseFocusReason);
            if(hit.midiId.isEmpty())_scoreId=hit.scoreId;
            else {_midiId=hit.midiId;if(_scoreId.isEmpty())_scoreId=hit.scoreId;}
            const QString scoreId=_scoreId,midiId=_midiId;
            if(auto note=displayedNote(_editor->score(),_scoreNotes.value(scoreId))){
                  const int index=_editor->_noteIndex.value(note,-1);if(index>=0)_editor->selectIndices({index},Qt::NoModifier);
            }
            workflowContext()->expressionAction("select",scoreId,midiId);emit selectionChanged();update();return;
      }
      }
      void WorkflowLanes::mouseMoveEvent(QMouseEvent *event) {
            const Hit *nearest = nullptr;
            double distance = 1e12;
            for (const auto &hit : _hits)
                  if (hit.bounds.contains(event->pos()) && !hit.tooltip.isEmpty()) {
                        const auto delta = hit.bounds.center() - event->pos();
                        const double cost = delta.x() * delta.x() + delta.y() * delta.y();
                        if (cost < distance) {
                              distance = cost;
                              nearest = &hit;
                        }
                  }
            if (nearest)
                  QToolTip::showText(event->globalPos(), nearest->tooltip, this);
            else
                  QToolTip::hideText();
      }
void WorkflowLanes::operation(const QString& action){if(!workflowContext()->current(_editor->score()))return;workflowContext()->expressionAction(action,_scoreId,_midiId);}
bool WorkflowLanes::selectedPairMatches() const {
      return !_scoreId.isEmpty()&&!_midiId.isEmpty()&&_scoreMatches.value(_scoreId).value("midiId").toString()==_midiId;
}
bool WorkflowLanes::canConnect() const {
      if(!workflowContext()->current(_editor->score())||workflowContext()->expression().value("busy").toBool()||
            !_scoreNotes.contains(_scoreId)||!_midiNotes.contains(_midiId)||!matchEnabled(_scoreNotes.value(_scoreId)))return false;
      const auto match=_scoreMatches.value(_scoreId);
      return !selectedPairMatches()||(!match.value("confirmed").toBool()&&match.value("confidence").toDouble()<.8);
}
QString WorkflowLanes::pairDescription() const {
      const auto score=_scoreNotes.value(_scoreId),midi=_midiNotes.value(_midiId);
      const auto first=score.isEmpty()?tr("点上排选择谱音"):tr("谱面 %1 · 第%2小节 第%3拍").arg(selectedPitch(score)).arg(score.value("measure").toInt()).arg(score.value("beat").toDouble(),0,'g',4);
      const auto second=midi.isEmpty()?tr("点下排选择演奏音"):tr("演奏 %1 · %2秒").arg(selectedPitch(midi,true)).arg(midi.value("seconds").toDouble(),0,'f',3);
      return first+QString::fromUtf8("  →  ")+second;
}
bool WorkflowLanes::event(QEvent* event){
      if((event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress)&&workflowContext()->current(_editor->score())){
            auto key=static_cast<QKeyEvent*>(event);const auto state=workflowContext()->expression();
            const bool undo=key->matches(QKeySequence::Undo)&&state.value("previewCanUndo").toBool();
            const bool redo=key->matches(QKeySequence::Redo)&&state.value("previewCanRedo").toBool();
            if(undo||redo){event->accept();if(event->type()==QEvent::KeyPress)operation(undo?"undoPreview":"redoPreview");return true;}
      }
      return QWidget::event(event);
}
void WorkflowLanes::wheelEvent(QWheelEvent* event){int rows=(_media?_media->property("tracks").toList().size():0)*60+(workflowContext()->expression().isEmpty()?0:qBound(100,height(),136)+4);_vertical=qBound(0,_vertical-event->angleDelta().y()/2,qMax(0,rows-height()));update();event->accept();}
void addWorkflowLanes(PerformanceEditor* editor,QWidget** result,QWidget* parent){
      auto panel=new QWidget(parent);panel->setObjectName("performanceWorkflowPanel");
      auto layout=new QVBoxLayout(panel);layout->setContentsMargins(0,0,0,0);
      auto tools=new QToolBar(panel);tools->setObjectName("workflowCorrespondenceTools");tools->setMovable(false);
      tools->setFloatable(false);tools->setIconSize(QSize(16,16));tools->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);layout->addWidget(tools);
      auto lane=new WorkflowLanes(editor);layout->addWidget(lane,1);
      auto mode=new QComboBox(panel);mode->setObjectName("workflowComparisonKind");mode->addItems({QObject::tr("力度对比"),QObject::tr("速度对比")});tools->addWidget(mode);
      QObject::connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),lane,[lane](int index){lane->setTempo(index==1);});
      auto add=[&](const QString& name,const QString& text,const QString& icon){
            auto action=tools->addAction(componentToolIcon(icon),text);action->setObjectName("workflow"+name);
            auto button=qobject_cast<QToolButton*>(tools->widgetForAction(action));if(button){button->setObjectName("workflow"+name);setComponentToolIcon(button,icon);}
            return action;
      };
      auto connect=add("connect",QObject::tr("连接这两音"),"link");
      if(auto button=tools->widgetForAction(connect))button->setObjectName("workflowConnectNotes");
      QObject::connect(connect,&QAction::triggered,lane,[lane]{lane->operation("connect");});
      auto original=add("auditionOriginal",QObject::tr("谱中效果"),"score");original->setCheckable(true);
      auto proposed=add("auditionCandidate",QObject::tr("拟采用效果"),"audition");proposed->setCheckable(true);
      auto stop=add("stopAudition",QObject::tr("停止试听"),"stop");
      for(auto action:{original,proposed,stop})QObject::connect(action,&QAction::triggered,lane,[lane,action]{lane->operation(action->objectName().mid(8));});
      auto syncAudition=[original,proposed,stop,editor]{
            const auto state=workflowContext()->expression(),audition=workflowContext()->audition(),candidate=state.value("candidate").toMap();
            const bool ready=workflowContext()->current(editor->score())&&!state.value("busy").toBool();
            const bool active=audition.value("playing").toBool()||audition.value("preparing").toBool();
            original->setEnabled(ready&&state.value("scope").toMap().value("end").toInt()>0);
            proposed->setEnabled(ready&&(!candidate.value("velocity").toList().isEmpty()||!candidate.value("tempos").toList().isEmpty()));
            stop->setEnabled(active);original->setChecked(active&&audition.value("mode").toString()=="score");proposed->setChecked(active&&audition.value("mode").toString()=="candidate");
      };
      QObject::connect(workflowContext(),&WorkflowContext::changed,panel,syncAudition);
      QObject::connect(workflowContext(),&WorkflowContext::auditionChanged,panel,syncAudition);syncAudition();
      auto apply=add("apply",QObject::tr("使用力度"),"apply");
      QObject::connect(apply,&QAction::triggered,lane,[lane,mode]{lane->operation(mode->currentIndex()==1?"applyTempo":"applyVelocity");});
      auto syncApply=[apply,mode,editor]{
            const auto state=workflowContext()->expression(),candidate=state.value("candidate").toMap();
            const auto kind=mode->currentIndex()?"tempos":"velocity";
            const int count=state.value("applyCounts").toMap().value(kind,candidate.value(kind).toList().size()).toInt();
            apply->setEnabled(count>0&&!state.value("busy").toBool()&&workflowContext()->current(editor->score()));
            apply->setText(mode->currentIndex()?QObject::tr("使用速度 · %1处").arg(count):QObject::tr("使用力度 · %1音").arg(count));
            apply->setToolTip(QObject::tr("采用演奏表达中选定范围的结果。Ctrl+Z撤销这次乐谱修改。"));
      };
      QObject::connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),panel,[syncApply](int){syncApply();});
      QObject::connect(workflowContext(),&WorkflowContext::changed,panel,syncApply);syncApply();
      auto more=add("more",QObject::tr("更多"),"settings");auto menu=new QMenu(panel);menu->setObjectName("workflowCorrespondenceMenu");
      QObject::connect(more,&QAction::triggered,menu,[tools,more,menu]{
            auto button=tools->widgetForAction(more);
            const auto point=button&&button->isVisible()?button->mapToGlobal(QPoint(0,button->height())):tools->mapToGlobal(QPoint(0,tools->height()));
            menu->popup(point);
      });
      for(const char* name:{"reference-audio-play","reference-joint-play"})menu->addAction(getAction(name));
      menu->addSeparator();menu->addAction(connect);
      auto confirm=menu->addAction(QObject::tr("确认对应"));confirm->setObjectName("workflowconfirm");
      auto anchor=menu->addAction(QObject::tr("固定这一处对应"));anchor->setObjectName("workflowanchor");
      auto unlink=menu->addAction(QObject::tr("解除对应"));unlink->setObjectName("workflowunlink");
      auto undo=menu->addAction(componentToolIcon("undo"),QObject::tr("撤销对应或预览调整"));undo->setObjectName("workflowundoPreview");
      auto redo=menu->addAction(componentToolIcon("redo"),QObject::tr("重做对应或预览调整"));redo->setObjectName("workflowredoPreview");
      for(auto action:{confirm,anchor,unlink,undo,redo})QObject::connect(action,&QAction::triggered,lane,[lane,action]{lane->operation(action->objectName().mid(8));});
      auto selection=new QLabel(panel);selection->setObjectName("workflowPairSelection");selection->setWordWrap(true);selection->setMinimumWidth(0);
      layout->insertWidget(1,selection);
      auto syncSelection=[lane,editor,selection,connect,confirm,anchor,unlink,undo,redo]{
            const auto state=workflowContext()->expression();const bool ready=workflowContext()->current(editor->score())&&!state.value("busy").toBool();
            const bool paired=lane->selectedPairMatches();selection->setText(lane->pairDescription());
            connect->setEnabled(lane->canConnect());connect->setText(paired?lane->canConnect()?QObject::tr("确认这两音"):QObject::tr("这两音已对应"):QObject::tr("连接这两音"));
            connect->setToolTip(QObject::tr("将上排选中的谱音与下排选中的演奏音对应。随后可以试听和使用力度。"));
            confirm->setEnabled(ready&&paired);anchor->setEnabled(ready&&paired);unlink->setEnabled(ready&&paired);
            undo->setEnabled(ready&&state.value("previewCanUndo").toBool());redo->setEnabled(ready&&state.value("previewCanRedo").toBool());
      };
      QObject::connect(lane,&WorkflowLanes::selectionChanged,panel,syncSelection);syncSelection();
      auto feedback=new QLabel(panel);feedback->setObjectName("workflowFeedback");feedback->setWordWrap(true);layout->addWidget(feedback);
      QObject::connect(workflowContext(),&WorkflowContext::feedbackChanged,panel,[feedback]{feedback->setText(workflowContext()->property("feedback").toString());});*result=panel;
}
}
