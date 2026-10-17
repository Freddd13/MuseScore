// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumpane.h"
#include "performanceeditor.h"
#include "../workflow/referencefactory.h"
#include "../workflow/workflowcontext.h"
#include "../workflow/builtinpanels.h"
#include "../workflow/referencepanel.h"
#include "../musescore.h"
#include "../scoreview.h"
#include "../seq.h"
#include "libmscore/input.h"
#include "libmscore/undo.h"
#include "libmscore/note.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QTabBar>
#include <QSplitter>
#include <QScrollBar>
#include <QSettings>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QUrl>
#include <QUuid>
#include <QFileInfo>
#include <cmath>
#include <algorithm>

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
        setMinimumHeight(100);
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
    auto tools = new QGridLayout;
    _tools = tools;
    tools->setHorizontalSpacing(4);
    tools->setVerticalSpacing(3);
    layout->addLayout(tools);
    auto button = [this, tools](const QString &text, int row, int col, auto fn) {
        auto b = new QToolButton(this);
        b->setText(text);
        connect(b, &QToolButton::clicked, this, fn);
        tools->addWidget(b, row, col);
        return b;
    };
    button(tr("原曲参考"), 0, 0, [] { showReferencePanel(true); });
    _tracks = new QComboBox;
    _tracks->setObjectName("transcriptionTracks");
    _tracks->setMinimumContentsLength(12);
    _tracks->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    tools->addWidget(_tracks, 0, 1);
    _clips = new QComboBox;
    _clips->setObjectName("transcriptionClips");
    _clips->setMinimumContentsLength(10);
    _clips->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    tools->addWidget(_clips, 0, 2);
    _display = new QComboBox;
    _display->setObjectName("transcriptionDisplay");
    _display->addItems({tr("上下对照"), tr("叠加对照")});
    _display->setToolTip(tr("叠加在钢琴卷帘后对照；按住 Alt 可以选择和拖动谱中音符。"));
    _display->setCurrentIndex(QSettings().value("spectrum/display", 0).toInt());
    tools->addWidget(_display, 0, 3);
    _preset = new QComboBox;
    _preset->addItems({tr("音高与起音"), tr("低音更清楚"), tr("起音更清楚")});
    _preset->setCurrentIndex(QSettings().value("spectrum/preset", 0).toInt());
    tools->addWidget(_preset, 0, 4);
    _tool = new QComboBox;
    _tool->setObjectName("transcriptionTool");
    _tool->addItems({tr("取音／试音"), tr("选择循环"), tr("手形平移")});
    tools->addWidget(_tool, 0, 5);
    button(tr("缓存设置"), 0, 6, [this] { cacheSettings(); });
    _play = button(tr("播放原曲"), 1, 0, [this] { playSource(); });
    _play->setObjectName("transcriptionPlay");
    _play->setEnabled(false);
    _loop = button(tr("循环"), 1, 1, [this] {
        if (_a < 0 || _b <= _a) {
            _status->setText(tr("使用“选择循环”拖过要反复听的片段。"));
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
    _speed = new QDoubleSpinBox;
    _speed->setRange(50, 150);
    _speed->setSuffix("%");
    _speed->setValue(100);
    _speed->setToolTip(tr("保调播放速度"));
    tools->addWidget(_speed, 1, 2);
    _tuning = new QDoubleSpinBox;
    _tuning->setRange(400, 480);
    _tuning->setDecimals(1);
    _tuning->setSuffix(" Hz");
    _tuning->setValue(QSettings().value("spectrum/tuning", 440).toDouble());
    _tuning->setToolTip(tr("A4 调律频率；只改变频率与音名的对照"));
    tools->addWidget(_tuning, 1, 3);
    _transpose = new QSpinBox;
    _transpose->setRange(-24, 24);
    _transpose->setPrefix(tr("移调 "));
    _transpose->setSuffix(tr(" 半音"));
    _transpose->setValue(QSettings().value("spectrum/transpose", 0).toInt());
    _transpose->setToolTip(tr("将选出的音名移调后标注和输入；录音频率保持原值"));
    tools->addWidget(_transpose, 1, 4);
    _harmonics = button(tr("谐波参考"), 1, 5, [this] {
        _canvas->update();
        _editor->updateSurfaces();
    });
    _harmonics->setCheckable(true);
    _harmonics->setChecked(true);
    auto inputButton = button(tr("输入乐谱"), 1, 6, [this] { insertPitches(); });
    inputButton->setObjectName("transcriptionInput");
    inputButton->setEnabled(false);
    inputButton->setToolTip(
        tr("把选中的音高按当前输入位置、声部和时值写入；多个音高组成和弦。Ctrl+Z 撤销。"));
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 7; ++col)
            _controls.append(tools->itemAtPosition(row, col)->widget());
    _canvas = new SpectrumCanvas(this);
    layout->addWidget(_canvas, 1);
    auto bottom = new QHBoxLayout;
    _status = new QLabel(tr("先在原曲参考中打开音频。"));
    _status->setWordWrap(true);
    bottom->addWidget(_status, 1);
    auto mark = new QToolButton;
    mark->setObjectName("transcriptionMark");
    mark->setText(tr("标注这个音"));
    mark->setEnabled(false);
    mark->setToolTip(tr("保存点击选中的音高和时间；鼠标移动后仍保留刚才的选择。"));
    connect(mark, &QToolButton::clicked, this, [this] {
        if (_path.isEmpty() || !_editor->score())
            return;
        QVariantMap identity;
        if (_audio)
            QMetaObject::invokeMethod(_audio, "identity", Q_RETURN_ARG(QVariantMap, identity));
        const int pitch = qBound(0, qRound(_cursorPitch) + _transpose->value(), 127);
        _annotations.append(QVariantMap{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                        {"trackId", _track.value("id")},
                                        {"identity", identity},
                                        {"path", _path},
                                        {"ms", _pickMs},
                                        {"frequency", _pickFrequency},
                                        {"pitch", pitch},
                                        {"tuning", _tuning->value()},
                                        {"transpose", _transpose->value()}});
        saveMarks();
    });
    bottom->addWidget(mark);
    auto remove = new QToolButton;
    remove->setText(tr("删除选中标注"));
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
    layout->addLayout(bottom);
    _marks = new QListWidget;
    _marks->setObjectName("transcriptionMarks");
    _marks->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _marks->setMaximumHeight(76);
    _marks->setMinimumHeight(28);
    _marks->setToolTip(tr("点击标注可定位与试音，按住 Ctrl 选择多个音高后输入为和弦。"));
    layout->addWidget(_marks);
    connect(_marks, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        auto m = item->data(Qt::UserRole + 1).toMap();
        _pickMs = m.value("ms").toDouble();
        _cursorMs = _pickMs;
        _pickFrequency = m.value("frequency", 440).toDouble();
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
        const auto path = _track.value("path").toString();
        if (path != _path) {
            _path = path;
            _image = QImage();
            _request.clear();
            _from = 0;
            _a = _b = -1;
            _cursorMs = 0;
            _hasPick = false;
            if (_session)
                QMetaObject::invokeMethod(_session, "cancel");
            _status->setText(_path.isEmpty() ? tr("在原曲参考中选择音频。") : tr("正在读取选中的音轨…"));
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
        _clips->addItem(tr("原曲时间"), QVariantMap());
        for (auto c : _track.value("associations").toList()) {
            auto a = c.toMap();
            _clips->addItem(a.value("name", tr("对应片段")).toString(), a);
        }
        int selected = _clips->count() > 1 ? 1 : 0;
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
        refreshMarks();
        updateActions();
        viewChanged();
    });
    connect(_clips, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        _clip = _clips->currentData().toMap();
        _request.clear();
        viewChanged();
    });
    connect(_display, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int n) {
        QSettings().setValue("spectrum/display", n);
        _canvas->setVisible(n == 0);
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
    connect(_speed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (ownsPlayback() && _media->property("playing").toBool()) {
            const double at = sourcePosition();
            QMetaObject::invokeMethod(_media, "stop");
            _cursorMs = at;
            playSource();
        }
    });
    connect(_editor->_scroll, &QScrollBar::valueChanged, this, [this] { viewChanged(); });
    _canvas->setVisible(_display->currentIndex() == 0);
    setMinimumHeight(_display->currentIndex() == 1 ? 126 : 180);
    applyAppearance();
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
    _audio = createReferenceMedia(this);
    _audio->setProperty("waveformEnabled", false);
    connect(_audio, SIGNAL(changed()), this, SLOT(audioChanged()));
    if (!_session->property("available").toBool()) {
        _status->setText(_session->property("status").toString());
        for (const char *name : {"transcriptionInput", "transcriptionMark"})
            if (auto button = findChild<QToolButton *>(name))
                button->setEnabled(false);
    }
}
void SpectrumPane::applyAppearance() {
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
        old.append(_tracks->itemData(i).toString());
    if (ids == old && (_tracks->currentIndex() < 0 || tracks[_tracks->currentIndex()].toMap() == _track))
        return;
    QString selected = _tracks->currentData().toString();
    {
        QSignalBlocker block(_tracks);
        _tracks->clear();
        for (auto value : tracks) {
            auto t = value.toMap();
            _tracks->addItem(t.value("name").toString(), t.value("id"));
        }
    }
    int index = _tracks->findData(selected);
    if (index < 0)
        index = qBound(0, _media->property("selectedTrack").toInt(), qMax(0, _tracks->count() - 1));
    {
        QSignalBlocker block(_tracks);
        _tracks->setCurrentIndex(tracks.isEmpty() ? -1 : index);
    }
    QMetaObject::invokeMethod(_tracks, "currentIndexChanged", Q_ARG(int, _tracks->currentIndex()));
}
void SpectrumPane::audioChanged() {
    _play->setEnabled(!_path.isEmpty() && _audio->property("duration").toDouble() > 0);
    const auto fileStatus = _audio->property("status").toString();
    if (_audio->property("duration").toDouble() <= 0 && fileStatus.startsWith("Cannot")) {
        _status->setText(tr("无法读取这份音频。请在原曲参考中检查文件位置。"));
        _status->setToolTip(fileStatus);
    }
    if (!_audioLoaded && _audio->property("duration").toDouble() > 0) {
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
double SpectrumPane::sourcePosition() const {
    if (ownsPlayback() && _media->property("playing").toBool())
        return qBound(_playStart, _playStart + _media->property("position").toDouble() * _playSpeed,
                      _playUntil);
    return _lastAudioMs;
}
void SpectrumPane::playSource() {
    if (!_media || !_audio || _audio->property("duration").toDouble() <= 0)
        return;
    if (ownsPlayback() && _media->property("playing").toBool()) {
        const double at = sourcePosition();
        QMetaObject::invokeMethod(_media, "stop");
        _cursorMs = at;
        _status->setText(tr("原曲已暂停"));
        return;
    }
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
    const QVariantMap clip{{"path", _path},
                           {"identity", _track.value("identity")},
                           {"volume", _track.value("volume", 1.)},
                           {"pieces", QVariantList{piece}}};
    const QVariantMap plan{{"owner", _playOwner},
                           {"clips", QVariantList{clip}},
                           {"joint", false},
                           {"loop", _loop->isChecked()},
                           {"volume", _media->property("volume")}};
    QVariantMap result;
    const bool called = QMetaObject::invokeMethod(_media, "start", Q_RETURN_ARG(QVariantMap, result),
                                                  Q_ARG(QVariantMap, plan));
    if (!called || result.contains("error"))
        _status->setText(called ? result.value("error").toString() : tr("原曲音频模块尚未就绪。"));
    else
        _status->setText(tr("正在播放所选原曲片段"));
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
    if (!ownsPlayback()) {
        _play->setText(tr("播放原曲"));
        return;
    }
    const bool playing = _media->property("playing").toBool();
    _play->setText(playing ? tr("暂停原曲") : tr("播放原曲"));
    if (playing)
        _lastAudioMs = sourcePosition();
    else {
        _cursorMs = _lastAudioMs;
        _status->setText(_media->property("status").toString());
    }
    if (isVisible()) {
        _canvas->update();
        _editor->_noteCanvas->update();
    }
}
void SpectrumPane::resizeEvent(QResizeEvent *) {
    if (!_tools || _controls.isEmpty())
        return;
    const int columns = width() >= 1050 ? 7 : width() >= 700 ? 4 : 3;
    if (columns == _columns)
        return;
    _columns = columns;
    for (auto widget : _controls)
        _tools->removeWidget(widget);
    for (int i = 0; i < _controls.size(); ++i)
        _tools->addWidget(_controls[i], i / columns, i % columns);
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
    _editor->invalidateVisual();
}
void SpectrumPane::scoreChanged() {
    const auto score = _editor->score();
    const auto text = score ? score->masterScore()->metaTag("kumo.transcription") : QString();
    if (score != _documentScore) {
        _documentScore = score;
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
        _annotations = QJsonDocument::fromJson(text.toUtf8()).toVariant().toMap().value("pitches").toList();
        refreshMarks();
    }
    refreshTracks();
    viewChanged();
}
void SpectrumPane::viewChanged() {
    if (!isVisible())
        return;
    updateActions();
    const auto score = _editor->score();
    const auto text = score ? score->masterScore()->metaTag("kumo.transcription") : QString();
    if (text != _savedData) {
        _savedData = text;
        _annotations = QJsonDocument::fromJson(text.toUtf8()).toVariant().toMap().value("pitches").toList();
        refreshMarks();
    }
    if (!_requestTimer.isActive())
        _requestTimer.start();
    _canvas->update();
}
double SpectrumPane::timeAtX(double x) const {
    return _clip.isEmpty() ? _from + (x - gutter) / qMax(1, _editor->_noteCanvas->width() - gutter) * _span
                           : audioAt(_clip, _editor->tickForX(x, false));
}
double SpectrumPane::xAtTime(double ms) const {
    return _clip.isEmpty() ? gutter + (ms - _from) / _span * qMax(1, _editor->_noteCanvas->width() - gutter)
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
    double from = timeAtX(gutter),
           until = timeAtX(_editor->_noteCanvas->width() - PerformanceViewport::rightMargin);
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
                        {"columns", qBound(64, (_editor->_noteCanvas->width() - gutter) / 2, 1024)},
                        {"rows", 352},
                        {"lowPitch", 20.5},
                        {"highPitch", 108.5},
                        {"fftSize", _preset->currentIndex() == 1   ? 32768
                                    : _preset->currentIndex() == 2 ? 4096
                                                                   : 16384},
                        {"tuning", _tuning->value()}};
    if (request == _request)
        return;
    _request = request;
    QMetaObject::invokeMethod(_session, "request", Q_ARG(QVariantMap, request));
}
void SpectrumPane::spectrumChanged() {
    const auto info = _session->property("info").toMap();
    const auto local = QUrl(_path).isLocalFile() ? QUrl(_path).toLocalFile() : _path;
    if (QFileInfo(info.value("sourcePath").toString()).absoluteFilePath() !=
        QFileInfo(local).absoluteFilePath())
        return;
    _image = qvariant_cast<QImage>(_session->property("image"));
    updateActions();
    _info = _session->property("info").toMap();
    QString status = _session->property("status").toString();
    if (_hasPick && !_session->property("busy").toBool() && !_image.isNull())
        status = tr("已选 %1 · %2 秒 · %3 Hz · ")
                     .arg(pitchName(qBound(0, qRound(_cursorPitch) + _transpose->value(), 127)))
                     .arg(_pickMs / 1000., 0, 'f', 3)
                     .arg(_pickFrequency, 0, 'f', 1) +
                 status;
    _status->setText(status);
    _canvas->update();
    _editor->updateSurfaces();
}
void SpectrumPane::paint(QPainter &p, const QRectF &bounds, bool overlay) {
    p.save();
    p.setClipRect(bounds.adjusted(gutter, 0, -PerformanceViewport::rightMargin, 0));
    if (!overlay)
        p.fillRect(bounds, QColor("#181c20"));
    if (!_image.isNull()) {
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
        p.setOpacity(overlay ? .55 : 1.);
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
    }
    p.setOpacity(1);
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
        p.drawEllipse(QPointF(x, yAtPitch(pitch, overlay)), 4, 4);
    }
    if (_hasPick) {
        const double cursor = xAtTime(_pickMs);
        p.setPen(QPen(accent, 1));
        p.drawLine(QPointF(cursor, 0), QPointF(cursor, bounds.height()));
        p.drawEllipse(QPointF(cursor, yAtPitch(_cursorPitch, overlay)), 4, 4);
    }
    if (ownsPlayback() && _media->property("playing").toBool()) {
        const double play = xAtTime(sourcePosition());
        p.setPen(QPen(palette().highlight().color(), 2));
        p.drawLine(QPointF(play, 0), QPointF(play, bounds.height()));
    }
    if (!overlay) {
        p.setPen(QColor("#d9dfe5"));
        for (int i = 0; i <= 4; ++i) {
            double x = gutter + (bounds.width() - gutter) * i / 4.;
            p.drawText(QPointF(x + 3, 13), QString::number(timeAtX(x) / 1000., 'f', 2) + tr("秒"));
        }
    }
    p.restore();
    if (!overlay) {
        p.fillRect(QRectF(0, 0, gutter, bounds.height()), palette().window());
        p.setPen(palette().text().color());
        for (int pitch = int(std::ceil(_low)); pitch <= int(_high); ++pitch)
            if (pitch % 12 == 0) {
                double y = yAtPitch(pitch, false);
                p.drawText(QRectF(2, y - 8, gutter - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                           pitchName(qBound(0, pitch + _transpose->value(), 127)));
            }
    }
}
void SpectrumPane::paintOverlay(QPainter &p, const QRectF &rect) {
    if (isVisible() && _display->currentIndex() == 1)
        paint(p, rect, true);
}
void SpectrumPane::pointer(QMouseEvent *e, bool release) {
    if (_path.isEmpty() || _image.isNull()) {
        e->accept();
        return;
    }
    const bool overlay = _display->currentIndex() == 1;
    const double ms = qMax(0., timeAtX(e->x())), pitch = pitchAtY(e->y(), overlay),
                 frequency = _tuning->value() * std::pow(2., (pitch - 69.) / 12.);
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
        const double delta = (e->x() - _press.x()) / qMax(1, _editor->_noteCanvas->width() - gutter) * _span;
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
        _status->setText(tr("循环片段 %1–%2 秒")
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
        _status->setText(text);
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
        if (_clip.isEmpty()) {
            double anchor = timeAtX(e->posF().x()), ratio = (anchor - _from) / _span;
            _span = qBound(250., _span / factor, 120000.);
            _from = qMax(0., anchor - ratio * _span);
        } else {
            _editor->_viewport.pixelsPerQuarter =
                qBound(8., _editor->_viewport.pixelsPerQuarter * factor, 2000.);
            _editor->invalidateVisual();
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
        item->setData(Qt::UserRole, m.value("id"));
        item->setData(Qt::UserRole + 1, m);
        item->setSelected(selected.contains(m.value("id").toString()));
    }
    updateActions();
}
void SpectrumPane::saveMarks() {
    auto score = _editor->score();
    if (!score)
        return;
    if (seq && seq->isPlaying()) {
        _status->setText(tr("暂停乐谱播放后保存取音标注。"));
        scoreChanged();
        return;
    }
    auto master = score->masterScore();
    if (master->readOnly()) {
        _status->setText(tr("这份乐谱以只读方式打开，请另存后标注。"));
        return;
    }
    auto data = QJsonDocument::fromJson(master->metaTag("kumo.transcription").toUtf8()).toVariant().toMap();
    data["schema"] = 1;
    data["pitches"] = _annotations;
    data["view"] = QVariantMap{{"tuning", _tuning->value()}, {"transpose", _transpose->value()}};
    const auto text = QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));
    master->startCmd();
    master->undo(new ChangeMetaText(master, "kumo.transcription", text));
    master->endCmd();
    if (mscore)
        mscore->endCmd();
    _savedData = text;
    refreshMarks();
    _canvas->update();
}
void SpectrumPane::insertPitches() {
    auto score = _editor->score();
    if (!score || !_editor->_view)
        return;
    if (score->masterScore()->readOnly()) {
        _status->setText(tr("这份乐谱以只读方式打开，请另存后输入音符。"));
        return;
    }
    if (seq && seq->isPlaying()) {
        _status->setText(tr("先暂停乐谱播放，再输入选出的音高。"));
        return;
    }
    QSet<int> selected;
    for (auto item : _marks->selectedItems())
        selected.insert(inputPitch(item->data(Qt::UserRole + 1).toMap()));
    if (selected.isEmpty()) {
        if (!_hasPick) {
            _status->setText(tr("先在频谱点选一个音高，或在下面选择已有标注。"));
            return;
        }
        selected.insert(qRound(_cursorPitch) + _transpose->value());
    }
    for (int pitch : selected)
        if (pitch < 0 || pitch > 127) {
            _status->setText(tr("移调后的音高超出了可输入范围，请减小移调。"));
            return;
        }
    if (!score->noteEntryMode())
        _editor->_view->cmd("note-input");
    auto &input = score->inputState();
    if (!input.segment() || input.track() < 0 || input.track() >= score->ntracks() ||
        input.duration().type() == TDuration::DurationType::V_INVALID) {
        _status->setText(tr("先在乐谱选择输入位置和时值，再点“输入乐谱”。"));
        return;
    }
    QList<int> pitches = selected.values();
    std::sort(pitches.begin(), pitches.end());
    score->startCmd();
    bool add = false;
    for (int pitch : pitches) {
        NoteVal value(pitch);
        if (!score->addPitch(value, add)) {
            score->endCmd(true);
            _status->setText(tr("当前位置无法输入，请选一个音符或休止符后重试。"));
            return;
        }
        add = true;
    }
    auto master = score->masterScore();
    auto data = QJsonDocument::fromJson(master->metaTag("kumo.transcription").toUtf8()).toVariant().toMap();
    data["schema"] = 1;
    data["pitches"] = _annotations;
    data["view"] = QVariantMap{{"tuning", _tuning->value()}, {"transpose", _transpose->value()}};
    const auto text = QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));
    if (master->metaTag("kumo.transcription") != text)
        master->undo(new ChangeMetaText(master, "kumo.transcription", text));
    score->endCmd();
    if (mscore)
        mscore->endCmd();
    _savedData = text;
    _status->setText(tr("已输入 %1 个音高 · Ctrl+Z 撤销").arg(pitches.size()));
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
