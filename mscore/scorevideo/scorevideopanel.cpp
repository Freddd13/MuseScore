// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "scorevideopanel.h"
#include "mscore/musescore.h"
#include "mscore/seq.h"
#include "mscore/workflow/featuregate.h"
#include "mscore/workflow/paneldock.h"
#include "mscore/workflow/referencefactory.h"
#include "mscore/workflow/toolicons.h"
#include "mscore/workflow/workspaceviews.h"
#include "mscore/workflow/controlflowlayout.h"
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>
namespace Ms
{
namespace
{
QString timeText(double seconds)
{
    const int minutes = int(seconds) / 60;
    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds - minutes * 60, 6, 'f', 3, QChar('0'));
}
QString previewOwner() { return "score-video-preview"; }
bool ownPlayback(QObject* project)
{
    return project &&
           project->property("playbackPlan").toMap().value("owner").toString() == previewOwner();
}
} // namespace
class VideoPreview final : public QWidget
{
  public:
    ScoreVideoDocument* document;
    double position = 0;
    VideoFrameCache cache;
    QPoint pressed;
    QVariantMap initial;
    bool dragging = false;
    int picture = -1;
    explicit VideoPreview(ScoreVideoDocument* value, QWidget* parent)
        : QWidget(parent), document(value)
    {
        setMinimumSize(180, 100);
        setFocusPolicy(Qt::StrongFocus);
        setObjectName("scoreVideoPreview");
    }
    QRect frameRect() const
    {
        const auto s = document->settings();
        QSize size(s.value("width", 1920).toInt(), s.value("height", 1080).toInt());
        size.scale(contentsRect().size() - QSize(16, 16), Qt::KeepAspectRatio);
        return {QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size};
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().dark());
        const auto area = frameRect();
        for (int y = area.top(); y <= area.bottom(); y += 12)
            for (int x = area.left(); x <= area.right(); x += 12)
                painter.fillRect(QRect(x, y, 12, 12).intersected(area),
                                 ((x - area.left()) / 12 + (y - area.top()) / 12) % 2
                                     ? QColor(205, 205, 205)
                                     : QColor(235, 235, 235));
        auto snapshot = document->snapshot();
        if (dragging && picture >= 0) {
            auto offsets = snapshot.settings.value("pictureOffsets").toMap();
            auto offset = initial;
            const auto delta = mapFromGlobal(QCursor::pos()) - pressed;
            offset["x"] = initial.value("x").toDouble() + 100. * delta.x() / qMax(1, area.width());
            offset["y"] = initial.value("y").toDouble() + 100. * delta.y() / qMax(1, area.height());
            offsets[snapshot.pictures[picture].id] = offset;
            snapshot.settings["pictureOffsets"] = offsets;
        }
        if (!snapshot.cues.isEmpty())
            painter.drawImage(area.topLeft(),
                              scoreVideoFrame(snapshot, position, &cache, area.size()));
        else {
            painter.setPen(palette().text().color());
            painter.drawText(rect(), Qt::AlignCenter, tr("准备画面后在这里预览"));
        }
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !document->ready())
            return;
        const auto& snapshot = document->snapshot();
        const int cue = scoreVideoCueAt(snapshot, position);
        if (cue < 0)
            return;
        picture = snapshot.cues[cue].picture;
        const auto area = frameRect();
        if (!scoreVideoPictureRect(snapshot, picture, area.size())
                 .translated(area.topLeft())
                 .contains(event->pos()))
            return;
        initial = snapshot.settings.value("pictureOffsets")
                      .toMap()
                      .value(snapshot.pictures[picture].id)
                      .toMap();
        pressed = event->pos();
        dragging = true;
        setCursor(Qt::ClosedHandCursor);
    }
    void mouseMoveEvent(QMouseEvent*) override
    {
        if (dragging)
            update();
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!dragging)
            return;
        dragging = false;
        unsetCursor();
        const auto delta = event->pos() - pressed;
        const auto size = frameRect().size();
        if (delta.manhattanLength() >= QApplication::startDragDistance())
            document->movePicture(
                picture, initial.value("x").toDouble() + 100. * delta.x() / qMax(1, size.width()),
                initial.value("y").toDouble() + 100. * delta.y() / qMax(1, size.height()));
        update();
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape && dragging) {
            dragging = false;
            unsetCursor();
            releaseMouse();
            update();
            event->accept();
        } else
            QWidget::keyPressEvent(event);
    }
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate) {
            dragging = false;
            unsetCursor();
        }
        return QWidget::event(event);
    }
};
class VideoTimeline final : public QWidget
{
  public:
    ScoreVideoDocument* document;
    double position = 0, proposed = 0;
    int moving = -1;
    std::function<void(double)> locate;
    VideoTimeline(ScoreVideoDocument* value, QWidget* parent) : QWidget(parent), document(value)
    {
        setMinimumHeight(56);
        setMaximumHeight(90);
        setFocusPolicy(Qt::StrongFocus);
        setObjectName("scoreVideoTimeline");
    }
    double at(int x) const
    {
        return qBound(0., double(x - 8) / qMax(1, width() - 16) * document->snapshot().duration,
                      document->snapshot().duration);
    }
    double x(double time) const
    {
        return 8 + time / qMax(.001, document->snapshot().duration) * (width() - 16);
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().base());
        const auto& snapshot = document->snapshot();
        for (int i = 0; i < snapshot.cues.size(); ++i) {
            const auto& cue = snapshot.cues[i];
            QRectF box(x(i == moving ? proposed : cue.from), 22, x(cue.until) - x(cue.from),
                       height() - 27);
            p.fillRect(box, (i % 2) ? palette().alternateBase() : palette().button());
            p.setPen(palette().mid().color());
            p.drawRect(box);
            p.setPen(palette().text().color());
            p.drawText(box.adjusted(5, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(cue.label, Qt::ElideRight,
                                                  qMax(0, int(box.width() - 9))));
            if (i > 0) {
                p.setPen(QPen(palette().highlight().color(), 2));
                p.drawLine(QPointF(box.left(), 20), box.topLeft());
            }
        }
        p.setPen(palette().text().color());
        p.drawText(QRect(8, 0, 100, 20), Qt::AlignLeft | Qt::AlignVCenter, "0:00");
        p.drawText(QRect(width() - 110, 0, 102, 20), Qt::AlignRight | Qt::AlignVCenter,
                   timeText(snapshot.duration));
        p.setPen(QPen(palette().highlight().color(), 2));
        p.drawLine(QPointF(x(position), 0), QPointF(x(position), height()));
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !document->ready())
            return;
        moving = -1;
        const auto& cues = document->snapshot().cues;
        for (int i = 1; i < cues.size(); ++i)
            if (qAbs(event->pos().x() - x(cues[i].from)) <= 6) {
                moving = i;
                proposed = cues[i].from;
                break;
            }
        if (moving < 0 && locate)
            locate(at(event->pos().x()));
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        if (moving >= 0) {
            proposed = at(event->pos().x());
            update();
        } else if (locate)
            locate(at(event->pos().x()));
    }
    void mouseReleaseEvent(QMouseEvent*) override
    {
        if (moving >= 0) {
            const int index = moving;
            moving = -1;
            document->moveBoundary(index, proposed);
            update();
        }
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape && moving >= 0) {
            moving = -1;
            releaseMouse();
            update();
            event->accept();
        } else
            QWidget::keyPressEvent(event);
    }
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate)
            moving = -1;
        return QWidget::event(event);
    }
};
ScoreVideoPanel::ScoreVideoPanel(QWidget* parent)
    : QDockWidget(tr("动态乐谱视频"), parent), _document(this), _export(this)
{
    setObjectName("kumoScoreVideoDock");
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setMinimumSize(320, 220);
    auto body = new QWidget(this);
    setWidget(body);
    auto layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(5);
    auto controls = new ControlFlowLayout;
    controls->setSpacing(5);
    auto button = [&](const QString& text, const QString& icon, const char* id) {
        auto value = new QPushButton(componentToolIcon(icon), text, body);
        setComponentToolIcon(value, icon);
        value->setObjectName(id);
        controls->addWidget(value);
        return value;
    };
    _prepare = button(tr("准备画面"), "reset", "scoreVideoPrepare");
    _play = button(tr("播放预览"), "play", "scoreVideoPlay");
    auto stop = button(tr("停止"), "stop", "scoreVideoStop");
    _sound = new QComboBox(body);
    _sound->setObjectName("scoreVideoSound");
    _sound->setMinimumContentsLength(12);
    _sound->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    controls->addWidget(_sound);
    _positionLabel = new QLabel(body);
    _positionLabel->setMinimumWidth(145);
    controls->addWidget(_positionLabel);
    auto settings = new QToolButton(body);
    settings->setObjectName("scoreVideoSettingsToggle");
    settings->setText(tr("设置"));
    settings->setIcon(componentToolIcon("settings"));
    setComponentToolIcon(settings, "settings");
    settings->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    settings->setCheckable(true);
    settings->setChecked(QSettings().value("scoreVideoPanel/settingsVisible", true).toBool());
    controls->addWidget(settings);
    _write = button(tr("导出视频…"), "save", "scoreVideoExport");
    _cancel = button(tr("取消"), "close", "scoreVideoCancel");
    layout->addLayout(controls);
    auto horizontal = new QSplitter(Qt::Horizontal, body);
    horizontal->setObjectName("scoreVideoHorizontal");
    layout->addWidget(horizontal, 1);
    auto vertical = new QSplitter(Qt::Vertical, horizontal);
    vertical->setObjectName("scoreVideoVertical");
    _preview = new VideoPreview(&_document, vertical);
    auto lower = new QWidget(vertical);
    auto lowerLayout = new QVBoxLayout(lower);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    lowerLayout->setSpacing(3);
    _timeline = new VideoTimeline(&_document, lower);
    lowerLayout->addWidget(_timeline);
    auto rowControls = new ControlFlowLayout;
    auto mark = new QPushButton(componentToolIcon("record"), tr("在此换到下一行"), lower);
    setComponentToolIcon(mark, "record");
    mark->setObjectName("scoreVideoMark");
    rowControls->addWidget(mark);
    auto restore = new QPushButton(componentToolIcon("reset"), tr("恢复自动换行"), lower);
    setComponentToolIcon(restore, "reset");
    rowControls->addWidget(restore);
    auto restorePosition = new QPushButton(componentToolIcon("reset"), tr("恢复本行位置"), lower);
    setComponentToolIcon(restorePosition, "reset");
    rowControls->addWidget(restorePosition);
    lowerLayout->addLayout(rowControls);
    _rows = new QTreeWidget(lower);
    _rows->setObjectName("scoreVideoCues");
    _rows->setHeaderLabels({tr("谱行与播放遍次"), tr("切入时间（秒）"), tr("时间依据")});
    _rows->setRootIsDecorated(false);
    _rows->setMinimumHeight(65);
    _rows->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _rows->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _rows->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    lowerLayout->addWidget(_rows, 1);
    vertical->setStretchFactor(0, 4);
    vertical->setStretchFactor(1, 1);
    auto scroll = new QScrollArea(horizontal);
    scroll->setObjectName("scoreVideoSettings");
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(245);
    scroll->setMaximumWidth(360);
    auto fields = new QWidget(scroll);
    scroll->setWidget(fields);
    auto form = new QFormLayout(fields);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(7);
    auto staves = new QTreeWidget(fields);
    staves->setObjectName("scoreVideoStaves");
    staves->setHeaderHidden(true);
    staves->setRootIsDecorated(false);
    staves->setMaximumHeight(115);
    form->addRow(tr("显示谱表"), staves);
    connect(staves, &QTreeWidget::itemChanged, this, [this, staves] {
        if (_updating)
            return;
        QVariantList selected;
        for (int i = 0; i < staves->topLevelItemCount(); ++i)
            if (staves->topLevelItem(i)->checkState(0) == Qt::Checked)
                selected.append(i);
        _document.changeSettings({{"staves", selected}});
    });
    auto combo = [&](const QString& label, const char* key,
                     const QList<QPair<QString, QString>>& choices) {
        auto value = new QComboBox(fields);
        value->setObjectName(QString("scoreVideo_") + key);
        for (const auto& choice : choices)
            value->addItem(choice.first, choice.second);
        form->addRow(label, value);
        connect(value, QOverload<int>::of(&QComboBox::activated), this, [this, value, key] {
            if (!_updating)
                _document.changeSettings({{key, value->currentData()}});
        });
    };
    combo(tr("画面对齐"), "alignment", {{tr("上谱表对齐"), "top"}, {tr("居中"), "center"}});
    combo(tr("原曲时间"), "audioMapping",
          {{tr("使用片段关联"), "associations"}, {tr("按整曲估计，可手动换行"), "whole"}});
    auto number = [&](const QString& label, const char* key, double minimum, double maximum,
                      double step, int decimals) {
        auto value = new QDoubleSpinBox(fields);
        value->setObjectName(QString("scoreVideo_") + key);
        value->setRange(minimum, maximum);
        value->setSingleStep(step);
        value->setDecimals(decimals);
        form->addRow(label, value);
        connect(value, &QDoubleSpinBox::editingFinished, this, [this, value, key] {
            if (!_updating)
                _document.changeSettings({{key, value->value()}});
        });
    };
    number(tr("宽度（像素）"), "width", 64, 7680, 2, 0);
    number(tr("高度（像素）"), "height", 64, 7680, 2, 0);
    number(tr("每秒帧数"), "fps", 1, 120, 1, 0);
    number(tr("边距（像素）"), "margin", 0, 1000, 4, 0);
    number(tr("结尾停留（秒）"), "tail", 0, 30, .5, 1);
    auto gainRow = new QWidget(fields);
    auto gainLayout = new QHBoxLayout(gainRow);
    gainLayout->setContentsMargins(0, 0, 0, 0);
    gainLayout->setSpacing(5);
    auto gainSlider = new QSlider(Qt::Horizontal, gainRow);
    gainSlider->setObjectName("videoSoundGainSlider");
    gainSlider->setRange(0, 100);
    gainSlider->setMinimumWidth(65);
    auto gain = new QSpinBox(gainRow);
    gain->setObjectName("videoSoundGain");
    gain->setRange(0, 100);
    gain->setSuffix("%");
    gainLayout->addWidget(gainSlider, 1);
    gainLayout->addWidget(gain);
    form->addRow(tr("音量"), gainRow);
    auto applyGain = [this](int value) {
        if (_updating)
            return;
        _document.changeSettings({{"volume", value / 100.}});
        if (_playing) {
            auto project = referenceProject();
            if (ownPlayback(project))
                project->setProperty("volume", value / 100.);
        }
    };
    connect(gainSlider, &QSlider::valueChanged, this,
            [this, gainSlider, gain, applyGain](int value) {
                if (_updating)
                    return;
                gain->setValue(value);
                if (!gainSlider->isSliderDown())
                    applyGain(value);
            });
    connect(gainSlider, &QSlider::sliderReleased, this,
            [gainSlider, applyGain] { applyGain(gainSlider->value()); });
    connect(gain, &QSpinBox::editingFinished, this,
            [gain, applyGain] { applyGain(gain->value()); });
    auto check = [&](const QString& label, const char* key) {
        auto value = new QCheckBox(label, fields);
        value->setObjectName(QString("scoreVideo_") + key);
        form->addRow(value);
        connect(value, &QCheckBox::clicked, this, [this, value, key] {
            if (!_updating)
                _document.changeSettings({{key, value->isChecked()}});
        });
    };
    check(tr("各行使用相同缩放"), "uniformScale");
    check(tr("展开反复"), "expandRepeats");
    check(tr("保留原曲开头"), "includeLead");
    check(tr("透明背景（MOV）"), "transparent");
    check(tr("反相"), "invert");
    check(tr("显示播放光标"), "cursor");
    check(tr("高亮正在发声的音"), "highlight");
    auto color = [&](const QString& label, const char* key) {
        auto value = new QPushButton(tr("选择颜色…"), fields);
        form->addRow(label, value);
        connect(value, &QPushButton::clicked, this, [this, key] {
            auto result = QColorDialog::getColor(QColor(_document.settings().value(key).toString()),
                                                 this, tr("选择颜色"));
            if (result.isValid())
                _document.changeSettings({{key, result.name()}});
        });
    };
    color(tr("背景"), "background");
    color(tr("光标"), "cursorColor");
    color(tr("音符高亮"), "highlightColor");
    auto explanation = new QLabel(tr("拖动画面可调整本行位置。下方拖换行边界，或在播放时点“在此换到"
                                     "下一行”。设置随乐谱保存，可撤销。"),
                                  fields);
    explanation->setWordWrap(true);
    form->addRow(explanation);
    horizontal->setStretchFactor(0, 1);
    horizontal->setStretchFactor(1, 0);
    horizontal->setSizes({800, 290});
    vertical->setSizes({360, 170});
    auto footer = new QHBoxLayout;
    _status = new QLabel(body);
    _status->setWordWrap(true);
    footer->addWidget(_status, 1);
    _progress = new QProgressBar(body);
    _progress->setRange(0, 1000);
    _progress->setTextVisible(false);
    _progress->setFixedHeight(5);
    _progress->setMaximumWidth(160);
    footer->addWidget(_progress);
    layout->addLayout(footer);
    connect(settings, &QToolButton::toggled, scroll, &QWidget::setVisible);
    scroll->setVisible(settings->isChecked());
    connect(settings, &QToolButton::clicked, this,
            [](bool shown) { QSettings().setValue("scoreVideoPanel/settingsVisible", shown); });
    connect(_prepare, &QPushButton::clicked, this, [this] {
        commitCues();
        stopPlayback();
        _document.prepare();
    });
    connect(_cancel, &QPushButton::clicked, this, [this] {
        _playAfterPrepare = false;
        _export.cancel();
        _document.cancel();
    });
    connect(_play, &QPushButton::clicked, this, &ScoreVideoPanel::togglePlayback);
    connect(stop, &QPushButton::clicked, this, &ScoreVideoPanel::stopPlayback);
    connect(_sound, QOverload<int>::of(&QComboBox::activated), this, [this] {
        const auto data = _sound->currentData().toMap();
        stopPlayback();
        _document.changeSettings(
            {{"audioKind", data.value("kind")}, {"audioId", data.value("id")}});
    });
    connect(mark, &QPushButton::clicked, this, [this] {
        _document.moveBoundary(scoreVideoCueAt(_document.snapshot(), _position) + 1, _position);
    });
    connect(restore, &QPushButton::clicked, &_document, &ScoreVideoDocument::restoreTiming);
    connect(restorePosition, &QPushButton::clicked, this, [this] {
        const int cue = scoreVideoCueAt(_document.snapshot(), _position);
        if (cue >= 0)
            _document.restorePicture(_document.snapshot().cues[cue].picture);
    });
    _timeline->locate = [this](double time) { seek(time); };
    connect(_rows, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        seek(_document.snapshot().cues[item->data(0, Qt::UserRole).toInt()].from);
    });
    connect(_write, &QPushButton::clicked, this, [this] {
        commitCues();
        stopPlayback();
        const bool alpha = _document.settings().value("transparent").toBool();
        const QString extension = alpha ? "mov" : "mp4";
        const auto path = QFileDialog::getSaveFileName(
            this, tr("保存动态乐谱"),
            QDir::homePath() + "/" + _document.snapshot().title + "." + extension,
            alpha ? tr("透明视频 (*.mov)") : tr("视频 (*.mp4)"));
        if (!path.isEmpty())
            if (!_export.start(&_document, path))
                _status->setText(_export.status());
    });
    connect(&_document, &ScoreVideoDocument::changed, this, &ScoreVideoPanel::refresh);
    connect(&_document, &ScoreVideoDocument::prepared, this, [this] {
        if (_playAfterPrepare) {
            _playAfterPrepare = false;
            togglePlayback();
        }
    });
    connect(&_export, &ScoreVideoExport::changed, this, &ScoreVideoPanel::refresh);
    connect(&_export, &ScoreVideoExport::finished, this, [this](const QVariantMap& result) {
        if (_preparingPreview) {
            _preparingPreview = false;
            if (result.contains("previewPath")) {
                _previewPath = result.value("previewPath").toString();
                _audioDuration = result.value("audioDuration").toDouble();
                _previewKey = soundKey();
                playPrepared();
            }
        }
        refresh();
        if (!result.contains("previewPath"))
            _status->setText(_export.status());
    });
    _timer.setInterval(33);
    connect(&_timer, &QTimer::timeout, this, &ScoreVideoPanel::poll);
    for (auto child : findChildren<QWidget*>())
        child->installEventFilter(this);
    body->installEventFilter(this);
    installEventFilter(this);
    addEditorDockActions(this);
    refresh();
}
ScoreVideoPanel::~ScoreVideoPanel() { stopPlayback(); }
QString ScoreVideoPanel::soundKey() const
{
    auto snapshot = _document.snapshot();
    auto settings = snapshot.settings;
    settings.remove("pictureOffsets");
    settings.remove("timingOverrides");
    return QString::number(quintptr(snapshot.assets.get())) +
           QString::fromUtf8(
               QJsonDocument::fromVariant(QVariantMap{{"kind", snapshot.audioKind},
                                                      {"id", snapshot.audioId},
                                                      {"pieces", snapshot.audioPieces},
                                                      {"duration", snapshot.duration}})
                   .toJson(QJsonDocument::Compact));
}
void ScoreVideoPanel::refresh()
{
    if (_updating)
        return;
    _updating = true;
    const auto state = _document.state();
    setWindowTitle(tr("动态乐谱视频 · %1").arg(state.value("source").toString()));
    const auto settings = _document.settings();
    const bool busy = _export.busy() || _document.busy();
    _prepare->setEnabled(_document.source() && !busy);
    _play->setEnabled(_document.source() && !busy);
    _write->setEnabled(_document.ready() && _document.current() && !busy);
    _sound->setEnabled(!busy && !_playing);
    _cancel->setVisible(busy);
    _progress->setVisible(busy);
    _progress->setValue(int(_export.progress() * 1000));
    _status->setText(_export.busy() ? _export.status()
                     : _playing     ? tr("正在预览 · %1").arg(_sound->currentText())
                                    : _document.status());
    const int gain = qRound(settings.value("volume", 1.).toDouble() * 100);
    findChild<QSpinBox*>("videoSoundGain")->setValue(gain);
    auto gainSlider = findChild<QSlider*>("videoSoundGainSlider");
    gainSlider->setValue(gain);
    gainSlider->setEnabled(!busy);
    findChild<QSpinBox*>("videoSoundGain")->setEnabled(!busy);
    auto staves = findChild<QTreeWidget*>("scoreVideoStaves");
    staves->clear();
    const auto selected = settings.value("staves").toList();
    for (auto value : state.value("staves").toList()) {
        auto entry = value.toMap();
        auto item = new QTreeWidgetItem(staves, {entry.value("name").toString()});
        item->setCheckState(0,
                            selected.contains(entry.value("index")) ? Qt::Checked : Qt::Unchecked);
    }
    staves->setEnabled(!busy && !_playing);
    _sound->clear();
    _sound->addItem(tr("乐谱声音"), QVariantMap{{"kind", "score"}});
    _sound->addItem(tr("静音视频"), QVariantMap{{"kind", "none"}});
    for (auto value : _document.audioSources()) {
        auto source = value.toMap();
        _sound->addItem(tr("原曲：%1%2")
                            .arg(source.value("name").toString(), source.value("missing").toBool()
                                                                      ? tr("（文件待找回）")
                                                                      : QString()),
                        QVariantMap{{"kind", "reference"}, {"id", source.value("id")}});
    }
    for (int i = 0; i < _sound->count(); ++i) {
        const auto value = _sound->itemData(i).toMap();
        if (value.value("kind") == settings.value("audioKind") &&
            value.value("id").toString() == settings.value("audioId").toString())
            _sound->setCurrentIndex(i);
    }
    for (auto field : findChildren<QWidget*>()) {
        const auto name = field->objectName();
        if (!name.startsWith("scoreVideo_"))
            continue;
        const auto key = name.mid(11);
        if (auto spin = qobject_cast<QDoubleSpinBox*>(field))
            spin->setValue(settings.value(key).toDouble());
        if (auto check = qobject_cast<QCheckBox*>(field))
            check->setChecked(settings.value(key).toBool());
        if (auto combo = qobject_cast<QComboBox*>(field))
            combo->setCurrentIndex(combo->findData(settings.value(key)));
        field->setEnabled(!busy && !_playing);
        if (key == "audioMapping" || key == "includeLead") {
            const bool reference = settings.value("audioKind").toString() == "reference";
            field->setVisible(reference);
            if (auto form = qobject_cast<QFormLayout*>(field->parentWidget()->layout()))
                if (auto label = form->labelForField(field))
                    label->setVisible(reference);
        }
    }
    const auto cueKey =
        QJsonDocument::fromVariant(state.value("cues")).toJson(QJsonDocument::Compact);
    if (_rows->property("videoCueKey").toByteArray() != cueKey) {
        _rows->setProperty("videoCueKey", cueKey);
        const int currentRow = _rows->currentIndex().row();
        _rows->clear();
        for (int i = 0; i < _document.snapshot().cues.size(); ++i) {
            const auto& cue = _document.snapshot().cues[i];
            const auto basis = cue.clockSource == "association" ? tr("原曲关联")
                               : cue.clockSource == "estimate"  ? tr("整曲估计")
                               : cue.clockSource == "score-gap" ? tr("待关联")
                                                                : tr("乐谱演奏");
            auto item =
                new QTreeWidgetItem(_rows, {cue.label, QString::number(cue.from, 'f', 3), basis});
            item->setData(0, Qt::UserRole, i);
            if (i > 0) {
                auto time = new QDoubleSpinBox(_rows);
                time->setDecimals(3);
                time->setRange(0, _document.snapshot().duration);
                time->setValue(cue.from);
                time->setSingleStep(.1);
                _rows->setItemWidget(item, 1, time);
                time->installEventFilter(this);
                for (auto editor : time->findChildren<QLineEdit*>())
                    editor->installEventFilter(this);
                connect(time, &QDoubleSpinBox::editingFinished, this, [this, time, i] {
                    if (_updating)
                        return;
                    const double at = time->value();
                    _updating = true;
                    _document.moveBoundary(i, at);
                    _updating = false;
                    QTimer::singleShot(0, this, [this] { refresh(); });
                });
            }
            if (i == currentRow)
                _rows->setCurrentItem(item);
        }
    }
    _rows->setEnabled(!busy);
    _position = qBound(0., _position, _document.snapshot().duration);
    _positionLabel->setText(timeText(_position) + " / " + timeText(_document.snapshot().duration));
    _preview->position = _position;
    _timeline->position = _position;
    _preview->update();
    _timeline->update();
    _updating = false;
}
void ScoreVideoPanel::showEvent(QShowEvent* event)
{
    QDockWidget::showEvent(event);
    if (!property("videoLayoutRestored").toBool()) {
        for (const char* name : {"scoreVideoHorizontal", "scoreVideoVertical"}) {
            const auto state = QSettings().value(QString("scoreVideoPanel/") + name).toByteArray();
            if (!state.isEmpty())
                findChild<QSplitter*>(name)->restoreState(state);
        }
        setProperty("videoLayoutRestored", true);
    }
    _document.setScore(mscore ? mscore->currentScore() : nullptr);
    _timer.start();
    if (_document.source() && _document.snapshot().cues.isEmpty())
        QTimer::singleShot(0, &_document, [this] {
            if (isVisible() && !_document.busy())
                _document.prepare();
        });
}
void ScoreVideoPanel::hideEvent(QHideEvent* event)
{
    commitCues();
    stopPlayback();
    for (const char* name : {"scoreVideoHorizontal", "scoreVideoVertical"})
        QSettings().setValue(QString("scoreVideoPanel/") + name,
                             findChild<QSplitter*>(name)->saveState());
    _timer.stop();
    _document.cancel();
    if (_export.busy())
        _export.cancel();
    QDockWidget::hideEvent(event);
}
void ScoreVideoPanel::seek(double seconds)
{
    commitCues();
    const bool resume = _playing;
    stopPlayback();
    _position = qBound(0., seconds, _document.snapshot().duration);
    _preview->position = _timeline->position = _position;
    _preview->update();
    _timeline->update();
    _positionLabel->setText(timeText(_position) + " / " + timeText(_document.snapshot().duration));
    if (resume)
        playPrepared();
}
void ScoreVideoPanel::togglePlayback()
{
    commitCues();
    if (_playing) {
        stopPlayback();
        return;
    }
    if (_export.busy())
        return;
    if (!_document.ready() || !_document.current()) {
        _playAfterPrepare = true;
        _document.prepare();
        return;
    }
    if (_position >= _document.snapshot().duration - .01)
        _position = 0;
    if (_document.snapshot().audioKind == "none" || _previewKey == soundKey()) {
        playPrepared();
        return;
    }
    _preparingPreview = true;
    if (!_export.start(&_document, _document.snapshot().assets->filePath("preview.wav"), true))
        _preparingPreview = false;
}
void ScoreVideoPanel::playPrepared()
{
    if (!_document.ready())
        return;
    auto project = referenceProject();
    if (_document.snapshot().audioKind != "none" && !project->property("available").toBool()) {
        _status->setText(project->property("status").toString());
        return;
    }
    if (seq && seq->isPlaying()) {
        seq->stopWait();
        seq->synchronizeStoppedGui();
    }
    if (project->property("playing").toBool())
        QMetaObject::invokeMethod(project, "stop");
    _previewPlan = {};
    if (_document.snapshot().audioKind != "none" && !_previewPath.isEmpty()) {
        const double duration = _document.snapshot().duration * 1000;
        QVariantMap clip{
            {"id", previewOwner()},
            {"path", _previewPath},
            {"volume", 1.},
            {"pieces", QVariantList{QVariantMap{{"from", 0.},
                                                {"until", qMin(duration, _audioDuration * 1000)},
                                                {"at", 0.},
                                                {"duration", qMin(duration, _audioDuration * 1000)},
                                                {"fade", false}}}}};
        _previewPlan = {{"owner", previewOwner()},
                        {"clips", QVariantList{clip}},
                        {"duration", duration},
                        {"offset", _position * 1000},
                        {"volume", _document.settings().value("volume", 1.)},
                        {"joint", false},
                        {"loop", false}};
        QVariantMap result;
        QMetaObject::invokeMethod(project, "start", Q_RETURN_ARG(QVariantMap, result),
                                  Q_ARG(QVariantMap, _previewPlan));
        if (result.contains("error")) {
            _status->setText(result.value("error").toString());
            return;
        }
    }
    _playing = true;
    _base = _position;
    _clock.restart();
    _play->setText(tr("暂停预览"));
    setComponentToolIcon(_play, "pause");
    refresh();
    _status->setText(tr("正在预览 · %1").arg(_sound->currentText()));
}
void ScoreVideoPanel::stopPlayback()
{
    _playAfterPrepare = false;
    if (_preparingPreview) {
        _preparingPreview = false;
        _export.cancel();
    }
    if (_playing && !_previewPlan.isEmpty()) {
        auto project = referenceProject();
        if (ownPlayback(project)) {
            if (project->property("playing").toBool())
                _position = project->property("position").toDouble() / 1000;
            QMetaObject::invokeMethod(project, "stop");
        }
    }
    _playing = false;
    _previewPlan = {};
    _play->setText(tr("播放预览"));
    setComponentToolIcon(_play, "play");
    if (isVisible())
        refresh();
}
void ScoreVideoPanel::poll()
{
    if (mscore && (_document.source() != mscore->currentScore() ||
                   (!mscore->currentScore() && (_document.preparedScore() || _document.busy())))) {
        stopPlayback();
        _previewKey.clear();
        _document.setScore(mscore->currentScore());
        if (_document.source())
            _document.prepare();
        return;
    }
    _document.restoreStoredSettings();
    if (_playing) {
        if (!_previewPlan.isEmpty()) {
            auto project = referenceProject();
            if (!ownPlayback(project)) {
                stopPlayback();
                return;
            }
            _position = project->property("position").toDouble() / 1000;
            if (!project->property("playing").toBool()) {
                const bool completed = project->property("completed").toBool();
                const auto status = project->property("status").toString();
                if (completed)
                    _position = _document.snapshot().duration;
                stopPlayback();
                _status->setText(completed ? tr("预览已播放完毕。") : status);
            }
        } else
            _position = _base + _clock.elapsed() / 1000.;
        if (_position >= _document.snapshot().duration) {
            _position = _document.snapshot().duration;
            stopPlayback();
        }
    }
    _positionLabel->setText(timeText(_position) + " / " + timeText(_document.snapshot().duration));
    if (_preview->position != _position) {
        _preview->position = _timeline->position = _position;
        _preview->update();
        _timeline->update();
    }
    if (!_document.current() && _document.ready() && !_export.busy())
        _status->setText(tr("乐谱已有修改，点“准备画面”更新视频。"));
}
bool ScoreVideoPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        auto key = static_cast<QKeyEvent*>(event);
        if (key->matches(QKeySequence::Save) || key->matches(QKeySequence::SaveAs))
            commitCues();
    }
    auto widget = qobject_cast<QWidget*>(watched);
    const bool text = qobject_cast<QAbstractSpinBox*>(widget) || qobject_cast<QLineEdit*>(widget) ||
                      qobject_cast<QComboBox*>(widget);
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) && !text) {
        auto key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() == Qt::NoModifier &&
            (key->key() == Qt::Key_Space || key->key() == Qt::Key_Home ||
             key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)) {
            event->accept();
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                if (key->key() == Qt::Key_Space)
                    togglePlayback();
                else if (key->key() == Qt::Key_Home)
                    seek(0);
                else
                    seek(_position + (key->key() == Qt::Key_Left ? -.1 : .1));
            }
            return true;
        }
    }
    return QDockWidget::eventFilter(watched, event);
}
void ScoreVideoPanel::commitCues()
{
    if (_updating || _document.busy())
        return;
    _updating = true;
    for (int i = 1; i < _rows->topLevelItemCount() && i < _document.snapshot().cues.size(); ++i) {
        auto spin = qobject_cast<QDoubleSpinBox*>(_rows->itemWidget(_rows->topLevelItem(i), 1));
        if (!spin)
            continue;
        spin->interpretText();
        const double value = spin->value();
        if (qAbs(value - _document.snapshot().cues[i].from) > .00001)
            _document.moveBoundary(i, value);
    }
    _updating = false;
}
void addScoreVideoAction(QMenu* menu)
{
    auto action = mscore->findChild<QAction*>("kumo-score-video");
    if (!action) {
        action = new QAction(componentToolIcon("video"), QObject::tr("动态乐谱视频…"), mscore);
        action->setObjectName("kumo-score-video");
        action->setCheckable(true);
        markPersonalAction(action);
        QObject::connect(action, &QAction::triggered, mscore, [action](bool visible) {
            if (!personalFeaturesEnabled())
                return;
            auto dock = mscore->findChild<ScoreVideoPanel*>();
            if (!visible) {
                if (dock)
                    dock->hide();
                return;
            }
            if (auto mode = mscore->findChild<QAction*>("kumo-workspace-post"))
                mode->trigger();
            if (!dock) {
                dock = new ScoreVideoPanel(mscore);
                arrangeWorkflowDock(mscore, dock, "score-video", "bottomTools",
                                    Qt::BottomDockWidgetArea);
                QObject::connect(dock, &QDockWidget::visibilityChanged, action,
                                 [action](bool shown) {
                                     QSignalBlocker blocked(action);
                                     action->setChecked(shown);
                                 });
                if (!dock->isFloating())
                    QTimer::singleShot(0, dock, [dock] {
                        if (dock->isVisible())
                            mscore->resizeDocks({dock}, {mscore->height() * 3 / 4}, Qt::Vertical);
                    });
            }
            dock->show();
            dock->raise();
            dock->activateWindow();
        });
    }
    menu->addAction(action);
}
} // namespace Ms
