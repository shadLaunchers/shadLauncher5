// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <SDL3/SDL.h>

#include "common/input.h"
#include "common/logging/log.h"
#include "gamepad_selector.h"
#include "sdl_event_wrapper.h"

constexpr int TriggerPress = 16000;
constexpr int TriggerRelease = 5000;
constexpr int AxisPress = 16000;

QString GamepadButtonName(SDL_GamepadButton button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return QStringLiteral("cross");
    case SDL_GAMEPAD_BUTTON_EAST:
        return QStringLiteral("circle");
    case SDL_GAMEPAD_BUTTON_WEST:
        return QStringLiteral("square");
    case SDL_GAMEPAD_BUTTON_NORTH:
        return QStringLiteral("triangle");
    case SDL_GAMEPAD_BUTTON_BACK:
        return QStringLiteral("back");
    case SDL_GAMEPAD_BUTTON_START:
        return QStringLiteral("options");
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return QStringLiteral("l3");
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return QStringLiteral("r3");
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return QStringLiteral("l1");
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return QStringLiteral("r1");
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return QStringLiteral("pad_up");
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return QStringLiteral("pad_down");
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return QStringLiteral("pad_left");
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return QStringLiteral("pad_right");
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
        return QStringLiteral("touchpad_center");
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
        return QStringLiteral("lpaddle_high");
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
        return QStringLiteral("lpaddle_low");
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
        return QStringLiteral("rpaddle_high");
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
        return QStringLiteral("rpaddle_low");
    case SDL_GAMEPAD_BUTTON_MISC1:
        return QStringLiteral("qam");
    default:
        return {};
    }
}

QString GamepadAxisName(SDL_GamepadAxis axis) {
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        return QStringLiteral("axis_left_x");
    case SDL_GAMEPAD_AXIS_LEFTY:
        return QStringLiteral("axis_left_y");
    case SDL_GAMEPAD_AXIS_RIGHTX:
        return QStringLiteral("axis_right_x");
    case SDL_GAMEPAD_AXIS_RIGHTY:
        return QStringLiteral("axis_right_y");
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return QStringLiteral("l2");
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return QStringLiteral("r2");
    default:
        return {};
    }
}

QString GamepadNameForGuid(const QString& guid) {
    if (guid.isEmpty()) {
        return {};
    }
    if (guid == QStringLiteral("keyboard")) {
        return QObject::tr("Keyboard and Mouse");
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids == nullptr) {
        return {};
    }
    QString name;
    const int index = GamepadSelect::GetIndexfromGUID(ids, count, guid.toStdString());
    if (index >= 0) {
        const char* text = SDL_GetGamepadNameForID(ids[index]);
        name = text == nullptr ? QString() : QString::fromUtf8(text);
    }
    SDL_free(ids);
    return name;
}

GamepadSelector::GamepadSelector(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("Controller:"), this));
    m_box = new QComboBox(this);
    m_box->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    layout->addWidget(m_box, 1);
    m_id_label = new QLabel(this);
    layout->addWidget(m_id_label);

    SdlEventWrapper::Wrapper::Acquire();
    connect(SdlEventWrapper::Wrapper::GetInstance(), &SdlEventWrapper::Wrapper::SDLEvent, this,
            &GamepadSelector::OnSdlEvent);

    RefreshGamepadList();
    connect(m_box, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &GamepadSelector::OnCurrentIndexChanged);
}

GamepadSelector::~GamepadSelector() {
    CloseGamepad();
    if (m_gamepads != nullptr) {
        SDL_free(m_gamepads);
        m_gamepads = nullptr;
    }
    SdlEventWrapper::Wrapper::Release();
}

void GamepadSelector::CloseGamepad() {
    if (m_gamepad != nullptr) {
        SDL_CloseGamepad(m_gamepad);
        m_gamepad = nullptr;
    }
}

void GamepadSelector::RefreshGamepadList() {
    CloseGamepad();
    if (m_gamepads != nullptr) {
        SDL_free(m_gamepads);
        m_gamepads = nullptr;
    }
    m_gamepads = SDL_GetGamepads(&m_gamepad_count);
    if (m_gamepads == nullptr) {
        LOG_ERROR(Frontend, "Cannot get gamepad list: {}", SDL_GetError());
    }

    m_refreshing = true;
    m_box->clear();

    if (m_gamepads == nullptr || m_gamepad_count == 0) {
        m_box->addItem(tr("No controllers detected"));
        m_box->setCurrentIndex(0);
        m_box->setEnabled(false);
        m_id_label->clear();
        m_refreshing = false;
        emit SelectionChanged();
        return;
    }

    m_box->setEnabled(true);
    for (int i = 0; i < m_gamepad_count; i++) {
        const char* name = SDL_GetGamepadNameForID(m_gamepads[i]);
        m_box->addItem(QStringLiteral("%1: %2").arg(
            QString::number(i + 1), QString::fromUtf8(name == nullptr ? "Unknown" : name)));
    }

    int index = GamepadSelect::GetIndexfromGUID(m_gamepads, m_gamepad_count,
                                                GamepadSelect::GetSelectedGamepad());
    if (index == -1) {
        index = 0;
    }
    m_box->setCurrentIndex(index);
    m_refreshing = false;
    OpenSelected(index);
}

void GamepadSelector::OpenSelected(int index) {
    CloseGamepad();
    if (m_gamepads == nullptr || index < 0 || index >= m_gamepad_count) {
        m_id_label->clear();
        emit SelectionChanged();
        return;
    }

    GamepadSelect::SetSelectedGamepad(GamepadSelect::GetGUIDString(m_gamepads, index));
    m_gamepad = SDL_OpenGamepad(m_gamepads[index]);
    if (m_gamepad == nullptr) {
        LOG_ERROR(Frontend, "Failed to open gamepad {}: {}", index, SDL_GetError());
    }
    m_id_label->setText(
        tr("ID: %1").arg(QString::fromStdString(GamepadSelect::GetSelectedGamepad()).right(16)));

    m_l2_pressed = false;
    m_r2_pressed = false;
    emit SelectionChanged();
}

QString GamepadSelector::SelectedGuid() const {
    return QString::fromStdString(GamepadSelect::GetSelectedGamepad());
}

QString GamepadSelector::SelectedSerial() const {
    if (m_gamepad == nullptr) {
        return {};
    }
    const char* serial = SDL_GetGamepadSerial(m_gamepad);
    return serial == nullptr ? QString() : QString::fromUtf8(serial);
}

QString GamepadSelector::SelectedPath() const {
    if (m_gamepad == nullptr) {
        return {};
    }
    const char* path = SDL_GetGamepadPath(m_gamepad);
    return path == nullptr ? QString() : QString::fromUtf8(path);
}

bool GamepadSelector::SelectionIsAmbiguous() const {
    return m_gamepad != nullptr && SelectedSerial().isEmpty() && SelectedPath().isEmpty();
}

QString GamepadSelector::SelectedName() const {
    if (m_gamepad == nullptr) {
        return {};
    }
    const char* name = SDL_GetGamepadName(m_gamepad);
    return name == nullptr ? QString() : QString::fromUtf8(name);
}

bool GamepadSelector::SelectByGuid(const QString& guid) {
    if (guid.isEmpty() || m_gamepads == nullptr) {
        return false;
    }
    const int index =
        GamepadSelect::GetIndexfromGUID(m_gamepads, m_gamepad_count, guid.toStdString());
    if (index < 0) {
        return false;
    }
    if (index == m_box->currentIndex() && m_gamepad != nullptr) {
        return true; // already the open one; reopening would drop held state
    }
    m_refreshing = true;
    m_box->setCurrentIndex(index);
    m_refreshing = false;
    OpenSelected(index);
    return true;
}

void GamepadSelector::HideChooser() {
    m_box->hide();
    m_id_label->hide();
    for (auto* label : findChildren<QLabel*>()) {
        if (label != m_id_label) {
            label->hide();
        }
    }
}

void GamepadSelector::OnCurrentIndexChanged(int index) {
    if (m_refreshing) {
        return;
    }
    OpenSelected(index);
}

void GamepadSelector::OnSdlEvent(int type, int input, int value) {
    if (type == SDL_EVENT_GAMEPAD_ADDED || type == SDL_EVENT_GAMEPAD_REMOVED) {
        RefreshGamepadList();
        return;
    }
    if (m_gamepad == nullptr) {
        return;
    }

    switch (type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (const QString name = GamepadButtonName(static_cast<SDL_GamepadButton>(input));
            !name.isEmpty()) {
            emit ControlPressed(name);
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (const QString name = GamepadButtonName(static_cast<SDL_GamepadButton>(input));
            !name.isEmpty()) {
            emit ControlReleased(name);
        }
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const auto axis = static_cast<SDL_GamepadAxis>(input);
        const QString name = GamepadAxisName(axis);
        if (name.isEmpty()) {
            break;
        }
        if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            bool& held = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? m_l2_pressed : m_r2_pressed;
            if (value > TriggerPress && !held) {
                held = true;
                emit ControlPressed(name);
            } else if (value < TriggerRelease && held) {
                held = false;
                emit ControlReleased(name);
            }
            break;
        }
        if (value > AxisPress || value < -AxisPress) {
            emit ControlPressed(name);
        } else {
            emit ControlReleased(name);
        }
        break;
    }
    default:
        break;
    }
}
