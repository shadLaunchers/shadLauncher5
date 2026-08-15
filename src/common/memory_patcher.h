// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>
#include "common/types.h"

namespace MemoryPatcher {

enum PatchMask : u8 {
    None,
    Mask,
    Mask_Jump32,
};

struct PendingPatch {
    std::string modName;
    std::string address;
    std::string value;
    std::string target;
    std::string size;
    bool littleEndian = false;
    PatchMask mask = PatchMask::None;
    int maskOffset = 0;
};

std::string convertValueToHex(std::string type, std::string valueStr);

std::vector<PendingPatch> readPatches(std::string gameSerial, std::string appVersion);

} // namespace MemoryPatcher