// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Note: there are a few TODO here to take care
#include <algorithm>
#include <functional>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QtConcurrent>
#include <common/scm_rev.h>
#include <common/versions.h>
#include <fmt/core.h>

#include "about_dialog.h"
#include "background_music_player.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/emulator_state.h"
#include "core/file_sys/game_backend.h"
#include "game_list_exporter.h"
#include "game_list_frame.h"
#include "gui_settings.h"
#include "main_window.h"
#include "progress_dialog.h"
#include "qt_ui/check_update.h"
#include "settings_dialog.h"
#include "ui_main_window.h"
#include "user_manager_dialog.h"
#include "version.h"
#include "version_dialog.h"

MainWindow::MainWindow(std::shared_ptr<GUISettings> gui_settings,
                       std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                       std::shared_ptr<IpcClient> ipc_client, QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_gui_settings(gui_settings),
      m_emu_settings(std::move(emu_settings)), m_ipc_client(std::move(ipc_client)) {

    Q_INIT_RESOURCE(shadLauncher5);
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);

    m_ipc_client->gameClosedFunc = [this]() { onGameClosed(); };
    m_ipc_client->restartEmulatorFunc = [this]() { RestartEmulator(); };
    m_ipc_client->startGameFunc = [this]() { RunGame(); };
}

MainWindow::~MainWindow() {}

bool MainWindow::init() {
    ui->toolBar->setObjectName("mw_toolbar");
    ui->sizeSlider->setRange(0, GUI::game_list_max_slider_pos);
    ui->toolBar->addWidget(ui->sizeSliderContainer);
    ui->toolBar->addWidget(ui->mw_searchbar);
    createActions();
    createDockWindows();
    createConnects();

    m_toolbar_icon_color_label = new QLabel(this);
    m_toolbar_icon_color_label->setObjectName("toolbar_icon_color");
    m_toolbar_icon_color_label->hide();
    m_thumbnail_icon_color_label = new QLabel(this);
    m_thumbnail_icon_color_label->setObjectName("thumbnail_icon_color");
    m_thumbnail_icon_color_label->hide();
    CacheOriginalToolbarIcons();
    CacheOriginalMenuIcons();

    setMinimumSize(350, minimumSizeHint().height()); // seems fine on win 10

    auto isUnavailable = [](const std::string& value) {
        return value.empty() || value.find("NOTFOUND") != std::string::npos || value == "unknown" ||
               value == "HEAD";
    };

    std::string remote_host = Common::GetRemoteNameFromLink();
    if (isUnavailable(remote_host)) {
        remote_host.clear();
    }

    std::string window_title = "shadLauncher5";
    if (!remote_host.empty() && remote_host != "shadLaunchers") {
        window_title += fmt::format(" {}/v{}", remote_host, APP_VERSION);
    } else {
        window_title += fmt::format(" v{}", APP_VERSION);
    }

    if (!Common::g_is_release) {
        std::string branch(Common::g_scm_branch);
        if (!isUnavailable(branch)) {
            window_title += " " + branch;
        }
    }
    setWindowTitle(QString::fromStdString(window_title));

    Q_EMIT RequestGlobalStylesheetChange();
    configureGuiFromSettings();

    // Refresh gamelist last
    m_game_list_frame->Refresh(true);
    m_game_list_frame->CheckCompatibilityAtStartup();

    // Expandable spacer to push elements to the right (Version Manager)
    QWidget* expandingSpacer = new QWidget(this);
    expandingSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->toolBar->addWidget(expandingSpacer);
    QWidget* versionContainer = new QWidget(this);
    QVBoxLayout* versionLayout = new QVBoxLayout(versionContainer);
    versionLayout->setContentsMargins(0, 0, 0, 0);
    versionLayout->addWidget(ui->versionComboBox);
    versionLayout->addWidget(ui->versionManagerButton);
    ui->versionManagerButton->setText(tr("Version Manager"));
    ui->toolBar->addWidget(versionContainer);
    LoadVersionComboBox();
    show();

    // Check Update Emu
    if (m_gui_settings->GetValue(GUI::version_manager_checkOnStartup).toBool()) {
        auto versionDialog = new VersionDialog(m_gui_settings, this);
        versionDialog->checkUpdatePre(false);
    }

    // Check Update Gui
    if (m_gui_settings->GetValue(GUI::general_check_gui_updates).toBool()) {
        auto* checkUpdate = new CheckUpdate(m_gui_settings, false, this);
        checkUpdate->exec();
    }

    return true;
}

void MainWindow::createActions() {
    ui->exitAct->setShortcuts(QKeySequence::Quit);

    m_icon_size_act_group = new QActionGroup(this);
    m_icon_size_act_group->addAction(ui->setIconSizeTinyAct);
    m_icon_size_act_group->addAction(ui->setIconSizeSmallAct);
    m_icon_size_act_group->addAction(ui->setIconSizeMediumAct);
    m_icon_size_act_group->addAction(ui->setIconSizeLargeAct);

    m_list_mode_act_group = new QActionGroup(this);
    m_list_mode_act_group->addAction(ui->setlistModeListAct);
    m_list_mode_act_group->addAction(ui->setlistModeGridAct);
}

void MainWindow::createConnects() {
    connect(m_icon_size_act_group, &QActionGroup::triggered, this, [this](QAction* act) {
        static const int index_small = GUI::GetIndex(GUI::game_list_icon_size_small);
        static const int index_medium = GUI::GetIndex(GUI::game_list_icon_size_medium);

        int index;

        if (act == ui->setIconSizeTinyAct)
            index = 0;
        else if (act == ui->setIconSizeSmallAct)
            index = index_small;
        else if (act == ui->setIconSizeMediumAct)
            index = index_medium;
        else
            index = GUI::game_list_max_slider_pos;

        m_save_slider_pos = true;
        resizeIcons(index);
    });
    connect(ui->showGameListAct, &QAction::triggered, this, [this](bool checked) {
        checked ? m_game_list_frame->show() : m_game_list_frame->hide();
        m_gui_settings->SetValue(GUI::main_window_gamelist, checked);
    });
    connect(ui->showTitleBarsAct, &QAction::triggered, this, [this](bool checked) {
        showTitleBars(checked);
        m_gui_settings->SetValue(GUI::main_window_titleBarsVisible, checked);
    });

    connect(ui->showToolBarAct, &QAction::triggered, this, [this](bool checked) {
        ui->toolBar->setVisible(checked);
        m_gui_settings->SetValue(GUI::main_window_toolBarVisible, checked);
    });

    connect(ui->actionReset_Custom_Titles, &QAction::triggered, this, [this] {
        const int count = m_game_list_frame->CustomTitleCount();
        if (count == 0) {
            QMessageBox::information(this, tr("Reset All Custom Titles"),
                                     tr("No game has been renamed."));
            return;
        }

        if (QMessageBox::question(this, tr("Reset All Custom Titles"),
                                  tr("Restore the original name of %n renamed game(s)?", "", count),
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            m_game_list_frame->ResetCustomTitles();
        }
    });

    connect(ui->showHiddenEntriesAct, &QAction::triggered, this, [this](bool checked) {
        m_gui_settings->SetValue(GUI::game_list_show_hidden, checked);
        m_game_list_frame->SetShowHidden(checked);
        m_game_list_frame->Refresh();
    });

    connect(ui->showLogAct, &QAction::triggered, this, [this](bool checked) {
        m_game_list_frame->ShowLog(checked);
        m_gui_settings->SetValue(GUI::main_window_showLog, checked);
    });

    connect(ui->showCompatibilityInGridAct, &QAction::triggered, m_game_list_frame,
            &GameListFrame::SetShowCompatibilityInGrid);

    connect(ui->refreshGameListAct, &QAction::triggered, this,
            [this] { m_game_list_frame->Refresh(true); });

    connect(m_game_list_frame, &GameListFrame::RequestIconSizeChange, this, [this](const int& val) {
        const int idx = ui->sizeSlider->value() + val;
        m_save_slider_pos = true;
        resizeIcons(idx);
    });
    connect(m_game_list_frame, &GameListFrame::GameCountChanged, this,
            [this](int visible_count, int total_count) {
                if (!m_game_count_label) {
                    return;
                }
                m_game_count_label->setText(
                    visible_count == total_count
                        ? tr("%n game(s)", "", total_count)
                        : tr("%1 of %n game(s)", "", total_count).arg(visible_count));
            });
    connect(m_game_list_frame, &GameListFrame::GameListFrameClosed, this, [this]() {
        if (ui->showGameListAct->isChecked()) {
            ui->showGameListAct->setChecked(false);
            m_gui_settings->SetValue(GUI::main_window_gamelist, false);
        }
    });
    connect(m_list_mode_act_group, &QActionGroup::triggered, this, [this](QAction* act) {
        const bool is_list_act = act == ui->setlistModeListAct;
        if (is_list_act == m_is_list_mode)
            return;

        const int slider_pos = ui->sizeSlider->sliderPosition();
        ui->sizeSlider->setSliderPosition(m_other_slider_pos);
        setIconSizeActions(m_other_slider_pos);
        m_other_slider_pos = slider_pos;

        m_is_list_mode = is_list_act;
        m_game_list_frame->SetListMode(m_is_list_mode);
    });

    // toolbar actions
    connect(ui->toolbar_start, &QAction::triggered, this,
            [this] { MainWindow::StartGameWithArgs({}); });
    connect(ui->toolbar_stop, &QAction::triggered, this, &MainWindow::StopGame);
    connect(ui->toolbar_refresh, &QAction::triggered, this,
            [this]() { m_game_list_frame->Refresh(true); });
    connect(ui->toolbar_fullscreen, &QAction::triggered, this, &MainWindow::ToggleFullscreen);
    connect(ui->toolbar_list, &QAction::triggered, this,
            [this]() { ui->setlistModeListAct->trigger(); });
    connect(ui->toolbar_grid, &QAction::triggered, this,
            [this]() { ui->setlistModeGridAct->trigger(); });
    connect(ui->sizeSlider, &QSlider::valueChanged, this, &MainWindow::resizeIcons);
    connect(ui->sizeSlider, &QSlider::sliderReleased, this, [this] {
        const int index = ui->sizeSlider->value();
        m_gui_settings->SetValue(
            m_is_list_mode ? GUI::game_list_iconSize : GUI::game_list_iconSizeGrid, index);
        setIconSizeActions(index);
    });

    connect(ui->sizeSlider, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderNoAction &&
            action !=
                QAbstractSlider::SliderMove) { // we only want to save on mouseclicks or slider
                                               // release (the other connect handles this)
            m_save_slider_pos = true; // actionTriggered happens before the value was changed
        }
    });
    connect(ui->mw_searchbar, &QLineEdit::textChanged, m_game_list_frame,
            &GameListFrame::SetSearchText);
    connect(ui->mw_searchbar, &QLineEdit::returnPressed, m_game_list_frame,
            &GameListFrame::FocusAndSelectFirstEntryIfNoneIs);
    connect(m_game_list_frame, &GameListFrame::FocusToSearchBar, this,
            [this]() { ui->mw_searchbar->setFocus(); });

    connect(ui->actionManage_Users, &QAction::triggered, this, [this] {
        UserManagerDialog user_manager(m_gui_settings, m_emu_settings, this);
        user_manager.exec();
        m_game_list_frame->Refresh(true); // New user may have different games unlocked.
    });
    connect(ui->actionExport_GameList, &QAction::triggered, this, [this] {
        GameListExporter exporter(m_game_list_frame, this);
        exporter.ShowExportDialog();
    });

    const auto open_settings = [this](int tabIndex) {
        SettingsDialog* dlg =
            new SettingsDialog(m_gui_settings, m_emu_settings, m_ipc_client, tabIndex, this);

        connect(dlg, &SettingsDialog::GameFoldersChanged, this, [this]() {
            qDebug() << "Game folders changed!";
            m_game_list_frame->Refresh(true);
        });

        connect(dlg, &SettingsDialog::CompatUpdateRequested, m_game_list_frame, [this]() {
            if (m_game_list_frame) {
                m_game_list_frame->OnCompatUpdatedRequested();
            }
        });

        // When the user picks a new stylesheet in the dialog, ask GUIApplication
        // to reload it through the existing channel.
        connect(dlg, &SettingsDialog::ThemeChanged, this,
                [this]() { emit RequestGlobalStylesheetChange(); });

        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    };
    connect(ui->actionConfigGUI, &QAction::triggered, this,
            [open_settings]() { open_settings(0); });
    connect(ui->actionConfigPaths, &QAction::triggered, this,
            [open_settings]() { open_settings(1); });
    connect(ui->actionConfigLog, &QAction::triggered, this,
            [open_settings]() { open_settings(2); });

    connect(ui->bootGameAct, &QAction::triggered, this,
            [this] { MainWindow::StartGameWithArgs({}); });
    connect(ui->sysPauseAct, &QAction::triggered, this, &MainWindow::PauseGame);
    connect(ui->sysRebootAct, &QAction::triggered, this, &MainWindow::RestartGame);
    connect(ui->sysStopAct, &QAction::triggered, this, &MainWindow::StopGame);

    connect(ui->toolbar_config, &QAction::triggered, this, [=]() { open_settings(0); });

    connect(ui->versionManagerButton, &QPushButton::clicked, this, [this]() {
        auto versionDialog = new VersionDialog(m_gui_settings, this);
        connect(versionDialog, &QDialog::finished, this, [this](int) { LoadVersionComboBox(); });
        versionDialog->exec();
    });

    connect(m_game_list_frame, &GameListFrame::RequestBoot, this,
            [this](game_info game, QStringList args) { StartGameWithArgs(game, args); });

    connect(m_ipc_client.get(), &IpcClient::LogEntrySent, m_game_list_frame,
            &GameListFrame::PrintLog);

    connect(ui->updaterAct, &QAction::triggered, this, [this] {
        auto* checkUpdate = new CheckUpdate(m_gui_settings, true, this);
        checkUpdate->exec();
    });

    connect(ui->aboutAct, &QAction::triggered, this, [this] {
        AboutDialog about(this);
        about.exec();
    });
}

void MainWindow::LoadVersionComboBox() {
    ui->versionComboBox->clear();
    ui->versionComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    QString savedVersionPath =
        m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();
    auto versions = VersionManager::GetVersionList();

    std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) {
        auto getOrder = [](int type) {
            switch (type) {
            case 1:
                return 0; // Pre-release
            case 0:
                return 1; // Release
            case 2:
                return 2; // Local
            default:
                return 3;
            }
        };

        int orderA = getOrder(static_cast<int>(a.type));
        int orderB = getOrder(static_cast<int>(b.type));
        if (orderA != orderB)
            return orderA < orderB;

        if (a.type == VersionManager::VersionType::Release) {
            static QRegularExpression versionRegex("^v\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)$");
            QRegularExpressionMatch matchA = versionRegex.match(QString::fromStdString(a.name));
            QRegularExpressionMatch matchB = versionRegex.match(QString::fromStdString(b.name));

            if (matchA.hasMatch() && matchB.hasMatch()) {
                int majorA = matchA.captured(1).toInt();
                int minorA = matchA.captured(2).toInt();
                int patchA = matchA.captured(3).toInt();
                int majorB = matchB.captured(1).toInt();
                int minorB = matchB.captured(2).toInt();
                int patchB = matchB.captured(3).toInt();

                if (majorA != majorB)
                    return majorA > majorB;
                if (minorA != minorB)
                    return minorA > minorB;
                return patchA > patchB;
            }
        }

        return QString::fromStdString(a.name).compare(QString::fromStdString(b.name),
                                                      Qt::CaseInsensitive) < 0;
    });

    if (versions.empty()) {
        ui->versionComboBox->addItem(tr("None"));
        ui->versionComboBox->setCurrentIndex(0);
        return;
    }

    // Populate combo box
    for (const auto& v : versions) {
        ui->versionComboBox->addItem(QString::fromStdString(v.name),
                                     QString::fromStdString(v.path));
    }

    int selectedIndex = ui->versionComboBox->findData(savedVersionPath);
    ui->versionComboBox->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);

    // Disconnect previous connections to prevent duplicate execution
    ui->versionComboBox->disconnect();

    // Connect activated signal
    connect(
        ui->versionComboBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            QString fullPath = ui->versionComboBox->itemData(index).toString();
            m_gui_settings->SetValue(GUI::version_manager_versionSelected, fullPath);

            QString rootFolder = QCoreApplication::applicationDirPath();
            QString destExe = rootFolder + "/shadPS4.exe";

            auto future = QtConcurrent::run([fullPath, destExe]() {
                if (QFile::exists(destExe))
                    QFile::remove(destExe);
                return QFile::copy(fullPath, destExe);
            });

            auto watcher = new QFutureWatcher<bool>();
            connect(watcher, &QFutureWatcher<bool>::finished, this, [watcher]() {
                bool success = watcher->result();
                watcher->deleteLater();

                if (success) {
                    QMessageBox::information(nullptr, QObject::tr("Version Activated"),
                                             QObject::tr("The selected version is now active."));
                } else {
                    QMessageBox::critical(nullptr, QObject::tr("Copy Failed"),
                                          QObject::tr("Unable to activate selected version."));
                }
            });
            watcher->setFuture(future);
        });

    ui->versionComboBox->adjustSize();
}

void MainWindow::createDockWindows() {
    // new mainwindow widget because existing seems to be bugged for now
    m_mw = new QMainWindow();
    m_mw->setContextMenuPolicy(Qt::PreventContextMenu);

    m_game_list_frame = new GameListFrame(m_gui_settings, m_emu_settings, m_ipc_client, m_mw);
    m_game_list_frame->setObjectName("gamelist");

    m_mw->addDockWidget(Qt::LeftDockWidgetArea, m_game_list_frame);
    m_mw->setDockNestingEnabled(true);
    setCentralWidget(m_mw);

    // Status bar: permanent "N games" / "N of M games" counter, kept in
    // sync via GameListFrame::GameCountChanged.
    m_game_count_label = new QLabel(this);
    statusBar()->addPermanentWidget(m_game_count_label);
}

void MainWindow::updateLanguageActions(const QStringList& language_codes,
                                       const QString& language_code) {
    ui->languageMenu->clear();

    for (const auto& code : language_codes) {
        const QLocale locale(code);
        QString locale_name = locale.nativeLanguageName();

        if (locale.territory() != QLocale::AnyTerritory) {
            locale_name += " (" + locale.nativeTerritoryName() + ")";
        }

        // create new action
        QAction* act = new QAction(locale_name, this);
        act->setData(code);
        act->setToolTip(locale_name);
        act->setCheckable(true);
        act->setChecked(code == language_code);

        // connect to language changer
        connect(act, &QAction::triggered, this, [this, code]() { requestLanguageChange(code); });

        ui->languageMenu->addAction(act);
    }
}

void MainWindow::retranslateUI(const QStringList& language_codes, const QString& language_code) {
    updateLanguageActions(language_codes, language_code);

    ui->retranslateUi(this);

    if (m_game_list_frame) {
        m_game_list_frame->Refresh(true);
    }
}

void MainWindow::resizeIcons(int index) {
    if (ui->sizeSlider->value() != index) {
        ui->sizeSlider->setSliderPosition(index);
        return; // ResizeIcons will be triggered again by setSliderPosition, so return here
    }

    if (m_save_slider_pos) {
        m_save_slider_pos = false;
        m_gui_settings->SetValue(
            m_is_list_mode ? GUI::game_list_iconSize : GUI::game_list_iconSizeGrid, index);

        // this will also fire when we used the actions, but i didn't want to add another boolean
        // member
        setIconSizeActions(index);
    }

    m_game_list_frame->ResizeIcons(index);
}

void MainWindow::setIconSizeActions(int idx) const {
    static const int threshold_tiny =
        GUI::GetIndex((GUI::game_list_icon_size_small + GUI::game_list_icon_size_min) / 2);
    static const int threshold_small =
        GUI::GetIndex((GUI::game_list_icon_size_medium + GUI::game_list_icon_size_small) / 2);
    static const int threshold_medium =
        GUI::GetIndex((GUI::game_list_icon_size_max + GUI::game_list_icon_size_medium) / 2);

    if (idx < threshold_tiny)
        ui->setIconSizeTinyAct->setChecked(true);
    else if (idx < threshold_small)
        ui->setIconSizeSmallAct->setChecked(true);
    else if (idx < threshold_medium)
        ui->setIconSizeMediumAct->setChecked(true);
    else
        ui->setIconSizeLargeAct->setChecked(true);
}

void MainWindow::showTitleBars(bool show) const {
    m_game_list_frame->setTitleBarVisible(show);
}

void MainWindow::configureGuiFromSettings() {
    // Restore GUI state if needed. We need to if they exist.
    if (!restoreGeometry(m_gui_settings->GetValue(GUI::main_window_geometry).toByteArray())) {
        resize(QGuiApplication::primaryScreen()->availableSize() * 0.7);
    }

    restoreState(m_gui_settings->GetValue(GUI::main_window_windowState).toByteArray());
    m_mw->restoreState(m_gui_settings->GetValue(GUI::main_window_mwState).toByteArray());

    ui->showGameListAct->setChecked(m_gui_settings->GetValue(GUI::main_window_gamelist).toBool());
    ui->showToolBarAct->setChecked(
        m_gui_settings->GetValue(GUI::main_window_toolBarVisible).toBool());
    ui->showTitleBarsAct->setChecked(
        m_gui_settings->GetValue(GUI::main_window_titleBarsVisible).toBool());

    m_game_list_frame->setVisible(ui->showGameListAct->isChecked());
    ui->toolBar->setVisible(ui->showToolBarAct->isChecked());

    showTitleBars(ui->showTitleBarsAct->isChecked());

    ui->showHiddenEntriesAct->setChecked(
        m_gui_settings->GetValue(GUI::game_list_show_hidden).toBool());
    m_game_list_frame->SetShowHidden(
        ui->showHiddenEntriesAct
            ->isChecked()); // prevent GetValue in m_game_list_frame->LoadSettings

    ui->showLogAct->setChecked(m_gui_settings->GetValue(GUI::main_window_showLog).toBool());

    ui->showCompatibilityInGridAct->setChecked(
        m_gui_settings->GetValue(GUI::game_list_draw_compat).toBool());

    m_is_list_mode = m_gui_settings->GetValue(GUI::game_list_listMode).toBool();

    // handle icon size options
    if (m_is_list_mode)
        ui->setlistModeListAct->setChecked(true);
    else
        ui->setlistModeGridAct->setChecked(true);

    const int icon_size_index =
        m_gui_settings
            ->GetValue(m_is_list_mode ? GUI::game_list_iconSize : GUI::game_list_iconSizeGrid)
            .toInt();
    m_other_slider_pos =
        m_gui_settings
            ->GetValue(!m_is_list_mode ? GUI::game_list_iconSize : GUI::game_list_iconSizeGrid)
            .toInt();
    ui->sizeSlider->setSliderPosition(icon_size_index);
    setIconSizeActions(icon_size_index);

    // Gamelist
    m_game_list_frame->LoadSettings();
    BackgroundMusicPlayer::getInstance().SetVolume(
        m_gui_settings->GetValue(GUI::game_list_bg_volume).toInt());
}

void MainWindow::saveWindowState() const {
    // Save gui settings
    m_gui_settings->SetValue(GUI::main_window_geometry, saveGeometry(), false);
    m_gui_settings->SetValue(GUI::main_window_windowState, saveState(), false);

    if (m_mw) {
        m_gui_settings->SetValue(GUI::main_window_mwState, m_mw->saveState(), true);
    }

    if (m_game_list_frame) {
        // Save column settings
        m_game_list_frame->SaveSettings();
    }
}

void MainWindow::closeEvent(QCloseEvent* closeEvent) {
    saveWindowState();
}

void MainWindow::RepaintGUI() {
    RepaintToolbarIcons();
    RepaintMenuIcons();
    if (m_game_list_frame) {
        m_game_list_frame->RepaintIcons(true);
    }
}

void MainWindow::CacheOriginalToolbarIcons() {
    m_original_toolbar_icons.clear();
    if (!ui->toolBar) {
        return;
    }
    for (QAction* action : ui->toolBar->actions()) {
        if (action && !action->icon().isNull()) {
            m_original_toolbar_icons.insert(action, action->icon());
        }
    }
}

QIcon MainWindow::ColorizeIcon(const QIcon& source, const QColor& color) {
    QIcon out;
    // Iterate the icon's available sizes so HiDPI variants stay sharp.
    const QList<QSize> sizes = source.availableSizes();
    const QList<QSize> render_sizes = sizes.isEmpty() ? QList<QSize>{QSize(64, 64)} : sizes;
    for (const QSize& size : render_sizes) {
        QPixmap pm = source.pixmap(size);
        if (pm.isNull()) {
            continue;
        }
        QPixmap tinted(pm.size());
        tinted.setDevicePixelRatio(pm.devicePixelRatio());
        tinted.fill(Qt::transparent);
        QPainter p(&tinted);
        p.drawPixmap(0, 0, pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(tinted.rect(), color);
        p.end();
        out.addPixmap(tinted);
    }
    return out;
}

bool MainWindow::IsMonochromeIcon(const QIcon& icon) {
    if (icon.isNull()) {
        return false;
    }
    const QImage img = icon.pixmap(32, 32).toImage().convertToFormat(QImage::Format_ARGB32);
    if (img.isNull()) {
        return false;
    }
    bool any_opaque = false;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.alpha() < 24) {
                continue;
            }
            any_opaque = true;
            const int mx = std::max({c.red(), c.green(), c.blue()});
            const int mn = std::min({c.red(), c.green(), c.blue()});
            if (mx - mn > 24) {
                return false; // has real chroma -> colored icon
            }
        }
    }
    return any_opaque;
}

void MainWindow::RepaintToolbarIcons() {
    if (!ui->toolBar || m_original_toolbar_icons.isEmpty()) {
        return;
    }
    const QColor target_color =
        m_toolbar_icon_color_label
            ? m_toolbar_icon_color_label->palette().color(QPalette::WindowText)
            : QColor(Qt::black);

    for (auto it = m_original_toolbar_icons.constBegin(); it != m_original_toolbar_icons.constEnd();
         ++it) {
        if (QAction* action = it.key()) {
            action->setIcon(ColorizeIcon(it.value(), target_color));
        }
    }
}

void MainWindow::CacheOriginalMenuIcons() {
    m_original_menu_icons.clear();
    if (!menuBar()) {
        return;
    }
    std::function<void(QMenu*)> walk = [&](QMenu* menu) {
        if (!menu) {
            return;
        }
        for (QAction* action : menu->actions()) {
            if (!action) {
                continue;
            }
            if (action->menu()) {
                walk(action->menu());
            } else if (!action->icon().isNull() && IsMonochromeIcon(action->icon())) {
                m_original_menu_icons.insert(action, action->icon());
            }
        }
    };
    for (QAction* top : menuBar()->actions()) {
        if (top && top->menu()) {
            walk(top->menu());
        }
    }
}

void MainWindow::RepaintMenuIcons() {
    if (m_original_menu_icons.isEmpty()) {
        return;
    }
    const QColor target_color =
        m_toolbar_icon_color_label
            ? m_toolbar_icon_color_label->palette().color(QPalette::WindowText)
            : QColor(Qt::black);

    for (auto it = m_original_menu_icons.constBegin(); it != m_original_menu_icons.constEnd();
         ++it) {
        if (QAction* action = it.key()) {
            action->setIcon(ColorizeIcon(it.value(), target_color));
        }
    }
}

void MainWindow::StartGameWithArgs(const game_info& game, QStringList args) {
    BackgroundMusicPlayer::getInstance().StopMusic();
    QString gamePath = "";
    game_info selected_game_info;

    if (!game) {
        selected_game_info = m_game_list_frame->GetSelectedGameInfo();
        if (!selected_game_info) {
            QMessageBox::information(this, tr("Error"), tr("No game selected"));
            return;
        }
    } else {
        selected_game_info = game;
    }

    std::filesystem::path basePath = selected_game_info->info.path;
    std::filesystem::path ebootPath = basePath / "eboot.bin";
    Common::FS::PathToQString(gamePath, ebootPath);

    if (gamePath != "") {
        // AddRecentFiles(gamePath);
        // For a ".zar" game root the eboot lives inside the archive, so probe the
        // archive rather than the (non-existent) path on disk.
        const bool eboot_present =
            Core::FileSys::IsZArchiveFile(basePath)
                ? Core::FileSys::ReadGameFile(basePath, "eboot.bin").has_value()
                : std::filesystem::exists(ebootPath);
        if (!eboot_present) {
            QMessageBox::critical(nullptr, tr("Run Game"), QString(tr("Eboot.bin file not found")));
            return;
        }
        StartEmulator(ebootPath, args);
        last_game_info = selected_game_info;

        // UpdateToolbarButtons();
    }
}

void MainWindow::StartEmulator(std::filesystem::path path, QStringList args) {
    if (EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("Run Game"), QString(tr("Game is already running!")));
        return;
    }

    QString selectedVersion =
        m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();
    if (selectedVersion.isEmpty()) {
        QMessageBox::warning(this, tr("No Version Selected"),
                             // clang-format off
                             tr("No emulator version was selected.\nThe Version Manager menu will then open.\nSelect an emulator version from the right panel."));
        // clang-format on
        auto versionDialog = new VersionDialog(m_gui_settings, this);
        connect(versionDialog, &QDialog::finished, this, [this](int) { LoadVersionComboBox(); });
        versionDialog->exec();
        return;
    }

    QFileInfo fileInfo(selectedVersion);
    if (!fileInfo.exists()) {
        QMessageBox::critical(nullptr, "shadPS4",
                              QString(tr("Could not find the emulator executable")));
        return;
    }

    QStringList final_args{"--game", QString::fromStdWString(path.wstring())};
    final_args.append(args);

    EmulatorState::GetInstance()->SetGameRunning(true);

    QString workDir = QDir::currentPath();
    m_ipc_client->startEmulator(fileInfo, final_args, workDir);
    // TODO//m_ipc_client->setActiveController(GamepadSelect::GetSelectedGamepad());
}

void MainWindow::RunGame() {
    auto info = last_game_info;
    auto appVersion = info->info.app_ver;
    auto gameSerial = info->info.serial;
    auto patches = MemoryPatcher::readPatches(gameSerial, appVersion);
    for (auto patch : patches) {
        m_ipc_client->sendMemoryPatches(patch.modName, patch.address, patch.value, patch.target,
                                        patch.size, patch.maskOffset, patch.littleEndian,
                                        patch.mask, patch.maskOffset);
    }

    m_ipc_client->startGame();
}

void MainWindow::onGameClosed() {
    EmulatorState::GetInstance()->SetGameRunning(false);
    is_paused = false;

    // shadPS4 writes the session's play time to play_time.txt right before
    // exiting; do a lightweight (no disk rescan) refresh so Last
    // Played/Time Played pick it up immediately instead of waiting for the
    // next manual refresh.
    if (m_game_list_frame) {
        m_game_list_frame->Refresh(false);
    }
}

void MainWindow::RestartEmulator() {
    QString exe = m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();
    std::filesystem::path last_game_path = last_game_info->info.path;
    QStringList args{"--game", QString::fromStdWString(last_game_path.wstring())};

    if (m_ipc_client->parsedArgs.size() > 0) {
        args.clear();
        for (auto arg : m_ipc_client->parsedArgs) {
            args.append(QString::fromStdString(arg));
        }
        m_ipc_client->parsedArgs.clear();
    }

    QFileInfo fileInfo(exe);
    QString workDir = QDir::currentPath();

    m_ipc_client->startEmulator(fileInfo, args, workDir);
}

void MainWindow::RestartGame() {
    if (!EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("No Running Game"),
                              QString(tr("No running game to restart")));
        return;
    }

    m_ipc_client->restartEmulator();
}

void MainWindow::PauseGame() {
    if (!EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("No Running Game"),
                              QString(tr("No running game to pause")));
        return;
    }

    if (is_paused) {
        m_ipc_client->resumeGame();
        ui->sysPauseAct->setText(tr("Pause"));
        ui->sysPauseAct->setToolTip(tr("Pause emulation"));
        is_paused = false;
    } else {
        m_ipc_client->pauseGame();
        ui->sysPauseAct->setText(tr("Resume"));
        ui->sysPauseAct->setToolTip(tr("Resume emulation"));
        is_paused = true;
    }
}

void MainWindow::StopGame() {
    if (!EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("No Running Game"),
                              QString(tr("No running game to stop")));
        return;
    }

    m_ipc_client->stopEmulator();
}

void MainWindow::ToggleFullscreen() {
    if (!EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("No Running Game"),
                              QString(tr("No running game to toggle fullscreen")));
        return;
    }

    m_ipc_client->toggleFullscreen();
}

void MainWindow::StartEmulatorExecutable(QString emulatorArg, QString gameArg,
                                         QStringList passed_args) {
    if (EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("Run Emulator"),
                              QString(tr("Emulator is already running!")));
        return;
    }

    std::filesystem::path gamePath;
    bool gameFound = false;
    if (std::filesystem::exists(Common::FS::PathFromQString(gameArg))) {
        gameFound = true;
        gamePath = Common::FS::PathFromQString(gameArg);
    } else {
        // In install folders, find game folder with same name as gameArg
        const auto install_dir_array = m_emu_settings->GetGameInstallDirs();
        std::vector<bool> install_dirs_enabled;

        try {
            install_dirs_enabled = m_emu_settings->GetGameInstallDirsEnabled();
        } catch (...) {
            // If it does not exist, assume that all are enabled.
            install_dirs_enabled.resize(install_dir_array.size(), true);
        }

        for (size_t i = 0; i < install_dir_array.size(); i++) {
            std::filesystem::path dir = install_dir_array[i];
            bool enabled = install_dirs_enabled[i];

            if (enabled && std::filesystem::exists(dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    if (entry.is_directory()) {
                        if (entry.path().filename().string() == gameArg.toStdString()) {
                            gamePath = entry.path() / "eboot.bin";
                            gameFound = true;
                            break;
                        }
                    } else if (Core::FileSys::IsZArchiveFile(entry.path())) {
                        // "<title>.zar" is the archived form of the "<title>" folder,
                        // so compare against the stem and pass the archive itself.
                        if (entry.path().stem().string() == gameArg.toStdString()) {
                            gamePath = entry.path();
                            gameFound = true;
                            break;
                        }
                    }
                }
            }

            if (gameFound)
                break;
        }
    }

    QStringList args = {};
    if (!gameArg.isEmpty()) {
        if (!gameFound) {
            QMessageBox::critical(nullptr, "shadPS4",
                                  QString(tr("Invalid game argument provided")));
            quick_exit(1);
        }

        QStringList game_args{"--game", QString::fromStdWString(gamePath.wstring())};
        args.append(game_args);
    }

    if (!passed_args.isEmpty())
        args.append(passed_args);

    QString emulatorPath;
    if (std::filesystem::exists(Common::FS::PathFromQString(emulatorArg))) {
        emulatorPath = emulatorArg;
    } else {
        if (emulatorArg == "default") {
            emulatorPath =
                m_gui_settings->GetValue(GUI::version_manager_versionSelected).toString();
        }
    }

    QFileInfo fileInfo(emulatorPath);
    if (!fileInfo.exists()) {
        QMessageBox::critical(nullptr, "shadPS4",
                              QString(tr("Could not find the emulator executable")));
        return;
    }

    EmulatorState::GetInstance()->SetGameRunning(true);
    QString workDir = QDir::currentPath();
    m_ipc_client->startEmulator(fileInfo, args, workDir);

    GameInfo game = GameInfoTools::readGameInfo(
        Core::FileSys::IsZArchiveFile(gamePath) ? gamePath : gamePath.parent_path());
    auto appVersion = game.app_ver;
    auto gameSerial = game.serial;
    auto patches = MemoryPatcher::readPatches(gameSerial, appVersion);
    for (auto patch : patches) {
        m_ipc_client->sendMemoryPatches(patch.modName, patch.address, patch.value, patch.target,
                                        patch.size, patch.maskOffset, patch.littleEndian,
                                        patch.mask, patch.maskOffset);
    }

    m_ipc_client->startGame();
}
