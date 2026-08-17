// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QActionGroup>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QList>
#include <QMainWindow>
#include <QMimeData>
#include <QUrl>

#include "core/ipc/ipc_client.h"
#include "gui_game_info.h"

class EmulatorSettingsImpl;
class GUISettings;
class GameListFrame;
class IpcClient;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
    std::unique_ptr<Ui::MainWindow> ui;
    bool m_save_slider_pos = false;
    bool m_is_list_mode = true;
    int m_other_slider_pos = 0;

public:
    MainWindow(std::shared_ptr<GUISettings> gui_settings,
               std::shared_ptr<EmulatorSettingsImpl> emu_settings,
               std::shared_ptr<IpcClient> ipc_client, QWidget* parent = nullptr);
    ~MainWindow();
    bool init();

    std::shared_ptr<IpcClient> m_ipc_client;

Q_SIGNALS:
    void requestLanguageChange(const QString& language);
    void RequestGlobalStylesheetChange();

public Q_SLOTS:
    void retranslateUI(const QStringList& language_codes, const QString& language_code);
    void resizeIcons(int index);
    void setIconSizeActions(int idx) const;
    void RepaintGUI();
    void StartGameWithArgs(const game_info& game, QStringList args = {});
    void StartEmulator(std::filesystem::path path, QStringList args = {});
    void RestartGame();
    void PauseGame();
    void StopGame();
    void ToggleFullscreen();
    void StartEmulatorExecutable(QString emulatorArg, QString gameArg, QStringList passed_args);

private Q_SLOTS:
    void saveWindowState() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void showTitleBars(bool show) const;
    void configureGuiFromSettings();
    void createDockWindows();
    void createActions();
    void createConnects();
    void LoadVersionComboBox();
    void updateLanguageActions(const QStringList& language_codes, const QString& language_code);
    void RunGame();
    void onGameClosed();
    void RestartEmulator();

    void CacheOriginalToolbarIcons();
    void RepaintToolbarIcons();
    void CacheOriginalMenuIcons();
    void RepaintMenuIcons();
    static bool IsMonochromeIcon(const QIcon& icon);
    static QIcon ColorizeIcon(const QIcon& source, const QColor& color);
    QHash<QAction*, QIcon> m_original_toolbar_icons;
    QHash<QAction*, QIcon> m_original_menu_icons;
    QLabel* m_toolbar_icon_color_label = nullptr;
    QLabel* m_thumbnail_icon_color_label = nullptr;
    // Permanent status bar widget showing "N games" / "N of M games".
    QLabel* m_game_count_label = nullptr;

    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<EmulatorSettingsImpl> m_emu_settings;
    QMainWindow* m_mw = nullptr;
    GameListFrame* m_game_list_frame = nullptr;
    QActionGroup* m_icon_size_act_group = nullptr;
    QActionGroup* m_list_mode_act_group = nullptr;

    // IPC things
    game_info last_game_info;
    bool is_paused;
};
