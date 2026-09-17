// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A simple, original schematic drawing of a generic PlayStation-style pad --
// rounded body, D-pad, four face buttons (plain triangle/circle/cross/square
// outlines), two sticks, shoulder buttons, and a touchpad strip. This is a
// deliberately generic diagram (single accent color, no specific curvature
// or brand color-coding), not a reproduction of any specific controller's
// industrial design. It exists to make picking a pad control in
// input_bindings_dialog.cpp more visual than reading a plain list: click a
// region, and the matching output gets selected.
//
// All theme colors come from palette() (QPalette::Window/WindowText/
// Highlight/...), the same pattern table_item_delegate.cpp uses, so this
// draws correctly in both PS5_Dark and PS5_White without any hardcoded hex.

#pragma once

#include <QWidget>
#include <map>
#include <string>

class GamepadDiagramWidget : public QWidget {
    Q_OBJECT
public:
    explicit GamepadDiagramWidget(QWidget* parent = nullptr);

    // Highlights outputName's region, if the diagram has one for it (not
    // every output does -- axes and analog-to-analog outputs aren't drawn).
    void SetHighlightedOutput(const QString& outputName);

    [[nodiscard]] QSize sizeHint() const override {
        return QSize(360, 260);
    }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return QSize(280, 200);
    }

signals:
    // Emitted when the person clicks a region that maps to a known output
    // (input_ids.h's pad vocabulary).
    void OutputClicked(const QString& outputName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void RebuildRegions();
    void DrawBody(QPainter& p, const QColor& body, const QColor& outline) const;
    void DrawDpad(QPainter& p, const QColor& fill, const QColor& highlight) const;
    void DrawFaceButtons(QPainter& p, const QColor& outline, const QColor& highlight) const;
    void DrawSticks(QPainter& p, const QColor& fill, const QColor& highlight) const;
    void DrawShoulders(QPainter& p, const QColor& fill, const QColor& highlight) const;
    void DrawTouchpadAndCenter(QPainter& p, const QColor& fill, const QColor& highlight) const;

    // Normalized (0-1 of the widget's drawing rect) regions per output name,
    // rebuilt whenever the widget resizes.
    std::map<std::string, QRectF> m_regions;
    QString m_highlighted;
};
