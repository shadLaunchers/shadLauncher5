// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// History:
//   2026-01-02  Copied from shadPS4 Emulator Project (v0.13.0)
//   2026-07-20  Replaced binary PS3/PS4 "param.sfo" parsing with PS5
//               "param.json" parsing; removed PS4 ".pkg" support, the only
//               other consumer of the binary SFO format, along with it.

#include <cstring>

#include <nlohmann/json.hpp>

#include "common/assert.h"
#include "common/io_file.h"
#include "common/logging/log.h"
#include "core/file_format/psf.h"

static const std::unordered_map<std::string_view, u32> psf_known_max_sizes = {
    {"ACCOUNT_ID", 8},  {"CATEGORY", 4},  {"DETAIL", 1024},       {"FORMAT", 4},
    {"MAINTITLE", 128}, {"PARAMS", 1024}, {"SAVEDATA_BLOCKS", 8}, {"SAVEDATA_DIRECTORY", 32},
    {"SUBTITLE", 128},  {"TITLE_ID", 12},
};
static inline u32 get_max_size(std::string_view key, u32 default_value) {
    if (const auto& v = psf_known_max_sizes.find(key); v != psf_known_max_sizes.end()) {
        return v->second;
    }
    return default_value;
}

// PS5 "param.json" replaces the binary "param.sfo" used on PS3/PS4. It uses
// plain locale codes (e.g. "en-US", "ja-JP") for localized strings, while
// PSF/CELL identify languages by a small integer index. This table lets us
// keep producing the familiar "TITLE_%02d" keys that the rest of the
// codebase (game_info.h, game_list_frame.cpp, ...) already knows how to read,
// regardless of which format a given title shipped with. Kept local to this
// file (rather than reusing Libraries::SystemService::ProsperoSystemParamLanguage)
// so the file-format layer has no dependency on the emulated system libraries.
static const std::pair<int, const char*> json_lang_table[] = {
    {0, "ja-JP"},    {1, "en-US"},  {2, "fr-FR"},    {3, "es-ES"}, {4, "de-DE"},
    {5, "it-IT"},    {6, "nl-NL"},  {7, "pt-PT"},    {8, "ru-RU"}, {9, "ko-KR"},
    {10, "zh-Hant"}, {11, "zh-Hans"}, {12, "fi-FI"}, {13, "sv-SE"}, {14, "da-DK"},
    {15, "no-NO"},   {16, "pl-PL"}, {17, "pt-BR"},   {18, "en-GB"}, {19, "tr-TR"},
    {20, "es-419"},  {21, "ar-AE"}, {22, "fr-CA"},   {23, "cs-CZ"}, {24, "hu-HU"},
    {25, "el-GR"},   {26, "ro-RO"}, {27, "th-TH"},   {28, "vi-VN"}, {29, "id-ID"},
    {30, "uk-UA"},
};

// requiredSystemSoftwareVersion / sdkVersion in param.json are 64-bit values
// encoded as a "0x..." hex string, e.g. "0x0400000000000000". Just like PSF's
// SYSTEM_VER, the top byte is the BCD-encoded major version and the next byte
// is the BCD-encoded minor version; only the upper 32 bits are meaningful for
// display purposes.
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

bool PSF::Open(const std::filesystem::path& filepath) {
    using namespace std::chrono;
    if (std::filesystem::exists(filepath)) {
        const auto t = std::filesystem::last_write_time(filepath);
        const auto rel =
            duration_cast<seconds>(t - std::filesystem::file_time_type::clock::now()).count();
        const auto tp = system_clock::to_time_t(system_clock::now() + seconds{rel});
        last_write = system_clock::from_time_t(tp);
    }

    Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        return false;
    }

    const u64 fileSize = file.GetSize();
    ASSERT_MSG(fileSize != 0, "Param file at {} is empty!", filepath.string());
    std::vector<u8> buffer(fileSize);
    file.Seek(0);
    file.Read(buffer);
    file.Close();

    return OpenJson(buffer);
}

bool PSF::OpenJson(const std::vector<u8>& json_buffer) {
    entry_list.clear();
    map_binaries.clear();
    map_strings.clear();
    map_integers.clear();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_buffer, /*cb=*/nullptr, /*allow_exceptions=*/true);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Core, "Failed to parse param.json: {}", e.what());
        return false;
    }
    if (!j.is_object()) {
        LOG_ERROR(Core, "param.json root is not an object");
        return false;
    }

    // --- Identity -----------------------------------------------------
    if (const auto it = j.find("titleId"); it != j.end() && it->is_string()) {
        AddString("TITLE_ID", it->get<std::string>());
    }
    std::string content_id;
    if (const auto it = j.find("contentId"); it != j.end() && it->is_string()) {
        content_id = it->get<std::string>();
        AddString("CONTENT_ID", content_id);
    }

    // --- Titles ---------------------------------------------------------
    // param.json keys localized titles by locale (e.g. "en-US") instead of
    // by the numeric CELL language index PSF uses. Populate both the
    // default TITLE and, where available, the same TITLE_%02d keys PSF
    // would have so per-language lookups (e.g. game_list_frame.cpp) keep
    // working unmodified.
    if (const auto lp = j.find("localizedParameters"); lp != j.end() && lp->is_object()) {
        std::string default_lang = lp->value("defaultLanguage", "en-US");
        std::string default_title;
        if (const auto it = lp->find(default_lang);
            it != lp->end() && it->contains("titleName")) {
            default_title = (*it)["titleName"].get<std::string>();
        }
        if (default_title.empty()) {
            // Fall back to the first localized entry we can find.
            for (const auto& [locale, value] : lp->items()) {
                if (locale == "defaultLanguage") {
                    continue;
                }
                if (value.contains("titleName")) {
                    default_title = value["titleName"].get<std::string>();
                    break;
                }
            }
        }
        if (!default_title.empty()) {
            AddString("TITLE", default_title);
        }

        char title_key[16];
        for (const auto& [lang_index, locale] : json_lang_table) {
            if (const auto it = lp->find(locale); it != lp->end() && it->contains("titleName")) {
                std::snprintf(title_key, sizeof(title_key), "TITLE_%02d", lang_index);
                AddString(title_key, (*it)["titleName"].get<std::string>());
            }
        }
    }

    // --- Category ---------------------------------------------------------
    // PS4's PSF encodes content type as a short CATEGORY string ("gd" for a
    // full game, "ac" for add-on content, "gp" for a patch, ...). PS5's
    // param.json instead uses a numeric "applicationCategoryType". Per
    // psdevwiki (psdevwiki.com/ps5/Param.json), the confirmed values are:
    //   0=Native Game, 65536=Prospero Native Media App, 65792=RNPS Media App,
    //   66048=Web Based Media App, 131328=System Built-in App,
    //   131584=Big Daemon, 16777216=ShellUI, 33554432=Daemon,
    //   50331648=CommonDialog, 67108864=ShellApp.
    // None of these represent add-on/DLC content, so there is no known
    // equivalent of PSF's "ac" category yet; the downstream "skip DLC"
    // check in game_list_frame.cpp will not fire for any PS5 title until
    // that's identified. Only tag real, installable games as "gd" so we
    // don't misrepresent media apps/daemons/system UI as full games; leave
    // CATEGORY unset for anything else rather than guessing.
    if (const auto it = j.find("applicationCategoryType");
        it != j.end() && it->is_number_integer() && it->get<s64>() == 0) {
        AddString("CATEGORY", "gd");
    }

    // --- Versions -----------------------------------------------------
    if (const auto it = j.find("contentVersion"); it != j.end() && it->is_string()) {
        AddString("APP_VER", it->get<std::string>());
    }
    if (const auto it = j.find("requiredSystemSoftwareVersion"); it != j.end() && it->is_string()) {
        const u32 fw_bcd = TopU32FromHexU64String(it->get<std::string>());
        AddInteger("SYSTEM_VER", static_cast<s32>(fw_bcd));
    }
    if (const auto it = j.find("sdkVersion"); it != j.end() && it->is_string()) {
        // Re-encode as a synthetic PUBTOOLINFO string so the existing
        // "sdk_ver=XXXXXXXX" parsing in game_list_frame.cpp keeps working
        // unchanged regardless of which format's SDK version we're reading.
        const u32 sdk_bcd = TopU32FromHexU64String(it->get<std::string>());
        char pubtoolinfo[32];
        std::snprintf(pubtoolinfo, sizeof(pubtoolinfo), "sdk_ver=%08x", sdk_bcd);
        AddString("PUBTOOLINFO", pubtoolinfo);
    }

    // --- Save data --------------------------------------------------------
    // param.json doesn't carry a PS4-style INSTALL_DIR_SAVEDATA folder name;
    // callers already fall back to TITLE_ID when this key is absent.

    return true;
}

std::optional<std::span<const u8>> PSF::GetBinary(std::string_view key) const {
    const auto& [it, index] = FindEntry(key);
    if (it == entry_list.end()) {
        return {};
    }
    ASSERT(it->param_fmt == PSFEntryFmt::Binary);
    return std::span{map_binaries.at(index)};
}

std::optional<std::string_view> PSF::GetString(std::string_view key) const {
    const auto& [it, index] = FindEntry(key);
    if (it == entry_list.end()) {
        return {};
    }
    ASSERT(it->param_fmt == PSFEntryFmt::Text);
    return std::string_view{map_strings.at(index)};
}

std::optional<s32> PSF::GetInteger(std::string_view key) const {
    const auto& [it, index] = FindEntry(key);
    if (it == entry_list.end()) {
        return {};
    }
    ASSERT(it->param_fmt == PSFEntryFmt::Integer);
    return map_integers.at(index);
}

void PSF::AddBinary(std::string key, std::vector<u8> value, bool update) {
    auto [it, index] = FindEntry(key);
    bool exist = it != entry_list.end();
    if (exist && !update) {
        LOG_ERROR(Core, "PSF: Tried to add binary key that already exists: {}", key);
        return;
    }
    if (exist) {
        ASSERT_MSG(it->param_fmt == PSFEntryFmt::Binary, "PSF: Change format is not supported");
        it->max_len = get_max_size(key, value.size());
        map_binaries.at(index) = std::move(value);
        return;
    }
    Entry& entry = entry_list.emplace_back();
    entry.max_len = get_max_size(key, value.size());
    entry.key = std::move(key);
    entry.param_fmt = PSFEntryFmt::Binary;
    map_binaries.emplace(entry_list.size() - 1, std::move(value));
}

void PSF::AddBinary(std::string key, uint64_t value, bool update) {
    std::vector<u8> data(8);
    std::memcpy(data.data(), &value, 8);
    return AddBinary(std::move(key), std::move(data), update);
}

void PSF::AddString(std::string key, std::string value, bool update) {
    auto [it, index] = FindEntry(key);
    bool exist = it != entry_list.end();
    if (exist && !update) {
        LOG_ERROR(Core, "PSF: Tried to add string key that already exists: {}", key);
        return;
    }
    if (exist) {
        ASSERT_MSG(it->param_fmt == PSFEntryFmt::Text, "PSF: Change format is not supported");
        it->max_len = get_max_size(key, value.size() + 1);
        map_strings.at(index) = std::move(value);
        return;
    }
    Entry& entry = entry_list.emplace_back();
    entry.max_len = get_max_size(key, value.size() + 1);
    entry.key = std::move(key);
    entry.param_fmt = PSFEntryFmt::Text;
    map_strings.emplace(entry_list.size() - 1, std::move(value));
}

void PSF::AddInteger(std::string key, s32 value, bool update) {
    auto [it, index] = FindEntry(key);
    bool exist = it != entry_list.end();
    if (exist && !update) {
        LOG_ERROR(Core, "PSF: Tried to add integer key that already exists: {}", key);
        return;
    }
    if (exist) {
        ASSERT_MSG(it->param_fmt == PSFEntryFmt::Integer, "PSF: Change format is not supported");
        it->max_len = sizeof(s32);
        map_integers.at(index) = value;
        return;
    }
    Entry& entry = entry_list.emplace_back();
    entry.key = std::move(key);
    entry.param_fmt = PSFEntryFmt::Integer;
    entry.max_len = sizeof(s32);
    map_integers.emplace(entry_list.size() - 1, value);
}

std::pair<std::vector<PSF::Entry>::iterator, size_t> PSF::FindEntry(std::string_view key) {
    auto entry =
        std::ranges::find_if(entry_list, [&](const auto& entry) { return entry.key == key; });
    return {entry, std::distance(entry_list.begin(), entry)};
}

std::pair<std::vector<PSF::Entry>::const_iterator, size_t> PSF::FindEntry(
    std::string_view key) const {
    auto entry =
        std::ranges::find_if(entry_list, [&](const auto& entry) { return entry.key == key; });
    return {entry, std::distance(entry_list.begin(), entry)};
}
