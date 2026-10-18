// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "rhythmpane.h"
#include "../workflow/editorareas.h"
#include "performanceeditor.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include "mscore/workflow/midiinput.h"
#include "mscore/workflow/workflowcontext.h"
#include "libmscore/measure.h"
#include "libmscore/tempo.h"
#include "importexport/midiimport/importmidi_quant.h"
#include "importexport/midiimport/importmidi_fraction.h"
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QDockWidget>
#include <QLayout>
#include <QScreen>
#include <QWindow>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>
#include <QUrl>
#include <QSet>
#include <algorithm>
#include <QtConcurrent>
#include <cmath>
#include <functional>
#include <limits>

namespace Ms {
namespace {
QString module(const QString &name, QString source) {
    source.remove(QRegularExpression("^\\.(pragma|import).*?$", QRegularExpression::MultilineOption));
    auto matches =
        QRegularExpression("^(?:function|var)\\s+([A-Za-z_$][\\w$]*)", QRegularExpression::MultilineOption)
            .globalMatch(source);
    QStringList names;
    while (matches.hasNext()) {
        auto n = matches.next().captured(1);
        names.append(n + ":" + n);
    }
    return "var " + name + "=(function(){\n" + source + "\nreturn {" + names.join(',') + "};})();\n";
}
QString pitchName(int p) {
    return QStringList{"C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"}.at((p % 12 + 12) %
                                                                                           12) +
           QString::number(p / 12 - 1);
}
double number(QString text, bool *valid) {
    auto parts = text.split('/');
    bool a = false, b = true;
    double x = parts[0].toDouble(&a), y = 1;
    if (parts.size() == 2)
        y = parts[1].toDouble(&b);
    *valid =
        a && b && parts.size() <= 2 && std::isfinite(x) && std::isfinite(y) && y != 0 && qAbs(x / y) < 100000;
    return *valid ? x / y : 0;
}
} // namespace
class RhythmCanvas : public QWidget {
    RhythmPane *_pane;
    int _drag = -1;
    bool _tail = false;
    double _press = 0;
    int _original = 0;
    QVector<QPair<QRectF, int>> _hits;
    double span() const {
        auto scope = _pane->_data["scope"].toMap();
        return qMax(480, scope["end"].toInt() - scope["start"].toInt());
    }
    double x(int tick) const { return 52 + tick / span() * qMax(1, width() - 64); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        _hits.clear();
        p.setPen(palette().mid().color());
        for (int beat = 0; beat <= span() / 480.; ++beat) {
            double at = x(beat * 480);
            p.setPen(palette().mid().color());
            p.drawLine(QPointF(at, 22), QPointF(at, height()));
            p.setPen(palette().text().color());
            p.drawText(QPointF(at + 3, 16), QString::number(beat + 1));
        }
        auto groups = _pane->_draft["sourceGroups"].toList();
        int low = 108, high = 21;
        for (auto group : groups)
            for (auto n : group.toMap()["notes"].toList()) {
                int pitch = n.toMap()["pitch"].toInt();
                low = qMin(low, pitch);
                high = qMax(high, pitch);
            }
        if (low > high) {
            p.setPen(palette().text().color());
            p.drawText(rect(), Qt::AlignCenter, tr("读取音高后，在这里拖动节奏草稿"));
            return;
        }
        low -= 2;
        high += 2;
        const double step = qMax(2., double(height() - 30) / qMax(5, high - low + 1));
        QSet<int> labels;
        for (int i = 0; i < _pane->_rows.size(); ++i) {
            auto row = _pane->_rows[i].toMap();
            int source = row["sourceIndex"].toInt();
            if (source < 0 || source >= groups.size())
                continue;
            auto notes = groups[source].toMap()["notes"].toList();
            int length = row.value("effectiveDuration", row.value("duration", 120)).toInt();
            if (length <= 0)
                length = 120;
            for (auto value : notes) {
                int pitch = value.toMap()["pitch"].toInt();
                double y = 26 + (high - pitch) * step;
                QRectF box(x(row["start"].toInt()), y,
                           qMax(6., x(row["start"].toInt() + length) - x(row["start"].toInt())),
                           qMax(4., step - 1));
                auto color = palette().highlight().color();
                color.setAlpha(row["estimated"].toBool() ? 105 : 195);
                p.setBrush(color);
                p.setPen(palette().highlight().color());
                p.drawRoundedRect(box, 2, 2);
                _hits.append({box, i});
                if (box.width() > 34 && box.height() >= 11) {
                    p.setPen(palette().highlightedText().color());
                    p.drawText(box, Qt::AlignCenter, QString::number(row["velocity"].toInt()));
                }
                p.setPen(palette().text().color());
                if (!labels.contains(pitch) && (step >= 11 || pitch == low + 2 || pitch == high - 2)) {
                    labels.insert(pitch);
                    p.drawText(QRectF(0, y - 4, 48, qMax(12., step)), Qt::AlignRight | Qt::AlignVCenter,
                               pitchName(pitch));
                }
            }
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton)
            return;
        double best = std::numeric_limits<double>::infinity();
        QRectF chosen;
        for (const auto &hit : _hits)
            if (hit.first.adjusted(-3, -3, 3, 3).contains(e->pos())) {
                const double distance =
                    qAbs(e->pos().y() - hit.first.center().y()) / qMax(2., hit.first.height()) +
                    (hit.first.contains(e->pos()) ? 0 : 2);
                if (distance < best) {
                    best = distance;
                    _drag = hit.second;
                    chosen = hit.first;
                }
            }
        if (_drag >= 0) {
            _tail = e->pos().x() > chosen.right() - 6;
            _press = e->pos().x();
            auto row = _pane->_rows[_drag].toMap();
            _original = _tail ? row.value("effectiveDuration", 120).toInt() : row["start"].toInt();
            _pane->_table->selectRow(_drag);
            _pane->_draftTimer.stop();
            _pane->_observer.cancelReadOnlyJob();
            _pane->_calculating = false;
            grabMouse();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (_drag < 0)
            return;
        auto row = _pane->_rows[_drag].toMap();
        int value =
            _pane->quantize(_original + qRound((e->pos().x() - _press) / qMax(1, width() - 64) * span()));
        if (_tail) {
            row["duration"] = qMax(_pane->_grid->currentData().toInt(), value);
            row["effectiveDuration"] = row["duration"];
        } else
            row["start"] = qMax(0, value);
        row["estimated"] = false;
        row["manual"] = true;
        _pane->_rows[_drag] = row;
        _pane->_manualRows = true;
        _pane->_candidate.clear();
        _pane->_apply->setEnabled(false);
        update();
        QToolTip::showText(e->globalPos(),
                           tr("第%1音 · 第%2拍 · 力度%3")
                               .arg(row["sourceIndex"].toInt() + 1)
                               .arg(1. + row["start"].toInt() / 480., 0, 'f', 2)
                               .arg(row["velocity"].toInt()),
                           this);
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        if (_drag >= 0) {
            _drag = -1;
            if (QWidget::mouseGrabber() == this)
                releaseMouse();
            _pane->queueDraft();
        }
    }

    void hideEvent(QHideEvent *e) override {
        if (QWidget::mouseGrabber() == this)
            releaseMouse();
        _drag = -1;
        QWidget::hideEvent(e);
    }

  public:
    explicit RhythmCanvas(RhythmPane *pane) : QWidget(pane), _pane(pane) {
        setObjectName("rhythmDraftCanvas");
        setMinimumHeight(55);
        setMouseTracking(true);
        setToolTip(tr("拖音符改变起音，拖右端改变时值；上方数字是选段中的拍位。"));
    }
};

RhythmPane::RhythmPane(PerformanceEditor *editor) : QWidget(editor), _observer(this), _capture(this) {
    setObjectName("rhythmWritingPane");
    setMinimumHeight(90);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(3);
    auto top = new QHBoxLayout;
    _scopeLabel = new QLabel(tr("先选择一段有音高的乐谱"));
    top->addWidget(_scopeLabel, 1);
    auto scope = new QPushButton(tr("读取当前选段"));
    scope->setObjectName("rhythmReadScope");
    top->addWidget(scope);
    layout->addLayout(top);
    auto settingsRow = new QHBoxLayout;
    _tracks = new QComboBox;
    _tracks->setObjectName("rhythmVoice");
    settingsRow->addWidget(_tracks, 1);
    _grid = new QComboBox;
    _grid->setObjectName("rhythmGrid");
    _grid->addItem(tr("十六分音符"), 120);
    _grid->addItem(tr("八分音符"), 240);
    _grid->addItem(tr("八分三连音"), 160);
    settingsRow->addWidget(_grid);
    _releases = new QCheckBox(tr("按松键写休止"));
    _releases->setToolTip(
        tr("开启后，松键到下一次起音之间写休止；关闭时延续到下一次起音，最后沿用该音原时值。"));
    settingsRow->addWidget(_releases);
    auto original = new QCheckBox(tr("对照原卷帘"));
    original->setObjectName("rhythmShowOriginal");
    original->setChecked(QSettings().value("rhythmWriting/showOriginal", false).toBool());
    setProperty("showOriginal", original->isChecked());
    settingsRow->addWidget(original);
    connect(original, &QCheckBox::toggled, this, [this, editor](bool on) {
        setProperty("showOriginal", on);
        QSettings().setValue("rhythmWriting/showOriginal", on);
        if (isVisible())
            if (auto notes = editor->findChild<QWidget *>("performanceNotesArea"))
                notes->setVisible(on);
    });
    layout->addLayout(settingsRow);
    auto recording = new QHBoxLayout;
    _record = new QToolButton;
    _record->setText(tr("录制"));
    _record->setObjectName("rhythmRecord");
    recording->addWidget(_record);
    _endMode = new QComboBox;
    _endMode->addItem(tr("手动停止"), "manual");
    _endMode->addItem(tr("弹完音序后停止"), "passage");
    _endMode->addItem(tr("按秒数停止"), "duration");
    recording->addWidget(_endMode);
    _duration = new QSpinBox;
    _duration->setRange(1, 3600);
    _duration->setValue(30);
    _duration->setSuffix(tr("秒"));
    _duration->setVisible(false);
    recording->addWidget(_duration);
    auto import = new QPushButton(tr("导入节奏MIDI"));
    import->setObjectName("rhythmImportMidi");
    recording->addWidget(import);
    auto restore = new QPushButton(tr("重用上次录音"));
    recording->addWidget(restore);
    recording->addStretch();
    layout->addLayout(recording);
    auto split = new QSplitter(Qt::Vertical);
    _canvas = new RhythmCanvas(this);
    split->addWidget(_canvas);
    _table = new QTableWidget;
    _table->setObjectName("rhythmDraftTable");
    _table->setColumnCount(6);
    _table->setHorizontalHeaderLabels(
        {tr("音序"), tr("音高"), tr("起音（四分拍）"), tr("时值（四分拍）"), tr("力度"), tr("说明")});
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setMinimumHeight(50);
    _table->verticalHeader()->hide();
    _table->verticalHeader()->setDefaultSectionSize(24);
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    _table->setToolTip(
        tr("音序从1开始，0表示跳过这次敲击。拍位从选段第1拍算起。时值填0会自动延续；可输入1/3等分数。"));
    split->addWidget(_table);
    if (!split->restoreState(QSettings().value("performanceEditor/rhythmDraftLayout").toByteArray()))
        split->setSizes({160, 150});
    connect(split, &QSplitter::splitterMoved, this,
            [split] { QSettings().setValue("performanceEditor/rhythmDraftLayout", split->saveState()); });
    layout->addWidget(split, 1);
    auto actions = new QHBoxLayout;
    for (auto pair : QList<QPair<QString, QString>>{{tr("看谱面草稿"), "preview"},
                                                    {tr("试听草稿"), "audition"},
                                                    {tr("听谱中写法"), "original"},
                                                    {tr("停止试听"), "stop"}}) {
        auto button = new QPushButton(pair.first);
        button->setObjectName("rhythm-" + pair.second);
        actions->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, pair] { operate(pair.second); });
    }
    _apply = new QPushButton(tr("使用草稿"));
    _apply->setObjectName("rhythmApply");
    _apply->setEnabled(false);
    actions->addWidget(_apply);
    layout->addLayout(actions);
    _status = new QLabel(tr("选择一条声部，用任意琴键敲它的节奏和力度。录完后可拖动草稿，再试听和采用。"));
    _status->setObjectName("rhythmStatus");
    _status->setWordWrap(true);
    layout->addWidget(_status);
    _pages.setInterval(0);
    _draftTimer.setSingleShot(true);
    _draftTimer.setInterval(70);
    connect(&_pages, &QTimer::timeout, this, &RhythmPane::readPage);
    connect(&_draftTimer, &QTimer::timeout, this, &RhythmPane::calculateDraft);
    connect(scope, &QPushButton::clicked, this, &RhythmPane::readScope);
    connect(_record, &QToolButton::clicked, this, &RhythmPane::record);
    connect(_apply, &QPushButton::clicked, this, [this] { operate("apply"); });
    connect(import, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, tr("选择用来写节奏的MIDI"), QString(),
                                                 tr("MIDI文件 (*.mid *.midi)"));
        if (!path.isEmpty())
            loadMidiFile(path);
    });
    connect(restore, &QPushButton::clicked, this, &RhythmPane::restoreRecording);
    connect(_tracks, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (!_editing)
            queueDraft(true);
    });
    connect(_grid, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        QSettings().setValue("rhythmWriting/grid", _grid->currentData());
        queueDraft();
    });
    connect(_releases, &QCheckBox::toggled, this, [this] {
        QSettings().setValue("rhythmWriting/releases", _releases->isChecked());
        queueDraft(true);
    });
    connect(_endMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { _duration->setVisible(_endMode->currentData() == "duration"); });
    connect(_table, &QTableWidget::cellChanged, this, &RhythmPane::editCell);
    connect(&_capture, &MidiCaptureSession::changed, this, &RhythmPane::recordingChanged);
    connect(&_capture, &MidiCaptureSession::finished, this,
            [this](const QString &path, const QString &) { loadMidiFile(path); });
    connect(&_capture, &MidiCaptureSession::failed, _status, &QLabel::setText);
    connect(workflowContext(), &WorkflowContext::activeScoreChanged, this, &RhythmPane::resetForScore);
    connect(&_observer, &PluginAPI::ScoreObserver::readOnlyJobFinished, this,
            [this](int token, const QVariantMap &result) {
                if (token != _token)
                    return;
                _calculating = false;
                if (result.contains("error")) {
                    _status->setText(result["error"].toString());
                    return;
                }
                if (_observer.analysisContentRevision() != _state["snapshotId"].toString()) {
                    _status->setText(tr("乐谱已经改变，点“读取当前选段”即可按现在的内容继续。"));
                    return;
                }
                _draft = result;
                _rows = result["rows"].toList();
                fillTable();
                _canvas->update();
                auto errors = result["errors"].toStringList();
                _apply->setEnabled(errors.isEmpty() && !_rows.isEmpty() && !_applied &&
                                   (_manualRows || !_midi["notes"].toList().isEmpty()));
                if (_midi["notes"].toList().isEmpty() && !_manualRows) {
                    _status->setText(tr("已读取%1组音高。点“录制”敲出节奏，或直接拖动草稿、修改表格。")
                                         .arg(result["sourceGroups"].toList().size()));
                    return;
                }
                _status->setText(
                    errors.isEmpty()
                        ? tr("%1组音高 · %2次敲击 · "
                             "%3组按前后估计。拖动草稿或修改表格后，直接试听或使用。演奏力度按100%采用。")
                              .arg(result["sourceGroups"].toList().size())
                              .arg(_midi["notes"].toList().size())
                              .arg(result["estimated"].toInt())
                        : errors.join("\n"));
            });
    _grid->setCurrentIndex(qMax(0, _grid->findData(QSettings().value("rhythmWriting/grid", 120))));
    _releases->setChecked(QSettings().value("rhythmWriting/releases", false).toBool());
}
RhythmPane::~RhythmPane() {
    if (_cancelRead)
        _cancelRead->store(true);
    _capture.cancel();
}
QVariantMap RhythmPane::settings() const {
    return {{"track", _tracks->currentData()},
            {"quantum", _grid->currentData()},
            {"useReleases", _releases->isChecked()},
            {"keepAll", true}};
}
int RhythmPane::quantize(int tick) const {
    return Quantize::quantizeValue(ReducedFraction::fromTicks(tick),
                                   ReducedFraction::fromTicks(_grid->currentData().toInt()))
        .ticks();
}
void RhythmPane::resetForScore() {
    auto current = mscore ? mscore->currentScore() : nullptr;
    if (_score == current)
        return;
    _capture.cancel();
    _pages.stop();
    _draftTimer.stop();
    _observer.cancelReadOnlyJob();
    if (_cancelRead)
        _cancelRead->store(true);
    ++_readGeneration;
    _score = current;
    _ready = false;
    _data.clear();
    _midi.clear();
    _rows.clear();
    _draft.clear();
    _candidate.clear();
    _apply->setEnabled(false);
    _table->setRowCount(0);
    _canvas->update();
    if (isVisible())
        QTimer::singleShot(0, this, &RhythmPane::readScope);
}
void RhythmPane::showEvent(QShowEvent *e) {
    PerformanceAppearance appearance;
    appearance.load();
    auto colors = appearance.colors;
    QPalette theme = palette();
    theme.setColor(QPalette::Window, colors[PerformanceAppearance::Background]);
    theme.setColor(QPalette::Base, colors[PerformanceAppearance::Background]);
    theme.setColor(QPalette::AlternateBase, colors[PerformanceAppearance::NaturalRow]);
    theme.setColor(QPalette::Text, colors[PerformanceAppearance::Text]);
    theme.setColor(QPalette::WindowText, colors[PerformanceAppearance::Text]);
    theme.setColor(QPalette::Button, colors[PerformanceAppearance::Background].lighter(125));
    theme.setColor(QPalette::ButtonText, colors[PerformanceAppearance::Text]);
    theme.setColor(QPalette::Mid, colors[PerformanceAppearance::Grid]);
    setPalette(theme);
    _table->setPalette(theme);
    _table->setStyleSheet(QString("QTableWidget { background-color:%1; color:%2; gridline-color:%3; }"
                                  "QTableWidget::item { background-color:%1; color:%2; }"
                                  "QTableWidget::item:selected { background-color:%4; color:%5; }"
                                  "QHeaderView::section { background:%6; color:%2; border:0px; "
                                  "border-right:1px solid %3; border-bottom:1px solid %3; padding:3px; }")
                              .arg(theme.base().color().name(), theme.text().color().name(),
                                   theme.mid().color().name(), theme.highlight().color().name(),
                                   theme.highlightedText().color().name(), theme.button().color().name()));
    _canvas->setPalette(theme);
    QWidget::showEvent(e);
    auto current = mscore ? mscore->currentScore() : nullptr;
    if (current != _score) {
        resetForScore();
        return;
    }
    if (!_ready && !_pages.isActive())
        readScope();
    else
        queueDraft();
}
void RhythmPane::hideEvent(QHideEvent *e) {
    _capture.stop();
    _pages.stop();
    _draftTimer.stop();
    _observer.cancelReadOnlyJob();
    QWidget::hideEvent(e);
}
void RhythmPane::readScope() {
    if (_capture.state()["recording"].toBool()) {
        _status->setText(tr("先停止本次录制，再更换音高或选段。"));
        return;
    }
    auto score = mscore ? mscore->currentScore() : nullptr;
    if (!score) {
        _status->setText(tr("打开乐谱并选择要写节奏的小节。"));
        return;
    }
    _pages.stop();
    _observer.cancelReadOnlyJob();
    _score = score;
    PluginAPI::Score wrapped(score);
    _observer.setScore(&wrapped);
    auto selection = _observer.arrangementState();
    if (selection.contains("error")) {
        _status->setText(selection["error"].toString());
        return;
    }
    const auto selected = selection["scope"].toMap();
    _state = _observer.arrangementState(
        {{"firstMeasure", selected["firstMeasure"]}, {"lastMeasure", selected["lastMeasure"]}});
    if (_state.contains("error")) {
        _status->setText(_state["error"].toString());
        return;
    }
    auto scope = _state["scope"].toMap();
    scope["firstTrack"] = (selected["firstTrack"].toInt() / 4) * 4;
    scope["endTrack"] = ((selected["endTrack"].toInt() + 3) / 4) * 4;
    _state["scope"] = scope;
    _data = {{"scope", scope}, {"snapshotId", _state["snapshotId"]}};
    _cursor = scope["start"].toInt();
    _ready = false;
    _applied = false;
    _manualRows = false;
    _draft.clear();
    _candidate.clear();
    _apply->setEnabled(false);
    _tracks->setProperty("preferredTrack", selected["firstTrack"]);
    _pages.start();
    _status->setText(tr("正在读取选段的音高…"));
}
void RhythmPane::readPage() {
    auto page = _observer.arrangementRange({{"snapshotId", _state["snapshotId"]},
                                            {"scope", _data["scope"]},
                                            {"fromTick", _cursor},
                                            {"limit", 24}});
    if (page.contains("error")) {
        _pages.stop();
        _status->setText(page["error"].toString());
        return;
    }
    for (const auto &name : QStringList{"notes", "rests", "meters", "tempos"}) {
        auto rows = _data[name].toList();
        rows.append(page[name].toList());
        _data[name] = rows;
    }
    if (_data["notes"].toList().size() > 2048) {
        _pages.stop();
        _status->setText(tr("先选较短的段落，一次为一条声部写节奏。"));
        return;
    }
    if (!page["done"].toBool()) {
        _cursor = page["nextTick"].toInt();
        return;
    }
    _pages.stop();
    _ready = true;
    _editing = true;
    _tracks->clear();
    QSet<int> tracks;
    for (auto value : _data["notes"].toList()) {
        auto n = value.toMap();
        if (!n["contextOnly"].toBool() && !n["tieBack"].toBool() && !n["grace"].toBool())
            tracks.insert(n["track"].toInt());
    }
    auto ordered = tracks.values();
    std::sort(ordered.begin(), ordered.end());
    for (int track : ordered)
        _tracks->addItem(tr("谱表%1 · 声部%2").arg(track / 4 + 1).arg(track % 4 + 1), track);
    _tracks->setCurrentIndex(qMax(0, _tracks->findData(_tracks->property("preferredTrack"))));
    _editing = false;
    auto scope = _data["scope"].toMap();
    _scopeLabel->setText(tr("写入第%1–%2小节 · 一次一条声部")
                             .arg(scope["firstMeasure"].toInt())
                             .arg(scope["lastMeasure"].toInt()));
    queueDraft(true);
}
void RhythmPane::queueDraft(bool fresh) {
    _calculating = true;
    if (fresh)
        _manualRows = false;
    _candidate.clear();
    _apply->setEnabled(false);
    if (_ready && isVisible())
        _draftTimer.start();
}
void RhythmPane::calculateDraft() {
    if (!_ready || !_score || _tracks->currentIndex() < 0)
        return;
    auto notes = _midi["notes"].toList();
    double first = std::numeric_limits<double>::infinity();
    for (auto value : notes)
        first = qMin(first, value.toMap()["seconds"].toDouble());
    int start = _data["scope"].toMap()["start"].toInt(), firstPitch = start;
    for (auto value : _data["notes"].toList()) {
        auto n = value.toMap();
        if (n["track"] == _tracks->currentData() && !n["contextOnly"].toBool() && !n["tieBack"].toBool()) {
            firstPitch = qMax(start, n["tick"].toInt());
            break;
        }
    }
    const double seconds = _score->tempomap()->tick2time(firstPitch);
    for (auto &value : notes) {
        auto n = value.toMap();
        n["relativeTick"] =
            quantize(_score->tempomap()->time2tick(seconds + n["seconds"].toDouble() - first) - start);
        n["relativeOffTick"] =
            quantize(_score->tempomap()->time2tick(seconds + n["endSeconds"].toDouble() - first) - start);
        value = n;
    }
    QString source = "(function(){";
    for (auto pair : QList<QPair<QString, QString>>{{"Alignment", "Alignment.js"}, {"Rhythm", "Rhythm.js"}}) {
        auto text = _observer.readTextFile(
            QUrl::fromLocalFile(mscoreGlobalShare + "plugins/PerformanceExpression/" + pair.second)
                .toString());
        if (text.isEmpty()) {
            _status->setText(tr("找不到节奏规则：%1").arg(pair.second));
            return;
        }
        source += module(pair.first, text);
    }
    source += "return Rhythm.build(input);})()";
    QVariantMap input{{"data", _data}, {"settings", settings()}, {"performance", notes}};
    if (_manualRows)
        input["rows"] = _rows;
    _token = _observer.startReadOnlyJob(source, input);
}
void RhythmPane::fillTable() {
    QSignalBlocker block(_table);
    _table->setRowCount(_rows.size());
    auto groups = _draft["sourceGroups"].toList();
    for (int i = 0; i < _rows.size(); ++i) {
        auto row = _rows[i].toMap();
        int index = row["sourceIndex"].toInt();
        QStringList pitches;
        if (index >= 0 && index < groups.size())
            for (auto n : groups[index].toMap()["notes"].toList())
                pitches.append(pitchName(n.toMap()["pitch"].toInt()));
        QStringList texts{QString::number(index + 1),
                          pitches.join(" "),
                          QString::number(1. + row["start"].toInt() / 480., 'g', 7),
                          QString::number(row.value("duration", 0).toInt() / 480., 'g', 7),
                          QString::number(row["velocity"].toInt()),
                          row["status"].toString()};
        for (int col = 0; col < texts.size(); ++col) {
            auto item = new QTableWidgetItem(texts[col]);
            if (col == 1 || col == 5)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            if (row["estimated"].toBool())
                item->setForeground(palette().link());
            _table->setItem(i, col, item);
        }
    }
}
void RhythmPane::editCell(int row, int col) {
    if (row < 0 || row >= _rows.size() || col == 1 || col == 5)
        return;
    auto value = _rows[row].toMap();
    bool ok = false;
    double n = number(_table->item(row, col)->text(), &ok);
    if (!ok) {
        _status->setText(tr("请输入拍数或分数，例如1.5、1/3。"));
        _draft["errors"] = QStringList{tr("请先改好表格中的数值。")};
        _apply->setEnabled(false);
        _candidate.clear();
        return;
    }
    if (col == 0)
        value["sourceIndex"] = qRound(n) - 1;
    else if (col == 2)
        value["start"] = quantize(qRound((n - 1) * 480));
    else if (col == 3)
        value["duration"] = n > 0 ? quantize(qRound(n * 480)) : 0;
    else if (col == 4)
        value["velocity"] = qBound(1, qRound(n), 127);
    value["estimated"] = false;
    value["manual"] = true;
    _rows[row] = value;
    _manualRows = true;
    _canvas->update();
    queueDraft();
}
void RhythmPane::record() {
    if (_capture.state()["recording"].toBool()) {
        _capture.stop();
        return;
    }
    if (!_ready) {
        _status->setText(tr("先点“读取当前选段”，选择要写节奏的声部。"));
        return;
    }
    auto options = settings();
    options["notes"] = _data["notes"];
    options["role"] = 2;
    options["tap"] = true;
    options["monitor"] = false;
    options["endMode"] = _endMode->currentData();
    options["duration"] = _duration->value();
    options["firstTrack"] = _tracks->currentData();
    options["start"] = _data["scope"].toMap()["start"];
    auto result = _capture.start(options);
    if (result.contains("error"))
        _status->setText(result["error"].toString());
}
void RhythmPane::recordingChanged() {
    auto state = _capture.state();
    bool recording = state["recording"].toBool();
    _record->setText(recording ? tr("停止录制") : tr("录制"));
    _record->setEnabled(!state["stopping"].toBool() && !state["saving"].toBool());
    if (recording)
        _status->setText(tr("录制中 · %1次起音 · %2秒 · 再点一次结束")
                             .arg(state["count"].toInt())
                             .arg(state["seconds"].toDouble(), 0, 'f', 1));
}
void RhythmPane::loadMidiFile(const QString &path) {
    _draft.clear();
    _candidate.clear();
    _apply->setEnabled(false);
    _calculating = true;
    if (_cancelRead)
        _cancelRead->store(true);
    _cancelRead = std::make_shared<std::atomic_bool>(false);
    auto cancel = _cancelRead;
    const int generation = ++_readGeneration;
    auto watcher = new QFutureWatcher<QVariantMap>(this);
    _status->setText(tr("正在读取节奏MIDI…"));
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, generation] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (generation != _readGeneration)
            return;
        _calculating = false;
        if (result.contains("error")) {
            _status->setText(result["error"].toString());
            return;
        }
        if (result["notes"].toList().size() > 4096) {
            _status->setText(tr("这份录音较长，请选取对应段落的MIDI。"));
            return;
        }
        _midi = result;
        _applied = false;
        queueDraft(true);
    });
    watcher->setFuture(QtConcurrent::run([path, cancel] { return readPerformanceMidi(path, *cancel); }));
}
bool RhythmPane::prepare() {
    if (_capture.state()["recording"].toBool() || _capture.state()["saving"].toBool()) {
        _status->setText(tr("停止录制后即可试听或使用草稿。"));
        return false;
    }
    if (_calculating) {
        _status->setText(tr("正在更新刚才的调整，稍后即可试听或使用。"));
        return false;
    }
    if (_midi["notes"].toList().isEmpty() && !_manualRows) {
        _status->setText(tr("先录制节奏，或直接调整草稿，再试听和使用。"));
        return false;
    }
    if (!_candidate.isEmpty())
        return true;
    if (!_score || !_ready || _draft["voices"].toList().isEmpty() ||
        !_draft["errors"].toStringList().isEmpty()) {
        _status->setText(tr("先在草稿中调整提示的位置，再试听或使用。"));
        return false;
    }
    if (_draftTimer.isActive()) {
        _status->setText(tr("正在更新刚才的调整，稍后即可试听或使用。"));
        return false;
    }
    QVariantMap source{{"kind", _midi["notes"].toList().isEmpty() ? "rhythmDraft" : "rhythmTake"},
                       {"path", _midi["path"]},
                       {"sha256", _midi["sha256"]},
                       {"notes", _midi["notes"]},
                       {"rows", _rows},
                       {"settings", settings()}};
    QVariantMap option{{"title", tr("节奏草稿")},
                       {"summary", _draft["summary"]},
                       {"scope", _draft["scope"]},
                       {"voices", _draft["voices"]},
                       {"source", source}};
    auto result = _observer.arrangementRequest("submitArrangementOptions",
                                               {{"snapshotId", _state["snapshotId"]},
                                                {"options", QVariantList{option}},
                                                {"showPanel", false},
                                                {"taskBrief", QVariantMap{{"scope", _draft["scope"]},
                                                                          {"preserveTracks", QVariantList{}},
                                                                          {"allowMelodyChanges", true},
                                                                          {"preserveHarmony", true}}}});
    if (result.contains("error")) {
        _status->setText(result["error"].toString());
        return false;
    }
    _candidate = result["options"].toList().front().toMap()["id"].toString();
    return true;
}
void RhythmPane::operate(const QString &action) {
    if (action != "stop" && !prepare())
        return;
    auto result =
        _observer.arrangementRequest("arrangementOption", {{"optionId", _candidate}, {"action", action}});
    if (result.contains("error")) {
        _status->setText(result["error"].toString());
        return;
    }
    if (action == "apply") {
        _applied = true;
        _apply->setEnabled(false);
        _status->setText(tr("已采用节奏与力度 · Ctrl+Z撤销整次采用 · Ctrl+S保存。录音和对应也随谱保存。"));
    } else if (action == "audition")
        _status->setText(tr("正在听：节奏草稿"));
    else if (action == "original")
        _status->setText(tr("正在听：谱中写法"));
    else if (action == "stop")
        _status->setText(tr("已停止试听，可以继续调整草稿。"));
}
void RhythmPane::restoreRecording() {
    auto saved = _observer.workflowData("arrangement");
    auto history = saved["adoptions"].toList();
    for (int i = history.size() - 1; i >= 0; --i) {
        auto source = history[i].toMap()["source"].toMap();
        if (source["kind"] != "rhythmTake")
            continue;
        auto raw = source["notes"];
        if (raw.toList().isEmpty())
            for (auto v : saved["materials"].toList()) {
                auto m = v.toMap();
                if (m["id"] == source["recordingId"])
                    raw = m["notes"];
            }
        {
            QSignalBlocker gridBlock(_grid), releaseBlock(_releases);
            auto savedSettings = source["settings"].toMap();
            _grid->setCurrentIndex(qMax(0, _grid->findData(savedSettings.value("quantum", 120))));
            _releases->setChecked(savedSettings.value("useReleases", false).toBool());
        }
        _midi = {{"path", source["path"]}, {"sha256", source["sha256"]}, {"notes", raw}};
        _rows = source["rows"].toList();
        _manualRows = true;
        _applied = false;
        queueDraft();
        _status->setText(tr("已取回上次录音，可重新调整和试听。"));
        return;
    }
    _status->setText(tr("这份谱还没有采用过节奏录音。先录制或导入，使用草稿后即可重用。"));
}
void installRhythmPane(PerformanceEditor *editor, QSplitter *splitter) {
    auto pane = new RhythmPane(editor);
    splitter->addWidget(pane);
    pane->hide();
    auto tabs = editor->findChild<QTabBar *>("workflowExpressionTabs");
    if (!tabs)
        return;
    tabs->addTab(QObject::tr("写节奏"));
    struct LayoutState {
        bool active = false;
        QMap<QWidget *, bool> visible;
    };
    auto state = std::make_shared<LayoutState>();
    QObject::connect(tabs, &QTabBar::currentChanged, pane, [pane, splitter, editor, state](int page) {
        if(page==3&&showDetachedEditorArea(pane))return;
        if(pane->property("kumoDetachedArea").toBool())return;
        QSize before = mscore ? mscore->size() : QSize();
        if (mscore)
            if (auto screen = mscore->windowHandle() ? mscore->windowHandle()->screen()
                                                     : QGuiApplication::primaryScreen())
                before = before.boundedTo(screen->availableGeometry().size() - QSize(16, 48));
        if (page == 3) {
            const QSize opening = editor->property("windowBeforeEditor").toSize();
            if (opening.isValid())
                before = before.boundedTo(opening);
            if (!state->active) {
                state->visible.clear();
                std::function<void(QLayout *)> collect = [&](QLayout *layout) {
                    for (int i = 0; i < layout->count(); ++i) {
                        auto item = layout->itemAt(i);
                        if (item->layout())
                            collect(item->layout());
                        else if (auto w = item->widget())
                            if (w != splitter && !qobject_cast<QTabBar *>(w))
                                state->visible[w] = !w->isHidden();
                    }
                };
                collect(editor->layout());
                for (auto name : QStringList{"performanceNotesArea", "performanceParametersArea",
                                             "performanceWorkflowPanel"})
                    if (auto w = editor->findChild<QWidget *>(name))
                        state->visible[w] = !w->isHidden();
                state->active = true;
            }
            for (auto it = state->visible.cbegin(); it != state->visible.cend(); ++it)
                it.key()->hide();
            if (pane->property("showOriginal").toBool())
                if (auto notes = editor->findChild<QWidget *>("performanceNotesArea"))
                    notes->show();
            pane->show();
            auto saved = QSettings().value("performanceEditor/modeLayout/3").toByteArray();
            if (saved.isEmpty() || !splitter->restoreState(saved)) {
                auto sizes = splitter->sizes();
                for (int i = 0; i < sizes.size(); ++i)
                    sizes[i] = splitter->widget(i) == pane ? 440 : splitter->widget(i)->isVisible() ? 160 : 0;
                splitter->setSizes(sizes);
            }
            QTimer::singleShot(25, pane, [editor, before] {
                if (!mscore)
                    return;
                if (editor->layout())
                    editor->layout()->activate();
                if (static_cast<QWidget *>(mscore)->layout())
                    static_cast<QWidget *>(mscore)->layout()->activate();
                if (before.isValid() && !mscore->isMaximized())
                    mscore->resize(before.boundedTo(mscore->size()));
                QWidget *owner = editor;
                while (owner && !qobject_cast<QDockWidget *>(owner))
                    owner = owner->parentWidget();
                if (auto dock = qobject_cast<QDockWidget *>(owner))
                    if (!dock->isFloating() && mscore->property("kumoWorkspaceView").toString()!="daw")
                        mscore->resizeDocks({dock}, {qBound(280, mscore->height() / 2, 520)}, Qt::Vertical);
            });
        } else {
            pane->hide();
            if (state->active) {
                for (auto it = state->visible.cbegin(); it != state->visible.cend(); ++it)
                    it.key()->setVisible(it.value());
                state->active = false;
            }
        }
    });
}
} // namespace Ms
