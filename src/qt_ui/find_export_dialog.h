// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>
#include <QAbstractTableModel>
#include <QDialog>
#include <QString>
#include "core/analysis/export_index.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QTableView;
class QTimer;

class ExportHitsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    using QAbstractTableModel::QAbstractTableModel;
    void SetHits(std::vector<Core::Analysis::ExportHit> hits);
    const Core::Analysis::ExportHit& HitAt(int row) const {
        return m_hits[static_cast<size_t>(row)];
    }
    void RefreshNames();

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<Core::Analysis::ExportHit> m_hits;
};

class FindExportDialog : public QDialog {
    Q_OBJECT
public:
    FindExportDialog(const QString& title, std::filesystem::path game_root,
                     std::filesystem::path sys_modules_dir, const QString& initial_query,
                     bool include_sys_modules, QWidget* parent = nullptr);
    ~FindExportDialog() override;

private slots:
    void runSearch();
    void onResultActivated(const QModelIndex& index);
    void showModules();

private:
    void startGameIndex();
    void startSysIndex();
    void updateStatus();
    void openSourceAt(const Core::Analysis::ExportHit& hit);

    QString m_title;
    std::filesystem::path m_gameRoot;
    std::filesystem::path m_sysDir;

    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<const Core::Analysis::ExportIndex> m_gameIndex;
    std::shared_ptr<const Core::Analysis::ExportIndex> m_sysIndex;
    bool m_gameIndexing = false;
    bool m_sysIndexing = false;
    QString m_progressText;

    QLineEdit* m_query = nullptr;
    QCheckBox* m_includeSys = nullptr;
    QLabel* m_verdict = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    QTableView* m_view = nullptr;
    ExportHitsModel* m_model = nullptr;
    QTimer* m_searchTimer = nullptr;
};
