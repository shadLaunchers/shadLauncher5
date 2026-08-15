// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// History:
//   2026-01-02  Copied from shadPS4 Emulator Project (v0.13.0)
//   2026-07-20  Replaced binary PS3/PS4 "param.sfo" parsing with PS5
//               "param.json" parsing. PS4 ".pkg" support (the only other
//               consumer of the binary SFO container format) has been
//               removed, so the binary encoder/decoder went with it.

#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "common/endian.h"

enum class PSFEntryFmt : u16 {
    Binary = 0x0004,  // Binary data
    Text = 0x0204,    // String in UTF-8 format and NULL terminated
    Integer = 0x0404, // Signed 32-bit integer
};

class PSF {
    struct Entry {
        std::string key;
        PSFEntryFmt param_fmt;
        u32 max_len;
    };

public:
    PSF() = default;
    ~PSF() = default;

    PSF(const PSF& other) = default;
    PSF(PSF&& other) noexcept = default;
    PSF& operator=(const PSF& other) = default;
    PSF& operator=(PSF&& other) noexcept = default;

    // Loads a PS5 "param.json" file from disk and normalizes it into the
    // same key/value entries the rest of the codebase already knows how to
    // read (TITLE, TITLE_ID, CONTENT_ID, SYSTEM_VER, APP_VER, CATEGORY,
    // ...).
    bool Open(const std::filesystem::path& filepath);

    std::optional<std::span<const u8>> GetBinary(std::string_view key) const;
    std::optional<std::string_view> GetString(std::string_view key) const;
    std::optional<s32> GetInteger(std::string_view key) const;

    void AddBinary(std::string key, std::vector<u8> value, bool update = false);
    void AddBinary(std::string key, uint64_t value, bool update = false); // rsv4 format
    void AddString(std::string key, std::string value, bool update = false);
    void AddInteger(std::string key, s32 value, bool update = false);

    [[nodiscard]] std::chrono::system_clock::time_point GetLastWrite() const {
        return last_write;
    }

    [[nodiscard]] const std::vector<Entry>& GetEntries() const {
        return entry_list;
    }

private:
    mutable std::chrono::system_clock::time_point last_write;

    std::vector<Entry> entry_list;

    std::unordered_map<size_t, std::vector<u8>> map_binaries;
    std::unordered_map<size_t, std::string> map_strings;
    std::unordered_map<size_t, s32> map_integers;

    [[nodiscard]] std::pair<std::vector<Entry>::iterator, size_t> FindEntry(std::string_view key);
    [[nodiscard]] std::pair<std::vector<Entry>::const_iterator, size_t> FindEntry(
        std::string_view key) const;

    // Parses a PS5 "param.json" buffer and normalizes it into PSF-style
    // key/value entries. See psf.cpp for the field mapping table and its
    // caveats.
    bool OpenJson(const std::vector<u8>& json_buffer);
};
