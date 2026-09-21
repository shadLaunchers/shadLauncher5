// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

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

    [[nodiscard]] SDL_Gamepad* Gamepad() const {
        return m_gamepad;
    }
    [[nodiscard]] bool HasGamepad() const {
        return m_gamepad != nullptr;
    }
    [[nodiscard]] QString SelectedGuid() const;
    [[nodiscard]] QString SelectedSerial() const;
    [[nodiscard]] QString SelectedPath() const;
    [[nodiscard]] bool SelectionIsAmbiguous() const;

    [[nodiscard]] QString SelectedName() const;
    bool SelectByGuid(const QString& guid);
    void HideChooser();

signals:
    void ControlPressed(const QString& name);
    void ControlReleased(const QString& name);

    // The list changed or a different pad was chosen.
    void SelectionChanged();

private slots:
    void OnCurrentIndexChanged(int index);
    void OnSdlEvent(int type, int input, int value);

private:
    void RefreshGamepadList();
    void OpenSelected(int index);
    void CloseGamepad();

    QComboBox* m_box = nullptr;
    QLabel* m_id_label = nullptr;

    SDL_Gamepad* m_gamepad = nullptr;
    SDL_JoystickID* m_gamepads = nullptr;
    int m_gamepad_count = 0;
    bool m_refreshing = false;
    bool m_l2_pressed = false;
    bool m_r2_pressed = false;
};

[[nodiscard]] QString GamepadButtonName(SDL_GamepadButton button);
[[nodiscard]] QString GamepadAxisName(SDL_GamepadAxis axis);
[[nodiscard]] QString GamepadNameForGuid(const QString& guid);
