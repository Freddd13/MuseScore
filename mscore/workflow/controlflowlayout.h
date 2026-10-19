// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MS_CONTROLFLOWLAYOUT_H
#define MS_CONTROLFLOWLAYOUT_H

#include <QLayout>
#include <QList>
#include <QScrollArea>
#include <QWidget>

namespace Ms {
// Compact controls keep their actual preferred widths and wrap as a row.
// The trailing space belongs to the canvas, rather than to oversized buttons.
class ControlFlowLayout final : public QLayout {
    QList<QLayoutItem*> _items;
    int arrange(const QRect& rect, bool measure) const
    {
        const auto margins = contentsMargins();
        const QRect area = rect.marginsRemoved(margins);
        const int gap = qMax(0, spacing());
        int x = area.x(), y = area.y(), rowHeight = 0;
        QList<QPair<QLayoutItem*, QRect>> row;
        auto finishRow = [&] {
            if (!measure)
                for (const auto& cell : row) {
                    auto bounds = cell.second;
                    bounds.translate(0, (rowHeight - bounds.height()) / 2);
                    cell.first->setGeometry(bounds);
                }
            row.clear();
        };
        for (auto item : _items) {
            if (item->isEmpty())
                continue;
            QSize size = item->sizeHint().expandedTo(item->minimumSize()).boundedTo(item->maximumSize());
            size.setWidth(qMin(size.width(), qMax(1, area.width())));
            if (!row.isEmpty() && x + size.width() > area.right() + 1) {
                finishRow();
                x = area.x();
                y += rowHeight + gap;
                rowHeight = 0;
            }
            row.append({item, QRect(QPoint(x, y), size)});
            x += size.width() + gap;
            rowHeight = qMax(rowHeight, size.height());
        }
        finishRow();
        return y + rowHeight - rect.y() + margins.bottom();
    }

public:
    explicit ControlFlowLayout(QWidget* parent = nullptr) : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(5);
    }
    ~ControlFlowLayout() override
    {
        while (auto item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem* item) override { _items.append(item); invalidate(); }
    int count() const override { return _items.size(); }
    QLayoutItem* itemAt(int index) const override { return _items.value(index); }
    QLayoutItem* takeAt(int index) override
    {
        return index >= 0 && index < _items.size() ? _items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), true); }
    QSize minimumSize() const override
    {
        QSize size;
        for (auto item : _items)
            if (!item->isEmpty())
                size = size.expandedTo(item->minimumSize());
        const auto margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }
    QSize sizeHint() const override
    {
        int width = 0;
        for (auto item : _items)
            if (!item->isEmpty())
                width += item->sizeHint().width() + qMax(0, spacing());
        return QSize(width, minimumSize().height());
    }
    void setGeometry(const QRect& rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, false);
    }
};

class ControlScrollArea final : public QScrollArea {
public:
    explicit ControlScrollArea(QWidget* parent = nullptr) : QScrollArea(parent)
    {
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setAutoFillBackground(false);
        viewport()->setAutoFillBackground(false);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setMinimumHeight(28);
        setMaximumHeight(120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    void setWidget(QWidget* content)
    {
        QScrollArea::setWidget(content);
        // QScrollArea enables a separate background on its content. The
        // surrounding editor or detached panel already owns the theme.
        if (content)
            content->setAutoFillBackground(false);
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        if (!widget() || !widget()->layout())
            return minimumHeight();
        const int height = widget()->layout()->heightForWidth(qMax(1, width - 4));
        return qBound(minimumHeight(), height + 4, maximumHeight());
    }
    QSize sizeHint() const override { return QSize(480, heightForWidth(qMax(320, width()))); }
};
} // namespace Ms
#endif
