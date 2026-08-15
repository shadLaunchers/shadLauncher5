// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <QCheckBox>
#include <QDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QtWidgets>
#include "sfo_key_map.h"
#include "sfo_model.h"

class SFOViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit SFOViewerDialog(QWidget* parent = nullptr, const QString& sfoPath = QString());
    ~SFOViewerDialog() override;

private:
    void setupUi();
    bool loadSFO(const QString& path);
    void applyFilter(const QString& text);
    QMap<QString, QString> extractLocalizedTitles(const std::vector<SFOEntry>& entries);
    QString languageNameFromProsperoKey(const QString& key) {
        static const QMap<int, QString> langMap = {{0, "Japanese"},
                                                   {1, "English (US)"},
                                                   {2, "French"},
                                                   {3, "Spanish"},
                                                   {4, "German"},
                                                   {5, "Italian"},
                                                   {6, "Dutch"},
                                                   {7, "Portuguese (PT)"},
                                                   {8, "Russian"},
                                                   {9, "Korean"},
                                                   {10, "Chinese (Traditional)"},
                                                   {11, "Chinese (Simplified)"},
                                                   {12, "Finnish"},
                                                   {13, "Swedish"},
                                                   {14, "Danish"},
                                                   {15, "Norwegian"},
                                                   {16, "Polish"},
                                                   {17, "Portuguese (BR)"},
                                                   {18, "English (UK)"},
                                                   {19, "Turkish"},
                                                   {20, "Spanish (Latin America)"},
                                                   {21, "Arabic"},
                                                   {22, "French (Canada)"},
                                                   {23, "Czech"},
                                                   {24, "Hungarian"},
                                                   {25, "Greek"},
                                                   {26, "Romanian"},
                                                   {27, "Thai"},
                                                   {28, "Vietnamese"},
                                                   {29, "Indonesian"},
                                                   {30, "Ukrainian"}};

        bool ok = false;
        int idx = key.toInt(&ok);
        if (ok) {
            return langMap.value(idx, key); // mapped numeric key
        }
        return key; // fallback for non-numeric suffix
    }

private slots:
    void onReload();
    void onExport();
    void onOpenFolder();
    void onCopyValue();
    void onSearchTextChanged(const QString& text);
    void onRegexToggled(bool);

private:
    QString m_sfoPath;
    QFileInfo m_sfoInfo;

    SFOModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;

    QLineEdit* m_searchEdit = nullptr;
    QCheckBox* m_regexCheck = nullptr;
    QTableView* m_tableView = nullptr;

    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_titleIdLabel = nullptr;
    QLabel* m_versionLabel = nullptr;

    QGroupBox* m_localizedGroup = nullptr;
    QListWidget* m_localizedList = nullptr;
};
