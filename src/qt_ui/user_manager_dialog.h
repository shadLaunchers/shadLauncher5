// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <memory>
#include <string>
#include <QDialog>
#include <QTableWidget>
#include <core/user_manager.h>

class GUISettings;
class EmulatorSettingsImpl;
class IpcClient;

class UserManagerDialog : public QDialog {
    Q_OBJECT

public:
    UserManagerDialog(std::shared_ptr<GUISettings> gui_settings,
                      std::shared_ptr<EmulatorSettingsImpl> emulator_settings,
                      std::shared_ptr<IpcClient> ipc_client, bool is_game_running,
                      std::string running_serial, QWidget* parent = nullptr);

private Q_SLOTS:
    void OnUserCreate();
    void OnUserRemove();
    void OnUserRename();
    void OnUserSetDefault();
    void OnUserSetColor();
    void OnUserSetControllerPort();
    void OnUserAssignDevice();
    void OnUserClearPinnedDevice();
    void OnSort(int logicalIndex);

private:
    QColor GetQColorFromIndex(int index) {
        switch (index) {
        case 1:
            return Qt::blue;
        case 2:
            return Qt::red;
        case 3:
            return Qt::green;
        case 4:
            return Qt::magenta;
        default:
            return Qt::gray;
        }
    }
    QString GetColorName(int index) {
        switch (index) {
        case 1:
            return "Blue";
        case 2:
            return "Red";
        case 3:
            return "Green";
        case 4:
            return "Pink";
        default:
            return "Unknown";
        }
    }
    static QString DescribePinnedDevice(const User& user);
    void UpdateTable(bool mark_only = false);
    u32 GetUserKey() const;
    void ShowContextMenu(const QPoint& pos);
    void closeEvent(QCloseEvent* event) override;

    QTableWidget* m_table = nullptr;
    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<EmulatorSettingsImpl> m_emu_settings;
    std::shared_ptr<IpcClient> m_ipc_client;
    bool m_game_running = false;
    std::string m_running_serial;

    void ReloadUsers();
    void ReloadRunningGame();
    void NoteAppliesNextLaunch();
    int m_active_user;

    QPushButton* push_create_user;
    QPushButton* push_remove_user;
    QPushButton* push_rename_user;
    QPushButton* push_set_default;
    QPushButton* push_set_color;
    QPushButton* push_set_controller;
    QPushButton* push_assign_device;
    QPushButton* push_clear_device;
    QPushButton* push_close;

    int m_sort_column = 1;
    bool m_sort_ascending = true;
};
