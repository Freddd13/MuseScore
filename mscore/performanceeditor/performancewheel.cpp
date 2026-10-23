// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "mscore/scoreview.h"
#include "mscore/workflow/featuregate.h"
#include <QApplication>
#include <QPainter>
#include <QWheelEvent>
#include <QToolButton>
#include <QComboBox>
#include <QCheckBox>
#include <QToolTip>
#include <QScopedValueRollback>
#include <cmath>
#include <QSettings>
#include <QPainterPath>
namespace Ms {
bool PerformanceEditor::adjustNearestScoreVelocity(ScoreView* view,QWheelEvent* event)
      {
      if(!nearestWheelShortcut(event))return false;
      if(view!=_view)setView(view);
      QScopedValueRollback<bool> active(_closestWheelActive,true);
      if(!overlayAllowed())return false;
      if(_parameter->currentIndex()!=0)_parameter->setCurrentIndex(0);
      return wheelVelocity(event,false,true);
      }
void PerformanceEditor::finishWheel(bool commit)
      {
      if (_wheelIndex >= 0) { repaintWheelTarget(_wheelIndex); if (_wheelSurface) _wheelSurface->update(wheelBadge(_wheelSurface).adjusted(-2, -2, 2, 2).toAlignedRect()); }
      if(_wheelIndex>=0){rememberWheelFeedback();_wheelFeedbackTicks=0;_wheelFeedbackTimer.start(30);}
      _wheelTimer.stop(); _wheelSurface = nullptr; _wheelIndex = -1; _wheelRemainder = 0; _wheelBefore.clear();
      if (!_dragging && !_marquee && !_seeking && !_rangePanning) qApp->removeEventFilter(this);
      if (commit) applyPending();
      }
void PerformanceEditor::cancelWheel()
      {
      if (_wheelIndex < 0) return;
      _pending = _wheelBefore; finishWheel(false); QToolTip::hideText();
      status(tr("已取消本次滚轮调整。")); updateSurfaces(); invalidateOverlay();
      }
bool PerformanceEditor::wheelVelocity(QWheelEvent* wheel, bool notes, bool onScore)
      {
      const auto modifiers = wheel->modifiers();
      const bool closest=onScore&&nearestWheelShortcut(wheel);
      const bool temporary = closest||(!onScore && (modifiers & Qt::AltModifier));
      if (((modifiers & Qt::ControlModifier)&&!closest) || (onScore && (modifiers & Qt::AltModifier)&&!closest) || (!_wheelButton->isChecked() && !temporary)
            || !_score || _score->readOnly() || _dragging || _parameter->currentIndex() != 0) return false;
      // A new wheel event can arrive before the queued post-commit refresh.
      // Resolve that snapshot first instead of interpreting the event as scrolling.
      if (_dirty || _geometryDirty) refresh();
      if (_dirty) return false;
      const QPointF point = wheel->position();
      const bool overStrip = onScore && _band->isChecked() && _scoreFrame.contains(point);
      if (!closest && overStrip && !_scoreLane.contains(point)) return false;
      QWidget* surface = onScore ? static_cast<QWidget*>(_view) : (notes ? static_cast<QWidget*>(_noteCanvas) : _canvas);
      int target = -1;
      // Keep a stationary wheel burst attached to its first endpoint even when
      // the edited endpoint moves away. Moving to another note releases it.
      if (_wheelIndex >= 0 && _wheelSurface == surface && (closest||QLineF(point, _wheelPoint).length() <= 4)) target = _wheelIndex;
      else if(closest){
            rebuildVisibleScoreNotes();double best=1e20;for(auto item:_visibleScoreNotes){double d=QLineF(point,item.second.center()).length();if(d<best&&noteEditable(_notes[item.first])){best=d;target=item.first;}}
            }
      else if (onScore) {
            // The explicit wheel mode edits native note heads without prior selection.
            // Alt belongs to ScoreView navigation; score editing requires the mode.
            if (_band->isChecked() && _scoreLane.contains(point)) {
                  const auto candidates = hits(point, true, true); if (!candidates.isEmpty()) target = candidates.front();
                  }
            else if (_handles->isChecked()) {
                  auto candidates = _selectedIndices; if (_hover >= 0 && !candidates.contains(_hover)) candidates.append(_hover);
                  for (int i : candidates) if (QLineF(point, _view->matrix().mapRect(_notes[i].bounds).topRight() + QPointF(12, -12)).length() <= 9) { target = i; break; }
                  }
            if (target < 0 && !overStrip && (_wheelButton->isChecked() || temporary)) target = _noteIndex.value(_view->elementNear(_view->toLogical(point.toPoint())), -1);
            }
      else {
            buildGeometry(); const auto candidates = hits(point, !notes); if (!candidates.isEmpty()) target = candidates.front();
            }
      if(closest&&target<0){status(tr("当前视口没有可编辑的音，请显示目标谱行或调整筛选。"));wheel->accept();return true;}
      if (target < 0 || !noteEditable(_notes[target])) return false;
      const int delta = wheel->angleDelta().y() ? wheel->angleDelta().y() : (closest ? wheel->angleDelta().x() : 0);
      const int pixels = wheel->pixelDelta().y() ? wheel->pixelDelta().y() : (closest ? wheel->pixelDelta().x() : 0);
      if (!delta && !pixels) return false; // Do not swallow horizontal-only trackpads.
      if (_wheelIndex != target || _wheelSurface != surface) {
            // Different targets form separate undo bursts. A native commit refresh
            // preserves sorted note indices, but never carries a dying pointer.
            if (_wheelIndex >= 0) { finishWheel(); refresh(); }
            if (_dirty || target >= _notes.size() || !noteEditable(_notes[target])) return true;
            _wheelPoint = point; _wheelSurface = surface;
            _wheelIndex = target; _wheelValue = noteValue(target); _wheelBefore = _pending;
            pauseFollow(); qApp->installEventFilter(this); QToolTip::hideText();
            status(tr("滚轮仅调整指向音：细调 1／Shift 粗调 8 · Esc 取消 · 停转后提交。"));
            }
      if (_wheelPoint != point) { surface->update(wheelBadge(surface).adjusted(-2, -2, 2, 2).toAlignedRect()); _wheelPoint = point; }
      _wheelRemainder += delta ? delta / 120.0 : pixels / 15.0;
      const int steps = int(std::trunc(_wheelRemainder)); _wheelRemainder -= steps;
      if (steps) {
            const auto limit = PerformanceViewport::limits(rangeKind());
            _wheelValue = qBound(limit.minimum, _wheelValue + steps * (((modifiers & Qt::ShiftModifier)&&!closest) ? 8 : 1), limit.maximum);
            setNoteValue(target, _wheelValue, false); updateHover(target);
            repaintWheelTarget(target); surface->update(wheelBadge(surface).adjusted(-2, -2, 2, 2).toAlignedRect());
            }
      rememberWheelFeedback();_wheelFeedbackTimer.stop();_wheelTimer.start(); wheel->accept(); return true;
      }
QRectF PerformanceEditor::wheelBadge(QWidget* surface) const
      {
      const auto point=_wheelIndex>=0?_wheelPoint:_wheelFeedbackPoint;
      return QRectF(qBound(0.0, point.x()+16,double(qMax(0,surface->width()-276))),
            qBound(0.0,point.y()-63,double(qMax(0,surface->height()-55))),272,52);
      }
void PerformanceEditor::paintWheelBadge(QPainter& painter, QWidget* surface) const
      {
      if(_wheelFeedbackSurface!=surface || (_wheelIndex<0&&!_wheelFeedbackTimer.isActive()))return;
      painter.save();const double opacity=_wheelIndex>=0?1.:qBound(0.,(30.-_wheelFeedbackTicks)/8.,1.);painter.setOpacity(opacity);
      if(surface==_view && !_wheelFeedbackHead.isEmpty()){
            auto head=_view->matrix().mapRect(_wheelFeedbackHead).adjusted(-3,-3,3,3);painter.setBrush(Qt::NoBrush);painter.setPen(QPen(_wheelFeedbackColor,2.5));painter.drawRoundedRect(head,3,3);}
      const auto badge=wheelBadge(surface);painter.setBrush(_appearance.colors[PerformanceAppearance::Background]);painter.setPen(QPen(_wheelFeedbackColor,1.5));painter.drawRoundedRect(badge,4,4);
      painter.fillRect(QRectF(badge.left(),badge.top()+5,4,badge.height()-10),_wheelFeedbackColor);
      painter.setPen(_appearance.colors[PerformanceAppearance::Text]);QFont font=painter.font();font.setBold(false);painter.setFont(font);painter.drawText(badge.adjusted(12,3,-5,-28),Qt::AlignVCenter,_wheelFeedbackName);
      font.setBold(true);font.setPointSizeF(font.pointSizeF()>0?font.pointSizeF()+1:10);painter.setFont(font);painter.drawText(badge.adjusted(12,23,-5,-3),Qt::AlignVCenter,_wheelFeedbackValue);painter.restore();
      }
void PerformanceEditor::repaintWheelTarget(int index)
      {
      if (index < 0 || index >= _notes.size()) return;
      if(_view)_view->update(_view->matrix().mapRect(_notes[index].bounds).adjusted(-6,-6,6,6).toAlignedRect());
      if(_noteCanvas->isVisible())buildGeometry();
      if(_noteCanvas->isVisible())for (const auto& geometry : _noteGeometry) if (geometry.index == index) _noteCanvas->update(geometry.rect.adjusted(-3, -24, 3, 3).toAlignedRect());
      const int x = qRound(xForTick(_notes[index].tick, false)); _canvas->update(QRect(x - 9, 0, 19, _canvas->height()));
      if (_view && (_handles->isChecked() || _band->isChecked())) invalidateOverlay();
      }

bool PerformanceEditor::nearestWheelShortcut(const QWheelEvent* event)
      {
      if(!personalFeaturesEnabled())return false;
      const int required=QSettings().value("performanceEditor/nearestWheelModifiers",int(Qt::AltModifier|Qt::ShiftModifier)).toInt();
      return required && (int(event->modifiers())&~int(Qt::KeypadModifier))==required;
      }
void PerformanceEditor::rememberWheelFeedback()
      {
      if(_wheelIndex<0 || _wheelIndex>=_notes.size())return;
      const auto& n=_notes[_wheelIndex];const auto e=_pending.value(n.note,{n.type,n.raw});
      _wheelFeedbackSurface=_wheelSurface;_wheelFeedbackPoint=_wheelPoint;
      _wheelFeedbackHead=_wheelSurface==_view?n.bounds:QRectF();
      _wheelFeedbackColor=MScore::selectColor[n.track%VOICES];
      _wheelFeedbackName=tr("谱表%1 · 声部%2 · %3").arg(n.track/VOICES+1).arg(n.track%VOICES+1).arg(n.name);
      const QString value=n.base<0&&e.type==Note::ValueType::OFFSET_VAL ? tr("读取中")
            : QString::number(n.base<0?e.raw:NoteVelocity::effective(n.base,e.type,e.raw));
      const QString stored=e.type==Note::ValueType::USER_VAL ? tr("固定")
            : (e.raw>0?QString("+%1%").arg(e.raw):QString("%1%").arg(e.raw));
      _wheelFeedbackValue=tr("力度 %1    %2    参考 %3").arg(value,stored,n.base<0?tr("读取中"):QString::number(n.markBase>=0?n.markBase:n.base));
      }

}
