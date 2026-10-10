// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

class ModulePickerDialog : public QDialog {
    Q_OBJECT
public:
    ModulePickerDialog(const QString& title, std::filesystem::path game_root,
                       std::filesystem::path sys_modules_dir, bool include_sys_modules,
                       QWidget* parent = nullptr);

private slots:
    void applyFilter();
    void openSelected();
    void browseForFile();

private:
    struct Entry {
        QString display_path;
        bool from_sys_modules = false;
        std::string rel_path;
        std::filesystem::path host_path;
    };

    void startListing();
    void populate();

    QString m_title;
    std::filesystem::path m_gameRoot;
    std::filesystem::path m_sysDir;
    std::vector<Entry> m_entries;
    bool m_listing = false;

    QLineEdit* m_filter = nullptr;
    QCheckBox* m_includeSys = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
};
