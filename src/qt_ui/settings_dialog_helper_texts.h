// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>

class SettingsDialogHelperTexts : public QObject {
    Q_OBJECT

public:
    SettingsDialogHelperTexts();

    const struct settings {
        // clang-format off
        //general
        const QString general_updater = tr("GUI Updates:\\nRelease: Official versions released every month that may be very outdated, but are more reliable and tested.\\nNightly: Development versions that have all the latest features and fixes, but may contain bugs and are less stable.\\n\\n*This update applies only to the Qt user interface. To update the emulator core, please use the 'Version Manager' menu.");
        const QString general_updater_check_startup = tr("Check for Updates at Startup:\\nAutomatically check for a new launcher version each time it starts.");
        const QString general_updater_changelog = tr("Always Show Changelog:\\nDisplay the changelog dialog after installing an update, even for minor releases.");
        const QString general_updater_check_now = tr("Check for Updates:\\nManually check right now for a newer launcher version.");
        //paths
        const QString paths_gameDir = tr("Game Folders:\\nThe list of folders to check for installed games.");
        const QString paths_gameDir_add = tr("Add Folder:\\nAdd a new folder to the list of game installation folders.");
        const QString paths_gameDir_remove = tr("Remove Folder:\\nRemove the selected folder from the list of game installation folders.");
        const QString paths_dlcDir = tr("DLC Path:\\nThe folder where game DLC is loaded from.");
        const QString paths_dlcDir_browse = tr("Browse:\\nBrowse for a folder to set as the DLC path.");
        const QString paths_homeDir = tr("Home Folder:\\nThe folder where the emulator stores user data such as save files and trophies.");
        const QString paths_homeDir_browse = tr("Browse:\\nBrowse for a folder to set as the Home folder.");
        const QString paths_sysmodulesDir = tr("System Modules Folder:\\nThe folder where system modules are loaded from.");
        const QString paths_sysmodulesDir_browse = tr("Browse:\\nBrowse for a folder to set as the System Modules folder.");
        //log
        const QString log_filter = tr("Log Filter:\\nFilters the log to only print specific information.\\nExamples: \"Core:Debug\" \"Lib.Pad:Debug Common.Filesystem:Error\" \"*:Critical\"\\nLevels: trace, debug, info, warning, error, critical, off - in this order, a specific level silences all levels preceding it in the list and logs every level after it.");
        const QString log_enable = tr("Enable Logging:\\nEnables logging.\\nDo not change this if you do not know what you're doing!\\nWhen asking for help, make sure this setting is ENABLED.");
        const QString log_open_location = tr("Open Log Location:\\nOpen the folder where the log file is saved.");
        const QString log_separate_files = tr("Separate Log Files:\\nWrites a separate logfile for each game.");
        const QString log_sync = tr("Log Sync:\\nSwitch between sync (order) or async (performance).");
        const QString log_skip_duplicate = tr("Log Skip Duplicate:\\nSave storage by avoiding writing log that is identical.");
        const QString log_max_skip_duration = tr("Log Max Skip Duration:\\nInterval without writing same lines (ms) - only if 'Log Skip Duplicate' enabled.");
        const QString log_size_limit = tr("Log Size Limit:\\nMaximum size of log files (bytes).");
        const QString log_append = tr("Log Append:\\nAppend to existing logs.");
        const QString log_type = tr("Log Type:\\nChoose between wincolor or msvc log types.\\nwincolor: Default logging for Windows\\nmsvc: Logging for debugging");
        const QString log_section = tr("Log:\\nSettings that control what gets logged and how log files are written.");
        const QString log_presets = tr("Load Presets...:\\nChoose from a list of common log filter presets instead of typing one by hand.");
        //gui
        const QString general_scan_depth_combo = tr("Directory Scan Depth:\\nSet the maximum depth when scanning for games in the specified game folders.\\n1 means one level of subfolders is scanned, and so on.");
        const QString gui_music = tr("Play Title Music:\\nIf a game supports it, enable playing special music when selecting the game in the GUI.");
        const QString gui_music_volume = tr("Music Volume:\\nAdjust the volume of the background/title music played in the GUI.");
        const QString gui_theme = tr("Theme:\\nChoose the stylesheet used for the launcher's interface. Drop .qss stylesheet files into the \"themes\" folder inside your user data directory to add custom themes.");
        const QString gui_background_image = tr("Background Image:\\nControl the opacity of the game background image.");
        const QString gui_show_background_image = tr("Show Background Image:\\nDisplay a background image behind the game list. Use the Opacity slider below to control how visible it is.");
        //compatibility
        const QString compat_section = tr("Compatibility:\\nSettings for displaying and updating the game compatibility database.");
        const QString compat_check_on_startup = tr("Update Compatibility On Startup:\\nAutomatically update the compatibility database when shadPS4 starts.");
        const QString compat_update_button = tr("Update Compatibility Database:\\nImmediately update the compatibility database.");
        // clang-format on
    } settings;
};
