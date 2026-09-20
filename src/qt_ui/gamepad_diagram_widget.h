// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// An original schematic drawing of a generic game pad -- body, D-pad, four
// face buttons (plain triangle/circle/cross/square outlines), two sticks,
// shoulders, triggers and a touchpad strip -- used in input_bindings_dialog.cpp
// to pick a pad control by clicking it instead of hunting through a list of
// thirty names.
//
// Deliberately generic: invented proportions, one accent colour, no brand
// colour-coding, no specific manufacturer's industrial design. It is a
// diagram of the *vocabulary* in input_ids.h, not a picture of a product, and
// it should stay that way -- see the note in input_bindings_dialog.h.
//
// Only outputs are drawn. `back` and `share` are input-only names
// (input_ids.h's kPadInputOnlyNames), so the shapes where they would sit are
// inert decoration: nothing in the editor could be selected by clicking them.
//
// Every colour comes from palette() -- Window/WindowText/Base/Highlight, the
// same pattern table_item_delegate.cpp uses -- so it draws correctly in both
// PS5_Dark and PS5_White with no hardcoded hex.

#pragma once

#include <QRectF>
#include <QSet>
#include <QString>
#include <QWidget>
#include <string>
#include <utility>
#include <vector>

class GamepadDiagramWidget : public QWidget {
    Q_OBJECT
public:
    explicit GamepadDiagramWidget(QWidget* parent = nullptr);

    // Highlights outputName's region, if the diagram has one for it (the
    // axis outputs aren't drawn -- there is no one place on a pad that means
    // "axis_left_x_plus" that the stick itself doesn't already mean).
    void SetHighlightedOutput(const QString& outputName);

    // The outputs that currently have at least one binding, marked with a dot
    // so the diagram answers "what is mapped?" at a glance. Names the diagram
    // doesn't draw are ignored.
    void SetBoundOutputs(const QSet<QString>& outputNames);

    // Small on purpose. This is a picker, not the point of the window, and
    // its old floor of 207px was a third of the dialog's height before
    // anything else had asked for any.
    [[nodiscard]] QSize sizeHint() const override {
        return QSize(340, 234);
    }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return QSize(200, 138);
    }

signals:
    // Emitted when the person clicks a region that maps to a known output
    // (input_ids.h's pad vocabulary).
    void OutputClicked(const QString& outputName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Region {
        std::string output;
        QRectF rect;
    };

    // The drawing area: the largest rect of the diagram's own aspect ratio
    // that fits, centred. Without this the pad stretches with the widget.
    [[nodiscard]] QRectF Canvas() const;
    void RebuildRegions();
    [[nodiscard]] const QRectF* RectFor(const std::string& output) const;
    [[nodiscard]] std::string OutputAt(const QPointF& pos) const;

    // How a control is drawn right now. Selection fills; hover tints; being
    // bound adds a dot. They are three separate channels on purpose, so a
    // bound control still reads as bound while something else is selected.
    struct Look {
        QColor fill;
        QColor pen;
        bool bound = false;
    };
    [[nodiscard]] Look LookFor(const std::string& output) const;
    void DrawBound(QPainter& p, const QRectF& rect, const Look& look) const;

    void DrawBody(QPainter& p) const;
    void DrawTriggersAndShoulders(QPainter& p) const;
    void DrawTouchpad(QPainter& p) const;
    void DrawDpad(QPainter& p) const;
    void DrawFaceButtons(QPainter& p) const;
    void DrawSticks(QPainter& p) const;
    void DrawCentreButtons(QPainter& p) const;

    std::vector<Region> m_regions; // in hit-test order
    QRectF m_canvas;
    QString m_highlighted;
    std::string m_hovered;
    QSet<QString> m_bound;
};
