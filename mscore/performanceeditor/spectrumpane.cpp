// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumpane.h"
#include "../musescore.h"
#include "../scoreview.h"
#include "../seq.h"
#include "../plugin/api/scoreobserver.h"
#include "../workflow/builtinpanels.h"
#include "../workflow/editorareas.h"
#include "../workflow/referencefactory.h"
#include "../workflow/referencepanel.h"
#include "../workflow/workflowcontext.h"
#include "../workflow/toolicons.h"
#include "libmscore/input.h"
#include "libmscore/note.h"
#include "libmscore/undo.h"
#include "performanceeditor.h"
#include "spectrumannotation.h"
#include "spectrumbeat.h"
#include "spectrumdisplay.h"
#include "spectrumlisten.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDynamicPropertyChangeEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace Ms {
namespace {
constexpr int gutter = PerformanceViewport::gutter;
double audioAt(const QVariantMap &clip, double tick) {
    auto points = clip.value("anchors").toList();
    points.prepend(QVariantMap{{"tick", clip.value("start")}, {"ms", clip.value("from")}});
    points.append(QVariantMap{{"tick", clip.value("end")}, {"ms", clip.value("until")}});
    for (int i = 1; i < points.size(); ++i) {
        auto a = points[i - 1].toMap(), b = points[i].toMap();
        if (tick <= b.value("tick").toDouble())
            return a.value("ms").toDouble() +
                   (tick - a.value("tick").toDouble()) *
                       (b.value("ms").toDouble() - a.value("ms").toDouble()) /
                       qMax(1., b.value("tick").toDouble() - a.value("tick").toDouble());
    }
    return clip.value("until").toDouble();
}
double tickAt(const QVariantMap &clip, double ms) {
    auto points = clip.value("anchors").toList();
    points.prepend(QVariantMap{{"tick", clip.value("start")}, {"ms", clip.value("from")}});
    points.append(QVariantMap{{"tick", clip.value("end")}, {"ms", clip.value("until")}});
    for (int i = 1; i < points.size(); ++i) {
        auto a = points[i - 1].toMap(), b = points[i].toMap();
        if (ms <= b.value("ms").toDouble())
            return a.value("tick").toDouble() +
                   (ms - a.value("ms").toDouble()) *
                       (b.value("tick").toDouble() - a.value("tick").toDouble()) /
                       qMax(.001, b.value("ms").toDouble() - a.value("ms").toDouble());
    }
    return clip.value("end").toDouble();
}
QString pitchName(int pitch) {
    static const QStringList names = {"C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"};
    return names[qBound(0, pitch, 127) % 12] + QString::number(pitch / 12 - 1);
}
} // namespace
class SpectrumCanvas : public QWidget {
    SpectrumPane *_pane;
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        _pane->paint(p, rect(), false);
    }
    void resizeEvent(QResizeEvent *) override { _pane->viewChanged(); }
    void mousePressEvent(QMouseEvent *e) override { _pane->pointer(e, false); }
    void mouseMoveEvent(QMouseEvent *e) override { _pane->pointer(e, false); }
    void mouseReleaseEvent(QMouseEvent *e) override { _pane->pointer(e, true); }
    void wheelEvent(QWheelEvent *e) override { _pane->wheel(e); }

  public:
    SpectrumCanvas(SpectrumPane *p) : QWidget(p), _pane(p) {
        setMinimumHeight(64);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setObjectName("transcriptionSpectrumCanvas");
    }
};

SpectrumPane::SpectrumPane(PerformanceEditor *editor) : QWidget(editor), _editor(editor) {
    _playOwner = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setObjectName("transcriptionSpectrumPane");
    setMinimumHeight(180);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    auto toolHost = new QWidget;
    auto tools = new ControlFlowLayout(toolHost);
    _tools = tools;
    toolHost->setObjectName("spectrumTransportTools");
    tools->setSpacing(5);
    auto toolbarScroll = new ControlScrollArea;
    toolbarScroll->setWidget(toolHost);
    toolbarScroll->setWidgetResizable(true);
    toolbarScroll->setFrameShape(QFrame::NoFrame);
    toolbarScroll->setMaximumHeight(125);
    toolbarScroll->setMinimumHeight(32);
    toolbarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(toolbarScroll);
    auto button = [this, tools](const QString &text, int row, int col, auto fn) {
        auto b = new QToolButton(this);
        b->setText(text);
        connect(b, &QToolButton::clicked, this, fn);
        Q_UNUSED(row); Q_UNUSED(col);
        tools->addWidget(b);
        return b;
    };
    auto referenceButton = button(tr("原曲参考"), 0, 0, [] { showReferencePanel(true); });
    setComponentToolIcon(referenceButton, "waveform");
    _tracks = new QComboBox;
    _tracks->setObjectName("transcriptionTracks");
    _tracks->setPlaceholderText(tr("选择原曲音轨"));
    _tracks->addItem(tr("先打开音频"), QString());
    _tracks->setCurrentIndex(0);
    _tracks->setEnabled(false);
    _tracks->setMinimumContentsLength(12);
    _tracks->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    tools->addWidget(_tracks);
    _clips = new QComboBox;
    _clips->setObjectName("transcriptionClips");
    _clips->setPlaceholderText(tr("时间范围"));
    _clips->setToolTip(tr("整条录音：独立浏览原曲时间。按谱对齐：选择一个关联片段，与编辑器共用缩放和平移。"));
    _clips->addItem(tr("整条录音 · 原曲时间"), QVariantMap());
    _clips->setCurrentIndex(0);
    _clips->setEnabled(false);
    _clips->setMinimumContentsLength(10);
    _clips->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    tools->addWidget(_clips);
    _fitTime = button(tr("匹配视图"), 0, 0, [this] { fitTimeRange(); });
    _fitTime->setObjectName("spectrumFitTime");
    setComponentToolIcon(_fitTime, "fit");
    _fitTime->setEnabled(false);
    _display = new QComboBox;
    _display->setObjectName("transcriptionDisplay");
    _display->addItems({tr("上下对照"), tr("叠加对照")});
    _display->setToolTip(tr("叠加在钢琴卷帘后对照；按住 Alt 可以选择和拖动谱中音符。"));
    _display->setCurrentIndex(QSettings().value("spectrum/display", 0).toInt());
    tools->addWidget(_display);
    _preset = new QComboBox;
    _preset->addItems({tr("音高与起音"), tr("低音更清楚"), tr("起音更清楚")});
    _preset->setCurrentIndex(QSettings().value("spectrum/preset", 0).toInt());
    tools->addWidget(_preset);
    _tool = new QComboBox;
    _tool->setObjectName("transcriptionTool");
    _tool->addItems({tr("取音／试音"), tr("选择循环"), tr("手形平移"), tr("画／调整标注")});
    tools->addWidget(_tool);
    auto cacheButton = button(tr("缓存设置"), 0, 6, [this] { cacheSettings(); });
    setComponentToolIcon(cacheButton, "settings");
    _play = button(tr("播放原曲"), 1, 0, [this] { playSource(); });
    _play->setObjectName("transcriptionPlay");
    setComponentToolIcon(_play, "play");
    _play->setEnabled(false);
    _loop = button(tr("循环"), 1, 1, [this] {
        if (_a < 0 || _b <= _a) {
            setStatus(tr("使用“选择循环”拖过要反复听的片段。"));
            _loop->setChecked(false);
            return;
        }
        if (ownsPlayback() && _media->property("playing").toBool()) {
            const double at = sourcePosition();
            QMetaObject::invokeMethod(_media, "stop");
            _cursorMs = at;
            playSource();
        }
    });
    _loop->setCheckable(true);
    setComponentToolIcon(_loop, "loop");
    _speed = new QDoubleSpinBox;
    _speed->setRange(50, 150);
    _speed->setSuffix("%");
    _speed->setValue(100);
    _speed->setToolTip(tr("保调播放速度"));
    auto speedControl = new QWidget;
    auto speedLayout = new QHBoxLayout(speedControl);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    speedLayout->setSpacing(3);
    auto speedSlider = new QSlider(Qt::Horizontal);
    speedSlider->setObjectName("spectrumSpeedSlider");
    speedSlider->setRange(50, 150);
    speedSlider->setValue(100);
    speedSlider->setMinimumWidth(30);
    speedSlider->setToolTip(tr("保调播放速度：50%–150%"));
    speedLayout->addWidget(new QLabel(tr("速度")));
    speedLayout->addWidget(speedSlider, 1);
    speedLayout->addWidget(_speed);
    connect(speedSlider, &QSlider::valueChanged, this, [this](int value) { _speed->setValue(value); });
    connect(_speed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [speedSlider](double value) {
        QSignalBlocker block(speedSlider);
        speedSlider->setValue(qRound(value));
    });
    tools->addWidget(speedControl);
    _tuning = new QDoubleSpinBox;
    _tuning->setRange(400, 480);
    _tuning->setDecimals(1);
    _tuning->setSuffix(" Hz");
    _tuning->setPrefix("A4 ");
    _tuning->setValue(QSettings().value("spectrum/tuning", 440).toDouble());
    _tuning->setToolTip(tr("A4 调律频率；只改变频率与音名的对照"));
    tools->addWidget(_tuning);
    _transpose = new QSpinBox;
    _transpose->setRange(-24, 24);
    _transpose->setPrefix(tr("移调 "));
    _transpose->setSuffix(tr(" 半音"));
    _transpose->setValue(QSettings().value("spectrum/transpose", 0).toInt());
    _transpose->setToolTip(tr("将选出的音名移调后标注和输入；录音频率保持原值"));
    tools->addWidget(_transpose);
    _harmonics = button(tr("谐波参考"), 1, 5, [this] {
        _canvas->update();
        _editor->updateSurfaces();
    });
    _harmonics->setCheckable(true);
    _harmonics->setChecked(true);
    auto inputButton = button(tr("输入乐谱"), 1, 6, [this] { insertPitches(); });
    inputButton->setObjectName("transcriptionInput");
    setComponentToolIcon(inputButton, "score");
    inputButton->setEnabled(false);
    inputButton->setToolTip(
        tr("把选中的音高按当前输入位置、声部和时值写入；多个音高组成和弦。Ctrl+Z 撤销。"));
    _controls = {referenceButton, _tracks, _clips, _display, _preset, _tool,
                 cacheButton, _play, _loop, speedControl, _tuning, _transpose,
                 _harmonics, inputButton};
    auto displayButton = button(tr("频谱设置"), 2, 0, [this] {
        _displayPanel->setVisible(!_displayPanel->isVisible());
        QSettings().setValue("spectrum/displayControls", _displayPanel->isVisible());
    });
    displayButton->setObjectName("spectrumDisplaySettings");
    setComponentToolIcon(displayButton, "settings");
    _controls.append(displayButton);
    auto rebuild = button(tr("重新分析"), 2, 1, [this] {
        _forceSpectrum = true;
        _request.clear();
        viewChanged();
    });
    _controls.append(rebuild);
    setComponentToolIcon(rebuild, "refresh");
    auto cancel = button(tr("停止分析"), 2, 2, [this] {
        if (_session)
            QMetaObject::invokeMethod(_session, "cancel");
        setStatus(tr("已请求停止分析，已有频谱继续保留。"));
    });
    _controls.append(cancel);
    setComponentToolIcon(cancel, "stop");
    _displayControls = new SpectrumDisplayControls(this);
    auto options = new QTabWidget;
    options->setObjectName("spectrumOptionsTabs");
    options->addTab(_displayControls, tr("显示"));
    _listenControls = new SpectrumListenControls(this);
    options->addTab(_listenControls, tr("听辨"));
    _beatControls = new SpectrumBeatControls(this);
    options->addTab(_beatControls, tr("节拍"));
    tools->removeWidget(_preset);
    tools->removeWidget(_controls[6]);
    tools->removeWidget(rebuild);
    tools->removeWidget(cancel);
    auto analysis = new QWidget;
    auto analysisBox = new QVBoxLayout(analysis);
    analysisBox->setContentsMargins(4, 4, 4, 4);
    analysisBox->addWidget(new QLabel(tr("分析方式")));
    analysisBox->addWidget(_preset);
    _durationFromMark = new QCheckBox(tr("输入乐谱时，按标注长度估计记谱时值"));
    _durationFromMark->setObjectName("spectrumInputMarkedDuration");
    _durationFromMark->setChecked(QSettings().value("spectrum/inputMarkedDuration", false).toBool());
    analysisBox->addWidget(_durationFromMark);
    connect(_durationFromMark, &QCheckBox::clicked, this,
            [](bool checked) { QSettings().setValue("spectrum/inputMarkedDuration", checked); });
    auto analysisButtons = new QHBoxLayout;
    analysisButtons->addWidget(_controls[6]);
    analysisButtons->addWidget(rebuild);
    analysisButtons->addWidget(cancel);
    analysisBox->addLayout(analysisButtons);
    analysisBox->addStretch();
    options->addTab(analysis, tr("分析"));
    const auto primary = _controls;
    for (auto control : primary)
        tools->removeWidget(control);
    _controls = {primary[0],  primary[1],    primary[2], primary[7],  primary[8],  primary[3], primary[5],
                 primary[13], displayButton, primary[9], primary[10], primary[11], primary[12]};
    for (auto control : _controls) {
        control->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        control->setMinimumWidth(0);
    }
    speedControl->setFixedWidth(224);
    for (auto box : {_tracks, _clips}) {
        box->setMinimumWidth(140);
        box->setMaximumWidth(240);
    }
    _clips->setMinimumWidth(180);
    _clips->setMaximumWidth(280);
    tools->removeWidget(_fitTime);
    for (auto control : _controls) {
        tools->addWidget(control);
        if (control == _clips)
            tools->addWidget(_fitTime);
    }

    _displayPanel = new QScrollArea;
    _displayPanel->setObjectName("spectrumOptionsScroll");
    _displayPanel->setWidget(options);
    _displayPanel->setWidgetResizable(true);
    _displayPanel->setFrameShape(QFrame::NoFrame);
    _displayPanel->setMinimumHeight(50);
    _displayPanel->setMaximumHeight(180);
    _displayPanel->setVisible(QSettings().value("spectrum/displayControls", false).toBool());
    layout->addWidget(_displayPanel);
    connect(_beatControls, &SpectrumBeatControls::changed, this, [this] {
        if (!_track.value("id").toString().isEmpty())
            _grids[_track.value("id").toString()] = _beatControls->values();
        _canvas->update();
        _editor->_noteCanvas->update();
    });
    connect(_beatControls, &SpectrumBeatControls::estimateRequested, this, &SpectrumPane::estimateTempo);
    connect(_beatControls, &SpectrumBeatControls::tapRequested, this, &SpectrumPane::tapGrid);
    connect(_beatControls, &SpectrumBeatControls::firstBeatRequested, this,
            [this] { _beatControls->setFirstBeat(sourceInPlayback() ? sourcePosition() : _cursorMs); });
    connect(_beatControls, &SpectrumBeatControls::saveRequested, this, [this] {
        if (_track.value("id").toString().isEmpty()) {
            setStatus(tr("先选择一条原曲音轨，再保存节拍网格。"));
            return;
        }
        saveMarks();
    });
    _listenTimer.setSingleShot(true);
    _listenTimer.setInterval(120);
    connect(_listenControls, &SpectrumListenControls::changed, this, [this] { _listenTimer.start(); });
    connect(&_listenTimer, &QTimer::timeout, this, [this] {
        if (ownsPlayback() && _media->property("playing").toBool()) {
            const double at = sourcePosition();
            QMetaObject::invokeMethod(_media, "stop");
            _cursorMs = at;
            playSource();
        }
    });
    connect(_displayControls, &SpectrumDisplayControls::changed, this, [this](bool analysis) {
        if (analysis) {
            _request.clear();
            viewChanged();
        } else
            recolor();
    });
    auto canvasHost = new QWidget;
    auto canvasGrid = new QGridLayout(canvasHost);
    canvasGrid->setContentsMargins(0, 0, 0, 0);
    canvasGrid->setSpacing(0);
    _canvas = new SpectrumCanvas(this);
    canvasGrid->addWidget(_canvas, 0, 0);
    _pitchScroll = new QScrollBar(Qt::Vertical);
    _pitchScroll->setObjectName("spectrumPitchScroll");
    canvasGrid->addWidget(_pitchScroll, 0, 1);
    _timeScroll = new QScrollBar(Qt::Horizontal);
    _timeScroll->setObjectName("spectrumTimeScroll");
    canvasGrid->addWidget(_timeScroll, 1, 0);
    _marksSplitter = new QSplitter(Qt::Vertical);
    _marksSplitter->setObjectName("spectrumMarksSplitter");
    _marksSplitter->setChildrenCollapsible(false);
    _marksSplitter->addWidget(canvasHost);
    layout->addWidget(_marksSplitter, 1);
    connect(_timeScroll, &QScrollBar::valueChanged, this, [this](int value) {
        if (_clip.isEmpty())
            _from = value;
        else
            _editor->_scroll->setValue(value);
        viewChanged();
    });
    connect(_pitchScroll, &QScrollBar::valueChanged, this, [this](int value) {
        const double span = _high - _low;
        _high = 128 - value / 100.;
        _low = _high - span;
        viewChanged();
    });
    _target = new QLabel;
    _target->setObjectName("spectrumInputTarget");
    _target->setMinimumWidth(0);
    _target->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(_target);
    auto bottomHost = new QWidget;
    bottomHost->setObjectName("spectrumMarkTools");
    auto bottom = new ControlFlowLayout(bottomHost);
    _status = new QLabel(tr("先在原曲参考中打开音频。"));
    _status->setObjectName("spectrumStatus");
    _status->setWordWrap(true);
    _status->setMaximumHeight(fontMetrics().lineSpacing() * 2 + 4);
    layout->addWidget(_status);
    auto mark = new QToolButton;
    mark->setObjectName("transcriptionMark");
    mark->setText(tr("标注这个音"));
    setComponentToolIcon(mark, "pencil");
    mark->setEnabled(false);
    mark->setToolTip(tr("保存点击选中的音高和时间；鼠标移动后仍保留刚才的选择。"));
    connect(mark, &QToolButton::clicked, this, &SpectrumPane::markPitch);
    bottom->addWidget(mark);
    auto remove = new QToolButton;
    remove->setText(tr("删除选中标注"));
    setComponentToolIcon(remove, "delete");
    remove->setObjectName("spectrumDeleteMarks");
    connect(remove, &QToolButton::clicked, this, [this] {
        QSet<QString> ids;
        for (auto item : _marks->selectedItems())
            ids.insert(item->data(Qt::UserRole).toString());
        for (int i = _annotations.size() - 1; i >= 0; --i)
            if (ids.contains(_annotations[i].toMap().value("id").toString()))
                _annotations.removeAt(i);
        saveMarks();
    });
    bottom->addWidget(remove);
    _marksToggle = new QToolButton;
    _marksToggle->setObjectName("spectrumMarksToggle");
    _marksToggle->setCheckable(true);
    _marksToggle->setChecked(QSettings().value("spectrum/showMarks", true).toBool());
    _marksToggle->setToolTip(tr("展开或收起已经保存的音高标注。拖动频谱与列表之间的分隔条可调整高度。"));
    connect(_marksToggle, &QToolButton::clicked, this, [this](bool checked) {
        QSettings().setValue("spectrum/showMarks", checked);
        updateMarksVisibility();
    });
    bottom->addWidget(_marksToggle);
    layout->addWidget(bottomHost);
    _marks = new QListWidget;
    _marks->setObjectName("transcriptionMarks");
    _marks->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _marks->setMinimumHeight(28);
    _marks->setMinimumWidth(0);
    _marks->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    _marks->setToolTip(tr("点击标注可定位与试音，按住 Ctrl 选择多个音高后输入为和弦。"));
    _marksSplitter->addWidget(_marks);
    _marksSplitter->setStretchFactor(0, 5);
    _marksSplitter->setStretchFactor(1, 1);
    _marksSplitter->setSizes({400, 80});
    const auto markSizes = QSettings().value("spectrum/marksSplitter").toByteArray();
    if (!markSizes.isEmpty())
        _marksSplitter->restoreState(markSizes);
    connect(_marksSplitter, &QSplitter::splitterMoved, this, [this] {
        QSettings().setValue("spectrum/marksSplitter", _marksSplitter->saveState());
    });
    connect(_marks, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        auto m = item->data(Qt::UserRole + 1).toMap();
        _pickMs = m.value("ms").toDouble();
        _cursorMs = _pickMs;
        _pickFrequency = m.value("frequency", 440).toDouble();
        _listenControls->setFrequency(_pickFrequency);
        _hasPick = true;
        _cursorPitch = 69. + 12. * std::log2(m.value("frequency", 440).toDouble() / _tuning->value());
        if (_clip.isEmpty())
            _from = qMax(0., _cursorMs - _span / 2);
        seekSource(_cursorMs);
        _editor->auditionPitch(inputPitch(m));
        viewChanged();
    });
    connect(_marks, &QListWidget::itemSelectionChanged, this, &SpectrumPane::updateActions);
    _requestTimer.setSingleShot(true);
    _requestTimer.setInterval(90);
    connect(&_requestTimer, &QTimer::timeout, this, &SpectrumPane::requestVisible);
    connect(workflowContext(), &WorkflowContext::activeScoreChanged, this, &SpectrumPane::scoreChanged);
    connect(_tracks, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const auto tracks = _media->property("tracks").toList();
        const auto previousTrack = _track.value("id");
        const auto previousClip = _clip;
        _track = index >= 0 && index < tracks.size() ? tracks[index].toMap() : QVariantMap();
        _clips->setEnabled(!_track.isEmpty());
        if (index >= 0 && index < tracks.size() && _media->property("selectedTrack").toInt() != index)
            _media->setProperty("selectedTrack", index);
        const auto path = _track.value("path").toString();
        if (path != _path) {
            _path = path;
            _drawingMark = false;
            _editingMark.clear();
            _image = QImage();
            _rawImage = QImage();
            _info.clear();
            _request.clear();
            _from = 0;
            _a = _b = -1;
            _cursorMs = 0;
            _hasPick = false;
            if (_session)
                QMetaObject::invokeMethod(_session, "cancel");
            setStatus(_path.isEmpty() ? tr("在原曲参考中选择音频。") : tr("正在读取选中的音轨…"));
            if (ownsPlayback())
                QMetaObject::invokeMethod(_media, "stop");
            if (_audio) {
                QMetaObject::invokeMethod(_audio, "pause");
                _audioLoaded = false;
                _metadataCancelled = false;
                if (!_path.isEmpty())
                    QMetaObject::invokeMethod(_audio, "open", Q_ARG(QString, _path));
            }
        }
        QSignalBlocker block(_clips);
        _clips->clear();
        _clips->addItem(tr("整条录音 · 原曲时间"), QVariantMap());
        for (auto c : _track.value("associations").toList()) {
            auto a = c.toMap();
            _clips->addItem(tr("按谱对齐 · %1").arg(a.value("name", tr("对应片段")).toString()), a);
        }
        int selected = 0;
        const auto savedView = QSettings().value(timeViewKey()).toString();
        for (int i = 1; i < _clips->count(); ++i)
            if (!savedView.isEmpty() && _clips->itemData(i).toMap().value("id").toString() == savedView) {
                selected = i;
                break;
            }
        if (previousTrack == _track.value("id")) {
            if (previousClip.isEmpty())
                selected = 0;
            else
                for (int i = 1; i < _clips->count(); ++i) {
                    auto c = _clips->itemData(i).toMap();
                    if ((!previousClip.value("id").toString().isEmpty() &&
                         c.value("id") == previousClip.value("id")) ||
                        c == previousClip) {
                        selected = i;
                        break;
                    }
                }
        }
        _clips->setCurrentIndex(selected);
        _clip = _clips->currentData().toMap();
        _request.clear();
        if (previousTrack != _track.value("id"))
            restoreGrid();
        refreshMarks();
        updateActions();
        viewChanged();
    });
    connect(_clips, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        _clip = _clips->currentData().toMap();
        QSettings().setValue(timeViewKey(), _clip.value("id").toString());
        _request.clear();
        if (!_clip.isEmpty())
            fitTimeRange();
        viewChanged();
    });
    connect(_display, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int n) {
        QSettings().setValue("spectrum/display", n);
        updateMarksVisibility();
        recolor();
        setMinimumHeight(n == 1 ? 126 : 180);
        viewChanged();
        _editor->invalidateVisual();
        if (n == 1)
            QTimer::singleShot(0, this, [this] {
                const auto &v = _editor->_viewport;
                _editor->_viewport.topPitch =
                    v.pitchAtIndex(v.pitchIndex(_cursorPitch + _transpose->value()) +
                                   _editor->_noteCanvas->height() / (2 * v.rowHeight) - .5);
                _editor->invalidateVisual();
            });
    });
    connect(_preset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int n) {
        QSettings().setValue("spectrum/preset", n);
        viewChanged();
    });
    connect(_tuning, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double n) {
        QSettings().setValue("spectrum/tuning", n);
        if (_hasPick)
            _cursorPitch = 69. + 12. * std::log2(_pickFrequency / n);
        viewChanged();
    });
    connect(_transpose, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) {
        QSettings().setValue("spectrum/transpose", n);
        refreshMarks();
        _canvas->update();
        _editor->_noteCanvas->update();
    });
    connect(_speed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { _listenTimer.start(); });
    connect(_editor->_scroll, &QScrollBar::valueChanged, this, [this] { viewChanged(); });
    updateMarksVisibility();
    setMinimumHeight(_display->currentIndex() == 1 ? 126 : 180);
    applyAppearance();
    installEventFilter(this);
    for (auto widget : findChildren<QWidget *>())
        widget->installEventFilter(this);
}
void SpectrumPane::setStatus(const QString &text) {
    _status->setText(text);
    _status->setToolTip(text);
}
int SpectrumPane::sourceWidth() const {
    return _display->currentIndex() == 1 ? _editor->_noteCanvas->width() : _canvas->width();
}
int SpectrumPane::plotWidth() const {
    return qMax(1, sourceWidth() - gutter - PerformanceViewport::rightMargin);
}
QString SpectrumPane::timeViewKey() const {
    const auto identity = (_track.value("id").toString() + "\n" + _path).toUtf8();
    return "spectrum/timeView/" + QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
}
void SpectrumPane::fitTimeRange() {
    if (!_clip.isEmpty()) {
        if (!_editor->score())
            return;
        const int from = _clip.value("start").toInt(), until = _clip.value("end").toInt();
        const auto score = _editor->score();
        const double units = _editor->comparisonTimeActive()
            ? 2. * (score->tempomap()->tick2time(until) - score->tempomap()->tick2time(from))
            : double(until - from) / DIVISION;
        if (units <= 0)
            return;
        _editor->pauseFollow();
        _editor->_viewport.pixelsPerQuarter = qBound(.01, plotWidth() / units, 1000.);
        _editor->surfaceResized(PerformanceSurface::Parameter);
        _editor->_scroll->setValue(from);
        _editor->invalidateVisual();
    } else if (_audio && _audio->property("duration").toDouble() > 0) {
        const double span = qBound(250., _audio->property("duration").toDouble(), 120000.);
        if (_display->currentIndex() == 1)
            _editor->zoomTimeline(_span / span, gutter);
        else
            _span = span;
        _from = qMax(0., _cursorMs - _span / 2);
    }
    viewChanged();
}
bool SpectrumPane::showRollNotes() const {
    return !isVisible() || _display->currentIndex() != 1 ||
           _displayControls->values().value("showNotes", true).toBool();
}
void SpectrumPane::recolor() {
    _image = colorSpectrum(_rawImage, _displayControls->values(), _display->currentIndex() == 1);
    _canvas->update();
    _editor->_noteCanvas->update();
}
void SpectrumPane::syncScrollBars() {
    QSignalBlocker horizontal(_timeScroll), vertical(_pitchScroll);
    if (_clip.isEmpty()) {
        const int duration = qBound(0., _audio ? _audio->property("duration").toDouble() : 0., 2147480000.);
        _timeScroll->setRange(0, qMax(0, duration - qRound(_span)));
        _timeScroll->setPageStep(qRound(_span));
        _timeScroll->setSingleStep(qMax(1, qRound(_span / 20)));
        _from = qBound(0., _from, double(_timeScroll->maximum()));
        _timeScroll->setValue(qRound(_from));
    } else {
        _timeScroll->setRange(_editor->_scroll->minimum(), _editor->_scroll->maximum());
        _timeScroll->setPageStep(_editor->_scroll->pageStep());
        _timeScroll->setValue(_editor->_scroll->value());
    }
    const double span = _high - _low;
    _pitchScroll->setRange(0, qRound((128 - span) * 100));
    _pitchScroll->setPageStep(qRound(span * 100));
    _pitchScroll->setSingleStep(100);
    _pitchScroll->setValue(qRound((128 - _high) * 100));
}
bool SpectrumPane::eventFilter(QObject *object, QEvent *event) {
    if (object == this && _displayControls &&
        (event->type() == QEvent::ParentChange ||
         (event->type() == QEvent::DynamicPropertyChange &&
          static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "kumoDetachedArea")))
        applyAppearance();
    if (isVisible() && event->type() == QEvent::KeyPress && (object == _canvas || object == _marks)) {
        auto key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier &&
            (_tool->currentIndex() == 3 || object == _marks)) {
            if (auto remove = findChild<QToolButton *>("spectrumDeleteMarks"))
                remove->click();
            event->accept();
            return true;
        }
    }
    if (isVisible() && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ||
                        event->type() == QEvent::KeyRelease)) {
        auto key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space &&
            (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier) &&
            !qobject_cast<QLineEdit *>(object)) {
            event->accept();
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                if (key->modifiers() == Qt::ShiftModifier)
                    tapGrid();
                else
                    playSource();
            }
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}
void SpectrumPane::restoreGrid() {
    if (_beatControls)
        _beatControls->restore(_grids.value(_track.value("id").toString()).toMap());
}
void SpectrumPane::tapGrid() {
    const bool playing = sourceInPlayback();
    _beatControls->tap(playing ? sourcePosition() : _cursorMs, playing);
    _canvas->update();
    _editor->_noteCanvas->update();
}
void SpectrumPane::estimateTempo() {
    if (!_session || _path.isEmpty()) {
        setStatus(tr("先选择原曲音轨，再估计速度。"));
        return;
    }
    const double duration = _audio ? _audio->property("duration").toDouble() : 0.;
    const double from = _a >= 0 && _b > _a ? _a : qMax(0., timeAtX(gutter)),
                 until = _a >= 0 && _b > _a ? _b : qMin(duration, from + qBound(4000., _span, 120000.));
    const QVariantMap request{{"path", _path}, {"from", from}, {"until", until}};
    QMetaObject::invokeMethod(_session, "estimateTempo", Q_ARG(QVariantMap, request));
}
void SpectrumPane::tempoStateChanged() {
    _beatControls->setBusy(_session && _session->property("tempoBusy").toBool());
}
void SpectrumPane::tempoReady(const QVariantMap &result) {
    _beatControls->setCandidates(result);
    _canvas->update();
    _editor->_noteCanvas->update();
}
void SpectrumPane::paintGrid(QPainter &p, const QRectF &bounds) {
    const auto values = _beatControls->values();
    if (!values.value("visible").toBool())
        return;
    auto draw = [&](double x, bool bar, const QString &label) {
        if (x < gutter || x > bounds.right())
            return;
        p.setPen(QPen(QColor(210, 222, 235, bar ? 150 : 65), bar ? 1.2 : .7));
        p.drawLine(QPointF(x, 0), QPointF(x, bounds.height()));
        if (bar) {
            p.setPen(QColor(225, 233, 240));
            p.drawText(QPointF(x + 3, 12), label);
        }
    };
    if (values.value("source").toInt() == 1 && !_clip.isEmpty()) {
        const int from = _editor->tickForX(gutter, false), until = _editor->tickForX(bounds.right(), false);
        int drawn = 0;
        auto measure = std::lower_bound(_editor->_measures.cbegin(), _editor->_measures.cend(), from,
                                        [](const auto &m, int tick) { return m.until <= tick; });
        for (; measure != _editor->_measures.cend() && measure->from <= until && drawn < 1000; ++measure)
            for (int tick = measure->from; tick < measure->until && drawn < 1000;
                 tick += qMax(1, measure->beat)) {
                if (tick < from)
                    continue;
                ++drawn;
                const double x = _editor->xForTick(tick, false);
                draw(x, tick == measure->from, QString::number(measure->bar + 1));
            }
        return;
    }
    const double interval = 60000. / values.value("bpm", 120.).toDouble() * 4 /
                            values.value("denominator", 4).toInt(),
                 first = values.value("firstBeat", 0.).toDouble();
    const int numerator = values.value("numerator", 4).toInt();
    const double from = timeAtX(gutter), until = timeAtX(bounds.right());
    if (interval <= 0 || until <= from)
        return;
    const qint64 start = qint64(std::floor((from - first) / interval)),
                 end = qint64(std::ceil((until - first) / interval));
    const int stride =
        end - start > 500 ? qMax(1, int(std::ceil(double(end - start) / 500 / numerator)) * numerator) : 1;
    for (qint64 index = start - start % stride; index <= end; index += stride) {
        const double ms = first + index * interval;
        if (ms < 0)
            continue;
        const bool bar = index % numerator == 0;
        draw(xAtTime(ms), bar, index >= 0 ? QString::number(index / numerator + 1) : QString());
    }
}
QVariantMap SpectrumPane::pitchMark() const {
    QVariantMap identity;
    if (_audio)
        QMetaObject::invokeMethod(_audio, "identity", Q_RETURN_ARG(QVariantMap, identity));
    return {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"trackId", _track.value("id")},
            {"identity", identity},
            {"path", _path},
            {"ms", _pickMs},
            {"pickedMs", _pickMs},
            {"frequency", _pickFrequency},
            {"originalFrequency", _pickFrequency},
            {"pitch", qBound(0, qRound(_cursorPitch) + _transpose->value(), 127)},
            {"tuning", _tuning->value()},
            {"transpose", _transpose->value()}};
}
void SpectrumPane::selectMark(const QString &id) {
    for (auto item : _marks->findItems("*", Qt::MatchWildcard)) {
        item->setSelected(item->data(Qt::UserRole).toString() == id);
    }
    updateActions();
}
void SpectrumPane::markPitch() {
    if (_path.isEmpty() || !_editor->score())
        return;
    auto mark = pitchMark();
    auto info = _info;
    info["tuning"] = _tuning->value();
    info["fftSize"] = _request.value("fftSize");
    const auto extent = estimateSpectrumExtent(_rawImage, info, _pickMs, _pickFrequency);
    for (auto i = extent.cbegin(); i != extent.cend(); ++i)
        mark[i.key()] = i.value();
    _annotations.append(mark);
    saveMarks();
    selectMark(mark.value("id").toString());
}
void SpectrumPane::finishDrawnMark() {
    _drawingMark = false;
    _canvas->update();
    _editor->_noteCanvas->update();
    double from = qMin(_drawStart, _drawEnd), until = qMax(_drawStart, _drawEnd);
    if (until - from < 20)
        return;
    auto mark = pitchMark();
    int previous = -1;
    if (!_editingMark.isEmpty()) {
        for (int i = 0; i < _annotations.size(); ++i)
            if (_annotations[i].toMap().value("id").toString() == _editingMark) {
                previous = i;
                mark = _annotations[i].toMap();
                break;
            }
        if (previous < 0)
            return;
    }
    mark["ms"] = from;
    mark["until"] = until;
    mark["frequency"] = _drawFrequency;
    mark["originalFrequency"] = _drawOriginal;
    mark["pitch"] =
        qBound(0, qRound(69 + 12 * std::log2(_drawFrequency / _tuning->value())) + _transpose->value(), 127);
    mark["durationSource"] = "drawn";
    mark["manual"] = true;
    _hasPick = true;
    _pickMs = from;
    _pickFrequency = _drawFrequency;
    _cursorPitch = 69 + 12 * std::log2(_drawFrequency / _tuning->value());
    if (previous < 0)
        _annotations.append(mark);
    else
        _annotations[previous] = mark;
    saveMarks();
    selectMark(mark.value("id").toString());
}
void SpectrumPane::updateInputTarget() {
    if (!_target)
        return;
    auto score = _editor->score();
    if (!score || !score->inputState().segment()) {
        _target->setText(tr("写入位置：先在乐谱中选择音符或休止符。"));
        return;
    }
    const auto &input = score->inputState();
    auto measure = input.segment()->measure();
    const int track = input.track();
    const QString text = tr("写入：第 %1 小节 · 谱表 %2 · 声部 %3 · %4")
                             .arg(measure ? measure->no() + 1 : 1)
                             .arg(track / 4 + 1)
                             .arg(track % 4 + 1)
                             .arg(_durationFromMark && _durationFromMark->isChecked()
                                      ? tr("按标注长度估计时值")
                                      : input.duration().durationTypeUserName());
    _target->setText(text);
    _target->setToolTip(text);
}
void SpectrumPane::ensureSessions() {
    if (_session)
        return;
    _media = referenceProject();
    connect(_media, SIGNAL(tracksChanged()), this, SLOT(refreshTracks()));
    connect(_media, SIGNAL(changed()), this, SLOT(projectChanged()));
    _session = createReferenceSpectrum(this);
    _session->setObjectName("transcriptionSpectrumSession");
    connect(_session, SIGNAL(changed()), this, SLOT(spectrumChanged()));
    connect(_session, SIGNAL(tempoStateChanged()), this, SLOT(tempoStateChanged()));
    connect(_session, SIGNAL(tempoReady(QVariantMap)), this, SLOT(tempoReady(QVariantMap)));
    _audio = createReferenceMedia(this);
    _audio->setProperty("waveformEnabled", false);
    connect(_audio, SIGNAL(changed()), this, SLOT(audioChanged()));
    if (!_session->property("available").toBool()) {
        setStatus(_session->property("status").toString());
        for (const char *name : {"transcriptionInput", "transcriptionMark"})
            if (auto button = findChild<QToolButton *>(name))
                button->setEnabled(false);
    }
}
void SpectrumPane::applyAppearance() {
    if (property("kumoDetachedArea").toBool()) {
        setStyleSheet(QString());
        setPalette(QApplication::palette());
        setAutoFillBackground(true);
        return;
    }
    const auto &colors = _editor->_appearance.colors;
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString("QWidget#transcriptionSpectrumPane {background:%1;} QListWidget,QSpinBox "
                          "{background:%1;color:%2;border:1px solid %3;} QListWidget::item:selected "
                          "{background:%4;color:%2;}")
                      .arg(colors[PerformanceAppearance::Background].name(),
                           colors[PerformanceAppearance::Text].name(),
                           colors[PerformanceAppearance::Grid].name(), palette().highlight().color().name()));
}
void SpectrumPane::refreshTracks() {
    if (!_media || !isVisible())
        return;
    auto tracks = workflowContext()->referenceCurrent(_editor->score()) ? _media->property("tracks").toList()
                                                                        : QVariantList();
    QStringList ids;
    for (auto value : tracks)
        ids.append(value.toMap().value("id").toString());
    QStringList old;
    for (int i = 0; i < _tracks->count(); ++i)
        if (!_tracks->itemData(i).toString().isEmpty())
            old.append(_tracks->itemData(i).toString());
    const int wanted =
        tracks.isEmpty() ? -1 : qBound(0, _media->property("selectedTrack").toInt(), tracks.size() - 1);
    const int displayedIndex = tracks.isEmpty() ? 0 : wanted;
    if(ids==old&&_tracks->currentIndex()==displayedIndex&&wanted>=0){
        auto current=tracks[wanted].toMap(),previous=_track;
        for(const char* field:{"volume","mute","solo","participate"}){current.remove(field);previous.remove(field);}
        if(current==previous){_track=tracks[wanted].toMap();return;}
    }
    if (ids == old && _tracks->currentIndex() == displayedIndex &&
        (wanted < 0 ? _track.isEmpty() : tracks[wanted].toMap() == _track))
        return;
    QString selected = wanted < 0 ? QString() : tracks[wanted].toMap().value("id").toString();
    {
        QSignalBlocker block(_tracks);
        _tracks->clear();
        if (tracks.isEmpty())
            _tracks->addItem(tr("先打开音频"), QString());
        for (auto value : tracks) {
            auto t = value.toMap();
            _tracks->addItem(t.value("name").toString(), t.value("id"));
        }
    }
    _tracks->setEnabled(!tracks.isEmpty());
    int index = _tracks->findData(selected);
    if (index < 0)
        index = qBound(0, _media->property("selectedTrack").toInt(), qMax(0, _tracks->count() - 1));
    {
        QSignalBlocker block(_tracks);
        _tracks->setCurrentIndex(tracks.isEmpty() ? 0 : index);
    }
    QMetaObject::invokeMethod(_tracks, "currentIndexChanged", Q_ARG(int, tracks.isEmpty() ? -1 : _tracks->currentIndex()));
}
void SpectrumPane::audioChanged() {
    const auto wanted=QUrl(_path).isLocalFile()?QUrl(_path).toLocalFile():_path;
    const bool ready=!_path.isEmpty()&&!_audio->property("reading").toBool()&&_audio->property("duration").toDouble()>0&&QFileInfo(_audio->property("path").toString()).absoluteFilePath()==QFileInfo(wanted).absoluteFilePath();
    _play->setEnabled(ready||(_media&&_media->property("playing").toBool()));
    const auto fileStatus = _audio->property("status").toString();
    if (!ready && (fileStatus.startsWith("Cannot")||fileStatus.startsWith(QString::fromUtf8("无法读取")))) {
        setStatus(tr("无法读取这份音频。请在原曲参考中检查文件位置。"));
        _status->setToolTip(fileStatus);
    }
    if (!_audioLoaded && ready) {
        _audioLoaded = true;
        _span = qBound(250., _audio->property("duration").toDouble(), 8000.);
        viewChanged();
    }
    updateActions();
    _canvas->update();
    _editor->_noteCanvas->update();
}
bool SpectrumPane::ownsPlayback() const {
    return _media && _media->property("workflowPlaybackOwner").toString() == _playOwner;
}
bool SpectrumPane::sourceInPlayback() const {
    if (!_media || !_media->property("playing").toBool())
        return false;
    for (auto value : _media->property("workflowPlaybackPlan").toMap().value("clips").toList()) {
        const auto clip=value.toMap();
        if(clip.contains("id")&&!_track.value("id").toString().isEmpty()&&clip.value("id")!=_track.value("id"))continue;
        const auto text = value.toMap().value("path").toString();
        const auto path = QUrl(text).isLocalFile() ? QUrl(text).toLocalFile() : text;
        if (QFileInfo(path).absoluteFilePath() == QFileInfo(_path).absoluteFilePath())
            return true;
    }
    return false;
}
int SpectrumPane::linkedTimelineWidth() const { return isVisible() && !_clip.isEmpty() ? sourceWidth() : -1; }
double SpectrumPane::sourcePosition() const {
    if (_media && _media->property("playing").toBool()) {
        const double at = _media->property("position").toDouble();
        const auto plan = _media->property("workflowPlaybackPlan").toMap();
        for (auto value : plan.value("clips").toList()) {
            const auto clip = value.toMap();
            if(clip.contains("id")&&!_track.value("id").toString().isEmpty()&&clip.value("id")!=_track.value("id"))continue;
            const auto path = QUrl(clip.value("path").toString()).isLocalFile()
                                  ? QUrl(clip.value("path").toString()).toLocalFile()
                                  : clip.value("path").toString();
            if (QFileInfo(path).absoluteFilePath() != QFileInfo(_path).absoluteFilePath())
                continue;
            for (auto pieceValue : clip.value("pieces").toList()) {
                const auto piece = pieceValue.toMap();
                const double start = piece.value("at").toDouble(),
                             duration = piece.value("duration").toDouble();
                if (at >= start && at < start + duration)
                    return piece.value("from").toDouble() +
                           (at - start) / duration *
                               (piece.value("until").toDouble() - piece.value("from").toDouble());
            }
        }
    }
    if (ownsPlayback() && _media->property("playing").toBool())
        return qBound(_playStart, _playStart + _media->property("position").toDouble() * _playSpeed,
                      _playUntil);
    return _lastAudioMs;
}
void SpectrumPane::playSource() {
    if (!_media || !_audio)
        return;
    if (_media->property("playing").toBool()) {
        const double at = sourcePosition();
        if(_media->property("joint").toBool()){PluginAPI::ScoreObserver observer;observer.stopReferencePlayback();}
        else QMetaObject::invokeMethod(_media, "stop");
        _cursorMs = at;
        setStatus(tr("原曲已暂停"));
        return;
    }
    if(!_audioLoaded||_audio->property("reading").toBool()){setStatus(tr("音频读取完成后即可播放；缺失文件可在原曲参考中找回。"));return;}
    const double begin = _loop->isChecked() && _a >= 0 ? _a
                         : _clip.isEmpty()             ? 0
                                                       : _clip.value("from").toDouble();
    const double until = _loop->isChecked() && _b > _a ? _b
                         : _clip.isEmpty()             ? _audio->property("duration").toDouble()
                                                       : _clip.value("until").toDouble();
    double from = _loop->isChecked() ? begin : _cursorMs >= begin && _cursorMs < until ? _cursorMs : begin;
    if (_media->property("completed").toBool())
        from = begin;
    _playStart = from;
    _playUntil = until;
    _playSpeed = _speed->value() / 100.;
    _lastAudioMs = from;
    const QVariantMap piece{{"from", from},
                            {"until", until},
                            {"at", 0.},
                            {"duration", (until - from) / _playSpeed},
                            {"fade", true}};
    const QVariantMap clip{{"id", _track.value("id")},
                           {"path", _path},
                           {"identity", _track.value("identity")},
                           {"listen", _listenControls->values()},
                           {"volume", _track.value("volume", 1.)},
                           {"pieces", QVariantList{piece}}};
    const QVariantMap plan{{"owner", _playOwner},
                           {"clips", QVariantList{clip}},
                           {"joint", false},
                           {"loop", _loop->isChecked()},
                           {"volume", 1.}};
    QVariantMap result;
    const bool called = QMetaObject::invokeMethod(_media, "start", Q_RETURN_ARG(QVariantMap, result),
                                                  Q_ARG(QVariantMap, plan));
    if (!called || result.contains("error"))
        setStatus(called ? result.value("error").toString() : tr("原曲音频模块尚未就绪。"));
    else
        setStatus(tr("正在播放所选原曲片段"));
}
void SpectrumPane::seekSource(double ms) {
    const bool playing = ownsPlayback() && _media->property("playing").toBool();
    if (playing)
        QMetaObject::invokeMethod(_media, "stop");
    _cursorMs = ms;
    if (_audio)
        QMetaObject::invokeMethod(_audio, "seek", Q_ARG(double, ms));
    if (playing)
        playSource();
}
void SpectrumPane::projectChanged() {
    const bool playing = _media->property("playing").toBool();
    _play->setText(playing ? tr("暂停原曲") : tr("播放原曲"));
    setComponentToolIcon(_play, playing ? "pause" : "play");
    if (playing)
        _lastAudioMs = sourcePosition();
    else {
        _cursorMs = _lastAudioMs;
        setStatus(_media->property("status").toString());
    }
    if (isVisible()) {
        _canvas->update();
        _editor->_noteCanvas->update();
    }
}
void SpectrumPane::showEvent(QShowEvent *) {
    ensureSessions();
    refreshTracks();
    if (_metadataCancelled && _audio && _audio->property("available").toBool() && !_path.isEmpty()) {
        _metadataCancelled = false;
        QMetaObject::invokeMethod(_audio, "open", Q_ARG(QString, _path));
    }
    scoreChanged();
    viewChanged();
}
void SpectrumPane::hideEvent(QHideEvent *) {
    for (auto owner = static_cast<QWidget*>(this); owner; owner = owner->parentWidget())
        if (owner->property("kumoAreaMoving").toBool() || owner->property("kumoWindowTransition").toBool())
            return;
    _drawingMark = false;
    _listenTimer.stop();
    _requestTimer.stop();
    _request.clear();
    if (_session)
        QMetaObject::invokeMethod(_session, "cancel");
    if (ownsPlayback())
        QMetaObject::invokeMethod(_media, "stop");
    if (_audio && !_audioLoaded && _session && _session->property("available").toBool()) {
        _metadataCancelled = true;
        QMetaObject::invokeMethod(_audio, "cancelRead");
    }
    _editor->surfaceResized(PerformanceSurface::Parameter);
}
void SpectrumPane::scoreChanged() {
    const auto score = _editor->score();
    const auto text = score ? score->masterScore()->metaTag("kumo.transcription") : QString();
    if (score != _documentScore) {
        const auto document = QJsonDocument::fromJson(text.toUtf8()).toVariant().toMap();
        _grids = document.value("grids").toMap();
        _annotations = document.value("pitches").toList();
        restoreGrid();
        refreshMarks();
        _documentScore = score;
        _drawingMark = false;
        _editingMark.clear();
        _hasPick = false;
        const auto view = QJsonDocument::fromJson(text.toUtf8()).toVariant().toMap().value("view").toMap();
        if (!view.isEmpty()) {
            QSignalBlocker a(_tuning), b(_transpose);
            _tuning->setValue(view.value("tuning", 440).toDouble());
            _transpose->setValue(view.value("transpose", 0).toInt());
        }
    }
    if (text != _savedData) {
        _savedData = text;
        const auto data = QJsonDocument::fromJson(text.toUtf8()).toVariant().toMap();
        _annotations = data.value("pitches").toList();
        const auto grids = data.value("grids").toMap();
        if (grids != _grids) {
            _grids = grids;
            restoreGrid();
        }
        refreshMarks();
    }
    refreshTracks();
    viewChanged();
}
void SpectrumPane::viewChanged() {
    if (!isVisible())
        return;
    const int width = linkedTimelineWidth();
    if (property("linkedTimelineWidth").toInt() != width) {
        setProperty("linkedTimelineWidth", width);
        _editor->surfaceResized(PerformanceSurface::Parameter);
    }
    updateActions();
    const auto score = _editor->score();
    const auto text = score ? score->masterScore()->metaTag("kumo.transcription") : QString();
    if (text != _savedData) {
        _savedData = text;
        const auto data = QJsonDocument::fromJson(text.toUtf8()).toVariant().toMap();
        _annotations = data.value("pitches").toList();
        const auto grids = data.value("grids").toMap();
        if (grids != _grids) {
            _grids = grids;
            restoreGrid();
        }
        refreshMarks();
    }
    if (!_requestTimer.isActive())
        _requestTimer.start();
    syncScrollBars();
    _canvas->update();
}
void SpectrumPane::editorTimelineZoomed(double previousScale, double anchorX) {
    if (!isVisible() || !_clip.isEmpty() || _display->currentIndex() != 1 || previousScale <= 0)
        return;
    // Source-time overlays keep their own time origin, but share the editor's
    // horizontal magnification. All editor zoom controls use this path.
    const double anchor = timeAtX(anchorX);
    const double ratio = (anchorX - gutter) / plotWidth();
    _span = qBound(250., _span * previousScale / _editor->_viewport.pixelsPerQuarter, 120000.);
    _from = qMax(0., anchor - ratio * _span);
}
double SpectrumPane::timeAtX(double x) const {
    return _clip.isEmpty() ? _from + (x - gutter) / plotWidth() * _span
                           : audioAt(_clip, _editor->tickForX(x, false));
}
double SpectrumPane::xAtTime(double ms) const {
    return _clip.isEmpty() ? gutter + (ms - _from) / _span * plotWidth()
                           : _editor->xForTick(qRound(tickAt(_clip, ms)), false);
}
double SpectrumPane::pitchAtY(double y, bool overlay) const {
    return overlay
               ? _editor->_viewport.pitchAtIndex(_editor->_viewport.pitchIndex(_editor->_viewport.topPitch) -
                                                 y / _editor->_viewport.rowHeight + .5) -
                     _transpose->value()
               : _high - y / qMax(1, _canvas->height()) * (_high - _low);
}
double SpectrumPane::yAtPitch(double pitch, bool overlay) const {
    return overlay ? _editor->_viewport.pitchY(pitch + _transpose->value()) + _editor->_viewport.rowHeight / 2
                   : (_high - pitch) / (_high - _low) * _canvas->height();
}
void SpectrumPane::requestVisible() {
    if (_path.isEmpty() || !_session || !_session->property("available").toBool())
        return;
    double from = timeAtX(gutter), until = timeAtX(sourceWidth() - PerformanceViewport::rightMargin);
    if (!_clip.isEmpty()) {
        from = qMax(_clip.value("from").toDouble(), from);
        until = qMin(_clip.value("until").toDouble(), until);
    }
    if (until <= from)
        return;
    // One bounded viewport; horizontal requests are quantized to avoid repaint-sized recomputation.
    from = qMax(0., std::floor(from / 20.) * 20.);
    until = qMin(from + 120000., std::ceil(until / 20.) * 20.);
    QVariantMap request{{"path", _path},
                        {"from", from},
                        {"until", until},
                        {"columns", qBound(64, (sourceWidth() - gutter) / 2, 1024)},
                        {"rows", 512},
                        {"lowPitch", 0.},
                        {"highPitch", 128.},
                        {"fundamentalMix", _displayControls->values().value("fundamental", 0.)},
                        {"fftSize", _preset->currentIndex() == 1   ? 32768
                                    : _preset->currentIndex() == 2 ? 4096
                                                                   : 16384},
                        {"tuning", _tuning->value()}};
    if (request == _request && !_forceSpectrum)
        return;
    _request = request;
    if (_forceSpectrum) {
        request["refresh"] = true;
        _forceSpectrum = false;
    }
    QMetaObject::invokeMethod(_session, "request", Q_ARG(QVariantMap, request));
}
void SpectrumPane::spectrumChanged() {
    const auto info = _session->property("info").toMap();
    const auto local = QUrl(_path).isLocalFile() ? QUrl(_path).toLocalFile() : _path;
    if (QFileInfo(info.value("sourcePath").toString()).absoluteFilePath() !=
        QFileInfo(local).absoluteFilePath()) {
        setStatus(_session->property("status").toString());
        return;
    }
    const auto next = qvariant_cast<QImage>(_session->property("image"));
    const bool changed = _rawImage.cacheKey() != next.cacheKey();
    _rawImage = next;
    updateActions();
    _info = _session->property("info").toMap();
    QString status = _session->property("status").toString();
    if (_hasPick && !_session->property("busy").toBool() && !_image.isNull())
        status = tr("已选 %1 · %2 秒 · %3 Hz · ")
                     .arg(pitchName(qBound(0, qRound(_cursorPitch) + _transpose->value(), 127)))
                     .arg(_pickMs / 1000., 0, 'f', 3)
                     .arg(_pickFrequency, 0, 'f', 1) +
                 status;
    setStatus(status);
    if (changed)
        recolor();
}
void SpectrumPane::paint(QPainter &p, const QRectF &bounds, bool overlay) {
    p.save();
    if (!overlay) {
        p.fillRect(QRectF(0, 0, gutter, bounds.height()), palette().window());
        const double row = bounds.height() / (_high - _low), top = _high - .5;
        QVector<QPair<int, QRectF>> labels;
        p.save();
        p.setClipRect(QRectF(0, 0, gutter, bounds.height()));
        for (bool black : {false, true})
            for (int pitch = qMax(0, int(std::floor(_low)) - 1);
                 pitch <= qMin(127, int(std::ceil(_high)) + 1); ++pitch) {
                const int shown = pitch + _transpose->value();
                if (shown < 0 || shown > 127 || PerformanceKeyboard::isBlack(shown) != black)
                    continue;
                const auto shape = PerformanceKeyboard::shape(shown, top + _transpose->value(), row, gutter);
                p.setBrush(QColor(black ? "#161616" : "#f1f1f1"));
                if (row < 3)
                    p.setPen(Qt::NoPen);
                else
                    p.setPen(QColor(black ? "#161616" : "#777777"));
                p.drawPolygon(shape);
                if (row >= 10 || shown % 12 == 0)
                    labels.append(qMakePair(shown, shape.boundingRect()));
            }
        // Adjacent keys can cover text at small row heights. Finish the key
        // shapes first, then center labels in the visible part of each key.
        auto font = p.font();
        font.setPixelSize(qBound(7, int(row * .82), 12));
        p.setFont(font);
        const double labelHeight = qMax(row, double(p.fontMetrics().height() + 2));
        for (const auto &label : labels) {
            const double center = label.second.center().y();
            if (center < 0 || center > bounds.height())
                continue;
            const bool black = PerformanceKeyboard::isBlack(label.first);
            p.setPen(QColor(black ? "#ededed" : "#292929"));
            const double y = qBound(0., center - labelHeight / 2,
                                    qMax(0., bounds.height() - labelHeight));
            p.drawText(QRectF(black ? 2 : gutter * .58 + 2, y,
                              gutter * (black ? .58 : .42) - 4, labelHeight),
                       Qt::AlignCenter, pitchName(label.first));
        }
        p.restore();
    }
    p.setClipRect(bounds.adjusted(gutter, 0, -PerformanceViewport::rightMargin, 0));
    if (!overlay)
        p.fillRect(bounds, QColor("#181c20"));
    const bool outsideClip = !_clip.isEmpty() &&
        (_editor->tickForX(bounds.right() - PerformanceViewport::rightMargin, false) <= _clip.value("start").toInt() ||
         _editor->tickForX(gutter, false) >= _clip.value("end").toInt());
    if (outsideClip) {
        p.setPen(QColor("#cbd1d7"));
        p.drawText(bounds.adjusted(gutter + 12, 8, -24, -8), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("当前视口在所选关联片段之外\n点击“匹配片段”回到这一段，或切换到“整条录音”。"));
    }
    if (!_image.isNull()) {
        p.save();
        if (!_clip.isEmpty()) {
            const double left = _editor->xForTick(_clip.value("start").toInt(), false);
            const double right = _editor->xForTick(_clip.value("end").toInt(), false);
            p.setClipRect(QRectF(left, bounds.top(), right - left, bounds.height()), Qt::IntersectClip);
        }
        // Draw each linear alignment interval as one image piece; reuse the
        // same pitch mapping as compressed keyboards in overlay mode.
        QVector<double> cuts{_info.value("from").toDouble()};
        if (!_clip.isEmpty())
            for (auto value : _clip.value("anchors").toList()) {
                const double ms = value.toMap().value("ms").toDouble();
                if (ms > cuts.front() && ms < _info.value("until").toDouble())
                    cuts.append(ms);
            }
        cuts.append(_info.value("until").toDouble());
        std::sort(cuts.begin(), cuts.end());
        const double high = _info.value("highPitch").toDouble(), low = _info.value("lowPitch").toDouble();
        auto sourceY = [&](double pitch) { return (high - pitch) / qMax(.01, high - low) * _image.height(); };
        p.setOpacity(1.);
        for (int i = 1; i < cuts.size(); ++i) {
            double x0 = xAtTime(cuts[i - 1]), x1 = xAtTime(cuts[i]);
            if (x1 <= gutter || x0 >= bounds.right() || x1 <= x0)
                continue;
            const double sx = (cuts[i - 1] - cuts.front()) / qMax(.001, cuts.back() - cuts.front()) *
                              _image.width(),
                         sw = (cuts[i] - cuts[i - 1]) / qMax(.001, cuts.back() - cuts.front()) *
                              _image.width();
            if (overlay && !_editor->_viewport.pitchRows.isEmpty())
                for (int pitch : _editor->_viewport.pitchRows) {
                    double top = yAtPitch(pitch + .5 - _transpose->value(), true),
                           bottom = yAtPitch(pitch - .5 - _transpose->value(), true);
                    if (bottom < 0 || top > bounds.bottom())
                        continue;
                    p.drawImage(QRectF(x0, top, x1 - x0, bottom - top), _image,
                                QRectF(sx, sourceY(pitch + .5 - _transpose->value()), sw,
                                       sourceY(pitch - .5 - _transpose->value()) -
                                           sourceY(pitch + .5 - _transpose->value())));
                }
            else {
                const double top = yAtPitch(high, overlay), bottom = yAtPitch(low, overlay);
                p.drawImage(QRectF(x0, top, x1 - x0, bottom - top), _image,
                            QRectF(sx, 0, sw, _image.height()));
            }
        }
        p.restore();
    }
    p.setOpacity(1);
    paintGrid(p, bounds);
    const QColor accent = palette().highlight().color();
    if (_a >= 0 && _b > _a) {
        QColor c = accent;
        c.setAlpha(35);
        p.fillRect(QRectF(xAtTime(_a), 0, xAtTime(_b) - xAtTime(_a), bounds.height()), c);
    }
    if (_harmonics->isChecked() && _hasPick)
        for (int harmonic = 1; harmonic <= 10; ++harmonic) {
            double pitch = _cursorPitch + 12. * std::log2(harmonic);
            double y = yAtPitch(pitch, overlay);
            p.setPen(QPen(QColor(180, 192, 205, harmonic == 1 ? 180 : 90), 1,
                          harmonic == 1 ? Qt::SolidLine : Qt::DashLine));
            p.drawLine(QPointF(gutter, y), QPointF(bounds.right(), y));
        }
    p.setPen(accent);
    for (auto value : _annotations) {
        auto m = value.toMap();
        if (m.value("trackId") != _track.value("id"))
            continue;
        double x = xAtTime(m.value("ms").toDouble()),
               pitch = 69. + 12. * std::log2(m.value("frequency", 440).toDouble() / _tuning->value());
        const double y = yAtPitch(pitch, overlay), end = m.value("until", m.value("ms")).toDouble();
        if (end > m.value("ms").toDouble()) {
            const QRectF note(x, y - 4, qMax(2., xAtTime(end) - x), 8);
            QColor fill = accent;
            fill.setAlpha(45);
            p.setBrush(fill);
            p.drawRoundedRect(note, 2, 2);
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(note.left(), y - 7), QPointF(note.left(), y + 7));
            p.drawLine(QPointF(note.right(), y - 7), QPointF(note.right(), y + 7));
        } else
            p.drawEllipse(QPointF(x, y), 4, 4);
    }
    if (_drawingMark) {
        const double y = yAtPitch(69 + 12 * std::log2(_drawFrequency / _tuning->value()), overlay);
        QColor fill = accent;
        fill.setAlpha(65);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(QPointF(xAtTime(qMin(_drawStart, _drawEnd)), y - 5),
                                 QPointF(xAtTime(qMax(_drawStart, _drawEnd)), y + 5)),
                          2, 2);
        p.setBrush(Qt::NoBrush);
    }
    if (_hasPick) {
        const double cursor = xAtTime(_pickMs);
        p.setPen(QPen(accent, 1));
        p.drawLine(QPointF(cursor, 0), QPointF(cursor, bounds.height()));
        p.drawEllipse(QPointF(cursor, yAtPitch(_cursorPitch, overlay)), 4, 4);
    }
    if (sourceInPlayback()) {
        const double play = xAtTime(sourcePosition());
        p.setPen(QPen(palette().highlight().color(), 2));
        p.drawLine(QPointF(play, 0), QPointF(play, bounds.height()));
    }
    if (!overlay) {
        p.setPen(QColor("#d9dfe5"));
        const int divisions = qBound(1, plotWidth() / 160, 6);
        for (int i = 0; i <= divisions; ++i) {
            const double x = gutter + double(plotWidth()) * i / divisions;
            if (!_clip.isEmpty() &&
                (x < _editor->xForTick(_clip.value("start").toInt(), false) - .5 ||
                 x > _editor->xForTick(_clip.value("end").toInt(), false) + .5))
                continue;
            const auto text = QString::number(timeAtX(x) / 1000., 'f', 2) + tr("秒");
            const double left = qMin(x + 3, double(sourceWidth() - PerformanceViewport::rightMargin - p.fontMetrics().horizontalAdvance(text) - 3));
            p.drawText(QPointF(left, _beatControls->values().value("visible").toBool() ? 26 : 13), text);
        }
    }
    p.restore();
}
void SpectrumPane::paintOverlay(QPainter &p, const QRectF &rect) {
    if (isVisible() && _display->currentIndex() == 1)
        paint(p, rect, true);
}
void SpectrumPane::pointer(QMouseEvent *e, bool release) {
    if (e->x() < gutter && _display->currentIndex() == 0) {
        if (e->type() == QEvent::MouseButtonPress && e->button() == Qt::LeftButton) {
            const int pitch = PerformanceKeyboard::pitchAt(e->localPos(), _high - .5 + _transpose->value(),
                                                           _canvas->height() / (_high - _low), gutter);
            if (pitch >= 0) {
                const bool enabled = _editor->_auditionButton->isChecked();
                _editor->_auditionButton->setChecked(true);
                _editor->auditionPitch(pitch);
                _editor->_auditionButton->setChecked(enabled);
                setStatus(tr("试音 %1").arg(pitchName(pitch)));
            }
        }
        e->accept();
        return;
    }
    if (_path.isEmpty() || _image.isNull()) {
        e->accept();
        return;
    }
    const bool overlay = _display->currentIndex() == 1;
    if (!_clip.isEmpty() && e->type() == QEvent::MouseButtonPress &&
        e->button() == Qt::LeftButton && _tool->currentIndex() != 2 &&
        (e->x() < _editor->xForTick(_clip.value("start").toInt(), false) ||
         e->x() > _editor->xForTick(_clip.value("end").toInt(), false))) {
        _hasPick = false;
        updateActions();
        setStatus(tr("此处在当前关联片段之外。点击“匹配片段”返回，或切换到“整条录音”。"));
        _canvas->update();
        _editor->_noteCanvas->update();
        e->accept();
        return;
    }
    const double ms = qMax(0., timeAtX(e->x())), pitch = pitchAtY(e->y(), overlay),
                 frequency = _tuning->value() * std::pow(2., (pitch - 69.) / 12.);
    if (_tool->currentIndex() == 3 && e->button() != Qt::RightButton) {
        if (e->type() == QEvent::MouseButtonPress && e->button() == Qt::LeftButton) {
            _drawingMark = true;
            _editingMark.clear();
            _hasPick = true;
            _pickMs = ms;
            _pickFrequency = frequency;
            _cursorPitch = pitch;
            _drawStart = _drawEnd = ms;
            _drawFrequency = _drawOriginal = frequency;
            _drawEdge = 1;
            for (int i = _annotations.size() - 1; i >= 0; --i) {
                const auto mark = _annotations[i].toMap();
                if (mark.value("trackId") != _track.value("id"))
                    continue;
                const double begin = mark.value("ms").toDouble(), end = mark.value("until", begin).toDouble(),
                             f = mark.value("frequency", 440.).toDouble(),
                             y = yAtPitch(69 + 12 * std::log2(f / _tuning->value()), overlay);
                if (qAbs(e->y() - y) > 8 || e->x() < xAtTime(begin) - 6 || e->x() > xAtTime(end) + 6)
                    continue;
                _editingMark = mark.value("id").toString();
                _drawStart = begin;
                _drawEnd = end;
                _drawFrequency = f;
                _drawOriginal = mark.value("originalFrequency", f).toDouble();
                _drawEdge = qAbs(e->x() - xAtTime(begin)) < 6 ? 0 : qAbs(e->x() - xAtTime(end)) < 6 ? 1 : 2;
                _drawOffset = ms - begin;
                selectMark(_editingMark);
                break;
            }
            e->accept();
            return;
        }
        if (_drawingMark) {
            const double limit = _audio ? _audio->property("duration").toDouble() : ms;
            const double point = qBound(0., ms, limit > 0 ? limit : ms);
            if (_drawEdge == 0)
                _drawStart = point;
            else if (_drawEdge == 1)
                _drawEnd = point;
            else {
                const double length = _drawEnd - _drawStart;
                _drawStart = qMax(0., point - _drawOffset);
                _drawEnd = _drawStart + length;
                _drawFrequency = frequency;
            }
            _canvas->update();
            _editor->_noteCanvas->update();
            if (release)
                finishDrawnMark();
            e->accept();
            return;
        }
    }
    if (e->type() == QEvent::MouseButtonPress) {
        _press = e->localPos();
        _pressFrom = _from;
        _panning = e->button() == Qt::MiddleButton || _tool->currentIndex() == 2;
        _dragging = true;
        if (_tool->currentIndex() == 1 && !_panning) {
            _a = ms;
            _b = ms;
        }
        if (e->button() == Qt::LeftButton && _tool->currentIndex() == 0 && !_panning && e->x() >= gutter) {
            _hasPick = true;
            _pickMs = ms;
            _pickFrequency = frequency;
            _listenControls->setFrequency(_pickFrequency);
            _cursorMs = ms;
            _cursorPitch = pitch;
            seekSource(ms);
            const bool enabled = _editor->_auditionButton->isChecked();
            _editor->_auditionButton->setChecked(true);
            _editor->auditionPitch(qBound(0, qRound(pitch) + _transpose->value(), 127));
            _editor->_auditionButton->setChecked(enabled);
            updateActions();
        }
    }
    if (_panning && _dragging) {
        const double delta = (e->x() - _press.x()) / plotWidth() * _span;
        if (_clip.isEmpty())
            _from = qMax(0., _pressFrom - delta);
        else
            _editor->_scroll->setValue(
                _editor->_scroll->value() -
                qRound((e->x() - _press.x()) / _editor->_viewport.pixelsPerQuarter * DIVISION));
        _press = e->localPos();
        _pressFrom = _from;
        viewChanged();
    } else if (_dragging && _tool->currentIndex() == 1) {
        _b = ms;
        setStatus(tr("循环片段 %1–%2 秒")
                      .arg(qMin(_a, _b) / 1000., 0, 'f', 3)
                      .arg(qMax(_a, _b) / 1000., 0, 'f', 3));
    } else {
        QString text = tr("鼠标 %1 秒 · %2 Hz · %3")
                           .arg(ms / 1000., 0, 'f', 3)
                           .arg(frequency, 0, 'f', 1)
                           .arg(pitchName(qBound(0, qRound(pitch) + _transpose->value(), 127)));
        if (_hasPick)
            text = tr("已选 %1 · %2 秒 · ")
                       .arg(pitchName(qBound(0, qRound(_cursorPitch) + _transpose->value(), 127)))
                       .arg(_pickMs / 1000., 0, 'f', 3) +
                   text;
        setStatus(text);
    }
    _canvas->update();
    _editor->_noteCanvas->update();
    if (release) {
        _dragging = false;
        _panning = false;
        if (_b < _a)
            std::swap(_a, _b);
        if (_loop->isChecked() && _b > _a && ownsPlayback() && _media->property("playing").toBool()) {
            QMetaObject::invokeMethod(_media, "stop");
            _cursorMs = _a;
            playSource();
        }
    }
    e->accept();
}
bool SpectrumPane::overlayEvent(QEvent *event) {
    if (!isVisible() || _display->currentIndex() != 1)
        return false;
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        auto key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space &&
            (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier)) {
            event->accept();
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                if (key->modifiers() == Qt::ShiftModifier)
                    tapGrid();
                else
                    playSource();
            }
            return true;
        }
    }
    if (event->type() == QEvent::Wheel) {
        auto e = static_cast<QWheelEvent *>(event);
        if ((e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier) && e->posF().x() >= gutter) {
            wheel(e);
            return true;
        }
    }
    // Alt keeps ordinary roll selection available while the transcription page is open.
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonRelease) {
        auto e = static_cast<QMouseEvent *>(event);
        if (e->modifiers() & Qt::AltModifier)
            return false;
        pointer(e, event->type() == QEvent::MouseButtonRelease);
        return true;
    }
    return false;
}
void SpectrumPane::wheel(QWheelEvent *e) {
    const double factor = std::pow(1.2, e->angleDelta().y() / 120.);
    if (e->modifiers() & Qt::ShiftModifier) {
        double centre = pitchAtY(e->posF().y()), span = qBound(12., (_high - _low) / factor, 108.);
        _low = qBound(0., centre - span / 2, 128. - span);
        _high = _low + span;
    } else if (e->modifiers() & Qt::ControlModifier) {
        if (_clip.isEmpty() && _display->currentIndex() != 1) {
            double anchor = timeAtX(e->posF().x()), ratio = (anchor - _from) / _span;
            _span = qBound(250., _span / factor, 120000.);
            _from = qMax(0., anchor - ratio * _span);
        } else {
            _editor->zoomTimeline(factor, e->posF().x());
        }
    } else {
        if (_clip.isEmpty())
            _from = qMax(0., _from - e->angleDelta().y() / 120. * _span * .15);
        else
            _editor->_scroll->setValue(_editor->_scroll->value() - e->angleDelta().y() * 2);
    }
    viewChanged();
    e->accept();
}
int SpectrumPane::inputPitch(const QVariantMap &mark) const {
    return mark.value("pitch").toInt() + _transpose->value() - mark.value("transpose", 0).toInt();
}
void SpectrumPane::updateActions() {
    updateInputTarget();
    _fitTime->setEnabled(!_path.isEmpty() && _audioLoaded && _audio && !_audio->property("reading").toBool());
    _fitTime->setText(_clip.isEmpty() ? tr("匹配音频") : tr("匹配片段"));
    _fitTime->setToolTip(_clip.isEmpty() ? tr("扩大可见原曲范围；用底部滚动条浏览整条录音。")
                                     : tr("将当前关联片段放到视口内，卷帘与频谱一起调整。"));
    if (auto remove = findChild<QToolButton *>("spectrumDeleteMarks"))
        remove->setEnabled(_marks && !_marks->selectedItems().isEmpty());
    const bool editable =
        _editor->score() && !_editor->score()->masterScore()->readOnly() && (!seq || !seq->isPlaying());
    if (auto button = findChild<QToolButton *>("transcriptionMark"))
        button->setEnabled(editable && _hasPick && !_image.isNull());
    if (auto button = findChild<QToolButton *>("transcriptionInput"))
        button->setEnabled(editable &&
                           (!_marks->selectedItems().isEmpty() || (_hasPick && !_image.isNull())));
}
void SpectrumPane::refreshMarks() {
    QSet<QString> selected;
    for (auto item : _marks->selectedItems())
        selected.insert(item->data(Qt::UserRole).toString());
    _marks->clear();
    for (auto value : _annotations) {
        auto m = value.toMap();
        if (m.value("trackId") != _track.value("id"))
            continue;
        auto item = new QListWidgetItem(tr("%1 秒 · %2 · %3 Hz")
                                            .arg(m.value("ms").toDouble() / 1000, 0, 'f', 3)
                                            .arg(pitchName(m.value("pitch").toInt()))
                                            .arg(m.value("frequency").toDouble(), 0, 'f', 1),
                                        _marks);
        const int target = inputPitch(m);
        if (target != m.value("pitch").toInt())
            item->setText(item->text() + tr(" → 输入 %1").arg(pitchName(qBound(0, target, 127))));
        if (m.value("until").toDouble() > m.value("ms").toDouble())
            item->setText(
                item->text() +
                tr(" · 长 %1 秒")
                    .arg((m.value("until").toDouble() - m.value("ms").toDouble()) / 1000, 0, 'f', 3));
        item->setData(Qt::UserRole, m.value("id"));
        item->setData(Qt::UserRole + 1, m);
        item->setSelected(selected.contains(m.value("id").toString()));
    }
    updateMarksVisibility();
    updateActions();
}
void SpectrumPane::updateMarksVisibility() {
    const bool hasMarks = _marks->count() > 0;
    const bool showMarks = hasMarks && _marksToggle->isChecked();
    const bool showCanvas = _display->currentIndex() == 0;
    _marksToggle->setText(tr("标注列表 · %1").arg(_marks->count()));
    _marksToggle->setEnabled(hasMarks);
    _marks->setVisible(showMarks);
    _canvas->parentWidget()->setVisible(showCanvas);
    _marksSplitter->setVisible(showCanvas || showMarks);
}
void SpectrumPane::saveMarks() {
    auto score = _editor->score();
    if (!score)
        return;
    if (seq && seq->isPlaying()) {
        setStatus(tr("暂停乐谱播放后保存取音标注。"));
        scoreChanged();
        return;
    }
    auto master = score->masterScore();
    if (master->readOnly()) {
        setStatus(tr("这份乐谱以只读方式打开，请另存后标注。"));
        return;
    }
    auto data = QJsonDocument::fromJson(master->metaTag("kumo.transcription").toUtf8()).toVariant().toMap();
    data["schema"] = 1;
    data["pitches"] = _annotations;
    data["grids"] = _grids;
    data["view"] = QVariantMap{{"tuning", _tuning->value()}, {"transpose", _transpose->value()}};
    const auto text = QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));
    if (master->metaTag("kumo.transcription") == text) {
        setStatus(tr("标注和节拍网格已经保存。"));
        return;
    }
    master->startCmd();
    master->undo(new ChangeMetaText(master, "kumo.transcription", text));
    master->endCmd();
    if (mscore)
        mscore->endCmd();
    _savedData = text;
    refreshMarks();
    _canvas->update();
    setStatus(tr("标注和节拍网格已保存 · Ctrl+S 保留到乐谱，Ctrl+Z 撤销。"));
}
void SpectrumPane::insertPitches() {
    auto score = _editor->score();
    if (!score || !_editor->_view)
        return;
    if (score->masterScore()->readOnly()) {
        setStatus(tr("这份乐谱以只读方式打开，请另存后输入音符。"));
        return;
    }
    if (seq && seq->isPlaying()) {
        setStatus(tr("先暂停乐谱播放，再输入选出的音高。"));
        return;
    }
    QSet<int> selected;
    for (auto item : _marks->selectedItems())
        selected.insert(inputPitch(item->data(Qt::UserRole + 1).toMap()));
    if (selected.isEmpty()) {
        if (!_hasPick) {
            setStatus(tr("先在频谱点选一个音高，或在下面选择已有标注。"));
            return;
        }
        selected.insert(qRound(_cursorPitch) + _transpose->value());
    }
    for (int pitch : selected)
        if (pitch < 0 || pitch > 127) {
            setStatus(tr("移调后的音高超出了可输入范围，请减小移调。"));
            return;
        }
    if (!score->noteEntryMode())
        _editor->_view->cmd("note-input");
    auto &input = score->inputState();
    if (!input.segment() || input.track() < 0 || input.track() >= score->ntracks() ||
        input.duration().type() == TDuration::DurationType::V_INVALID) {
        setStatus(tr("先在乐谱选择输入位置和时值，再点“输入乐谱”。"));
        return;
    }
    const auto previousDuration = input.duration();
    if (_durationFromMark->isChecked()) {
        double duration = 0;
        QVariantList ranges;
        for (auto item : _marks->selectedItems())
            ranges.append(item->data(Qt::UserRole + 1));
        if (ranges.isEmpty() && _hasPick) {
            auto info = _info;
            info["tuning"] = _tuning->value();
            info["fftSize"] = _request.value("fftSize");
            ranges.append(estimateSpectrumExtent(_rawImage, info, _pickMs, _pickFrequency));
        }
        for (auto value : ranges) {
            const auto mark = value.toMap();
            const double from = mark.value("ms").toDouble(), until = mark.value("until", from).toDouble();
            if (until > from)
                duration =
                    qMax(duration, _clip.isEmpty() ? (until - from) / 60000 *
                                                         _beatControls->values().value("bpm", 120.).toDouble()
                                                   : (tickAt(_clip, until) - tickAt(_clip, from)) / DIVISION);
        }
        if (duration <= 0) {
            setStatus(tr("先用“画／调整标注”画出音的起止，再按标注长度输入。"));
            return;
        }
        input.setDuration(spectrumInputDuration(duration));
    }
    const auto writtenDuration = input.duration();
    QList<int> pitches = selected.values();
    std::sort(pitches.begin(), pitches.end());
    score->startCmd();
    bool add = false;
    for (int pitch : pitches) {
        NoteVal value(pitch);
        if (!score->addPitch(value, add)) {
            input.setDuration(previousDuration);
            score->endCmd(true);
            setStatus(tr("当前位置无法输入，请选一个音符或休止符后重试。"));
            return;
        }
        add = true;
    }
    auto master = score->masterScore();
    auto data = QJsonDocument::fromJson(master->metaTag("kumo.transcription").toUtf8()).toVariant().toMap();
    data["schema"] = 1;
    data["pitches"] = _annotations;
    data["grids"] = _grids;
    data["view"] = QVariantMap{{"tuning", _tuning->value()}, {"transpose", _transpose->value()}};
    const auto text = QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));
    if (master->metaTag("kumo.transcription") != text)
        master->undo(new ChangeMetaText(master, "kumo.transcription", text));
    input.setDuration(previousDuration);
    score->endCmd();
    if (mscore)
        mscore->endCmd();
    _savedData = text;
    const auto durationName =
        (writtenDuration.dots() ? tr("附点 ") : QString()) + writtenDuration.durationTypeUserName();
    setStatus(tr("已输入 %1 个音高 · %2 · Ctrl+Z 撤销").arg(pitches.size()).arg(durationName));
}
void SpectrumPane::cacheSettings() {
    auto values = _session->property("cacheOptions").toMap();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("频谱缓存"));
    auto layout = new QVBoxLayout(&dialog);
    QString directory = values.value("directory").toString();
    auto path = new QToolButton;
    path->setText(directory);
    connect(path, &QToolButton::clicked, &dialog, [&] {
        auto next = QFileDialog::getExistingDirectory(&dialog, tr("选择缓存位置"), directory);
        if (!next.isEmpty()) {
            directory = next;
            path->setText(next);
        }
    });
    layout->addWidget(new QLabel(tr("计算过的频谱保存在这里，方便再次打开。达到容量后清理较早的频谱缓存。")));
    layout->addWidget(path);
    auto ram = new QSpinBox;
    ram->setRange(8, 1024);
    ram->setValue(values.value("memoryMiB", 128).toInt());
    ram->setSuffix(tr(" MiB 内存"));
    layout->addWidget(ram);
    auto disk = new QSpinBox;
    disk->setRange(0, 16384);
    disk->setValue(values.value("diskMiB", 1024).toInt());
    disk->setSuffix(tr(" MiB 磁盘"));
    layout->addWidget(disk);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        QMetaObject::invokeMethod(_session, "setCacheOptions", Q_ARG(QString, directory),
                                  Q_ARG(int, ram->value()), Q_ARG(int, disk->value()));
}
void installSpectrumPane(PerformanceEditor *editor, QSplitter *splitter) {
    auto pane = new SpectrumPane(editor);
    editor->_spectrum = pane;
    splitter->addWidget(pane);
    pane->hide();
    auto tabs = editor->findChild<QTabBar *>("workflowExpressionTabs");
    if (!tabs)
        return;
    tabs->addTab(QObject::tr("扒谱"));
    QObject::connect(tabs, &QTabBar::currentChanged, pane, [pane, splitter, tabs, editor](int page) {
        if (page == 2 && showDetachedEditorArea(pane))
            return;
        if (!pane->property("kumoDetachedArea").toBool())
            pane->setVisible(page == 2);
        const auto saved =
            QSettings().value("performanceEditor/modeLayout/" + QString::number(page)).toByteArray();
        if (!saved.isEmpty())
            splitter->restoreState(saved);
        else if (page == 2)
            splitter->setSizes({220, 0, 0, 0, 220});
        if (page == 2)
            editor->centerPitch(true);
        tabs->setProperty("previousLayoutPage", page);
    });
    QObject::connect(splitter, &QSplitter::splitterMoved, pane, [tabs, splitter] {
        QSettings().setValue("performanceEditor/modeLayout/" + QString::number(tabs->currentIndex()),
                             splitter->saveState());
    });
}
} // namespace Ms
