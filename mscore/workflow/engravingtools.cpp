// SPDX-License-Identifier: GPL-2.0-or-later
#include "engravingtools.h"
#include "engravingedit.h"
#include "systemlocks.h"
#include "workspaceviews.h"
#include "libmscore/arpeggio.h"
#include "libmscore/element.h"
#include "libmscore/measure.h"
#include "libmscore/page.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/select.h"
#include "libmscore/spacer.h"
#include "libmscore/spanner.h"
#include "libmscore/system.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <memory>

namespace Ms {
namespace {
QString tr(const char* message)
{
    return QCoreApplication::translate("EngravingTools", message);
}

QVariantMap geometry(const QRectF& rect, qreal scale)
{
    return {{"x", rect.x() / scale}, {"y", rect.y() / scale},
            {"width", rect.width() / scale}, {"height", rect.height() / scale}};
}

struct GeometryItems {
    QVariantList items;
    int firstTrack;
    int endTrack;
    qreal scale;
    int systemStart;
};

void collectGeometry(void* data, Element* item)
{
    auto& result = *static_cast<GeometryItems*>(data);
    if (!item->visible() || (!engravingMovableSymbol(item) && !item->isNote() && !item->isRest()
        && !item->isAccidental() && !item->isStem() && !item->isBeam()))
        return;
    if (item->track() >= 0 && (item->track() < result.firstTrack || item->track() >= result.endTrack))
        return;
    QVariantMap row{{"type", item->name()}, {"tick", item->tick().ticks()},
                    {"track", item->track()}, {"box", geometry(item->pageBoundingRect(), result.scale)},
                    {"autoplace", item->autoplace()}, {"movable", engravingMovableSymbol(item)},
                    {"systemStart", result.systemStart}};
    if (engravingMovableSymbol(item)) {
        row["offsetX"] = item->offset().x() / item->spatium();
        row["offsetY"] = item->offset().y() / item->spatium();
        row["minimumDistance"] = item->minDistance().val();
    }
    if (item->type() == ElementType::ARPEGGIO)
        row["staffSpan"] = static_cast<const Arpeggio*>(item)->span();
    if (item->isSpannerSegment()) {
        const auto spanner = toSpannerSegment(item)->spanner();
        row["connectionStart"] = spanner->tick().ticks();
        row["connectionEnd"] = spanner->tick2().ticks();
    }
    result.items.append(row);
}

QDoubleSpinBox* distance(QWidget* owner, double minimum = -10.0)
{
    auto field = new QDoubleSpinBox(owner);
    field->setRange(minimum, 20.0);
    field->setDecimals(2);
    field->setSingleStep(0.25);
    field->setSuffix(tr(" 谱间"));
    return field;
}
}

bool engravingMovableSymbol(const Element* item)
{
    if (!item || item->generated() || item->systemFlag())
        return false;
    if (item->isSpannerSegment()) {
        const auto span = toSpannerSegment(item)->spanner();
        return span && !span->systemFlag();
    }
    return item->isTextBase() || item->isArticulation() || item->type() == ElementType::ARPEGGIO;
}

EngravingTarget::operator bool() const
{
    switch (tool) {
    case EngravingTool::Symbol: return symbol;
    case EngravingTool::Beat: return segment;
    case EngravingTool::Measure: return measure;
    case EngravingTool::Staff:
    case EngravingTool::System: return measure && staff >= 0;
    }
    return false;
}

EngravingTarget engravingTarget(Score* score, EngravingTool tool)
{
    EngravingTarget result;
    result.tool = tool;
    if (!score || score->readOnly() || score->isPlaying())
        return result;
    const auto& selection = score->selection();
    auto item = selection.element();
    if (item && engravingMovableSymbol(item))
        result.symbol = item;
    if (item) {
        for (auto current = item; current; current = current->parent()) {
            if (current->isSegment() && toSegment(current)->isChordRestType())
                result.segment = toSegment(current);
            if (current->isSystem()) {
                result.system = toSystem(current);
                break;
            }
        }
        result.measure = item->findMeasure();
        result.staff = item->staffIdx();
    } else if (selection.isRange()) {
        selection.measureRange(&result.measure, &result.rangeEnd);
        result.staff = selection.staffStart();
    }
    if (!result.system && result.measure)
        result.system = result.measure->system();
    if (result.measure && result.measure->isMMRest()) {
        result.measure = nullptr;
        result.segment = nullptr;
    }
    if (tool == EngravingTool::System && result.system && result.system->firstMeasure()) {
        // A system spacer lives on the first measure and its last visible staff.
        result.measure = result.system->firstMeasure();
        result.staff = -1;
        for (int i = result.system->staves()->size() - 1; i >= 0; --i)
            if (result.system->staff(i)->show()) {
                result.staff = i;
                break;
            }
    }
    if (!result.system || result.staff < 0 || result.staff >= result.system->staves()->size()
        || !result.system->staff(result.staff)->show() || (result.measure && result.measure->isMMRest()))
        result.staff = -1;
    return result;
}

QVariant engravingValue(const EngravingTarget& target)
{
    switch (target.tool) {
    case EngravingTool::Symbol: return target.symbol->offset();
    case EngravingTool::Beat: return target.segment->extraLeadingSpace().val();
    case EngravingTool::Measure: return target.measure->userStretch();
    case EngravingTool::Staff:
    case EngravingTool::System:
        if (auto spacer = target.measure->vspacerDown(target.staff))
            return spacer->gap() / target.measure->score()->spatium();
        return {};
    }
    return {};
}

bool changeEngravingValue(const EngravingTarget& target, const QVariant& value, bool reset)
{
    if (!target)
        return false;
    const auto old = engravingValue(target);
    const bool range = target.tool == EngravingTool::Measure
        && target.rangeEnd && target.rangeEnd != target.measure;
    if (!reset && !range && old == value)
        return false;
    auto score = target.symbol && target.tool == EngravingTool::Symbol
        ? target.symbol->score() : target.measure ? target.measure->score() : target.segment->score();
    if (score->isPlaying() || score->readOnly())
        return false;
    score->startCmd();
    switch (target.tool) {
    case EngravingTool::Symbol:
        if (reset) {
            target.symbol->undoResetProperty(Pid::OFFSET);
            target.symbol->undoResetProperty(Pid::AUTOPLACE);
        } else {
            target.symbol->undoChangeProperty(Pid::OFFSET, value.toPointF());
        }
        break;
    case EngravingTool::Beat:
        target.segment->undoChangeProperty(Pid::LEADING_SPACE, Spatium(reset ? 0.0 : value.toDouble()));
        break;
    case EngravingTool::Measure:
        for (auto bar = target.measure; bar; bar = bar->nextMeasure()) {
            bar->undoChangeProperty(Pid::USER_STRETCH, reset ? 1.0 : value.toDouble());
            if (!target.rangeEnd || bar == target.rangeEnd)
                break;
        }
        break;
    case EngravingTool::Staff:
    case EngravingTool::System: {
        auto spacer = target.measure->vspacerDown(target.staff);
        const auto gap = value.toDouble() * score->spatium();
        if (reset) {
            if (spacer)
                score->undoRemoveElement(spacer);
        } else if (spacer) {
            spacer->undoChangeProperty(Pid::SPACE, gap);
        } else {
            spacer = new Spacer(score);
            spacer->setParent(target.measure);
            spacer->setTrack(target.staff * VOICES);
            spacer->setSpacerType(SpacerType::DOWN);
            spacer->setGap(gap);
            score->undoAddElement(spacer);
        }
        break;
    }
    }
    score->endCmd();
    return true;
}

QVariantMap engravingLayout(Score* source, const QVariantMap& scope)
{
    if (!source)
        return {{"error", tr("先打开要检查排版的乐谱。")}};
    std::unique_ptr<MasterScore> pageCopy;
    Score* score = source;
    if (!source->pageMode()) {
        pageCopy.reset(source->isMaster() ? source->masterScore()->clone()
                                         : static_cast<MasterScore*>(source->clone()));
        if (!pageCopy)
            return {{"error", tr("暂时无法准备页面排版。")}};
        pageCopy->switchToPageMode();
        score = pageCopy.get();
    }
    const auto start = scope.value("start").toInt();
    const auto end = scope.value("end").toInt();
    const int firstTrack = scope.value("firstTrack").toInt();
    const int endTrack = scope.value("endTrack", score->ntracks()).toInt();
    const auto scale = score->spatium();
    QVariantList systems;
    for (const auto page : score->pages())
        for (const auto system : page->systems()) {
            const auto first = system->firstMeasure();
            const auto last = system->lastMeasure();
            if (!first || !last || first->tick().ticks() >= end || last->endTick().ticks() <= start)
                continue;
            QVariantList staves, bars;
            for (int staff = 0; staff < score->nstaves(); ++staff)
                if (const auto row = system->staff(staff); row && row->show())
                    staves.append(QVariantMap{{"staff", staff}, {"y", system->staffYpage(staff) / scale}});
            for (const auto base : system->measures()) {
                if (!base->isMeasure())
                    continue;
                const auto measure = toMeasure(base);
                QVariantList timeColumns, staffSpacing;
                for (auto segment = measure->first(SegmentType::ChordRest); segment;
                     segment = segment->next(SegmentType::ChordRest))
                    if (segment->enabled())
                        timeColumns.append(QVariantMap{{"tick", segment->tick().ticks()},
                            {"x", segment->pagePos().x() / scale},
                            {"leadingSpace", segment->extraLeadingSpace().val()}});
                for (int staff = 0; staff < score->nstaves(); ++staff) {
                    const auto above = measure->vspacerUp(staff);
                    const auto below = measure->vspacerDown(staff);
                    if (above || below)
                        staffSpacing.append(QVariantMap{{"staff", staff},
                            {"above", above ? QVariant(above->gap() / scale) : QVariant()},
                            {"below", below ? QVariant(below->gap() / scale) : QVariant()}});
                }
                bars.append(QVariantMap{{"measure", measure->no() + 1},
                    {"start", measure->tick().ticks()}, {"end", measure->endTick().ticks()},
                    {"stretch", measure->userStretch()}, {"lineBreak", measure->lineBreak()},
                    {"pageBreak", measure->pageBreak()},
                    {"box", geometry(measure->pageBoundingRect(), scale)}, {"timeColumns", timeColumns},
                    {"staffSpacing", staffSpacing}});
            }
            GeometryItems items{{}, firstTrack, endTrack, scale, first->tick().ticks()};
            for (auto base : system->measures())
                base->scanElements(&items, collectGeometry, false);
            system->scanElements(&items, collectGeometry, false);
            systems.append(QVariantMap{{"page", page->no() + 1},
                {"start", first->tick().ticks()}, {"end", last->endTick().ticks()},
                {"box", geometry(system->pageBoundingRect(), scale)}, {"visibleStaves", staves},
                {"measures", bars}, {"objects", items.items}});
        }
    return {{"schemaVersion", "engraving-layout/1"}, {"coordinateUnit", "spatium"},
            {"origin", "pageTopLeft"}, {"systems", systems}};
}

void openEngravingTools(MuseScore* window)
{
    if (auto existing = window->findChild<QDockWidget*>("kumoEngravingTools")) {
        existing->show();
        existing->raise();
        return;
    }
    auto dock = new QDockWidget(tr("雕版工具"), window);
    dock->setObjectName("kumoEngravingTools");
    auto content = new QWidget(dock);
    auto layout = new QVBoxLayout(content);
    auto mode = new QComboBox(content);
    mode->setObjectName("kumoEngravingTool");
    mode->addItems({tr("符号位置"), tr("横向间距"), tr("谱表与分行")});
    layout->addWidget(mode);
    auto pages = new QStackedWidget(content);
    layout->addWidget(pages);
    auto page = [&] {
        auto widget = new QWidget(pages);
        pages->addWidget(widget);
        return new QFormLayout(widget);
    };
    auto symbols = page();
    auto x = distance(content), y = distance(content);
    symbols->addRow(tr("水平移动"), x);
    symbols->addRow(tr("垂直移动"), y);
    auto spacing = page();
    auto leading = distance(content);
    auto stretch = new QDoubleSpinBox(content);
    stretch->setRange(25, 400);
    stretch->setValue(100);
    stretch->setSuffix("%");
    auto spacingTarget = new QComboBox(content);
    spacingTarget->setObjectName("kumoEngravingSpacingTarget");
    spacingTarget->addItems({tr("共用拍位"), tr("所选小节")});
    spacing->addRow(tr("调整"), spacingTarget);
    spacing->addRow(tr("拍位前增减"), leading);
    spacing->addRow(tr("小节宽度"), stretch);
    const auto showSpacingTarget = [spacing, leading, stretch](int target) {
        spacing->labelForField(leading)->setVisible(target == 0);
        leading->setVisible(target == 0);
        spacing->labelForField(stretch)->setVisible(target == 1);
        stretch->setVisible(target == 1);
    };
    QObject::connect(spacingTarget, QOverload<int>::of(&QComboBox::currentIndexChanged), content, showSpacingTarget);
    showSpacingTarget(0);
    auto staves = page();
    auto staffGap = distance(content, 0);
    staffGap->setValue(8);
    auto staffTarget = new QComboBox(content);
    staffTarget->setObjectName("kumoEngravingStaffTarget");
    staffTarget->addItems({tr("所选谱表下方"), tr("所选谱行下方")});
    staves->addRow(tr("位置"), staffTarget);
    staves->addRow(tr("最小距离"), staffGap);
    auto lineButtons = new QHBoxLayout;
    auto lock = new QPushButton(tr("锁定谱行"), content);
    auto unlock = new QPushButton(tr("解除锁定"), content);
    lineButtons->addWidget(lock);
    lineButtons->addWidget(unlock);
    lineButtons->addStretch();
    staves->addRow(lineButtons);
    auto actions = new QHBoxLayout;
    auto apply = new QPushButton(tr("调整选中位置"), content);
    auto restore = new QPushButton(tr("恢复自动"), content);
    actions->addWidget(apply);
    actions->addWidget(restore);
    actions->addStretch();
    layout->addLayout(actions);
    auto handles = new QCheckBox(tr("在谱面显示拖动手柄"), content);
    handles->setObjectName("kumoEngravingHandles");
    handles->setChecked(true);
    layout->addWidget(handles);
    auto status = new QLabel(content);
    status->setWordWrap(true);
    layout->addWidget(status);
    layout->addStretch();
    auto prompt = [status](int index) {
        const QStringList instructions{
            tr("选中文字、发音记号或曲线，拖动谱面手柄或输入移动量。正数向右或向下。"),
            tr("选音或休止符后拖动共同拍位；也可以选择小节，调整宽度。"),
            tr("选中目标位置后上下拖动，为谱表或整行下方留出空间。")};
        status->setText(instructions.at(index));
    };
    prompt(0);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), pages, &QStackedWidget::setCurrentIndex);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), content, prompt);
    const auto tool = [=] {
        if (mode->currentIndex() == 0)
            return EngravingTool::Symbol;
        if (mode->currentIndex() == 1)
            return spacingTarget->currentIndex() == 0 ? EngravingTool::Beat : EngravingTool::Measure;
        return staffTarget->currentIndex() == 0 ? EngravingTool::Staff : EngravingTool::System;
    };
    const auto updateHandle = [=] {
        dock->setProperty("engravingTool", int(tool()));
        refreshEngravingSelection(window);
    };
    for (auto selector : {mode, spacingTarget, staffTarget})
        QObject::connect(selector, QOverload<int>::of(&QComboBox::currentIndexChanged), content, updateHandle);
    QObject::connect(handles, &QCheckBox::toggled, content, [=](bool enabled) {
        dock->setProperty("engravingShowHandles", enabled);
        refreshEngravingSelection(window);
    });
    QObject::connect(dock, &QDockWidget::visibilityChanged, content, [window] {
        refreshEngravingSelection(window);
    });
    auto modify = [=](bool reset) {
        auto score = window->currentScore();
        if (!score || score->readOnly() || score->isPlaying()) {
            status->setText(tr("先停止播放，再打开要调整的乐谱。"));
            return;
        }
        const auto target = engravingTarget(score, tool());
        if (!target) {
            prompt(mode->currentIndex());
            return;
        }
        QVariant next;
        switch (target.tool) {
        case EngravingTool::Symbol:
            next = engravingValue(target).toPointF() + QPointF(x->value(), y->value()) * target.symbol->spatium();
            break;
        case EngravingTool::Beat:
            next = engravingValue(target).toDouble() + leading->value();
            break;
        case EngravingTool::Measure:
            next = stretch->value() / 100;
            break;
        case EngravingTool::Staff:
        case EngravingTool::System:
            next = staffGap->value();
            break;
        }
        if (changeEngravingValue(target, next, reset))
            status->setText(tr("已调整排版。Ctrl+Z 可撤销。"));
    };
    QObject::connect(apply, &QPushButton::clicked, content, [modify] { modify(false); });
    QObject::connect(restore, &QPushButton::clicked, content, [modify] { modify(true); });
    auto lockLine = [window, status](bool on) {
        const auto result = changeSystemLocks(window->currentScore(), on, true);
        status->setText(result.value("error", result.value("message")).toString());
    };
    QObject::connect(lock, &QPushButton::clicked, content, [lockLine] { lockLine(true); });
    QObject::connect(unlock, &QPushButton::clicked, content, [lockLine] { lockLine(false); });
    dock->setWidget(content);
    window->addDockWidget(Qt::RightDockWidgetArea, dock);
    addEditorDockActions(dock);
    dock->setProperty("engravingShowHandles", true);
    updateHandle();
    dock->show();
}
}
