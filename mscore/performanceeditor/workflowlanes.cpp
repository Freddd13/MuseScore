// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "workflowlanes.h"
#include "performanceeditor.h"
#include "../workflow/workflowcontext.h"
#include "../workflow/referencefactory.h"
#include "../scoreview.h"
#include "../shortcut.h"
#include "../musescore.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QTimer>
#include <QVBoxLayout>
#include <QToolButton>
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
double audioAt(const QVariantMap& a,double tick){auto points=a.value("anchors").toList();points.prepend(QVariantMap{{"tick",a.value("start")},{"ms",a.value("from")}});points.append(QVariantMap{{"tick",a.value("end")},{"ms",a.value("until")}});for(int i=1;i<points.size();++i){auto p=points[i-1].toMap(),q=points[i].toMap();if(tick<=q.value("tick").toDouble())return p.value("ms").toDouble()+(tick-p.value("tick").toDouble())*(q.value("ms").toDouble()-p.value("ms").toDouble())/qMax(1.,q.value("tick").toDouble()-p.value("tick").toDouble());}return a.value("until").toDouble();}
}
WorkflowLanes::WorkflowLanes(PerformanceEditor* editor):QWidget(editor),_editor(editor){
      setObjectName("performanceWorkflowLanes");setMinimumHeight(100);setMouseTracking(true);setToolTip(tr("上排是谱面起音，下排是演奏起音；每一段使用附近的可靠对应来对齐时间。线条表示对应关系，? 是漏弹，+ 是多弹。力度细框是原值，实心是拟采用值。滚轮上下浏览音轨。"));connect(workflowContext(),&WorkflowContext::changed,this,&WorkflowLanes::refreshExpression);refreshExpression();
      connect(editor-> _scroll,&QScrollBar::valueChanged,this,QOverload<>::of(&WorkflowLanes::update));
      }
void WorkflowLanes::refreshExpression(){
      const auto expression=workflowContext()->expression();
      const QString version=expression.value("revision").toString()+":"+expression.value("resultVersion").toString();
      if(version!=_expressionVersion||!expression.contains("resultVersion")){
            _expressionVersion=version;const auto result=expression.value("result").toMap();
            _matches=result.value("displayMatches").toList();if(!result.contains("displayMatches"))_matches=result.value("matches").toList();
            _timing.clear();for(auto value:result.value("timing").toList()){auto p=value.toMap();_timing.append({p.value("seconds").toDouble(),p.value("tick").toDouble()});}
            // Older panels provide only the endpoints. New panels supply the local clock.
            if(_timing.isEmpty()&&!_matches.isEmpty()){auto a=_matches.front().toMap(),b=_matches.back().toMap();_timing.append({a.value("seconds").toDouble(),a.value("tick").toDouble()});if(b.value("seconds").toDouble()>_timing.front().first)_timing.append({b.value("seconds").toDouble(),b.value("tick").toDouble()});}
            }
      update();
      }
QObject* WorkflowLanes::wave(const QVariantMap& t){
      const QString key=t.value("path").toString();auto found=_waves.value(key);if(found)return found;
      auto source=createReferenceMedia(this);if(!source->property("available").toBool()){source->deleteLater();return nullptr;}
      _waves.insert(key,source);connect(source,SIGNAL(waveformChanged()),this,SLOT(waveChanged()));connect(source,SIGNAL(changed()),this,SLOT(waveChanged()));
      QMetaObject::invokeMethod(source,"open",Q_ARG(QString,key));return source;
      }
      bool WorkflowLanes::matchEnabled(const QVariantMap &value) const {
            auto score = _editor->score();
            if (!score)
                  return false;
            auto cr =
                score->findCR(Fraction::fromTicks(value.value("tick").toInt()), value.value("track").toInt());
            if (!cr || !cr->isChord())
                  return false;
            const auto &notes = toChord(cr)->notes();
            const int index = value.value("index", -1).toInt();
            if (index < 0 || index >= int(notes.size()))
                  return false;
            const int cached = _editor->_noteIndex.value(notes[size_t(index)], -1);
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
                        const auto original = m.value("original").toMap();
                        auto target = targets.value(m.value("scoreId").toString()).toMap();
                        const int pitch = m.value("pitch", original.value("pitch", 60)).toInt();
                        static const QStringList names = {"C",  "C♯", "D",  "D♯", "E",  "F",
                                                          "F♯", "G",  "G♯", "A",  "A♯", "B"};
                        const auto tooltip =
                            tr("%1%2 · 谱表%3／声部%4\n原力度 %5 → 新力度 %6\n演奏时间 %7 秒")
                                .arg(names[qBound(0, pitch, 127) % 12])
                                .arg(pitch / 12 - 1)
                                .arg(m.value("track").toInt() / 4 + 1)
                                .arg(m.value("track").toInt() % 4 + 1)
                                .arg(original.value("velocity", 64).toInt())
                                .arg(target.value("target", m.value("velocity", 64)).toInt())
                                .arg(m.value("seconds").toDouble(), 0, 'f', 3);
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
                        p.drawText(QPointF(px-4,y+42),"+");_hits.append({QRectF(px-7,y+28,14,18),QString(),n.value("id").toString(),-1,tr("多弹 · MIDI %1 · %2 秒").arg(n.value("pitch").toInt()).arg(n.value("seconds").toDouble(),0,'f',3)});
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
            if(!hit.scoreId.isEmpty())_scoreId=hit.scoreId;if(!hit.midiId.isEmpty())_midiId=hit.midiId;
            const auto matches=workflowContext()->expression().value("result").toMap().value("matches").toList();
            for(auto value:matches){auto m=value.toMap();if(m.value("scoreId").toString()!=hit.scoreId)continue;for(int i=0;i<_editor->_notes.size();++i){auto n=_editor->_notes[i];const auto& notes=n.note->chord()->notes();int ni=m.value("index").toInt();if(n.tick==m.value("tick").toInt()&&n.track==m.value("track").toInt()&&ni>=0&&ni<int(notes.size())&&notes[size_t(ni)]==n.note){_editor->selectIndices({i},Qt::NoModifier);break;}}break;}
            workflowContext()->expressionAction("select",_scoreId,hit.midiId);update();return;
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
void WorkflowLanes::wheelEvent(QWheelEvent* event){int rows=(_media?_media->property("tracks").toList().size():0)*60+(workflowContext()->expression().isEmpty()?0:qBound(100,height(),136)+4);_vertical=qBound(0,_vertical-event->angleDelta().y()/2,qMax(0,rows-height()));update();event->accept();}
void addWorkflowLanes(PerformanceEditor* editor,QWidget** result,QWidget* parent){
      auto panel=new QWidget(parent);panel->setObjectName("performanceWorkflowPanel");auto layout=new QVBoxLayout(panel);layout->setSizeConstraint(QLayout::SetMinimumSize);layout->setContentsMargins(0,0,0,0);auto row=new QHBoxLayout;layout->addLayout(row);auto lane=new WorkflowLanes(editor);layout->addWidget(lane,1);
      auto mode=new QComboBox(panel);mode->setObjectName("workflowComparisonKind");mode->addItems({QObject::tr("力度对比"),QObject::tr("速度对比")});row->addWidget(mode);QObject::connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),lane,[lane](int index){lane->setTempo(index==1);});
      for (auto item : QList<QPair<QString, QString>>{{QObject::tr("谱中效果"), "auditionOriginal"},
                                                      {QObject::tr("拟采用效果"), "auditionCandidate"},
                                                      {QObject::tr("使用力度"), "apply"}}) {
            auto b = new QToolButton(panel);
            b->setText(item.first);
            b->setObjectName("workflow" + item.second);
            QObject::connect(b, &QToolButton::clicked, lane, [lane, item, mode] {
                  lane->operation(item.second == "apply"
                                      ? (mode->currentIndex() == 1 ? "applyTempo" : "applyVelocity")
                                      : item.second);
            });
            if (item.second == "apply") {
                  auto sync = [b, mode, editor] {
                        const auto candidate = workflowContext()->expression().value("candidate").toMap();
                        const int count =
                            workflowContext()->expression().value("applyCounts").toMap().value(mode->currentIndex()?"tempos":"velocity",candidate.value(mode->currentIndex() ? "tempos" : "velocity").toList().size()).toInt();
                        b->setEnabled(count > 0 && !workflowContext()->expression().value("busy").toBool() && workflowContext()->current(editor->score()));
                        b->setText(mode->currentIndex() ? QObject::tr("使用速度 · %1处").arg(count)
                                                        : QObject::tr("使用力度 · %1音").arg(count));
                        b->setToolTip(
                            QObject::tr("按演奏表达中选定的对象应用这份预览。Ctrl+Z撤销整次修改。"));
                  };
                  QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), b,
                                   [sync](int) { sync(); });
                  QObject::connect(workflowContext(), &WorkflowContext::changed, b, sync);
                  sync();
            }
            row->addWidget(b);
      }
      auto more=new QToolButton(panel);more->setText(QObject::tr("更多"));more->setPopupMode(QToolButton::InstantPopup);auto menu=new QMenu(more);more->setMenu(menu);row->addWidget(more);
      for(const char* name:{"reference-audio-play","reference-joint-play"})menu->addAction(getAction(name));
      for (auto item : QList<QPair<QString, QString>>{{QObject::tr("确认对应"), "confirm"},
                                                      {QObject::tr("连接所选两音"), "connect"},
                                                      {QObject::tr("固定对齐点"), "anchor"},
                                                      {QObject::tr("解除对应"), "unlink"}}) {
            auto action = menu->addAction(item.first);
            action->setObjectName("workflow" + item.second);
            QObject::connect(action, &QAction::triggered, lane,
                             [lane, item] { lane->operation(item.second); });
      }
      auto legend = new QLabel(
          QObject::tr(
              "上排：谱面起音；下排：演奏起音。力度空心框是原值，实心条是新值。指向点可查看音名和数值。"),
          panel);
      legend->setWordWrap(true);
      layout->insertWidget(1, legend);
      row->addStretch();auto feedback=new QLabel(panel);feedback->setObjectName("workflowFeedback");feedback->setWordWrap(true);layout->addWidget(feedback);QObject::connect(workflowContext(),&WorkflowContext::feedbackChanged,panel,[feedback]{feedback->setText(workflowContext()->property("feedback").toString());});*result=panel;
      }
}
