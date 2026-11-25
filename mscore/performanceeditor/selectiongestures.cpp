// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "performanceeditor.h"
#include "mscore/scoreview.h"
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#ifdef SCRIPT_INTERFACE
#include "miditrackpane.h"
#include "spectrumpane.h"
#include "timingeditor.h"
#endif

namespace Ms {
QVector<int> PerformanceEditor::boxSelectionNotes(const QRectF& box, bool parameter)
{
    QVector<int> result;
    if (!parameter) {
        buildGeometry();
#ifdef SCRIPT_INTERFACE
        if (_timingEditor && _timingEditor->active()) return _timingEditor->notesIn(box);
#endif
        for (const auto& item : _noteGeometry)
            if (_notes[item.index].enabled && item.rect.intersects(box)) result.append(item.index);
        return result;
    }
    const int from=tickForX(box.left()-8,false),until=tickForX(box.right()+8,false);
    for(int index:comparisonVisible(from,until)) {
        const auto& note=_notes[index];
        if (!noteEditable(note)) continue;
        if (_parameter->currentIndex()!=0) {
            if (note.tick>=from&&note.tick<=until) result.append(index);
            continue;
        }
        const auto lane=velocityLane(index,laneRect(),false);if(lane.isEmpty())continue;
        const double base=yForValue(rangeKind()==1?0:1,lane,false);
        const auto bar=[&](double x,double y){return QRectF(x-4,qMin(base,y)-3,8,qMax(6.,qAbs(base-y)+6));};
        bool inside=bar(xForNote(index,false),velocityY(index,lane,false)).intersects(box);
        if(comparisonActive()&&_comparisonValues.contains(index))
            inside=inside||bar(xForTick(comparisonDisplayTick(index),false)+4,yForValue(comparisonValue(index),lane)).intersects(box);
        if(inside)result.append(index);
    }
    return result;
}
bool PerformanceEditor::boxSelectionEvent(QObject* object,QEvent* event)
{
    const bool parameter=object==_canvas,notes=object==_noteCanvas;
    if(!parameter&&!notes)return false;
#ifdef SCRIPT_INTERFACE
    if(_midiTracks&&_midiTracks->active())return false;
#endif
    if(event->type()==QEvent::ContextMenu)return true; // Right-click menus are opened once, on release.
    if(event->type()!=QEvent::MouseButtonPress&&event->type()!=QEvent::MouseMove&&event->type()!=QEvent::MouseButtonRelease)return false;
    const auto mouse=static_cast<QMouseEvent*>(event);
    const auto boxModifiers=(_selectionModifiers&Qt::AltModifier)?Qt::AltModifier
        :(_selectionModifiers&(Qt::ControlModifier|Qt::ShiftModifier))?Qt::ShiftModifier:Qt::NoModifier;
    if(event->type()==QEvent::MouseButtonPress&&mouse->button()==Qt::RightButton) {
        if(!_score||_dirty||mouse->x()<PerformanceViewport::gutter)return false;
        if(parameter&&!laneRect().contains(mouse->pos()))return false;
        finishWheel();cancelGesture();pauseFollow();
        _marquee=true;_marqueeButton=Qt::RightButton;_marqueeSurface=static_cast<QWidget*>(object);
        _marqueeStart=_marqueeEnd=mouse->pos();_selectionModifiers=mouse->modifiers();
        _selectionBefore.clear();for(auto note:_score->selection().noteList())_selectionBefore.append(note);
        _marqueeSurface->grabMouse();syncInputTracking();return true;
    }
    if(!_marquee||_marqueeButton!=Qt::RightButton||object!=_marqueeSurface)return false;
    if(event->type()==QEvent::MouseMove) {
        _marqueeEnd=mouse->pos();
        if(QLineF(_marqueeStart,_marqueeEnd).length()>=QApplication::startDragDistance())
            selectIndices(boxSelectionNotes(QRectF(_marqueeStart,_marqueeEnd).normalized(),parameter),boxModifiers);
        updateSurfaces();return true;
    }
    if(event->type()!=QEvent::MouseButtonRelease||mouse->button()!=Qt::RightButton)return true;
    const bool click=QLineF(_marqueeStart,mouse->pos()).length()<QApplication::startDragDistance();
    const auto indices=boxSelectionNotes(QRectF(_marqueeStart,mouse->pos()).normalized(),parameter);
    if(!click)selectIndices(indices,boxModifiers);
    cancelSurfaceGesture();
    if(!click)return true;
#ifdef SCRIPT_INTERFACE
    // A short right click still auditions the selected spectral pitch.
    if(notes&&_spectrum) {
        QMouseEvent press(QEvent::MouseButtonPress,mouse->localPos(),mouse->screenPos(),Qt::RightButton,Qt::RightButton,mouse->modifiers());
        if(_spectrum->overlayEvent(&press))return true;
    }
#endif
    if(parameter&&_parameter->currentIndex()!=0&&tempoCurveMenu(mouse->pos(),false))return true;
    buildGeometry();const auto candidates=hits(mouse->pos(),parameter);
    if(candidates.isEmpty())return true;
    QMenu menu;
    for(int index:candidates) {
        auto action=menu.addAction(tr("%1 · 谱表 %2 · 声部 %3").arg(_notes[index].name).arg(_notes[index].track/VOICES+1).arg(_notes[index].track%VOICES+1));
        connect(action,&QAction::triggered,this,[this,index]{selectIndices({index},Qt::NoModifier);});
    }
    menu.exec(mouse->globalPos());return true;
}
void PerformanceEditor::paintBoxSelection(QPainter& painter,QWidget* widget) const
{
    if(!_marquee||widget!=(_marqueeSurface?_marqueeSurface.data():static_cast<QWidget*>(_noteCanvas)))return;
    auto color=_appearance.colors[PerformanceAppearance::Selection];auto fill=color;fill.setAlpha(35);
    painter.save();painter.setPen(QPen(color,1,Qt::DashLine));painter.setBrush(fill);
    painter.drawRect(QRectF(_marqueeStart,_marqueeEnd).normalized());painter.restore();
}
}
