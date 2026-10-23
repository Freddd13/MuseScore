// SPDX-License-Identifier: GPL-2.0-or-later
#include "rollwriting.h"
#include "libmscore/chord.h"
#include "libmscore/input.h"
#include "libmscore/note.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/staff.h"
#include "libmscore/undo.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "mscore/workflow/arrangement.h"
#include "mscore/workflow/editorareas.h"
#include "mscore/workflow/controlflowlayout.h"
#include "mscore/workflow/workflowcontext.h"
#include "mscore/workflow/featuregate.h"
#include "mscore/workflow/playbackpreview.h"
#include "mscore/workflow/workspaceviews.h"
#include "performanceeditor.h"
#include "rollnotation.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QHideEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtConcurrent>
#include <algorithm>

namespace Ms {
namespace {
QVariantMap json(const QString &text) {
  return QJsonDocument::fromJson(text.toUtf8()).toVariant().toMap();
}
} // namespace
QAction *rollWritingAction() {
  static QPointer<QAction> action;
  if (action)
    return action;
  action = new QAction(QObject::tr("卷帘写音"), qApp);
  action->setIconText(QObject::tr("写音"));
  action->setObjectName("kumo-roll-writing");
  action->setCheckable(true);
  action->setIcon(personalToolIcon("kumo-roll-writing"));
  action->setToolTip(
      QObject::tr("在钢琴卷帘中画音、移动和改长度；可直接写谱或先试草稿。"));
  markPersonalAction(action);
  const QPointer<QAction> created = action;
  QObject::connect(action, &QAction::toggled, qApp, [created](bool enabled) {
    if (!enabled)
      return;
    QTimer::singleShot(0, qApp, [created] {
      if (created && created->isChecked() && personalFeaturesEnabled() &&
          mscore) {
        mscore->showPerformanceEditor(true);
        openEditorArea(mscore->performanceEditor(), "performanceNotesArea");
      }
    });
  });
  return action;
}
RollWritingBar::RollWritingBar(PerformanceEditor *editor, QToolButton *toggle,
                               QWidget *parent)
    : QWidget(parent), _editor(editor), _reader(this), _toggle(toggle) {
  setObjectName("rollWritingBar");
  _intervals.reset(new PerformanceIntervalIndex);
  auto outer = new QVBoxLayout(this);
  outer->setContentsMargins(3, 0, 3, 3);
  outer->setSpacing(3);
  auto host = new QWidget;
  _layout = new ControlFlowLayout(host);
  _layout->setContentsMargins(0, 0, 0, 0);
  _layout->setSpacing(4);
  auto scroll = new ControlScrollArea;
  scroll->setWidget(host);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setMaximumHeight(120);
  scroll->setMinimumHeight(28);
  outer->addWidget(scroll);
  auto combo = [&](const QString &name, const QStringList &items) {
    auto box = new QComboBox;
    box->setObjectName(name);
    box->addItems(items);
    box->setMinimumContentsLength(4);
    box->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    _controls.append(box);
    return box;
  };
  _mode = combo("rollWriteMode", {tr("直接写谱"), tr("草稿")});
  _tool = combo("rollWriteTool", {tr("画音／拖音"), tr("框选"), tr("删除")});
  _grid = combo("rollWriteGrid",
                {tr("四分"), tr("八分"), tr("十六分"), tr("三十二分"),
                 tr("八分三连音"), tr("十六分三连音")});
  _grid->setCurrentIndex(2);
  for (int i = 0; i < 6; ++i)
    _grid->setItemData(i, QList<int>{480, 240, 120, 60, 160, 80}[i]);
  _track = combo("rollWriteTrack", {});
  _track->setMaximumWidth(230);
  auto button = [&](const QString &name, const QString &text, auto action) {
    auto b = new QToolButton;
    b->setObjectName(name);
    b->setText(text);
    connect(b, &QToolButton::clicked, this, action);
    _controls.append(b);
    return b;
  };
  button("rollWriteScope", tr("改用当前选区"), [this] { readScope(); });
  auto locks = button("rollWriteLocks", tr("声部锁定"), [] {});
  _lockMenu = new QMenu(locks);
  locks->setMenu(_lockMenu);
  locks->setPopupMode(QToolButton::InstantPopup);
  connect(_lockMenu, &QMenu::aboutToShow, this, &RollWritingBar::lockMenu);
  _autoVoice = _lockMenu->addAction(tr("交叠音自动选择声部"));
  _autoVoice->setCheckable(true);
  _autoVoice->setChecked(true);
  auto optionsButton = button("rollWriteOptions", tr("写音设置"), [] {});
  auto optionsMenu = new QMenu(optionsButton);
  optionsButton->setMenu(optionsMenu);
  optionsButton->setPopupMode(QToolButton::InstantPopup);
  _precision = optionsMenu->addAction(tr("保留拖动的细小时间差"));
  _precision->setCheckable(true);
  _precision->setChecked(
      QSettings().value("rollWriting/precision", false).toBool());
  connect(_precision, &QAction::toggled, this, [](bool value) {
    QSettings().setValue("rollWriting/precision", value);
  });
  _followDynamics = optionsMenu->addAction(tr("新音跟随力度记号"));
  _followDynamics->setCheckable(true);
  _followDynamics->setChecked(
      QSettings().value("rollWriting/followDynamics", true).toBool());
  connect(_followDynamics, &QAction::toggled, this, [](bool value) {
    QSettings().setValue("rollWriting/followDynamics", value);
  });
  auto velocityRow = new QWidget;
  auto velocityLayout = new QHBoxLayout(velocityRow);
  velocityLayout->setContentsMargins(8, 3, 8, 3);
  velocityLayout->addWidget(new QLabel(tr("新音固定力度")));
  _fixedVelocity = new QSpinBox;
  _fixedVelocity->setObjectName("rollNewVelocity");
  _fixedVelocity->setRange(1, 127);
  _fixedVelocity->setValue(
      QSettings().value("rollWriting/velocity", 80).toInt());
  _fixedVelocity->setEnabled(!_followDynamics->isChecked());
  velocityLayout->addWidget(_fixedVelocity);
  auto velocityAction = new QWidgetAction(optionsMenu);
  velocityAction->setDefaultWidget(velocityRow);
  optionsMenu->addAction(velocityAction);
  connect(
      _fixedVelocity, QOverload<int>::of(&QSpinBox::valueChanged), this,
      [](int value) { QSettings().setValue("rollWriting/velocity", value); });
  connect(_followDynamics, &QAction::toggled, _fixedVelocity,
          [this](bool value) { _fixedVelocity->setEnabled(!value); });
  _precision->setToolTip(tr(
      "记谱对齐网格，播放保留拖动前后的细小时间差；原始位置也保存在草稿中。"));
  button("rollWriteQuantize", tr("量化选音"), [this] { quantizeSelected(); });
  _original =
      button("rollWriteOriginal", tr("谱中效果"), [this] { audition(false); });
  _proposed =
      button("rollWriteProposed", tr("草稿效果"), [this] { audition(true); });
  _stop = button("rollWriteStop", tr("停止"), [this] { stopAudition(); });
  _original->setCheckable(true);
  _proposed->setCheckable(true);
  _apply = button("rollWriteApply", tr("使用草稿"), [this] { applyDraft(); });
  auto drafts = button("rollWriteSaved", tr("草稿…"), [] {});
  _savedMenu = new QMenu(drafts);
  drafts->setMenu(_savedMenu);
  drafts->setPopupMode(QToolButton::InstantPopup);
  connect(_savedMenu, &QMenu::aboutToShow, this, &RollWritingBar::savedMenu);
  button("rollWriteReset", tr("从谱子重新开始"),
         [this] { restoreFromScore(); });
  for (auto widget : _controls) {
    widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    _layout->addWidget(widget);
  }
  _range = new QLabel;
  _range->setObjectName("rollWriteRange");
  _range->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  _range->setWordWrap(true);
  outer->addWidget(_range);
  _message = new QLabel;
  _message->setObjectName("rollWriteStatus");
  _message->setWordWrap(true);
  _message->setMaximumHeight(fontMetrics().lineSpacing() * 2 + 2);
  outer->addWidget(_message);
  connect(workflowContext(), &WorkflowContext::auditionChanged, this,
          &RollWritingBar::updateActions);
  if (seq)
    connect(seq, &Seq::stopped, this, [this] {
      QTimer::singleShot(0, this, [this] {
        if (_enabled && !_ready && !_pages.isActive())
          readScope(_state["scope"].toMap());
        updateActions();
      });
    });
  _pages.setSingleShot(false);
  _pages.setInterval(0);
  connect(&_pages, &QTimer::timeout, this, &RollWritingBar::readPage);
  connect(&_calculation, &QFutureWatcher<QVariantMap>::finished, this,
          &RollWritingBar::calculated);
  connect(_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] {
            stopAudition();
            updateActions();
            if (_enabled)
              readScope(_state.value("scope").toMap());
          });
  connect(_toggle, &QToolButton::toggled, this,
          &RollWritingBar::setWritingEnabled);
  if (_toggle->isChecked())
    QTimer::singleShot(0, this,
                       [this] { setWritingEnabled(_toggle->isChecked()); });
  connect(_grid, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] {
            if (_ready)
              stopAudition();
          });
  connect(_precision, &QAction::toggled, this, [this] {
    if (_ready)
      stopAudition();
  });
  connect(_autoVoice, &QAction::toggled, this, [this] {
    if (_ready)
      stopAudition();
  });
  _mode->setToolTip(
      tr("直接写谱：松开鼠标后更新乐谱。草稿：先比较试听，再点“使用草稿”。"));
  _track->setToolTip(tr("新音写入此声部；拖动已有音时保持它原来的声部。"));
  _grid->setToolTip(
      tr("选择画音和移动使用的网格；“量化选音”整理已选音的起止。"));
  hide();
}
void RollWritingBar::setWritingEnabled(bool enabled) {
  _enabled = enabled;
  if (enabled)
    qApp->installEventFilter(this);
  else
    qApp->removeEventFilter(this);
  setVisible(enabled);
  if (enabled) {
    if (auto tabs = _editor->findChild<QTabBar *>("workflowExpressionTabs"))
      tabs->setCurrentIndex(0);
    _editor->cancelGesture();
    if (_editor->score() && _editor->score()->noteEntryMode() && _editor->_view)
      _editor->_view->cmd("escape");
    readScope();
  } else {
    cancelGesture();
    stopAudition();
    _pages.stop();
    _editor->_noteCanvas->unsetCursor();
    if (_cancel)
      *_cancel = true;
    ++_generation;
  }
  _editor->updateSurfaces();
}
RollWritingBar::~RollWritingBar() {
  shutdown();
  _calculation.waitForFinished();
}
void RollWritingBar::shutdown() {
  if (_closing)
    return;
  _closing = true;
  qApp->removeEventFilter(this);
  _pages.stop();
  if (_cancel)
    *_cancel = true;
  ++_generation;
  cancelGesture();
  stopAudition();
  _enabled = false;
}
void RollWritingBar::setMessage(const QString &text) {
  _message->setText(text);
  _message->setToolTip(text);
}
QString RollWritingBar::scopeId() const {
  const auto s = _state.value("scope").toMap();
  return QString("%1:%2:%3:%4")
      .arg(s["start"].toInt())
      .arg(s["end"].toInt())
      .arg(s["firstTrack"].toInt())
      .arg(s["endTrack"].toInt());
}
bool RollWritingBar::editableTrack(int track) const {
  if (!_score || track < 0 || track >= _score->ntracks())
    return false;
  const int context = qBound(0, _editor->_contextTrack, _score->ntracks() - 1);
  const bool inScope = _editor->_scope->currentIndex() == 2 ||
                       (_editor->_scope->currentIndex() == 0
                            ? _score->staff(track / 4)->part() ==
                                  _score->staff(context / 4)->part()
                            : track / 4 == context / 4);
  if (!inScope || (_editor->_voice->currentIndex() &&
                   track % 4 != _editor->_voice->currentIndex() - 1))
    return false;
  return !_locked.contains(track) && _editor->staffEnabled(track) &&
         !_editor->_disabledTracks.contains(track) &&
         (_editor->_voiceMask & (1 << (track % 4)));
}
QVariantMap RollWritingBar::settings() const {
  QVariantList locks;
  const auto scope = _state["scope"].toMap();
  for (int track = scope["firstTrack"].toInt();
       track < scope["endTrack"].toInt(); ++track)
    if (!editableTrack(track))
      locks.append(track);
  QVariantList manualLocks;
  for (int track : _locked)
    manualLocks.append(track);
  return {{"manualLocks", manualLocks},
          {"followDynamics", _followDynamics->isChecked()},
          {"newVelocity", _fixedVelocity->value()},

          {"quantum", _grid->currentData()},
          {"triplet", _grid->currentIndex() >= 4},
          {"lockedTracks", locks},
          {"suggestVoices", _autoVoice->isChecked()},
          {"preserveTiming", _precision->isChecked()}};
}
QVariantMap RollWritingBar::stored() const {
  return _score ? json(_score->masterScore()->metaTag("kumo.rollWriting"))
                : QVariantMap();
}
QVariantMap RollWritingBar::record(bool draft) const {
  auto root = stored();
  auto entries = root["drafts"].toMap();
  entries[scopeId()] =
      QVariantMap{{"scope", _state["scope"]}, {"notes", _rows},
                  {"tuplets", _tuplets},      {"settings", settings()},
                  {"identity", _identity},    {"draft", draft}};
  root["schema"] = 1;
  root["drafts"] = entries;
  root["activeId"] = scopeId();
  return root;
}
void RollWritingBar::hideEvent(QHideEvent *event) {
  qApp->removeEventFilter(this);
  if (_closing) {
    QWidget::hideEvent(event);
    return;
  }
  _pages.stop();
  if (_busy)
    _ready = false;
  if (_cancel)
    *_cancel = true;
  ++_generation;
  cancelGesture();
  stopAudition();
  QWidget::hideEvent(event);
}
void RollWritingBar::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  if (_enabled)
    qApp->installEventFilter(this);
  if (_enabled && !_ready)
    QTimer::singleShot(0, this, [this] {
      if (_enabled)
        readScope(_state["scope"].toMap());
    });
}
bool RollWritingBar::eventFilter(QObject *object, QEvent *event) {
  // The main application handles Escape before widget filters. Give the active
  // writing gesture its own cancellation while the roll has keyboard focus.
  if (object == _editor->_noteCanvas &&
      (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
      (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape ||
       static_cast<QKeyEvent *>(event)->matches(QKeySequence::Undo) ||
       static_cast<QKeyEvent *>(event)->matches(QKeySequence::Redo)))
    return handle(event);
  return false;
}
void RollWritingBar::updateActions() {
  const bool usable =
      _enabled && _ready && !_busy && _hasChanges && !_built.contains("error");
  _apply->setVisible(_mode->currentIndex() == 1);
  _apply->setEnabled(usable);
  _original->setVisible(_mode->currentIndex() == 1);
  _proposed->setVisible(_mode->currentIndex() == 1);
  _stop->setVisible(_mode->currentIndex() == 1);
  _original->setEnabled(usable);
  _proposed->setEnabled(usable);
  const bool own = _score && !_previewKey.isEmpty() && playbackPreviewPrepared(_score, _previewKey);
  _stop->setEnabled(own && (playbackPreviewActive(_score, false) || playbackPreviewActive(_score, true)));
  _grid->setEnabled(_enabled && _ready && !_busy);
  _tool->setEnabled(_enabled && _ready && !_busy);
  _track->setEnabled(_enabled && _ready && !_busy);
  for (const auto name :
       {"rollWriteLocks", "rollWriteOptions", "rollWriteQuantize"})
    if (auto button = findChild<QToolButton *>(name))
      button->setEnabled(_enabled && _ready && !_busy);
  {
    QSignalBlocker original(_original), proposed(_proposed);
    _original->setChecked(own && playbackPreviewActive(_score, false));
    _proposed->setChecked(own && playbackPreviewActive(_score, true));
  }
  setProperty("rollReady", _enabled && _ready && !_busy && !_pages.isActive());
}
void RollWritingBar::lockMenu() {
  const auto actions = _lockMenu->actions();
  for (int i = 1; i < actions.size(); ++i)
    delete actions[i];
  _lockMenu->addSeparator();
  const auto scope = _state["scope"].toMap();
  for (int t = scope["firstTrack"].toInt(); t < scope["endTrack"].toInt();
       ++t) {
    auto a = _lockMenu->addAction(
        tr("锁定谱表 %1 · 声部 %2").arg(t / 4 + 1).arg(t % 4 + 1));
    a->setCheckable(true);
    a->setChecked(_locked.contains(t));
    connect(a, &QAction::toggled, this, [this, t](bool value) {
      if (value)
        _locked.insert(t);
      else
        _locked.remove(t);
      _editor->updateSurfaces();
    });
  }
}
void RollWritingBar::savedMenu() {
  _savedMenu->clear();
  const auto entries = stored()["drafts"].toMap();
  for (auto i = entries.cbegin(); i != entries.cend(); ++i) {
    const auto row = i.value().toMap(), scope = row["scope"].toMap();
    if (!row["draft"].toBool())
      continue;
    auto action =
        _savedMenu->addAction(tr("第 %1–%2 小节 · 谱表 %3–%4")
                                  .arg(scope["firstMeasure"].toInt())
                                  .arg(scope["lastMeasure"].toInt())
                                  .arg(scope["firstTrack"].toInt() / 4 + 1)
                                  .arg((scope["endTrack"].toInt() + 3) / 4));
    connect(action, &QAction::triggered, this, [this, scope] {
      _mode->setCurrentIndex(1);
      readScope(scope);
    });
  }
  if (_savedMenu->isEmpty())
    _savedMenu->addAction(tr("画音后，草稿会随乐谱保存。"))->setEnabled(false);
  _savedMenu->addSeparator();
  auto discard = _savedMenu->addAction(tr("删除当前草稿"));
  discard->setEnabled(entries.value(scopeId()).toMap().value("draft").toBool());
  connect(discard, &QAction::triggered, this, &RollWritingBar::discardDraft);
}
QVariantMap RollWritingBar::state() const {
  QVariantList selected;
  for (auto id : _selected)
    selected.append(id);
  return {{"enabled", _enabled},
          {"ready", _ready && !_busy && !_pages.isActive()},
          {"busy", _busy},
          {"scope", _state["scope"]},
          {"notes", _rows},
          {"selected", selected},
          {"quarterPixels", _editor->_viewport.pixelsPerQuarter},
          {"fromX",
           _editor->xForTick(_state["scope"].toMap()["start"].toInt(), false)},
          {"sourceIdentity", _identity},
          {"status", _message->text()}};
}
} // namespace Ms
