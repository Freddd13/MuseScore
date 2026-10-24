// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceeditor.h"
#include "mscore/scoreview.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/pedal.h"
#include "libmscore/segment.h"
#include "libmscore/chordrest.h"
#include "libmscore/sig.h"
#include <QMenu>
#include <QToolButton>
#include <QKeyEvent>
#include <QMouseEvent>
#include "mscore/seq.h"
#include <QComboBox>
#include <algorithm>
#include "libmscore/pedalplayback.h"
#include "mscore/musescore.h"
#include <QSignalBlocker>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
namespace Ms {
bool PerformanceEditor::staffEnabled(int track) const
      { return !_excludedStaves.contains(track / VOICES); }
void PerformanceEditor::rebuildStaffMenu()
      {
      _staffMenu->clear();
      auto all = _staffMenu->addAction(tr("显示所有谱表"));
      connect(all, &QAction::triggered, this, [this] { cancelGesture(); _excludedStaves.clear(); rebuildFilter(); });
      if (!_score) return;
      for (int staff = 0; staff < _score->nstaves(); ++staff) {
            auto action = _staffMenu->addAction(tr("谱表 %1 · %2").arg(staff + 1).arg(_score->staff(staff)->part()->partName()));
            action->setCheckable(true); action->setChecked(!_excludedStaves.contains(staff)); action->setObjectName(QString("performanceStaff%1").arg(staff));
            connect(action, &QAction::toggled, this, [this, staff](bool on) { finishWheel(); cancelGesture(); if (on) _excludedStaves.remove(staff); else _excludedStaves.insert(staff); rebuildFilter(); });
            }
      }
QColor PerformanceEditor::scoreNoteColor(int index) const
      {
      const auto& note = _notes[index];
      if (_scoreVoiceColors) {
            return MScore::selectColor[note.track % VOICES];
            }
      const auto edit = _pending.value(note.note, {note.type, note.raw});
      QColor color = _appearance.noteColor(NoteVelocity::effective(note.base, edit.type, edit.raw), note.track);
      return QColor::fromHsvF(color.hsvHueF(), color.hsvSaturationF() * .55, color.valueF() * .65);
      }
void PerformanceEditor::resetScoreRange(bool dataRange)
      {
      auto range = PerformanceViewport::limits(rangeKind());
      if (dataRange && rangeKind() != 3) {
            double low = range.maximum, high = range.minimum;
            const bool selected = std::any_of(_systemIndices.cbegin(), _systemIndices.cend(), [this](int i) { return _notes[i].selected && noteEditable(_notes[i]); });
            if (rangeKind() == 2) for (const auto& s : _systemSegments) { low = qMin(low, s.bpm); high = qMax(high, s.bpm); }
            else for (int i : _systemIndices) if (noteEditable(_notes[i]) && (!selected || _notes[i].selected)) { low = qMin(low, noteValue(i)); high = qMax(high, noteValue(i)); }
            if (low <= high) {
                  const auto limit = range;
                  const double minimumSpan = rangeKind() == 2 ? 5 : 8, pad = qMax(minimumSpan / 2, (high - low) * .1);
                  range = {qMax(limit.minimum, low - pad), qMin(limit.maximum, high + pad)};
                  if (range.maximum - range.minimum < minimumSpan) { range.minimum = qBound(limit.minimum, (low + high - minimumSpan) / 2, limit.maximum - minimumSpan); range.maximum = range.minimum + minimumSpan; }
                  }
            }
      _scoreViewport.ranges[rangeKind()] = range; invalidateOverlay();
      }
void PerformanceEditor::scoreRangeControl(int control)
      {
      finishWheel(); cancelGesture();
      auto range = _scoreViewport.ranges[rangeKind()];
      if (control < 2) _scoreViewport.zoomRange(rangeKind(), control ? 1.5 : 1 / 1.5, (range.minimum + range.maximum) / 2, false);
      else if (control < 4) _scoreViewport.panRange(rangeKind(), (control == 2 ? 1 : -1) * (range.maximum - range.minimum) / 8);
      else resetScoreRange(control == 4);
      invalidateOverlay();
      }
int PerformanceEditor::nearestSegment(int tick) const
      {
      auto next = std::lower_bound(_segments.cbegin(), _segments.cend(), tick, [](const SegmentInfo& s, int t) { return s.tick < t; });
      if (next == _segments.cend()) return _segments.isEmpty() ? -1 : _segments.back().tick;
      if (next == _segments.cbegin()) return next->tick;
      return tick - (next - 1)->tick < next->tick - tick ? (next - 1)->tick : next->tick;
      }
bool PerformanceEditor::pedalVisible(const PedalInfo& pedal) const
      {
      return _score && _pedalVisibleParts.contains(pedal.pedal->part());
      }
bool PerformanceEditor::pedalBoundary(int tick, int track, bool end) const
      {
      // Score::tick2segment still scans measures in MS3. Cache boundaries at the
      // content snapshot, so hover, drawing and pedal drags never traverse a score.
      const auto& cache = _pedalBoundaries[end ? 1 : 0];
      auto staff = cache.constFind(_score->staff(track / VOICES)->part());
      return staff != cache.cend() && staff->contains(tick);
      }

QPointF PerformanceEditor::pedalHandle(int tick, bool end, const QRectF& rect, bool onScore) const
{
    return QPointF(xForTick(tick, onScore) + (end ? -6 : 6), yForValue(127, rect, onScore));
}
Pedal* PerformanceEditor::pedalHit(QPointF point, const QRectF& rect, bool onScore, int* grip) const
{
    Pedal* best = nullptr;
    double distance = 1e20;
    int chosen = -1;
    for (const auto& row : _pedals) {
        if (!pedalVisible(row)) continue;
        for (int side = 0; side < 2; ++side) {
            const QPointF handle = pedalHandle(side ? row.until : row.from, side, rect, onScore);
            const double d = QLineF(point, handle).length();
            const double rank = d + (row.pedal == _selectedPedal ? 0 : .1);
            if (d < 9 && rank < distance) { best = row.pedal; chosen = side; distance = rank; }
        }
    }
    if (!best && rect.contains(point)) {
        const int tick = tickForX(point.x(), onScore);
        for (const auto& row : _pedals) {
            if (!pedalVisible(row) || tick < row.from || tick > row.until) continue;
            const double rank = (row.track / VOICES == _contextTrack / VOICES ? 0 : 1)
                              + (row.pedal == _selectedPedal ? 0 : .1);
            if (rank < distance) { best = row.pedal; chosen = 2; distance = rank; }
        }
    }
    if (grip) *grip = chosen;
    return best;
}
void PerformanceEditor::syncPedalSelection()
{
    if (!_score) _selectedPedal = nullptr;
    else if (_selectedPedal) {
        const auto found = std::find_if(_pedals.cbegin(), _pedals.cend(), [this](const PedalInfo& row) { return row.pedal == _selectedPedal; });
        if (found == _pedals.cend() || !pedalVisible(*found)) _selectedPedal = nullptr;
    }
    if (_pedalDelete) _pedalDelete->setEnabled(_selectedPedal && _score && !_score->readOnly()
        && !_score->isPlaying() && (!seq || (!seq->isPlaying() && seq->backgroundRenderingIdle())));
}
bool PerformanceEditor::beginPedalGesture(QPointF point, const QRectF& rect, bool onScore)
{
    const int tool = _pedalTool->currentIndex();
    _pedalTarget = nullptr; _pedalGrip = -1;
    _pedalFrom = _pedalUntil = tickForX(point.x(), onScore);
    if (tool == 0 || tool == 3) _pedalTarget = pedalHit(point, rect, onScore, &_pedalGrip);
    _selectedPedal = _pedalTarget; _selectedPedalOnScore = onScore; syncPedalSelection();
    if (tool == 3 && !_pedalTarget) { status(tr("请在要分开的踏板段内部点一下。")); return false; }
    if (_pedalTarget) {
        _pedalOriginalFrom = _pedalTarget->tick().ticks();
        _pedalOriginalUntil = _pedalTarget->tick2().ticks();
        if (tool == 0) { _pedalFrom = _pedalOriginalFrom; _pedalUntil = _pedalOriginalUntil; }
    }
    return true;
}
void PerformanceEditor::movePedalGesture(QPointF point)
{
    const int tick = tickForX(point.x(), _scoreGesture);
    const int tool = _pedalTool->currentIndex();
    if (tool == 3) { _pedalFrom = _pedalUntil = snap(tick); return; }
    if (tool == 0 && _pedalTarget) {
        const int delta = tick - tickForX(_press.x(), _scoreGesture);
        if (_pedalGrip == 0) _pedalFrom = qBound(0, snap(_pedalOriginalFrom + delta), _pedalOriginalUntil - 1);
        else if (_pedalGrip == 1) _pedalUntil = qBound(_pedalOriginalFrom + 1, snap(_pedalOriginalUntil + delta, true), _endTick);
        else {
            const int move = qBound(-_pedalOriginalFrom, snap(_pedalOriginalFrom + delta) - _pedalOriginalFrom, _endTick - _pedalOriginalUntil);
            _pedalFrom = _pedalOriginalFrom + move; _pedalUntil = _pedalOriginalUntil + move;
        }
    }
    else {
        const int pressed = tickForX(_press.x(), _scoreGesture);
        _pedalFrom = snap(qMin(pressed, tick)); _pedalUntil = snap(qMax(pressed, tick), true);
    }
}
void PerformanceEditor::deleteSelectedPedal()
{
    if (_dirty || !_selectedPedal) return;
    cancelGesture(); QString error;
    _committing = true;
    const bool changed = ParameterEdit::removePedal(_score, _selectedPedal, &error);
    _committing = false;
    if (changed) { _selectedPedal = _pedalTarget = nullptr; _dirty = true; scheduleRefresh(); status(tr("已删除踏板段 · Ctrl+Z撤销")); }
    else status(error);
    syncPedalSelection(); updateSurfaces(); if (_view) _view->update();
}
bool PerformanceEditor::pedalKeyEvent(QObject* object, QEvent* event)
{
    if (!_parameter || _parameter->currentIndex() != 2) return false;
    const bool onScore = object == _view;
    if (onScore && event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && !_scoreLane.contains(mouse->pos())) {
            _selectedPedal = nullptr; syncPedalSelection();
        }
    }
    if (object != _canvas && !(onScore && _selectedPedalOnScore)) return false;
    if (!_selectedPedal || _dirty || _dragging) return false;
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) return false;
    auto key = static_cast<QKeyEvent*>(event);
    const bool remove = key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace;
    const bool move = (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)
                   && (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier);
    if (!remove && !move) return false;
    event->accept();
    if (event->type() == QEvent::ShortcutOverride) return true;
    if (remove) { deleteSelectedPedal(); return true; }
    const int mode = _pedalGrid->currentIndex();
    const int step = key->modifiers() == Qt::ShiftModifier || mode >= 4 ? 1 : DIVISION / (4 << mode);
    const int delta = step * (key->key() == Qt::Key_Right ? 1 : -1);
    int from = _selectedPedal->tick().ticks(), until = _selectedPedal->tick2().ticks();
    if (_pedalGrip == 0) from += delta;
    else if (_pedalGrip == 1) until += delta;
    else { from += delta; until += delta; }
    QString error; _committing = true;
    const bool changed = ParameterEdit::pedal(_score, _selectedPedal->track(), from, until, _selectedPedal, &error);
    _committing = false;
    if (changed) { _dirty = true; scheduleRefresh(); status(tr("已微调踏板 · Shift+方向键每次1 tick")); }
    else status(error);
    return true;
}

QString PerformanceEditor::parameterTooltip(QPointF point, bool onScore) const
      {
      const int tick = tickForX(point.x(), onScore);
      int bar, beat, sub; _score->sigmap()->tickValues(tick, &bar, &beat, &sub);
      const QString position = tr("%1.%2 +%3 ticks").arg(bar + 1).arg(beat + 1).arg(sub);
      if (_parameter->currentIndex() == 1)
            return tr("%1 · 四分音符 %2 BPM\n节点模式：空白点按添加、上下拖动改值。\n原速度文字受保护；请点中方形节点再修改。").arg(position).arg(_score->tempo(Fraction::fromTicks(tick)) * 60, 0, 'f', 2);
      int grip = -1;
      const auto target = pedalHit(point, onScore ? _scoreLane : laneRect(), onScore, &grip);
      if (target) return tr("%1 · 谱表%2\n%3 · 拖动调整；Delete删除\nShift+方向键每次微调1 tick")
          .arg(position).arg(target->staffIdx() + 1).arg(grip == 0 ? tr("踩下端") : grip == 1 ? tr("抬起端") : tr("踏板段，拖动可整体移动"));
      return tr("%1 · %2\n水平拖动可画踩下段或抬起间隙；分开工具在段内点一下。").arg(position, _pedalTool->currentText());
      }
void PerformanceEditor::showPedalPlayback()
{
    if (!_score || _score->nstaves() == 0) return;
    if (auto existing = findChild<QDialog*>("performancePedalPlaybackDialog")) { existing->show(); existing->raise(); return; }
    auto originalScore = _score;
    auto part = _selectedPedal ? _selectedPedal->part() : _score->staff(qBound(0, _contextTrack / VOICES, _score->nstaves()-1))->part();
    auto target = _selectedPedal;
    auto dialog = new QDialog(this, Qt::Window); dialog->setObjectName("performancePedalPlaybackDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(tr("踏板播放 · %1").arg(part->partName()));
    const auto theme = mscore ? mscore->palette() : QApplication::palette();
    dialog->setPalette(theme); dialog->setFont(QApplication::font());
    dialog->setStyleSheet(QString("QDialog {background:%1;color:%2;} QLabel,QCheckBox {color:%2;} QComboBox,QSpinBox,QPushButton {background:%3;color:%2;} QComboBox QAbstractItemView {background:%3;color:%2;selection-background-color:%4;selection-color:%5;}")
        .arg(theme.color(QPalette::Window).name(), theme.color(QPalette::WindowText).name(), theme.color(QPalette::Base).name(), theme.color(QPalette::Highlight).name(), theme.color(QPalette::HighlightedText).name()));
    auto form = new QFormLayout(dialog); form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto scope = new QComboBox(dialog); scope->setObjectName("pedalPlaybackScope");
    scope->addItem(tr("这架乐器的全部踏板")); if (target) scope->addItem(tr("选中的踏板段"));
    if (target) scope->setCurrentIndex(1);
    auto mode = new QComboBox(dialog); mode->setObjectName("pedalPlaybackMode");
    auto lift = new QSpinBox(dialog); lift->setObjectName("pedalPlaybackReleaseMs"); lift->setRange(1, 300); lift->setSuffix(tr(" ms"));
    auto down = new QSpinBox(dialog); down->setObjectName("pedalPlaybackDownMs"); down->setRange(0, 500); down->setSuffix(tr(" ms"));
    auto first = new QCheckBox(tr("开始处也在音后踩下"), dialog); first->setObjectName("pedalPlaybackDelayFirst");
    auto explanation = new QLabel(tr("钢琴方式在相接踏板之间安排抬起间隙，再于起音后踩下。谱上已画出的空隙继续保留。"), dialog);
    explanation->setWordWrap(true); explanation->setMaximumWidth(400);
    auto message = new QLabel(dialog); message->setWordWrap(true); message->setMaximumWidth(400); message->setObjectName("pedalPlaybackResult");
    form->addRow(tr("应用于"), scope); form->addRow(tr("方式"), mode); form->addRow(tr("抬起间隔"), lift);
    form->addRow(tr("音后踩下"), down); form->addRow(first); form->addRow(explanation); form->addRow(message);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, dialog);
    form->addRow(buttons); buttons->button(QDialogButtonBox::Apply)->setText(tr("保存设置")); buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    auto updateEnabled = [=] { const bool active = mode->currentData().toInt() == 1; lift->setEnabled(active); down->setEnabled(active); first->setEnabled(active); };
    auto available = [=] {
        if (_score != originalScore || !_score->parts().contains(part)) return false;
        if (scope->currentIndex() == 1) {
            const auto& map = _score->spannerMap().map();
            return std::any_of(map.cbegin(), map.cend(), [target](const auto& row) { return row.second == target; });
        }
        return true;
    };
    auto load = [=] {
        if (!available()) { message->setText(tr("乐谱或所选踏板已改变，请重新打开当前对象的设置。")); return; }
        const bool own = scope->currentIndex() == 1;
        const auto data = own ? target->getProperty(Pid::PEDAL_PLAYBACK).toString() : PedalPlayback::partData(part);
        const auto settings = own ? PedalPlayback::effective(target) : PedalPlayback::decode(data);
        QSignalBlocker blocked(mode); mode->clear();
        if (own) mode->addItem(tr("跟随这架乐器"), -1);
        mode->addItem(tr("SF2原有方式"), 0); mode->addItem(tr("钢琴换踏板"), 1);
        mode->setCurrentIndex(mode->findData(own && data.isEmpty() ? -1 : settings.piano ? 1 : 0));
        lift->setValue(settings.releaseMs); down->setValue(settings.downMs); first->setChecked(settings.delayFirst); updateEnabled();
    };
    connect(scope, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, load);
    connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, updateEnabled);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, dialog, &QDialog::close);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, dialog, [=] {
        if (!available()) { message->setText(tr("乐谱或所选踏板已改变，请重新打开当前对象的设置。")); return; }
        auto selected = scope->currentIndex() == 1 ? target : nullptr;
        PedalPlayback::Settings settings; settings.piano = mode->currentData().toInt() == 1;
        settings.releaseMs = lift->value(); settings.downMs = down->value(); settings.delayFirst = first->isChecked();
        QString error; _committing = true;
        const bool changed = ParameterEdit::pedalPlayback(_score, part, selected, settings, mode->currentData().toInt() == -1, &error);
        _committing = false;
        if (changed) { _dirty = true; scheduleRefresh(); if (mscore) mscore->endCmd(); }
        message->setText(!error.isEmpty() ? error : tr("设置已保存 · Ctrl+Z撤销"));
    });
    load(); dialog->show();
}

}
