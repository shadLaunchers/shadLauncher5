// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <QDialog>

#include "core/file_format/ps5_pkg.h"

class EmulatorSettingsImpl;
class GUISettings;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

class PkgInstallDialog final : public QDialog {
    Q_OBJECT

public:
    PkgInstallDialog(std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                     std::shared_ptr<GUISettings> gui_settings,
                     std::vector<PS5Pkg::PkgInfo> packages, QWidget* parent = nullptr);

    [[nodiscard]] std::vector<PS5Pkg::PkgInfo> SelectedPackages() const;
    [[nodiscard]] std::filesystem::path InstallDirectory() const {
        return m_install_dir;
    }

    static bool InstallPackages(QWidget* parent, std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                                std::shared_ptr<GUISettings> gui_settings,
                                const std::vector<std::filesystem::path>& files);
    [[nodiscard]] static std::vector<std::filesystem::path> PickPackages(QWidget* parent);
    [[nodiscard]] static std::vector<PS5Pkg::PkgInfo> ReadPackages(
        const std::vector<std::filesystem::path>& files);

private:
    void populate();
    void onBrowse();
    void onDetails();
    void setInstallDirectory(const QString& dir);
    void updateState();
    [[nodiscard]] bool selectionIsAddOnOnly() const;

    std::shared_ptr<EmulatorSettingsImpl> m_emu_settings;
    std::shared_ptr<GUISettings> m_gui_settings;
    std::vector<PS5Pkg::PkgInfo> m_packages;
    std::filesystem::path m_install_dir;

    QTableWidget* m_table = nullptr;
    QComboBox* m_dir_combo = nullptr;
    QPushButton* m_browse = nullptr;
    QPushButton* m_details = nullptr;
    QPushButton* m_install = nullptr;
    QLabel* m_dir_note = nullptr;
};
