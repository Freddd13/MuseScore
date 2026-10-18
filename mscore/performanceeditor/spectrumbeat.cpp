// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumbeat.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <cmath>
namespace Ms {
SpectrumBeatControls::SpectrumBeatControls(QWidget *parent) : QWidget(parent) {
    setObjectName("spectrumBeatControls");
    _clock.start();
    auto grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(8);
    _visible = new QCheckBox(tr("节拍网格"));
    _visible->setObjectName("spectrumBeatGrid");
    grid->addWidget(_visible, 0, 0);
    connect(_visible, &QCheckBox::clicked, this, [this] { edited(); });
    _source = new QComboBox;
    _source->addItems({tr("原曲节拍"), tr("乐谱拍位（有关联时）")});
    grid->addWidget(_source, 0, 1, 1, 2);
    connect(_source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { edited(); });
    _bpm = new QDoubleSpinBox;
    _bpm->setObjectName("spectrumGridBpm");
    _bpm->setRange(1, 4000);
    _bpm->setDecimals(2);
    _bpm->setSuffix(tr(" BPM"));
    _bpm->setValue(120);
    grid->addWidget(new QLabel(tr("网格速度 ♩")), 1, 0);
    grid->addWidget(_bpm, 1, 1);
    auto multiplier = new QWidget;
    auto row = new QHBoxLayout(multiplier);
    row->setContentsMargins(0, 0, 0, 0);
    for (const auto &item :
         QList<QPair<QString, double>>{{QString::fromUtf8("÷2"), .5}, {QString::fromUtf8("×2"), 2.}}) {
        auto button = new QToolButton;
        button->setText(item.first);
        row->addWidget(button);
        connect(button, &QToolButton::clicked, this,
                [this, item] { _bpm->setValue(_bpm->value() * item.second); });
    }
    grid->addWidget(multiplier, 1, 2);
    connect(_bpm, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { edited(); });
    _numerator = new QSpinBox;
    _numerator->setRange(1, 16);
    _numerator->setValue(4);
    _denominator = new QComboBox;
    for (int denominator : {2, 4, 8, 16, 32})
        _denominator->addItem(QString::number(denominator), denominator);
    _denominator->setCurrentIndex(1);
    grid->addWidget(new QLabel(tr("拍号")), 2, 0);
    grid->addWidget(_numerator, 2, 1);
    grid->addWidget(_denominator, 2, 2);
    connect(_numerator, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { edited(); });
    connect(_denominator, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { edited(); });
    _first = new QDoubleSpinBox;
    _first->setObjectName("spectrumFirstBeat");
    _first->setRange(0, 1e8);
    _first->setDecimals(3);
    _first->setSingleStep(.01);
    _first->setSuffix(tr(" 秒"));
    grid->addWidget(new QLabel(tr("第一拍")), 3, 0);
    grid->addWidget(_first, 3, 1);
    connect(_first, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { edited(); });
    auto first = new QToolButton;
    first->setText(tr("这里为第一拍"));
    first->setToolTip(tr("将当前播放位置或频谱光标作为第一小节第一拍。"));
    grid->addWidget(first, 3, 2);
    connect(first, &QToolButton::clicked, this, &SpectrumBeatControls::firstBeatRequested);
    _estimate = new QToolButton;
    _estimate->setObjectName("spectrumEstimateTempo");
    _estimate->setText(tr("估计这段速度"));
    _estimate->setToolTip(tr("优先分析循环片段；其它情况下，从当前画面起分析至少 4 秒、最多 2 分钟。"));
    grid->addWidget(_estimate, 4, 0);
    connect(_estimate, &QToolButton::clicked, this, &SpectrumBeatControls::estimateRequested);
    _candidates = new QComboBox;
    _candidates->setObjectName("spectrumTempoCandidates");
    _candidates->addItem(tr("估计后选择候选"));
    _candidates->setEnabled(false);
    grid->addWidget(_candidates, 4, 1, 1, 2);
    connect(_candidates, QOverload<int>::of(&QComboBox::activated), this, &SpectrumBeatControls::choose);
    auto tapButton = new QToolButton;
    tapButton->setObjectName("spectrumTap");
    tapButton->setText(tr("跟拍"));
    tapButton->setToolTip(tr("跟着音乐点至少 4 拍；也可在频谱中按 Shift+空格。"));
    grid->addWidget(tapButton, 5, 0);
    connect(tapButton, &QToolButton::clicked, this, &SpectrumBeatControls::tapRequested);
    _unit = new QComboBox;
    _unit->addItems({tr("每次四分音符"), tr("每次八分音符"), tr("每次附点四分"), tr("每次二分音符")});
    grid->addWidget(_unit, 5, 1);
    auto reset = new QToolButton;
    reset->setText(tr("重测"));
    grid->addWidget(reset, 5, 2);
    connect(reset, &QToolButton::clicked, this, [this] {
        _tap.reset();
        _lastTap = -1;
        _status->setText(tr("跟着音乐点击，至少记录 4 拍。"));
    });
    auto save = new QToolButton;
    save->setText(tr("保存节拍"));
    save->setObjectName("spectrumSaveGrid");
    grid->addWidget(save, 6, 0);
    connect(save, &QToolButton::clicked, this, &SpectrumBeatControls::saveRequested);
    _status = new QLabel(tr("调整速度和第一拍后，对照音乐检查网格，再保存到乐谱。"));
    _status->setWordWrap(true);
    grid->addWidget(_status, 6, 1, 1, 2);
    grid->setColumnStretch(1, 1);
}
QVariantMap SpectrumBeatControls::values() const {
    return {{"visible", _visible->isChecked()},
            {"source", _source->currentIndex()},
            {"bpm", _bpm->value()},
            {"numerator", _numerator->value()},
            {"denominator", _denominator->currentData()},
            {"firstBeat", _first->value() * 1000}};
}
void SpectrumBeatControls::restore(const QVariantMap &value) {
    _loading = true;
    _visible->setChecked(value.value("visible", false).toBool());
    _source->setCurrentIndex(value.value("source", 0).toInt());
    _bpm->setValue(value.value("bpm", 120.).toDouble());
    _numerator->setValue(value.value("numerator", 4).toInt());
    _denominator->setCurrentIndex(qMax(0, _denominator->findData(value.value("denominator", 4).toInt())));
    _first->setValue(value.value("firstBeat", 0.).toDouble() / 1000);
    _choices.clear();
    _candidates->clear();
    _candidates->addItem(tr("估计后选择候选"));
    _candidates->setEnabled(false);
    _tap.reset();
    _lastTap = -1;
    _loading = false;
}
void SpectrumBeatControls::edited() {
    if (_loading)
        return;
    _status->setText(tr("网格已调整；点“保存节拍”可随乐谱保留。"));
    emit changed();
}
void SpectrumBeatControls::setFirstBeat(double ms) {
    _first->setValue(qMax(0., ms) / 1000);
    _visible->setChecked(true);
    edited();
}
void SpectrumBeatControls::tap(double sourceMs, bool playing) {
    const qint64 timestamp = playing ? qRound64(sourceMs) : _clock.elapsed();
    if (_tapSource != playing || timestamp < _lastTap) {
        _tap.reset();
        _lastTap = -1;
    }
    _tapSource = playing;
    if (!_tap.count())
        _firstTap = sourceMs;
    if (!_tap.add(timestamp))
        return;
    _lastTap = timestamp;
    if (_tap.count() == 1)
        _firstTap = sourceMs;
    const double units[] = {1., .5, 1.5, 2.};
    if (_tap.ready()) {
        _bpm->setValue(_tap.bpm(units[_unit->currentIndex()]));
        _first->setValue(_firstTap / 1000);
        _visible->setChecked(true);
        edited();
    }
    _status->setText(tr("已记录 %1 拍 · %2")
                         .arg(_tap.count())
                         .arg(_tap.ready() ? (_tap.stable() ? tr("稳定") : tr("继续敲几拍")) : tr("起步")));
}
void SpectrumBeatControls::setBusy(bool busy) {
    _estimate->setEnabled(!busy);
    if (busy)
        _status->setText(tr("正在估计这一段的速度…"));
}
void SpectrumBeatControls::choose(int index) {
    if (index < 0 || index >= _choices.size())
        return;
    const auto value = _choices[index].toMap();
    _bpm->setValue(value.value("bpm").toDouble());
    _first->setValue(value.value("firstBeat").toDouble() / 1000);
    _visible->setChecked(true);
    edited();
}
void SpectrumBeatControls::setCandidates(const QVariantMap &result) {
    if (result.contains("error")) {
        _status->setText(result.value("error").toString());
        return;
    }
    _choices = result.value("candidates").toList();
    _candidates->clear();
    _candidates->setEnabled(!_choices.isEmpty());
    for (auto value : _choices) {
        const auto candidate = value.toMap();
        _candidates->addItem(tr("%1 BPM · 第一拍 %2 秒")
                                 .arg(candidate.value("bpm").toDouble(), 0, 'f', 2)
                                 .arg(candidate.value("firstBeat").toDouble() / 1000, 0, 'f', 3));
    }
    if (!_choices.isEmpty())
        choose(0);
    _status->setText(result.value("status").toString());
}
} // namespace Ms
