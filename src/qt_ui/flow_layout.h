// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <QLayout>
#include <QRect>
#include <QStyle>

class FlowLayout : public QLayout {
public:
    struct position {
        int row{};
        int col{};

        operator bool() const {
            return row >= 0 && col >= 0;
        }
    };

    explicit FlowLayout(QWidget* parent, int margin = -1, bool dynamic_spacing = false,
                        int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, bool dynamic_spacing = false, int hSpacing = -1,
                        int vSpacing = -1);
    ~FlowLayout();

    void clear();
    const QList<QLayoutItem*>& item_list() const {
        return m_item_list;
    }
    const QList<position>& positions() const {
        return m_positions;
    }
    int rows() const {
        return m_rows;
    }
    int cols() const {
        return m_cols;
    }

    void addItem(QLayoutItem* item) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int) const override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

private:
    int horizontalSpacing() const;
    int verticalSpacing() const;
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_item_list;
    bool m_dynamic_spacing{};
    int m_h_space_initial{-1};
    int m_v_space_initial{-1};
    mutable int m_h_space{-1};
    mutable int m_v_space{-1};

    mutable QList<position> m_positions;
    mutable int m_rows{};
    mutable int m_cols{};
};
