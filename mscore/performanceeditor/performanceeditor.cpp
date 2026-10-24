#include "performanceeditor.h"
#include "mscore/workflow/toolicons.h"
#include "../workflow/featuregate.h"
#include "../workflow/workspaceviews.h"
#include "../workflow/editorareas.h"
#ifdef SCRIPT_INTERFACE
#include "rhythmpane.h"
#include "mscore/workflow/builtinpanels.h"
#include "spectrumpane.h"
#include "rollwriting.h"
#endif
#include <QJsonDocument>
#include "markvelocity.h"
#include "performanceselection.h"
#include "workflowlanes.h"
#include "../workflow/workflowcontext.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/sig.h"
#include "libmscore/undo.h"
#include <QSplitter>
#include <QToolButton>
#include <QSettings>
#include <QScopedValueRollback>
#include <QElapsedTimer>
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "libmscore/chord.h"
#include "libmscore/symbol.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/system.h"
#include "libmscore/pedal.h"
#include "libmscore/spanner.h"
#include "libmscore/tempotext.h"
#include "libmscore/textline.h"
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
#include <QTabBar>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QActionGroup>
#include <QToolTip>
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
QSize PerformanceEditor::minimumSizeHint() const {
      return QSize(260, layout() ? qMax(160, qMax(layout()->minimumSize().height(), layout()->minimumHeightForWidth(width()))) : 240);
      }

PerformanceEditor::PerformanceEditor(QWidget* parent) : QWidget(parent)
      {
      _score = nullptr;
      _appearance.load();
      _noteTips = QSettings().value("performanceEditor/noteTooltips", true).toBool();
      _keyboardNames = QSettings().value("performanceEditor/keyboardNames", true).toBool();
      _scoreVoiceColors = QSettings().value("performanceEditor/scoreVoiceColors", true).toBool();
      _bandHeight = qBound(48, QSettings().value("performanceEditor/bandHeight", 48).toInt(), 240);
      _bandFollowsSystem = QSettings().value("performanceEditor/bandFollowsSystem", false).toBool();
      editors.append(this);
      setObjectName("performanceEditor");
      auto layout = new QVBoxLayout(this);
      layout->setSizeConstraint(QLayout::SetNoConstraint);
      layout->setContentsMargins(6, 4, 6, 4);
      auto toolHost = new QWidget(this);
      toolHost->setObjectName("performanceToolbarContent");
      auto toolLayout = new QVBoxLayout(toolHost);
      toolLayout->setContentsMargins(0, 0, 0, 0);
      toolLayout->setSpacing(4);
      auto toolbar = new ControlScrollArea(this);
      toolbar->setObjectName("performanceControlScroll");
      toolbar->setWidget(toolHost);
      layout->addWidget(toolbar);
      auto row = new ControlFlowLayout; _topRow = row;
      auto combo = [this, row](const QStringList& labels, const char* name) {
            auto box = new QComboBox(this); box->addItems(labels); box->setObjectName(name); row->addWidget(box); return box;
            };
      _parameter = combo({tr("力度"), tr("速度 · 阶梯"), tr("延音踏板")}, "performanceParameter");
      _axis = combo({tr("MIDI 力度"), tr("相对 %")}, "performanceAxis");
      _scope = combo({tr("当前乐器"), tr("当前谱表"), tr("全谱")}, "performanceScope");
      _voice = combo({tr("声部 1–4"), "1", "2", "3", "4"}, "performanceVoice"); row->removeWidget(_voice); _voice->hide();
      auto voiceButton = new QToolButton(this); voiceButton->setText(tr("声部…")); voiceButton->setPopupMode(QToolButton::InstantPopup);
      auto voiceMenu = new QMenu(voiceButton); voiceMenu->setObjectName("performanceVoiceMenu"); voiceButton->setMenu(voiceMenu);
      connect(voiceMenu,&QMenu::aboutToShow,this,[this,voiceMenu]{for(int v=0;v<4;++v){QSignalBlocker block(voiceMenu->actions()[v]);voiceMenu->actions()[v]->setChecked(_voiceMask&(1<<v));}QSignalBlocker block(voiceMenu->actions()[4]);voiceMenu->actions()[4]->setChecked(_ghostVoices);});
      for (int v = 0; v < 4; ++v) {
            auto action = voiceMenu->addAction(tr("声部 %1").arg(v + 1)); action->setCheckable(true); action->setChecked(true);
            connect(action, &QAction::toggled, this, [this, v](bool on) { cancelGesture(); _voiceMask = on ? _voiceMask | (1 << v) : _voiceMask & ~(1 << v); rebuildFilter(); });
            }
      auto ghost = voiceMenu->addAction(tr("显示过滤外音符为淡色参考")); ghost->setCheckable(true); ghost->setChecked(true);
      connect(ghost, &QAction::toggled, this, [this](bool on) { finishWheel(); cancelGesture(); _ghostVoices = on; rebuildFilter(); invalidateVisual(); invalidateOverlay(); });
      voiceMenu->addSeparator();
      for (int scope = 0; scope < 3; ++scope) {
            auto action = voiceMenu->addAction(scope == 0 ? tr("选择这些声部：时间选区／当前视窗") : (scope == 1 ? tr("选择这些声部：当前视窗") : tr("选择这些声部：全曲")));
            connect(action, &QAction::triggered, this, [this, scope] { selectVoices(scope); });
            }
      row->addWidget(voiceButton);
      auto staffButton = new QToolButton(this); staffButton->setText(tr("谱表…")); staffButton->setObjectName("performanceStaffFilter"); staffButton->setPopupMode(QToolButton::InstantPopup);
      staffButton->setToolTip(tr("在上方的当前乐器／当前谱表／全谱范围内筛选谱表；过滤保留原选择，只限制新编辑。"));
      _staffMenu = new QMenu(staffButton); staffButton->setMenu(_staffMenu); row->addWidget(staffButton);
      connect(_staffMenu, &QMenu::aboutToShow, this, &PerformanceEditor::rebuildStaffMenu);
      _tool = combo({tr("拖动"), tr("铅笔"), tr("直线")}, "performanceTool");
      _pedalTool = combo({tr("选择／拖动"), tr("画踩下段"), tr("画抬起间隙"), tr("分开")}, "performancePedalTool");
      for (int i = 0; i < 4; ++i) _pedalTool->setItemIcon(i, componentToolIcon(QStringList{"select", "down", "up", "line"}[i]));
      _pedalTool->hide();
      _pedalTool->setToolTip(tr("选择：拖两端改时机，拖内部移动整段；空白处仍可画踩下段。画抬起间隙：划过已有踏板。分开：在段内点一下。"));
      connect(_pedalTool, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { cancelGesture(); status(); updateSurfaces(); });
      _pedalGrid = combo({tr("踏板 1/16"), tr("踏板 1/32"), tr("踏板 1/64"), tr("踏板 1/128"), tr("踏板自由"), tr("踏板音符边界")}, "performancePedalGrid");
      _pedalGrid->setCurrentIndex(qBound(0, QSettings().value("performanceEditor/pedalGrid", 0).toInt(), 5)); _pedalGrid->hide();
      _pedalGrid->setToolTip(tr("按小节起点吸附细分；自由模式精确到 1 tick。不会添加音符或休止。"));
      connect(_pedalGrid, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) { cancelGesture(); QSettings().setValue("performanceEditor/pedalGrid", value); });
      _pedalDelete = new QToolButton(this); _pedalDelete->setObjectName("performanceDeletePedal");
      _pedalDelete->setText(tr("删除踏板")); _pedalDelete->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      setComponentToolIcon(_pedalDelete, "delete"); _pedalDelete->setToolTip(tr("删除选中的踏板段，Delete也可使用；Ctrl+Z撤销。"));
      _pedalDelete->hide(); _pedalDelete->setEnabled(false); row->addWidget(_pedalDelete);
      connect(_pedalDelete, &QToolButton::clicked, this, &PerformanceEditor::deleteSelectedPedal);
      row->setAlignment(Qt::AlignLeft);
      toolLayout->addLayout(row);
      _playButton = new QToolButton(this); _playButton->setObjectName("performancePlay"); _playButton->setText(tr("播放"));
      setComponentToolIcon(_playButton, "play");
      _playButton->setToolTip(tr("从当前播放光标开始／停止（空格）")); connect(_playButton, &QToolButton::clicked, this, [this] { togglePlayback(); }); row->addWidget(_playButton);
      auto locate = new QToolButton(this); locate->setObjectName("performanceLocateSelection"); locate->setText(tr("移至选音"));
      setComponentToolIcon(locate, "locate");
      locate->setToolTip(tr("把播放光标移到最近选中的音；双击音符可从该处播放。")); connect(locate, &QToolButton::clicked, this, &PerformanceEditor::locateSelection); row->addWidget(locate);
      _auditionButton = new QToolButton(this); _auditionButton->setObjectName("performanceAudition"); _auditionButton->setText(tr("试听")); _auditionButton->setCheckable(true);
      setComponentToolIcon(_auditionButton, "audition");
      _auditionButton->setChecked(QSettings().value("performanceEditor/audition", true).toBool()); _auditionButton->setToolTip(tr("停播时点选音符／力度端点发声，不移动播放位置。"));
      connect(_auditionButton, &QToolButton::toggled, this, [](bool on) { QSettings().setValue("performanceEditor/audition", on); }); row->addWidget(_auditionButton);
      _wheelButton = new QToolButton(this); _wheelButton->setObjectName("performanceWheelVelocity");
      _wheelButton->setText(tr("滚轮调力度")); _wheelButton->setCheckable(true);
      _wheelButton->setChecked(QSettings().value("performanceEditor/wheelVelocity", false).toBool());
      _wheelButton->setToolTip(tr("指向音符／力度柱／音旁手柄滚轮细调 1，Shift 粗调 8。编辑器 Alt+滚轮临时调节；谱面开启此模式后直接指向未选音头。Alt 保留谱面移动，Ctrl 仍缩放。只改指向的一个音，停转 300 ms 合并一次撤销，Esc 取消。"));
      connect(_wheelButton, &QToolButton::toggled, this, [this](bool on) { finishWheel(); QSettings().setValue("performanceEditor/wheelVelocity", on); syncOverlayTracking(); invalidateOverlay(); });
      row->addWidget(_wheelButton);
      _wheelTimer.setParent(this); _wheelTimer.setSingleShot(true); _wheelTimer.setInterval(300); _wheelTimer.setObjectName("performanceWheelTimer");
      connect(&_wheelTimer, &QTimer::timeout, this, [this] { finishWheel(); });
      auto second = new ControlFlowLayout; _actionsRow = second;
      auto button = [this, second](const QString& label, auto callback) {
            auto result = new QPushButton(label, this); second->addWidget(result); connect(result, &QPushButton::clicked, this, callback); return result;
            };
      _handles = new QCheckBox(tr("音旁手柄"), this); second->addWidget(_handles);
      _band = new QCheckBox(tr("谱行参数带"), this); second->addWidget(_band);
      _band->setObjectName("performanceScoreBand");
      button(tr("跟随力度记号"), [this] { convertSelected(Note::ValueType::OFFSET_VAL); })->setObjectName("performanceConvertRelative");
      button(tr("固定这个音的力度"), [this] { convertSelected(Note::ValueType::USER_VAL); })->setObjectName("performanceConvertAbsolute");
      _number = new QDoubleSpinBox(this); _number->setObjectName("performanceValue"); _number->setRange(1, 127); _number->setDecimals(0); second->addWidget(_number);
      button(tr("写入选中"), [this] { setSelectedValue(_number->value()); })->setObjectName("performanceWriteValue");
      button(tr("全曲"), [this] { fit(); });
      button(tr("选区"), [this] { fit(true); });
      button(tr("取消预览"), [this] { cancelWheel(); cancelGesture(); _pending.clear(); _propertyPending.clear(); status(); updateSurfaces(); if (_view) _view->update(); })->setObjectName("performanceCancelPreview");
      auto viewControls = new QToolButton(this); viewControls->setText(tr("视窗…")); viewControls->setPopupMode(QToolButton::InstantPopup);
      auto viewMenu = new QMenu(viewControls); viewControls->setMenu(viewMenu); second->addWidget(viewControls);
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
            connect(action, &QAction::triggered, this, [this, mode] { _appearance.mode = mode; _appearance.save(); invalidateVisual(); invalidateOverlay(); });
            }
      auto tips = displayMenu->addAction(tr("显示音符详细悬浮提示")); tips->setObjectName("performanceNoteTooltips"); tips->setCheckable(true); tips->setChecked(_noteTips);
      connect(tips, &QAction::toggled, this, [this](bool on) { _noteTips = on; QSettings().setValue("performanceEditor/noteTooltips", on); QToolTip::hideText(); });
      auto keyNames = displayMenu->addAction(tr("键盘显示音名")); keyNames->setObjectName("performanceKeyboardNames"); keyNames->setCheckable(true); keyNames->setChecked(_keyboardNames);
      connect(keyNames, &QAction::toggled, this, [this](bool on) { _keyboardNames = on; QSettings().setValue("performanceEditor/keyboardNames", on); invalidateVisual(); });
      auto scoreColors = displayMenu->addAction(tr("谱行带／手柄使用原生声部颜色")); scoreColors->setObjectName("performanceScoreVoiceColors"); scoreColors->setCheckable(true); scoreColors->setChecked(_scoreVoiceColors);
      connect(scoreColors, &QAction::toggled, this, [this](bool on) { _scoreVoiceColors = on; QSettings().setValue("performanceEditor/scoreVoiceColors", on); invalidateOverlay(); });
      auto placement = displayMenu->addMenu(tr("谱行带位置")); placement->setObjectName("performanceBandPlacement");
      auto placementGroup = new QActionGroup(this);
      for (int mode = 0; mode < 2; ++mode) {
            auto action = placement->addAction(mode ? tr("跟随选中／播放谱行下方") : tr("固定在视口底部"));
            action->setObjectName(QString("performanceBandPosition%1").arg(mode)); action->setCheckable(true); action->setChecked(_bandFollowsSystem == bool(mode)); placementGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, mode] { finishWheel(); cancelGesture(); invalidateOverlay(); _bandFollowsSystem = bool(mode); QSettings().setValue("performanceEditor/bandFollowsSystem", _bandFollowsSystem); if (_view) _view->update(); });
            }
      auto heights = displayMenu->addMenu(tr("谱行带高度"));
      for (int h : {48, 80, 112, 144}) {
            auto action = heights->addAction(tr("%1 像素").arg(h)); action->setObjectName(QString("performanceBandHeight%1").arg(h));
            connect(action, &QAction::triggered, this, [this, h] { cancelGesture(); invalidateOverlay(); _bandHeight = h; QSettings().setValue("performanceEditor/bandHeight", h); if (_view) _view->update(); });
            }
      auto values = displayMenu->addAction(tr("一直显示力度（谱面与卷帘）")); values->setCheckable(true); values->setChecked(QSettings().value("performanceEditor/showValues", false).toBool()); _showValues = values->isChecked();
      connect(values, &QAction::toggled, this, [this](bool on) { _showValues = on; QSettings().setValue("performanceEditor/showValues", on); if(on)scheduleRefresh(); invalidateOverlay(); syncOverlayTracking(); updateSurfaces(); });
      auto appearanceAction = displayMenu->addAction(tr("配色／力度柱宽…")); appearanceAction->setObjectName("performanceAppearance");
      connect(appearanceAction, &QAction::triggered, this, [this] { if (_appearance.edit(this)) { applyAppearance(); invalidateVisual(); if (_view) _view->update(); } });
      connect(displayMenu->addAction(tr("REAPER 默认力度色")), &QAction::triggered, this, [this] { _appearance = PerformanceAppearance(); _appearance.save(); applyAppearance(); invalidateVisual(); if (_view) _view->update(); });
      second->addWidget(appearance);
      _followButton = new QToolButton(this); _followButton->setText(tr("跟随播放")); _followButton->setToolTip(tr("手动浏览后暂停跟随；单击恢复，下一次开始播放也会恢复。")); _followButton->setCheckable(true); _followButton->setChecked(true);
      connect(_followButton, &QToolButton::clicked, this, [this] { _following = true; _followButton->setChecked(true); updatePlayhead(); });
      second->addWidget(_followButton);
      second->setAlignment(Qt::AlignLeft);
      toolLayout->addLayout(second);
      _splitter = new QSplitter(Qt::Vertical, this); _splitter->setObjectName("performanceMainSplitter"); _splitter->setChildrenCollapsible(false);
      auto noteArea = new QWidget(_splitter); noteArea->setObjectName("performanceNotesArea"); auto noteLayout = new QVBoxLayout(noteArea); noteLayout->setContentsMargins(0, 0, 0, 0); noteLayout->setSpacing(0);
      auto paneControls = [this](QVBoxLayout* parent, bool notes) {
            QWidget* controls = nullptr;
            auto row = new QHBoxLayout; row->setContentsMargins(notes ? 3 : 1, 0, notes ? 3 : 1, 0); row->setSpacing(notes ? 3 : 0);
            if (notes) row->addWidget(new QLabel(tr("音高")));
            else { controls = new QWidget(_ruler); controls->setObjectName("performanceRangeControls"); controls->setGeometry(0, 0, PerformanceViewport::gutter, _ruler->height()); controls->setLayout(row); }
            auto add = [this, row, notes](const QString& text, const QString& tip, const QString& name, auto callback) { auto button = new QToolButton; button->setText(text); button->setToolTip(tip); button->setObjectName(name); if (!notes) { button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); button->setStyleSheet("QToolButton { padding: 0px; margin: 0px; }"); } connect(button, &QToolButton::clicked, this, callback); row->addWidget(button); };
            const QString prefix = notes ? "performancePitch" : "performanceRange";
            add("−", tr("只缩小本区纵向视窗，不改变音符属性"), prefix + "Out", [this, notes] { zoomVertical(notes, 1 / 1.4); });
            add("+", tr("只放大本区纵向视窗，围绕当前选音／数值"), prefix + "In", [this, notes] { zoomVertical(notes, 1.4); });
            add(notes ? tr("音域") : tr("适"), notes ? tr("显示乐器音域") : tr("匹配值：放大到选中音的数值范围；没有选音时匹配当前视窗"), prefix + "Fit", [this, notes] { if (notes) centerPitch(false, true); else resetRange(true); });
            add(notes ? tr("选音") : tr("全"), notes ? tr("保持行高，音高区居中选音") : tr("恢复完整数值范围"), prefix + "Reset", [this, notes] { if (notes) centerPitch(true); else resetRange(false); });
            if (notes) { row->addStretch(); parent->addLayout(row); }
            };
      paneControls(noteLayout, true);
#ifdef SCRIPT_INTERFACE
      auto writeToggle=new QToolButton(noteArea);writeToggle->setObjectName("rollWriteToggle");writeToggle->setDefaultAction(rollWritingAction());writeToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);if(auto row=qobject_cast<QHBoxLayout*>(noteLayout->itemAt(0)->layout()))row->insertWidget(qMax(0,row->count()-1),writeToggle);
#endif
      _noteCanvas = new PerformanceCanvas(this, PerformanceSurface::Notes); _noteCanvas->installEventFilter(this); noteLayout->addWidget(_noteCanvas, 1);
      _pitchScroll = new QScrollBar(Qt::Vertical, _noteCanvas); _pitchScroll->setRange(0, 12700);
      connect(_pitchScroll, &QScrollBar::valueChanged, this, [this](int value) { pauseFollow(); _viewport.topPitch = _viewport.pitchAtIndex(_viewport.rowMaximum() - value / 100.0); invalidateVisual(); });
#ifdef SCRIPT_INTERFACE
      _rollWriting=new RollWritingBar(this,writeToggle,noteArea);noteLayout->insertWidget(1,_rollWriting);
#endif
      _splitter->addWidget(noteArea);
      auto parameterArea = new QWidget(_splitter); parameterArea->setObjectName("performanceParametersArea"); auto areaLayout = new QVBoxLayout(parameterArea); areaLayout->setContentsMargins(0, 0, 0, 0); areaLayout->setSpacing(0);
      _ruler = new PerformanceCanvas(this, PerformanceSurface::Ruler); _ruler->installEventFilter(this); areaLayout->addWidget(_ruler);
      paneControls(areaLayout, false);
      _parameterHint = new QLabel(this); _parameterHint->setObjectName("performanceParameterHint"); _parameterHint->hide();
      _canvas = new PerformanceCanvas(this, PerformanceSurface::Parameter); _canvas->installEventFilter(this); areaLayout->addWidget(_canvas, 1);
      _valueScroll = new QScrollBar(Qt::Vertical, _canvas); _valueScroll->setObjectName("performanceValueScroll");
      connect(_valueScroll, &QScrollBar::valueChanged, this, [this](int position) {
            if (rangeKind() == 3) return; pauseFollow(); const auto limit = PerformanceViewport::limits(rangeKind()); auto& range = _viewport.ranges[rangeKind()]; const double span = range.maximum - range.minimum;
            range.maximum = limit.maximum - (limit.maximum - limit.minimum - span) * position / 10000.0; range.minimum = range.maximum - span; invalidateVisual();
            });
      _splitter->addWidget(parameterArea); _splitter->setStretchFactor(0, 1); _splitter->setStretchFactor(1, 1); layout->addWidget(_splitter, 1);

      connect(_splitter, &QSplitter::splitterMoved, this, [this] { QSettings().setValue("performanceEditor/splitter", _splitter->saveState()); });
      _scroll = new QScrollBar(Qt::Horizontal, this); layout->addWidget(_scroll);
      addWorkflowLanes(this,&_workflowPanel,_splitter);_splitter->addWidget(_workflowPanel);_workflowPanel->hide();
      auto workflowButton=new QToolButton(this);workflowButton->setObjectName("performanceWorkflowToggle");workflowButton->setText(tr("音轨／演奏对应"));workflowButton->setCheckable(true);second->addWidget(workflowButton);
      connect(workflowButton,&QToolButton::toggled,this,[this](bool show){setEditorAreaVisible(this,"performanceWorkflowPanel",show);});
      _splitter->restoreState(QSettings().value("performanceEditor/splitter").toByteArray());
      auto areas=new QToolButton(this);areas->setText(tr("区域"));areas->setObjectName("performanceAreaMenu");areas->setPopupMode(QToolButton::InstantPopup);auto areaMenu=new QMenu(areas);areas->setMenu(areaMenu);second->addWidget(areas);
      for(auto item:QList<QPair<QString,QWidget*>>{{tr("钢琴卷帘"),noteArea},{tr("力度／速度／踏板"),parameterArea},{tr("音轨／演奏对应"),_workflowPanel}}){auto action=areaMenu->addAction(item.first);action->setCheckable(true);action->setChecked(!item.second->isHidden());connect(action,&QAction::toggled,this,[this,item,workflowButton](bool on){if(item.second==_workflowPanel){workflowButton->setChecked(on);return;}QSettings().setValue("performanceEditor/area/"+item.second->objectName(),on);item.second->setVisible(on);if(on){auto sizes=_splitter->sizes();int n=_splitter->indexOf(item.second);if(n>=0&&sizes[n]<40){sizes[n]=140;_splitter->setSizes(sizes);}}updateGeometry();});connect(areaMenu,&QMenu::aboutToShow,action,[item,action]{QSignalBlocker block(action);action->setChecked(!item.second->isHidden());});}
      auto restoreAreas=areaMenu->addAction(tr("恢复区域高度"));connect(restoreAreas,&QAction::triggered,this,[this]{_splitter->setSizes({150,130,180});});
      noteArea->setVisible(QSettings().value("performanceEditor/area/performanceNotesArea",true).toBool());parameterArea->setVisible(QSettings().value("performanceEditor/area/performanceParametersArea",true).toBool());
      QTimer::singleShot(0,this,[workflowButton]{workflowButton->setChecked(QSettings().value("performanceEditor/area/performanceWorkflowPanel",false).toBool());});
#ifdef SCRIPT_INTERFACE
      installExpressionTools(this,_splitter,layout);
      installSpectrumPane(this,_splitter);
      installRhythmPane(this,_splitter);
#endif
      installEditorAreas(this,_splitter,areaMenu);
#ifdef SCRIPT_INTERFACE
      if (auto tabs = findChild<QTabBar*>("workflowExpressionTabs")) {
            second->removeWidget(areas);
            auto heading = new QWidget(this);
            auto headingLayout = new QHBoxLayout(heading);
            headingLayout->setContentsMargins(0, 0, 0, 0);
            layout->removeWidget(tabs);
            headingLayout->addWidget(tabs, 1);
            headingLayout->addWidget(areas);
            layout->insertWidget(0, heading);
            connect(tabs, &QTabBar::currentChanged, toolbar, [toolbar, tabs] { toolbar->setVisible(tabs->currentIndex() != 3); });
            toolbar->setVisible(tabs->currentIndex() != 3);
      }
#endif
      _status = new QLabel(this); _status->setWordWrap(true); layout->addWidget(_status);
      _refreshTimer.setSingleShot(true);
      connect(&_refreshTimer, &QTimer::timeout, this, &PerformanceEditor::refresh);
      _commitTimer.setSingleShot(true);
      connect(&_commitTimer, &QTimer::timeout, this, &PerformanceEditor::applyPending);
      connect(_scroll, &QScrollBar::valueChanged, this, [this] { if (!_automaticScroll) pauseFollow(); invalidateVisual(); });
      for (auto box : {_scope, _voice}) connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { cancelGesture(); rebuildFilter(); });
      connect(_parameter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { QSignalBlocker blocker(_tool); _tool->setCurrentIndex(0); });
      for (auto box : {_axis, _parameter, _tool}) connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            _visibleScoreRect=QRect();finishWheel(); cancelGesture(); _selectedCurve = nullptr; _curveIndex = -1;
            const bool velocity = _parameter->currentIndex() == 0;
            _pedalGrid->setVisible(_parameter->currentIndex() == 2);
            _pedalTool->setVisible(_parameter->currentIndex() == 2); _pedalDelete->setVisible(_parameter->currentIndex() == 2);
            _tool->setVisible(_parameter->currentIndex() != 2); _tool->setEnabled(_parameter->currentIndex() != 2); syncPedalSelection();
            _tool->setItemText(0, _parameter->currentIndex() == 1 ? tr("节点 · 点按添加") : tr("拖动"));
            _parameterHint->setText(_parameter->currentIndex() == 1 ? tr("节点：空白点按添加，上下拖动改 BPM。rit.：拖首尾圆点或曲率菱形，右键解除关联后可自由画速度。时间尺定位播放。")
                  : (_parameter->currentIndex() == 2 ? tr("踏板按乐器共享。向右三角为踩下，向左三角为抬起；拖两端改时机，拖内部移动整段。可画踩下段、抬起间隙或分开。时间尺定位播放。") : QString()));
            _parameterHint->hide();
            _parameter->setToolTip(_parameterHint->text()); _tool->setToolTip(_parameterHint->text()); _ruler->setToolTip(_parameterHint->text());
            _axis->setEnabled(velocity); _axis->setVisible(velocity);
            for (const char* name : {"performanceConvertRelative", "performanceConvertAbsolute"}) {
                  auto button = findChild<QPushButton*>(name); button->setEnabled(velocity); button->setVisible(velocity);
                  }
            findChild<QPushButton*>("performanceWriteValue")->setEnabled(_parameter->currentIndex() != 2);
            findChild<QPushButton*>("performanceWriteValue")->setVisible(_parameter->currentIndex() != 2);
            findChild<QPushButton*>("performanceWriteValue")->setText(velocity ? tr("写入选中") : tr("写入速度节点"));
            _number->setEnabled(_parameter->currentIndex() != 2);
            _number->setVisible(_parameter->currentIndex() != 2);
            _number->setSuffix(velocity && rangeKind()==1 ? " %" : QString());
            _number->setPrefix(_parameter->currentIndex() == 1 ? "BPM " : QString());
            _number->setRange(velocity ? (rangeKind()==1 ? NoteVelocity::minOffset : 1) : 5,
                  velocity ? (rangeKind()==1 ? NoteVelocity::maxOffset : 127) : 999);
            _number->setDecimals(velocity ? 0 : 2);
            if (this->layout()) this->layout()->invalidate(); updateSelection(); status(); invalidateVisual(); if (_view) _view->update();
            });
      for (auto box : {_handles, _band}) connect(box, &QCheckBox::toggled, this, [this] { cancelGesture(); syncOverlayTracking(); if (_view) _view->update(); });
      connect(_band, &QCheckBox::toggled, this, [this](bool enabled) { emit scoreBandChanged(enabled); scheduleRefresh(); syncTransport(); });
      if (seq) {
            connect(seq, &Seq::stopped, this, [this] { cancelSurfaceGesture(); cancelGesture(); cancelComparisonDrag(); if (_snapshotDegraded) { _dirty = true; scheduleRefresh(); } _commitTimer.start(0); syncTransport(); });
            connect(seq, &Seq::started, this, [this] { cancelSurfaceGesture(); cancelGesture(); cancelComparisonDrag(); _following = true; _followButton->setChecked(true); syncTransport(); status(); });
            connect(seq, &Seq::heartBeat, this, [this](int, int, int) { syncTransport(); playbackNotes(); });
            }
      if (mscore) connect(mscore, &MuseScore::scoreStateChanged, this, [this](ScoreState state) {
            if (state == STATE_PLAY) { cancelSurfaceGesture(); cancelGesture(); cancelComparisonDrag(); } syncPedalSelection();
            });
      _playTimer.setParent(this); _playTimer.setObjectName("performancePlaybackTimer");
      _playTimer.setInterval(16); _playTimer.setTimerType(Qt::PreciseTimer);
      connect(&_playTimer, &QTimer::timeout, this, &PerformanceEditor::updatePlayhead);
      _moveTimer.setSingleShot(true); _moveTimer.setInterval(16);
      connect(&_moveTimer, &QTimer::timeout, this, &PerformanceEditor::drainGesture);
      setupUsabilityControls();setupExpressionComparison();
      applyAppearance(); status();
      }

void PerformanceEditor::resizeEvent(QResizeEvent* event)
      {
      QWidget::resizeEvent(event);
      // Layouts handle wrapping. A window resize preserves the split areas.
      }

PerformanceEditor::~PerformanceEditor()
      {
      setProperty("kumoEditorClosing",true);
#ifdef SCRIPT_INTERFACE
      if(_rollWriting)_rollWriting->shutdown();
#endif
      releaseEditorAreas(this);
      invalidateOverlay();
      if (_view && _trackingOwned) _view->setMouseTracking(_trackingBefore || _view->noteEntryMode() || _view->fotoMode());
      finishWheel(false); editors.removeAll(this);
      if (_score) _score->removeViewer(this);
      }

void PerformanceEditor::setView(ScoreView* view)
      {
      if(view!=_view || (view&&view->score()!=_score)){_wheelFeedbackTimer.stop();_wheelFeedbackSurface=nullptr;}
      if (_view == view && _score == (view ? view->score() : nullptr)) return;
      Score* next = view ? view->score() : nullptr;
      if (_score && _score != next && hasPending()) flushPending();
      finishWheel(false); _wheelFeedbackTimer.stop();_wheelFeedbackSurface=nullptr; cancelGesture();
      if (_score) _viewMemory.insert(_score, {_viewport, _scroll->value()});
      _playTimer.stop(); _playingNotes.clear(); _linkedIndex.clear(); cancelSurfaceGesture();
      for (auto connection : _transportConnections) disconnect(connection); _transportConnections.clear();
      if (_view) { invalidateOverlay(); if (_trackingOwned) _view->setMouseTracking(_trackingBefore || _view->noteEntryMode() || _view->fotoMode()); _trackingOwned = false; _view->removeEventFilter(this); }
      if (_score) _score->removeViewer(this);
      disconnect(_scoreDestroyed);
      _view = view; _score = next; _selectedPedal = _pedalTarget = nullptr; syncPedalSelection();
      _velocityReferences.clear();
      for (auto& boundaries : _pedalBoundaries) boundaries.clear();
      _selectedTempoTick = _tempoPointTick = _unlockedTempo = -1;
      _tempoCurves.clear(); _tempoPlot.clear(); _selectedCurve = nullptr; _curveIndex = -1;
      _peekHeld=false;_disabledTracks.clear();_visibleScoreNotes.clear();_visibleScoreRect=QRect();_usedPitches.clear();_excludedStaves.clear(); rebuildStaffMenu();
      _notes.clear(); _segments.clear(); _tempos.clear(); _pedals.clear(); _noteIndex.clear(); _layoutIndex.clear(); _selectedIndices.clear(); _systemIndices.clear();
      _system = nullptr; _hover = -1; _snapshotDegraded = false; _intervals.clear(); _measures.clear(); invalidateVisual();
      if (_score) {
            _score->addViewer(this);
            _scoreDestroyed = connect(_score, &QObject::destroyed, this, [this] {
                  _playTimer.stop(); _intervals.clear(); _playingNotes.clear(); _linkedIndex.clear(); cancelSurfaceGesture();
                  finishWheel(false);
                  _tempoCurves.clear(); _tempoPlot.clear(); _selectedCurve = nullptr; _curveIndex = -1;
                  _score = nullptr; _notes.clear(); _segments.clear(); _tempos.clear(); _pedals.clear(); _velocityReferences.clear();
                  _noteIndex.clear(); _layoutIndex.clear(); _pending.clear(); _propertyPending.clear(); _selectedIndices.clear(); _systemIndices.clear(); _system = nullptr; status(); updateSurfaces();
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
#ifdef SCRIPT_INTERFACE
      if(_spectrum)_spectrum->scoreChanged();
#endif
      }

void PerformanceEditor::scheduleRefresh()
      {
      if (_committing) return;
      if (!_refreshTimer.isActive()) _refreshTimer.start(0);
      }

void PerformanceEditor::refresh()
      {
      if (!_score) { status(); updateSurfaces(); return; }
      if (!surfacesActive()) return;
      // A late-opened panel may copy stable notation while playing, but must not
      // regenerate/read mutable playback events or velocity maps until stopped.
      const bool live = _score->isPlaying() || (seq && seq->isPlaying() && seq->score() == _score->masterScore());
      const bool stablePlayback = !seq || seq->backgroundRenderingIdle();
      if (_snapshotDegraded && stablePlayback) _dirty = true;
      if (live && !_notes.isEmpty() && !(_snapshotDegraded && stablePlayback)) {
            // Layout/selection are GUI data; never regenerate playback events here.
            if (_score->masterScore()->state() == _state) {
                  if (_geometryDirty) refreshLayout();
                  _dirty = false; if (_selectionRefreshNeeded) selectionChanged(); else updateSelection();
                  }
            if (_snapshotDegraded) _refreshTimer.start(20);
            status(); return;
            }
      if (!live && seq && !seq->backgroundRenderingIdle()) { _refreshTimer.start(20); return; }
      const auto state = _score->masterScore()->state();
      if (hasPending() && _state != state) { finishWheel(false); _pending.clear(); _propertyPending.clear(); status(tr("乐谱已被其他操作修改，待提交预览已取消。")); }
      if (_score->nstaves() == 0) return;
      if (!_dirty && _state == state) {
            if (_geometryDirty) {
                  refreshLayout();
                  }
            if (_selectionRefreshNeeded) selectionChanged(); else updateSelection(); return;
            }
      if (_dragging) cancelGesture();
      cancelSurfaceGesture();
      _notes.clear(); _segments.clear(); _tempos.clear(); _pedals.clear(); _noteIndex.clear(); _layoutIndex.clear(); _intervals.clear(); _linkedIndex.clear();
      for (auto& boundaries : _pedalBoundaries) boundaries.clear();
      QElapsedTimer snapshotTimer; snapshotTimer.start();
      if (!live) { _score->createPlayEvents(); _score->updateVelo(); }
      _snapshotDegraded = !stablePlayback;
      const int track = selectionTrack(_score, _contextTrack);
      _contextTrack = track;
      const int globalMethod = mscore ? mscore->synthesizerState().method() : 1;
      const auto markNames=readMarkVelocityText(_score);
      _velocityReferences.clear();
      if (stablePlayback) {
            // Copy numeric maps once with the note snapshot. Painting reads the
            // whole staff's dynamics, including rests and the middle of a tie.
            for (int staff = 0; staff < _score->nstaves(); ++staff)
                  _velocityReferences.append({_score->staff(staff)->velocities(), markNames.value(staff)});
            }
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
                        _tempos.append({text, tick, text->restoreMode() ? _score->tempo(text->tick()) * 60 : text->tempo() * 60.0, text->visible()});
                        _tempoMax = qMax(_tempoMax, text->tempo() * 60.0);
                        }
            for (int t = 0; t < _score->nstaves() * VOICES; ++t) {
                  Element* element = segment->element(t);
                  if (!element || !element->isChordRest()) continue;
                  auto cr = toChordRest(element);
                  _pedalBoundaries[0][_score->staff(t / VOICES)->part()].insert(tick); _pedalBoundaries[1][_score->staff(t / VOICES)->part()].insert((cr->tick() + cr->actualTicks()).ticks());
                  if (!element->isChord()) continue;
                  auto addChord = [this, tick, globalMethod, live, stablePlayback,&markNames](Chord* chord) {
                        for (Note* note : chord->notes()) {
                              const int index = _notes.size();
                              _noteIndex.insert(note, index);
                              const bool audible = note->play() && !note->hidden() && (live ? !note->tieBack() : (!note->tieBack() || note->playEvents().size() != 1))
                                    && (live ? !chord->isGrace() : (!chord->isGrace() || !graceNotesMerged(toChord(chord->parent()))));
                              _notes.append({note, tick, chord->endTick().ticks(), note->track(), note->ppitch(),
                                    stablePlayback ? NoteVelocity::referenceBase(note, globalMethod) : -1, note->veloOffset(), 0, 0, note->veloType(),
                                    note->selected(), audible, note->canvasBoundingRect(), chord->measure()->system()});
                              auto& cached = _notes.back(); cached.baseMin = cached.baseMax = cached.base;cached.dynamicText=markVelocityAt(markNames,note->track(),tick);cached.markBase=stablePlayback?note->staff()->velocities().val(Fraction::fromTicks(tick)):-1;
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
                              if (!live) cacheGeneratedVelocity(cached,globalMethod);
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
      if (_selectedPedal && std::none_of(_pedals.cbegin(), _pedals.cend(), [this](const PedalInfo& p) { return p.pedal == _selectedPedal; })) _selectedPedal = nullptr;
      syncPedalSelection();
      _endTick = _score->lastMeasure() ? _score->lastMeasure()->endTick().ticks() : 1;
      if (!_segments.isEmpty()) {
            auto measure = _score->lastMeasure();
            _segments.append({_endTick, measure->canvasPos() + QPointF(measure->width(), 0), measure->system(), _segments.back().bpm});
            }
      refreshTempoCurves();
      _hover = -1;
      _state = state; _dirty = false; _geometryDirty = false;
      rebuildScoreNoteRows(); rebuildFilter(); invalidateVisual();
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
      _selectedIndices.clear();
      System* system = nullptr;
      const auto focus = _score ? _score->inputState().cr() : nullptr;
      for (auto& note : _notes) {
            note.selected = note.note->selected();
            if (note.selected && noteEditable(note)) _selectedIndices.append(int(&note - _notes.data()));
            if (note.selected && (!system || note.note->chord() == focus)) system = note.system;
            }
      if (!system && _score) for (auto element : _score->selection().elements())
            if (element->isChordRest()) { system = toChordRest(element)->measure()->system(); break; }
      if (!system && _hover >= 0 && _hover < _notes.size()) system = _notes[_hover].system;
      if (!system && _layoutIndex.contains(_system)) system = _system;
      if (!system && !_notes.isEmpty()) system = _notes.front().system;
      if (seq && seq->isPlaying() && seq->score() == _score->masterScore()) system = systemAtTick(_playTick);
      if (system != _system || _systemSegments.isEmpty()) setOverlaySystem(system);
      if (_parameter->currentIndex() == 0 && !_selectedIndices.isEmpty()) {
            QSignalBlocker blocker(_number); _number->setValue(noteValue(_selectedIndices.front()));
            }
      findChild<QPushButton*>("performanceConvertRelative")->setEnabled(_parameter->currentIndex() == 0 && !_snapshotDegraded);
      findChild<QPushButton*>("performanceConvertAbsolute")->setEnabled(_parameter->currentIndex() == 0 && !_snapshotDegraded);
      syncExpressionComparison();updateSurfaces(); status();
#ifdef SCRIPT_INTERFACE
      if (_spectrum && _spectrum->isVisible()) _spectrum->updateInputTarget();
#endif
      if(_workflowPanel&&_workflowPanel->isVisible()&&!_selectedIndices.isEmpty()&&workflowContext()->current(_score)){
            auto selected=_notes[_selectedIndices.front()];auto data=workflowContext()->expression();auto matches=data.value("result").toMap().value("displayMatches").toList();
            auto it=std::lower_bound(matches.cbegin(),matches.cend(),selected.tick,[](const QVariant& v,int tick){return v.toMap().value("tick").toInt()<tick;});
            for(;it!=matches.cend()&&it->toMap().value("tick").toInt()==selected.tick;++it){auto m=it->toMap();const auto& notes=selected.note->chord()->notes();int index=m.value("index").toInt();if(m.value("track").toInt()==selected.track&&index>=0&&index<int(notes.size())&&notes[size_t(index)]==selected.note){if(data.value("selectedScoreId")!=m.value("scoreId"))workflowContext()->expressionAction("select",m.value("scoreId").toString(),m.value("midiId").toString());break;}}
            }
      if (_view && (_handles->isChecked() || _band->isChecked())) {
            _view->update();
            }
      }

void PerformanceEditor::refreshLayout()
      {
      _visibleScoreNotes.clear();_visibleScoreRect=QRect(); _layoutIndex.clear();
      for (auto& note : _notes) { note.bounds = note.note->canvasBoundingRect(); note.system = note.note->chord()->measure()->system(); if (note.system) _layoutIndex.insert(note.system); }
      for (auto& segment : _segments) if (segment.segment) { segment.position = segment.segment->canvasPos(); segment.system = segment.segment->measure()->system(); if (segment.system) _layoutIndex.insert(segment.system); }
      if (!_segments.isEmpty() && !_segments.back().segment && _score->lastMeasure()) { auto measure = _score->lastMeasure(); _segments.back().system = measure->system(); _segments.back().position = measure->canvasPos() + QPointF(measure->width(), 0); }
      _geometryDirty = false; rebuildScoreNoteRows(); invalidateVisual();
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
      _systemTop = _systemBottom = _previousSystemBottom = _nextSystemTop = 0;
      _systemSegments.clear();
      if (!_system || !_system->firstMeasure() || !_system->lastMeasure()) return;
      const int from = _system->firstMeasure()->tick().ticks(), until = _system->lastMeasure()->endTick().ticks();
      auto note = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int tick) { return n.tick < tick; });
      for (; note != _notes.cend() && note->tick < until; ++note) if (note->system == _system) _systemIndices.append(int(note - _notes.cbegin()));
      auto segment = std::lower_bound(_segments.cbegin(), _segments.cend(), from, [](const SegmentInfo& s, int tick) { return s.tick < tick; });
      for (; segment != _segments.cend() && segment->tick <= until; ++segment) if (segment->system == _system) _systemSegments.append(*segment);
      if (_system && !_systemSegments.isEmpty()) {
            // Cache native ink clearance only on row/layout changes, not at 60 Hz.
            const auto inkBounds = [](System* system) {
                  QPair<double,double> bounds;bool first = true;
                  for (auto staff : *system->staves()) if (staff->show()) {
                        const double north = staff->skyline().north().valid() ? staff->y() + staff->skyline().north().max() : staff->bbox().top();
                        const double top = system->canvasPos().y() + qMin(staff->bbox().top(), north);
                        const double bottom = system->canvasPos().y() + qMax(staff->bbox().bottom(), staff->y() + staff->yBottom());
                        bounds.first = first ? top : qMin(bounds.first, top);
                        bounds.second = first ? bottom : qMax(bounds.second, bottom);first = false;
                        }
                  return bounds;
                  };
            const auto ownBounds = inkBounds(_system);
            _systemTop = ownBounds.first;_systemBottom = ownBounds.second;
            auto first = _system->firstMeasure();
            if (first && first->prevMeasure()) {
                  auto previous = first->prevMeasure()->system();
                  if (previous && previous != _system)_previousSystemBottom = inkBounds(previous).second;
                  }
            auto last = _system->lastMeasure();
            if (last && last->nextMeasure()) {
                  auto next = last->nextMeasure()->system();
                  if (next && next != _system)_nextSystemTop = inkBounds(next).first;
                  }
            if (last && _systemSegments.back().tick < last->endTick().ticks())
                  _systemSegments.append({last->endTick().ticks(), last->canvasPos() + QPointF(last->width(), 0), _system, _systemSegments.back().bpm});
            }
      }

void PerformanceEditor::selectionChanged()
      {
      if (_dragging || _selecting) return;
      if (!surfacesActive()) { _selectionRefreshNeeded = true; return; }
      const int track = _score ? selectionTrack(_score, _contextTrack) : _contextTrack;
      if (!_selectionRefreshNeeded && track == _contextTrack) { updateSelection(); return; }
      _contextTrack = track; rebuildFilter();
      }

void PerformanceEditor::layoutChanged()
      {
      if (!_committing) cancelGesture();
      _geometryDirty = true; scheduleRefresh();
      }

void PerformanceEditor::onElementDestruction(Element* element)
      {
      _propertyPending.erase(std::remove_if(_propertyPending.begin(), _propertyPending.end(), [element](const PropertyDraft& draft) { return draft.element == element; }), _propertyPending.end());
      // Destruction callbacks must never call virtual methods on the dying Element.
      if (_layoutIndex.remove(element)) {
            cancelGesture(); _system = nullptr; _systemSegments.clear(); _geometryDirty = true;
            for (auto& note : _notes) if (note.system == element) note.system = nullptr;
            for (auto& segment : _segments) if (segment.system == element) segment.system = nullptr;
            }
      if (_noteIndex.contains(element)) {
            finishWheel(false); cancelGesture(); cancelSurfaceGesture(); _pending.clear(); _propertyPending.clear(); _playingNotes.clear(); _notes.clear(); _intervals.clear(); _linkedIndex.clear(); invalidateVisual(); _noteIndex.clear(); _layoutIndex.clear(); _selectedIndices.clear(); _systemIndices.clear(); _system = nullptr;
            _segments.clear(); _systemSegments.clear();
            }
      bool matched = false;
      for (const auto& tempo : _tempos) if (tempo.text == element) matched = true;
      for (const auto& pedal : _pedals) if (pedal.pedal == element) matched = true;
      if (!matched && !_notes.isEmpty()) return;
      _tempos.erase(std::remove_if(_tempos.begin(), _tempos.end(), [element](const TempoInfo& t) { return t.text == element; }), _tempos.end());
      _pedals.erase(std::remove_if(_pedals.begin(), _pedals.end(), [element](const PedalInfo& p) { return p.pedal == element; }), _pedals.end());
      if (_selectedCurve == element) _selectedCurve = nullptr;
      if (std::any_of(_tempoCurves.cbegin(), _tempoCurves.cend(), [element](const TempoCurveInfo& curve) { return curve.line == element; })) {
            cancelGesture(); _tempoCurves.clear(); _tempoPlot.clear(); _dirty = true; scheduleRefresh();
            }
      if (_pedalTarget == element) { cancelGesture(); _pedalTarget = nullptr; }
      if (_selectedPedal == element) { _selectedPedal = nullptr; syncPedalSelection(); }
      _dirty = true; invalidateVisual();
      }

void PerformanceEditor::status(const QString& message)
      {
      if (message.isEmpty() && _score && _parameter->currentIndex() == 2) {
            const int count = int(std::count_if(_pedals.cbegin(), _pedals.cend(), [this](const PedalInfo& p) { return pedalVisible(p); }));
            const QString operation = _pedalTool->currentIndex() == 2 ? tr("水平拖出抬起间隙") : _pedalTool->currentIndex() == 1 ? tr("水平拖动画踩下段")
                  : _pedalTool->currentIndex() == 3 ? tr("在踏板段内点一下分开") : _selectedPedal ? tr("已选踏板：拖两端改时机，拖内部移动，Delete删除") : tr("点三角选端点，拖段内移动；空白处可画踏板");
            _status->setText(_dirty ? tr("正在读取踏板…") : tr("延音踏板 · 当前%1段 · %2 · Ctrl+Z撤销").arg(count).arg(operation));
            return;
            }
      const QString mode = _parameter->currentIndex() == 1 ? tr("四分音符 BPM · 实际速度阶梯")
            : (_parameter->currentIndex() == 2 ? tr("原生延音踏板 · CC64 开/关")
                  : (rangeKind()==1 ? tr("在原力度上增减百分比") : tr("MIDI 力度 1–127")));
      const QString baseline = _snapshotDegraded ? tr(" · 正在读取播放力度") : QString();
      const QString pending = hasPending() ? tr(" · %1 音、%2 参数预览中；原生属性尚未提交").arg(_pending.size()).arg(_propertyPending.size()) : QString();
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
      if (note.base < 0) return rangeKind()==1 ? edit.raw : qBound(1, edit.raw, 127);
      if (!_axis->currentIndex()) return NoteVelocity::effective(note.base, edit.type, edit.raw);
      return edit.type == Note::ValueType::OFFSET_VAL ? edit.raw : NoteVelocity::offsetFor(note.base, edit.raw);
      }

void PerformanceEditor::setNoteValue(int index, double value, bool report)
      {
      const auto& note = _notes[index];
      if (!noteEditable(note)) return;
      const auto type = _pending.value(note.note, {note.type, note.raw}).type;
      const bool relative=_axis->currentIndex()!=0;
      const int raw = relative ? qBound(NoteVelocity::minOffset, qRound(value), NoteVelocity::maxOffset) : qBound(1, qRound(value), 127);
      if (note.base < 0 && (relative ? type != Note::ValueType::OFFSET_VAL : type != Note::ValueType::USER_VAL)) return;
      const int stored = note.base < 0 ? raw : (type == Note::ValueType::OFFSET_VAL
            ? (relative ? raw : NoteVelocity::offsetFor(note.base, raw))
            : (relative ? NoteVelocity::effective(note.base, Note::ValueType::OFFSET_VAL, raw) : raw));
      if (type == note.type && stored == note.raw) _pending.remove(note.note);
      else _pending.insert(note.note, {type, stored});
      if (!report) return;
      if (note.base < 0) { status(tr("预览 %1%2 · 实际 MIDI 原力度需停播读取；原生属性尚未提交。 ").arg(stored).arg(type == Note::ValueType::OFFSET_VAL ? " %" : " MIDI")); return; }
      const int actual = NoteVelocity::effective(note.base, type, stored);
      const auto actualRange=generatedVelocityRange(note,type,stored);
      const QString range = actualRange.first != actualRange.second || !note.generatedVelocities.isEmpty()
            ? tr(" · 实际发声 MIDI %1–%2").arg(actualRange.first).arg(actualRange.second) : QString();
      status(tr("音高 %1 · 原力度 %2 · 保存 %3%4 · MIDI %5%6")
            .arg(note.pitch).arg(note.base).arg(stored).arg(type == Note::ValueType::OFFSET_VAL ? "%" : "")
            .arg(actual).arg((!_axis->currentIndex() && actual != raw ? tr("（整数百分比舍入：目标 %1）").arg(raw) : QString()) + range));
      }

void PerformanceEditor::convertSelected(Note::ValueType type)
      {
      finishWheel(); if (_dirty) refresh(); pauseFollow();
      if (_snapshotDegraded) { status(tr("模式转换需要停播后建立原力度。")); return; }
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
      if(comparisonWaiting()){status(tr("正在更新拟采用效果…"));return;}
      if(comparisonActive()&&_parameter->currentIndex()==0){QVariantList edits;for(int index:selectedNotes())if(_comparisonValues.contains(index))edits.append(QVariantMap{{"id",_comparisonValues.value(index).value("id")},{"target",rangeKind()==1?NoteVelocity::effective(_notes[index].base,Note::ValueType::OFFSET_VAL,qRound(value)):qBound(1,qRound(value),127)}});if(!edits.isEmpty())workflowContext()->expressionAction("velocityTargets",QString(),QString::fromUtf8(QJsonDocument::fromVariant(edits).toJson(QJsonDocument::Compact)));return;}
      finishWheel(); if (_dirty) refresh(); pauseFollow();
      if (_parameter->currentIndex() == 1 && setTempoCurveNumber(value)) return;
      if (_parameter->currentIndex() == 1 && _selectedTempoTick >= 0) {
            QString error;
            _committing = true;
            ParameterEdit::tempos(_score, {{_selectedTempoTick, value}}, _unlockedTempo, &error);
            _committing = false; _dirty = true; scheduleRefresh(); status(error); return;
            }
      if (_parameter->currentIndex() != 0) { status(tr("先单击速度节点再写入 BPM；踏板请拖动端点。")); return; }
      for (int index : selectedNotes()) setNoteValue(index, value);
      applyPending(); updateSurfaces(); if (_view) _view->update();
      }

void PerformanceEditor::applyPending()
      {
      if (_dragging || _wheelIndex >= 0 || !_score || !hasPending()) { status(); return; }
      if (_score->isPlaying() || (seq && seq->isPlaying())) { _commitTimer.start(100); status(); return; }
      if (seq && !seq->backgroundRenderingIdle()) { _commitTimer.start(20); return; }
      if (_score->masterScore()->state() != _state) {
            _pending.clear(); _propertyPending.clear(); _dirty = true; scheduleRefresh();
            status(tr("乐谱已被其他操作修改，待提交预览已取消。")); return;
            }
      const auto edits = _pending;
      if (!_propertyPending.isEmpty()) {
            auto properties = _propertyPending; _propertyPending.clear();
            _committing = true;
            _score->startCmd();
            for (const auto& property : properties) property.element->undoChangeProperty(property.pid, property.value);
            _score->endCmd();
            _committing = false; _state = _score->masterScore()->state(); _dirty = true;
            if (edits.isEmpty()) { scheduleRefresh(); status(); return; }
            }
      const int undoIndex = _score->undoStack()->getCurIdx();
      _committing = true;
      const bool changed = ParameterEdit::velocities(_score, edits);
      _pending.clear(); _propertyPending.clear(); _committing = false;
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

void PerformanceEditor::queueProperty(Element* element, Pid pid, const QVariant& value)
      {
      if (!element || element->score() != _score || _score->readOnly()) return;
      if (!hasPending()) _state = _score->masterScore()->state();
      for (auto& draft : _propertyPending) if (draft.element == element && draft.pid == pid) {
            draft.value = value; _commitTimer.start(20); status(tr("播放参数将在停播后提交。")); return;
            }
      _propertyPending.append({element, pid, value});
      _commitTimer.start(20); status(tr("播放参数将在停播后提交。"));
      }

void PerformanceEditor::flushForScore(Score* score)
      {
      for (auto editor : editors) if (editor->_score && score && editor->_score->masterScore() == score->masterScore()) editor->flushPending();
      }

void PerformanceEditor::fit(bool selection)
      {
      int from = 0, until = comparisonTimeActive()?qMax(_endTick,_comparisonEnd):_endTick;
      if (selection) {
            const auto chosen = selectedNotes();
            if (chosen.isEmpty()) return;
            from = _notes[chosen.front()].tick; until = from + 1;
            for (int index : chosen) { from = qMin(from, _notes[index].tick); until = qMax(until, _notes[index].end);if(comparisonTimeActive()&&_comparisonValues.contains(index)){from=qMin(from,comparisonDisplayTick(index));until=qMax(until,_score->tempomap()->time2tick(_comparisonClock.tick2time(_notes[index].end)));} }
            }
      const double width=qMax(100,_canvas->width()-PerformanceViewport::gutter-PerformanceViewport::rightMargin);
      const double units=comparisonTimeActive()?qMax(.001,2*(_score->tempomap()->tick2time(until)-_score->tempomap()->tick2time(from))):double(qMax(1,until-from))/DIVISION;
      const double previousScale = _viewport.pixelsPerQuarter;
      _viewport.pixelsPerQuarter=qBound(.01,width/units,1000.);
#ifdef SCRIPT_INTERFACE
      if (_spectrum) _spectrum->editorTimelineZoomed(previousScale, PerformanceViewport::gutter);
#else
      Q_UNUSED(previousScale);
#endif
      _scroll->setRange(0, qMax(0, (comparisonTimeActive()?qMax(_endTick,_comparisonEnd):_endTick) - (until - from))); _scroll->setPageStep(until - from); _scroll->setValue(from);
      pauseFollow(); invalidateVisual();
      }

bool PerformanceEditor::overlayAllowed() const
      {
      if (!personalFeaturesEnabled()) return false;
      return surfacesActive() && _view && _score && !_score->printing() && !_view->fotoMode()
            && !_view->editMode() && (!_view->noteEntryMode() || _closestWheelActive || _wheelIndex >= 0 || _wheelFeedbackTimer.isActive());
      }
bool PerformanceEditor::surfacesActive() const
      { return personalFeaturesEnabled() && (isVisible() || detachedEditorAreaVisible(this) || ((_band->isChecked()||_peekHeld||_showValues||_closestWheelActive||(_wheelIndex>=0&&_wheelSurface==_view)||(_wheelFeedbackTimer.isActive()&&_wheelFeedbackSurface==_view)) && _view && _view->isVisible())); }
bool PerformanceEditor::scoreBandEnabled() const
      { return _band->isChecked(); }
void PerformanceEditor::setScoreBandEnabled(bool enabled)
      { _band->setChecked(enabled); }

QRectF PerformanceEditor::laneRect() const { return QRectF(PerformanceViewport::gutter, PerformanceViewport::parameterPadding, qMax(10, _canvas->width() - PerformanceViewport::gutter - PerformanceViewport::rightMargin), qMax(30, _canvas->height() - 2 * PerformanceViewport::parameterPadding)); }

double PerformanceEditor::xForNote(int index, bool onScore) const
      {
      // Note bounds are the laid-out head/fret, including mirroring, grace notes,
      // cross-staff displacement and user offsets. Time automation stays on ticks.
      return onScore && _view ? _view->matrix().map(_notes[index].bounds.center()).x() : xForTick(_notes[index].tick, false);
      }

double PerformanceEditor::xForTick(int tick, bool onScore) const
      {
      if (!onScore) {
            const double units=comparisonTimeActive()?2*(_score->tempomap()->tick2time(tick)-_score->tempomap()->tick2time(_scroll->value())):double(tick-_scroll->value())/DIVISION;
            return PerformanceViewport::gutter+units*_viewport.pixelsPerQuarter;
            }
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
      if (!onScore) {
            const double units=(x-PerformanceViewport::gutter)/_viewport.pixelsPerQuarter;
            const int tick=comparisonTimeActive()?_score->tempomap()->time2tick(_score->tempomap()->tick2time(_scroll->value())+units*.5):qRound(units*DIVISION)+_scroll->value();
            return qBound(0,tick,comparisonTimeActive()?qMax(_endTick,_comparisonEnd):_endTick);
            }
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
      const int mode = _pedalGrid->currentIndex();
      if (mode < 5) {
            if (mode == 4) return qBound(0, tick, _endTick);
            const int step = DIVISION / (4 << mode);
            auto m = std::upper_bound(_measures.cbegin(), _measures.cend(), tick, [](int t, const MeasureInfo& info) { return t < info.from; });
            const int start = m == _measures.cbegin() ? 0 : (m - 1)->from;
            return qBound(start, start + qRound(double(tick - start) / step) * step, m == _measures.cbegin() ? _endTick : (m - 1)->until);
            }
      const auto& grid = end ? _pedalEnds : _pedalStarts;
      if (grid.isEmpty()) return 0;
      auto next = std::lower_bound(grid.cbegin(), grid.cend(), tick);
      if (next == grid.cend()) return grid.back();
      if (next == grid.cbegin()) return *next;
      const auto previous = next - 1;
      return tick - *previous < *next - tick ? *previous : *next;
      }

double PerformanceEditor::valueForY(double y, const QRectF& rect, bool onScore) const
      {
      const auto range = (onScore ? _scoreViewport : _viewport).ranges[rangeKind()];
      return qBound(range.minimum, range.minimum + (rect.bottom() - y) * (range.maximum - range.minimum) / rect.height(), range.maximum);
      }
double PerformanceEditor::yForValue(double value, const QRectF& rect, bool onScore) const
      {
      const auto range = (onScore ? _scoreViewport : _viewport).ranges[rangeKind()];
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
      _noteGesture = false; _cycleOnClick = false; _gestureMoved=false; _pressedCandidates.clear(); _scoreGesture = onScore; _dragging = true; _anchor = forcedAnchor; _offsetDrag = false; _unlockedTempo = -1;
      if (_parameter->currentIndex() == 0) {
            if(_tool->currentIndex()==0){_pressedCandidates=hits(point,true,onScore);if(forcedAnchor<0&&!_pressedCandidates.isEmpty())_anchor=_pressedCandidates.front();
                  _cycleOnClick=_anchor>=0&&_notes[_anchor].selected&&_selectedIndices.size()==1&&_pressedCandidates.size()>1&&std::abs(velocityY(_pressedCandidates[1],rect,onScore)-velocityY(_anchor,rect,onScore))<3;
                  }
            double distance = _anchor>=0 ? -1 : 13;
            const int from = tickForX(rect.left() - 32, onScore), until = tickForX(rect.right() + 32, onScore);
            auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
            const int begin = onScore ? 0 : int(first - _notes.cbegin());
            const int end = onScore ? _systemIndices.size() : int(std::upper_bound(first, _notes.cend(), until, [](int t, const NoteInfo& n) { return t < n.tick; }) - _notes.cbegin());
            for (int position = begin; forcedAnchor < 0 && position < end; ++position) {
                  const int i = onScore ? _systemIndices[position] : position;
                  const auto& note = _notes[i];
                  if (!noteEditable(note)) continue;
                  const QPointF center(xForNote(i, onScore), velocityY(i, rect, onScore));
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
            if (beginTempoCurve(point, rect, onScore)) { if (!_dragging) return; }
            _tempoPointTick = nearestSegment(tickForX(point.x(), onScore));
            for (const auto& tempo : _tempos)
                  if (QLineF(point, QPointF(xForTick(tempo.tick, onScore), yForValue(tempo.bpm, rect, onScore))).length() < 10) { _unlockedTempo = tempo.tick; _tempoOriginalBpm = tempo.bpm; }
            }
      else if (!beginPedalGesture(point, rect, onScore)) { _dragging = false; return; }
      if (_parameter->currentIndex() == 2) {
            _pedalTrack = _pedalTarget ? _pedalTarget->track() : (_contextTrack / VOICES) * VOICES;
            _pedalStarts.clear(); _pedalEnds.clear();
            if (!_pedalVisibleParts.contains(_score->staff(_pedalTrack / VOICES)->part())) { _dragging = false; status(tr("当前谱表已被过滤；请启用它或在谱面选中目标谱表。")); return; }
            _pedalStarts = _pedalBoundaries[0].value(_score->staff(_pedalTrack / VOICES)->part()).values().toVector();
            _pedalEnds = _pedalBoundaries[1].value(_score->staff(_pedalTrack / VOICES)->part()).values().toVector();
            for (auto grid : {&_pedalStarts, &_pedalEnds}) {
                  std::sort(grid->begin(), grid->end()); grid->erase(std::unique(grid->begin(), grid->end()), grid->end());
                  }
            if (!_pedalTarget || _pedalTool->currentIndex() == 3) { _pedalFrom = snap(_pedalFrom); _pedalUntil = _pedalTool->currentIndex() == 3 ? _pedalFrom : snap(_pedalUntil, true); }
            }
      if (_parameter->currentIndex() == 1 && _curveIndex < 0) {
            _selectedTempoTick = _unlockedTempo >= 0 ? _unlockedTempo : _tempoPointTick;
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
      const bool moved=QLineF(_press,point).length()>=qMin(4,QApplication::startDragDistance());
      if(!_gestureMoved&&!moved&&_offsetDrag)return;
      _gestureMoved|=moved;
      QElapsedTimer gestureTimer; gestureTimer.start();
      if (_curveIndex >= 0) moveTempoCurve(point);
      else if (_parameter->currentIndex() == 2) {
            movePedalGesture(point);
            }
      else if (_offsetDrag) {
            const auto range = (_scoreGesture ? _scoreViewport : _viewport).ranges[rangeKind()];
            const auto valueLane=(_anchor>=0&&!_noteGesture)?velocityLane(_anchor,_gestureLane,_scoreGesture):_gestureLane;
            if(valueLane.isEmpty())return;
            const double delta = (_noteGesture ? (_press.y() - point.y()) * (range.maximum - range.minimum) / qMax(1.0, _gestureLane.height()) : valueForY(point.y(), valueLane, _scoreGesture) - valueForY(_press.y(), valueLane, _scoreGesture))*_dragSensitivity;
            for (int index : _dragTargets) {
                  const double scale=_groupRelative&&rangeKind()==0&&_dragTargets.size()>1&&_anchor>=0&&_notes[_anchor].base>0&&_notes[index].base>0 ? double(_notes[index].base)/_notes[_anchor].base:1.;
                  setNoteValue(index, _dragOriginal.value(index) + delta*scale, false);
                  }
            if(_groupRelative&&rangeKind()==0&&_dragTargets.size()>1&&_anchor>=0&&_notes[_anchor].base>0)status(tr("一起调整 %1% · 已选%2音").arg(delta*100/_notes[_anchor].base,0,'f',1).arg(_dragTargets.size()));
            }
      else {
            const bool line = _tool->currentIndex() == 2;
            const QPointF start = line ? _press : _last;
            if (line) { _pending = _gestureBefore; _tempoDraft.clear(); }
            int from = tickForX(start.x(), _scoreGesture), until = tickForX(point.x(), _scoreGesture);
            double v0 = valueForY(start.y(), _gestureLane, _scoreGesture), v1 = valueForY(point.y(), _gestureLane, _scoreGesture);
            if (until < from) { std::swap(from, until); std::swap(v0, v1); }
            auto value = [from, until, v0, v1](int tick) { return until == from ? v1 : v0 + (v1 - v0) * (tick - from) / (until - from); };
            if (_parameter->currentIndex() == 0) {
                  const bool selected = !_selectedIndices.isEmpty();
                  if (_scoreGesture) {
                        // Draw across the same native head centres used by rendering
                        // and hit testing; displaced notes are not collapsed by tick.
                        const double left = qMin(start.x(), point.x()) - .5, right = qMax(start.x(), point.x()) + .5;
                        for (int i : _systemIndices) {
                              const double x = xForNote(i, true);
                              if ((!selected || _notes[i].selected) && x >= left && x <= right) {
                                    const double fraction = point.x() == start.x() ? 1 : qBound(0.0, (x - start.x()) / (point.x() - start.x()), 1.0);
                                    const double y = start.y() + fraction * (point.y() - start.y());
                                    setNoteValue(i, valueForY(y, _gestureLane, true), false);
                                    }
                              }
                        }
                  else {
                        auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
                        for (auto it = first; it != _notes.cend() && it->tick <= until; ++it)
                              if (!selected || it->selected) {
                                    const int index=int(it-_notes.cbegin());const auto lane=velocityLane(index,_gestureLane,false);if(lane.isEmpty())continue;
                                    if(_velocityArrangement){if(!lane.contains(QPointF(qBound(lane.left(),point.x(),lane.right()),point.y())))continue;const double fraction=until==from?1.:double(it->tick-from)/(until-from);setNoteValue(index,valueForY(start.y()+(point.y()-start.y())*fraction,lane,false),false);}
                                    else setNoteValue(index, value(it->tick), false);
                                    }
                        }
                  }
            else {
                  if (_tool->currentIndex() == 0 && _selectedTempoTick >= 0) {
                        bool locked = false;
                        for (const auto& t : _tempos) if (t.tick == _selectedTempoTick && t.visible && t.tick != _unlockedTempo) locked = true;
                        if (!locked) _tempoDraft[_selectedTempoTick] = _unlockedTempo >= 0
                              ? qBound(5.0, _tempoOriginalBpm + valueForY(point.y(), _gestureLane, _scoreGesture) - valueForY(_press.y(), _gestureLane, _scoreGesture), 999.0) : v1;
                        }
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
      if (_parameter->currentIndex() == 1 && _curveIndex < 0) status(tr("预览 %1 BPM · 松开写入，Esc 取消").arg(_tempoDraft.value(_selectedTempoTick, valueForY(point.y(), _gestureLane, _scoreGesture)), 0, 'f', 2));
      else if (_parameter->currentIndex() == 2) {
            const auto position = [this](int tick) {
                  if (tick >= _endTick) return tr("谱尾");
                  auto after = std::upper_bound(_measures.cbegin(), _measures.cend(), tick, [](int t, const MeasureInfo& m) { return t < m.from; });
                  if (after == _measures.cbegin()) return QString("1.1");
                  const auto& measure = *(after - 1);
                  const int offset = tick - measure.from, sub = offset % measure.beat;
                  QString beat = QString::number(offset / measure.beat + 1);
                  if (sub) { Fraction fraction(sub, measure.beat); fraction.reduce(); beat += "+" + fraction.toString(); }
                  return QString("%1.%2").arg(measure.bar + 1).arg(beat);
                  };
            status(tr("%1 · %2 → %3（小节.拍） · 松开保存，Esc取消")
                  .arg(_pedalTool->currentIndex() == 2 ? tr("抬起间隙") : _pedalTool->currentIndex() == 3 ? tr("分开踏板") : _pedalGrip == 0 ? tr("调整踩下") : _pedalGrip == 1 ? tr("调整抬起") : tr("踏板"))
                  .arg(position(_pedalFrom), position(_pedalUntil)));
            }
      _last = point; updateSurfaces(); if (_view) _view->update();
      }

void PerformanceEditor::finishGesture()
      {
      if (!_dragging) return;
      drainGesture();
      if (!_gestureMoved&&_parameter->currentIndex()==0&&_tool->currentIndex()==0&&_pressedCandidates.size()>1) {
            const auto candidates=_pressedCandidates;const bool cycle=_cycleOnClick;int target=_anchor;double nearest=1e20;
            for(int i:candidates){double distance=QLineF(_press,QPointF(xForNote(i,_scoreGesture),velocityY(i,_gestureLane,_scoreGesture))).length();if(distance<nearest){nearest=distance;target=i;}}
            cancelGesture();if(cycle)cycleHit(candidates);else{selectIndices({target},Qt::NoModifier);updateHover(target);}return;
            }
      _dragging = false; qApp->removeEventFilter(this);
      if (_view) _view->releaseMouse(); _canvas->releaseMouse(); _noteCanvas->releaseMouse();
      if (_parameter->currentIndex() == 0) applyPending();
      else {
            QString error;
            _committing = true;
            bool changed = false;
            if (_curveIndex >= 0) changed = finishTempoCurve(&error);
            else if (_parameter->currentIndex() == 1) {
                  if (_unlockedTempo < 0 && _tool->currentIndex() != 0 && !_tempoDraft.isEmpty()) {
                        const int end = _tempoDraft.lastKey();
                        for (const auto& segment : _segments) if (segment.tick > end && segment.tick < _endTick) {
                              _tempoDraft[segment.tick] = segment.bpm; break;
                              }
                        }
                  changed = ParameterEdit::tempos(_score, _tempoDraft, _unlockedTempo, &error);
                  }
            else {
                  const int tool = _pedalTool->currentIndex();
                  if (tool == 3) changed = ParameterEdit::splitPedal(_score, _pedalTarget, _pedalFrom, &error);
                  else if (_pedalUntil > _pedalFrom && (tool != 0 || !_pedalTarget))
                        changed = ParameterEdit::paintPedal(_score, _pedalTrack, _pedalFrom, _pedalUntil, tool != 2, &error);
                  else if (_pedalTarget)
                        changed = ParameterEdit::pedal(_score, _pedalTrack, _pedalFrom, _pedalUntil, _pedalTarget, &error);
                  }
            _committing = false; _tempoDraft.clear(); if (changed) { _dirty = true; scheduleRefresh(); } else { updateSurfaces(); if (_view) _view->update(); } status(error);
            }
      _gestureBefore.clear();
      }

void PerformanceEditor::cancelGesture()
      {
      if(_bandResizing){_bandHeight=_bandResizeHeight;_bandResizing=false;qApp->removeEventFilter(this);if(_view){_view->releaseMouse();_view->unsetCursor();_view->update();}}
      if (_scoreRangePanning) { _scoreRangePanning = false; qApp->removeEventFilter(this); if (_view) { _view->releaseMouse(); _view->unsetCursor(); } }
      _moveTimer.stop(); _moveQueued = false;
      if (!_dragging) return;
      if (_curveIndex >= 0 && _curveIndex < _tempoCurves.size()) {
            const auto& original = _tempoCurves[_curveIndex]; QSignalBlocker blocker(_number);
            _number->setValue(_curveGrip == 2 ? original.shape : _curveGrip ? original.target : original.start);
            }
      _curveIndex = -1;
      _pending = _gestureBefore; _gestureBefore.clear(); _tempoDraft.clear(); _dragging = false; qApp->removeEventFilter(this);
      if (_view) _view->releaseMouse(); if (_canvas) _canvas->releaseMouse(); if (_noteCanvas) _noteCanvas->releaseMouse();
      }

bool PerformanceEditor::eventFilter(QObject* object, QEvent* event)
      {
      if (!personalFeaturesEnabled()) return false;
      if (pedalKeyEvent(object, event)) return true;
      QWidget* gestureOwner = nullptr;
      if (_comparisonDragging) gestureOwner = _canvas;
      else if (_bandResizing || _scoreRangePanning || (_dragging && _scoreGesture)) gestureOwner = _view;
      else if (_marquee || (_dragging && _noteGesture)) gestureOwner = _noteCanvas;
      else if (_seeking) gestureOwner = _seekingSurface;
      else if (_rangePanning || _dragging) gestureOwner = _canvas;
      if (event->type() == QEvent::ApplicationDeactivate ||
          (event->type() == QEvent::UngrabMouse && object == gestureOwner)) {
            cancelSurfaceGesture(); cancelGesture(); cancelComparisonDrag(); updateSurfaces();
            return false;
            }
#ifdef SCRIPT_INTERFACE
      if(object==_noteCanvas && _spectrum && _spectrum->overlayEvent(event))return true;
#endif
      if(comparisonEvent(object,event))return true;
      if ((_scoreRangePanning||_bandResizing) && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept(); if (event->type() == QEvent::KeyPress) cancelGesture(); return true;
            }
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
      if ((event->type() == QEvent::Show || event->type() == QEvent::Hide) && (object == _canvas || onScore)) {
            cancelGesture();
            QTimer::singleShot(0, this, [this] { syncOverlayTracking(); syncTransport(); scheduleRefresh(); invalidateOverlay(); });
            return false;
            }
      const bool closestWheel=onScore&&event->type()==QEvent::Wheel&&nearestWheelShortcut(static_cast<QWheelEvent*>(event));
      QScopedValueRollback<bool> wheelScope(_closestWheelActive,closestWheel);
      if (onScore && !overlayAllowed()) return false;
      if(onScore && !_dragging && !_bandResizing && !_scoreRangePanning
            && (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseButtonDblClick)) {
            auto mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::LeftButton) {
                  const QPointF logical=_view->toLogical(mouse->pos());
                  auto target=_view->elementNear(logical);
                  if(target && target->isSymbol() && toSymbol(target)->handBracket()
                        && target->contains(logical-(target->canvasPos()-target->pagePos())))return false;
                  }
            }
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
                  for (int position = 0; position < (onScore ? _systemIndices.size() : _notes.size()); ++position) {
                        const int i = onScore ? _systemIndices[position] : position;
                        const auto& note = _notes[i];
                        if (std::abs(xForNote(i, onScore) - mouse->x()) > 22) continue;
                        auto action = menu.addAction(tr("音高 %1 · 谱表 %2 / 声部 %3 · %4%5").arg(note.pitch).arg(note.track / VOICES + 1).arg(note.track % VOICES + 1).arg(noteValue(i))
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
