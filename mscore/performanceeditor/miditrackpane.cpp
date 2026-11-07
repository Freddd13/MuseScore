// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "miditrackpane.h"
#include "performanceeditor.h"
#include "rollwriting.h"
#include "../workflow/controlflowlayout.h"
#include "../workflow/midicapture.h"
#include "../workflow/miditransport.h"
#include "../workflow/midisources.h"
#include "mscore/seq.h"
#include "audio/midi/msynthesizer.h"
#include "../workflow/toolicons.h"
#include "../workflow/featuregate.h"
#include "../workflow/editorareas.h"
#include "../workflow/workflowcontext.h"
#include "../workflow/playbackpreview.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include "libmscore/staff.h"
#include "mscore/musescore.h"
#include <QCheckBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QCursor>
#include <QHeaderView>
#include <QShowEvent>
#include <QHideEvent>
#include <QLabel>
#include <QMenu>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace Ms
{
QAction* midiTracksAction()
{
    static QPointer<QAction> action;
    if (action)
        return action;
    action = new QAction(QObject::tr("MIDI轨道"), qApp);
    action->setIconText(QObject::tr("轨道"));
    action->setObjectName("kumo-midi-tracks");
    action->setIcon(componentToolIcon("list"));
    action->setToolTip(QObject::tr("导入外部MIDI或新建临时轨，在卷帘中对照、编辑和试听。"));
    markPersonalAction(action);
    QObject::connect(action, &QAction::triggered, qApp, [] {
        if (personalFeaturesEnabled() && mscore) {
            mscore->showPerformanceEditor(true);
            openEditorArea(mscore->performanceEditor(), "performanceNotesArea");
            mscore->performanceEditor()->showMidiTracks(true);
        }
    });
    return action;
}
namespace
{
QToolButton* trackButton(QWidget* parent, const QString& text, const QString& icon)
{
    auto button = new QToolButton(parent);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setComponentToolIcon(button, icon);
    return button;
}
} // namespace
MidiTrackPane::MidiTrackPane(PerformanceEditor* editor, QWidget* area, QVBoxLayout* noteLayout)
    : QWidget(area), _editor(editor)
{
    setObjectName("midiTrackPane");
    setMinimumWidth(0);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 0, 3, 0);
    layout->setSpacing(3);
    auto commands = new QHBoxLayout;
    commands->setSpacing(3);
    _import = trackButton(this, tr("导入"), "open");
    _import->setObjectName("midiTrackImport");
    _add = trackButton(this, tr("新建"), "add");
    _add->setObjectName("midiTrackAdd");
    commands->addWidget(_import);
    commands->addWidget(_add);
    commands->addStretch();
    auto close = trackButton(this, {}, "close");
    close->setToolTip(tr("关闭MIDI轨道"));
    close->setObjectName("midiTrackClose");
    commands->addWidget(close);
    connect(close, &QToolButton::clicked, this, [this] { setWorkspaceVisible(false); });
    layout->addLayout(commands);
    connect(_import, &QToolButton::clicked, this, &MidiTrackPane::chooseImport);
    connect(_add, &QToolButton::clicked, this, [this] {
        const auto id = _document.create(tr("临时MIDI"));
        if (id.isEmpty())
            showMessage(_document.error());
        else {
            _activeId = id;
            rebuild();
            showMessage(tr("临时MIDI已建立，选择画音或录制开始写作。"));
        }
    });
    _list = new QTreeWidget(this);
    _list->setObjectName("midiTrackList");
    _list->setRootIsDecorated(false);
    _list->setUniformRowHeights(true);
    _list->setColumnCount(4);
    _list->setHeaderLabels({tr("音轨"), tr("显"), "M", "S"});
    _list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _list->header()->setStretchLastSection(false);
    for (int c = 1; c < 4; ++c) {
        _list->header()->setSectionResizeMode(c, QHeaderView::Fixed);
        _list->setColumnWidth(c, 27);
    }
    _list->headerItem()->setToolTip(1, tr("显示这条音轨"));
    _list->headerItem()->setToolTip(2, tr("静音"));
    _list->headerItem()->setToolTip(3, tr("独听；可同时独听多轨"));
    _list->setMinimumSize(0, 72);
    layout->addWidget(_list, 1);
    connect(_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item && !_internal)
            selectTrack(item->data(0, Qt::UserRole).toString());
    });
    connect(_list, &QTreeWidget::itemChanged, this, &MidiTrackPane::changeItem);
    auto bottom = new ControlFlowLayout;
    layout->addLayout(bottom);
    auto copy = trackButton(this, tr("复制"), "copy");
    copy->setObjectName("midiTrackCopy");
    bottom->addWidget(copy);
    connect(copy, &QToolButton::clicked, this, &MidiTrackPane::duplicateTrack);
    _delete = trackButton(this, tr("删除"), "delete");
    _delete->setObjectName("midiTrackDelete");
    bottom->addWidget(_delete);
    connect(_delete, &QToolButton::clicked, this, &MidiTrackPane::removeTrack);
    _restore = trackButton(this, tr("恢复"), "undo");
    _restore->setObjectName("midiTrackRestore");
    bottom->addWidget(_restore);
    connect(_restore, &QToolButton::clicked, this, &MidiTrackPane::restoreTrack);
    _status = new QLabel(area);
    _status->setObjectName("midiTrackMessage");
    _status->setWordWrap(true);
    _status->hide();

    _bar = new QWidget(area);
    _bar->setObjectName("midiTrackControls");
    auto tools = new ControlFlowLayout(_bar);
    tools->setContentsMargins(3, 1, 3, 1);
    _label = new QLabel(_bar);
    _label->setObjectName("midiTrackCurrent");
    tools->addWidget(_label);
    _play = trackButton(_bar, tr("播放MIDI"), "play");
    _play->setObjectName("midiTrackPlay");
    tools->addWidget(_play);
    connect(_play, &QToolButton::clicked, this, &MidiTrackPane::togglePlayback);
    _withScore = new QCheckBox(tr("和乐谱一起"), _bar);
    _withScore->setObjectName("midiTrackWithScore");
    tools->addWidget(_withScore);
    _record = trackButton(_bar, tr("录制"), "record");
    _record->setObjectName("midiTrackRecord");
    tools->addWidget(_record);
    connect(_record, &QToolButton::clicked, this, &MidiTrackPane::toggleRecording);
    auto sourceLabel = new QLabel(tr("音源"), _bar);
    tools->addWidget(sourceLabel);
    _source = new QComboBox(_bar);
    _source->setObjectName("midiTrackSource");
    _source->setMaximumWidth(190);
    tools->addWidget(_source);
    auto volumeLabel = new QLabel(tr("音量"), _bar);
    tools->addWidget(volumeLabel);
    _volumeSlider = new QSlider(Qt::Horizontal, _bar);
    _volumeSlider->setObjectName("midiTrackVolumeSlider");
    _volumeSlider->setRange(0, 200);
    _volumeSlider->setFixedWidth(80);
    tools->addWidget(_volumeSlider);
    _volume = new QSpinBox(_bar);
    _volume->setObjectName("midiTrackVolume");
    _volume->setRange(0, 200);
    _volume->setSuffix("%");
    tools->addWidget(_volume);
    connect(_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (_internal || _closing)
            return;
        QSignalBlocker block(_volume);
        _volume->setValue(value);
        if (!_volumeSlider->isSliderDown())
            setVolume();
    });
    connect(_volumeSlider, &QSlider::sliderReleased, this, &MidiTrackPane::setVolume);
    connect(_volume, &QSpinBox::editingFinished, this, &MidiTrackPane::setVolume);
    connect(_source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (_closing || !_bar || _internal || !active() || index < 0)
            return;
        stopTransport();
        if (!_document.update(_activeId, {{"source", _source->itemData(index)}}))
            showMessage(_document.error());
        else
            showMessage(tr("音源已更换，可播放或点琴键试音。"));
        _soundTail.start(100);
        updateActions();
    });
    _timeView = new QComboBox(_bar);
    _timeView->setObjectName("midiTrackTimeView");
    _timeView->addItems({tr("录音时间"), tr("按谱对齐")});
    tools->addWidget(_timeView);
    connect(_timeView, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (!_closing && _bar && !_internal && _editor) {
            finishGesture(true);
            rebuildRows();
            _editor->surfaceResized(PerformanceSurface::Notes);
        }
    });
    auto align = trackButton(_bar, tr("对齐…"), "link");
    align->setObjectName("midiTrackAlign");
    tools->addWidget(align);
    connect(align, &QToolButton::clicked, this, &MidiTrackPane::alignTrack);
    _tool = new QComboBox(_bar);
    _tool->setObjectName("midiTrackTool");
    _tool->addItems({tr("选择／拖动"), tr("画音")});
    tools->addWidget(_tool);
    _grid = new QComboBox(_bar);
    _grid->setObjectName("midiTrackGrid");
    _grid->addItem(tr("自由"), 0);
    _grid->addItem(tr("1/16"), 120);
    _grid->addItem(tr("1/8"), 240);
    _grid->addItem(tr("三连1/8"), 160);
    _grid->setCurrentIndex(1);
    tools->addWidget(_grid);
    auto velocityLabel = new QLabel(tr("力度"), _bar);
    tools->addWidget(velocityLabel);
    _velocity = new QSpinBox(_bar);
    _velocity->setObjectName("midiTrackVelocity");
    _velocity->setRange(1, 127);
    _velocity->setValue(96);
    tools->addWidget(_velocity);
    auto velocity = trackButton(_bar, tr("写入选音"), "apply");
    velocity->setObjectName("midiTrackSetVelocity");
    tools->addWidget(velocity);
    connect(velocity, &QToolButton::clicked, this, &MidiTrackPane::setVelocity);
    auto adopt = trackButton(_bar, tr("写谱草稿…"), "score");
    adopt->setObjectName("midiTrackAdopt");
    tools->addWidget(adopt);
    connect(adopt, &QToolButton::clicked, this, &MidiTrackPane::adoptToScore);
    auto more = trackButton(_bar, tr("更多"), "settings");
    more->setObjectName("midiTrackMore");
    tools->addWidget(more);
    connect(more, &QToolButton::clicked, this, &MidiTrackPane::showMore);
    while (auto item = tools->takeAt(0))
        delete item;
    auto group = [this, tools](const QList<QWidget*>& widgets) {
        auto container = new QWidget(_bar);
        auto row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        for (auto widget : widgets)
            row->addWidget(widget);
        tools->addWidget(container);
    };
    tools->addWidget(_label);
    group({_play, _withScore, _record});
    group({sourceLabel, _source});
    group({volumeLabel, _volumeSlider, _volume});
    group({_timeView, align});
    group({_tool, _grid});
    group({velocityLabel, _velocity, velocity});
    group({adopt, more});
    const auto recordText = _record->text();
    _record->setText(tr("停止录制"));
    _record->setMinimumWidth(_record->sizeHint().width());
    _record->setText(recordText);
    _bar->hide();
    auto canvas = _editor->_noteCanvas;
    auto split = new QSplitter(Qt::Horizontal, area);
    split->setObjectName("midiTrackListSplitter");
    split->setChildrenCollapsible(false);
    split->addWidget(this);
    split->addWidget(canvas);
    split->setCollapsible(0, true);
    split->setStretchFactor(1, 1);
    noteLayout->insertWidget(qMax(1, noteLayout->count() - 1), _bar);
    noteLayout->addWidget(split, 1);
    noteLayout->addWidget(_status);
    split->setSizes({QSettings().value("performanceEditor/midiTrackWidth", 220).toInt(),
                     qMax(300, editor->width() - 220)});
    connect(split, &QSplitter::splitterMoved, this, [this, split] {
        if (isVisible() && width() > 80)
            QSettings().setValue("performanceEditor/midiTrackWidth", split->sizes().first());
        syncInset();
    });
    _toggle = trackButton(area, tr("轨道"), "list");
    _toggle->setDefaultAction(midiTracksAction());
    _toggle->setObjectName("midiTrackToggle");
    if (auto row = qobject_cast<QHBoxLayout*>(noteLayout->itemAt(0)->layout()))
        row->insertWidget(qMax(0, row->count() - 1), _toggle);
    hide();
    connect(&_loading, &QFutureWatcher<QVariantMap>::finished, this, &MidiTrackPane::loadFinished);
    _poll.setInterval(40);
    connect(&_poll, &QTimer::timeout, this, &MidiTrackPane::poll);
    _soundTail.setSingleShot(true);
    connect(&_soundTail, &QTimer::timeout, this, [this] {
        if (!_playbackId && !_scorePlayback && _previewPitch < 0 && seq &&
            !(_capture && (_capture->state().value("recording").toBool() ||
                           _capture->state().value("stopping").toBool()))) {
            if (_monitorId && midiTrackPlaybackState().value("id").toULongLong() == _monitorId)
                stopMidiTrackPlayback();
            _monitorId = 0;
            clearMidiTrackSounds(seq->synti(), this);
        }
    });
    connect(workflowContext(), &WorkflowContext::midiTracksChanged, this, [this](Score* score) {
        if (_enabled && score && score->masterScore() == _document.score())
            contextChanged();
    });
    connect(rollWritingAction(), &QAction::toggled, this, [this](bool on) {
        if (on && active())
            selectTrack({});
    });
}
void MidiTrackPane::setWorkspaceVisible(bool on)
{
    if (_closing)
        return;
    finishGesture(true);
    releasePreview();
    _enabled = on;
    setVisible(on);
    if (on)
        qApp->installEventFilter(this);
    else {
        qApp->removeEventFilter(this);
        if (_capture)
            _capture->finishBeforeLeaving();
        stopTransport(true);
    }
    contextChanged();
    updateMode();
    _editor->rebuildPitchRows();
    _editor->surfaceResized(PerformanceSurface::Notes);
}
MidiTrackPane::~MidiTrackPane() { shutdown(); }
void MidiTrackPane::attachParameterArea(QWidget* area)
{
    auto content = new QWidget(area);
    content->setLayout(area->layout());
    auto row = new QHBoxLayout(area);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    _parameterInset = new QWidget(area);
    _parameterInset->setFixedWidth(0);
    row->addWidget(_parameterInset);
    row->addWidget(content, 1);
}
void MidiTrackPane::syncInset()
{
    if (!_parameterInset || !_editor)
        return;
    const int width = _enabled && _editor->_noteCanvas->isVisible() &&
                              _editor->_noteCanvas->window() == _parameterInset->window()
                          ? this->width() + qobject_cast<QSplitter*>(parentWidget())->handleWidth()
                          : 0;
    if (_parameterInset->width() != width)
        _parameterInset->setFixedWidth(width);
}
void MidiTrackPane::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncInset();
}
void MidiTrackPane::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (_enabled && !_closing) {
        _poll.start();
        QTimer::singleShot(0, this, [this] {
            contextChanged();
            syncInset();
        });
    }
}
void MidiTrackPane::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (_closing)
        return;
    finishGesture(true);
    releasePreview();
    syncInset();
    if (!_playbackId && !_scorePlayback &&
        !(_capture && (_capture->state().value("recording").toBool() ||
                       _capture->state().value("saving").toBool())))
        _poll.stop();
}
void MidiTrackPane::shutdown()
{
    if (_closing)
        return;
    _closing = true;
    _poll.stop();
    _soundTail.stop();
    qApp->removeEventFilter(this);
    // The bar belongs to the note area and can outlive this sidebar during
    // floating-area restoration. Disconnect it before editor members disappear.
    if (_bar) {
        for (auto object : _bar->findChildren<QObject*>())
            object->disconnect(this);
        _bar->disconnect(this);
    }
    if (parentWidget())
        parentWidget()->disconnect(this);
    _loading.disconnect(this);
    if (_referenceProject)
        _referenceProject->disconnect(this);
    if (auto context = workflowContextIfCreated())
        context->disconnect(this);
    rollWritingAction()->disconnect(this);
    for (auto master : _chosenTracks.keys())
        master->disconnect(this);
    _chosenTracks.clear();
    finishGesture(bool(_bar));
    releasePreview();
    stopTransport(true);
    _soundTail.stop();
    if (_capture) {
        _capture->disconnect(this);
        _capture->finishBeforeLeaving();
    }
    if (_cancel)
        *_cancel = true;
    _loading.waitForFinished();
    _editor = nullptr;
}
bool MidiTrackPane::scoreVisible() const
{
    return !_enabled ||
           _document.state().value("scoreOptions").toMap().value("visible", true).toBool();
}
void MidiTrackPane::showMessage(const QString& text)
{
    if (!_status)
        return;
    _status->setText(text);
    _status->setToolTip(QString());
    _status->setVisible(_enabled && !text.isEmpty());
}
QVariantMap MidiTrackPane::state()
{
    _document.reload();
    QVariantList tracks;
    for (const auto& value : _document.tracks()) {
        auto t = value.toMap();
        t.insert("noteCount", t.value("notes").toList().size());
        t.remove("notes");
        t.remove("events");
        tracks.append(t);
    }
    QVariantList visible;
    for (int i : _intervals.query(_editor->_scroll->value(),
                                  _editor->_scroll->value() + _editor->_scroll->pageStep())) {
        const auto& row = _rows[i];
        const auto box = rectangle(row, false);
        if (row.active && _editor->_noteCanvas->rect().intersects(box.toAlignedRect()))
            visible.append(QVariantMap{{"id", row.id},
                                       {"rect", box},
                                       {"pitch", row.pitch},
                                       {"velocity", row.velocity},
                                       {"tick", row.tick},
                                       {"until", row.until}});
    }
    return {{"enabled", _enabled},
            {"activeId", _activeId},
            {"revision", _document.revision()},
            {"tracks", tracks},
            {"busy", _loading.isRunning()},
            {"message", _status->text()},
            {"selectedNotes", _selected.size()},
            {"visibleNotes", visible},
            {"recordId", _recordId},
            {"recordImport", _recordImport},
            {"recordAt", _recordAt}};
}
void MidiTrackPane::contextChanged()
{
    if (_closing || _internal || !_enabled)
        return;
    if (!isVisible() && !_playbackId && !_scorePlayback &&
        !(_capture && (_capture->state().value("recording").toBool() ||
                       _capture->state().value("saving").toBool())))
        return;
    QScopedValueRollback<bool> updating(_internal, true);
    auto master = _editor->_score ? _editor->_score->masterScore() : nullptr;
    if (_document.score() != master) {
        finishGesture(true);
        stopTransport(true);
        if (_document.score())
            _chosenTracks[_document.score()] = _activeId;
        _document.setScore(master);
        _activeId = _chosenTracks.value(master);
        _selected.clear();
        if (master && !_chosenTracks.contains(master)) {
            _chosenTracks.insert(master, {});
            connect(master, &QObject::destroyed, this,
                    [this, master] { _chosenTracks.remove(master); });
        }
        rebuild();
    } else if (_document.reload())
        rebuild();
    if (!_document.error().isEmpty())
        showMessage(_document.error());
    updateActions();
}
void MidiTrackPane::rebuild()
{
    QSignalBlocker block(_list);
    _list->clear();
    auto score = new QTreeWidgetItem(_list, {tr("乐谱")});
    score->setIcon(0, componentToolIcon("score"));
    score->setData(0, Qt::UserRole, QString());
    const auto options = _document.state().value("scoreOptions").toMap();
    for (int c = 1; c < 4; ++c)
        score->setCheckState(c, options.value(c == 1   ? "visible"
                                              : c == 2 ? "mute"
                                                       : "solo",
                                              c == 1)
                                        .toBool()
                                    ? Qt::Checked
                                    : Qt::Unchecked);
    QTreeWidgetItem* chosen = score;
    for (const auto& value : _document.tracks()) {
        const auto track = value.toMap();
        auto item = new QTreeWidgetItem(_list, {track.value("name").toString()});
        item->setIcon(0, componentToolIcon("midi"));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(0, Qt::UserRole, track.value("id"));
        item->setToolTip(
            0, tr("%1个音符；双击名称可改名。").arg(track.value("notes").toList().size()));
        for (int c = 1; c < 4; ++c)
            item->setCheckState(c, track.value(c == 1   ? "visible"
                                               : c == 2 ? "mute"
                                                        : "solo")
                                           .toBool()
                                       ? Qt::Checked
                                       : Qt::Unchecked);
        if (track.value("id").toString() == _activeId)
            chosen = item;
    }
    if (chosen == score && !_activeId.isEmpty())
        _activeId.clear();
    _list->setCurrentItem(chosen);
    rebuildRows();
    updateMode();
    updateActions();
}
void MidiTrackPane::selectTrack(const QString& id)
{
    finishGesture(true);
    releasePreview();
    if (_monitorId && midiTrackPlaybackState().value("id").toULongLong() == _monitorId)
        stopMidiTrackPlayback();
    _monitorId = 0;
    _activeId = id;
    _selected.clear();
    rebuildRows();
    updateMode();
    updateActions();
    _editor->invalidateVisual();
}
void MidiTrackPane::changeItem(QTreeWidgetItem* item, int column)
{
    if (_closing || _internal)
        return;
    finishGesture(true);
    const auto id = item->data(0, Qt::UserRole).toString();
    const QString key = column == 1   ? "visible"
                        : column == 2 ? "mute"
                        : column == 3 ? "solo"
                                      : "name";
    const QVariant value =
        column ? QVariant(item->checkState(column) == Qt::Checked) : QVariant(item->text(0));
    const bool ok = id.isEmpty() ? _document.setScoreOptions({{key, value}})
                                 : _document.update(id, {{key, value}});
    if (!ok)
        showMessage(_document.error());
    else {
        const bool restart = (column == 2 || column == 3) && (_playbackId || _scorePlayback);
        if (restart)
            stopTransport();
        rebuildRows();
        updateActions();
        _editor->rebuildPitchRows();
        _editor->invalidateVisual();
        if (restart)
            togglePlayback();
    }
}
void MidiTrackPane::chooseImport()
{
    const auto path =
        QFileDialog::getOpenFileName(this, tr("导入参考MIDI"), {}, tr("MIDI文件 (*.mid *.midi)"));
    if (!path.isEmpty())
        importFile(path);
}
void MidiTrackPane::importFile(const QString& path)
{
    if (!personalFeaturesEnabled()) {
        showMessage(tr("个人扩展功能已关闭。"));
        return;
    }
    _editor->showMidiTracks(true);
    loadPath(path, true);
}
void MidiTrackPane::loadPath(const QString& path, bool reveal)
{
    if (_closing)
        return;
    if (reveal && !_enabled)
        setWorkspaceVisible(true);
    contextChanged();
    if (!_document.score()) {
        showMessage(tr("打开乐谱后可以导入MIDI，资料将随乐谱保存。"));
        return;
    }
    if (_loading.isRunning()) {
        showMessage(tr("正在读取上一份MIDI。"));
        return;
    }
    _jobScore = _recordImport ? _recordScore.data() : _document.score();
    _cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = _cancel;
    const QUrl url(path);
    const QString local = url.isLocalFile() ? url.toLocalFile() : path;
    showMessage(tr("正在读取%1…").arg(QFileInfo(local).fileName()));
    _loading.setFuture(QtConcurrent::run([local, cancel] {
        QFile file(local);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
            return QVariantMap{{"error", QObject::tr("MIDI读取失败，请选择32MiB以内的文件。")}};
        return prepareMidiTracks(file.readAll(), QFileInfo(local).completeBaseName(), *cancel);
    }));
    updateActions();
}
void MidiTrackPane::loadFinished()
{
    if (_closing)
        return;
    const auto prepared = _loading.result();
    if (prepared.contains("error")) {
        showMessage(prepared.value("error").toString());
        _recordImport = false;
        updateActions();
        return;
    }
    if (!_jobScore) {
        showMessage(tr("MIDI已读取；原乐谱已经关闭，重新打开后可再次导入。"));
        _recordImport = false;
        updateActions();
        return;
    }
    MidiTrackDocument background;
    MidiTrackDocument* target = &_document;
    if (_document.score() != _jobScore) {
        background.setScore(_jobScore);
        target = &background;
    }
    const bool recorded = _recordImport;
    const bool ok = recorded ? target->appendRecording(_recordId, prepared, _recordAt)
                             : target->addPrepared(prepared);
    if (ok && target == &_document) {
        _activeId = _recordImport
                        ? _recordId
                        : prepared.value("tracks").toList().first().toMap().value("id").toString();
        if (_enabled) {
            rebuild();
            const double at = _recordImport ? _recordAt : 0;
            QTimer::singleShot(0, this, [this, at] { revealMaterial(at); });
        }
    }
    _recordImport = false;
    showMessage(ok ? (recorded
                          ? tr("录制已加入当前轨，可播放、修改或放入写谱草稿。资料随乐谱保存。")
                          : tr("MIDI已加入。检查位置后，取消M列静音即可试听。资料随乐谱保存。"))
                   : target->error());
    updateActions();
}
void MidiTrackPane::removeTrack()
{
    if (_activeId.isEmpty())
        return;
    finishGesture(true);
    stopTransport();
    if (_document.remove(_activeId)) {
        _activeId.clear();
        rebuild();
        showMessage(tr("音轨已移除，可用恢复或Ctrl+Z找回。"));
    } else
        showMessage(_document.error());
}
void MidiTrackPane::restoreTrack()
{
    if (_document.restore()) {
        _activeId = _document.tracks().last().toMap().value("id").toString();
        rebuild();
        showMessage(tr("音轨已恢复。"));
    }
}
void MidiTrackPane::duplicateTrack()
{
    if (_activeId.isEmpty())
        return;
    const auto copy = _document.duplicate(_activeId);
    if (!copy.isEmpty()) {
        _activeId = copy;
        rebuild();
    } else
        showMessage(_document.error());
}
void MidiTrackPane::exportTrack(bool mappedTime)
{
    if (_activeId.isEmpty())
        return;
    const auto path = QFileDialog::getSaveFileName(
        this, tr("导出当前MIDI轨"), _document.track(_activeId).value("name").toString() + ".mid",
        tr("MIDI文件 (*.mid)"));
    if (path.isEmpty())
        return;
    const auto result = exportMidiTrack(_document.state(), _activeId, path, mappedTime);
    showMessage(result.value("ok").toBool() ? tr("已导出%1。").arg(QFileInfo(path).fileName())
                                            : result.value("error").toString());
}
void MidiTrackPane::showMore()
{
    QMenu menu(this);
    menu.addAction(tr("复制当前轨"), this, &MidiTrackPane::duplicateTrack);
    menu.addAction(tr("导出当前轨（MIDI时间）…"), this, [this] { exportTrack(false); });
    menu.addAction(tr("导出当前轨（原曲对齐时间）…"), this, [this] { exportTrack(true); });
    menu.addAction(tr("采用到乐谱…"), this, &MidiTrackPane::adoptToScore);
    const auto take = workflowContext()->lastRecording();
    auto recover = menu.addAction(tr("导入最近录制…"));
    recover->setEnabled(!take.value("path").toString().isEmpty());
    connect(recover, &QAction::triggered, this,
            [this, take] { importFile(take.value("path").toString()); });
    menu.addSeparator();
    menu.addAction(tr("删除当前轨"), this, &MidiTrackPane::removeTrack);
    menu.addAction(tr("恢复最近删除的轨道"), this, &MidiTrackPane::restoreTrack);
    menu.exec(QCursor::pos());
}
void MidiTrackPane::setVolume()
{
    if (_closing || _internal || !active())
        return;
    const bool restart = _playbackId || _scorePlayback;
    const double value = _volume->value() / 100.;
    if (_document.track(_activeId).value("volume", 1.).toDouble() == value)
        return;
    if (restart)
        stopTransport();
    if (!_document.update(_activeId, {{"volume", value}}))
        showMessage(_document.error());
    else {
        QSignalBlocker block(_volumeSlider);
        _volumeSlider->setValue(_volume->value());
        if (restart)
            togglePlayback();
    }
}
void MidiTrackPane::updateActions()
{
    if (_closing || !_editor)
        return;
    const auto capture = _capture ? _capture->state() : QVariantMap();
    const bool recording = capture.value("recording").toBool(),
               busy = recording || capture.value("saving").toBool() ||
                      capture.value("stopping").toBool();
    const bool ready = _document.score() && !_loading.isRunning() && !busy;
    _import->setEnabled(ready);
    _add->setEnabled(ready);
    _delete->setEnabled(ready && !_activeId.isEmpty());
    _restore->setEnabled(ready && !_document.state().value("deleted").toList().isEmpty());
    _play->setEnabled(ready || _playbackId || _scorePlayback);
    _record->setEnabled(recording || (ready && !_activeId.isEmpty()));
    const auto recordText = recording ? tr("停止录制") : tr("录制");
    if (_record->text() != recordText) {
        _record->setText(recordText);
        setComponentToolIcon(_record, recording ? "stop" : "record");
    }
    _list->setEnabled(!busy);
    _source->setEnabled(ready);
    _tool->setEnabled(ready);
    _grid->setEnabled(ready);
    _velocity->setEnabled(ready);
    _timeView->setEnabled(ready && !_playbackId && !_scorePlayback);
    _withScore->setEnabled(ready && !_playbackId && !_scorePlayback);
    const bool playing = _playbackId || _scorePlayback;
    const auto playText = playing ? tr("停止播放") : tr("播放MIDI");
    if (_play->text() != playText) {
        _play->setText(playText);
        setComponentToolIcon(_play, playing ? "stop" : "play");
    }
    const auto track = _document.track(_activeId);
    const auto full = track.value("name").toString();
    _label->setText(track.isEmpty() ? tr("乐谱")
                                    : tr("MIDI：%1 · 已选%2音")
                                          .arg(fontMetrics().elidedText(full, Qt::ElideRight, 180))
                                          .arg(_selected.size()));
    _label->setToolTip(full);
}
} // namespace Ms
