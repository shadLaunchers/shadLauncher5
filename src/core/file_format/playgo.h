// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string>
#include <vector>

#include "common/types.h"

struct PlayGoChunk {
    std::string label;
    u8 flag = 0;
    u8 disc_layer = 0;
    u8 required_locus = 0;
    u64 language_mask = 0; // all bits set: every language
};

struct PlayGoScenario {
    std::string label;
    u8 type = 0;
    u16 initial_chunk_count = 0; // chunks needed before the game can start
    std::vector<u16> chunks;
};

// A contiguous storage range of the package covered by the chunks.
struct PlayGoRange {
    u64 offset = 0;
    u64 size = 0;
};

struct PlayGoInfo {
    std::string magic; // "plgx" or "plgo"
    u16 version = 0;
    u16 image_count = 0;
    u16 default_scenario = 0;
    std::string content_id;
    std::vector<PlayGoChunk> chunks;
    std::vector<PlayGoScenario> scenarios;
    std::vector<PlayGoRange> ranges;
    std::string error;

    bool Parse(std::span<const u8> data);

    [[nodiscard]] static bool AllLanguages(u64 mask) {
        return mask == ~u64{0};
    }
};
