// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "arrangement.h"
#include "toolicons.h"
#include "featuregate.h"
#include "localsuggestions.h"
#include "paneldock.h"
#include "workflowcontext.h"
#include "regionalcomparison.h"
#include "playbackpreview.h"
#include "mscore/scoreview.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/shortcut.h"
#include "mscore/plugin/qmlpluginengine.h"
#include "libmscore/measure.h"
#include "libmscore/repeatlist.h"
#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QTextBrowser>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUuid>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSettings>

namespace Ms {
namespace {
class SuggestionPanel : public QWidget {
    PluginAPI::ScoreObserver _observer;
    QTreeWidget *_choices;
    QTreeWidget *_changes;
    QLabel *_scope;
    QTextBrowser *_explanation;
    QLabel *_status;
    QString _selected;
    QPushButton *_showDifferences = nullptr;
    QPushButton *_cancelLocal = nullptr;
    QString _seenRevision;
    QVariantList _items;
    LocalSuggestionTask _local;
    QVariantMap _localSettings;
    QComboBox *_kind;
    QComboBox *_hand;
    QCheckBox *_lockFingers;
    QCheckBox *_preserveMelody;
    Score *score() const { return mscore ? mscore->currentScore() : nullptr; }
    bool attach() {
        if (!score())
            return false;
        PluginAPI::Score wrapped(score());
        _observer.setScore(&wrapped);
        return true;
    }
    QVariantMap selected() const {
        for (auto v : _items)
            if (v.toMap().value("id") == _selected)
                return v.toMap();
        return {};
    }
    QVariantMap inputSettings() const {
        auto settings=_localSettings;
        settings["hand"]=_hand->currentData();
        settings["lockFingers"]=_lockFingers->isChecked();
        settings["preserveMelody"]=_preserveMelody->isChecked();
        settings["preserveTracks"]=_preserveMelody->isChecked()?QVariantList{0}:QVariantList{};
        return settings;
    }
    bool matchesInputs(const QVariantMap& option) const {
        if(option.value("localKind").toString().isEmpty())return true;
        return option.value("localKind")==_kind->currentData()
            && option.value("localSettings").toMap()==inputSettings()
            && score() && option.value("pianoSettings").toMap()==score()->property("kumoPianoSettings").toMap();
    }
    bool sourceCurrent(const QVariantMap& option) {
        return !option.isEmpty() && attach()
            && option.value("snapshotId").toString() == _observer.analysisContentRevision();
    }
    void updateButtons() {
        const auto option=selected();const bool ready=!_local.running()&&!option.isEmpty()&&!option.value("used").toBool()&&matchesInputs(option)&&sourceCurrent(option);
        if(_cancelLocal)_cancelLocal->setEnabled(_local.running());
        for(auto button:findChildren<QPushButton*>())if(button->objectName().startsWith("arrangement-"))
            button->setEnabled(button->objectName()=="arrangement-stop"?!option.isEmpty():ready);
    }
    void inputsChanged() {
        _local.cancel();stopPlaybackPreview(true);updateButtons();updateDifference();
        if(!selected().isEmpty()&&!matchesInputs(selected()))_status->setText(tr("设置已改变，点“分析选段”更新建议。"));
    }
    void updateDifference() {
        if (!mscore || !mscore->currentScoreView()) return;
        const auto option = selected();
        if (isVisible() && !_local.running() && _showDifferences && _showDifferences->isChecked() && !option.isEmpty() && !option.value("used").toBool() && matchesInputs(option) && sourceCurrent(option))
            operate("preview");
        else
            showRegionalDifference(mscore->currentScoreView(), nullptr, {});
    }
    void details() {
        auto option = selected();
        auto scope = option.value("scope").toMap();
        const auto first=scope.value("firstMeasure").toInt(),last=scope.value("lastMeasure").toInt();
        const auto measures=first==last?QString::number(first):tr("%1–%2").arg(first).arg(last);
        _scope->setText(option.isEmpty() ? tr("在乐谱中选几小节，选择要比较的内容，再点“分析选段”。Codex 发来的写法也列在这里。")
                                         : tr("第%1小节 · %2处变化")
                                               .arg(measures)
                                               .arg(option.value("differences").toList().size()));
        _explanation->setPlainText(option.value("summary").toString());
        _changes->clear();
        for (auto value : option.value("differences").toList()) {
            auto row = value.toMap();
            const auto old = row.value("current").toMap(), next = row.value("other").toMap();
            QStringList types;
            auto a = old.value("notes").toList(), b = next.value("notes").toList();
            if (old.value("duration") != next.value("duration") || old.value("rest") != next.value("rest"))
                types << tr("节奏");
            if (a.size() != b.size())
                types << tr("增减音");
            bool pitch = false, velocity = false, marks = false;
            for (int i = 0; i < qMin(a.size(), b.size()); ++i) {
                auto x = a[i].toMap(), y = b[i].toMap();
                pitch |= x.value("pitch") != y.value("pitch") || x.value("tpc1") != y.value("tpc1");
                velocity |= x.value("velocityType") != y.value("velocityType") ||
                            x.value("velocityRaw") != y.value("velocityRaw");
                if (x.value("fingerings") != y.value("fingerings")) {
                    auto from=x.value("fingerings").toStringList(),to=y.value("fingerings").toStringList();
                    types << tr("指法 %1 → %2").arg(from.isEmpty()?tr("未标"):from.join(" / "))
                                                .arg(to.isEmpty()?tr("清除"):to.join(" / "));
                }
                if (x.value("hands") != y.value("hands")) {
                    auto names=[&](const QVariant& value){QStringList out;for(auto hand:value.toStringList())out << (hand.startsWith("left")?tr("左手"):tr("右手"))+(hand.endsWith(":end")?tr("结束"):QString());return out.isEmpty()?tr("未标"):out.join(" / ");};
                    types << tr("分手 %1 → %2").arg(names(x.value("hands")),names(y.value("hands")));
                }
                marks |= x.value("otherMarks") != y.value("otherMarks");
            }
            if (pitch)
                types << tr("音高");
            if (velocity)
                types << tr("力度");
            if (marks)
                types << tr("标记");
            auto item = new QTreeWidgetItem(_changes, {tr("第%1小节").arg(row.value("measure").toInt()),
                                                       tr("谱表%1 · 声部%2")
                                                           .arg(row.value("track").toInt() / 4 + 1)
                                                           .arg(row.value("track").toInt() % 4 + 1),
                                                       types.join("、")});
            item->setData(0, Qt::UserRole, row);
            for(int column=0;column<3;++column)item->setToolTip(column,item->text(column));
        }
        updateButtons();
        _status->setText(option.isEmpty() ? tr("分析完成后，选择写法查看变化、试听或采用。") : option.value("used").toBool()
                             ? tr("已采用这版 · Ctrl+Z撤销 · Ctrl+S保存")
                             : tr("已选写法的变化显示在谱上；点列表中的位置查看相关音。"));
        if(!option.isEmpty()&&!option.value("used").toBool()&&!matchesInputs(option))_status->setText(tr("设置已改变，点“分析选段”更新建议。"));
        if(!option.isEmpty()&&!option.value("used").toBool()&&!sourceCurrent(option))
            _status->setText(tr("谱子有修改。请让Codex更新这段写法，或点“分析选段”重新计算。"));
    }
    void operate(const QString &action) {
        if (!attach() || _selected.isEmpty())
            return;
        if(action!="stop"&&!matchesInputs(selected())) {
            _status->setText(tr("设置已改变，点“分析选段”更新建议。"));return;
        }
        const auto result = _observer.arrangementRequest(
            "arrangementOption", {{"optionId", _selected},
                                  {"action", action},
                                  {"operationId", QUuid::createUuid().toString(QUuid::WithoutBraces)}});
        if (result.contains("error"))
            _status->setText(result.value("error").toString());
        else if (action == "preview")
            _status->setText(tr("已在谱上显示%1处变化；点列表中的位置查看相关音。").arg(result.value("differences").toList().size()));
        else if (action == "audition" || action == "original") {
            auto from=score()->tick2measure(Fraction::fromTicks(score()->masterScore()->repeatList().utick2tick(result.value("fromUtick").toInt())));
            const auto label=action=="audition"?tr("新写法"):tr("谱中写法");
            _status->setText(from ? tr("正在听：%1 · 从第%2小节开始%3").arg(label).arg(from->no()+1)
                .arg(from->tick().ticks()<selected().value("scope").toMap().value("start").toInt()?tr("（含进入这段的前文）"):QString())
                : tr("正在听：%1").arg(label));
        }
        else if (action == "variant")
            _status->setText(tr("已保存另一种写法，可在“分段试写”切换。Ctrl+S保存乐谱。"));
        else if (action == "apply")
            refresh();
        else if (action == "stop")
            _status->setText(tr("已停止比较，可以继续编辑乐谱。"));
    }

    void hideEvent(QHideEvent* event) override {_local.cancel();QWidget::hideEvent(event);updateDifference();}

  public:
    explicit SuggestionPanel(QWidget *parent) : QWidget(parent), _observer(this), _local(this) {
        setObjectName("arrangementSuggestionPanel");
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        auto analysis = new QHBoxLayout;
        _kind = new QComboBox;
        _kind->setObjectName("localSuggestionKind");
        _kind->addItem(tr("指法"), "fingering");
        _kind->addItem(tr("换手"), "handoff");
        _kind->addItem(tr("配音"), "voicing");
        _kind->addItem(tr("对位"), "counterpoint");
        analysis->addWidget(_kind, 1);
        auto generate = new QPushButton(tr("分析选段"));
        setComponentToolIcon(generate,"refresh");
        generate->setObjectName("localSuggestionStart");
        analysis->addWidget(generate);
        auto cancel = _cancelLocal = new QPushButton(tr("取消"));
        cancel->setObjectName("localSuggestionCancel");
        setComponentToolIcon(cancel,"stop");
        analysis->addWidget(cancel);
        auto settings = new QPushButton(tr("参数…"));
        setComponentToolIcon(settings,"settings");
        analysis->addWidget(settings);
        layout->addLayout(analysis);
        auto choices = new QHBoxLayout;
        _hand = new QComboBox;
        _hand->setObjectName("localSuggestionHand");
        _hand->addItem(tr("按谱面分手"), "auto");
        _hand->addItem(tr("选段用右手"), "right");
        _hand->addItem(tr("选段用左手"), "left");
        choices->addWidget(_hand);
        _lockFingers = new QCheckBox(tr("保留指法"));
        _lockFingers->setChecked(true);
        choices->addWidget(_lockFingers);
        _preserveMelody = new QCheckBox(tr("保留旋律"));
        _preserveMelody->setObjectName("localSuggestionPreserveMelody");
        _preserveMelody->setChecked(true);
        choices->addWidget(_preserveMelody);
        layout->addLayout(choices);
        _localSettings = QSettings().value("arrangementSuggestions/parameters").toMap();
        connect(generate, &QPushButton::clicked, this, [this] {
            stopPlaybackPreview(true);
            if(mscore&&mscore->currentScoreView())showRegionalDifference(mscore->currentScoreView(),nullptr,{});
            _local.start(score(), _kind->currentData().toString(), inputSettings());
        });
        connect(cancel, &QPushButton::clicked, this, [this] {
            _local.cancel();
            updateButtons();
            _status->setText(tr("已取消，可以重新选段。"));
        });
        connect(&_local, &LocalSuggestionTask::progress, this,
                [this](const QString &text) { _status->setText(text);updateButtons(); });
        connect(&_local, &LocalSuggestionTask::completed, this, [this] { refresh(); });
        connect(settings, &QPushButton::clicked, this, [this] {
            QDialog dialog(this);
            dialog.setWindowTitle(tr("编配建议参数"));
            auto form = new QFormLayout(&dialog);
            QMap<QString, QSpinBox *> spans;
            QMap<QString, QDoubleSpinBox *> costs;
            auto defaults = _observer.loadConfiguration("ArrangementAssistant");
            for (auto side : QStringList{"left", "right"})
                for (auto name : QStringList{"comfort", "maximum", "white"}) {
                    auto box = new QSpinBox;
                    box->setRange(1, 36);
                    auto values = _localSettings.value(side, defaults[side]).toMap();
                    box->setValue(values.value(name, name == "comfort"   ? 14
                                                     : name == "maximum" ? 15
                                                                         : 16)
                                      .toInt());
                    spans[side + "/" + name] = box;
                    form->addRow((side == "left" ? tr("左手") : tr("右手")) + " · " +
                                     (name == "comfort"   ? tr("舒适跨度")
                                      : name == "maximum" ? tr("最大跨度")
                                                          : tr("两端白键上限")) +
                                     tr("（半音）"),
                                 box);
                }
            auto style = new QComboBox;
            style->addItems({tr("调性钢琴／流行"), tr("严格对位"), tr("爵士")});
            style->setCurrentIndex(_localSettings.value("style", defaults.value("style", 0)).toInt());
            form->addRow(tr("风格"), style);
            const QStringList names{"stretch", "move", "blackThumb", "crossing", "repeat"},
                labels{tr("和弦伸展"), tr("移动手位"), tr("黑键拇指"), tr("穿指／跨指"), tr("同音换指")};
            const QList<double> initial{1, 1, .6, 1.2, .35};
            for (int i = 0; i < names.size(); ++i) {
                auto box = new QDoubleSpinBox;
                box->setRange(0, 10);
                box->setSingleStep(.1);
                box->setValue(_localSettings["weights"].toMap().value(names[i], initial[i]).toDouble());
                costs[names[i]] = box;
                form->addRow(labels[i] + tr("的重视程度"), box);
            }
            auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            form->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            for (auto side : QStringList{"left", "right"}) {
                QVariantMap values;
                for (auto name : QStringList{"comfort", "maximum", "white"})
                    values[name] = spans[side + "/" + name]->value();
                _localSettings[side] = values;
            }
            QVariantMap values;
            for (auto name : names)
                values[name] = costs[name]->value();
            _localSettings["weights"] = values;
            _localSettings["style"] = style->currentIndex();
            QSettings().setValue("arrangementSuggestions/parameters", _localSettings);
            inputsChanged();
        });
        _scope = new QLabel;
        _scope->setWordWrap(true);
        layout->addWidget(_scope);
        _choices = new QTreeWidget;
        _choices->setObjectName("arrangementChoices");
        _choices->setMinimumHeight(82);
        _choices->setMaximumHeight(126);
        _choices->setHeaderLabels({tr("写法"), tr("状态")});
        _choices->setRootIsDecorated(false);
        _choices->header()->setStretchLastSection(false);
        _choices->header()->setSectionResizeMode(0,QHeaderView::Stretch);
        _choices->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        layout->addWidget(_choices, 2);
        _explanation = new QTextBrowser;
        _explanation->setMinimumHeight(64);
        _explanation->setMaximumHeight(110);
        layout->addWidget(_explanation);
        _changes = new QTreeWidget;
        _changes->setObjectName("arrangementChanges");
        _changes->setHeaderLabels({tr("位置"), tr("声部"), tr("变化")});
        _changes->setRootIsDecorated(false);
        _changes->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
        _changes->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        _changes->header()->setSectionResizeMode(2,QHeaderView::Stretch);
        layout->addWidget(_changes, 2);
        auto controls = new QHBoxLayout;
        layout->addLayout(controls);
        auto button = [&](const QString &label, const QString &action, QHBoxLayout *row) {
            auto b = new QPushButton(label);
            b->setObjectName("arrangement-" + action);
            setComponentToolIcon(b, action=="stop"?"stop":action=="apply"?"apply":action=="variant"?"copy":"audition");
            row->addWidget(b);
            connect(b, &QPushButton::clicked, this, [this, action] { operate(action); });
        };
        _showDifferences = new QPushButton(tr("谱面差异"));
        _showDifferences->setObjectName("arrangement-preview");
        _showDifferences->setCheckable(true);
        _showDifferences->setChecked(QSettings().value("arrangementSuggestions/showDifferences", true).toBool());
        _showDifferences->setToolTip(tr("选中写法后，在原谱上显示另一写法；关闭后收起差异。"));
        controls->addWidget(_showDifferences);
        connect(_showDifferences, &QPushButton::clicked, this, [this](bool checked) {
            QSettings().setValue("arrangementSuggestions/showDifferences", checked);
            updateDifference();
        });
        button(tr("试听谱中"), "original", controls);
        button(tr("试听新写法"), "audition", controls);
        auto apply = new QHBoxLayout;
        layout->addLayout(apply);
        button(tr("使用这版"), "apply", apply);
        button(tr("存为另一种写法"), "variant", apply);
        button(tr("停止比较"), "stop", apply);
        _status = new QLabel;
        _status->setWordWrap(true);
        _status->setMinimumHeight(46);
        layout->addWidget(_status);
        connect(_kind,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{inputsChanged();});
        connect(_hand,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{inputsChanged();});
        connect(_lockFingers,&QCheckBox::toggled,this,[this]{inputsChanged();});
        connect(_preserveMelody,&QCheckBox::toggled,this,[this]{inputsChanged();});
        connect(_choices, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
            _selected = item ? item->data(0, Qt::UserRole).toString() : QString();
            details();
            updateDifference();
        });
        connect(_changes, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
            if (!attach())
                return;
            auto row = item->data(0, Qt::UserRole).toMap();
            _observer.navigateWorkflow({{"start", row["tick"]},
                                        {"end", row["tick"].toInt() + 1},
                                        {"firstTrack", row["track"]},
                                        {"endTrack", row["track"].toInt() + 1}},
                                       false);
        });
        connect(workflowContext(), &WorkflowContext::activeScoreChanged, this, [this] {
            _local.cancel();
            refresh();
        });
        if(mscore)connect(mscore->getPluginEngine(),&QmlPluginEngine::endCmd,this,[this]{
            if(isVisible()&&!_local.running()&&!_items.isEmpty()&&attach()
                &&_seenRevision!=_observer.analysisContentRevision())refresh();
        });
        refresh();
    }
    void refresh() {
        _items = arrangementSuggestions(score()).value("options").toList();
        _seenRevision=attach()?_observer.analysisContentRevision():QString();
        QSignalBlocker block(_choices);
        _choices->clear();
        for (auto v : _items) {
            auto row = v.toMap();
            auto item =
                new QTreeWidgetItem(_choices, {row.value("title").toString(),
                                               row.value("used").toBool() ? tr("已采用") : sourceCurrent(row) ? tr("可比较") : tr("谱子已修改")});
            item->setData(0, Qt::UserRole, row["id"]);
            item->setToolTip(0,row.value("title").toString());
            if (row["id"] == _selected)
                _choices->setCurrentItem(item);
        }
        if (!_choices->currentItem() && _choices->topLevelItemCount())
            _choices->setCurrentItem(_choices->topLevelItem(0));
        _selected =
            _choices->currentItem() ? _choices->currentItem()->data(0, Qt::UserRole).toString() : QString();
        details();
        updateDifference();
    }
};
QPointer<QDockWidget> dock;
QPointer<SuggestionPanel> panel;
} // namespace
void notifyArrangementSuggestions(Score *) {
    if (panel)
        panel->refresh();
}
void showSuggestionPanel(bool visible) {
    if (!personalFeaturesEnabled()) return;
    if (!mscore)
        return;
    if (!dock && visible) {
        dock = new QDockWidget(QObject::tr("编配建议"), mscore);
        panel = new SuggestionPanel(dock);
        dock->setWidget(panel);
        dock->setMinimumWidth(280);
        arrangeWorkflowDock(mscore, dock, "ArrangementSuggestions", "rightTools", Qt::RightDockWidgetArea);
        auto action = getAction("arrangement-suggestions");
    markPersonalAction(action);
        QObject::connect(dock, &QDockWidget::visibilityChanged, action, [action](bool on) {
            QSignalBlocker block(action);
            action->setChecked(on);
        });
    }
    if (dock) {
        dock->setVisible(visible);
        if (visible) {
            panel->refresh();
            dock->raise();
        }
    }
}
void addSuggestionPanelAction(QMenu *menu) {
    auto action = getAction("arrangement-suggestions");
    markPersonalAction(action);
    action->setCheckable(true);
    menu->addAction(action);
}
} // namespace Ms
