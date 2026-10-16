// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <span>
#include <string>

#include "common/types.h"

namespace Common::Kraken {

struct Result {
    bool ok = false;
    size_t consumed = 0; // compressed bytes read
    std::string error;
};

[[nodiscard]] Result Decompress(std::span<const u8> src, std::span<u8> dst);
[[nodiscard]] Result DecompressChunk(std::span<const u8> src, std::span<u8> window, size_t position,
                                     size_t size, bool lz, u32 literal_mode);

} // namespace Common::Kraken
