// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QDialog>
#include <QImage>
#include <QString>
#include "core/file_format/playgo.h"
#include "core/file_format/ps5_pkg.h"
#include "core/file_format/ps5_pkg_image.h"
#include "core/file_format/ps5_pkg_keys.h"
#include "core/file_format/ucp.h"
#include "core/file_format/uds.h"

class QLabel;
class QComboBox;
class QTemporaryDir;
class QLineEdit;
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
    ~PkgInfoDialog() override;
    static QString PickPackage(QWidget* parent);
    void Load(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpen();
    void onSaveEntry();
    void onCopySummary();
    void onExportReport();
    void onEntrySelectionChanged();
    void onOpenNpbind();
    void onOpenParamViewer();
    void onTrophyContainerChanged();
    void onTrophyLanguageChanged();
    void onSiSelectionChanged();
    void onSaveSiMember();
    void onVerifyKeys();
    void onSaveOuterFile();
    void onOuterSelectionChanged();

private:
    void populate();
    void populateOverview();
    void populateEntries();
    void populateArtwork();
    void populateSegments();
    void populateSi();
    void populateKeys();
    QWidget* buildEntriesTab();
    QWidget* buildArtworkTab();
    QWidget* buildSegmentsTab();
    QWidget* buildSiTab();
    QWidget* buildKeysTab();
    QWidget* buildTrophiesTab();
    QWidget* buildActivitiesTab();
    QWidget* buildPlayGoTab();
    void populatePlayGo();
    QWidget* buildFilesTab();
    void populateFiles();
    void onSaveGameFile();
    QWidget* buildParamTab();
    void populateTrophies();
    void populateActivities();
    void showTrophySet(int language_index);
    void extractContainer(const QString& ucp_path);
    // Writes `data` into this dialog's temporary folder; empty on failure.
    QString writeTempFile(const QString& name, const std::vector<u8>& data);
    void saveImage(int index);
    QTreeWidgetItem* addSection(const QString& label);
    void addRow(QTreeWidgetItem* section, const QString& key, const QString& value);
    QString statusText() const;
    // Kind shown to the user: a retail-signed package whose keys verified is a fake package.
    QString kindLabel() const;
    // Whether encrypted metadata can be decrypted with content-ID keys.
    bool contentIdKeys() const;
    bool keysMatched() const;
    void updateHeaderStatus();
    // Everything that depends on the key check: Overview, Entries and the decrypted tabs.
    void refreshDecryptedViews();
    QString summaryText() const;
    QByteArray reportJson() const;
    const PS5Pkg::CntEntry* selectedEntry() const;
    // Entry payload, decrypted when needed; shows a message and returns nothing on failure.
    std::optional<std::vector<u8>> entryData(const PS5Pkg::CntEntry& entry, const QString& title);

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
    QWidget* m_keys_page = nullptr;
    QLineEdit* m_passcode = nullptr;
    QPushButton* m_verify = nullptr;
    QLabel* m_key_result = nullptr;
    QTreeWidget* m_key_details = nullptr;
    QWidget* m_key_results = nullptr;
    QTableWidget* m_outer_files = nullptr;
    QPushButton* m_save_outer = nullptr;
    PS5Pkg::KeyCheck m_keys;

    QWidget* m_trophy_page = nullptr;
    QLabel* m_trophy_summary = nullptr;
    QComboBox* m_trophy_container = nullptr;
    QComboBox* m_trophy_lang = nullptr;
    QTableWidget* m_trophy_table = nullptr;
    QStringList m_trophy_ucps; // temporary copies, one per trophy container
    QString m_trophy_text;     // for Copy Summary
    TrophySet m_trophy_set;    // as last shown, for the report

    QWidget* m_uds_page = nullptr;
    QLabel* m_uds_summary = nullptr;
    QTreeWidget* m_uds_tree = nullptr;
    QString m_uds_ucp;
    QString m_uds_text;
    UdsDefinitions m_uds;

    QWidget* m_files_page = nullptr;
    QLabel* m_files_summary = nullptr;
    QTreeWidget* m_files_tree = nullptr;
    QPushButton* m_files_save = nullptr;
    std::unique_ptr<PS5Pkg::InnerImage> m_inner;

    QWidget* m_playgo_page = nullptr;
    QLabel* m_playgo_summary = nullptr;
    QTreeWidget* m_playgo_tree = nullptr;
    PlayGoInfo m_playgo;
    QString m_playgo_text;

    struct ArtworkImage {
        QString name;   // "icon0"
        QString source; // entry it came from, e.g. "icon0.dds"
        QImage image;
    };
    std::vector<ArtworkImage> m_artwork;
    QPushButton* m_save_entry = nullptr;
    QPushButton* m_open_npbind = nullptr;
    std::unique_ptr<QTemporaryDir> m_temp_dir; // decrypted files handed to other viewers
    QPushButton* m_copy = nullptr;
    QPushButton* m_export = nullptr;
};
