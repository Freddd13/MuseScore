// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "arrangement.h"
#include "localsuggestions.h"
#include "paneldock.h"
#include "workflowcontext.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/shortcut.h"
#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QTextBrowser>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
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
    void details() {
        auto option = selected();
        auto scope = option.value("scope").toMap();
        _scope->setText(option.isEmpty() ? tr("在Codex中选取谱段并提出编配要求，收到的写法会列在这里。")
                                         : tr("第%1–%2小节 · %3处变化")
                                               .arg(scope.value("firstMeasure").toInt())
                                               .arg(scope.value("lastMeasure").toInt())
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
                marks |= x.value("marks") != y.value("marks");
            }
            if (pitch)
                types << tr("音高");
            if (velocity)
                types << tr("力度");
            if (marks)
                types << tr("附着标记");
            auto item = new QTreeWidgetItem(_changes, {tr("第%1小节").arg(row.value("measure").toInt()),
                                                       tr("谱表%1 · 声部%2")
                                                           .arg(row.value("track").toInt() / 4 + 1)
                                                           .arg(row.value("track").toInt() % 4 + 1),
                                                       types.join("、")});
            item->setData(0, Qt::UserRole, row);
        }
        for (auto button : findChildren<QPushButton *>())
            if (button->objectName().startsWith("arrangement-"))
                button->setEnabled(!option.isEmpty() && !option.value("used").toBool());
        _status->setText(option.value("used").toBool()
                             ? tr("已采用这版 · Ctrl+Z撤销 · Ctrl+S保存")
                             : tr("点一项变化可以定位；“查看差异”会在谱上叠加另一写法。"));
    }
    void operate(const QString &action) {
        if (!attach() || _selected.isEmpty())
            return;
        const auto result = _observer.arrangementRequest(
            "arrangementOption", {{"optionId", _selected},
                                  {"action", action},
                                  {"operationId", QUuid::createUuid().toString(QUuid::WithoutBraces)}});
        if (result.contains("error"))
            _status->setText(result.value("error").toString());
        else if (action == "audition" || action == "original")
            _status->setText(action == "audition" ? tr("正在听：新写法") : tr("正在听：谱中写法"));
        else if (action == "variant")
            _status->setText(tr("已保存另一种写法，可在“分段试写”切换。Ctrl+S保存乐谱。"));
        else if (action == "apply")
            refresh();
        else if (action == "stop")
            _status->setText(tr("已停止比较，可以继续编辑乐谱。"));
    }

    void hideEvent(QHideEvent* event) override {_local.cancel();QWidget::hideEvent(event);}

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
        generate->setObjectName("localSuggestionStart");
        analysis->addWidget(generate);
        auto cancel = new QPushButton(tr("取消"));
        analysis->addWidget(cancel);
        auto settings = new QPushButton(tr("参数…"));
        analysis->addWidget(settings);
        layout->addLayout(analysis);
        auto choices = new QHBoxLayout;
        _hand = new QComboBox;
        _hand->addItem(tr("按谱面分手"), "auto");
        _hand->addItem(tr("选段用右手"), "right");
        _hand->addItem(tr("选段用左手"), "left");
        choices->addWidget(_hand);
        _lockFingers = new QCheckBox(tr("保留指法"));
        _lockFingers->setChecked(true);
        choices->addWidget(_lockFingers);
        _preserveMelody = new QCheckBox(tr("保留旋律"));
        _preserveMelody->setChecked(true);
        choices->addWidget(_preserveMelody);
        layout->addLayout(choices);
        _localSettings = QSettings().value("arrangementSuggestions/parameters").toMap();
        connect(generate, &QPushButton::clicked, this, [this] {
            auto settings = _localSettings;
            settings["hand"] = _hand->currentData();
            settings["lockFingers"] = _lockFingers->isChecked();
            settings["preserveTracks"] = _preserveMelody->isChecked() ? QVariantList{0} : QVariantList{};
            _local.start(score(), _kind->currentData().toString(), settings);
        });
        connect(cancel, &QPushButton::clicked, this, [this] {
            _local.cancel();
            _status->setText(tr("已取消，可以重新选段。"));
        });
        connect(&_local, &LocalSuggestionTask::progress, this,
                [this](const QString &text) { _status->setText(text); });
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
        layout->addWidget(_choices, 2);
        _explanation = new QTextBrowser;
        _explanation->setMinimumHeight(64);
        _explanation->setMaximumHeight(110);
        layout->addWidget(_explanation);
        _changes = new QTreeWidget;
        _changes->setHeaderLabels({tr("位置"), tr("声部"), tr("变化")});
        _changes->setRootIsDecorated(false);
        layout->addWidget(_changes, 2);
        auto controls = new QHBoxLayout;
        layout->addLayout(controls);
        auto button = [&](const QString &label, const QString &action, QHBoxLayout *row) {
            auto b = new QPushButton(label);
            b->setObjectName("arrangement-" + action);
            row->addWidget(b);
            connect(b, &QPushButton::clicked, this, [this, action] { operate(action); });
        };
        button(tr("查看差异"), "preview", controls);
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
        connect(_choices, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
            _selected = item ? item->data(0, Qt::UserRole).toString() : QString();
            details();
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
        refresh();
    }
    void refresh() {
        _items = arrangementSuggestions(score()).value("options").toList();
        QSignalBlocker block(_choices);
        _choices->clear();
        for (auto v : _items) {
            auto row = v.toMap();
            auto item =
                new QTreeWidgetItem(_choices, {row.value("title").toString(),
                                               row.value("used").toBool() ? tr("已采用") : tr("可比较")});
            item->setData(0, Qt::UserRole, row["id"]);
            if (row["id"] == _selected)
                _choices->setCurrentItem(item);
        }
        if (!_choices->currentItem() && _choices->topLevelItemCount())
            _choices->setCurrentItem(_choices->topLevelItem(0));
        _selected =
            _choices->currentItem() ? _choices->currentItem()->data(0, Qt::UserRole).toString() : QString();
        details();
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
    if (!mscore)
        return;
    if (!dock && visible) {
        dock = new QDockWidget(QObject::tr("编配建议"), mscore);
        panel = new SuggestionPanel(dock);
        dock->setWidget(panel);
        dock->setMinimumWidth(280);
        arrangeWorkflowDock(mscore, dock, "ArrangementSuggestions", "rightTools", Qt::RightDockWidgetArea);
        auto action = getAction("arrangement-suggestions");
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
    action->setCheckable(true);
    menu->addAction(action);
}
} // namespace Ms
