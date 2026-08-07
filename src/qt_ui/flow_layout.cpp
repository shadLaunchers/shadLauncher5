// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause

#include <QtWidgets>
#include "flow_layout.h"

FlowLayout::FlowLayout(QWidget* parent, int margin, bool dynamic_spacing, int hSpacing,
                       int vSpacing)
    : QLayout(parent), m_dynamic_spacing(dynamic_spacing), m_h_space_initial(hSpacing),
      m_v_space_initial(vSpacing), m_h_space(hSpacing), m_v_space(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, bool dynamic_spacing, int hSpacing, int vSpacing)
    : m_dynamic_spacing(dynamic_spacing), m_h_space_initial(hSpacing), m_v_space_initial(vSpacing),
      m_h_space(hSpacing), m_v_space(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout() {
    clear();
}

void FlowLayout::clear() {
    // We can't use a ranged loop here, since deleting the widget will call takeAt on the layout. So
    // let's also use takeAt.
    while (QLayoutItem* item = takeAt(0)) {
        if (item) {
            delete item->widget();
            delete item;
        }
    }
    m_item_list.clear();
    m_positions.clear();
}

void FlowLayout::addItem(QLayoutItem* item) {
    m_positions.append(position{.row = -1, .col = -1});
    m_item_list.append(item);
}

int FlowLayout::horizontalSpacing() const {
    if (m_h_space >= 0) {
        return m_h_space;
    }

    return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const {
    if (m_v_space >= 0) {
        return m_v_space;
    }

    return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::count() const {
    return m_item_list.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const {
    return m_item_list.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index) {
    if (index >= 0 && index < m_item_list.size()) {
        m_positions.takeAt(index);
        return m_item_list.takeAt(index);
    }

    return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const {
    return {};
}

bool FlowLayout::hasHeightForWidth() const {
    return true;
}

int FlowLayout::heightForWidth(int width) const {
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const {
    return minimumSize();
}

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (const QLayoutItem* item : m_item_list) {
        if (item) {
            size = size.expandedTo(item->minimumSize());
        }
    }

    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const {
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    const QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;
    int row_count = 0;
    int col_count = 0;

    if (m_dynamic_spacing) {
        const int available_width = effectiveRect.width();
        const int min_spacing = smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
        bool fits_into_width = true;
        int width = 0;
        int index = 0;

        for (; index < m_item_list.size(); index++) {
            if (QLayoutItem* item = m_item_list.at(index)) {
                const int new_width =
                    width + item->sizeHint().width() + (width > 0 ? min_spacing : 0);

                if (new_width > effectiveRect.width()) {
                    fits_into_width = false;
                    break;
                }

                width = new_width;
            }
        }

        // Try to evenly distribute the items across the width
        m_h_space = (index == 0) ? -1 : ((available_width - width) / index);

        if (fits_into_width) {
            // Make sure there aren't huge gaps between the items
            m_h_space = smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
        }
    } else {
        m_h_space = m_h_space_initial;
    }

    for (int i = 0, row = 0, col = 0; i < m_item_list.size(); i++) {
        QLayoutItem* item = m_item_list.at(i);
        if (!item)
            continue;

        const QWidget* wid = item->widget();
        if (!wid)
            continue;

        int spaceX = horizontalSpacing();
        if (spaceX == -1)
            spaceX = wid->style()->layoutSpacing(QSizePolicy::PushButton, QSizePolicy::PushButton,
                                                 Qt::Horizontal);

        int spaceY = verticalSpacing();
        if (spaceY == -1)
            spaceY = wid->style()->layoutSpacing(QSizePolicy::PushButton, QSizePolicy::PushButton,
                                                 Qt::Vertical);

        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
            col = 0;
            row++;
        }

        position& pos = m_positions[i];
        pos.row = row;
        pos.col = col++;

        row_count = std::max(row_count, pos.row + 1);
        col_count = std::max(col_count, pos.col + 1);

        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }

    m_rows = row_count;
    m_cols = col_count;

    return y + lineHeight - rect.y() + bottom;
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const {
    const QObject* parent = this->parent();
    if (!parent) {
        return -1;
    }

    if (parent->isWidgetType()) {
        const QWidget* pw = static_cast<const QWidget*>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    }

    return static_cast<const QLayout*>(parent)->spacing();
}
