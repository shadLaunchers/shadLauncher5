// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <string_view>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include "gamepad_diagram_widget.h"

constexpr qreal Aspect = 1.45;

struct NormRegion {
    const char* output;
    qreal x, y, w, h; // fractions of the canvas
};

constexpr NormRegion kLayout[] = {
    // Triggers sit above the shoulders, both fully inside the canvas -- the
    // old layout put them at y = -0.04, half of each clipped off the top.
    {"l2", 0.170, 0.018, 0.166, 0.058},
    {"r2", 0.664, 0.018, 0.166, 0.058},
    // The shoulders run a little under the shell's top edge, so they read as
    // part of the pad rather than two pills floating above it.
    {"l1", 0.170, 0.082, 0.166, 0.076},
    {"r1", 0.664, 0.082, 0.166, 0.076},

    // One touchpad, three zones. The emulator splits a press by where it
    // landed across the pad's width (TOUCHPAD_REGIONS in input_bindings.cpp),
    // so the three are drawn as thirds of one strip rather than as separate
    // buttons.
    {"touchpad_left", 0.3930, 0.228, 0.0713, 0.146},
    {"touchpad_center", 0.4643, 0.228, 0.0713, 0.146},
    {"touchpad_right", 0.5357, 0.228, 0.0713, 0.146},

    {"options", 0.632, 0.238, 0.036, 0.042},

    // The arms are the shape they are on a pad: up/down taller than wide,
    // left/right wider than tall, meeting at the centre.
    {"pad_up", 0.2210, 0.296, 0.0380, 0.064},
    {"pad_down", 0.2210, 0.424, 0.0380, 0.064},
    {"pad_left", 0.1755, 0.360, 0.0455, 0.064},
    {"pad_right", 0.2590, 0.360, 0.0455, 0.064},

    {"triangle", 0.7230, 0.276, 0.0620, 0.089},
    {"circle", 0.7840, 0.364, 0.0620, 0.089},
    {"cross", 0.7230, 0.452, 0.0620, 0.089},
    {"square", 0.6620, 0.364, 0.0620, 0.089},

    {"l3", 0.3220, 0.500, 0.1080, 0.156},
    {"r3", 0.5700, 0.500, 0.1080, 0.156},
};

QColor Mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                            a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

QColor WithAlpha(QColor c, int alpha) {
    c.setAlpha(alpha);
    return c;
}

GamepadDiagramWidget::GamepadDiagramWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true); // for hover feedback
    setCursor(Qt::ArrowCursor);
    RebuildRegions();
}

QRectF GamepadDiagramWidget::Canvas() const {
    const qreal w = width();
    const qreal h = height();
    if (w <= 0 || h <= 0) {
        return {};
    }
    // Fit the authored box, centred, with a little breathing room.
    const qreal avail_w = w - 8.0;
    const qreal avail_h = h - 8.0;
    qreal cw = avail_w;
    qreal ch = cw / Aspect;
    if (ch > avail_h) {
        ch = avail_h;
        cw = ch * Aspect;
    }
    return {(w - cw) / 2.0, (h - ch) / 2.0, cw, ch};
}

void GamepadDiagramWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    RebuildRegions();
}

void GamepadDiagramWidget::RebuildRegions() {
    m_canvas = Canvas();
    m_regions.clear();
    m_regions.reserve(std::size(kLayout));
    for (const auto& r : kLayout) {
        m_regions.push_back({r.output, QRectF(m_canvas.x() + r.x * m_canvas.width(),
                                              m_canvas.y() + r.y * m_canvas.height(),
                                              r.w * m_canvas.width(), r.h * m_canvas.height())});
    }
}

const QRectF* GamepadDiagramWidget::RectFor(const std::string& output) const {
    const auto it = std::find_if(m_regions.begin(), m_regions.end(),
                                 [&](const Region& r) { return r.output == output; });
    return it == m_regions.end() ? nullptr : &it->rect;
}

std::string GamepadDiagramWidget::OutputAt(const QPointF& pos) const {
    const Region* best = nullptr;
    for (const auto& r : m_regions) {
        if (!r.rect.contains(pos)) {
            continue;
        }
        if (best == nullptr ||
            r.rect.width() * r.rect.height() < best->rect.width() * best->rect.height()) {
            best = &r;
        }
    }
    return best == nullptr ? std::string() : best->output;
}

void GamepadDiagramWidget::SetHighlightedOutput(const QString& outputName) {
    if (m_highlighted == outputName) {
        return;
    }
    m_highlighted = outputName;
    update();
}

void GamepadDiagramWidget::SetBoundOutputs(const QSet<QString>& outputNames) {
    if (m_bound == outputNames) {
        return;
    }
    m_bound = outputNames;
    update();
}

static QString RegionForControl(const QString& name) {
    if (name.startsWith(QLatin1String("axis_left"))) {
        return QStringLiteral("l3");
    }
    if (name.startsWith(QLatin1String("axis_right"))) {
        return QStringLiteral("r3");
    }
    return name;
}

void GamepadDiagramWidget::SetPressedControl(const QString& name, bool pressed) {
    if (pressed) {
        m_pressed_controls.insert(name);
    } else {
        m_pressed_controls.remove(name);
    }

    QSet<QString> regions;
    for (const QString& held : m_pressed_controls) {
        regions.insert(RegionForControl(held));
    }
    if (regions != m_pressed) {
        m_pressed = std::move(regions);
        update();
    }
}

void GamepadDiagramWidget::ClearPressed() {
    m_pressed_controls.clear();
    if (m_pressed.isEmpty()) {
        return;
    }
    m_pressed.clear();
    update();
}

void GamepadDiagramWidget::mousePressEvent(QMouseEvent* event) {
    const std::string hit = OutputAt(event->position());
    if (!hit.empty()) {
        emit OutputClicked(QString::fromStdString(hit));
    }
}

void GamepadDiagramWidget::mouseMoveEvent(QMouseEvent* event) {
    const std::string hit = OutputAt(event->position());
    if (hit == m_hovered) {
        return;
    }
    m_hovered = hit;
    setCursor(hit.empty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    setToolTip(hit.empty() ? QString() : QString::fromStdString(hit));
    update();
}

void GamepadDiagramWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (!m_hovered.empty()) {
        m_hovered.clear();
        setCursor(Qt::ArrowCursor);
        update();
    }
}

GamepadDiagramWidget::Look GamepadDiagramWidget::LookFor(const std::string& output) const {
    const QColor base = palette().color(QPalette::Base);
    const QColor text = palette().color(QPalette::WindowText);
    const QColor accent = palette().color(QPalette::Highlight);

    const QString id = QString::fromStdString(output);
    Look look;
    look.fill = base;
    look.pen = WithAlpha(text, 150);
    look.bound = m_bound.contains(id);
    look.pressed = m_pressed.contains(id);

    if (look.pressed) {
        look.fill = Mix(base, text, 0.72);
        look.pen = WithAlpha(text, 235);
    } else if (m_highlighted.toStdString() == output) {
        look.fill = accent;
        look.pen = accent.darker(130);
    } else if (m_hovered == output) {
        look.fill = Mix(base, accent, 0.30);
        look.pen = WithAlpha(accent, 220);
    }
    return look;
}

void GamepadDiagramWidget::DrawBound(QPainter& p, const QRectF& rect, const Look& look) const {
    if (!look.bound) {
        return;
    }
    const qreal r = std::max(1.8, m_canvas.width() * 0.0055);
    const QPointF at(rect.right() - r * 0.2, rect.top() + r * 0.2);
    p.save();
    p.setPen(QPen(palette().color(QPalette::Base), r * 0.7));
    p.setBrush(palette().color(QPalette::Highlight));
    p.drawEllipse(at, r, r);
    p.restore();
}

void GamepadDiagramWidget::DrawBody(QPainter& p) const {
    const QRectF c = m_canvas;
    const auto px = [&](qreal x, qreal y) {
        return QPointF(c.x() + x * c.width(), c.y() + y * c.height());
    };
    // Mirrored about the centre line, so the two halves cannot drift apart.
    const auto mx = [&](qreal x, qreal y) {
        return QPointF(c.x() + (1.0 - x) * c.width(), c.y() + y * c.height());
    };

    // One closed outline rather than a slab with two grips unioned onto it
    // the union left a visible seam where the shapes met.
    QPainterPath body;
    body.moveTo(px(0.500, 0.148));
    // top edge and right shoulder of the shell
    body.cubicTo(px(0.660, 0.148), px(0.800, 0.158), px(0.862, 0.196));
    body.cubicTo(px(0.906, 0.223), px(0.920, 0.300), px(0.922, 0.400));
    // down the outside and out into the right grip
    body.cubicTo(px(0.930, 0.508), px(0.934, 0.612), px(0.912, 0.712));
    body.cubicTo(px(0.888, 0.822), px(0.838, 0.936), px(0.772, 0.964));
    // round the tip and back up the inside
    body.cubicTo(px(0.714, 0.988), px(0.668, 0.938), px(0.654, 0.856));
    body.cubicTo(px(0.640, 0.778), px(0.628, 0.742), px(0.596, 0.702));
    // the waist, between the two grips
    body.cubicTo(px(0.566, 0.666), px(0.546, 0.656), px(0.500, 0.656));
    // and the mirror of all of it
    body.cubicTo(mx(0.546, 0.656), mx(0.566, 0.666), mx(0.596, 0.702));
    body.cubicTo(mx(0.628, 0.742), mx(0.640, 0.778), mx(0.654, 0.856));
    body.cubicTo(mx(0.668, 0.938), mx(0.714, 0.988), mx(0.772, 0.964));
    body.cubicTo(mx(0.838, 0.936), mx(0.888, 0.822), mx(0.912, 0.712));
    body.cubicTo(mx(0.934, 0.612), mx(0.930, 0.508), mx(0.922, 0.400));
    body.cubicTo(mx(0.918, 0.300), mx(0.906, 0.223), mx(0.862, 0.196));
    body.cubicTo(mx(0.800, 0.158), mx(0.660, 0.148), mx(0.500, 0.148));
    body.closeSubpath();

    const QColor window = palette().color(QPalette::Window);
    const QColor text = palette().color(QPalette::WindowText);
    const QColor shell = Mix(window, text, 0.13);

    p.save();
    p.setPen(QPen(WithAlpha(text, 90), std::max(1.0, c.width() * 0.0035)));
    p.setBrush(shell);
    p.drawPath(body);

    // A slightly lighter face plate, so the shell has some depth instead of
    // reading as one flat cut-out.
    QPainterPath face;
    const QRectF plate(px(0.152, 0.186), px(0.848, 0.648));
    face.addRoundedRect(plate, plate.height() * 0.30, plate.height() * 0.30);
    p.setPen(Qt::NoPen);
    p.setBrush(Mix(shell, window, 0.55));
    p.drawPath(face);
    p.restore();
}

void GamepadDiagramWidget::DrawTriggersAndShoulders(QPainter& p) const {
    for (const auto* name : {"l2", "r2", "l1", "r1"}) {
        const QRectF* rect = RectFor(name);
        if (rect == nullptr) {
            continue;
        }
        const Look look = LookFor(name);
        const qreal radius = m_canvas.height() * 0.026;
        p.setPen(QPen(look.pen, std::max(1.0, m_canvas.width() * 0.003)));
        p.setBrush(look.fill);
        p.drawRoundedRect(*rect, radius, radius);
        DrawBound(p, *rect, look);
    }
}

void GamepadDiagramWidget::DrawTouchpad(QPainter& p) const {
    const QRectF* left = RectFor("touchpad_left");
    const QRectF* right = RectFor("touchpad_right");
    if (left == nullptr || right == nullptr) {
        return;
    }
    const QRectF whole = left->united(*right);
    const qreal radius = whole.height() * 0.16;

    // One surface, drawn once, with the three zones marked on it -- that is
    // what the guest sees, and drawing three buttons would suggest otherwise.
    p.save();
    p.setPen(QPen(WithAlpha(palette().color(QPalette::WindowText), 150),
                  std::max(1.0, m_canvas.width() * 0.003)));
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(whole, radius, radius);

    QPainterPath clip;
    clip.addRoundedRect(whole, radius, radius);
    p.setClipPath(clip);
    for (const auto* name : {"touchpad_left", "touchpad_center", "touchpad_right"}) {
        const QRectF* zone = RectFor(name);
        if (zone == nullptr) {
            continue;
        }
        const Look look = LookFor(name);
        const bool active =
            m_highlighted.toStdString() == name || m_hovered == name || look.pressed;
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(look.fill);
            p.drawRect(*zone);
        }
    }
    p.setClipping(false);

    // Hairlines between the zones.
    p.setPen(QPen(WithAlpha(palette().color(QPalette::WindowText), 38), 1.0));
    for (const auto* name : {"touchpad_center", "touchpad_right"}) {
        if (const QRectF* zone = RectFor(name)) {
            p.drawLine(QPointF(zone->left(), whole.top() + whole.height() * 0.30),
                       QPointF(zone->left(), whole.bottom() - whole.height() * 0.30));
        }
    }
    p.restore();

    for (const auto* name : {"touchpad_left", "touchpad_center", "touchpad_right"}) {
        if (const QRectF* zone = RectFor(name)) {
            DrawBound(p, zone->adjusted(0, whole.height() * 0.12, 0, 0), LookFor(name));
        }
    }
}

void GamepadDiagramWidget::DrawDpad(QPainter& p) const {
    for (const auto* name : {"pad_up", "pad_down", "pad_left", "pad_right"}) {
        const QRectF* rect = RectFor(name);
        if (rect == nullptr) {
            continue;
        }
        const Look look = LookFor(name);
        const qreal radius = std::min(rect->width(), rect->height()) * 0.18;
        p.setPen(QPen(look.pen, std::max(1.0, m_canvas.width() * 0.003)));
        p.setBrush(look.fill);
        p.drawRoundedRect(*rect, radius, radius);

        // A small arrow, so the four arms are not four identical pills.
        const QRectF g = rect->adjusted(rect->width() * 0.30, rect->height() * 0.30,
                                        -rect->width() * 0.30, -rect->height() * 0.30);
        QPolygonF arrow;
        const std::string_view id(name);
        if (id == "pad_up") {
            arrow << QPointF(g.center().x(), g.top()) << g.bottomLeft() << g.bottomRight();
        } else if (id == "pad_down") {
            arrow << QPointF(g.center().x(), g.bottom()) << g.topLeft() << g.topRight();
        } else if (id == "pad_left") {
            arrow << QPointF(g.left(), g.center().y()) << g.topRight() << g.bottomRight();
        } else {
            arrow << QPointF(g.right(), g.center().y()) << g.topLeft() << g.bottomLeft();
        }
        p.setPen(Qt::NoPen);
        p.setBrush(WithAlpha(palette().color(QPalette::WindowText), 120));
        p.drawPolygon(arrow);

        DrawBound(p, *rect, look);
    }
}

void GamepadDiagramWidget::DrawFaceButtons(QPainter& p) const {
    for (const auto* name : {"triangle", "circle", "cross", "square"}) {
        const QRectF* rect = RectFor(name);
        if (rect == nullptr) {
            continue;
        }
        const Look look = LookFor(name);
        p.setPen(QPen(look.pen, std::max(1.0, m_canvas.width() * 0.0035)));
        p.setBrush(look.fill);
        p.drawEllipse(*rect);

        // Plain single-colour outlines. No per-button colour coding: the
        // shapes are the config file's vocabulary, the colours are somebody's
        // brand.
        const QRectF g = rect->adjusted(rect->width() * 0.30, rect->height() * 0.30,
                                        -rect->width() * 0.30, -rect->height() * 0.30);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(WithAlpha(palette().color(QPalette::WindowText), 170),
                      std::max(1.0, m_canvas.width() * 0.004)));
        const std::string_view id(name);
        if (id == "triangle") {
            QPolygonF tri;
            tri << QPointF(g.center().x(), g.top()) << g.bottomLeft() << g.bottomRight();
            p.drawPolygon(tri);
        } else if (id == "circle") {
            p.drawEllipse(g);
        } else if (id == "cross") {
            p.drawLine(g.topLeft(), g.bottomRight());
            p.drawLine(g.topRight(), g.bottomLeft());
        } else {
            p.drawRect(g);
        }

        DrawBound(p, *rect, look);
    }
}

void GamepadDiagramWidget::DrawSticks(QPainter& p) const {
    for (const auto* name : {"l3", "r3"}) {
        const QRectF* rect = RectFor(name);
        if (rect == nullptr) {
            continue;
        }
        const Look look = LookFor(name);
        const QColor text = palette().color(QPalette::WindowText);

        // A well, then the cap sitting in it ,two circles read as a stick
        // where one reads as a button.
        p.setPen(QPen(WithAlpha(text, 70), std::max(1.0, m_canvas.width() * 0.003)));
        p.setBrush(Mix(palette().color(QPalette::Window), text, 0.22));
        p.drawEllipse(*rect);

        const QRectF cap = rect->adjusted(rect->width() * 0.14, rect->height() * 0.14,
                                          -rect->width() * 0.14, -rect->height() * 0.14);
        p.setPen(QPen(look.pen, std::max(1.0, m_canvas.width() * 0.0035)));
        p.setBrush(look.fill);
        p.drawEllipse(cap);

        // The dished top.
        p.setPen(Qt::NoPen);
        p.setBrush(WithAlpha(text, 45));
        const QRectF dish = cap.adjusted(cap.width() * 0.26, cap.height() * 0.26,
                                         -cap.width() * 0.26, -cap.height() * 0.26);
        p.drawEllipse(dish);

        DrawBound(p, *rect, look);
    }
}

void GamepadDiagramWidget::DrawCentreButtons(QPainter& p) const {
    const QRectF* options = RectFor("options");
    if (options == nullptr) {
        return;
    }
    const Look look = LookFor("options");
    const qreal radius = options->height() * 0.35;
    p.setPen(QPen(look.pen, std::max(1.0, m_canvas.width() * 0.003)));
    p.setBrush(look.fill);
    p.drawRoundedRect(*options, radius, radius);
    DrawBound(p, *options, look);

    const QRectF mirror(m_canvas.x() + (1.0 - 0.632 - 0.036) * m_canvas.width(), options->y(),
                        options->width(), options->height());
    p.setPen(QPen(WithAlpha(palette().color(QPalette::WindowText), 55),
                  std::max(1.0, m_canvas.width() * 0.003)));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(mirror, radius, radius);
}

void GamepadDiagramWidget::paintEvent(QPaintEvent*) {
    if (m_canvas.isEmpty()) {
        RebuildRegions();
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    DrawBody(p);
    DrawTriggersAndShoulders(p);
    DrawTouchpad(p);
    DrawCentreButtons(p);
    DrawDpad(p);
    DrawFaceButtons(p);
    DrawSticks(p);
}
