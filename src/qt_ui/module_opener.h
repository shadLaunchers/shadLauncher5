// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <QString>

class QWidget;

struct ModuleLocation {
    QString display_path;
    std::filesystem::path game_root;
    std::string rel_path;
    std::filesystem::path host_path;
};
void OpenModuleInElfInfo(QWidget* parent, const ModuleLocation& location, const QString& game_title,
                         std::filesystem::path game_root, std::filesystem::path sys_modules_dir,
                         std::string select_export_nid = {});
