// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QWidget>

class GamepadDiagramWidget : public QWidget {
    Q_OBJECT
public:
    explicit GamepadDiagramWidget(QWidget* parent = nullptr);

    void SetHighlightedOutput(const QString& outputName);
    void SetBoundOutputs(const QSet<QString>& outputNames);
    void SetPressedControl(const QString& name, bool pressed);
    void ClearPressed();
    [[nodiscard]] QSize sizeHint() const override {
        return QSize(400, 276);
    }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return QSize(200, 138);
    }

signals:
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

    [[nodiscard]] QRectF Canvas() const;
    void RebuildRegions();
    [[nodiscard]] const QRectF* RectFor(const std::string& output) const;
    [[nodiscard]] std::string OutputAt(const QPointF& pos) const;

    struct Look {
        QColor fill;
        QColor pen;
        bool bound = false;
        bool pressed = false;
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

    std::vector<Region> m_regions;
    QRectF m_canvas;
    QString m_highlighted;
    std::string m_hovered;
    QSet<QString> m_bound;
    QSet<QString> m_pressed_controls;
    QSet<QString> m_pressed;
};
