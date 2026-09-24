// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/input.h"

namespace GamepadSelect {

namespace {
std::string SelectedGamepad;
} // namespace

int GetIndexfromGUID(SDL_JoystickID* gamepadIDs, int gamepadCount, std::string GUID) {
    if (gamepadIDs == nullptr || GUID.empty()) {
        return -1;
    }
    char GUIDbuf[33];
    for (int i = 0; i < gamepadCount; i++) {
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(gamepadIDs[i]), GUIDbuf, 33);
        if (std::string(GUIDbuf) == GUID) {
            return i;
        }
    }
    return -1;
}

std::string GetGUIDString(SDL_JoystickID* gamepadIDs, int index) {
    if (gamepadIDs == nullptr || index < 0) {
        return {};
    }
    char GUIDbuf[33];
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(gamepadIDs[index]), GUIDbuf, 33);
    return std::string(GUIDbuf);
}

std::string GetSelectedGamepad() {
    return SelectedGamepad;
}

void SetSelectedGamepad(std::string GUID) {
    SelectedGamepad = std::move(GUID);
}

} // namespace GamepadSelect
