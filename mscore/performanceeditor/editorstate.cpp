// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "performanceeditor.h"
#include "mscore/workflow/documentsettings.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "libmscore/score.h"
#include "libmscore/measure.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QScrollBar>
#include <QToolButton>
#include <QSignalBlocker>
#include <cmath>

namespace Ms {
namespace {
QString editorStateKey(Score* score)
{
    return "performanceEditor/document/" + (score->isMaster() ? QString("master")
        : QString::fromLatin1(score->title().toUtf8().toBase64()));
}
QVariantList ranges(const PerformanceViewport& view)
{
    QVariantList result;
    for (const auto& range : view.ranges) result.append(QVariant::fromValue(QVariantList{range.minimum,range.maximum}));
    return result;
}
void restoreRanges(PerformanceViewport& view, const QVariantList& list)
{
    for (int i=0;i<qMin(4,list.size());++i) {
        const auto pair=list[i].toList();
        if(pair.size()==2 && std::isfinite(pair[0].toDouble()) && std::isfinite(pair[1].toDouble())
           && pair[0].toDouble()<pair[1].toDouble())
            view.ranges[i]={pair[0].toDouble(),pair[1].toDouble()};
    }
}
}
void PerformanceEditor::captureDocumentState()
{
    if(!_score) return;
    QVariantList excluded;
    for(int staff:_excludedStaves) excluded.append(staff);
    QVariantList collapsed;for(int staff:_collapsedVelocityStaves)collapsed.append(staff);
    QVariantMap controls;
    for(auto box:QList<QComboBox*>{_scope,_voice,_axis,_parameter,_tool})
        controls[box->objectName()]=box->currentIndex();
    QVariantMap value{{"controls",controls},{"handles",_handles->isChecked()},
        {"band",_band->isChecked()},{"voiceMask",_voiceMask},{"ghost",_ghostVoices},
        {"excludedStaves",excluded},{"collapsedStaves",collapsed},{"quarterPixels",_viewport.pixelsPerQuarter},
        {"rowHeight",_viewport.rowHeight},{"topPitch",_viewport.topPitch},
        {"scrollTick",_scroll->value()},{"following",_following},
        {"ranges",ranges(_viewport)},{"scoreRanges",ranges(_scoreViewport)},
        {"playTick",_playTick>=0&&seq&&seq->isPlaying()&&seq->score()==_score->masterScore()?_playTick:_score->playPos().ticks()}};
    DocumentSettings(_score).setValue(editorStateKey(_score),value);
}
void PerformanceEditor::restoreDocumentState()
{
    if(!_score) return;
    DocumentSettings settings(_score);
    const auto value=settings.value(editorStateKey(_score)).toMap();
    finishWheel();cancelGesture();cancelSurfaceGesture();
    _appearance=PerformanceAppearance();_appearance.load();
    _bandHeight=qBound(48,settings.value("performanceEditor/bandHeight",48).toInt(),240);
    _bandFollowsSystem=settings.value("performanceEditor/bandFollowsSystem",false).toBool();
    _dragSensitivity=settings.value("performanceEditor/dragSensitivity",1.).toDouble();
    if(!std::isfinite(_dragSensitivity)||_dragSensitivity<.25||_dragSensitivity>4) _dragSensitivity=1.;
    _peekKey=QKeySequence(settings.value("performanceEditor/peekKey","Ctrl+Alt+V").toString())[0];
    _peekReleaseMs=qBound(0,settings.value("performanceEditor/peekReleaseMs",0).toInt(),10000);
    for(const auto& item:QList<QPair<QString,QString>>{
        {"performanceNoteTooltips","noteTooltips"},{"performanceKeyboardNames","keyboardNames"},
        {"performanceScoreVoiceColors","scoreVoiceColors"},{"performanceShowValues","showValues"}})
        if(auto action=findChild<QAction*>(item.first))
            action->setChecked(settings.value("performanceEditor/"+item.second,item.second!="showValues").toBool());
    _wheelButton->setChecked(settings.value("performanceEditor/wheelVelocity",false).toBool());
    _auditionButton->setChecked(settings.value("performanceEditor/audition",true).toBool());
    const auto controls=value.value("controls").toMap();
    for(auto box:QList<QComboBox*>{_parameter,_axis,_scope,_voice,_tool})
        box->setCurrentIndex(qBound(0,controls.value(box->objectName(),0).toInt(),box->count()-1));
    for(const auto& item:QList<QPair<QString,QString>>{
        {"performanceKeyboardMode","keyboardMode"},{"performanceVelocityArrangement","velocityArrangement"},
        {"performanceComparisonView","comparisonView"}})
        if(auto box=findChild<QComboBox*>(item.first))
            box->setCurrentIndex(qBound(0,settings.value("performanceEditor/"+item.second,item.second=="keyboardMode"?1:0).toInt(),box->count()-1));
    if(auto box=findChild<QComboBox*>("performanceGroupVelocity"))
        box->setCurrentIndex(settings.value("performanceEditor/groupRelative",true).toBool()?0:1);
    if(auto box=findChild<QComboBox*>("performanceDragSensitivity"))
        box->setCurrentIndex(qMax(0,box->findData(_dragSensitivity)));
    if(auto check=findChild<QCheckBox*>("performanceActualVelocity"))
        check->setChecked(settings.value("performanceEditor/markVelocityLines",true).toBool());
    _voiceMask=value.value("voiceMask",15).toInt()&15;
    _ghostVoices=value.value("ghost",true).toBool();
    _excludedStaves.clear();
    _collapsedVelocityStaves.clear();for(const auto& staff:value.value("collapsedStaves").toList())
        if(staff.toInt()>=0&&staff.toInt()<_score->nstaves())_collapsedVelocityStaves.insert(staff.toInt());
    for(const auto& staff:value.value("excludedStaves").toList())
        if(staff.toInt()>=0&&staff.toInt()<_score->nstaves()) _excludedStaves.insert(staff.toInt());
    _handles->setChecked(value.value("handles",false).toBool());
    _band->setChecked(value.value("band",false).toBool());
    if(value.contains("quarterPixels")) {
        _viewport.pixelsPerQuarter=qBound(.01,value.value("quarterPixels").toDouble(),1000.);
        _viewport.rowHeight=qBound(3.,value.value("rowHeight",12.).toDouble(),80.);
        _viewport.topPitch=qBound(0.,value.value("topPitch",72.).toDouble(),127.);
        restoreRanges(_viewport,value.value("ranges").toList());
        restoreRanges(_scoreViewport,value.value("scoreRanges").toList());
        _fitOnRefresh=false;
        _scroll->setValue(qMax(0,value.value("scrollTick").toInt()));
        const int end=_score->lastMeasure()?_score->lastMeasure()->endTick().ticks():0;
        if(!seq||!seq->isPlaying()) _score->setPlayPos(Fraction::fromTicks(qBound(0,value.value("playTick").toInt(),end)));
    }
    _following=value.value("following",true).toBool();
    {QSignalBlocker block(_followButton);_followButton->setChecked(_following);}
    _followButton->setText(_following?tr("跟随播放"):tr("跟随：暂停"));
    rebuildStaffMenu();rebuildFilter();applyAppearance();invalidateOverlay();
}
}
