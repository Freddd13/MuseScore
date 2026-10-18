// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumdisplay.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <cmath>
#include <cstring>

namespace Ms {
SpectrumDisplayControls::SpectrumDisplayControls(QWidget *parent) : QWidget(parent) {
    setObjectName("spectrumDisplayControls");
    const QVariantMap defaults{{"palette", 0},
                               {"gain", 0.},
                               {"contrast", 1.},
                               {"fundamental", 0.},
                               {"opacity", .55},
                               {"showNotes", true},
                               {"middle", QString("#20a5b6")},
                               {"high", QString("#f5db87")}};
    QSettings settings;
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
        _values[it.key()] = settings.value("spectrum/display/" + it.key(), it.value());
    auto grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(8);
    auto palette = new QComboBox;
    palette->setObjectName("spectrumPalette");
    palette->addItems({tr("蓝金"), tr("绿紫"), tr("灰度"), tr("自定颜色")});
    palette->setCurrentIndex(qBound(0, _values["palette"].toInt(), 3));
    grid->addWidget(new QLabel(tr("配色")), 0, 0);
    grid->addWidget(palette, 0, 1);
    connect(palette, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int n) { save("palette", n); });
    auto colors = new QWidget;
    auto colorRow = new QHBoxLayout(colors);
    colorRow->setContentsMargins(0, 0, 0, 0);
    for (const auto &entry : QList<QPair<QString, QString>>{{"middle", tr("主色…")}, {"high", tr("亮色…")}}) {
        auto button = new QToolButton;
        button->setText(entry.second);
        colorRow->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, entry, palette] {
            const auto color =
                QColorDialog::getColor(QColor(_values[entry.first].toString()), this, entry.second);
            if (color.isValid()) {
                save(entry.first, color.name());
                palette->setCurrentIndex(3);
            }
        });
    }
    grid->addWidget(colors, 0, 2);
    auto slider = [this, grid](int row, const QString &key, const QString &label, double low, double high,
                               double step, const QString &suffix) {
        grid->addWidget(new QLabel(label), row, 0);
        auto bar = new QSlider(Qt::Horizontal);
        bar->setObjectName("spectrumDisplay_" + key);
        bar->setRange(0, qRound((high - low) / step));
        bar->setTickPosition(QSlider::TicksBelow);
        bar->setTickInterval(qMax(1, bar->maximum() / 4));
        auto number = new QDoubleSpinBox;
        number->setRange(low, high);
        number->setSingleStep(step);
        number->setDecimals(step < 1 ? 2 : 0);
        number->setSuffix(suffix);
        number->setValue(qBound(low, _values[key].toDouble(), high));
        bar->setValue(qRound((number->value() - low) / step));
        grid->addWidget(bar, row, 1);
        grid->addWidget(number, row, 2);
        connect(bar, &QSlider::valueChanged, this, [this, key, number, low, step](int value) {
            QSignalBlocker block(number);
            number->setValue(low + value * step);
            save(key, number->value());
        });
        connect(number, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, key, bar, low, step](double value) {
                    QSignalBlocker block(bar);
                    bar->setValue(qRound((value - low) / step));
                    save(key, value);
                });
    };
    slider(1, "gain", tr("灵敏度"), -24, 36, 1, tr(" dB"));
    slider(2, "contrast", tr("对比度"), .25, 3., .05, QString());
    slider(3, "fundamental", tr("突出基音"), 0, 1, .05, QString());
    slider(4, "opacity", tr("叠加浓度"), .1, 1., .05, QString());
    auto notes = new QCheckBox(tr("叠加时显示谱中音符块"));
    notes->setObjectName("spectrumShowNotes");
    notes->setChecked(_values["showNotes"].toBool());
    grid->addWidget(notes, 5, 0, 1, 3);
    connect(notes, &QCheckBox::clicked, this, [this](bool on) { save("showNotes", on); });
    auto help =
        new QLabel(tr("灵敏度和对比度调整亮线。突出基音会淡化符合低音倍频关系的线条，归零可对照原始频谱。"));
    help->setWordWrap(true);
    grid->addWidget(help, 6, 0, 1, 3);
    grid->setColumnStretch(1, 1);
}
void SpectrumDisplayControls::save(const QString &key, const QVariant &value) {
    _values[key] = value;
    QSettings().setValue("spectrum/display/" + key, value);
    emit changed(key == "fundamental");
}
QImage colorSpectrum(const QImage &raw, const QVariantMap &style, bool overlay) {
    if (raw.isNull())
        return {};
    QColor low("#111925"), middle(style.value("middle", "#20a5b6").toString()),
        high(style.value("high", "#f5db87").toString());
    switch (style.value("palette").toInt()) {
    case 0:
        middle = QColor("#20a5b6");
        high = QColor("#f5db87");
        break;
    case 1:
        low = QColor("#141722");
        middle = QColor("#63b79d");
        high = QColor("#d6a5df");
        break;
    case 2:
        low = QColor("#15191d");
        middle = QColor("#81919b");
        high = QColor("#ecf0f2");
        break;
    }
    QVector<QRgb> table;
    table.reserve(256);
    const double offset = style.value("gain", 0.).toDouble() / 85.,
                 contrast = style.value("contrast", 1.).toDouble();
    for (int i = 0; i < 256; ++i) {
        const double value = std::pow(qBound(0., i / 255. + offset, 1.), qBound(.25, contrast, 3.));
        const bool upper = value > .6;
        const QColor a = upper ? middle : low, b = upper ? high : middle;
        const double t = upper ? (value - .6) / .4 : value / .6;
        QColor c(qRound(a.red() + (b.red() - a.red()) * t), qRound(a.green() + (b.green() - a.green()) * t),
                 qRound(a.blue() + (b.blue() - a.blue()) * t));
        if (overlay)
            c.setAlpha(qRound(255 * value * style.value("opacity", .55).toDouble()));
        table.append(c.rgba());
    }
    QImage result(raw.size(), QImage::Format_Indexed8);
    result.setColorTable(table);
    for (int y = 0; y < raw.height(); ++y)
        std::memcpy(result.scanLine(y), raw.constScanLine(y), size_t(raw.width()));
    return result;
}
} // namespace Ms
