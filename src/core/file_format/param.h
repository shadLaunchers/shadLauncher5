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
    Null,
    Boolean,
    Integer,
    Float,
    String,
    Array,
    Object,
};

// Reads a PS5 sce_sys/param.json.
// Field documentation: https://www.psdevwiki.com/ps5/Param.json
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

    // Clears every parsed field, including last_write and the retained source.
    void Reset();

    std::string title_id;         // "titleId",   e.g. "PPSA01234"
    std::string content_id;       // "contentId", e.g. "EP9000-PPSA01234_00-..."
    std::string concept_id;       // "conceptId"
    std::string title;            // titleName for `default_language`
    std::string default_language; // "localizedParameters.defaultLanguage"
    std::map<s32, std::string> localized_titles;
    s64 application_category_type = -1; // -1 = key absent
    std::string category;               // human-readable applicationCategoryType
    std::string drm_type;               // "applicationDrmType"
    std::string app_ver;                // "contentVersion", e.g. "01.000.000"
    std::string master_version;         // "masterVersion",  e.g. "01.00"
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

    struct Entry {
        // Full path to the value, e.g. "pubtools.creationDate", "disc[0].role".
        std::string key;
        // `key` with array indices collapsed.
        std::string lookup_key;
        // Scalars render as text; containers render as an item/key count.
        std::string value;
        ParamValueType type = ParamValueType::Object;
        // Nesting level, 0 for top-level keys.
        int depth = 0;
    };

    // Walks the retained source document. Empty if nothing was parsed
    // successfully. Object keys come out in sorted order.
    [[nodiscard]] std::vector<Entry> GetEntries() const;

    // The file exactly as it was read, for a raw view.
    [[nodiscard]] const std::string& RawJson() const {
        return raw_json;
    }

    // Re-indented source. Falls back to RawJson() if it doesn't parse.
    [[nodiscard]] std::string PrettyJson() const;

    [[nodiscard]] static std::string_view TypeName(ParamValueType type);
    [[nodiscard]] static std::string_view LocaleFromLanguage(s32 language_index);
    [[nodiscard]] static s32 LanguageFromLocale(std::string_view locale);
    // "applicationCategoryType" int -> label, empty for undocumented values.
    [[nodiscard]] static std::string_view CategoryName(s64 application_category_type);
    // Formats a BCD-packed version word, e.g. 0x03200000 -> "3.20".
    [[nodiscard]] static std::string FormatBcdVersion(u32 bcd);
    // Reads the top 32 bits out of a "0x0320000000000000"-style string.
    [[nodiscard]] static u32 TopU32FromHexU64String(std::string_view hex);

private:
    std::string raw_json;
};
