// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QString>
#include "core/analysis/middleware.h"

class QTreeWidget;
class QTreeWidgetItem;

class TechInfoDialog : public QDialog {
    Q_OBJECT
public:
    TechInfoDialog(const QString& title, const QString& serial,
                   Core::Analysis::GameTechReport report, QWidget* parent = nullptr);

private slots:
    void onExportJson();
    void onCopySummary();

private:
    QWidget* buildOverviewTab();
    QWidget* buildModulesTab();
    QWidget* buildVersionsTab();
    QTreeWidgetItem* addSection(QTreeWidget* tree, const QString& label, int count = -1);
    void addDetection(QTreeWidgetItem* parent, const Core::Analysis::Detection& d,
                      bool show_confidence = false);
    QString summaryText() const;

    QString m_title;
    QString m_serial;
    Core::Analysis::GameTechReport m_report;
};
