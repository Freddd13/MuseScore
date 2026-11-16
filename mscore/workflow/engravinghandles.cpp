// SPDX-License-Identifier: GPL-2.0-or-later
#include "engravingtools.h"
#include "engravingedit.h"
#include "featuregate.h"
#include "libmscore/element.h"
#include "libmscore/measure.h"
#include "libmscore/page.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/system.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"

#include <QApplication>
#include <QDockWidget>
#include <QKeyEvent>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>
#include <optional>

namespace Ms {
namespace {
const auto dockName = "kumoEngravingTools";

QString label(const char* text)
{
    return QCoreApplication::translate("EngravingHandles", text);
}

QString spaces(qreal value)
{
    return label("%1 谱间").arg(QLocale().toString(value, 'f', 1));
}

enum class DragAxis { Both, Horizontal, Vertical };

struct Handle {
    EngravingTarget target;
    QPointF point;
    QRectF guide;
    QString detail;
    DragAxis axis = DragAxis::Both;
    qreal referenceLength = 1;
};

int visibleStaff(const System* system, int start)
{
    for (int i = start; i < system->staves()->size(); ++i)
        if (system->staff(i)->show())
            return i;
    return -1;
}

qreal lineEndRoom(const Score* score, const System* system)
{
    // Use the actual end of the system, so a justified line can legitimately show zero.
    return (score->styleD(Sid::pagePrintableWidth) * DPI - system->width()) / score->spatium();
}

std::optional<qreal> symbolClearance(const Element* symbol, Page* page, qreal sp)
{
    const QRectF box = symbol->pageBoundingRect();
    const qreal radius = sp * 4;
    qreal nearest = radius;
    bool found = false;
    for (const auto item : page->items(box.adjusted(-radius, -radius, radius, radius))) {
        if (item == symbol || !item->visible()
            || !(engravingMovableSymbol(item) || item->isNote() || item->isRest()
                || item->isAccidental() || item->isStem() || item->isBeam()))
            continue;
        const auto other = item->pageBoundingRect();
        if (other.isEmpty())
            continue;
        const auto dx = qMax(0.0, qMax(box.left() - other.right(), other.left() - box.right()));
        const auto dy = qMax(0.0, qMax(box.top() - other.bottom(), other.top() - box.bottom()));
        const qreal gap = std::hypot(dx, dy);
        if (gap <= nearest) {
            nearest = gap;
            found = true;
        }
    }
    return found ? std::optional<qreal>(nearest) : std::nullopt;
}

QString roomText(const Score* score, const System* system)
{
    const qreal room = lineEndRoom(score, system);
    return (room >= -0.05 ? label("行尾余量 %1") : label("超出印刷宽度 %1"))
        .arg(spaces(room >= -0.05 ? qMax(0.0, room) : -room));
}

std::optional<qreal> spaceBefore(Segment* segment, const System* system)
{
    const auto before = segment->prev1MMenabled();
    if (!before || !before->measure() || before->measure()->system() != system)
        return std::nullopt;
    return segment->canvasPos().x() - before->canvasPos().x()
        - before->minHorizontalCollidingDistance(segment);
}

struct StaffSpace {
    qreal distance = 0;
    std::optional<qreal> clearance;
};

StaffSpace spaceBelow(const EngravingTarget& target)
{
    auto above = target.system;
    auto below = above;
    int staff = visibleStaff(above, target.staff + 1);
    if (target.tool == EngravingTool::System) {
        const auto& systems = above->page()->systems();
        const auto index = systems.indexOf(above);
        if (index < 0 || index + 1 >= systems.size() || systems[index + 1]->vbox())
            return {};
        below = systems[index + 1];
        staff = visibleStaff(below, 0);
    }
    if (staff < 0)
        return {};
    const auto topToTop = below->staffCanvasYpage(staff) - above->staffCanvasYpage(target.staff);
    StaffSpace result;
    result.distance = topToTop - above->staff(target.staff)->bbox().height();
    // Within one system the contours use the same horizontal origin.
    const auto& lower = above->staff(target.staff)->skyline().south();
    const auto& upper = below->staff(staff)->skyline().north();
    if (below == above && lower.valid() && upper.valid()) {
        const qreal contourDistance = lower.minDistance(upper);
        if (std::isfinite(contourDistance) && contourDistance > -1e6)
            result.clearance = topToTop - contourDistance;
    }
    if (!result.clearance)
        result.clearance = result.distance;
    return result;
}

QDockWidget* activeDock(ScoreView* view)
{
    if (!mscore || mscore->currentScoreView() != view
        || mscore->property("kumoWorkspaceView").toString() != "engrave"
        || !personalFeaturesEnabled() || view->mscoreState() != STATE_NORMAL
        || !view->score() || !view->score()->pageMode() || view->noteEntryMode())
        return nullptr;
    auto dock = mscore->findChild<QDockWidget*>(dockName);
    return dock && dock->isVisible() && dock->property("engravingShowHandles").toBool() ? dock : nullptr;
}

std::optional<Handle> currentHandle(ScoreView* view)
{
    const auto dock = activeDock(view);
    if (!dock)
        return std::nullopt;
    const auto tool = EngravingTool(dock->property("engravingTool").toInt());
    const auto score = view->score();
    const auto target = engravingTarget(score, tool);
    if (!target || !target.system || !target.system->page()
        || (target.rangeEnd && target.rangeEnd != target.measure))
        return std::nullopt;

    const auto transform = view->matrix();
    const auto system = target.system;
    const QRectF row = transform.mapRect(system->canvasBoundingRect());
    if (!row.intersects(QRectF(view->rect())))
        return std::nullopt;
    const qreal scale = transform.m11();
    const qreal sp = score->spatium();
    Handle handle;
    handle.target = target;
    handle.guide = row;
    auto addRoom = [&] (const QString& text) {
        handle.detail = text + QStringLiteral("  ·  ") + roomText(score, system);
    };
    switch (tool) {
    case EngravingTool::Symbol: {
        const auto bounds = target.symbol->canvasBoundingRect();
        handle.guide = transform.mapRect(bounds);
        handle.point = handle.guide.topRight() + QPointF(8, -7);
        const int staff = target.staff;
        if (const auto clearance = symbolClearance(target.symbol, system->page(), sp)) {
            addRoom(label("外框净距 %1").arg(spaces(*clearance / sp)));
        } else if (staff >= 0) {
            const qreal top = system->staffCanvasYpage(staff);
            const qreal bottom = top + system->staff(staff)->bbox().height();
            const qreal gap = qMax(qMax(top - bounds.bottom(), bounds.top() - bottom), 0.0);
            addRoom(label("距谱线 %1").arg(spaces(gap / sp)));
        } else {
            handle.detail = label("符号位置");
        }
        handle.referenceLength = target.symbol->spatium() * scale;
        break;
    }
    case EngravingTool::Beat: {
        const qreal x = transform.map(target.segment->canvasPos()).x();
        handle.point = QPointF(x, qMax(13.0, row.top() - 13.0));
        const auto gap = spaceBefore(target.segment, system);
        addRoom(gap ? label("列前净距 %1").arg(spaces(*gap / sp)) : label("共同拍位"));
        handle.axis = DragAxis::Horizontal;
        handle.referenceLength = sp * scale;
        break;
    }
    case EngravingTool::Measure: {
        const auto box = transform.mapRect(target.measure->canvasBoundingRect());
        handle.point = QPointF(box.right(), qMax(13.0, row.top() - 13.0));
        addRoom(label("小节宽 %1").arg(spaces(target.measure->width() / sp)));
        handle.axis = DragAxis::Horizontal;
        handle.referenceLength = qMax(1.0, box.width());
        break;
    }
    case EngravingTool::Staff:
    case EngravingTool::System: {
        const auto staff = system->staff(target.staff);
        const qreal bottom = system->staffCanvasYpage(target.staff) + staff->bbox().height();
        handle.point = QPointF(qMin(row.right() - 14., view->width() - 14.),
                               transform.map(QPointF(0, bottom)).y() + 12);
        const auto space = spaceBelow(target);
        handle.detail = space.clearance ? label("下方净距 %1").arg(spaces(*space.clearance / sp))
            : tool == EngravingTool::Staff ? label("谱表下方") : label("谱行下方");
        handle.axis = DragAxis::Vertical;
        handle.referenceLength = sp * scale;
        break;
    }
    }
    return view->rect().adjusted(8, 8, -8, -8).contains(handle.point.toPoint())
        ? std::optional<Handle>(std::move(handle)) : std::nullopt;
}

QPointF constrainedDelta(const Handle& handle, QPointF delta)
{
    if (handle.axis == DragAxis::Horizontal)
        delta.setY(0);
    else if (handle.axis == DragAxis::Vertical)
        delta.setX(0);
    return delta;
}

bool sameTarget(const EngravingTarget& a, const EngravingTarget& b)
{
    return a.tool == b.tool && a.symbol == b.symbol && a.measure == b.measure
        && a.rangeEnd == b.rangeEnd && a.segment == b.segment && a.system == b.system
        && a.staff == b.staff;
}

class EngravingDrag final : public QObject {
    struct Gesture {
        Handle handle;
        QVariant initial;
        QPointF press;
        QPointF delta;
        QTransform transform;
        const Score* score;
        ScoreContentState contentState;
    };
    ScoreView* view;
    std::optional<Gesture> gesture;
    bool cancelledPress = false;

    void cancel()
    {
        if (gesture) {
            gesture.reset();
            view->update();
        }
    }

    QVariant proposed(const Gesture& drag) const
    {
        const auto delta = drag.delta / drag.handle.referenceLength;
        switch (drag.handle.target.tool) {
        case EngravingTool::Symbol:
            return drag.initial.toPointF() + delta * drag.handle.target.symbol->spatium();
        case EngravingTool::Beat:
            return qBound(-10.0, drag.initial.toDouble() + delta.x(), 20.0);
        case EngravingTool::Measure:
            return qBound(0.25, drag.initial.toDouble() * (1 + delta.x()), 4.0);
        case EngravingTool::Staff:
        case EngravingTool::System: {
            const qreal startingGap = drag.initial.isValid() ? drag.initial.toDouble()
                : spaceBelow(drag.handle.target).distance / drag.score->spatium();
            return qBound(0.0, startingGap + delta.y(), 30.0);
        }
        }
        return {};
    }

    QString feedback(const Gesture& drag) const
    {
        const auto next = proposed(drag);
        switch (drag.handle.target.tool) {
        case EngravingTool::Symbol:
            return label("横向 %1  ·  纵向 %2")
                .arg(spaces(drag.delta.x() / drag.handle.referenceLength),
                     spaces(drag.delta.y() / drag.handle.referenceLength));
        case EngravingTool::Beat:
            return label("拍位前留白 %1").arg(spaces(next.toDouble()));
        case EngravingTool::Measure:
            return label("小节宽度 %1%").arg(qRound(next.toDouble() * 100));
        case EngravingTool::Staff:
        case EngravingTool::System:
            return label("下方最小距离 %1").arg(spaces(next.toDouble()));
        }
        return {};
    }

    bool current(const std::optional<Handle>& handle) const
    {
        return gesture && handle && view->score() == gesture->score && view->matrix() == gesture->transform
            && view->score()->masterScore()->state() == gesture->contentState
            && sameTarget(handle->target, gesture->handle.target);
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick: {
            const auto mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton)
                return false;
            cancelledPress = false;
            if (mouse->modifiers() != Qt::NoModifier)
                return false;
            const auto handle = currentHandle(view);
            if (!handle || QLineF(mouse->localPos(), handle->point).length() > 8.0)
                return false;
            gesture = Gesture{*handle, engravingValue(handle->target), mouse->localPos(), {},
                view->matrix(), view->score(), view->score()->masterScore()->state()};
            view->update();
            return true;
        }
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease: {
            const auto mouse = static_cast<QMouseEvent*>(event);
            if (cancelledPress) {
                if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
                    cancelledPress = false;
                    return true;
                }
                if (mouse->buttons() & Qt::LeftButton)
                    return true;
            }
            if (!gesture)
                return false;
            const auto handle = currentHandle(view);
            if (!current(handle)) {
                cancel();
                return true;
            }
            gesture->delta = constrainedDelta(*handle, mouse->localPos() - gesture->press);
            if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
                const auto value = proposed(*gesture);
                const auto changed = QLineF(QPointF(), gesture->delta).length() >= QApplication::startDragDistance();
                cancel();
                if (changed)
                    changeEngravingValue(handle->target, value);
            }
            view->update();
            return true;
        }
        case QEvent::ShortcutOverride:
        case QEvent::KeyPress:
            if (gesture && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
                if (event->type() == QEvent::KeyPress)
                    cancelWithRelease();
                event->accept();
                return true;
            }
            break;
        case QEvent::UngrabMouse:
        case QEvent::Hide:
        case QEvent::WindowDeactivate:
        case QEvent::FocusOut:
        case QEvent::Wheel:
            cancelWithRelease();
            break;
        default:
            break;
        }
        return false;
    }

public:
    explicit EngravingDrag(ScoreView* scoreView) : QObject(scoreView), view(scoreView)
    {
        setObjectName("kumoEngravingDrag");
        view->installEventFilter(this);
    }

    bool cancelWithRelease()
    {
        if (!gesture)
            return false;
        cancelledPress = true;
        cancel();
        return true;
    }

    void paint(QPainter& painter)
    {
        const auto handle = currentHandle(view);
        if (gesture && !current(handle))
            cancelWithRelease();
        if (!handle)
            return;
        const auto point = handle->point + (gesture ? gesture->delta : QPointF());
        painter.save();
        painter.resetTransform();
        painter.setClipRect(view->rect());
        painter.setRenderHint(QPainter::Antialiasing);
        const auto colors = view->palette();
        const QColor accent = colors.color(QPalette::Highlight);
        QColor guideColor = accent;
        guideColor.setAlpha(gesture ? 190 : 115);
        painter.setPen(QPen(guideColor, 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        if (handle->axis == DragAxis::Horizontal)
            painter.drawLine(QPointF(point.x(), handle->guide.top()), QPointF(point.x(), handle->guide.bottom()));
        else if (handle->axis == DragAxis::Vertical)
            painter.drawLine(QPointF(handle->guide.left(), point.y()), QPointF(point.x(), point.y()));
        else
            painter.drawRect(handle->guide.translated(gesture ? gesture->delta : QPointF()).adjusted(-2, -2, 2, 2));

        painter.setPen(QPen(accent, 1.4));
        painter.setBrush(colors.brush(QPalette::Base));
        const auto knob = QRectF(point - QPointF(6, 6), QSizeF(12, 12));
        painter.drawRoundedRect(knob, 3, 3);
        if (handle->axis != DragAxis::Vertical)
            painter.drawLine(point - QPointF(3, 0), point + QPointF(3, 0));
        if (handle->axis != DragAxis::Horizontal)
            painter.drawLine(point - QPointF(0, 3), point + QPointF(0, 3));

        const auto text = gesture ? feedback(*gesture) : handle->detail;
        const auto fit = painter.fontMetrics().elidedText(text, Qt::ElideRight, qMax(30, view->width() - 32));
        const auto extent = painter.fontMetrics().size(Qt::TextSingleLine, fit) + QSize(12, 6);
        const int x = qBound(4, qRound(point.x()) + 11, qMax(4, view->width() - extent.width() - 4));
        const int y = qBound(2, qRound(point.y()) - extent.height() / 2, qMax(2, view->height() - extent.height() - 2));
        const QRect box(QPoint(x, y), extent);
        painter.setPen(QPen(colors.color(QPalette::Mid), 1));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(colors.color(QPalette::Text));
        painter.drawText(box.adjusted(6, 0, -6, 0), Qt::AlignVCenter, fit);
        painter.restore();
    }
};

EngravingDrag* dragController(ScoreView* view)
{
    return view ? static_cast<EngravingDrag*>(view->findChild<QObject*>(
        "kumoEngravingDrag", Qt::FindDirectChildrenOnly)) : nullptr;
}
}

void paintEngravingHandle(ScoreView* view, QPainter& painter)
{
    auto drag = dragController(view);
    if (!drag && activeDock(view))
        drag = new EngravingDrag(view);
    if (drag)
        drag->paint(painter);
}

bool cancelEngravingDrag(ScoreView* view)
{
    const auto drag = dragController(view);
    return drag && drag->cancelWithRelease();
}

void refreshEngravingSelection(MuseScore* window)
{
    if (window && window->currentScoreView()
        && window->property("kumoWorkspaceView").toString() == "engrave")
        window->currentScoreView()->update();
}
}
