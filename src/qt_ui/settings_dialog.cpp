// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <QDesktopServices>
#include <QtWidgets>
#include "background_music_player.h"
#include "common/assert.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/emulator_state.h"
#include "game_info.h"
#include "gui_application.h"
#include "gui_settings.h"
#include "log_presets_dialog.h"
#ifdef ENABLE_UPDATER
#include "qt_ui/check_update.h"
#endif
#include <iostream>
#include "settings_dialog.h"
#include "settings_dialog_helper_texts.h"
#include "ui_settings_dialog.h"

// Normalize paths consistently for equality checks
static inline std::string NormalizePath(const std::filesystem::path& p) {
    // Convert to a normalized lexical path
    auto np = p.lexically_normal();

    // Convert to UTF-8 string
    auto u8 = np.generic_u8string();
    std::string s(u8.begin(), u8.end());

#ifdef _WIN32
    // Windows paths: drive letters are case-insensitive to normalize case
    // Example: "C:/Games" vs "c:/Games"
    if (s.size() >= 2 && s[1] == ':')
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
#endif

    return s;
}

// Equality operators
inline bool operator==(GameInstallDir const& a, GameInstallDir const& b) {
    return a.enabled == b.enabled && NormalizePath(a.path) == NormalizePath(b.path);
}

inline bool operator!=(GameInstallDir const& a, GameInstallDir const& b) {
    return !(a == b);
}

void LogUpdateLevels() {
    spdlog::level default_log_level = spdlog::level::info;
    std::unordered_map<std::string, spdlog::level> log_level_per_class;

    if (EmulatorSettings.IsLogEnable()) {
        for (const auto class_level : std::views::split(EmulatorSettings.GetLogFilter(), ' ')) {
            const auto class_level_pair =
                std::views::split(class_level, ':') | std::ranges::to<std::vector<std::string>>();

            if (class_level_pair.size() != 2) {
                std::cerr << "bad log filter provided" << std::endl;
                continue;
            }

            if (class_level_pair.front()[0] == '*') {
                default_log_level = spdlog::level_from_str(class_level_pair.back() |
                                                           std::ranges::to<std::string>());
            } else {
                log_level_per_class[class_level_pair.front() | std::ranges::to<std::string>()] =
                    spdlog::level_from_str(class_level_pair.back() |
                                           std::ranges::to<std::string>());
            }
        }
    }

    for (auto& [name, logger] : Common::Log::ALL_LOGGERS) {
        ASSERT_MSG(logger != nullptr, "logger {} is null", name);

        if (EmulatorSettings.IsLogEnable()) {
            const auto level_it = log_level_per_class.find(std::string(name));

            logger->set_level(level_it != log_level_per_class.end() ? level_it->second
                                                                    : default_log_level);
        } else {
            logger->set_level(spdlog::level::off);
        }
    }
}

SettingsDialog::SettingsDialog(std::shared_ptr<GUISettings> gui_settings,
                               std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                               std::shared_ptr<IpcClient> ipc_client, int tab_index,
                               QWidget* parent, const GameInfo* game, bool customFromGlobal)
    : QDialog(parent), m_tab_index(tab_index), ui(new Ui::SettingsDialog),
      m_gui_settings(std::move(gui_settings)), m_emu_settings(std::move(emu_settings)),
      m_ipc_client(ipc_client), m_custom_settings_from_global(customFromGlobal) {
    ui->setupUi(this);

    // Store game info if provided
    if (game) {
        m_current_game = *game;
        m_game_serial = game->serial;
    }

    if (!IsGlobal() && m_custom_settings_from_global && !m_game_serial.empty()) {
        // We need to load game-specific settings
        m_original_settings = std::make_shared<EmulatorSettingsImpl>();
        *m_original_settings = *m_emu_settings; // Backup original

        // Create and load game-specific settings
        m_game_specific_settings = std::make_shared<EmulatorSettingsImpl>();
        m_game_specific_settings->Load("");            // Load global
        m_game_specific_settings->Load(m_game_serial); // Apply overrides

        // Use game-specific settings
        m_emu_settings.swap(m_game_specific_settings);

        this->setWindowTitle(tr("Custom Settings for %1 [%2]")
                                 .arg(QString::fromStdString(m_current_game.name),
                                      QString::fromStdString(m_game_serial)));
        ui->customSettingsLabel->setVisible(true);
    } else if (IsGlobal()) {
        this->setWindowTitle(tr("Global Settings"));
        ui->customSettingsLabel->setVisible(false);
    }

    const SettingsDialogHelperTexts helptexts;
    // Paths
    SubscribeHelpText(ui->gameFoldersGroupBox, helptexts.settings.paths_gameDir);
    SubscribeHelpText(ui->gameFoldersListWidget, helptexts.settings.paths_gameDir);
    SubscribeHelpText(ui->addFolderButton, helptexts.settings.paths_gameDir_add);
    SubscribeHelpText(ui->removeFolderButton, helptexts.settings.paths_gameDir_remove);
    SubscribeHelpText(ui->dlcFolderGroupBox_2, helptexts.settings.paths_dlcDir);
    SubscribeHelpText(ui->currentDLCFolder, helptexts.settings.paths_dlcDir);
    SubscribeHelpText(ui->browseDLCButton, helptexts.settings.paths_dlcDir_browse);
    SubscribeHelpText(ui->homeGroupBox, helptexts.settings.paths_homeDir);
    SubscribeHelpText(ui->currentHomePath, helptexts.settings.paths_homeDir);
    SubscribeHelpText(ui->browseHomeButton, helptexts.settings.paths_homeDir_browse);
    SubscribeHelpText(ui->sysmodulesGroupBox, helptexts.settings.paths_sysmodulesDir);
    SubscribeHelpText(ui->currentSysmodulesPath, helptexts.settings.paths_sysmodulesDir);
    SubscribeHelpText(ui->browseSysmodulesButton, helptexts.settings.paths_sysmodulesDir_browse);

    // General
    SubscribeHelpText(ui->ScanDepthGroupBox, helptexts.settings.general_scan_depth_combo);
    SubscribeHelpText(ui->ScanDepthComboBox, helptexts.settings.general_scan_depth_combo);
    SubscribeHelpText(ui->updaterGroupBox, helptexts.settings.general_updater);
    SubscribeHelpText(ui->updaterCheckBox, helptexts.settings.general_updater_check_startup);
    SubscribeHelpText(ui->changelogCheckBox, helptexts.settings.general_updater_changelog);
    SubscribeHelpText(ui->checkUpdateButton, helptexts.settings.general_updater_check_now);

    // Log
    SubscribeHelpText(ui->loggerGroupBox, helptexts.settings.log_section);
    SubscribeHelpText(ui->logFilter, helptexts.settings.log_filter);
    SubscribeHelpText(ui->logFilterLineEdit, helptexts.settings.log_filter);
    SubscribeHelpText(ui->logPresetsButton, helptexts.settings.log_presets);
    SubscribeHelpText(ui->enableLoggingCheckBox, helptexts.settings.log_enable);
    SubscribeHelpText(ui->logOpenLocationButton, helptexts.settings.log_open_location);
    SubscribeHelpText(ui->separateLogFilesCheckbox, helptexts.settings.log_separate_files);
    SubscribeHelpText(ui->logSyncCheckBox, helptexts.settings.log_sync);
    SubscribeHelpText(ui->logSkipDuplicateCheckBox, helptexts.settings.log_skip_duplicate);
    SubscribeHelpText(ui->logMaxSkipDurationGroupBox, helptexts.settings.log_max_skip_duration);
    SubscribeHelpText(ui->logMaxSkipDurationLineEdit, helptexts.settings.log_max_skip_duration);
    SubscribeHelpText(ui->logSizeLimitGroupBox, helptexts.settings.log_size_limit);
    SubscribeHelpText(ui->logSizeLimitLineEdit, helptexts.settings.log_size_limit);
    SubscribeHelpText(ui->logAppendCheckBox, helptexts.settings.log_append);
    SubscribeHelpText(ui->logTypeGroupBox, helptexts.settings.log_type);
    SubscribeHelpText(ui->logTypeComboBox, helptexts.settings.log_type);

    // GUI
    SubscribeHelpText(ui->GUIMusicGroupBox, helptexts.settings.gui_music);
    SubscribeHelpText(ui->playBGMCheckBox, helptexts.settings.gui_music);
    SubscribeHelpText(ui->BGMVolumeSlider, helptexts.settings.gui_music_volume);
    SubscribeHelpText(ui->themeGroupBox, helptexts.settings.gui_theme);
    SubscribeHelpText(ui->themeComboBox, helptexts.settings.gui_theme);

    // Audio

    // Compatibility
    SubscribeHelpText(ui->CompatgroupBox, helptexts.settings.compat_section);
    SubscribeHelpText(ui->checkCompatibilityOnStartupCheckBox,
                      helptexts.settings.compat_check_on_startup);
    SubscribeHelpText(ui->updateCompatibilityButton, helptexts.settings.compat_update_button);

    // GPU / Graphics

    // Input

    // Debug

    // Experimental

    PopulateComboBoxes();
    PathTabConnections();
    OtherConnections();

    if (!IsGlobal()) {
        MapUIControls();
        DisableNonOverrideableSettings();
    }

    LoadValuesFromConfig();

    HandleButtonBox();
}

SettingsDialog::~SettingsDialog() {
    // Clean up game-specific settings when dialog closes
    if (m_game_specific_settings) {
        // If we swapped settings, swap them back
        if (!IsGlobal() && m_custom_settings_from_global) {
            if (m_original_settings) {
                // Restore original settings
                m_emu_settings.swap(m_game_specific_settings);
            }
        }

        // Clear the shared_ptr
        m_game_specific_settings.reset();
    }

    // Also clear original settings
    if (m_original_settings) {
        m_original_settings.reset();
    }
}

void SettingsDialog::open() {
    QDialog::open();
    ui->tabWidgetSettings->setCurrentIndex(m_tab_index);
}
/*
static std::vector<QString> m_physical_devices;
void SettingsDialog::GetPhysicalDevices() {
    if (volkInitialize() != VK_SUCCESS) {
        qWarning() << "Failed to initialize Volk.";
        return;
    }

    // Create Vulkan instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "shadLauncher5";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
    VkInstance instance;
    if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS) {
        qWarning() << "Failed to create Vulkan instance.";
        return;
    }

    // Load instance-based function pointers
    volkLoadInstance(instance);

    // Enumerate devices
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        qWarning() << "No Vulkan physical devices found.";
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    m_physical_devices.clear();
    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        QString name = QString::fromUtf8(props.deviceName, -1);
        m_physical_devices.push_back(name);
    }

    vkDestroyInstance(instance, nullptr);
}
*/
// ---------------------------- Help text ----------------------------
void SettingsDialog::SubscribeHelpText(QObject* object, const QString& text) {
    m_descriptions[object] = text;
    object->installEventFilter(this);
}

bool SettingsDialog::eventFilter(QObject* object, QEvent* event) {
    if (!m_descriptions.contains(object))
        return QDialog::eventFilter(object, event);

    if (event->type() == QEvent::Enter) {
        ui->descriptionText->setText(m_descriptions[object].replace("\\n", "\n"));
    } else if (event->type() == QEvent::Leave) {
        ui->descriptionText->setText(
            tr("Point your mouse at an option to display its description."));
    }

    return QDialog::eventFilter(object, event);
}

// ---------------------------- Path Tab Connections (UI only) ----------------------------
void SettingsDialog::PathTabConnections() {
    // -------------- Games Folder --------------------------------------------------------
    auto* list = ui->gameFoldersListWidget;

    // Enable drag & drop internal reordering (UI only)
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setDragEnabled(true);
    list->setAcceptDrops(true);
    list->setDropIndicatorShown(true);

    // --- Add folder (UI only) ---
    connect(ui->addFolderButton, &QPushButton::clicked, this, [this]() {
        const QString sel =
            QFileDialog::getExistingDirectory(this, tr("Directory to install games"));
        if (sel.isEmpty())
            return;

        const auto path = Common::FS::PathFromQString(sel);
        if (!std::filesystem::exists(path)) {
            QMessageBox::warning(this, tr("Invalid Path"), tr("Selected folder does not exist."));
            return;
        }

        // Prevent duplicates by comparing raw text entries (UI-level)
        for (int i = 0; i < ui->gameFoldersListWidget->count(); ++i) {
            if (ui->gameFoldersListWidget->item(i)->text() == sel) {
                QMessageBox::warning(this, tr("Duplicate Path"),
                                     tr("This folder is already added."));
                return;
            }
        }

        auto* item = new QListWidgetItem(sel);
        item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        ui->gameFoldersListWidget->addItem(item);
    });

    // --- Remove folder (UI only) ---
    connect(ui->removeFolderButton, &QPushButton::clicked, this, [this]() {
        auto* item = ui->gameFoldersListWidget->currentItem();
        if (item)
            delete item;
    });

    // Enable/disable remove button depending on selection
    connect(list, &QListWidget::itemSelectionChanged, this, [this]() {
        ui->removeFolderButton->setEnabled(!ui->gameFoldersListWidget->selectedItems().isEmpty());
    });

    // --- Double-click opens folder ---
    connect(list, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* item) {
        if (item)
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->text()));
    });

    // --- Context menu (UI only): open / remove / toggle ---
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = ui->gameFoldersListWidget->itemAt(pos);
        if (!item)
            return;

        QMenu menu(this);
        QAction* openAction = menu.addAction(tr("Open Folder"));
        QAction* removeAction = menu.addAction(tr("Remove"));
        QAction* toggleAction =
            menu.addAction(item->checkState() == Qt::Checked ? tr("Disable") : tr("Enable"));

        QAction* chosen = menu.exec(ui->gameFoldersListWidget->mapToGlobal(pos));
        if (!chosen)
            return;

        if (chosen == openAction) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->text()));
        } else if (chosen == removeAction) {
            delete item;
        } else if (chosen == toggleAction) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    });

    // ------------------Addon Folder ----------------------------------------------------------
    connect(ui->browseDLCButton, &QPushButton::clicked, this, [this]() {
        const auto dlc_folder_path = m_emu_settings->GetAddonInstallDir();
        QString initial_path;
        Common::FS::PathToQString(initial_path, dlc_folder_path);

        QString dlc_folder_path_string =
            QFileDialog::getExistingDirectory(this, tr("Select directory for DLC"), initial_path);

        auto file_path = Common::FS::PathFromQString(dlc_folder_path_string);
        if (!file_path.empty()) {
            ui->currentDLCFolder->setText(dlc_folder_path_string);
        }
    });
    // --------------Home Folder --------------------------------------------------------
    connect(ui->browseHomeButton, &QPushButton::clicked, this, [this]() {
        const auto home_path = m_emu_settings->GetHomeDir();
        QString initial_path;
        Common::FS::PathToQString(initial_path, home_path);

        QString home_path_string =
            QFileDialog::getExistingDirectory(this, tr("Select directory for home"), initial_path);

        auto file_path = Common::FS::PathFromQString(home_path_string);
        if (!file_path.empty()) {
            ui->currentHomePath->setText(home_path_string);
        }
    });
    // -----------Sys Modules Folder --------------------------------------------------------------
    connect(ui->browseSysmodulesButton, &QPushButton::clicked, this, [this]() {
        const auto sysmodules_path = m_emu_settings->GetSysModulesDir();
        QString initial_path;
        Common::FS::PathToQString(initial_path, sysmodules_path);

        QString sysmodules_path_string = QFileDialog::getExistingDirectory(
            this, tr("Select directory for System modules"), initial_path);

        auto file_path = Common::FS::PathFromQString(sysmodules_path_string);
        if (!file_path.empty()) {
            ui->currentSysmodulesPath->setText(sysmodules_path_string);
        }
    });
}

// ---------------------------- Non-Path Connections ----------------------------
void SettingsDialog::OtherConnections() {

    // ------------------ Gui tab --------------------------------------------------------
    connect(ui->BGMVolumeSlider, &QSlider::valueChanged, this,
            [](int value) { BackgroundMusicPlayer::getInstance().SetVolume(value); });

#ifdef ENABLE_UPDATER
    connect(ui->checkUpdateButton, &QPushButton::clicked, this, [this]() {
        auto checkUpdate = new CheckUpdate(m_gui_settings, true);
        checkUpdate->exec();
    });
#else
    ui->updaterGroupBox->setVisible(false);
#endif

    // ------------------ Log tab --------------------------------------------------------
    connect(ui->logOpenLocationButton, &QPushButton::clicked, this, []() {
        QString userPath;
        Common::FS::PathToQString(userPath, Common::FS::GetUserPath(Common::FS::PathType::LogDir));
        QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
    });

    connect(ui->logSkipDuplicateCheckBox, &QPushButton::clicked, this, [this]() {
        ui->logMaxSkipDurationGroupBox->setVisible(!ui->logMaxSkipDurationGroupBox->isVisible());
    });

    connect(ui->logPresetsButton, &QPushButton::clicked, this, [this]() {
        auto dlg = new LogPresetsDialog(m_gui_settings, this);
        connect(dlg, &LogPresetsDialog::PresetChosen, this,
                [this](const QString& filter) { ui->logFilterLineEdit->setText(filter); });
        dlg->exec();
    });
}

// ---------------------------- Load from backend to UI ----------------------------
void SettingsDialog::LoadValuesFromConfig() {

    // ------------------ GUI tab --------------------------------------------------------
    {
        const QString current_theme =
            m_gui_settings->GetValue(GUI::meta_currentStylesheet).toString();
        const int idx = ui->themeComboBox->findData(current_theme);
        if (idx >= 0) {
            ui->themeComboBox->setCurrentIndex(idx);
        } else {
            ui->themeComboBox->addItem(current_theme + tr(" (missing)"), current_theme);
            ui->themeComboBox->setCurrentIndex(ui->themeComboBox->count() - 1);
        }
    }
    ui->playBGMCheckBox->setChecked(m_gui_settings->GetValue(GUI::game_list_play_bg).toBool());
    ui->BGMVolumeSlider->setValue(m_gui_settings->GetValue(GUI::game_list_bg_volume).toInt());
    ui->checkCompatibilityOnStartupCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::compatibility_check_on_startup).toBool());
#ifdef ENABLE_UPDATER
    ui->updaterCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::general_check_gui_updates).toBool());
    ui->changelogCheckBox->setChecked(
        m_gui_settings->GetValue(GUI::general_show_changelog).toBool());
#endif

    // ------------------ Log tab --------------------------------------------------------
    ui->logAppendCheckBox->setChecked(EmulatorSettings.IsLogAppend());
    ui->enableLoggingCheckBox->setChecked(m_emu_settings->IsLogEnable());
    ui->logFilterLineEdit->setText(QString::fromStdString(m_emu_settings->GetLogFilter()));
    ui->separateLogFilesCheckbox->setChecked(m_emu_settings->IsLogSeparate());
    ui->logSizeLimitLineEdit->setValue(m_emu_settings->GetLogSizeLimit());
    ui->logSyncCheckBox->setChecked(m_emu_settings->IsLogSync());

    ui->logSkipDuplicateCheckBox->setChecked(m_emu_settings->IsLogSkipDuplicate());
    ui->logMaxSkipDurationGroupBox->setVisible(ui->logSkipDuplicateCheckBox->isChecked());
    ui->logMaxSkipDurationLineEdit->setValue(m_emu_settings->GetLogMaxSkipDuration());

#ifdef _WIN32
    std::string logType = m_emu_settings->GetLogType();
    QString translatedText_logType = logTypeMap.key(QString::fromStdString(logType));
    if (!translatedText_logType.isEmpty()) {
        ui->logTypeComboBox->setCurrentText(translatedText_logType);
    }
#else
    ui->logTypeGroupBox->setVisible(false);
#endif

    // ------------------ Games Folder --------------------------------------------------------
    ui->gameFoldersListWidget->clear();
    const auto& dirs = m_emu_settings->GetAllGameInstallDirs();
    for (const auto& entry : dirs) {
        QString qpath;
        Common::FS::PathToQString(qpath, entry.path);

        auto* item = new QListWidgetItem(qpath);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
        item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));

        if (!std::filesystem::exists(entry.path)) {
            item->setForeground(Qt::red);
            item->setToolTip(tr("This path does not exist on disk."));
        } else {
            item->setToolTip(QString());
        }

        ui->gameFoldersListWidget->addItem(item);
    }
    // ------------------ Addon Folder --------------------------------------------------------
    const auto dlc_folder_path = m_emu_settings->GetAddonInstallDir();
    QString dlc_folder_path_string;
    Common::FS::PathToQString(dlc_folder_path_string, dlc_folder_path);
    ui->currentDLCFolder->setText(dlc_folder_path_string);
    // ------------------ Home Folder --------------------------------------------------------
    const auto home_path = m_emu_settings->GetHomeDir();
    QString home_path_string;
    Common::FS::PathToQString(home_path_string, home_path);
    ui->currentHomePath->setText(home_path_string);
    // ------------------ Sys Modules Folder--------------------------------------------------
    const auto sysmodules_path = m_emu_settings->GetSysModulesDir();
    QString sysmodules_path_string;
    Common::FS::PathToQString(sysmodules_path_string, sysmodules_path);
    ui->currentSysmodulesPath->setText(sysmodules_path_string);
    // ----------GUI Settings --------------------------------------
    ui->ScanDepthComboBox->setCurrentIndex(
        m_gui_settings->GetValue(GUI::general_directory_depth_scanning).toInt() - 1);
}

// ---------------------------- Compare backend vs UI ----------------------------
bool SettingsDialog::IsGameFoldersChanged() const {
    // Compare game install dirs
    auto backend = m_emu_settings->GetAllGameInstallDirs();
    std::vector<GameInstallDir> ui_dirs;

    ui_dirs.reserve(ui->gameFoldersListWidget->count());
    for (int i = 0; i < ui->gameFoldersListWidget->count(); ++i) {
        auto* item = ui->gameFoldersListWidget->item(i);
        GameInstallDir d;
        d.path = Common::FS::PathFromQString(item->text());
        d.enabled = (item->checkState() == Qt::Checked);
        ui_dirs.push_back(d);
    }

    if (backend != ui_dirs)
        return true;

    // Compare scan depth
    int backend_depth = m_gui_settings->GetValue(GUI::general_directory_depth_scanning).toInt();
    int ui_depth = ui->ScanDepthComboBox->currentIndex() + 1;
    if (backend_depth != ui_depth)
        return true;

    return false;
}

void SettingsDialog::ApplyValuesToBackend() {
    const bool is_specific = !IsGlobal();

    std::vector<GameInstallDir> dirs;
    dirs.reserve(ui->gameFoldersListWidget->count());

    // ------------------ GUI tab --------------------------------------------------------
    {
        const QString new_theme = ui->themeComboBox->currentData().toString();
        const QString prev_theme = m_gui_settings->GetValue(GUI::meta_currentStylesheet).toString();
        if (!new_theme.isEmpty() && new_theme != prev_theme) {
            m_gui_settings->SetValue(GUI::meta_currentStylesheet, new_theme);
            emit ThemeChanged();
        }
    }

    m_gui_settings->SetValue(GUI::general_directory_depth_scanning,
                             ui->ScanDepthComboBox->currentIndex() + 1);
    m_gui_settings->SetValue(GUI::game_list_play_bg, ui->playBGMCheckBox->isChecked());
    m_gui_settings->SetValue(GUI::game_list_bg_volume, ui->BGMVolumeSlider->value());
    m_gui_settings->SetValue(GUI::compatibility_check_on_startup,
                             ui->checkCompatibilityOnStartupCheckBox->isChecked());
#ifdef ENABLE_UPDATER
    m_gui_settings->SetValue(GUI::general_show_changelog, ui->changelogCheckBox->isChecked());
    m_gui_settings->SetValue(GUI::general_check_gui_updates, ui->updaterCheckBox->isChecked());
#endif

    // ------------------ Log tab --------------------------------------------------------
    m_emu_settings->SetLogAppend(ui->logAppendCheckBox->isChecked(), is_specific);
    m_emu_settings->SetLogEnable(ui->enableLoggingCheckBox->isChecked(), is_specific);
    m_emu_settings->SetLogFilter(ui->logFilterLineEdit->text().toStdString(), is_specific);
    m_emu_settings->SetLogMaxSkipDuration(ui->logMaxSkipDurationLineEdit->value(), is_specific);
    m_emu_settings->SetLogSeparate(ui->separateLogFilesCheckbox->isChecked(), is_specific);
    m_emu_settings->SetLogSizeLimit(ui->logSizeLimitLineEdit->value(), is_specific);
    m_emu_settings->SetLogSkipDuplicate(ui->logSkipDuplicateCheckBox->isChecked(), is_specific);
    m_emu_settings->SetLogSync(ui->logSyncCheckBox->isChecked(), is_specific);

#ifdef _WIN32
    m_emu_settings->SetLogType(logTypeMap.value(ui->logTypeComboBox->currentText()).toStdString(),
                               is_specific);
#endif

    // ------------------ Paths tab --------------------------------------------------------
    for (int i = 0; i < ui->gameFoldersListWidget->count(); ++i) {
        auto* item = ui->gameFoldersListWidget->item(i);
        GameInstallDir d;
        d.path = Common::FS::PathFromQString(item->text());
        d.enabled = (item->checkState() == Qt::Checked);
        dirs.push_back(std::move(d));
    }
    m_emu_settings->SetAllGameInstallDirs(dirs);
    m_emu_settings->SetAddonInstallDir(Common::FS::PathFromQString(ui->currentDLCFolder->text()));
    m_emu_settings->SetHomeDir(Common::FS::PathFromQString(ui->currentHomePath->text()));
    m_emu_settings->SetSysModulesDir(
        Common::FS::PathFromQString(ui->currentSysmodulesPath->text()));
}

// ---------------------------- Button box handling ----------------------------
void SettingsDialog::HandleButtonBox() {
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QWidget::close);

    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, [this](QAbstractButton* button) {
        auto* applyBtn = ui->buttonBox->button(QDialogButtonBox::Apply);
        auto* saveBtn = ui->buttonBox->button(QDialogButtonBox::Save);
        auto* restoreBtn = ui->buttonBox->button(QDialogButtonBox::RestoreDefaults);
        auto* closeBtn = ui->buttonBox->button(QDialogButtonBox::Close);

        // APPLY: update backend (memory) and emit only if changed
        if (button == applyBtn) {
            bool changed = IsGameFoldersChanged();
            ApplyValuesToBackend();

            if (changed) {
                emit GameFoldersChanged();
            }

            if (!IsGlobal()) {
                // Save game-specific overrides immediately
                if (!m_emu_settings->Save(m_game_serial)) {
                    QMessageBox::warning(this, tr("Error"), tr("Failed to save game settings."));
                } else {
                    QMessageBox::information(this, tr("Settings Applied"),
                                             tr("Game-specific settings have been saved."));
                }
            }
            return;
        }

        // SAVE: apply, emit if changed, then persist and close
        if (button == saveBtn) {
            bool changed = IsGameFoldersChanged();
            ApplyValuesToBackend();

            if (changed) {
                emit GameFoldersChanged();
            }

            if (IsGlobal()) {
                // Save global settings
                if (!m_emu_settings->Save()) {
                    QMessageBox::warning(this, tr("Error"), tr("Failed to save global settings."));
                    return;
                }
            } else {
                // Save game-specific overrides
                if (!m_emu_settings->Save(m_game_serial)) {
                    QMessageBox::warning(this, tr("Error"), tr("Failed to save game settings."));
                    return;
                }
            }

            close();
            Q_EMIT EmuSettingsApplied();
            return;
        }

        if (button == restoreBtn) {
            QString message =
                IsGlobal()
                    ? tr("Are you sure you want to restore all settings to their default values?")
                    : tr("Are you sure you want to restore all settings to global defaults?\n"
                         "This will remove all game-specific overrides.");

            const auto reply = QMessageBox::question(this, tr("Restore Defaults"), message,
                                                     QMessageBox::Yes | QMessageBox::No);

            if (reply != QMessageBox::Yes)
                return;

            if (IsGlobal()) {
                // Snapshot before defaults
                const auto before = m_emu_settings->GetAllGameInstallDirs();
                m_emu_settings->SetDefaultValues();
                const auto after = m_emu_settings->GetAllGameInstallDirs();

                // Update UI to reflect defaults
                LoadValuesFromConfig();

                if (before != after) {
                    emit GameFoldersChanged();
                }
            } else {
                // For game-specific: restore to global settings
                if (m_original_settings) {
                    // Restore from backup
                    *m_emu_settings = *m_original_settings;

                    // Delete game-specific config file
                    if (!m_game_serial.empty()) {
                        const auto gamePath =
                            Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                            (m_game_serial + ".json");
                        std::filesystem::remove(gamePath);
                    }

                    // Update UI
                    LoadValuesFromConfig();

                    QMessageBox::information(this, tr("Settings Restored"),
                                             tr("Game settings restored to global defaults."));
                }
            }
            Q_EMIT EmuSettingsApplied();
            return;
        }

        // CLOSE
        if (button == closeBtn) {
            close();
            LogUpdateLevels();
            Q_EMIT EmuSettingsApplied();
            return;
        }
    });

    ui->buttonBox->button(QDialogButtonBox::Save)->setText(tr("Save"));
    ui->buttonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));
    ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Restore Defaults"));
    ui->buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));

    connect(ui->tabWidgetSettings, &QTabWidget::currentChanged, this,
            [this]() { ui->buttonBox->button(QDialogButtonBox::Close)->setFocus(); });
}

void SettingsDialog::PopulateComboBoxes() {
    // Themes / stylesheets
    ui->themeComboBox->addItem(tr("Default"), GUI::DefaultStylesheet);
    ui->themeComboBox->addItem(tr("None"), GUI::NoStylesheet);
    for (const QString& style : QStyleFactory::keys()) {
        const QString display = GUI::NativeStylesheet + " (" + style + ")";
        ui->themeComboBox->addItem(display, display);
    }
    for (const QString& entry : m_gui_settings->GetStylesheetEntries()) {
        ui->themeComboBox->addItem(entry, entry);
    }
}

bool SettingsDialog::IsSettingOverrideable(const char* setting_key,
                                           const QString& setting_group) const {
    // Check if the setting is in the overrideable list for the given group.
    // Only the groups the launcher still configures are listed here.
    const auto contains = [setting_key](const std::vector<OverrideItem>& fields) {
        for (const auto& item : fields) {
            if (std::string(item.key) == setting_key) {
                return true;
            }
        }
        return false;
    };

    if (setting_group == "General") {
        return contains(m_emu_settings->GetGeneralOverrideableFields());
    }
    if (setting_group == "Log") {
        return contains(m_emu_settings->GetLogOverrideableFields());
    }
    if (setting_group == "Debug") {
        return contains(m_emu_settings->GetDebugOverrideableFields());
    }

    return false;
}

void SettingsDialog::MapUIControls() {
    // Log is the only group that still exposes per-game overrides; the other
    // groups were removed from the launcher's config.
    m_uiSettingMap[ui->logAppendCheckBox] = {"append", "Log"};
    m_uiSettingMap[ui->enableLoggingCheckBox] = {"enable", "Log"};
    m_uiSettingMap[ui->logFilterLineEdit] = {"filter", "Log"};
    m_uiSettingMap[ui->logMaxSkipDurationLineEdit] = {"max_skip_duration", "Log"};
    m_uiSettingMap[ui->separateLogFilesCheckbox] = {"separate", "Log"};
    m_uiSettingMap[ui->logSizeLimitLineEdit] = {"size_limit", "Log"};
    m_uiSettingMap[ui->logSkipDuplicateCheckBox] = {"skip_duplicate", "Log"};
    m_uiSettingMap[ui->logSyncCheckBox] = {"sync", "Log"};
#ifdef _WIN32
    m_uiSettingMap[ui->logTypeComboBox] = {"type", "Log"};
#endif
}

void SettingsDialog::DisableNonOverrideableSettings() {
    if (m_game_serial.empty()) {
        // Global settings dialog - don't disable anything
        return;
    }

    // For game-specific settings dialog, disable non-overrideable controls
    for (auto it = m_uiSettingMap.begin(); it != m_uiSettingMap.end(); ++it) {
        QObject* control = it.key();
        const char* setting_key = it.value().first;
        const QString& setting_group = it.value().second;

        if (!IsSettingOverrideable(setting_key, setting_group)) {
            QWidget* widget = qobject_cast<QWidget*>(control);
            if (widget) {
                widget->setEnabled(false);
                widget->setToolTip(tr("This setting cannot be overridden per-game. "
                                      "Use global settings to change it."));

                // For checkboxes and comboboxes, also set visual cue
                QCheckBox* checkbox = qobject_cast<QCheckBox*>(control);
                if (checkbox) {
                    checkbox->setStyleSheet("QCheckBox:disabled { color: gray; }");
                }

                QComboBox* combo = qobject_cast<QComboBox*>(control);
                if (combo) {
                    combo->setStyleSheet("QComboBox:disabled { color: gray; }");
                }

                QSpinBox* spin = qobject_cast<QSpinBox*>(control);
                if (spin) {
                    spin->setStyleSheet("QSpinBox:disabled { color: gray; }");
                }

                QSlider* slider = qobject_cast<QSlider*>(control);
                if (slider) {
                    slider->setStyleSheet("QSlider:disabled { color: gray; }");
                }
            }
        }
    }

    // Special handling for controls not directly mapped
    // GUI-only controls (not in emulator settings)
    QList<QObject*> guiOnlyControls = {
        ui->playBGMCheckBox, ui->BGMVolumeSlider,   ui->checkCompatibilityOnStartupCheckBox,
        ui->updaterCheckBox, ui->changelogCheckBox, ui->ScanDepthComboBox};

    for (QObject* control : guiOnlyControls) {
        QWidget* widget = qobject_cast<QWidget*>(control);
        if (widget) {
            widget->setEnabled(false);
            widget->setToolTip(tr("GUI-only settings cannot be overridden per-game. "
                                  "Use global settings to change them."));
        }
    }

    // Path controls (always global)
    QList<QObject*> pathControls = {
        ui->gameFoldersListWidget, ui->addFolderButton,       ui->removeFolderButton,
        ui->currentDLCFolder,      ui->browseDLCButton,       ui->currentHomePath,
        ui->browseHomeButton,      ui->currentSysmodulesPath, ui->browseSysmodulesButton,
        ui->gameFoldersGroupBox,   ui->dlcFolderGroupBox,     ui->homeGroupBox,
        ui->sysmodulesGroupBox};

    for (QObject* control : pathControls) {
        QWidget* widget = qobject_cast<QWidget*>(control);
        if (widget) {
            widget->setEnabled(false);
            widget->setToolTip(tr("Path settings cannot be overridden per-game. "
                                  "Use global settings to change them."));
        }
    }
}
