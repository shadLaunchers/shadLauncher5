// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <QCheckBox>
#include <QDockWidget>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <cmrc/cmrc.hpp>
#include <nlohmann/json.hpp>

#include <game_info.h>
#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/file_format/npbind.h"
#include "core/file_sys/game_backend.h"
#include "core/user_settings.h"
#include "trophy_viewer.h"

bool TrophyViewer::ExtractUcpTrophies(const std::filesystem::path& gamePath,
                                      const std::filesystem::path& outputPath,
                                      const std::string& npCommId, int index) {
    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : Core::FileSys::ListGameDir(gamePath, Core::FileSys::TrophyRelDir)) {
        if (entry.is_directory || !UCP::IsContainerFileName(entry.name)) {
            continue;
        }
        const std::string rel = std::string(Core::FileSys::TrophyRelDir) + "/" + entry.name;
        if (const auto resolved = Core::FileSys::ResolveGameFilePath(gamePath, rel)) {
            candidates.push_back(*resolved);
        }
    }
    // ListDir order is backend-defined, so sort for a stable index fallback.
    std::sort(candidates.begin(), candidates.end());

    const auto ucpPath = UCP::SelectContainerFor(candidates, npCommId, index);
    if (!ucpPath) {
        return false;
    }

    UCP ucp;
    if (!ucp.Open(*ucpPath)) {
        LOG_ERROR(Loader, "Failed to open UCP trophy container: {}", ucpPath->string());
        return false;
    }

    return ucp.ExtractTrophyFiles(outputPath);
}

namespace fs = std::filesystem;

CMRC_DECLARE(res);

// true: European format; false: American format
bool useEuropeanDateFormat = true;

void TrophyViewer::resizeEvent(QResizeEvent* event) {
    if (!programmaticResize_) {
        userResizedWindow_ = true;
    }
    QMainWindow::resizeEvent(event);
}

void TrophyViewer::updateTrophyInfo() {
    int total = 0;
    int unlocked = 0;

    // Cycles through each tab (table) of the QTabWidget
    for (int i = 0; i < tabWidget->count(); i++) {
        QTableWidget* table = qobject_cast<QTableWidget*>(tabWidget->widget(i));
        if (table) {
            total += table->rowCount();
            for (int row = 0; row < table->rowCount(); ++row) {
                QString cellText;
                // The "Unlocked" column can be a widget or a simple item
                QWidget* widget = table->cellWidget(row, 0);
                if (widget) {
                    // Looks for the QLabel inside the widget (as defined in SetTableItem)
                    QLabel* label = widget->findChild<QLabel*>();
                    if (label) {
                        cellText = label->text();
                    }
                } else {
                    QTableWidgetItem* item = table->item(row, 0);
                    if (item) {
                        cellText = item->text();
                    }
                }
                if (cellText == "unlocked")
                    unlocked++;
            }
        }
    }
    int progress = (total > 0) ? (unlocked * 100 / total) : 0;
    trophyInfoLabel->setText(
        QString(tr("Progress") + ": %1% (%2/%3)").arg(progress).arg(unlocked).arg(total));
}

void TrophyViewer::updateTableFilters() {
    bool showEarned = showEarnedCheck->isChecked();
    bool showNotEarned = showNotEarnedCheck->isChecked();
    bool showHidden = showHiddenCheck->isChecked();

    // Cycles through each tab of the QTabWidget
    for (int i = 0; i < tabWidget->count(); ++i) {
        QTableWidget* table = qobject_cast<QTableWidget*>(tabWidget->widget(i));
        if (!table)
            continue;
        for (int row = 0; row < table->rowCount(); ++row) {
            QString unlockedText;
            // Gets the text of the "Unlocked" column (index 0)
            QWidget* widget = table->cellWidget(row, 0);
            if (widget) {
                QLabel* label = widget->findChild<QLabel*>();
                if (label)
                    unlockedText = label->text();
            } else {
                QTableWidgetItem* item = table->item(row, 0);
                if (item)
                    unlockedText = item->text();
            }

            QString hiddenText;
            // Gets the text of the "Hidden" column (index 7)
            QWidget* hiddenWidget = table->cellWidget(row, 7);
            if (hiddenWidget) {
                QLabel* label = hiddenWidget->findChild<QLabel*>();
                if (label)
                    hiddenText = label->text();
            } else {
                QTableWidgetItem* item = table->item(row, 7);
                if (item)
                    hiddenText = item->text();
            }

            bool visible = true;
            if (unlockedText == "unlocked" && !showEarned)
                visible = false;
            if (unlockedText == "locked" && !showNotEarned)
                visible = false;
            if (hiddenText.toLower() == "yes" && !showHidden)
                visible = false;

            table->setRowHidden(row, !visible);
        }
    }
}

TrophyViewer::TrophyViewer(std::shared_ptr<GUISettings> gui_settings, QString trophyPath,
                           QString gameUcpPath, QString gameName,
                           const QVector<TrophyGameInfo>& allTrophyGames)
    : QMainWindow(), allTrophyGames_(allTrophyGames), currentGameName_(gameName),
      m_gui_settings(std::move(gui_settings)) {
    this->setWindowTitle(tr("Trophy Viewer") + " - " + currentGameName_);
    this->setAttribute(Qt::WA_DeleteOnClose);
    tabWidget = new QTabWidget(this);

    gameUcpPath_ = gameUcpPath;
    std::filesystem::path npbindPath =
        Common::FS::PathFromQString(gameUcpPath) / "sce_sys" / "trophy2" / "npbind.dat";
    if (const auto resolved = Core::FileSys::ResolveGameFilePath(
            Common::FS::PathFromQString(gameUcpPath), "sce_sys/trophy2/npbind.dat")) {
        npbindPath = *resolved;
    }

    NPBindFile npbind;
    if (!npbind.Load(npbindPath.string())) {
        LOG_WARNING(Common_Filesystem, "Failed to load npbind.dat file");
    } else {
        npCommIds = npbind.GetNpCommIds();
        if (npCommIds.empty()) {
            LOG_WARNING(Common_Filesystem, "No NPComm IDs found in npbind.dat");
        }
    }

    auto lan = m_gui_settings->GetValue(GUI::localization_language).toString();
    if (lan == "en_US" || lan == "zh_CN" || lan == "zh_TW" || lan == "ja_JP" || lan == "ko_KR" ||
        lan == "lt_LT" || lan == "nb_NO" || lan == "nl_NL") {
        useEuropeanDateFormat = false;
    }

    headers << "Unlocked"
            << "Trophy"
            << "Name"
            << "Description"
            << "Time Unlocked"
            << "Type"
            << "ID"
            << "Hidden"
            << "PID";

    std::string defaultUsername = UserSettings.GetUserManager().GetDefaultUser().user_name;
    PopulateTrophyWidget(trophyPath, QString::fromStdString(defaultUsername));

    trophyInfoDock = new QDockWidget("", this);
    QWidget* dockWidget = new QWidget(trophyInfoDock);
    QVBoxLayout* dockLayout = new QVBoxLayout(dockWidget);
    dockLayout->setAlignment(Qt::AlignTop);

    // ComboBox for game selection
    if (!allTrophyGames_.isEmpty()) {
        QLabel* userSelectionLabel = new QLabel(tr("Select User:"), dockWidget);
        dockLayout->addWidget(userSelectionLabel);

        userSelectionComboBox = new QComboBox(dockWidget);
        for (const auto& User : UserSettings.GetUserManager().GetAllUsers()) {
            userSelectionComboBox->addItem(QString::fromStdString(User.user_name));
        }

        // Select default user in ComboBox
        int defaultIndex = userSelectionComboBox->findText(QString::fromStdString(defaultUsername));
        if (defaultIndex >= 0) {
            userSelectionComboBox->setCurrentIndex(defaultIndex);
        }

        dockLayout->addWidget(userSelectionComboBox);
        QLabel* gameSelectionLabel = new QLabel(tr("Select Game:"), dockWidget);
        dockLayout->addWidget(gameSelectionLabel);

        gameSelectionComboBox = new QComboBox(dockWidget);
        for (const auto& game : allTrophyGames_) {
            gameSelectionComboBox->addItem(game.name);
        }

        // Select current game in ComboBox
        if (!currentGameName_.isEmpty()) {
            int index = gameSelectionComboBox->findText(currentGameName_);
            if (index >= 0) {
                gameSelectionComboBox->setCurrentIndex(index);
            }
        }

        dockLayout->addWidget(gameSelectionComboBox);

        connect(gameSelectionComboBox, &QComboBox::currentIndexChanged, this, [this]() {
            SelectionChanged(gameSelectionComboBox->currentIndex(),
                             userSelectionComboBox->currentText());
        });

        connect(userSelectionComboBox, &QComboBox::currentIndexChanged, this, [this]() {
            SelectionChanged(gameSelectionComboBox->currentIndex(),
                             userSelectionComboBox->currentText());
        });

        QFrame* line = new QFrame(dockWidget);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        dockLayout->addWidget(line);
    }

    trophyInfoLabel = new QLabel(tr("Progress") + ": 0% (0/0)", dockWidget);
    trophyInfoLabel->setStyleSheet(
        "font-weight: bold; font-size: 16px; color: white; background: #333; padding: 5px;");
    dockLayout->addWidget(trophyInfoLabel);

    // Creates QCheckBox to filter trophies
    showEarnedCheck = new QCheckBox(tr("Show Earned Trophies"), dockWidget);
    showNotEarnedCheck = new QCheckBox(tr("Show Not Earned Trophies"), dockWidget);
    showHiddenCheck = new QCheckBox(tr("Show Hidden Trophies"), dockWidget);

    // Defines the initial states (all checked)
    showEarnedCheck->setChecked(true);
    showNotEarnedCheck->setChecked(true);
    showHiddenCheck->setChecked(false);

    // Adds checkboxes to the layout
    dockLayout->addWidget(showEarnedCheck);
    dockLayout->addWidget(showNotEarnedCheck);
    dockLayout->addWidget(showHiddenCheck);

    dockWidget->setLayout(dockLayout);
    trophyInfoDock->setWidget(dockWidget);

    // Adds the dock to the left area
    this->addDockWidget(Qt::LeftDockWidgetArea, trophyInfoDock);

    expandButton = new QPushButton(">>", this);
    expandButton->setGeometry(80, 0, 27, 27);
    expandButton->hide();

    connect(expandButton, &QPushButton::clicked, this, [this] {
        trophyInfoDock->setVisible(true);
        expandButton->hide();
    });

    // Connects checkbox signals to update trophy display
    connect(showEarnedCheck, &QCheckBox::checkStateChanged, this,
            &TrophyViewer::updateTableFilters);
    connect(showNotEarnedCheck, &QCheckBox::checkStateChanged, this,
            &TrophyViewer::updateTableFilters);
    connect(showHiddenCheck, &QCheckBox::checkStateChanged, this,
            &TrophyViewer::updateTableFilters);

    updateTrophyInfo();
    updateTableFilters();

    connect(trophyInfoDock, &QDockWidget::topLevelChanged, this, [this] {
        if (!trophyInfoDock->isVisible()) {
            expandButton->show();
        }
    });

    connect(trophyInfoDock, &QDockWidget::visibilityChanged, this, [this] {
        if (!trophyInfoDock->isVisible()) {
            expandButton->show();
        } else {
            expandButton->hide();
        }
    });
}

void TrophyViewer::SelectionChanged(int gameIndex, QString user) {
    if (gameIndex < 0 || gameIndex >= allTrophyGames_.size()) {
        return;
    }

    while (tabWidget->count() > 0) {
        QWidget* widget = tabWidget->widget(0);
        tabWidget->removeTab(0);
        delete widget;
    }

    const TrophyGameInfo& selectedGame = allTrophyGames_[gameIndex];
    currentGameName_ = selectedGame.name;
    gameUcpPath_ = selectedGame.gameUcpPath;

    this->setWindowTitle(tr("Trophy Viewer") + " - " + currentGameName_);

    std::filesystem::path npbindPath =
        Common::FS::PathFromQString(gameUcpPath_) / "sce_sys" / "trophy2" / "npbind.dat";

    NPBindFile npbind;
    if (!npbind.Load(npbindPath.string())) {
        LOG_WARNING(Common_Filesystem, "Failed to load npbind.dat file");
    } else {
        npCommIds = npbind.GetNpCommIds();
        if (npCommIds.empty()) {
            LOG_WARNING(Common_Filesystem, "No NPComm IDs found in npbind.dat");
        }
    }

    PopulateTrophyWidget(selectedGame.trophyPath, user);

    updateTrophyInfo();
    updateTableFilters();
}

void TrophyViewer::onDockClosed() {
    if (!trophyInfoDock->isVisible()) {
        reopenButton->setVisible(true);
    }
}

void TrophyViewer::reopenLeftDock() {
    trophyInfoDock->show();
    reopenButton->setVisible(false);
}

void TrophyViewer::PopulateTrophyWidget(QString title, QString user) {

    // Position within npbind.dat's list. Advanced at the top of the loop so
    // that skipping a set on error can't shift every later index.
    int index = -1;
    for (const auto& npCommId : npCommIds) {
        ++index;
        auto trophyFilesPath =
            Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "trophy" / npCommId;
        QString trophyDirQt;
        Common::FS::PathToQString(trophyDirQt, trophyFilesPath);

        const std::filesystem::path extractPath = Common::FS::PathFromQString(gameUcpPath_);
        QDir dir(trophyDirQt);
        if (!dir.exists()) {
            if (!ExtractUcpTrophies(extractPath, trophyFilesPath, npCommId, index)) {
                LOG_ERROR(Loader, "Couldn't extract trophies for {}", npCommId);
                continue;
            }
        }

        // Definitions plus display strings for the active console language.
        TrophySet set;
        if (!set.LoadFromDir(trophyFilesPath, EmulatorSettings.GetConsoleLanguage())) {
            LOG_ERROR(Loader, "Couldn't load trophy set for {}", npCommId);
            continue;
        }

        std::string userId = "1000";
        for (const auto& User : UserSettings.GetUserManager().GetAllUsers()) {
            if (User.user_name == user) {
                userId = std::to_string(User.user_id);
            }
        }

        // Per-user unlock state. A missing file just means nothing is earned
        // yet, so seed one rather than leaving the set unreadable.
        const auto user_trophy_file =
            EmulatorSettings.GetHomeDir() / userId / "trophy" / (npCommId + ".json");
        TrophyProgress progress;
        if (!progress.Load(user_trophy_file)) {
            LOG_WARNING(Loader, "Couldn't read trophy progress: {}", user_trophy_file.string());
        }
        if (!std::filesystem::exists(user_trophy_file)) {
            if (progress.np_comm_id.empty()) {
                progress.np_comm_id = npCommId;
            }
            progress.SeedFrom(set);
            progress.Save(user_trophy_file);
        }

        // Icons are addressed by trophy id ("trop0000.png"), not by directory
        // order, so a set with a gap in its numbering still lines up.
        const std::filesystem::path iconsDir = trophyFilesPath / "Icons";

        QStringList trpId;
        QStringList trpHidden;
        QStringList trpUnlocked;
        QStringList trpType;
        QStringList trpPid;
        QStringList trophyNames;
        QStringList trophyDetails;
        QStringList trpTimeUnlocked;
        std::vector<QImage> icons;

        for (const auto& def : set.trophies) {
            QString iconPath;
            Common::FS::PathToQString(iconPath, iconsDir / def.IconFileName());
            icons.push_back(QImage(iconPath).scaled(QSize(128, 128), Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation));

            trpId.append(QString::fromStdString(def.id));
            trpHidden.append(def.hidden ? "yes" : "no");
            trpType.append(QString::fromUtf8(TrophySet::GradeCode(def.grade).data(),
                                             qsizetype(TrophySet::GradeCode(def.grade).size())));
            trpPid.append(QString::fromStdString(def.platinum_id));
            trophyNames.append(QString::fromStdString(def.name));
            trophyDetails.append(QString::fromStdString(def.detail));

            const auto entry = progress.Get(def.id);
            trpUnlocked.append(entry.unlocked ? "unlocked" : "locked");
            if (entry.unlocked && entry.timestamp != 0) {
                const QDateTime dt = QDateTime::fromSecsSinceEpoch(entry.timestamp);
                const char* format =
                    useEuropeanDateFormat ? "dd/MM/yyyy HH:mm:ss" : "MM/dd/yyyy HH:mm:ss";
                trpTimeUnlocked.append(dt.toString(format));
            } else {
                trpTimeUnlocked.append("");
            }
        }

        const QString tabName =
            QString::fromStdString(set.title_name.empty() ? npCommId : set.title_name);

        QTableWidget* tableWidget = new QTableWidget(this);
        tableWidget->setShowGrid(false);
        tableWidget->setColumnCount(9);
        tableWidget->setHorizontalHeaderLabels(headers);
        tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        tableWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        tableWidget->horizontalHeader()->setStretchLastSection(false);
        tableWidget->verticalHeader()->setVisible(false);
        tableWidget->setRowCount(static_cast<int>(icons.size()));
        tableWidget->setSortingEnabled(true);
        tableWidget->setWordWrap(true);

        for (int row = 0; auto& icon : icons) {
            QTableWidgetItem* item = new QTableWidgetItem();
            item->setData(Qt::DecorationRole, icon);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            tableWidget->setItem(row, 1, item);

            const auto grade = TrophySet::GradeFromCode(trpType[row].toStdString());
            const auto grade_icon = TrophySet::GradeIconFileName(grade);
            const std::string filename(grade_icon);
            QTableWidgetItem* typeitem = new QTableWidgetItem();

            const auto CustomTrophy_Dir =
                Common::FS::GetUserPath(Common::FS::PathType::CustomTrophy);
            std::string customPath;

            if (fs::exists(CustomTrophy_Dir / filename)) {
                customPath = (CustomTrophy_Dir / filename).string();
            }

            std::vector<char> imgdata;

            if (!customPath.empty()) {
                std::ifstream file(customPath, std::ios::binary);
                if (file) {
                    imgdata = std::vector<char>(std::istreambuf_iterator<char>(file),
                                                std::istreambuf_iterator<char>());
                }
            } else {
                auto resource = cmrc::res::get_filesystem();
                std::string resourceString = "src/images/" + filename;
                auto file = resource.open(resourceString);
                imgdata = std::vector<char>(file.begin(), file.end());
            }

            QImage type_icon = QImage::fromData(imgdata).scaled(
                QSize(100, 100), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            typeitem->setData(Qt::DecorationRole, type_icon);
            typeitem->setFlags(typeitem->flags() & ~Qt::ItemIsEditable);
            tableWidget->setItem(row, 5, typeitem);

            std::string detailString = trophyDetails[row].toStdString();
            std::size_t newline_pos = 0;
            while ((newline_pos = detailString.find("\n", newline_pos)) != std::string::npos) {
                detailString.replace(newline_pos, 1, " ");
                ++newline_pos;
            }

            if (!trophyNames.isEmpty() && !trophyDetails.isEmpty()) {
                SetTableItem(tableWidget, row, 0, trpUnlocked[row]);
                SetTableItem(tableWidget, row, 2, trophyNames[row]);
                SetTableItem(tableWidget, row, 3, QString::fromStdString(detailString));
                SetTableItem(tableWidget, row, 4, trpTimeUnlocked[row]);
                SetTableItem(tableWidget, row, 6, trpId[row]);
                SetTableItem(tableWidget, row, 7, trpHidden[row]);
                SetTableItem(tableWidget, row, 8, trpPid[row]);
            }

            tableWidget->verticalHeader()->resizeSection(row, icon.height());
            row++;
        }

        auto header = tableWidget->horizontalHeader();
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::Stretch);
        header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(6, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(7, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(8, QHeaderView::ResizeToContents);

        tableWidget->resizeColumnsToContents();
        tableWidget->resizeRowsToContents();

        const int hardMinDesc = 300;
        int currentDesc = tableWidget->columnWidth(3);
        if (currentDesc < hardMinDesc) {
            tableWidget->setColumnWidth(3, hardMinDesc);
        }

        tabWidget->addTab(tableWidget, tabName);
    }

    this->setCentralWidget(tabWidget);

    if (!this->isMaximized() && !this->isFullScreen()) {
        if (!userResizedWindow_ && !initialSizeApplied_) {
            QScreen* screen = QGuiApplication::primaryScreen();
            QSize screenSize(1024, 768);
            if (screen) {
                screenSize = screen->availableGeometry().size();
            }
            programmaticResize_ = true;
            this->resize(screenSize.width() * 0.8, screenSize.height() * 0.8);
            programmaticResize_ = false;
            initialSizeApplied_ = true;
        }
    }
}

void TrophyViewer::SetTableItem(QTableWidget* parent, int row, int column, QString str) {
    QTableWidgetItem* item = new QTableWidgetItem(str);

    if (column != 1 && column != 2 && column != 3)
        item->setTextAlignment(Qt::AlignCenter);
    QFont f = parent->font();
    f.setPointSize(12);
    f.setBold(true);
    item->setFont(f);

    /* Theme theme = static_cast<Theme>(m_gui_settings->GetValue(gui::gen_theme).toInt());

    if (theme == Theme::Light) {
        item->setForeground(QBrush(Qt::black));
    } else {
        item->setForeground(QBrush(Qt::white));
    }
    */
    item->setForeground(QBrush(Qt::black));

    parent->setItem(row, column, item);
}
