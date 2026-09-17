// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <string_view>

#include "gamepad_diagram_widget.h"

namespace {

// Normalized (0-1) layout, independent of actual widget size. Loosely
// schematic, not a trace of any specific controller's proportions.
struct NormRegion {
    const char* output;
    qreal x, y, w, h; // fraction of the drawing rect
};

constexpr NormRegion kLayout[] = {
    {"pad_up", 0.18, 0.38, 0.07, 0.09},   {"pad_down", 0.18, 0.55, 0.07, 0.09},
    {"pad_left", 0.10, 0.46, 0.09, 0.07}, {"pad_right", 0.25, 0.46, 0.09, 0.07},

    {"triangle", 0.79, 0.30, 0.09, 0.09}, {"cross", 0.79, 0.55, 0.09, 0.09},
    {"square", 0.71, 0.42, 0.09, 0.09},   {"circle", 0.87, 0.42, 0.09, 0.09},

    {"l3", 0.30, 0.68, 0.12, 0.16},       {"r3", 0.58, 0.68, 0.12, 0.16},

    {"l1", 0.06, 0.06, 0.16, 0.08},       {"r1", 0.78, 0.06, 0.16, 0.08},
    {"l2", 0.06, -0.04, 0.16, 0.08},      {"r2", 0.78, -0.04, 0.16, 0.08},

    {"touchpad_left", 0.30, 0.16, 0.15, 0.14},
    {"touchpad_center", 0.45, 0.16, 0.10, 0.14},
    {"touchpad_right", 0.55, 0.16, 0.15, 0.14},

    {"options", 0.68, 0.20, 0.06, 0.05},  {"back", 0.26, 0.20, 0.06, 0.05},
};

} // namespace

GamepadDiagramWidget::GamepadDiagramWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(false);
    RebuildRegions();
}

void GamepadDiagramWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    RebuildRegions();
}

void GamepadDiagramWidget::RebuildRegions() {
    m_regions.clear();
    const QRectF rect(0, 0, width(), height());
    for (const auto& r : kLayout) {
        m_regions[r.output] =
            QRectF(rect.x() + r.x * rect.width(), rect.y() + r.y * rect.height(),
                  r.w * rect.width(), r.h * rect.height());
    }
}

void GamepadDiagramWidget::SetHighlightedOutput(const QString& outputName) {
    if (m_highlighted == outputName) {
        return;
    }
    m_highlighted = outputName;
    update();
}

void GamepadDiagramWidget::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    for (const auto& [name, rect] : m_regions) {
        if (rect.contains(pos)) {
            emit OutputClicked(QString::fromStdString(name));
            return;
        }
    }
}

void GamepadDiagramWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor body = palette().color(QPalette::Button);
    const QColor outline = palette().color(QPalette::WindowText);
    const QColor fill = palette().color(QPalette::Base);
    const QColor highlight = palette().color(QPalette::Highlight);

    DrawShoulders(p, fill, highlight);
    DrawBody(p, body, outline);
    DrawTouchpadAndCenter(p, fill, highlight);
    DrawDpad(p, fill, highlight);
    DrawFaceButtons(p, outline, highlight);
    DrawSticks(p, fill, highlight);
}

void GamepadDiagramWidget::DrawBody(QPainter& p, const QColor& body, const QColor& outline) const {
    const QRectF r(0, height() * 0.05, width(), height() * 0.85);
    QPainterPath path;
    path.addRoundedRect(r, r.height() * 0.35, r.height() * 0.35);
    // Two grip bulges at the bottom corners -- what reads as "a pad" at a
    // glance without tracing anyone's specific curvature.
    path.addEllipse(QPointF(r.left() + r.width() * 0.12, r.bottom() - r.height() * 0.05),
                    r.width() * 0.14, r.height() * 0.22);
    path.addEllipse(QPointF(r.right() - r.width() * 0.12, r.bottom() - r.height() * 0.05),
                    r.width() * 0.14, r.height() * 0.22);

    p.setPen(QPen(outline, 1.5));
    p.setBrush(body);
    p.drawPath(path.simplified());
}

void GamepadDiagramWidget::DrawDpad(QPainter& p, const QColor& fill, const QColor& highlight) const {
    static const char* names[] = {"pad_up", "pad_down", "pad_left", "pad_right"};
    for (const auto* name : names) {
        const auto it = m_regions.find(name);
        if (it == m_regions.end()) {
            continue;
        }
        const bool hi = m_highlighted.toStdString() == name;
        p.setPen(QPen(palette().color(QPalette::WindowText), 1.2));
        p.setBrush(hi ? highlight : fill);
        p.drawRoundedRect(it->second, 3, 3);
    }
}

void GamepadDiagramWidget::DrawFaceButtons(QPainter& p, const QColor& outline,
                                           const QColor& highlight) const {
    const QColor bg = palette().color(QPalette::Base);
    for (const auto& name : {"triangle", "circle", "cross", "square"}) {
        const auto it = m_regions.find(name);
        if (it == m_regions.end()) {
            continue;
        }
        const QRectF r = it->second;
        const bool hi = m_highlighted.toStdString() == name;
        p.setPen(QPen(outline, 1.5));
        p.setBrush(hi ? highlight : bg);
        p.drawEllipse(r);

        // Plain, single-color glyphs -- generic outlines, not a reproduction
        // of any brand's specific per-button color coding.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(outline, 1.6));
        const QRectF g = r.adjusted(r.width() * 0.28, r.height() * 0.28, -r.width() * 0.28,
                                    -r.height() * 0.28);
        if (std::string_view(name) == "triangle") {
            QPolygonF tri;
            tri << QPointF(g.center().x(), g.top()) << QPointF(g.left(), g.bottom())
                << QPointF(g.right(), g.bottom());
            p.drawPolygon(tri);
        } else if (std::string_view(name) == "circle") {
            p.drawEllipse(g);
        } else if (std::string_view(name) == "cross") {
            p.drawLine(g.topLeft(), g.bottomRight());
            p.drawLine(g.topRight(), g.bottomLeft());
        } else { // square
            p.drawRect(g);
        }
    }
}

void GamepadDiagramWidget::DrawSticks(QPainter& p, const QColor& fill, const QColor& highlight) const {
    for (const auto& name : {"l3", "r3"}) {
        const auto it = m_regions.find(name);
        if (it == m_regions.end()) {
            continue;
        }
        const bool hi = m_highlighted.toStdString() == name;
        p.setPen(QPen(palette().color(QPalette::WindowText), 1.5));
        p.setBrush(hi ? highlight : fill);
        p.drawEllipse(it->second);
        p.setBrush(palette().color(QPalette::WindowText));
        p.setPen(Qt::NoPen);
        const QPointF c = it->second.center();
        p.drawEllipse(c, it->second.width() * 0.18, it->second.width() * 0.18);
    }
}

void GamepadDiagramWidget::DrawShoulders(QPainter& p, const QColor& fill,
                                         const QColor& highlight) const {
    for (const auto& name : {"l1", "r1", "l2", "r2"}) {
        const auto it = m_regions.find(name);
        if (it == m_regions.end()) {
            continue;
        }
        const bool hi = m_highlighted.toStdString() == name;
        p.setPen(QPen(palette().color(QPalette::WindowText), 1.2));
        p.setBrush(hi ? highlight : fill);
        p.drawRoundedRect(it->second, 4, 4);
    }
}

void GamepadDiagramWidget::DrawTouchpadAndCenter(QPainter& p, const QColor& fill,
                                                 const QColor& highlight) const {
    for (const auto& name : {"touchpad_left", "touchpad_center", "touchpad_right"}) {
        const auto it = m_regions.find(name);
        if (it == m_regions.end()) {
            continue;
        }
        const bool hi = m_highlighted.toStdString() == name;
        p.setPen(QPen(palette().color(QPalette::WindowText), 1.0));
        p.setBrush(hi ? highlight : fill);
        p.drawRoundedRect(it->second, 3, 3);
    }
    for (const auto& name : {"options", "back"}) {
        const auto it = m_regions.find(name);
        if (it == m_regions.end()) {
            continue;
        }
        const bool hi = m_highlighted.toStdString() == name;
        p.setPen(QPen(palette().color(QPalette::WindowText), 1.0));
        p.setBrush(hi ? highlight : fill);
        p.drawEllipse(it->second);
    }
}
