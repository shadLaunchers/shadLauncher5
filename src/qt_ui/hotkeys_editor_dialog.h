// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <QDialog>
#include <QListWidget>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>

#include "core/input/hotkeys_config.h"

class QLabel;
class IpcClient;

#ifdef _WIN32
#define LCTRL_KEY 29
#define LALT_KEY 56
#define LSHIFT_KEY 42
#else
#define LCTRL_KEY 37
#define LALT_KEY 64
#define LSHIFT_KEY 50
#endif

class KeyCaptureDialog : public QDialog {
    Q_OBJECT
public:
    enum class Accepts {
        Any,
        KeyboardAndMouse,
        Gamepad,
    };

    explicit KeyCaptureDialog(QWidget* parent = nullptr, Accepts accepts = Accepts::Any,
                              const QString& reason = {});
    ~KeyCaptureDialog() override;

    [[nodiscard]] const std::vector<std::string>& CapturedInput() const {
        return m_captured;
    }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(class QWheelEvent* event) override;

private:
    void UpdatePreview();
    void OpenSelectedGamepad();
    void OnSdlEvent(int type, int input, int value);
    void TryCapture(const std::string& name);
    [[nodiscard]] bool Allows(const std::string& name) const;
    static std::string NameForKeyEvent(QKeyEvent* event);

    QLabel* m_preview_label = nullptr;
    QLabel* m_gamepad_label = nullptr;
    QLabel* m_rule_label = nullptr;
    Accepts m_accepts = Accepts::Any;
    QString m_reason;
    std::vector<std::string> m_captured;

    SDL_Gamepad* m_gamepad = nullptr;
    bool m_l2_pressed = false;
    bool m_r2_pressed = false;
    bool m_axis_captured = false;
};

class HotkeysEditorDialog : public QDialog {
    Q_OBJECT
public:
    HotkeysEditorDialog(std::shared_ptr<IpcClient> ipc_client, bool is_game_running,
                        std::string running_serial, QWidget* parent = nullptr);

private slots:
    void OnAddWay();
    void OnRemoveSelected();
    void OnSetUnmapped();
    void OnResetToDefaults();
    void OnSave();

protected:
    void closeEvent(class QCloseEvent* event) override;
    void reject() override;

private:
    [[nodiscard]] bool ConfirmDiscardingEdits();
    bool SaveAndReload();
    void ReloadRunningGame();
    void PopulateHotkeyList();
    void RefreshBindingsList();
    void RefreshProblemsList();
    [[nodiscard]] std::string CurrentHotkeyName() const;
    [[nodiscard]] static QString DisplayChord(const std::vector<std::string>& input);

    QListWidget* m_hotkey_list = nullptr;
    QListWidget* m_bindings_list = nullptr;
    QLabel* m_default_label = nullptr;
    QListWidget* m_problems_list = nullptr;

    std::unique_ptr<Core::Input::HotkeysConfig> m_config;
    std::shared_ptr<IpcClient> m_ipc_client;
    bool m_game_running = false;
    std::string m_running_serial;
};
