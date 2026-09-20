// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Which physical pad the launcher's input UI is talking to. Ported from
// shadLauncher4's src/common/input.h so the two agree on what identifies a
// controller: SDL's GUID string, the same identifier the emulator stores in
// users.json's device_guid (see src/core/user_manager.h) and that
// IpcClient::setActiveController carries.
//
// A GUID names a controller *model*, not a unit -- two identical pads share
// one. That is fine here: this is the "which pad am I binding right now"
// selection, not the per-port device pinning the emulator does.

#pragma once

#include <string>
#include <SDL3/SDL_gamepad.h>

namespace GamepadSelect {

// Index of `GUID` within `gamepadIDs`, or -1 if that controller is not
// currently connected.
int GetIndexfromGUID(SDL_JoystickID* gamepadIDs, int gamepadCount, std::string GUID);

// The GUID string of the controller at `index`. `index` must be in range.
std::string GetGUIDString(SDL_JoystickID* gamepadIDs, int index);

// The pad the input UI is currently pointed at, as a GUID string. Empty
// until something selects one.
std::string GetSelectedGamepad();
void SetSelectedGamepad(std::string GUID);

} // namespace GamepadSelect
