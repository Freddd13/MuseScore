// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "editorsettings.h"
#include "libmscore/dynamic.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/score.h"
#include "libmscore/style.h"
#include "mscore/seq.h"
#include "mscore/musescore.h"
#include <QWindow>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace Ms {
void showPerformanceSettings(Score* score, QWidget* parent, std::function<void()> saved) {
    // Native dialogs inherit the application theme, independent of roll colors.
    QWidget* owner=mscore ? static_cast<QWidget*>(mscore) : parent->window();
    if (auto existing=owner->findChild<QDialog*>("performanceSettings")) {
        if(existing->property("targetScore").value<QObject*>()==score) {
            existing->show(); existing->raise(); existing->activateWindow(); return;
        }
        existing->setObjectName("closedPerformanceSettings");existing->close();
    }
    auto dialog=new QDialog(owner);
    dialog->setProperty("targetScore",QVariant::fromValue<QObject*>(score));
    dialog->setObjectName("performanceSettings");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("演奏编辑设置"));
    dialog->resize(480, 560);
    auto layout=new QVBoxLayout(dialog);
    auto tabs=new QTabWidget(dialog); tabs->setObjectName("performanceSettingsTabs");
    layout->addWidget(tabs);
    auto input=new QWidget(tabs); auto keys=new QVBoxLayout(input);
    auto explanation=new QLabel(QObject::tr("按住快捷键查看鼠标附近的音符力度；同时按住 Shift 可查看整个视口。松开后恢复原来的显示。"), input);
    explanation->setWordWrap(true); keys->addWidget(explanation);
    keys->addWidget(new QLabel(QObject::tr("临时查看音旁力度"), input));
    auto key=new QKeySequenceEdit(QKeySequence(QSettings().value("performanceEditor/peekKey", "Ctrl+Alt+V").toString()), input);
    key->setObjectName("performancePeekKey"); keys->addWidget(key);
    keys->addWidget(new QLabel(QObject::tr("谱面最近音滚轮快捷键"), input));
    auto wheel=new QComboBox(input); wheel->setObjectName("performanceNearestWheelKey");
    wheel->addItem("Alt+Shift", int(Qt::AltModifier|Qt::ShiftModifier));
    wheel->addItem("Ctrl+Alt", int(Qt::ControlModifier|Qt::AltModifier));
    wheel->addItem("Ctrl+Shift", int(Qt::ControlModifier|Qt::ShiftModifier));
    wheel->addItem(QObject::tr("关闭"), 0);
    wheel->setCurrentIndex(qMax(0,wheel->findData(QSettings().value("performanceEditor/nearestWheelModifiers", int(Qt::AltModifier|Qt::ShiftModifier)))));
    keys->addWidget(wheel); keys->addStretch();
    auto inputScroll=new QScrollArea(tabs);inputScroll->setWidgetResizable(true);inputScroll->setFrameShape(QFrame::NoFrame);inputScroll->setWidget(input);
    tabs->addTab(inputScroll, QObject::tr("鼠标与按键"));

    auto dynamics=new QWidget(tabs); auto body=new QVBoxLayout(dynamics);
    auto source=new QLabel(score ? QObject::tr("乐谱：%1").arg(score->name()) : QObject::tr("打开乐谱后可设置力度记号。"), dynamics);
    source->setObjectName("performanceSettingsScore");source->setWordWrap(true); body->addWidget(source);
    auto description=new QLabel(QObject::tr("设置这份乐谱中 p、mp、mf 等记号的默认播放力度。音符的相对增减会随参考线一起变化。"), dynamics);
    description->setObjectName("performanceSettingsDescription");description->setWordWrap(true); body->addWidget(description);
    auto table=new QTableWidget(14, 3, dynamics); table->setObjectName("performanceDynamicsDefaults");
    table->setHorizontalHeaderLabels({QObject::tr("力度记号"), QObject::tr("MIDI 力度"), QObject::tr("原版值")});
    table->setSelectionMode(QAbstractItemView::NoSelection); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(qMax(28,table->fontMetrics().height()+10));
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);table->setAlternatingRowColors(true);
    // Common marks first; extreme dynamics remain available in the same table.
    const QList<int> order{4,5,6,7,8,9,10,11,1,2,3,12,13,14};
    QList<QSpinBox*> values;
    for (int row=0; row<order.size(); ++row) {
        const int type=order[row]; auto value=new QSpinBox(table);
        value->setObjectName("dynamicDefault_"+QString::fromLatin1(dynList[type].tag));
        value->setRange(1,127); value->setAlignment(Qt::AlignRight);
        value->setValue(score ? score->style().dynamicsVelocity(type,dynList[type].velocity) : dynList[type].velocity);
        table->setItem(row,0,new QTableWidgetItem(QString::fromLatin1(dynList[type].tag)));
        table->setCellWidget(row,1,value); auto original=new QTableWidgetItem(QString::number(dynList[type].velocity));
        original->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter); table->setItem(row,2,original);
        table->setRowHidden(row,row>=8); values.append(value);
    }
    auto includeExisting=new QCheckBox(QObject::tr("同时更新采用旧默认值的记号"),dynamics);
    includeExisting->setObjectName("performanceDynamicsIncludeExisting");includeExisting->setChecked(QSettings().value("performanceEditor/updateExistingDynamics",true).toBool());
    includeExisting->setToolTip(QObject::tr("例如，mp从64改到58时，乐谱中仍采用64的mp也改到58。其它数值继续保留。"));
    body->addWidget(includeExisting);
    auto extra=new QCheckBox(QObject::tr("显示极弱、极强记号"),dynamics);
    extra->setObjectName("performanceDynamicsShowAll"); body->addWidget(extra);
    const auto fitRows=[table](int rows){table->setFixedHeight(table->horizontalHeader()->sizeHint().height()+rows*table->verticalHeader()->defaultSectionSize()+2*table->frameWidth());};
    fitRows(8);
    QObject::connect(extra,&QCheckBox::toggled,table,[table,fitRows](bool on){
        for(int row=8;row<14;++row)table->setRowHidden(row,!on);
        fitRows(on?14:8);
    });
    body->addWidget(table);
    auto restore=new QPushButton(QObject::tr("恢复原版值"), dynamics);
    restore->setObjectName("performanceDynamicsReset");
    body->addWidget(restore,0,Qt::AlignLeft);
    QObject::connect(restore,&QPushButton::clicked,dialog,[values,order]{for(int row=0;row<order.size();++row)values[row]->setValue(dynList[order[row]].velocity);});
    body->addStretch();
    dynamics->setEnabled(score && !score->masterScore()->readOnly());
    auto dynamicsScroll=new QScrollArea(tabs);dynamicsScroll->setObjectName("performanceDynamicsScroll");
    dynamicsScroll->setWidgetResizable(true);dynamicsScroll->setFrameShape(QFrame::NoFrame);dynamicsScroll->setWidget(dynamics);
    tabs->addTab(dynamicsScroll, QObject::tr("力度记号"));
    auto status=new QLabel(dialog); status->setObjectName("performanceSettingsStatus"); status->setWordWrap(true); layout->addWidget(status);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("取消")); layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QPointer<QWidget> editor=parent;
    QPointer<Score> target=score ? score->masterScore() : nullptr;
    QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[=]{
        QVariantMap changed;
        for(int row=0;row<order.size();++row)if(values[row]->value()!=dynList[order[row]].velocity)
            changed[QString::fromLatin1(dynList[order[row]].tag)]=values[row]->value();
        const QString text=changed.isEmpty()?QString():QString::fromUtf8(QJsonDocument::fromVariant(changed).toJson(QJsonDocument::Compact));
        const bool profileChanged=target && text!=target->styleSt(Sid::dynamicsPlaybackVelocities);
        QVector<QPair<Dynamic*,int>> marks;
        if(target && includeExisting->isChecked() && (profileChanged || tabs->currentIndex()==1))for(auto part:target->scoreList())
            for(auto segment=part->firstMeasure()?part->firstMeasure()->first():nullptr;segment;segment=segment->next1())
                for(auto element:segment->annotations())if(element->isDynamic()){
                    auto mark=toDynamic(element);const int type=int(mark->dynamicType());
                    if(type<1 || type>14)continue;
                    const int old=part->style().dynamicsVelocity(type,dynList[type].velocity);
                    const auto index=order.indexOf(type);const int next=values[index]->value();
                    if((mark->velocity()==old || mark->velocity()==dynList[type].velocity) && mark->velocity()!=next)marks.append(qMakePair(mark,next));
                }
        if(target && !target->readOnly() && (profileChanged || !marks.isEmpty())) {
            if(seq && (seq->isPlaying() || !seq->backgroundRenderingIdle())) {
                status->setText(QObject::tr("停止播放后点“应用”，即可更新力度。这个窗口可以保持打开。")); return;
            }
            target->startCmd();
            for(const auto& item:marks)item.first->undoChangeProperty(Pid::VELOCITY,item.second);
            for(auto part:target->scoreList())part->undoChangeStyleVal(Sid::dynamicsPlaybackVelocities,text);
            target->setPlaylistDirty(); target->endCmd();
        }
        QSettings settings; settings.setValue("performanceEditor/updateExistingDynamics",includeExisting->isChecked());settings.setValue("performanceEditor/nearestWheelModifiers",wheel->currentData());
        settings.setValue("performanceEditor/peekKey",key->keySequence().toString());
        if(saved && editor)saved(); dialog->accept();
    });
    if(score)QObject::connect(score,&QObject::destroyed,dialog,&QDialog::reject);
    QObject::connect(parent,&QObject::destroyed,dialog,&QDialog::reject);
    dialog->show();
    if(dialog->windowHandle() && parent->window()->windowHandle())
        dialog->windowHandle()->setTransientParent(parent->window()->windowHandle());
    dialog->raise();dialog->activateWindow();
}
}
