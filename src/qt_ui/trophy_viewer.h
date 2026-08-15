// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileInfoList>
#include <QGraphicsBlurEffect>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QPair>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVector>
#include <QXmlStreamReader>

#include "common/types.h"
#include "core/file_format/trp.h"
#include "core/file_format/ucp.h"
#include "gui_settings.h"

struct TrophyGameInfo {
    QString name;
    QString trophyPath;
    QString gameTrpPath;
};

class TrophyViewer : public QMainWindow {
    Q_OBJECT
public:
    explicit TrophyViewer(
        std::shared_ptr<GUISettings> gui_settings, QString trophyPath, QString gameTrpPath,
        QString gameName = "",
        const QVector<TrophyGameInfo>& allTrophyGames = QVector<TrophyGameInfo>());

    void updateTrophyInfo();
    void updateTableFilters();
    void onDockClosed();
    void reopenLeftDock();

private slots:
    void SelectionChanged(int gameIndex, QString user);

private:
    void PopulateTrophyWidget(QString title, QString user);
    void SetTableItem(QTableWidget* parent, int row, int column, QString str);
    std::filesystem::path GetTrpFilesPath(std::filesystem::path gamePath);

    // PS5 titles ship trophy data as a "Trophy00.ucp"/"uds00.ucp" container
    // (see core/file_format/ucp.h) instead of PS3/PS4/Vita's ".trp" files.
    // This locates one under gamePath/sce_sys/trophy2, extracts its PNG
    // icons as-is, and transcodes its tropconf.json/tropmeta_<locale>.json
    // definitions into the same TROPCONF.XML/TROP_XX.XML shape TRP::Extract
    // already produces, so the rest of this class (which only knows how to
    // read that XML shape) needs no changes to support either source.
    // Returns false (with the output directory left untouched) if no UCP
    // container is found, so callers can fall back to the legacy TRP path.
    bool ExtractUcpTrophies(const std::filesystem::path& gamePath,
                            const std::filesystem::path& outputPath);
    bool userResizedWindow_ = false;
    bool programmaticResize_ = false;
    bool initialSizeApplied_ = false;

    QTabWidget* tabWidget = nullptr;
    QStringList headers;
    QString gameTrpPath_;
    QString currentGameName_;
    TRP trp;
    QLabel* trophyInfoLabel;
    QCheckBox* showEarnedCheck;
    QCheckBox* showNotEarnedCheck;
    QCheckBox* showHiddenCheck;
    QComboBox* gameSelectionComboBox;
    QComboBox* userSelectionComboBox;
    QPushButton* expandButton;
    QDockWidget* trophyInfoDock;
    QPushButton* reopenButton;
    QVector<TrophyGameInfo> allTrophyGames_;
    std::vector<std::string> npCommIds;

    std::string GetTrpType(const QChar trp_) {
        switch (trp_.toLatin1()) {
        case 'B':
            return "bronze.png";
        case 'S':
            return "silver.png";
        case 'G':
            return "gold.png";
        case 'P':
            return "platinum.png";
        }
        return "Unknown";
    }
    std::shared_ptr<GUISettings> m_gui_settings;

protected:
    void resizeEvent(QResizeEvent* event) override;
};
