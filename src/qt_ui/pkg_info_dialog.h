// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>
#include <QDialog>
#include <QImage>
#include <QString>
#include "core/file_format/ps5_pkg.h"

class QLabel;
class QPlainTextEdit;
class QGridLayout;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

class PkgInfoDialog : public QDialog {
    Q_OBJECT
public:
    explicit PkgInfoDialog(const QString& path = QString(), QWidget* parent = nullptr);
    static QString PickPackage(QWidget* parent);

    void Load(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpen();
    void onSaveEntry();
    void onCopySummary();
    void onEntrySelectionChanged();
    void onSiSelectionChanged();
    void onSaveSiMember();

private:
    void populate();
    void populateOverview();
    void populateEntries();
    void populateArtwork();
    void populateSegments();
    void populateSi();
    QWidget* buildEntriesTab();
    QWidget* buildArtworkTab();
    QWidget* buildSegmentsTab();
    QWidget* buildSiTab();
    void saveImage(int index);
    QTreeWidgetItem* addSection(const QString& label);
    void addRow(QTreeWidgetItem* section, const QString& key, const QString& value);
    QString statusText() const;
    QString summaryText() const;

    PS5Pkg::PkgInfo m_info;
    QString m_path;

    QLabel* m_icon = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QLabel* m_status = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTreeWidget* m_overview = nullptr;
    QTableWidget* m_entries = nullptr;
    QPlainTextEdit* m_param = nullptr;
    QWidget* m_artwork_page = nullptr;
    QGridLayout* m_artwork_grid = nullptr;
    QLabel* m_artwork_empty = nullptr;
    QTableWidget* m_segments = nullptr;
    QLabel* m_segments_note = nullptr;
    QTableWidget* m_si = nullptr;
    QPlainTextEdit* m_si_preview = nullptr;
    QPushButton* m_save_si = nullptr;

    struct ArtworkImage {
        QString name;   // "icon0"
        QString source; // entry it came from, e.g. "icon0.dds"
        QImage image;
    };
    std::vector<ArtworkImage> m_artwork;
    QPushButton* m_save_entry = nullptr;
    QPushButton* m_copy = nullptr;
};
