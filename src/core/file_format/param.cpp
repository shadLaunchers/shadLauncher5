// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/io_file.h"
#include "common/logging/log.h"
#include "core/file_format/param.h"

static constexpr std::array<std::pair<s32, std::string_view>, 31> lang_table = {{
    {0, "ja-JP"},  {1, "en-US"},  {2, "fr-FR"},   {3, "es-ES"},  {4, "de-DE"},    {5, "it-IT"},
    {6, "nl-NL"},  {7, "pt-PT"},  {8, "ru-RU"},   {9, "ko-KR"},  {10, "zh-Hant"}, {11, "zh-Hans"},
    {12, "fi-FI"}, {13, "sv-SE"}, {14, "da-DK"},  {15, "no-NO"}, {16, "pl-PL"},   {17, "pt-BR"},
    {18, "en-GB"}, {19, "tr-TR"}, {20, "es-419"}, {21, "ar-AE"}, {22, "fr-CA"},   {23, "cs-CZ"},
    {24, "hu-HU"}, {25, "el-GR"}, {26, "ro-RO"},  {27, "th-TH"}, {28, "vi-VN"},   {29, "id-ID"},
    {30, "uk-UA"},
}};

static u32 TopU32FromHexU64String(std::string_view hex) {
    if (hex.starts_with("0x") || hex.starts_with("0X")) {
        hex.remove_prefix(2);
    }
    u64 value = 0;
    for (const char c : hex) {
        u8 nibble;
        if (c >= '0' && c <= '9') {
            nibble = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            nibble = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            nibble = c - 'A' + 10;
        } else {
            break;
        }
        value = (value << 4) | nibble;
    }
    return static_cast<u32>(value >> 32);
}

std::string Param::FormatBcdVersion(u32 bcd) {
    if (bcd == 0) {
        return "0.00";
    }
    const u8 major_bcd = (bcd >> 24) & 0xFF;
    const u8 minor_bcd = (bcd >> 16) & 0xFF;
    const int major = ((major_bcd >> 4) * 10) + (major_bcd & 0xF);
    const int minor = ((minor_bcd >> 4) * 10) + (minor_bcd & 0xF);
    return fmt::format("{}.{:02}", major, minor);
}

std::string_view Param::LocaleFromLanguage(s32 language_index) {
    for (const auto& [index, locale] : lang_table) {
        if (index == language_index) {
            return locale;
        }
    }
    return {};
}

s32 Param::LanguageFromLocale(std::string_view locale) {
    for (const auto& [index, code] : lang_table) {
        if (code == locale) {
            return index;
        }
    }
    return -1;
}

void Param::Reset() {
    *this = Param{};
}

bool Param::Open(const std::filesystem::path& filepath) {
    Reset();

    Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        return false;
    }

    const u64 file_size = file.GetSize();
    if (file_size == 0) {
        LOG_ERROR(Core, "Param file at {} is empty!", filepath.string());
        return false;
    }

    std::string buffer(file_size, '\0');
    file.Seek(0);
    file.Read(buffer);
    file.Close();

    const bool parsed = Parse(buffer);

    // Set after Parse(), which resets every field.
    using namespace std::chrono;
    if (std::error_code ec; std::filesystem::exists(filepath, ec) && !ec) {
        const auto t = std::filesystem::last_write_time(filepath, ec);
        if (!ec) {
            const auto rel =
                duration_cast<seconds>(t - std::filesystem::file_time_type::clock::now()).count();
            const auto tp = system_clock::to_time_t(system_clock::now() + seconds{rel});
            last_write = system_clock::from_time_t(tp);
        }
    }

    return parsed;
}

bool Param::Parse(std::string_view json_text) {
    Reset();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text, /*cb=*/nullptr, /*allow_exceptions=*/true);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Core, "Failed to parse param.json: {}", e.what());
        return false;
    }
    if (!j.is_object()) {
        LOG_ERROR(Core, "param.json root is not an object");
        return false;
    }

    // --- Identity ---------------------------------------------------------
    if (const auto it = j.find("titleId"); it != j.end() && it->is_string()) {
        title_id = it->get<std::string>();
    }
    if (const auto it = j.find("contentId"); it != j.end() && it->is_string()) {
        content_id = it->get<std::string>();
    }

    // --- Titles -----------------------------------------------------------
    if (const auto lp = j.find("localizedParameters"); lp != j.end() && lp->is_object()) {
        default_language = lp->value("defaultLanguage", "en-US");

        if (const auto it = lp->find(default_language);
            it != lp->end() && it->is_object() && it->contains("titleName")) {
            title = (*it)["titleName"].get<std::string>();
        }
        if (title.empty()) {
            // Fall back to the first localized entry we can find.
            for (const auto& [locale, value] : lp->items()) {
                if (locale == "defaultLanguage" || !value.is_object()) {
                    continue;
                }
                if (value.contains("titleName")) {
                    title = value["titleName"].get<std::string>();
                    break;
                }
            }
        }

        for (const auto& [index, locale] : lang_table) {
            if (const auto it = lp->find(locale);
                it != lp->end() && it->is_object() && it->contains("titleName")) {
                localized_titles[index] = (*it)["titleName"].get<std::string>();
            }
        }
    }

    // --- Category ---------------------------------------------------------
    if (const auto it = j.find("applicationCategoryType");
        it != j.end() && it->is_number_integer()) {
        application_category_type = it->get<s64>();
        if (application_category_type == 0) {
            category = "gd"; // native game; see param.h for the other values
        }
    }

    // --- Versions ---------------------------------------------------------
    if (const auto it = j.find("contentVersion"); it != j.end() && it->is_string()) {
        app_ver = it->get<std::string>();
    }
    if (const auto it = j.find("masterVersion"); it != j.end() && it->is_string()) {
        master_version = it->get<std::string>();
    }
    if (const auto it = j.find("requiredSystemSoftwareVersion"); it != j.end() && it->is_string()) {
        system_ver = TopU32FromHexU64String(it->get<std::string>());
        system_ver_string = FormatBcdVersion(system_ver);
    }
    if (const auto it = j.find("sdkVersion"); it != j.end() && it->is_string()) {
        sdk_ver = TopU32FromHexU64String(it->get<std::string>());
        sdk_ver_string = FormatBcdVersion(sdk_ver);
    }

    return true;
}

const std::string& Param::LocalizedTitle(s32 language_index) const {
    if (const auto it = localized_titles.find(language_index);
        it != localized_titles.end() && !it->second.empty()) {
        return it->second;
    }
    return title;
}

std::vector<Param::DisplayEntry> Param::GetDisplayEntries() const {
    std::vector<DisplayEntry> entries;
    entries.reserve(8 + localized_titles.size());

    const auto add_string = [&entries](std::string key, const std::string& value) {
        if (!value.empty()) {
            entries.push_back({std::move(key), value, ParamValueType::String});
        }
    };

    add_string("TITLE", title);
    add_string("TITLE_ID", title_id);
    add_string("CONTENT_ID", content_id);
    add_string("CATEGORY", category);
    add_string("APP_VER", app_ver);
    add_string("MASTER_VERSION", master_version);

    if (application_category_type >= 0) {
        entries.push_back({"APPLICATION_CATEGORY_TYPE", std::to_string(application_category_type),
                           ParamValueType::Integer});
    }
    if (system_ver != 0) {
        entries.push_back({"SYSTEM_VER", std::to_string(system_ver), ParamValueType::Integer});
    }
    if (sdk_ver != 0) {
        add_string("SDK_VER", sdk_ver_string);
    }
    for (const auto& [index, localized] : localized_titles) {
        entries.push_back({fmt::format("TITLE_{:02}", index), localized, ParamValueType::String});
    }

    return entries;
}
