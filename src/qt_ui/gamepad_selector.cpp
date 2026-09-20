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

namespace {

// shadLauncher4's thresholds (control_settings.cpp): "SDL trigger axis values
// range from 0 to 32000, set mapping on half movement. Set zone for trigger
// release signal arbitrarily at 5000." Two values, not one, so a trigger
// resting near the edge cannot chatter press/release.
constexpr int kTriggerPress = 16000;
constexpr int kTriggerRelease = 5000;

// A stick has to be pushed past half travel to count as a deliberate
// movement -- same number shadLauncher4 uses for axis mapping.
constexpr int kAxisPress = 16000;

} // namespace

QString GamepadButtonName(SDL_GamepadButton button) {
    // Matches shadLauncher4's ControlSettings::processSDLEvents, and through
    // it the emulator's own table in src/core/input/input_ids.cpp -- SDL3's
    // LEFT_PADDLE1 is SDL2's PADDLE2, which the emulator calls lpaddle_high,
    // and so on for the other three.
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return QStringLiteral("cross");
    case SDL_GAMEPAD_BUTTON_EAST: return QStringLiteral("circle");
    case SDL_GAMEPAD_BUTTON_WEST: return QStringLiteral("square");
    case SDL_GAMEPAD_BUTTON_NORTH: return QStringLiteral("triangle");
    case SDL_GAMEPAD_BUTTON_BACK: return QStringLiteral("back");
    case SDL_GAMEPAD_BUTTON_START: return QStringLiteral("options");
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return QStringLiteral("l3");
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return QStringLiteral("r3");
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return QStringLiteral("l1");
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return QStringLiteral("r1");
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return QStringLiteral("pad_up");
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return QStringLiteral("pad_down");
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return QStringLiteral("pad_left");
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return QStringLiteral("pad_right");
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return QStringLiteral("touchpad_center");
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return QStringLiteral("lpaddle_high");
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return QStringLiteral("lpaddle_low");
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return QStringLiteral("rpaddle_high");
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return QStringLiteral("rpaddle_low");
    // The emulator maps "qam" to SDL2's MISC1 (input_ids.cpp); shadLauncher4
    // has no entry for it, so this one is ours.
    case SDL_GAMEPAD_BUTTON_MISC1: return QStringLiteral("qam");
    default: return {};
    }
}

QString GamepadAxisName(SDL_GamepadAxis axis) {
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: return QStringLiteral("axis_left_x");
    case SDL_GAMEPAD_AXIS_LEFTY: return QStringLiteral("axis_left_y");
    case SDL_GAMEPAD_AXIS_RIGHTX: return QStringLiteral("axis_right_x");
    case SDL_GAMEPAD_AXIS_RIGHTY: return QStringLiteral("axis_right_y");
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return QStringLiteral("l2");
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return QStringLiteral("r2");
    default: return {};
    }
}

GamepadSelector::GamepadSelector(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("Pad:"), this));
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
    // shadLauncher4's CheckGamePad, minus its "default controller" row: this
    // launcher has no such setting yet, so the remembered selection is the
    // only preference there is.
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
        m_box->addItem(tr("No pads detected"));
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
        m_box->addItem(QStringLiteral("%1: %2")
                           .arg(QString::number(i + 1),
                                QString::fromUtf8(name == nullptr ? "Unknown" : name)));
    }

    // Keep the pad that was already chosen if it is still plugged in; fall
    // back to the first one. Selection is by GUID rather than index, so
    // unplugging a *different* pad does not silently move it.
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
    // Last 16 of the GUID, as shadLauncher4 shows it -- the leading half is
    // bus and vendor, the same for every pad of a kind.
    m_id_label->setText(
        tr("ID: %1").arg(QString::fromStdString(GamepadSelect::GetSelectedGamepad()).right(16)));

    m_l2_pressed = false;
    m_r2_pressed = false;
    emit SelectionChanged();
}

void GamepadSelector::OnCurrentIndexChanged(int index) {
    if (m_refreshing) {
        return; // our own repopulation, not the person choosing
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
            if (value > kTriggerPress && !held) {
                held = true;
                emit ControlPressed(name);
            } else if (value < kTriggerRelease && held) {
                held = false;
                emit ControlReleased(name);
            }
            break;
        }
        // A stick reports continuously; "pressed" here means pushed past
        // half travel, and anything less is a release, which is what makes
        // the live view settle when the stick is let go.
        if (value > kAxisPress || value < -kAxisPress) {
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
