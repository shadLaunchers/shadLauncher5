// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Editor for <user dir>/hotkeys.json (docs/input-bindings.md). Deliberately
// scoped to just the ten known hotkeys -- see hotkeys_config.h for why this
// is a separate, narrower thing from the full per-game/global binding
// editor.
//
// Not wired into any menu yet: see the note where MainWindow would normally
// gain the menu action.

#pragma once

#include <QDialog>
#include <QListWidget>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>
#include <memory>

#include "core/input/hotkeys_config.h"

class QLabel;
class QTimer;

// Shown when the person clicks "Add a way..." for a hotkey. Captures up to
// three keys/mouse buttons/pad buttons held together and reports them as an
// ordered list of input-vocabulary names (docs/input-bindings.md section 7).
//
// Pad capture follows the same SDL_InitSubSystem(SDL_INIT_GAMEPAD |
// SDL_INIT_EVENTS) / SDL_QuitSubSystem lifecycle shadLauncher4's
// ControlSettings dialog uses (src/qt_ui/control_settings.cpp), scoped to
// this dialog's lifetime rather than the whole app's -- this dialog polls
// SDL on a QTimer on the UI thread instead of shadLauncher4's background
// SdlEventWrapper thread, since a short-lived modal capture doesn't need
// that machinery.
class KeyCaptureDialog : public QDialog {
    Q_OBJECT
public:
    explicit KeyCaptureDialog(QWidget* parent = nullptr);
    ~KeyCaptureDialog() override;

    // Valid only if the dialog was accepted.
    [[nodiscard]] const std::vector<std::string>& CapturedInput() const {
        return m_captured;
    }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void UpdatePreview();
    void OpenFirstGamepad();
    void PollGamepad();
    void TryCapture(const std::string& name);
    // Returns the input-vocabulary name for a key press, or empty if this
    // key isn't one the emulator's input vocabulary can name.
    static std::string NameForKeyEvent(QKeyEvent* event);
    // SDL_GamepadButton -> input-vocabulary name (docs/input-bindings.md
    // section 7, "Inputs -- pad"), same mapping shadLauncher4's
    // ControlSettings::processSDLEvents uses.
    static std::string NameForGamepadButton(SDL_GamepadButton button);

    QLabel* m_preview_label = nullptr;
    QLabel* m_gamepad_label = nullptr;
    std::vector<std::string> m_captured;

    QTimer* m_poll_timer = nullptr;
    SDL_Gamepad* m_gamepad = nullptr;
    bool m_l2_pressed = false;
    bool m_r2_pressed = false;
};

class HotkeysEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit HotkeysEditorDialog(QWidget* parent = nullptr);

private slots:
    void OnAddWay();
    void OnRemoveSelected();
    void OnSetUnmapped();
    void OnResetToDefaults();
    void OnSave();

private:
    void PopulateHotkeyList();
    void RefreshBindingsList();
    void RefreshProblemsList();
    // The hotkey name currently selected in the left-hand list, or empty.
    [[nodiscard]] std::string CurrentHotkeyName() const;
    [[nodiscard]] static QString DisplayChord(const std::vector<std::string>& input);

    QListWidget* m_hotkey_list = nullptr;
    QListWidget* m_bindings_list = nullptr;
    QLabel* m_default_label = nullptr;
    QListWidget* m_problems_list = nullptr;

    std::unique_ptr<Core::Input::HotkeysConfig> m_config;
};
