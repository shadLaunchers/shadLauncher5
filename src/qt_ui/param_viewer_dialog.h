// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <QCheckBox>
#include <QDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSortFilterProxyModel>
#include <QString>
#include <QTabWidget>
#include <QTableView>
#include "param_model.h"

// Shows the contents of a PS5 sce_sys/param.json: every field the document
// actually contains, plus the document itself.
class ParamViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ParamViewerDialog(QWidget* parent = nullptr, const QString& paramPath = QString());
    ~ParamViewerDialog() override;

private:
    void setupUi();
    bool loadParam(const QString& path);
    void applyFilter(const QString& text);

private slots:
    void onReload();
    void onExport();
    void onOpenFolder();
    void onCopyValue();
    void onSearchTextChanged(const QString& text);
    void onRegexToggled(bool);

private:
    QString m_paramPath;
    QFileInfo m_paramInfo;

    ParamModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;

    QTabWidget* m_tabs = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QCheckBox* m_regexCheck = nullptr;
    QTableView* m_tableView = nullptr;
    QPlainTextEdit* m_rawView = nullptr;

    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_titleIdLabel = nullptr;
    QLabel* m_contentIdLabel = nullptr;
    QLabel* m_versionLabel = nullptr;
    QLabel* m_categoryLabel = nullptr;

    QGroupBox* m_localizedGroup = nullptr;
    QListWidget* m_localizedList = nullptr;
};
