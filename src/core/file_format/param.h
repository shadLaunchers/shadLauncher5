// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"

enum class ParamValueType {
    String,
    Integer,
};

class Param {
public:
    Param() = default;
    ~Param() = default;

    Param(const Param&) = default;
    Param(Param&&) noexcept = default;
    Param& operator=(const Param&) = default;
    Param& operator=(Param&&) noexcept = default;

    bool Open(const std::filesystem::path& filepath);

    // Parses an already-loaded param.json buffer. Resets all fields first.
    bool Parse(std::string_view json_text);

    // Clears every parsed field, including last_write.
    void Reset();

    std::string title_id;         // "titleId",   e.g. "PPSA01234"
    std::string content_id;       // "contentId", e.g. "EP9000-PPSA01234_00-..."
    std::string title;            // titleName for `default_language`
    std::string default_language; // "localizedParameters.defaultLanguage"
    std::map<s32, std::string> localized_titles;
    s64 application_category_type = -1; // -1 = key absent
    std::string category;
    std::string app_ver;        // "contentVersion", e.g. "01.02"
    std::string master_version; // "masterVersion",  e.g. "01.00"
    u32 system_ver = 0;
    u32 sdk_ver = 0;
    std::string system_ver_string = "0.00";
    std::string sdk_ver_string = "0.00";

    // Last-write time of the file passed to Open().
    std::chrono::system_clock::time_point last_write{};

    // titleName for language_index, falling back to the default title
    // when the game doesn't ship that language.
    [[nodiscard]] const std::string& LocalizedTitle(s32 language_index) const;

    // A param.json without a titleId is useless to the game list.
    [[nodiscard]] bool IsValid() const {
        return !title_id.empty();
    }

    // Flat key/value view for the param viewer UI.
    struct DisplayEntry {
        std::string key;
        std::string value;
        ParamValueType type = ParamValueType::String;
    };
    [[nodiscard]] std::vector<DisplayEntry> GetDisplayEntries() const;
    [[nodiscard]] static std::string_view LocaleFromLanguage(s32 language_index);
    [[nodiscard]] static s32 LanguageFromLocale(std::string_view locale);
    // Formats a BCD-packed version word
    [[nodiscard]] static std::string FormatBcdVersion(u32 bcd);
};
