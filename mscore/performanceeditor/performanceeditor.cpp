#include "performanceeditor.h"
#include "performanceselection.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/sig.h"
#include "libmscore/undo.h"
#include <QSplitter>
#include <QToolButton>
#include <QSettings>
#include <QElapsedTimer>
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/system.h"
#include "libmscore/pedal.h"
#include "libmscore/spanner.h"
#include "libmscore/tempotext.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/select.h"
#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QResizeEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QDoubleSpinBox>
#include <QMenu>
#include <algorithm>

namespace Ms {
extern bool graceNotesMerged(Chord*);
namespace {
QVector<PerformanceEditor*> editors;
const QColor accent("#3199ce"), pendingColor("#e99d32");
int selectionTrack(Score* score, int fallback)
      {
      const auto notes = score->selection().noteList();
      if (!notes.empty()) return notes.front()->track();
      for (auto element : score->selection().elements()) if (element->isChordRest()) return element->track();
      return qBound(0, fallback, qMax(0, score->nstaves() * VOICES - 1));
      }
}
PerformanceEditor::PerformanceEditor(QWidget* parent) : QWidget(parent)
      {
      _score = nullptr;
      _appearance.load();
      editors.append(this);
      setObjectName("performanceEditor");
      auto layout = new QVBoxLayout(this);
      layout->setContentsMargins(6, 4, 6, 4);
      auto row = new QGridLayout; _topRow = row;
      auto combo = [this, row](const QStringList& labels, const char* name) {
            auto box = new QComboBox(this); box->addItems(labels); box->setObjectName(name); row->addWidget(box, row->count() / 5, row->count() % 5); return box;
            };
      _parameter = combo({tr("力度"), tr("速度 · 阶梯"), tr("延音踏板")}, "performanceParameter");
      _axis = combo({tr("MIDI 力度"), tr("相对 %")}, "performanceAxis");
      _scope = combo({tr("当前乐器"), tr("当前谱表"), tr("全谱")}, "performanceScope");
      _voice = combo({tr("声部 1–4"), "1", "2", "3", "4"}, "performanceVoice"); row->removeWidget(_voice); _voice->hide();
      auto voiceButton = new QToolButton(this); voiceButton->setText(tr("声部…")); voiceButton->setPopupMode(QToolButton::InstantPopup);
      auto voiceMenu = new QMenu(voiceButton); voiceButton->setMenu(voiceMenu);
      for (int v = 0; v < 4; ++v) {
            auto action = voiceMenu->addAction(tr("声部 %1").arg(v + 1)); action->setCheckable(true); action->setChecked(true);
            connect(action, &QAction::toggled, this, [this, v](bool on) { cancelGesture(); _voiceMask = on ? _voiceMask | (1 << v) : _voiceMask & ~(1 << v); rebuildFilter(); });
            }
      auto ghost = voiceMenu->addAction(tr("显示其他声部为淡色参考")); ghost->setCheckable(true); ghost->setChecked(true);
      connect(ghost, &QAction::toggled, this, [this](bool on) { _ghostVoices = on; invalidateVisual(); });
      voiceMenu->addSeparator();
      for (int scope = 0; scope < 3; ++scope) {
            auto action = voiceMenu->addAction(scope == 0 ? tr("选择这些声部：时间选区／当前视窗") : (scope == 1 ? tr("选择这些声部：当前视窗") : tr("选择这些声部：全曲")));
            connect(action, &QAction::triggered, this, [this, scope] { selectVoices(scope); });
            }
      row->addWidget(voiceButton, row->count() / 5, row->count() % 5);
      _tool = combo({tr("拖动"), tr("铅笔"), tr("直线")}, "performanceTool");
      row->setAlignment(Qt::AlignLeft);
      layout->addLayout(row);
      _playButton = new QToolButton(this); _playButton->setObjectName("performancePlay"); _playButton->setText(tr("▶ 播放"));
      _playButton->setToolTip(tr("从当前播放光标开始／停止（空格）")); connect(_playButton, &QToolButton::clicked, this, [this] { togglePlayback(); }); row->addWidget(_playButton, 0, row->count());
      auto locate = new QToolButton(this); locate->setObjectName("performanceLocateSelection"); locate->setText(tr("移至选音"));
      locate->setToolTip(tr("把播放光标移到最近选中的音；双击音符可从该处播放。")); connect(locate, &QToolButton::clicked, this, &PerformanceEditor::locateSelection); row->addWidget(locate, 0, row->count());
      _auditionButton = new QToolButton(this); _auditionButton->setObjectName("performanceAudition"); _auditionButton->setText(tr("试听")); _auditionButton->setCheckable(true);
      _auditionButton->setChecked(QSettings().value("performanceEditor/audition", true).toBool()); _auditionButton->setToolTip(tr("停播时点选音符／力度端点发声，不移动播放位置。"));
      connect(_auditionButton, &QToolButton::toggled, this, [](bool on) { QSettings().setValue("performanceEditor/audition", on); }); row->addWidget(_auditionButton, 0, row->count());
      _wheelButton = new QToolButton(this); _wheelButton->setObjectName("performanceWheelVelocity");
      _wheelButton->setText(tr("滚轮调力度")); _wheelButton->setCheckable(true);
      _wheelButton->setChecked(QSettings().value("performanceEditor/wheelVelocity", false).toBool());
      _wheelButton->setToolTip(tr("指向音符／力度柱／音旁手柄滚轮细调 1，Shift 粗调 8。编辑器 Alt+滚轮临时调节；谱面开启此模式后直接指向未选音头。Alt 保留谱面移动，Ctrl 仍缩放。只改指向的一个音，停转 300 ms 合并一次撤销，Esc 取消。"));
      connect(_wheelButton, &QToolButton::toggled, this, [this](bool on) { finishWheel(); QSettings().setValue("performanceEditor/wheelVelocity", on); syncOverlayTracking(); invalidateOverlay(); });
      row->addWidget(_wheelButton, 0, row->count());
      _wheelTimer.setParent(this); _wheelTimer.setSingleShot(true); _wheelTimer.setInterval(300); _wheelTimer.setObjectName("performanceWheelTimer");
      connect(&_wheelTimer, &QTimer::timeout, this, [this] { finishWheel(); });
      auto second = new QGridLayout; _actionsRow = second;
      auto button = [this, second](const QString& label, auto callback) {
            auto result = new QPushButton(label, this); second->addWidget(result, second->count() / 6, second->count() % 6); connect(result, &QPushButton::clicked, this, callback); return result;
            };
      _handles = new QCheckBox(tr("音旁手柄"), this); second->addWidget(_handles, second->count() / 6, second->count() % 6);
      _band = new QCheckBox(tr("谱行参数带"), this); second->addWidget(_band, second->count() / 6, second->count() % 6);
      button(tr("转换为相对"), [this] { convertSelected(Note::ValueType::OFFSET_VAL); })->setObjectName("performanceConvertRelative");
      button(tr("转换为绝对"), [this] { convertSelected(Note::ValueType::USER_VAL); })->setObjectName("performanceConvertAbsolute");
      _number = new QDoubleSpinBox(this); _number->setObjectName("performanceValue"); _number->setRange(1, 127); _number->setDecimals(0); second->addWidget(_number, second->count() / 6, second->count() % 6);
      button(tr("写入选中"), [this] { setSelectedValue(_number->value()); })->setObjectName("performanceWriteValue");
      button(tr("全曲"), [this] { fit(); });
      button(tr("选区"), [this] { fit(true); });
      button(tr("取消预览"), [this] { cancelWheel(); cancelGesture(); _pending.clear(); status(); updateSurfaces(); if (_view) _view->update(); })->setObjectName("performanceCancelPreview");
      auto viewControls = new QToolButton(this); viewControls->setText(tr("视窗…")); viewControls->setPopupMode(QToolButton::InstantPopup);
      auto viewMenu = new QMenu(viewControls); viewControls->setMenu(viewMenu); second->addWidget(viewControls, second->count() / 6, second->count() % 6);
      auto viewAction = [this, viewMenu](const QString& label, auto callback) { auto action = viewMenu->addAction(label); connect(action, &QAction::triggered, this, callback); };
      viewAction(tr("音域"), [this] { centerPitch(); });
      viewAction(tr("定位选音"), [this] { centerPitch(true); });
      viewAction(tr("音域概览"), [this] { centerPitch(false, true); });
      viewAction(tr("纵向＋"), [this] { pauseFollow(); _viewport.rowHeight = qMin(36.0, _viewport.rowHeight * 1.2); surfaceResized(PerformanceSurface::Notes); });
      viewAction(tr("纵向－"), [this] { pauseFollow(); _viewport.rowHeight = qMax(2.0, _viewport.rowHeight / 1.2); surfaceResized(PerformanceSurface::Notes); });
      viewAction(tr("参数＋"), [this] { pauseFollow(); auto r = _viewport.ranges[rangeKind()]; _viewport.zoomRange(rangeKind(), 1.2, (r.minimum + r.maximum) / 2); invalidateVisual(); });
      viewAction(tr("参数－"), [this] { pauseFollow(); auto r = _viewport.ranges[rangeKind()]; _viewport.zoomRange(rangeKind(), 1 / 1.2, (r.minimum + r.maximum) / 2); invalidateVisual(); });
      viewAction(tr("完整范围"), [this] { resetRange(false); });
      viewAction(tr("数据范围"), [this] { resetRange(true); });
      viewAction(tr("参数↑"), [this] { auto r = _viewport.ranges[rangeKind()]; _viewport.panRange(rangeKind(), (r.maximum - r.minimum) / 8); invalidateVisual(); });
      viewAction(tr("参数↓"), [this] { auto r = _viewport.ranges[rangeKind()]; _viewport.panRange(rangeKind(), -(r.maximum - r.minimum) / 8); invalidateVisual(); });
      auto appearance = new QToolButton(this); appearance->setText(tr("显示…")); appearance->setPopupMode(QToolButton::InstantPopup);
      auto displayMenu = new QMenu(appearance); appearance->setMenu(displayMenu);
      for (int mode = 0; mode < 3; ++mode) {
            auto action = displayMenu->addAction(mode == 0 ? tr("按力度着色") : (mode == 1 ? tr("按声部着色") : tr("按谱表着色")));
            connect(action, &QAction::triggered, this, [this, mode] { _appearance.mode = mode; _appearance.save(); invalidateVisual(); });
            }
      auto values = displayMenu->addAction(tr("音符内显示力度")); values->setCheckable(true); values->setChecked(QSettings().value("performanceEditor/showValues", false).toBool()); _showValues = values->isChecked();
      connect(values, &QAction::toggled, this, [this](bool on) { _showValues = on; QSettings().setValue("performanceEditor/showValues", on); updateSurfaces(); });
      connect(displayMenu->addAction(tr("编辑配色…")), &QAction::triggered, this, [this] { if (_appearance.edit(this)) { applyAppearance(); invalidateVisual(); if (_view) _view->update(); } });
      connect(displayMenu->addAction(tr("REAPER 默认力度色")), &QAction::triggered, this, [this] { _appearance = PerformanceAppearance(); _appearance.save(); applyAppearance(); invalidateVisual(); if (_view) _view->update(); });
      second->addWidget(appearance, second->count() / 6, second->count() % 6);
      _followButton = new QToolButton(this); _followButton->setText(tr("跟随播放")); _followButton->setToolTip(tr("手动浏览后暂停跟随；单击恢复，下一次开始播放也会恢复。")); _followButton->setCheckable(true); _followButton->setChecked(true);
      connect(_followButton, &QToolButton::clicked, this, [this] { _following = true; _followButton->setChecked(true); updatePlayhead(); });
      second->addWidget(_followButton, second->count() / 6, second->count() % 6);
      second->setAlignment(Qt::AlignLeft);
      layout->addLayout(second);
      _splitter = new QSplitter(Qt::Vertical, this); _splitter->setChildrenCollapsible(false);
      auto noteArea = new QWidget(_splitter); auto noteLayout = new QVBoxLayout(noteArea); noteLayout->setContentsMargins(0, 0, 0, 0); noteLayout->setSpacing(0);
      auto paneControls = [this](QVBoxLayout* parent, bool notes) {
            auto row = new QHBoxLayout; row->setContentsMargins(3, 0, 3, 0); row->setSpacing(3); row->addWidget(new QLabel(notes ? tr("音高") : tr("数值范围")));
            auto add = [this, row](const QString& text, const QString& tip, const QString& name, auto callback) { auto button = new QToolButton; button->setText(text); button->setToolTip(tip); button->setObjectName(name); connect(button, &QToolButton::clicked, this, callback); row->addWidget(button); };
            const QString prefix = notes ? "performancePitch" : "performanceRange";
            add("−", tr("只缩小本区纵向视窗，不改变音符属性"), prefix + "Out", [this, notes] { zoomVertical(notes, 1 / 1.4); });
            add("+", tr("只放大本区纵向视窗，围绕当前选音／数值"), prefix + "In", [this, notes] { zoomVertical(notes, 1.4); });
            add(notes ? tr("音域") : tr("匹配值"), notes ? tr("显示乐器音域") : tr("放大到选中音的数值范围；没有选音时匹配当前视窗"), prefix + "Fit", [this, notes] { if (notes) centerPitch(false, true); else resetRange(true); });
            add(notes ? tr("选音") : tr("全范围"), notes ? tr("保持行高，音高区居中选音") : tr("恢复完整数值范围"), prefix + "Reset", [this, notes] { if (notes) centerPitch(true); else resetRange(false); });
            row->addStretch(); parent->addLayout(row);
            };
      paneControls(noteLayout, true);
      _noteCanvas = new PerformanceCanvas(this, PerformanceSurface::Notes); _noteCanvas->installEventFilter(this); noteLayout->addWidget(_noteCanvas, 1);
      _pitchScroll = new QScrollBar(Qt::Vertical, _noteCanvas); _pitchScroll->setRange(0, 12700);
      connect(_pitchScroll, &QScrollBar::valueChanged, this, [this](int value) { pauseFollow(); _viewport.topPitch = 127 - value / 100.0; invalidateVisual(); });
      _splitter->addWidget(noteArea);
      auto parameterArea = new QWidget(_splitter); auto areaLayout = new QVBoxLayout(parameterArea); areaLayout->setContentsMargins(0, 0, 0, 0); areaLayout->setSpacing(0);
      _ruler = new PerformanceCanvas(this, PerformanceSurface::Ruler); _ruler->installEventFilter(this); areaLayout->addWidget(_ruler);
      paneControls(areaLayout, false);
      _canvas = new PerformanceCanvas(this, PerformanceSurface::Parameter); _canvas->installEventFilter(this); areaLayout->addWidget(_canvas, 1);
      _valueScroll = new QScrollBar(Qt::Vertical, _canvas); _valueScroll->setObjectName("performanceValueScroll");
      connect(_valueScroll, &QScrollBar::valueChanged, this, [this](int position) {
            if (rangeKind() == 3) return; pauseFollow(); const auto limit = PerformanceViewport::limits(rangeKind()); auto& range = _viewport.ranges[rangeKind()]; const double span = range.maximum - range.minimum;
            range.maximum = limit.maximum - (limit.maximum - limit.minimum - span) * position / 10000.0; range.minimum = range.maximum - span; invalidateVisual();
            });
      _splitter->addWidget(parameterArea); _splitter->setStretchFactor(0, 1); _splitter->setStretchFactor(1, 1); layout->addWidget(_splitter, 1);
      _splitter->restoreState(QSettings().value("performanceEditor/splitter").toByteArray());
      connect(_splitter, &QSplitter::splitterMoved, this, [this] { QSettings().setValue("performanceEditor/splitter", _splitter->saveState()); });
      _scroll = new QScrollBar(Qt::Horizontal, this); layout->addWidget(_scroll);
      _status = new QLabel(this); _status->setWordWrap(true); layout->addWidget(_status);
      _refreshTimer.setSingleShot(true);
      connect(&_refreshTimer, &QTimer::timeout, this, &PerformanceEditor::refresh);
      _commitTimer.setSingleShot(true);
      connect(&_commitTimer, &QTimer::timeout, this, &PerformanceEditor::applyPending);
      connect(_scroll, &QScrollBar::valueChanged, this, [this] { if (!_automaticScroll) pauseFollow(); invalidateVisual(); });
      for (auto box : {_scope, _voice}) connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { cancelGesture(); rebuildFilter(); });
      for (auto box : {_axis, _parameter, _tool}) connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            finishWheel(); cancelGesture();
            const bool velocity = _parameter->currentIndex() == 0;
            _axis->setEnabled(velocity);
            findChild<QPushButton*>("performanceConvertRelative")->setEnabled(velocity);
            findChild<QPushButton*>("performanceConvertAbsolute")->setEnabled(velocity);
            findChild<QPushButton*>("performanceWriteValue")->setEnabled(_parameter->currentIndex() != 2);
            _number->setEnabled(_parameter->currentIndex() != 2);
            _number->setSuffix(velocity && _axis->currentIndex() ? " %" : QString());
            _number->setPrefix(_parameter->currentIndex() == 1 ? "BPM " : QString());
            _number->setRange(velocity ? (_axis->currentIndex() ? NoteVelocity::minOffset : 1) : 5,
                  velocity ? (_axis->currentIndex() ? NoteVelocity::maxOffset : 127) : 999);
            _number->setDecimals(velocity ? 0 : 2);
            updateSelection(); status(); invalidateVisual(); if (_view) _view->update();
            });
      for (auto box : {_handles, _band}) connect(box, &QCheckBox::toggled, this, [this] { cancelGesture(); syncOverlayTracking(); if (_view) _view->update(); });
      if (seq) {
            connect(seq, &Seq::stopped, this, [this] { if (_snapshotDegraded) { _dirty = true; scheduleRefresh(); } _commitTimer.start(0); syncTransport(); });
            connect(seq, &Seq::started, this, [this] { cancelGesture(); _following = true; _followButton->setChecked(true); syncTransport(); status(); });
            connect(seq, &Seq::heartBeat, this, [this](int, int, int) { syncTransport(); playbackNotes(); });
            }
      _playTimer.setParent(this); _playTimer.setObjectName("performancePlaybackTimer");
      _playTimer.setInterval(16); _playTimer.setTimerType(Qt::PreciseTimer);
      connect(&_playTimer, &QTimer::timeout, this, &PerformanceEditor::updatePlayhead);
      _moveTimer.setSingleShot(true); _moveTimer.setInterval(16);
      connect(&_moveTimer, &QTimer::timeout, this, &PerformanceEditor::drainGesture);
      applyAppearance(); status();
      }

void PerformanceEditor::resizeEvent(QResizeEvent*)
      {
      for (auto grid : {_topRow, _actionsRow}) {
            if (!grid) continue;
            QVector<QWidget*> widgets;
            while (auto item = grid->takeAt(0)) { if (item->widget()) widgets.append(item->widget()); delete item; }
            int cellWidth = 1;
            for (auto widget : widgets) cellWidth = qMax(cellWidth, widget->minimumSizeHint().width());
            const int spacing = qMax(6, grid->horizontalSpacing());
            const int columns = qBound(1, (width() - 12 + spacing) / (cellWidth + spacing), widgets.size());
            for (int i = 0; i < widgets.size(); ++i) grid->addWidget(widgets[i], i / columns, i % columns);
            }
      }

PerformanceEditor::~PerformanceEditor()
      {
      invalidateOverlay();
      if (_view && _trackingOwned) _view->setMouseTracking(_trackingBefore || _view->noteEntryMode() || _view->fotoMode());
      finishWheel(false); editors.removeAll(this);
      if (_score) _score->removeViewer(this);
      }

void PerformanceEditor::setView(ScoreView* view)
      {
      if (_view == view && _score == (view ? view->score() : nullptr)) return;
      Score* next = view ? view->score() : nullptr;
      if (_score && _score != next && hasPending()) flushPending();
      finishWheel(false); cancelGesture();
      if (_score) _viewMemory.insert(_score, {_viewport, _scroll->value()});
      _playTimer.stop(); _playingNotes.clear(); _linkedIndex.clear(); cancelSurfaceGesture();
      for (auto connection : _transportConnections) disconnect(connection); _transportConnections.clear();
      if (_view) { invalidateOverlay(); if (_trackingOwned) _view->setMouseTracking(_trackingBefore || _view->noteEntryMode() || _view->fotoMode()); _trackingOwned = false; _view->removeEventFilter(this); }
      if (_score) _score->removeViewer(this);
      disconnect(_scoreDestroyed);
      _view = view; _score = next;
      _notes.clear(); _segments.clear(); _tempos.clear(); _pedals.clear(); _noteIndex.clear(); _layoutIndex.clear(); _selectedIndices.clear(); _systemIndices.clear();
      _system = nullptr; _hover = -1; _snapshotDegraded = false; _intervals.clear(); _measures.clear(); invalidateVisual();
      if (_score) {
            _score->addViewer(this);
            _scoreDestroyed = connect(_score, &QObject::destroyed, this, [this] {
                  _playTimer.stop(); _intervals.clear(); _playingNotes.clear(); _linkedIndex.clear(); cancelSurfaceGesture();
                  finishWheel(false);
                  _score = nullptr; _notes.clear(); _segments.clear(); _tempos.clear(); _pedals.clear();
                  _noteIndex.clear(); _layoutIndex.clear(); _pending.clear(); _selectedIndices.clear(); _systemIndices.clear(); _system = nullptr; status(); updateSurfaces();
                  });
            }
      _overlayDamage = QRegion(); _overBand = false;
      if (_view) {
            _view->installEventFilter(this); _overlayMatrix = _view->matrix();
            _transportConnections.append(connect(_view, &ScoreView::viewRectChanged, this, &PerformanceEditor::overlayViewChanged));
            _transportConnections.append(connect(_view, &ScoreView::scaleChanged, this, [this](double) { overlayViewChanged(); }));
            }
      _fitOnRefresh = !_viewMemory.contains(_score);
      _scroll->setRange(0, _score && _score->lastMeasure() ? _score->lastMeasure()->endTick().ticks() : 0);
      _playTick = -1; _following = true; _followButton->setChecked(true); _followButton->setText(tr("跟随播放"));
      if (_fitOnRefresh) _viewport = PerformanceViewport();
      if (_viewMemory.contains(_score)) { _viewport = _viewMemory.value(_score).viewport; _scroll->setValue(_viewMemory.value(_score).tick); }
      if (_score) {
            _transportConnections.append(connect(_score, &Score::posChanged, this, [this](POS pos, unsigned) { if (pos == POS::CURRENT) updatePlayhead(); }));
            if (!_rememberedScores.contains(_score)) {
                  _rememberedScores.insert(_score);
                  connect(_score, &QObject::destroyed, this, [this, next] { _viewMemory.remove(next); _rememberedScores.remove(next); });
                  }
            }
      _dirty = true; scheduleRefresh(); syncTransport(); syncOverlayTracking();
      }

void PerformanceEditor::scheduleRefresh()
      {
      if (_committing) return;
      if (!_refreshTimer.isActive()) _refreshTimer.start(0);
      }

void PerformanceEditor::refresh()
      {
      if (!_score) { status(); updateSurfaces(); return; }
      if (!isVisible()) return;
      // A late-opened panel may copy stable notation while playing, but must not
      // regenerate/read mutable playback events or velocity maps until stopped.
      const bool live = _score->isPlaying() || (seq && seq->isPlaying());
      if (live && !_notes.isEmpty()) {
            // Layout/selection are GUI data; never regenerate playback events here.
            if (_score->masterScore()->state() == _state) {
                  if (_geometryDirty) refreshLayout();
                  _dirty = false; updateSelection();
                  }
            status(); return;
            }
      if (!live && seq && !seq->backgroundRenderingIdle()) { _refreshTimer.start(20); return; }
      const auto state = _score->masterScore()->state();
      if (hasPending() && _state != state) { finishWheel(false); _pending.clear(); status(tr("乐谱已被其他操作修改，待提交预览已取消。")); }
      if (_score->nstaves() == 0) return;
      if (!_dirty && _state == state) {
            if (_geometryDirty) {
                  refreshLayout();
                  }
            updateSelection(); return;
            }
      if (_dragging) cancelGesture();
      cancelSurfaceGesture();
      _notes.clear(); _segments.clear(); _tempos.clear(); _pedals.clear(); _noteIndex.clear(); _layoutIndex.clear(); _intervals.clear(); _linkedIndex.clear();
      QElapsedTimer snapshotTimer; snapshotTimer.start();
      if (!live) { _score->createPlayEvents(); _score->updateVelo(); }
      _snapshotDegraded = live;
      const int track = selectionTrack(_score, _contextTrack);
      _contextTrack = track;
      const int globalMethod = mscore ? mscore->synthesizerState().method() : 1;
      _relativeMax = 100; _tempoMax = 240; _measures.clear();
      for (auto measure = _score->firstMeasure(); measure; measure = measure->nextMeasure()) {
            int bar, beat, subtick; _score->sigmap()->tickValues(measure->tick().ticks(), &bar, &beat, &subtick);
            _measures.append({measure->tick().ticks(), measure->endTick().ticks(), bar, ticks_beat(measure->timesig().denominator())});
            }
      for (auto segment = _score->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest)) {
            const int tick = segment->tick().ticks();
            auto system = segment->measure()->system();
            if (system) _layoutIndex.insert(system);
            _segments.append({tick, segment->canvasPos(), system, _score->tempo(segment->tick()) * 60.0, segment});
            for (Element* annotation : segment->annotations())
                  if (annotation->isTempoText()) {
                        auto text = toTempoText(annotation);
                        _tempos.append({text, tick, text->tempo() * 60.0, text->visible()});
                        _tempoMax = qMax(_tempoMax, text->tempo() * 60.0);
                        }
            for (int t = 0; t < _score->nstaves() * VOICES; ++t) {
                  Element* element = segment->element(t);
                  if (!element || !element->isChord()) continue;
                  auto addChord = [this, tick, globalMethod, live](Chord* chord) {
                        for (Note* note : chord->notes()) {
                              const int index = _notes.size();
                              _noteIndex.insert(note, index);
                              const bool audible = note->play() && !note->hidden() && (live ? !note->tieBack() : (!note->tieBack() || note->playEvents().size() != 1))
                                    && (live ? !chord->isGrace() : (!chord->isGrace() || !graceNotesMerged(toChord(chord->parent()))));
                              _notes.append({note, tick, chord->endTick().ticks(), note->track(), note->ppitch(),
                                    live ? -1 : NoteVelocity::referenceBase(note, globalMethod), note->veloOffset(), 0, 0, note->veloType(),
                                    note->selected(), audible, note->canvasBoundingRect(), chord->measure()->system()});
                              auto& cached = _notes.back(); cached.baseMin = cached.baseMax = cached.base;
                              if (!note->tieBack() && note->tieFor()) cached.end = note->lastTiedNote()->chord()->endTick().ticks();
                              cached.name = tpc2name(note->tpc1(), NoteSpellingType::STANDARD, NoteCaseType::AUTO, false) + QString::number((note->ppitch() - static_cast<int>(tpc2alter(note->tpc1()))) / 12 - 1);
                              cached.written = tpc2name(note->tpc(), NoteSpellingType::STANDARD, NoteCaseType::AUTO, false) + QString::number((note->epitch() - static_cast<int>(tpc2alter(note->tpc()))) / 12 - 1); cached.part = note->part(); cached.instrument = cached.part->partName();
                              int bar, beat, subtick; _score->sigmap()->tickValues(tick, &bar, &beat, &subtick); cached.position = QString("%1.%2 +%3 ticks").arg(bar + 1).arg(beat + 1).arg(subtick); cached.duration = chord->actualTicks().toString();
                              _intervals.add(index, tick, qMax(tick + 1, cached.end), cached.pitch);
                              _linkedIndex.insert(note, index);
                              if (note->links()) for (auto linked : *note->links()) if (linked != note && linked->type() == ElementType::NOTE) _linkedIndex.insert(static_cast<Element*>(linked), index);
                              for (int event = 1; !live && event < note->playEvents().size(); ++event) {
                                    const int base = NoteVelocity::referenceBase(note, globalMethod, event);
                                    cached.baseMin = qMin(cached.baseMin, base); cached.baseMax = qMax(cached.baseMax, base);
                                    }
                              if (note->veloType() == Note::ValueType::OFFSET_VAL) _relativeMax = qMax(_relativeMax, note->veloOffset());
                              }
                        };
                  Chord* chord = toChord(element);
                  for (Chord* grace : chord->graceNotesBefore()) addChord(grace);
                  addChord(chord);
                  for (Chord* grace : chord->graceNotesAfter()) addChord(grace);
                  }
            }
      for (const auto& pair : _score->spannerMap().map()) {
            auto span = pair.second;
            if (span->isPedal())
                  _pedals.append({toPedal(span), span->tick().ticks(), span->tick2().ticks(), span->track()});
            }
      _endTick = _score->lastMeasure() ? _score->lastMeasure()->endTick().ticks() : 1;
      if (!_segments.isEmpty()) {
            auto measure = _score->lastMeasure();
            _segments.append({_endTick, measure->canvasPos() + QPointF(measure->width(), 0), measure->system(), _segments.back().bpm});
            }
      _hover = -1;
      _state = state; _dirty = false; _geometryDirty = false;
      rebuildFilter(); invalidateVisual();
      if (_fitOnRefresh) { fit(); centerPitch(!_selectedIndices.isEmpty()); _fitOnRefresh = false; _following = true; _followButton->setChecked(true); _followButton->setText(tr("跟随播放")); }
      else surfaceResized(PerformanceSurface::Notes);
      if (qEnvironmentVariableIsSet("PERFORMANCE_BENCHMARK")) qInfo("Performance snapshot %d notes: %.2f ms", _notes.size(), snapshotTimer.nsecsElapsed() / 1e6);
      }

void PerformanceEditor::updateSelection()
      {
      // Layout callbacks can precede selection notifications: never inspect a stale System.
      if (_dirty || (_score && _score->masterScore()->state() != _state)) {
            _dirty = true; scheduleRefresh(); return;
            }
      _selectedIndices.clear(); _systemIndices.clear();
      _system = _hover >= 0 && _hover < _notes.size() ? _notes[_hover].system : nullptr;
      for (auto& note : _notes) {
            note.selected = note.note->selected();
            if (note.selected && noteEditable(note)) _selectedIndices.append(int(&note - _notes.data()));
            if (note.selected && !_system) _system = note.system;
            }
      if (!_system && _score) for (auto element : _score->selection().elements())
            if (element->isChordRest()) { _system = toChordRest(element)->measure()->system(); break; }
      if (!_system && _hover >= 0 && _hover < _notes.size()) _system = _notes[_hover].system;
      if (!_system && !_notes.isEmpty()) _system = _notes.front().system;
      if (seq && seq->isPlaying() && seq->score() == _score->masterScore()) _system = systemAtTick(_playTick);
      setOverlaySystem(_system);
      if (_parameter->currentIndex() == 0 && !_selectedIndices.isEmpty()) {
            QSignalBlocker blocker(_number); _number->setValue(noteValue(_selectedIndices.front()));
            }
      findChild<QPushButton*>("performanceConvertRelative")->setEnabled(_parameter->currentIndex() == 0 && !_snapshotDegraded);
      findChild<QPushButton*>("performanceConvertAbsolute")->setEnabled(_parameter->currentIndex() == 0 && !_snapshotDegraded);
      updateSurfaces(); status(); if (_view) _view->update();
      }

void PerformanceEditor::refreshLayout()
      {
      _layoutIndex.clear();
      for (auto& note : _notes) { note.bounds = note.note->canvasBoundingRect(); note.system = note.note->chord()->measure()->system(); if (note.system) _layoutIndex.insert(note.system); }
      for (auto& segment : _segments) if (segment.segment) { segment.position = segment.segment->canvasPos(); segment.system = segment.segment->measure()->system(); if (segment.system) _layoutIndex.insert(segment.system); }
      if (!_segments.isEmpty() && !_segments.back().segment && _score->lastMeasure()) { auto measure = _score->lastMeasure(); _segments.back().system = measure->system(); _segments.back().position = measure->canvasPos() + QPointF(measure->width(), 0); }
      _geometryDirty = false; invalidateVisual();
      setOverlaySystem(systemAtTick(_playTick));
      }
System* PerformanceEditor::systemAtTick(int tick) const
      {
      auto next = std::upper_bound(_segments.cbegin(), _segments.cend(), tick, [](int t, const SegmentInfo& s) { return t < s.tick; });
      return next == _segments.cbegin() ? (_segments.isEmpty() ? nullptr : _segments.front().system) : (next - 1)->system;
      }
void PerformanceEditor::setOverlaySystem(System* system)
      {
      _system = system; _systemIndices.clear();
      for (int i = 0; i < _notes.size(); ++i) if (_notes[i].system == _system) _systemIndices.append(i);
      _systemSegments.clear();
      for (const auto& segment : _segments) if (segment.system == _system) _systemSegments.append(segment);
      if (_system && !_systemSegments.isEmpty()) {
            auto last = _system->lastMeasure();
            if (last && _systemSegments.back().tick < last->endTick().ticks())
                  _systemSegments.append({last->endTick().ticks(), last->canvasPos() + QPointF(last->width(), 0), _system, _systemSegments.back().bpm});
            }
      }

void PerformanceEditor::selectionChanged()
      {
      if (_dragging || _selecting) return;
      if (_score) _contextTrack = selectionTrack(_score, _contextTrack);
      rebuildFilter();
      }

void PerformanceEditor::layoutChanged()
      {
      if (!_committing) cancelGesture();
      _geometryDirty = true; scheduleRefresh();
      }

void PerformanceEditor::onElementDestruction(Element* element)
      {
      // Destruction callbacks must never call virtual methods on the dying Element.
      if (_layoutIndex.remove(element)) {
            cancelGesture(); _system = nullptr; _systemSegments.clear(); _geometryDirty = true;
            for (auto& note : _notes) if (note.system == element) note.system = nullptr;
            for (auto& segment : _segments) if (segment.system == element) segment.system = nullptr;
            }
      if (_noteIndex.contains(element)) {
            finishWheel(false); cancelGesture(); cancelSurfaceGesture(); _pending.clear(); _playingNotes.clear(); _notes.clear(); _intervals.clear(); _linkedIndex.clear(); invalidateVisual(); _noteIndex.clear(); _layoutIndex.clear(); _selectedIndices.clear(); _systemIndices.clear(); _system = nullptr;
            _segments.clear(); _systemSegments.clear();
            }
      bool matched = false;
      for (const auto& tempo : _tempos) if (tempo.text == element) matched = true;
      for (const auto& pedal : _pedals) if (pedal.pedal == element) matched = true;
      if (!matched && !_notes.isEmpty()) return;
      _tempos.erase(std::remove_if(_tempos.begin(), _tempos.end(), [element](const TempoInfo& t) { return t.text == element; }), _tempos.end());
      _pedals.erase(std::remove_if(_pedals.begin(), _pedals.end(), [element](const PedalInfo& p) { return p.pedal == element; }), _pedals.end());
      if (_pedalTarget == element) { cancelGesture(); _pedalTarget = nullptr; }
      _dirty = true; invalidateVisual();
      }

void PerformanceEditor::status(const QString& message)
      {
      const QString mode = _parameter->currentIndex() == 1 ? tr("四分音符 BPM · 实际速度阶梯")
            : (_parameter->currentIndex() == 2 ? tr("原生延音踏板 · CC64 开/关")
                  : (_axis->currentIndex() ? tr("相对整数百分比") : tr("参考 Note-on 力度 1–127")));
      const QString baseline = _snapshotDegraded ? tr(" · 播放中首次打开：基准待停播；仅编辑原相对%／绝对MIDI模式") : QString();
      const QString pending = hasPending() ? tr(" · %1 音预览中；检视器仍为已保存值").arg(_pending.size()) : QString();
      _status->setText(message.isEmpty() ? (_score ? mode + baseline + pending + tr(" · 可编辑选音 %1 · Ctrl 选音／缩放时间 · Ctrl+Shift 缩放纵向").arg(_selectedIndices.size()) : tr("打开乐谱后使用演奏编辑器。")) : message);
      }

QVector<int> PerformanceEditor::selectedNotes() const
      {
      return _selectedIndices;
      }

double PerformanceEditor::noteValue(int index) const
      {
      const auto& note = _notes[index];
      const auto edit = _pending.value(note.note, {note.type, note.raw});
      if (note.base < 0) return _axis->currentIndex() ? edit.raw : qBound(1, edit.raw, 127);
      if (!_axis->currentIndex()) return NoteVelocity::effective(note.base, edit.type, edit.raw);
      return edit.type == Note::ValueType::OFFSET_VAL ? edit.raw : NoteVelocity::offsetFor(note.base, edit.raw);
      }

void PerformanceEditor::setNoteValue(int index, double value, bool report)
      {
      const auto& note = _notes[index];
      if (!noteEditable(note)) return;
      const auto type = _pending.value(note.note, {note.type, note.raw}).type;
      const int raw = _axis->currentIndex() ? qBound(NoteVelocity::minOffset, qRound(value), NoteVelocity::maxOffset) : qBound(1, qRound(value), 127);
      if (note.base < 0 && (_axis->currentIndex() ? type != Note::ValueType::OFFSET_VAL : type != Note::ValueType::USER_VAL)) return;
      const int stored = note.base < 0 ? raw : (type == Note::ValueType::OFFSET_VAL
            ? (_axis->currentIndex() ? raw : NoteVelocity::offsetFor(note.base, raw))
            : (_axis->currentIndex() ? NoteVelocity::effective(note.base, Note::ValueType::OFFSET_VAL, raw) : raw));
      if (type == note.type && stored == note.raw) _pending.remove(note.note);
      else _pending.insert(note.note, {type, stored});
      if (!report) return;
      if (note.base < 0) { status(tr("预览 %1%2 · 实际 MIDI 基准待停播；原生属性尚未提交。 ").arg(stored).arg(type == Note::ValueType::OFFSET_VAL ? " %" : " MIDI")); return; }
      const int actual = NoteVelocity::effective(note.base, type, stored);
      const QString range = note.baseMin != note.baseMax ? tr(" · 多次发声范围 %1–%2").arg(NoteVelocity::effective(note.baseMin, type, stored)).arg(NoteVelocity::effective(note.baseMax, type, stored)) : QString();
      status(tr("音高 %1 · 基准 %2 · 保存 %3%4 · MIDI %5%6")
            .arg(note.pitch).arg(note.base).arg(stored).arg(type == Note::ValueType::OFFSET_VAL ? "%" : "")
            .arg(actual).arg((!_axis->currentIndex() && actual != raw ? tr("（整数百分比舍入：目标 %1）").arg(raw) : QString()) + range));
      }

void PerformanceEditor::convertSelected(Note::ValueType type)
      {
      finishWheel(); if (_dirty) refresh(); pauseFollow();
      if (_snapshotDegraded) { status(tr("模式转换需要停播后建立力度基准。")); return; }
      for (int index : selectedNotes()) {
            const auto& note = _notes[index];
            const auto old = _pending.value(note.note, {note.type, note.raw});
            const int value = NoteVelocity::effective(note.base, old.type, old.raw);
            _pending.insert(note.note, {type, type == Note::ValueType::USER_VAL ? value : NoteVelocity::offsetFor(note.base, value)});
            }
      applyPending(); updateSurfaces(); if (_view) _view->update();
      }

void PerformanceEditor::setSelectedValue(double value)
      {
      finishWheel(); if (_dirty) refresh(); pauseFollow();
      if (_parameter->currentIndex() == 1 && _selectedTempoTick >= 0) {
            QString error;
            _committing = true;
            ParameterEdit::tempos(_score, {{_selectedTempoTick, value}}, _selectedTempoTick, &error);
            _committing = false; _dirty = true; scheduleRefresh(); status(error); return;
            }
      if (_parameter->currentIndex() != 0) { status(tr("先单击速度节点再写入 BPM；踏板请拖动端点。")); return; }
      for (int index : selectedNotes()) setNoteValue(index, value);
      applyPending(); updateSurfaces(); if (_view) _view->update();
      }

void PerformanceEditor::applyPending()
      {
      if (_dragging || _wheelIndex >= 0 || !_score || !hasPending()) { status(); return; }
      if (_score->isPlaying() || (seq && seq->isPlaying())) { status(); return; }
      if (seq && !seq->backgroundRenderingIdle()) { _commitTimer.start(20); return; }
      if (_score->masterScore()->state() != _state) {
            _pending.clear(); _dirty = true; scheduleRefresh();
            status(tr("乐谱已被其他操作修改，待提交预览已取消。")); return;
            }
      const auto edits = _pending;
      const int undoIndex = _score->undoStack()->getCurIdx();
      _committing = true;
      const bool changed = ParameterEdit::velocities(_score, edits);
      _pending.clear(); _committing = false;
      // Velocity properties do not change timing, dynamics or event bases. Only
      // reuse this snapshot when the completed native macro proves that no other
      // property/command was added by a host or plugin callback. Undo, external
      // edits and late playback snapshots still take the normal rebuild path.
      bool reuse = changed && !_snapshotDegraded && !_notes.isEmpty()
            && _score->undoStack()->getCurIdx() == undoIndex + 1;
      auto macro = _score->undoStack()->last();
      if (!macro) reuse = false;
      QSet<ScoreElement*> targets;
      for (auto i = edits.cbegin(); i != edits.cend(); ++i) {
            targets.insert(i.key()); if (!_noteIndex.contains(i.key())) reuse = false;
            }
      if (reuse) for (auto command : macro->commands()) {
            if (QString::fromLatin1(command->name()) != "ChangeProperty" || command->childCount()) { reuse = false; break; }
            const auto change = static_cast<const ChangeProperty*>(command);
            if (!targets.contains(change->getElement()) || (change->getId() != Pid::VELO_TYPE && change->getId() != Pid::VELO_OFFSET)) { reuse = false; break; }
            }
      if (reuse) {
            for (auto i = edits.cbegin(); i != edits.cend(); ++i) {
                  auto& cached = _notes[_noteIndex.value(i.key())]; cached.type = i.key()->veloType(); cached.raw = i.key()->veloOffset();
                  }
            _state = _score->masterScore()->state(); invalidateVisual();
            }
      _dirty = !reuse; scheduleRefresh(); status();
      }

void PerformanceEditor::flushPending()
      {
      finishWheel(false); cancelGesture();
      if (!hasPending()) return;
      if (seq) {
            seq->stopWait(); seq->waitForStoppedRendering();
            // The GUI stopped signal can still be queued while saveFile is synchronous.
            // Transport and background rendering are already stopped at this boundary.
            _score->setIsPlaying(false);
            }
      applyPending();
      }

void PerformanceEditor::flushForScore(Score* score)
      {
      for (auto editor : editors) if (editor->_score && score && editor->_score->masterScore() == score->masterScore()) editor->flushPending();
      }

void PerformanceEditor::fit(bool selection)
      {
      int from = 0, until = _endTick;
      if (selection) {
            const auto chosen = selectedNotes();
            if (chosen.isEmpty()) return;
            from = _notes[chosen.front()].tick; until = from + 1;
            for (int index : chosen) { from = qMin(from, _notes[index].tick); until = qMax(until, _notes[index].end); }
            }
      _viewport.pixelsPerQuarter = qBound(0.01, double(qMax(100, _canvas->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin)) * DIVISION / qMax(1, until - from), 1000.0);
      _scroll->setRange(0, qMax(0, _endTick - (until - from))); _scroll->setPageStep(until - from); _scroll->setValue(from);
      pauseFollow(); invalidateVisual();
      }

bool PerformanceEditor::overlayAllowed() const
      {
      return isVisible() && _view && _score && !_score->printing() && !_view->fotoMode()
            && !_view->editMode() && !_view->noteEntryMode();
      }

QRectF PerformanceEditor::laneRect() const { return QRectF(PerformanceViewport::gutter, 18, qMax(10, _canvas->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin), qMax(30, _canvas->height() - 36)); }

double PerformanceEditor::xForTick(int tick, bool onScore) const
      {
      if (!onScore) return PerformanceViewport::gutter + (tick - _scroll->value()) * _viewport.pixelsPerQuarter / DIVISION;
      if (!_view) return 0;
      if (_systemSegments.isEmpty()) return 0;
      auto next = std::lower_bound(_systemSegments.cbegin(), _systemSegments.cend(), tick, [](const SegmentInfo& s, int t) { return s.tick < t; });
      if (next == _systemSegments.cend()) return _view->matrix().map(_systemSegments.back().position).x();
      if (next == _systemSegments.cbegin()) return _view->matrix().map(next->position).x();
      const auto before = next - 1;
      const double x = before->position.x() + (next->position.x() - before->position.x()) * (tick - before->tick) / (next->tick - before->tick);
      return _view->matrix().map(QPointF(x, 0)).x();
      }

int PerformanceEditor::tickForX(double x, bool onScore) const
      {
      if (!onScore) return qBound(0, qRound((x - PerformanceViewport::gutter) * DIVISION / _viewport.pixelsPerQuarter) + _scroll->value(), _endTick);
      if (_systemSegments.isEmpty()) return 0;
      const double logical = _view->matrix().inverted().map(QPointF(x, 0)).x();
      auto next = std::lower_bound(_systemSegments.cbegin(), _systemSegments.cend(), logical, [](const SegmentInfo& s, double v) { return s.position.x() < v; });
      if (next == _systemSegments.cend()) return _systemSegments.back().tick;
      if (next == _systemSegments.cbegin()) return next->tick;
      const auto before = next - 1;
      const double width = next->position.x() - before->position.x();
      return qRound(before->tick + (width > 0 ? (logical - before->position.x()) * (next->tick - before->tick) / width : 0));
      }

int PerformanceEditor::snap(int tick, bool end) const
      {
      const auto& grid = end ? _pedalEnds : _pedalStarts;
      if (grid.isEmpty()) return 0;
      auto next = std::lower_bound(grid.cbegin(), grid.cend(), tick);
      if (next == grid.cend()) return grid.back();
      if (next == grid.cbegin()) return *next;
      const auto previous = next - 1;
      return tick - *previous < *next - tick ? *previous : *next;
      }

double PerformanceEditor::valueForY(double y, const QRectF& rect) const
      {
      const auto range = _viewport.ranges[rangeKind()];
      return qBound(range.minimum, range.minimum + (rect.bottom() - y) * (range.maximum - range.minimum) / rect.height(), range.maximum);
      }
double PerformanceEditor::yForValue(double value, const QRectF& rect) const
      {
      const auto range = _viewport.ranges[rangeKind()];
      return rect.bottom() - (value - range.minimum) * rect.height() / (range.maximum - range.minimum);
      }

void PerformanceEditor::paintForView(ScoreView* view, QPainter& painter)
      { for (auto editor : editors) if (editor->_view == view) editor->paintOverlay(painter); }

void PerformanceEditor::beginGesture(QPointF point, QRectF rect, bool onScore, int forcedAnchor)
      {
      if (!_score || _score->readOnly() || _dirty || _segments.isEmpty()) return;
      if (forcedAnchor >= 0 && !noteEditable(_notes[forcedAnchor])) return;
      if (_parameter->currentIndex() && (_score->isPlaying() || (seq && (seq->isPlaying() || !seq->backgroundRenderingIdle())))) { status(tr("速度与踏板请停播后编辑。")); return; }
      pauseFollow();
      _gestureBefore = _pending; _tempoDraft.clear(); _gestureLane = rect; _press = _last = point;
      _noteGesture = false; _cycleOnClick = false; _scoreGesture = onScore; _dragging = true; _anchor = forcedAnchor; _offsetDrag = false; _unlockedTempo = -1;
      if (_parameter->currentIndex() == 0) {
            double distance = 13;
            const int from = tickForX(rect.left() - 32, onScore), until = tickForX(rect.right() + 32, onScore);
            auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
            for (auto it = first; forcedAnchor < 0 && it != _notes.cend() && it->tick <= until; ++it) {
                  const int i = int(it - _notes.cbegin());
                  const auto& note = *it;
                  if (!noteEditable(note) || (onScore && note.system != _system)) continue;
                  const QPointF center(xForTick(note.tick, onScore), yForValue(noteValue(i), rect));
                  const double d = QLineF(point, center).length();
                  if (noteEditable(note) && d < distance) { distance = d; _anchor = i; }
                  }
            if (_tool->currentIndex() == 0 && _anchor < 0) { _dragging = false; return; }
            if (_tool->currentIndex() == 0 && _anchor >= 0) {
                  selectIndices({_anchor}, Qt::NoModifier, true);
                  _offsetDrag = true;
                  _dragTargets = _notes[_anchor].selected ? selectedNotes() : QVector<int>{_anchor};
                  _dragOriginal.clear(); for (int index : _dragTargets) _dragOriginal.insert(index, noteValue(index));
                  }
            }
      else if (_parameter->currentIndex() == 1) {
            for (const auto& tempo : _tempos)
                  if (QLineF(point, QPointF(xForTick(tempo.tick, onScore), yForValue(tempo.bpm, rect))).length() < 10) _unlockedTempo = tempo.tick;
            }
      else {
            _pedalTarget = nullptr;
            _pedalFrom = _pedalUntil = tickForX(point.x(), onScore);
            for (const auto& pedal : _pedals) {
                  if (std::abs(point.y() - rect.center().y()) > 14) continue;
                  if (std::abs(point.x() - xForTick(pedal.from, onScore)) < 10 || std::abs(point.x() - xForTick(pedal.until, onScore)) < 10) {
                        _pedalTarget = pedal.pedal; _pedalFrom = pedal.from; _pedalUntil = pedal.until;
                        _pedalEnd = std::abs(point.x() - xForTick(pedal.until, onScore)) < std::abs(point.x() - xForTick(pedal.from, onScore));
                        break;
                        }
                  }
            }
      if (_parameter->currentIndex() == 2) {
            _pedalTrack = _pedalTarget ? _pedalTarget->track() : _score->staffIdx(_score->staff(_contextTrack / VOICES)->part()) * VOICES;
            _pedalStarts.clear(); _pedalEnds.clear();
            const int staffTrack = (_pedalTrack / VOICES) * VOICES;
            for (auto segment = _score->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest))
                  for (int voice = 0; voice < VOICES; ++voice) {
                        auto element = segment->element(staffTrack + voice);
                        if (!element || !element->isChordRest()) continue;
                        auto cr = toChordRest(element);
                        _pedalStarts.append(cr->tick().ticks()); _pedalEnds.append((cr->tick() + cr->actualTicks()).ticks());
                        }
            for (auto grid : {&_pedalStarts, &_pedalEnds}) {
                  std::sort(grid->begin(), grid->end()); grid->erase(std::unique(grid->begin(), grid->end()), grid->end());
                  }
            if (!_pedalTarget) { _pedalFrom = snap(_pedalFrom); _pedalUntil = snap(_pedalUntil, true); }
            }
      if (_parameter->currentIndex() == 1) {
            _selectedTempoTick = _unlockedTempo;
            for (const auto& tempo : _tempos) if (tempo.tick == _selectedTempoTick) {
                  QSignalBlocker blocker(_number); _number->setValue(tempo.bpm); break;
                  }
            }
      // The host's application filter dispatches Escape directly to QWidget::event.
      // Observe the application only for our active gesture, ahead of that filter.
      qApp->installEventFilter(this);
      if (onScore) { _view->setFocus(Qt::MouseFocusReason); _view->grabMouse(); }
      else { _canvas->setFocus(Qt::MouseFocusReason); _canvas->grabMouse(); }
      }

void PerformanceEditor::moveGesture(QPointF point)
      {
      if (!_dragging) return;
      QElapsedTimer gestureTimer; gestureTimer.start();
      if (_parameter->currentIndex() == 2) {
            const int tick = tickForX(point.x(), _scoreGesture);
            if (_pedalTarget) {
                  if (_pedalEnd) { const int end = snap(tick, true); if (end > _pedalFrom) _pedalUntil = end; }
                  else { const int from = snap(tick); if (from < _pedalUntil) _pedalFrom = from; }
                  }
            else {
                  const int pressed = tickForX(_press.x(), _scoreGesture);
                  _pedalFrom = snap(qMin(pressed, tick)); _pedalUntil = snap(qMax(pressed, tick), true);
                  }
            }
      else if (_offsetDrag) {
            const auto range = _viewport.ranges[rangeKind()];
            const double delta = _noteGesture ? (_press.y() - point.y()) * (range.maximum - range.minimum) / qMax(1.0, _gestureLane.height()) : valueForY(point.y(), _gestureLane) - valueForY(_press.y(), _gestureLane);
            for (int index : _dragTargets) setNoteValue(index, _dragOriginal.value(index) + delta, false);
            }
      else {
            const bool line = _tool->currentIndex() == 2;
            const QPointF start = line ? _press : _last;
            if (line) { _pending = _gestureBefore; _tempoDraft.clear(); }
            int from = tickForX(start.x(), _scoreGesture), until = tickForX(point.x(), _scoreGesture);
            double v0 = valueForY(start.y(), _gestureLane), v1 = valueForY(point.y(), _gestureLane);
            if (until < from) { std::swap(from, until); std::swap(v0, v1); }
            auto value = [from, until, v0, v1](int tick) { return until == from ? v1 : v0 + (v1 - v0) * (tick - from) / (until - from); };
            if (_parameter->currentIndex() == 0) {
                  const bool selected = !_selectedIndices.isEmpty();
                  auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
                  for (auto it = first; it != _notes.cend() && it->tick <= until; ++it)
                        if ((!selected || it->selected) && (!_scoreGesture || it->system == _system)) setNoteValue(int(it - _notes.cbegin()), value(it->tick), false);
                  }
            else {
                  if (_unlockedTempo >= 0 && _tool->currentIndex() == 0) _tempoDraft[_unlockedTempo] = v1;
                  else {
                        auto first = std::lower_bound(_segments.cbegin(), _segments.cend(), from, [](const SegmentInfo& s, int t) { return s.tick < t; });
                        for (auto it = first; it != _segments.cend() && it->tick <= until; ++it)
                              if (it->tick < _endTick && (!_scoreGesture || it->system == _system)) {
                                    bool locked = false;
                                    auto tempo = std::lower_bound(_tempos.cbegin(), _tempos.cend(), it->tick, [](const TempoInfo& info, int t) { return info.tick < t; });
                                    for (; tempo != _tempos.cend() && tempo->tick == it->tick; ++tempo)
                                          if (tempo->visible && tempo->tick != _unlockedTempo) locked = true;
                                    if (!locked) _tempoDraft[it->tick] = value(it->tick);
                                    }
                        }
                  }
            }
      if (_parameter->currentIndex() == 0) {
            if (_anchor >= 0) setNoteValue(_anchor, noteValue(_anchor)); else status();
            }
      if (qEnvironmentVariableIsSet("PERFORMANCE_BENCHMARK")) qInfo("Performance gesture %d editable targets: %.2f ms", _selectedIndices.size(), gestureTimer.nsecsElapsed() / 1e6);
      _last = point; updateSurfaces(); if (_view) _view->update();
      }

void PerformanceEditor::finishGesture()
      {
      if (!_dragging) return;
      drainGesture();
      if (_cycleOnClick && QLineF(_press, _last).length() < 3) { const auto candidates = _pressedCandidates; cancelGesture(); cycleHit(candidates); return; }
      _dragging = false; qApp->removeEventFilter(this);
      if (_view) _view->releaseMouse(); _canvas->releaseMouse(); _noteCanvas->releaseMouse();
      if (_parameter->currentIndex() == 0) applyPending();
      else {
            QString error;
            _committing = true;
            if (_parameter->currentIndex() == 1) {
                  if (_unlockedTempo < 0 && !_tempoDraft.isEmpty()) {
                        const int end = _tempoDraft.lastKey();
                        for (const auto& segment : _segments) if (segment.tick > end && segment.tick < _endTick) {
                              _tempoDraft[segment.tick] = segment.bpm; break;
                              }
                        }
                  ParameterEdit::tempos(_score, _tempoDraft, _unlockedTempo, &error);
                  }
            else {
                  ParameterEdit::pedal(_score, _pedalTrack, _pedalFrom, _pedalUntil, _pedalTarget, &error);
                  }
            _committing = false; _tempoDraft.clear(); _dirty = true; scheduleRefresh(); status(error);
            }
      _gestureBefore.clear();
      }

void PerformanceEditor::cancelGesture()
      {
      _moveTimer.stop(); _moveQueued = false;
      if (!_dragging) return;
      _pending = _gestureBefore; _gestureBefore.clear(); _tempoDraft.clear(); _dragging = false; qApp->removeEventFilter(this);
      if (_view) _view->releaseMouse(); if (_canvas) _canvas->releaseMouse(); if (_noteCanvas) _noteCanvas->releaseMouse();
      }

bool PerformanceEditor::eventFilter(QObject* object, QEvent* event)
      {
      if (_wheelIndex >= 0 && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept(); if (event->type() == QEvent::KeyPress) cancelWheel(); return true;
            }
      if (_wheelIndex >= 0 && event->type() == QEvent::MouseButtonPress) { if (object->objectName() == "performanceCancelPreview") cancelWheel(); else { finishWheel(); refresh(); } }
      if (surfaceEvent(object, event)) return true;
      if (_dragging && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept();
            if (event->type() == QEvent::KeyPress) { cancelGesture(); updateSurfaces(); if (_view) _view->update(); }
            return true;
            }
      const bool onScore = object == _view;
      if (onScore && event->type() == QEvent::MouseButtonRelease) QTimer::singleShot(0, this, [this] { syncOverlayTracking(); });
      if (object != _canvas && !onScore) return false;
      if (event->type() == QEvent::Show && object == _canvas) { scheduleRefresh(); return false; }
      if (onScore && !overlayAllowed()) return false;
      if (onScore && overlayEvent(event)) return true;
      if (event->type() == QEvent::ShortcutOverride && _dragging && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) { event->accept(); return true; }
      if (event->type() == QEvent::Hide) cancelGesture();
      if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && _dragging) {
            cancelGesture(); updateSurfaces(); if (_view) _view->update(); return true;
            }
      if (event->type() == QEvent::MouseMove) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (_dragging) { queueGesture(mouse->pos()); return true; }

            }
      if (event->type() == QEvent::MouseButtonPress) {
            auto mouse = static_cast<QMouseEvent*>(event);
            const QRectF contextLane = onScore ? _scoreLane : laneRect();
            if (mouse->button() == Qt::RightButton && contextLane.contains(mouse->pos()) && _parameter->currentIndex() == 0) {
                  QMenu menu;
                  for (int i = 0; i < _notes.size(); ++i) {
                        const auto& note = _notes[i];
                        if ((onScore && note.system != _system) || std::abs(xForTick(note.tick, onScore) - mouse->x()) > 22) continue;
                        auto action = menu.addAction(tr("音高 %1 · 声部 %2 · %3%4").arg(note.pitch).arg(note.track % VOICES + 1).arg(noteValue(i))
                              .arg(note.audible ? QString() : tr(" · 不独立发声")));
                        action->setEnabled(note.audible && note.enabled);
                        connect(action, &QAction::triggered, this, [this, i] {
                              selectIndices({i}, Qt::NoModifier); updateHover(i); audition(i);
                              });
                        }
                  if (!menu.isEmpty()) menu.exec(mouse->globalPos());
                  return true;
                  }
            if (mouse->button() != Qt::LeftButton) return false;
            if (onScore && _handles->isChecked() && _parameter->currentIndex() == 0) {
                  QVector<int> handles = _selectedIndices;
                  if (_hover >= 0 && !handles.contains(_hover)) handles.append(_hover);
                  for (int i : handles) {
                        if (i >= _notes.size()) continue;
                        const auto& note = _notes[i];
                        const QPointF center = _view->matrix().mapRect(note.bounds).topRight() + QPointF(12, -12);
                        if (noteEditable(note) && QLineF(center, mouse->pos()).length() < 11) {
                              if (!note.selected) {
                                    selectIndices({i}, mouse->modifiers());
                                    }
                              beginGesture(mouse->pos(), QRectF(mouse->x() - 20, mouse->y() - 80, 40, 160), true, i);
                              if (_dragging) {
                                    _anchor = i; _offsetDrag = true;
                                    _dragTargets = note.selected ? selectedNotes() : QVector<int>{i};
                                    _dragOriginal.clear(); for (int index : _dragTargets) _dragOriginal[index] = noteValue(index);
                                    }
                              return _dragging;
                              }
                        }
                  }
            const QRectF rect = onScore ? _scoreLane : laneRect();
            if (rect.contains(mouse->pos())) { beginGesture(mouse->pos(), rect, onScore); return _dragging; }
            }
      if (event->type() == QEvent::MouseButtonRelease && _dragging) { queueGesture(static_cast<QMouseEvent*>(event)->pos()); finishGesture(); return true; }
      return false;
      }
}
