// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// "Which pad am I binding?" -- the combo box and GUID label from
// shadLauncher4's ControlSettings (ActiveGamepadBox / ActiveGamepadLabel in
// control_settings.ui), as a widget rather than part of one dialog, because
// two editors here ask the same question.
//
// The selection itself lives in GamepadSelect (src/common/input.h), process
// wide and keyed by GUID, exactly as in shadLauncher4 -- so picking a pad in
// the bindings editor is the pad the hotkeys editor captures from too, and
// the GUID is the one users.json and IpcClient::setActiveController already
// speak.
//
// It also re-broadcasts what the selected pad is doing, so a dialog can show
// presses live without opening the device a second time.

#pragma once

#include <QString>
#include <QWidget>
#include <SDL3/SDL_gamepad.h>

class QComboBox;
class QLabel;

class GamepadSelector : public QWidget {
    Q_OBJECT
public:
    explicit GamepadSelector(QWidget* parent = nullptr);
    ~GamepadSelector() override;

    // The open SDL handle for the selected pad, or nullptr when none is
    // connected. Not owned by the caller.
    [[nodiscard]] SDL_Gamepad* Gamepad() const {
        return m_gamepad;
    }
    [[nodiscard]] bool HasGamepad() const {
        return m_gamepad != nullptr;
    }

    // What users.json needs to pin this device, as the emulator stores it
    // (src/bridge/core/input_devices.h). The GUID is the primary key; the
    // serial and the path are optional narrowers, and both are commonly
    // empty -- only HIDAPI-backed pads report a serial, and the path names
    // the socket rather than the pad. Empty GUID means nothing is selected.
    [[nodiscard]] QString SelectedGuid() const;
    [[nodiscard]] QString SelectedSerial() const;
    [[nodiscard]] QString SelectedPath() const;

    // The selected pad reports neither a serial nor a path, so nothing
    // separates it from another of the same model. input_devices.h keeps
    // IsAmbiguous() "so the assignment UI can say so rather than storing
    // something that will not match next time" -- this is that check, on
    // the launcher's side of the same question.
    [[nodiscard]] bool SelectionIsAmbiguous() const;

    // The human name of the selected pad, for confirmations.
    [[nodiscard]] QString SelectedName() const;

signals:
    // A button went down or up, or an axis moved, on the selected pad.
    // `name` is an input-vocabulary name (input_ids.h) -- "cross", "l2",
    // "axis_left_x" -- or empty for a control the vocabulary has no name for.
    void ControlPressed(const QString& name);
    void ControlReleased(const QString& name);

    // The list changed or a different pad was chosen.
    void SelectionChanged();

private slots:
    void OnCurrentIndexChanged(int index);
    void OnSdlEvent(int type, int input, int value);

private:
    // Rebuilds the combo from SDL and opens whichever pad should be active,
    // preferring the one already selected by GUID. shadLauncher4's
    // ControlSettings::CheckGamePad.
    void RefreshGamepadList();
    void OpenSelected(int index);
    void CloseGamepad();

    QComboBox* m_box = nullptr;
    QLabel* m_id_label = nullptr;

    SDL_Gamepad* m_gamepad = nullptr;
    SDL_JoystickID* m_gamepads = nullptr;
    int m_gamepad_count = 0;
    bool m_refreshing = false; // guards the combo's own change signal

    // Triggers are axes, so their "pressed" edge has to be tracked to emit
    // one press and one release rather than a stream. shadLauncher4 uses the
    // same two thresholds (control_settings.cpp): half travel to press, and a
    // lower one to release so a resting trigger cannot chatter.
    bool m_l2_pressed = false;
    bool m_r2_pressed = false;
};

// SDL3 gamepad button -> input-vocabulary name, and axis -> name. Shared so
// the selector and the capture dialog cannot drift apart on what a control is
// called. Empty when the vocabulary has no name for it.
[[nodiscard]] QString GamepadButtonName(SDL_GamepadButton button);
[[nodiscard]] QString GamepadAxisName(SDL_GamepadAxis axis);
