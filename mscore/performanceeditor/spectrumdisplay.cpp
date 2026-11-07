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
#include <QMenu>
#include <QWidgetAction>
#include <QFormLayout>
#include <QSpinBox>
#include <cmath>
#include <cstring>

namespace Ms {
SpectrumDisplayControls::SpectrumDisplayControls(QWidget *parent) : QWidget(parent) {
    setObjectName("spectrumDisplayControls");
    const QVariantMap defaults{{"palette", 4},
                               {"gain", 0.},
                               {"contrast", 1.},
                               {"fundamental", 0.},
                               {"opacity", .55},
                               {"showNotes", true},
                               {"showMidi",true},
                               {"harmonicCount",10},
                               {"harmonicOpacity",.85},
                               {"harmonicColor",QString("#ffdd88")},
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
    palette->addItems({tr("蓝金"), tr("绿紫"), tr("灰度"), tr("自定颜色"), tr("WaveTone 强弱色")});
    palette->setCurrentIndex(qBound(0, _values["palette"].toInt(), 4));
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
    auto noteOptions=new QWidget;auto noteRow=new QHBoxLayout(noteOptions);noteRow->setContentsMargins(0,0,0,0);
    noteRow->addWidget(notes);
    auto midi=new QCheckBox(tr("显示MIDI轨道"));midi->setObjectName("spectrumShowMidi");midi->setChecked(_values["showMidi"].toBool());
    midi->setToolTip(tr("在频谱上对照MIDI轨道列表中开启显示的音符。静音轨也可作参考。"));
    noteRow->addWidget(midi);noteRow->addStretch(1);grid->addWidget(noteOptions,5,0,1,3);
    connect(midi,&QCheckBox::clicked,this,[this](bool on){save("showMidi",on);});
    connect(notes, &QCheckBox::clicked, this, [this](bool on) { save("showNotes", on); });
    auto help =
        new QLabel(tr("WaveTone 强弱色：黑、蓝、青、绿、黄、橙、红依次增强。灵敏度和对比度调整亮线；“突出基音”淡化倍频线，归零查看原始频谱。"));
    help->setWordWrap(true);
    auto reference=new QToolButton;reference->setObjectName("spectrumReferenceSettings");reference->setText(tr("参考线…"));reference->setPopupMode(QToolButton::InstantPopup);
    auto menu=new QMenu(reference);reference->setMenu(menu);auto body=new QWidget;auto form=new QFormLayout(body);
    auto count=new QSpinBox;count->setObjectName("spectrumHarmonicCount");count->setRange(1,16);count->setValue(qBound(1,_values["harmonicCount"].toInt(),16));
    form->addRow(tr("显示几倍"),count);connect(count,QOverload<int>::of(&QSpinBox::valueChanged),this,[this](int n){save("harmonicCount",n);});
    auto strength=new QWidget;auto strengthRow=new QHBoxLayout(strength);strengthRow->setContentsMargins(0,0,0,0);
    auto alpha=new QSlider(Qt::Horizontal);alpha->setObjectName("spectrumHarmonicOpacity");alpha->setRange(0,100);alpha->setMinimumWidth(140);alpha->setValue(qRound(_values["harmonicOpacity"].toDouble()*100));
    auto number=new QSpinBox;number->setRange(0,100);number->setSuffix("%");number->setValue(alpha->value());strengthRow->addWidget(alpha);strengthRow->addWidget(number);form->addRow(tr("强度"),strength);
    connect(alpha,&QSlider::valueChanged,this,[this,number](int n){QSignalBlocker block(number);number->setValue(n);save("harmonicOpacity",n/100.);});
    connect(number,QOverload<int>::of(&QSpinBox::valueChanged),this,[this,alpha](int n){QSignalBlocker block(alpha);alpha->setValue(n);save("harmonicOpacity",n/100.);});
    auto lineColor=new QToolButton;lineColor->setText(tr("选择颜色…"));form->addRow(tr("颜色"),lineColor);
    connect(lineColor,&QToolButton::clicked,this,[this]{const auto color=QColorDialog::getColor(QColor(_values["harmonicColor"].toString()),this,tr("基音与倍频参考线"));if(color.isValid())save("harmonicColor",color.name());});
    auto action=new QWidgetAction(menu);action->setDefaultWidget(body);menu->addAction(action);grid->addWidget(reference,6,0,1,3,Qt::AlignLeft);
    grid->addWidget(help, 7, 0, 1, 3);
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
        if (style.value("palette", 4).toInt() == 4) {
            static const QColor colors[] = {QColor("#000000"), QColor("#0046dc"), QColor("#00cfff"),
                QColor("#20d34b"), QColor("#ffe32d"), QColor("#ff921c"), QColor("#ef3434")};
            const double position = value * 6;
            const int stop = qMin(5, int(position));
            const double blend = position - stop;
            const auto& left = colors[stop]; const auto& right = colors[stop + 1];
            c = QColor(qRound(left.red() + (right.red() - left.red()) * blend),
                       qRound(left.green() + (right.green() - left.green()) * blend),
                       qRound(left.blue() + (right.blue() - left.blue()) * blend));
        }
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
