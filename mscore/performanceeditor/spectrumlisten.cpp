// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumlisten.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
namespace Ms {
SpectrumListenControls::SpectrumListenControls(QWidget *parent) : QWidget(parent) {
    setObjectName("spectrumListenControls");
    QSettings settings;
    for (const auto &key : QStringList{"low", "middle", "high"})
        _values[key] = settings.value("spectrum/listen/" + key, 0.);
    _values["enabled"] = settings.value("spectrum/listen/enabled", false);
    _values["selectedFrequency"] = false;
    _values["width"] = settings.value("spectrum/listen/width", .5);
    _values["frequency"] = 440.;
    auto grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(8);
    auto enabled = new QCheckBox(tr("试听均衡"));
    enabled->setChecked(_values["enabled"].toBool());
    grid->addWidget(enabled, 0, 0, 1, 2);
    connect(enabled, &QCheckBox::clicked, this, [this](bool value) { save("enabled", value); });
    const QStringList keys{"low", "middle", "high"}, labels{tr("低音"), tr("中音"), tr("高音")};
    QList<QDoubleSpinBox *> numbers;
    for (int i = 0; i < 3; ++i) {
        const auto key = keys[i];
        auto bar = new QSlider(Qt::Horizontal);
        bar->setObjectName("spectrumListen_" + key);
        bar->setRange(-12, 12);
        bar->setTickPosition(QSlider::TicksBelow);
        bar->setTickInterval(6);
        auto number = new QDoubleSpinBox;
        number->setRange(-12, 12);
        number->setDecimals(0);
        number->setSuffix(tr(" dB"));
        number->setValue(_values[key].toDouble());
        bar->setValue(qRound(number->value()));
        numbers.append(number);
        grid->addWidget(new QLabel(labels[i]), i + 1, 0);
        grid->addWidget(bar, i + 1, 1);
        grid->addWidget(number, i + 1, 2);
        connect(bar, &QSlider::valueChanged, this, [this, key, number](int value) {
            QSignalBlocker block(number);
            number->setValue(value);
            save(key, value);
        });
        connect(number, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, key, bar](double value) {
                    QSignalBlocker block(bar);
                    bar->setValue(qRound(value));
                    save(key, value);
                });
    }
    auto reset = new QToolButton;
    reset->setText(tr("恢复平直"));
    grid->addWidget(reset, 0, 2);
    connect(reset, &QToolButton::clicked, this, [numbers] {
        for (auto number : numbers)
            number->setValue(0);
    });
    auto selected = new QCheckBox(tr("按选频听"));
    selected->setObjectName("spectrumListenSelected");
    grid->addWidget(selected, 4, 0);
    connect(selected, &QCheckBox::clicked, this, [this](bool value) { save("selectedFrequency", value); });
    _frequency = new QLabel(tr("440 Hz"));
    grid->addWidget(_frequency, 4, 1);
    auto width = new QDoubleSpinBox;
    width->setRange(.1, 4);
    width->setDecimals(2);
    width->setSingleStep(.1);
    width->setValue(_values["width"].toDouble());
    width->setSuffix(tr(" 八度"));
    width->setToolTip(tr("围绕所选频率的试听宽度；较小的值适合分辨一个音。"));
    grid->addWidget(width, 4, 2);
    connect(width, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double value) { save("width", value); });
    auto help =
        new QLabel(tr("在频谱中点选频率后试听。关闭试听均衡和按选频听即可听原声；音量继续由原曲参考控制。"));
    help->setWordWrap(true);
    grid->addWidget(help, 5, 0, 1, 3);
    grid->setColumnStretch(1, 1);
}
void SpectrumListenControls::save(const QString &key, const QVariant &value) {
    _values[key] = value;
    if (key != "selectedFrequency")
        QSettings().setValue("spectrum/listen/" + key, value);
    emit changed();
}
void SpectrumListenControls::setFrequency(double value) {
    _values["frequency"] = value;
    _frequency->setText(tr("%1 Hz").arg(value, 0, 'f', 1));
}
} // namespace Ms
