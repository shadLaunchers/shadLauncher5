// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cctype>
#include <cstddef>
#include <utility>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/io_file.h"
#include "common/logging/log.h"
#include "core/file_format/param.h"

// "localizedParameters" locale keys, indexed the same way the emulator indexes
// its own language setting. See psdevwiki.com/ps5/Param.json#localizedParameters.
static constexpr std::array<std::pair<s32, std::string_view>, 31> lang_table = {{
    {0, "ja-JP"},  {1, "en-US"},  {2, "fr-FR"},   {3, "es-ES"},  {4, "de-DE"},    {5, "it-IT"},
    {6, "nl-NL"},  {7, "pt-PT"},  {8, "ru-RU"},   {9, "ko-KR"},  {10, "zh-Hant"}, {11, "zh-Hans"},
    {12, "fi-FI"}, {13, "sv-SE"}, {14, "da-DK"},  {15, "no-NO"}, {16, "pl-PL"},   {17, "pt-BR"},
    {18, "en-GB"}, {19, "tr-TR"}, {20, "es-419"}, {21, "ar-AE"}, {22, "fr-CA"},   {23, "cs-CZ"},
    {24, "hu-HU"}, {25, "el-GR"}, {26, "ro-RO"},  {27, "th-TH"}, {28, "vi-VN"},   {29, "id-ID"},
    {30, "uk-UA"},
}};

// "applicationCategoryType" values.
static constexpr std::array<std::pair<s64, std::string_view>, 10> category_table = {{
    {0, "Native Game"},
    {65536, "Prospero Native Media App"},
    {65792, "RNPS Media App"},
    {66048, "Web Based Media App"},
    {131328, "System Built-in App"},
    {131584, "Big Daemon"},
    {16777216, "ShellUI"},
    {33554432, "Daemon"},
    {50331648, "CommonDialog"},
    {67108864, "ShellApp"},
}};

namespace {

// param.json pads several string fields out to a fixed width with spaces
// (versionFileUri and addcont.serviceIdForSharing are the usual offenders).
// The padding is not part of the value, so it never reaches the UI.
std::string TrimPadding(std::string s) {
    const auto is_pad = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_pad(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    std::size_t start = 0;
    while (start < s.size() && is_pad(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    return s.substr(start);
}

ParamValueType TypeOf(const nlohmann::json& node) {
    if (node.is_object()) {
        return ParamValueType::Object;
    }
    if (node.is_array()) {
        return ParamValueType::Array;
    }
    if (node.is_string()) {
        return ParamValueType::String;
    }
    if (node.is_boolean()) {
        return ParamValueType::Boolean;
    }
    if (node.is_number_float()) {
        return ParamValueType::Float;
    }
    if (node.is_number()) {
        return ParamValueType::Integer;
    }
    return ParamValueType::Null;
}

std::string RenderScalar(const nlohmann::json& node) {
    if (node.is_string()) {
        return TrimPadding(node.get<std::string>());
    }
    if (node.is_boolean()) {
        return node.get<bool>() ? "true" : "false";
    }
    if (node.is_number_float()) {
        return fmt::format("{}", node.get<double>());
    }
    if (node.is_number_unsigned()) {
        return std::to_string(node.get<u64>());
    }
    if (node.is_number_integer()) {
        return std::to_string(node.get<s64>());
    }
    if (node.is_null()) {
        return "null";
    }
    return node.dump();
}

void Flatten(const nlohmann::json& node, const std::string& path, const std::string& lookup,
             int depth, std::vector<Param::Entry>& out) {
    if (node.is_object()) {
        if (!path.empty()) {
            out.push_back({path, lookup, fmt::format("{} key(s)", node.size()),
                           ParamValueType::Object, depth});
        }
        for (const auto& [key, value] : node.items()) {
            const std::string child = path.empty() ? key : fmt::format("{}.{}", path, key);
            const std::string child_lookup =
                lookup.empty() ? key : fmt::format("{}.{}", lookup, key);
            Flatten(value, child, child_lookup, path.empty() ? depth : depth + 1, out);
        }
        return;
    }

    if (node.is_array()) {
        out.push_back(
            {path, lookup, fmt::format("{} item(s)", node.size()), ParamValueType::Array, depth});
        for (std::size_t i = 0; i < node.size(); ++i) {
            Flatten(node[i], fmt::format("{}[{}]", path, i), fmt::format("{}[]", lookup), depth + 1,
                    out);
        }
        return;
    }

    out.push_back({path, lookup, RenderScalar(node), TypeOf(node), depth});
}

} // namespace

u32 Param::TopU32FromHexU64String(std::string_view hex) {
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

std::string_view Param::TypeName(ParamValueType type) {
    switch (type) {
    case ParamValueType::Null:
        return "null";
    case ParamValueType::Boolean:
        return "bool";
    case ParamValueType::Integer:
        return "int";
    case ParamValueType::Float:
        return "float";
    case ParamValueType::String:
        return "string";
    case ParamValueType::Array:
        return "array";
    case ParamValueType::Object:
        return "object";
    }
    return "unknown";
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

std::string_view Param::CategoryName(s64 application_category_type) {
    for (const auto& [value, name] : category_table) {
        if (value == application_category_type) {
            return name;
        }
    }
    return {};
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
        LOG_ERROR(Core, "param.json at {} is empty!", filepath.string());
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

    // Retained so the viewer can show the document as-is and walk fields this
    // class has no member for.
    raw_json.assign(json_text);

    // --- Identity ---------------------------------------------------------
    if (const auto it = j.find("titleId"); it != j.end() && it->is_string()) {
        title_id = TrimPadding(it->get<std::string>());
    }
    if (const auto it = j.find("contentId"); it != j.end() && it->is_string()) {
        content_id = TrimPadding(it->get<std::string>());
    }
    if (const auto it = j.find("conceptId"); it != j.end() && it->is_string()) {
        concept_id = TrimPadding(it->get<std::string>());
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
        category = CategoryName(application_category_type);
        if (category.empty()) {
            category = fmt::format("Unknown ({})", application_category_type);
        }
    }
    if (const auto it = j.find("applicationDrmType"); it != j.end() && it->is_string()) {
        drm_type = it->get<std::string>();
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

std::vector<Param::Entry> Param::GetEntries() const {
    std::vector<Entry> entries;
    if (raw_json.empty()) {
        return entries;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(raw_json, /*cb=*/nullptr, /*allow_exceptions=*/true);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Core, "Failed to re-parse retained param.json: {}", e.what());
        return entries;
    }

    Flatten(j, /*path=*/"", /*lookup=*/"", /*depth=*/0, entries);
    return entries;
}

std::string Param::PrettyJson() const {
    if (raw_json.empty()) {
        return {};
    }
    try {
        return nlohmann::json::parse(raw_json).dump(4);
    } catch (const nlohmann::json::exception&) {
        return raw_json;
    }
}
