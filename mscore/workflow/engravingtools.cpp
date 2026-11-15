// SPDX-License-Identifier: GPL-2.0-or-later
#include "engravingtools.h"
#include "systemlocks.h"
#include "workspaceviews.h"
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

bool movableSymbol(const Element* item)
{
    return item && !item->generated() && !item->systemFlag()
        && (item->isTextBase() || item->isArticulation() || item->isSpannerSegment());
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
    if (!item->visible() || (!movableSymbol(item) && !item->isNote() && !item->isRest()
        && !item->isAccidental() && !item->isStem() && !item->isBeam()))
        return;
    if (item->track() >= 0 && (item->track() < result.firstTrack || item->track() >= result.endTrack))
        return;
    QVariantMap row{{"type", item->name()}, {"tick", item->tick().ticks()},
                    {"track", item->track()}, {"box", geometry(item->pageBoundingRect(), result.scale)},
                    {"autoplace", item->autoplace()}, {"movable", movableSymbol(item)},
                    {"systemStart", result.systemStart}};
    if (movableSymbol(item)) {
        row["offsetX"] = item->offset().x() / item->spatium();
        row["offsetY"] = item->offset().y() / item->spatium();
        row["minimumDistance"] = item->minDistance().val();
    }
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
    auto status = new QLabel(content);
    status->setWordWrap(true);
    layout->addWidget(status);
    layout->addStretch();
    auto prompt = [status](int index) {
        const QStringList instructions{
            tr("选中文字、发音记号或曲线，再输入要移动的距离。正数向右或向下。"),
            tr("选音或休止符调整两手共用的拍位；选小节可调整宽度。"),
            tr("选中目标位置，为谱表或整行下方留出空间。")};
        status->setText(instructions.at(index));
    };
    prompt(0);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), pages, &QStackedWidget::setCurrentIndex);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), content, prompt);
    auto modify = [=](bool reset) {
        auto score = window->currentScore();
        if (!score || score->readOnly() || score->isPlaying()) {
            status->setText(tr("先停止播放，再打开要调整的乐谱。"));
            return;
        }
        auto item = score->selection().element();
        Measure* measure = item ? item->findMeasure() : nullptr;
        Measure* last = nullptr;
        if (!measure && score->selection().isRange())
            score->selection().measureRange(&measure, &last);
        auto segment = item;
        while (segment && !segment->isSegment())
            segment = segment->parent();
        bool changed = false;
        score->startCmd();
        if (mode->currentIndex() == 0 && movableSymbol(item)) {
            if (reset) {
                item->undoResetProperty(Pid::OFFSET);
                item->undoResetProperty(Pid::AUTOPLACE);
            } else {
                const auto delta = QPointF(x->value(), y->value()) * item->spatium();
                item->undoChangeProperty(Pid::OFFSET, item->offset() + delta);
            }
            changed = true;
        } else if (mode->currentIndex() == 1 && spacingTarget->currentIndex() == 0 && segment) {
            const auto column = toSegment(segment);
            const auto space = reset ? 0.0 : column->extraLeadingSpace().val() + leading->value();
            column->undoChangeProperty(Pid::LEADING_SPACE, Spatium(space));
            changed = true;
        } else if (mode->currentIndex() == 1 && spacingTarget->currentIndex() == 1 && measure) {
            for (auto bar = measure; bar; bar = bar->nextMeasure()) {
                bar->undoChangeProperty(Pid::USER_STRETCH, reset ? 1.0 : stretch->value() / 100);
                if (!last || bar == last)
                    break;
            }
            changed = true;
        } else if (mode->currentIndex() == 2 && measure && item && item->staffIdx() >= 0) {
            int staff = item->staffIdx();
            if (staffTarget->currentIndex() == 1 && measure->system()) {
                for (int i = score->nstaves() - 1; i >= 0; --i)
                    if (measure->system()->staff(i)->show()) {
                        staff = i;
                        break;
                    }
            }
            auto spacer = measure->vspacerDown(staff);
            if (reset && spacer)
                score->undoRemoveElement(spacer);
            else if (!reset && spacer)
                spacer->undoChangeProperty(Pid::SPACE, staffGap->value() * score->spatium());
            else if (!reset) {
                spacer = new Spacer(score);
                spacer->setParent(measure);
                spacer->setTrack(staff * VOICES);
                spacer->setSpacerType(SpacerType::DOWN);
                spacer->setGap(staffGap->value() * score->spatium());
                score->undoAddElement(spacer);
            }
            changed = true;
        }
        score->endCmd();
        if (changed)
            status->setText(tr("已调整排版。Ctrl+Z 可撤销。"));
        else
            prompt(mode->currentIndex());
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
    dock->show();
}
}
