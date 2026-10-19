// SPDX-License-Identifier: GPL-2.0-or-later
#include "systemlocks.h"
#include "featuregate.h"
#include "libmscore/element.h"
#include "libmscore/layoutbreak.h"
#include "libmscore/measure.h"
#include "libmscore/score.h"
#include "libmscore/select.h"
#include "libmscore/system.h"
#include "libmscore/undo.h"
#include "mscore/musescore.h"
#include "mscore/seq.h"
#include "mscore/workspace.h"
#include "workspaceviews.h"
#include <QAction>
#include <QJsonDocument>
#include <QMenu>
#include <QSet>
#include <QUuid>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>

namespace Ms {
namespace {
QVariantMap data(LayoutBreak* item) {
    return QJsonDocument::fromJson(item->systemLockData().toUtf8()).toVariant().toMap();
}
QString encode(const QVariantMap& value) {
    return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}
class ChangeLockData final : public UndoCommand {
    LayoutBreak* item;
    QString value;
    void flip(EditData*) override {
        const auto old = item->systemLockData();
        item->setSystemLockData(value);
        value = old;
    }
public:
    ChangeLockData(LayoutBreak* target, const QVariantMap& next) : item(target), value(next.isEmpty() ? QString() : encode(next)) {}
};
QList<LayoutBreak*> breaks(MeasureBase* measure) {
    QList<LayoutBreak*> result;
    if (measure->isMeasure() && toMeasure(measure)->isMMRest()) {
        const auto rest = toMeasure(measure);
        for (auto bar = rest->mmRestFirst(); bar; bar = bar->nextMeasure()) {
            result.append(breaks(bar));
            if (bar == rest->mmRestLast()) break;
        }
        return result;
    }
    for (auto item : measure->el())
        if (item->isLayoutBreak())
            result.append(toLayoutBreak(item));
    return result;
}
void require(Score* score, MeasureBase* measure, LayoutBreak::Type type,
             const QString& id, const QString& role) {
    if (!measure)
        return;
    if (measure->isMeasure() && toMeasure(measure)->isMMRest())
        measure = toMeasure(measure)->mmRestLast();
    LayoutBreak* found = nullptr;
    for (auto item : breaks(measure))
        if (item->layoutBreakType() == type ||
            (type == LayoutBreak::LINE && (item->isPageBreak() || item->isSectionBreak()))) {
            found = item;
            break;
        }
    if (!found) {
        found = new LayoutBreak(score);
        found->setLayoutBreakType(type);
        found->setParent(measure);
        found->setTrack(0);
        found->setSystemLockData(encode({{"created", true}, {"groups", QVariantMap{{id, role}}}}));
        score->undoAddElement(found);
    } else {
        auto value = data(found), groups = value["groups"].toMap();
        groups[id] = role;
        value["groups"] = groups;
        score->undo(new ChangeLockData(found, value));
    }
}
QSet<System*> targetSystems(Score* score, bool selected, bool whole) {
    QSet<System*> result;
    auto add = [&](MeasureBase* bar) { if (bar && bar->system()) result.insert(bar->system()); };
    if (whole) {
        for (auto bar = score->firstMeasure(); bar; bar = bar->nextMeasure()) add(bar);
    } else if (selected && score->selection().isRange()) {
        Measure *first = nullptr, *last = nullptr;
        if (score->selection().measureRange(&first, &last))
            for (auto bar = first; bar; bar = bar->nextMeasureMM()) {
                add(bar);
                if (bar == last) break;
            }
    } else {
        for (auto item : score->selection().elements()) {
            add(item->findMeasure());
            if (!selected && !result.isEmpty()) break;
        }
    }
    return result;
}
QSet<QString> idsIn(const QSet<System*>& systems) {
    QSet<QString> result;
    for (auto system : systems)
        for (auto bar : system->measures())
            for (auto item : breaks(bar)) {
                const auto groups = data(item)["groups"].toMap();
                for (auto i = groups.cbegin(); i != groups.cend(); ++i)
                    if (i.value() != "before") result.insert(i.key());
            }
    return result;
}
}
QVariantMap changeSystemLocks(Score* score, bool lock, bool selected, bool whole) {
    if (!personalFeaturesEnabled()) return {{"error", QObject::tr("个人扩展功能已关闭。")}};
    if (!score || score->readOnly()) return {{"error", QObject::tr("打开可编辑乐谱，选中要调整的谱行。")}};
    if (seq && seq->isPlaying()) return {{"error", QObject::tr("暂停播放后即可调整谱行。")}};
    const auto systems = targetSystems(score, selected, whole);
    if (systems.isEmpty()) return {{"error", QObject::tr("点一个音符或框选小节，再选择要锁定或解锁的谱行。")}};
    int count = 0;
    score->startCmd();
    if (lock) {
        for (auto system : systems) {
            if (!idsIn(QSet<System*>{system}).isEmpty()) continue;
            QList<MeasureBase*> bars;
            for (auto bar : system->measures()) bars.append(bar);
            if (bars.isEmpty()) continue;
            const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            // A page or section boundary already gives the required line ending.
            auto first = bars.front();
            if (first->isMeasure() && toMeasure(first)->isMMRest()) first = toMeasure(first)->mmRestFirst();
            require(score, first->prev(), LayoutBreak::LINE, id, "before");
            for (int i = 0; i + 1 < bars.size(); ++i)
                require(score, bars[i], LayoutBreak::NOBREAK, id, "inside");
            require(score, bars.back(), LayoutBreak::LINE, id, "end");
            ++count;
        }
    } else {
        const auto ids = idsIn(systems);
        count = ids.size();
        for (auto bar = score->first(); bar; bar = bar->next())
            for (auto item : breaks(bar)) {
                auto value = data(item), groups = value["groups"].toMap();
                bool changed = false;
                for (const auto& id : ids) changed |= groups.remove(id) > 0;
                if (!changed) continue;
                if (groups.isEmpty() && value["created"].toBool()) score->undoRemoveElement(item);
                else {
                    if (groups.isEmpty()) value.clear();
                    else value["groups"] = groups;
                    score->undo(new ChangeLockData(item, value));
                }
            }
    }
    score->setLayoutAll();
    score->endCmd();
    if (mscore && mscore->currentScore() == score) mscore->endCmd();
    return {{"ok", true}, {"count", count},
            {"message", lock ? QObject::tr("已锁定 %1 行；行内间距仍随内容调整。").arg(count)
                              : QObject::tr("已解除 %1 行锁定；原来的换行和分页继续保留。").arg(count)}};
}
void addSystemLockActions(QMenu* menu) {
    auto owner = qobject_cast<MuseScore*>(menu->parentWidget());
    if (!owner) owner = mscore;
    if (owner)
        if (auto existing = owner->findChild<QMenu*>("kumo-system-lock-menu")) {
            existing->setTitle(QObject::tr("谱行锁定"));
            for (auto action : menu->actions())
                if (auto restored = action->menu())
                    if (restored != existing &&
                        (restored == Workspace::findMenuFromString("menu-kumo-system-lock") ||
                         restored->title() == "menu-kumo-system-lock")) {
                        for (auto saved : restored->actions()) {
                            if (existing->actions().contains(saved)) continue;
                            if (auto child = saved->menu()) {
                                if (child->parent() == restored) child->setParent(existing);
                            } else if (saved->parent() == restored) saved->setParent(existing);
                            existing->addAction(saved);
                        }
                        menu->removeAction(action);
                    }
            Workspace::addMenuAndString(existing, "menu-kumo-system-lock");
            if (!menu->actions().contains(existing->menuAction())) menu->addMenu(existing);
            return;
        }
    auto group = new QMenu(QObject::tr("谱行锁定"), owner ? static_cast<QWidget*>(owner) : menu);
    menu->addMenu(group);
    group->setObjectName("kumo-system-lock-menu");
    group->menuAction()->setObjectName("kumo-system-lock-menu");
    group->setIcon(personalToolIcon("kumo-system-lock"));
    markPersonalAction(group->menuAction());
    Workspace::addMenuAndString(group, "menu-kumo-system-lock");
    struct Choice {const char* key; const char* text; bool lock, selected, whole;};
    for (auto choice : {Choice{"current", "锁定当前行", true, false, false},
                        Choice{"selected", "锁定选中行", true, true, false},
                        Choice{"unlock", "解除选中行锁定", false, true, false},
                        Choice{"all", "解除全谱行锁定", false, true, true}}) {
        auto action = group->addAction(QObject::tr(choice.text));
        action->setObjectName(QString("kumo-system-lock-") + choice.key);
        action->setToolTip(QObject::tr("固定当前行的小节范围；Ctrl+Z 撤销，保存后继续保留。"));
        markPersonalAction(action);
        Workspace::addActionAndString(action, action->objectName());
        QObject::connect(action, &QAction::triggered, group, [choice] {
            if (!mscore) return;
            const auto result = changeSystemLocks(mscore->currentScore(), choice.lock, choice.selected, choice.whole);
            mscore->showMessage(result.contains("error") ? result["error"].toString() : result["message"].toString(), 6000);
        });
    }
}
void addSystemLockSelector(QToolBar* toolbar) {
    if (!toolbar || !mscore || !personalFeaturesEnabled()) return;
    auto menu = mscore->findChild<QMenu*>("kumo-system-lock-menu");
    if (!menu) return;
    auto button = new QToolButton(toolbar);
    button->setObjectName("kumoSystemLockSelector");
    button->setDefaultAction(menu->menuAction());
    button->setMenu(menu);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolTip(QObject::tr("谱行锁定：保留这一行的小节，或解除锁定。"));
    toolbar->addWidget(button);
}
}
