// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "arrangement.h"
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

  public:
    explicit SuggestionPanel(QWidget *parent) : QWidget(parent), _observer(this) {
        setObjectName("arrangementSuggestionPanel");
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
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
        connect(workflowContext(), &WorkflowContext::activeScoreChanged, this, [this] { refresh(); });
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
