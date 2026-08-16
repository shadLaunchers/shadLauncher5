// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
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

#include "common/types.h"
#include "core/file_format/ucp.h"
#include "gui_settings.h"

struct TrophyGameInfo {
    QString name;
    QString trophyPath;
    QString gameUcpPath;
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
    bool ExtractUcpTrophies(const std::filesystem::path& gamePath,
                            const std::filesystem::path& outputPath, const std::string& npCommId,
                            int index);
    bool userResizedWindow_ = false;
    bool programmaticResize_ = false;
    bool initialSizeApplied_ = false;

    QTabWidget* tabWidget = nullptr;
    QStringList headers;
    QString gameUcpPath_;
    QString currentGameName_;
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

    std::shared_ptr<GUISettings> m_gui_settings;

protected:
    void resizeEvent(QResizeEvent* event) override;
};
