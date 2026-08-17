// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <QDialog>

#include "core/file_sys/game_backend.h"

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

// Lets the user browse the contents of a .zar archive as a folder tree and
// extract either the whole archive or just a hand-picked subset of files to
// a destination folder on disk.
class ZArchiveViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ZArchiveViewerDialog(std::filesystem::path archive_path, QWidget* parent = nullptr);
    ~ZArchiveViewerDialog() override;

private:
    void SetupUi();
    void PopulateRoot();
    void PopulateChildren(QTreeWidgetItem* parent_item, const std::string& rel_path);
    void EnsurePopulated(QTreeWidgetItem* item);
    void EnsurePopulatedRecursive(QTreeWidgetItem* item);
    void SetChildrenCheckState(QTreeWidgetItem* item, Qt::CheckState state);
    void UpdateAncestorCheckState(QTreeWidgetItem* item);
    void CollectCheckedFiles(QTreeWidgetItem* item, std::vector<std::string>& out) const;
    [[nodiscard]] std::vector<std::string> GetCheckedFiles() const;
    void UpdateStatusLabel();

    void OnItemExpanded(QTreeWidgetItem* item);
    void OnItemChanged(QTreeWidgetItem* item, int column);
    void OnSelectAll();
    void OnSelectNone();
    void OnExtractSelected();
    void OnExtractAll();

    // rel_paths empty => extract the entire archive.
    void RunExtraction(const std::vector<std::string>& rel_paths,
                       const std::filesystem::path& output_dir);

    std::filesystem::path m_archive_path;
    std::unique_ptr<Core::FileSys::IGameBackend> m_backend;

    QTreeWidget* m_tree = nullptr;
    QLabel* m_status_label = nullptr;
    bool m_updating_checks = false;
};
