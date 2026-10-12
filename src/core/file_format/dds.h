// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "common/types.h"

namespace Dds {

struct Image {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba; // width * height * 4
    std::string format;   // "BC7", "BC1", "B8G8R8A8", ...
};

[[nodiscard]] bool IsDds(std::span<const u8> data);

[[nodiscard]] std::optional<Image> Decode(std::span<const u8> data, std::string* problem = nullptr);

} // namespace Dds
