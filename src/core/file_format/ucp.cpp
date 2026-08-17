// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <utility>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/io_file.h"
#include "common/logging/log.h"
#include "core/file_format/ucp.h"

namespace {

// tropmeta locale codes, indexed the same way param.json's localizedParameters
// are. Kept in sync with the table in core/file_format/param.cpp.
constexpr std::array<std::pair<int, std::string_view>, 31> locale_table = {{
    {0, "ja-JP"},  {1, "en-US"},  {2, "fr-FR"},   {3, "es-ES"},  {4, "de-DE"},    {5, "it-IT"},
    {6, "nl-NL"},  {7, "pt-PT"},  {8, "ru-RU"},   {9, "ko-KR"},  {10, "zh-Hant"}, {11, "zh-Hans"},
    {12, "fi-FI"}, {13, "sv-SE"}, {14, "da-DK"},  {15, "no-NO"}, {16, "pl-PL"},   {17, "pt-BR"},
    {18, "en-GB"}, {19, "tr-TR"}, {20, "es-419"}, {21, "ar-AE"}, {22, "fr-CA"},   {23, "cs-CZ"},
    {24, "hu-HU"}, {25, "el-GR"}, {26, "ro-RO"},  {27, "th-TH"}, {28, "vi-VN"},   {29, "id-ID"},
    {30, "uk-UA"},
}};

constexpr std::string_view meta_prefix = "tropmeta_";
constexpr std::string_view json_suffix = ".json";

// Reads a whole file into a string. Returns false if it can't be read.
bool ReadWholeFile(const std::filesystem::path& path, std::string& out) {
    Common::FS::IOFile file(path, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        return false;
    }
    const u64 size = file.GetSize();
    if (size == 0) {
        return false;
    }
    out.assign(size, '\0');
    file.Seek(0);
    file.Read(out);
    return true;
}

// Several tropconf fields are strings in every sample seen ("id": "0000",
// "targetValue": "1"), but the schema is young enough that a producer emitting
// them as numbers is plausible. Accept both rather than dropping the field.
std::string AsString(const nlohmann::json& node) {
    if (node.is_string()) {
        return node.get<std::string>();
    }
    if (node.is_number_integer()) {
        return std::to_string(node.get<s64>());
    }
    if (node.is_number_unsigned()) {
        return std::to_string(node.get<u64>());
    }
    if (node.is_boolean()) {
        return node.get<bool>() ? "true" : "false";
    }
    return {};
}

std::string GetString(const nlohmann::json& obj, std::string_view key) {
    const auto it = obj.find(key);
    return it != obj.end() ? AsString(*it) : std::string{};
}

} // namespace

std::string TrophyDefinition::IconFileName() const {
    if (id.size() >= 4) {
        return fmt::format("trop{}.png", id);
    }
    return fmt::format("trop{:0>4}.png", id);
}

TrophyGrade TrophySet::GradeFromCode(std::string_view code) {
    if (code == "P") {
        return TrophyGrade::Platinum;
    }
    if (code == "G") {
        return TrophyGrade::Gold;
    }
    if (code == "S") {
        return TrophyGrade::Silver;
    }
    if (code == "B") {
        return TrophyGrade::Bronze;
    }
    return TrophyGrade::Unknown;
}

std::string_view TrophySet::GradeCode(TrophyGrade grade) {
    switch (grade) {
    case TrophyGrade::Platinum:
        return "P";
    case TrophyGrade::Gold:
        return "G";
    case TrophyGrade::Silver:
        return "S";
    case TrophyGrade::Bronze:
        return "B";
    case TrophyGrade::Unknown:
        break;
    }
    return "";
}

std::string_view TrophySet::GradeName(TrophyGrade grade) {
    switch (grade) {
    case TrophyGrade::Platinum:
        return "Platinum";
    case TrophyGrade::Gold:
        return "Gold";
    case TrophyGrade::Silver:
        return "Silver";
    case TrophyGrade::Bronze:
        return "Bronze";
    case TrophyGrade::Unknown:
        break;
    }
    return "Unknown";
}

std::string_view TrophySet::GradeIconFileName(TrophyGrade grade) {
    switch (grade) {
    case TrophyGrade::Platinum:
        return "platinum.png";
    case TrophyGrade::Gold:
        return "gold.png";
    case TrophyGrade::Silver:
        return "silver.png";
    case TrophyGrade::Bronze:
        return "bronze.png";
    case TrophyGrade::Unknown:
        break;
    }
    return "";
}

std::string_view TrophySet::LocaleForLanguage(int language_index) {
    for (const auto& [index, locale] : locale_table) {
        if (index == language_index) {
            return locale;
        }
    }
    return {};
}

int TrophySet::LanguageForLocale(std::string_view locale) {
    for (const auto& [index, code] : locale_table) {
        if (code == locale) {
            return index;
        }
    }
    return -1;
}

std::string TrophySet::LocaleFromMetaFileName(std::string_view file_name) {
    if (!file_name.starts_with(meta_prefix) || !file_name.ends_with(json_suffix)) {
        return {};
    }
    const std::size_t len = file_name.size() - meta_prefix.size() - json_suffix.size();
    return std::string(file_name.substr(meta_prefix.size(), len));
}

std::string TrophySet::MetaFileNameForLocale(std::string_view locale) {
    return fmt::format("{}{}{}", meta_prefix, locale, json_suffix);
}

void TrophySet::Reset() {
    *this = TrophySet{};
}

int TrophySet::FindById(std::string_view id) const {
    for (std::size_t i = 0; i < trophies.size(); ++i) {
        if (trophies[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool TrophySet::ParseConf(std::string_view json_text) {
    Reset();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Loader, "Failed to parse tropconf.json: {}", e.what());
        return false;
    }
    if (!j.is_object()) {
        LOG_ERROR(Loader, "tropconf.json root is not an object");
        return false;
    }

    schema_version = GetString(j, "schemaVersion");
    np_comm_id = GetString(j, "trophyNpCommId");
    uds_np_comm_id = GetString(j, "udsNpCommId");
    set_version = GetString(j, "trophySetVersion");
    default_language = GetString(j, "defaultLanguage");

    if (const auto it = j.find("trophyDefinitionRevision");
        it != j.end() && it->is_number_integer()) {
        definition_revision = it->get<s64>();
    }
    if (const auto it = j.find("platform"); it != j.end() && it->is_array()) {
        for (const auto& p : *it) {
            platform.push_back(AsString(p));
        }
    }
    if (const auto it = j.find("languages"); it != j.end() && it->is_array()) {
        for (const auto& l : *it) {
            languages.push_back(AsString(l));
        }
    }

    const auto trophies_it = j.find("trophies");
    if (trophies_it == j.end() || !trophies_it->is_array()) {
        LOG_ERROR(Loader, "tropconf.json has no trophies array");
        return false;
    }

    for (const auto& t : *trophies_it) {
        if (!t.is_object()) {
            continue;
        }
        TrophyDefinition def;
        def.id = GetString(t, "id");
        def.grade = GradeFromCode(GetString(t, "grade"));
        def.hidden = t.value("hidden", false);
        def.has_reward = t.value("hasReward", false);
        def.platinum_id = GetString(t, "platinumTrophyId");

        if (const auto uc = t.find("unlockCondition"); uc != t.end() && uc->is_object()) {
            def.has_unlock_condition = true;
            def.unlock_condition.uds_stat_id = GetString(*uc, "udsStatId");
            def.unlock_condition.comparator = GetString(*uc, "comparator");
            def.unlock_condition.target_value = GetString(*uc, "targetValue");
            def.unlock_condition.progressive = uc->value("progressive", false);
        }

        trophies.push_back(std::move(def));
    }

    return !trophies.empty();
}

bool TrophySet::ApplyMeta(std::string_view json_text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Loader, "Failed to parse tropmeta json: {}", e.what());
        return false;
    }
    if (!j.is_object()) {
        return false;
    }

    const auto metadata = j.find("metadata");
    if (metadata == j.end() || !metadata->is_object()) {
        return false;
    }

    if (const auto tm = metadata->find("titleMetadata"); tm != metadata->end() && tm->is_object()) {
        title_name = GetString(*tm, "name");
    }

    const auto list = metadata->find("trophyMetadata");
    if (list == metadata->end() || !list->is_array()) {
        return false;
    }

    for (const auto& t : *list) {
        if (!t.is_object()) {
            continue;
        }
        const int index = FindById(GetString(t, "id"));
        if (index < 0) {
            // A string for a trophy tropconf doesn't define. Nothing sensible
            // to show it against, so skip rather than inventing a row.
            continue;
        }
        trophies[index].name = GetString(t, "name");
        trophies[index].detail = GetString(t, "detail");
    }

    return true;
}

bool TrophySet::LoadFromDir(const std::filesystem::path& dir, int language_index) {
    std::string conf_text;
    if (!ReadWholeFile(dir / "tropconf.json", conf_text)) {
        LOG_ERROR(Loader, "Missing tropconf.json in {}", dir.string());
        return false;
    }
    if (!ParseConf(conf_text)) {
        return false;
    }

    // Preference order: the requested language, the set's own default, then
    // any tropmeta present. A set with no readable tropmeta still loads --
    // the trophy list is valid, just unnamed.
    std::vector<std::string> candidates;
    if (const auto locale = LocaleForLanguage(language_index); !locale.empty()) {
        candidates.emplace_back(locale);
    }
    if (!default_language.empty()) {
        candidates.push_back(default_language);
    }
    for (const auto& l : languages) {
        candidates.push_back(l);
    }

    for (const auto& locale : candidates) {
        std::string meta_text;
        if (!ReadWholeFile(dir / MetaFileNameForLocale(locale), meta_text)) {
            continue;
        }
        if (ApplyMeta(meta_text)) {
            loaded_locale = locale;
            return true;
        }
    }

    // Nothing in `languages` matched either; fall back to scanning the
    // directory, which covers containers whose languages array is missing or
    // disagrees with the files actually packed.
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        const std::string locale = LocaleFromMetaFileName(entry.path().filename().string());
        if (locale.empty()) {
            continue;
        }
        std::string meta_text;
        if (ReadWholeFile(entry.path(), meta_text) && ApplyMeta(meta_text)) {
            loaded_locale = locale;
            return true;
        }
    }

    LOG_WARNING(Loader, "No usable tropmeta_<locale>.json in {}", dir.string());
    return true;
}

UCP::UCP() = default;
UCP::~UCP() = default;

bool UCP::Open(const std::filesystem::path& ucpPath) {
    entries.clear();
    m_path.clear();

    Common::FS::IOFile file(ucpPath, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        LOG_WARNING(Common_Filesystem, "Failed to open UCP file: {}", ucpPath.string());
        return false;
    }

    UcpHeader header{};
    if (!file.ReadObject(header)) {
        LOG_ERROR(Common_Filesystem, "UCP file too small to contain a header: {}",
                  ucpPath.string());
        return false;
    }

    if (header.magic != UCP_MAGIC) {
        LOG_ERROR(Common_Filesystem, "Invalid UCP magic in {}: expected 0x{:08X}, got 0x{:08X}",
                  ucpPath.string(), UCP_MAGIC, static_cast<u32>(header.magic));
        return false;
    }

    const u64 file_size = file.GetSize();
    const u32 num_files = header.num_files;
    const u64 toc_offset = header.toc_offset;

    // TOC begins with a 0x20-byte reserved block, then num_files entries of
    // 0x40 bytes each, packed with no gap.
    const u64 toc_bytes_needed =
        UCP_TOC_RESERVED_SIZE + static_cast<u64>(num_files) * sizeof(UcpTocEntry);
    if (toc_offset > file_size || toc_bytes_needed > file_size - toc_offset) {
        LOG_ERROR(Common_Filesystem,
                  "UCP table of contents in {} extends past end of file (toc_offset=0x{:X}, "
                  "num_files={})",
                  ucpPath.string(), toc_offset, num_files);
        return false;
    }

    if (!file.Seek(static_cast<s64>(toc_offset + UCP_TOC_RESERVED_SIZE))) {
        LOG_ERROR(Common_Filesystem, "Failed to seek to UCP table of contents in {}",
                  ucpPath.string());
        return false;
    }

    entries.reserve(num_files);
    for (u32 i = 0; i < num_files; i++) {
        UcpTocEntry raw{};
        if (!file.ReadObject(raw)) {
            LOG_ERROR(Common_Filesystem, "Failed to read UCP table of contents entry {} in {}", i,
                      ucpPath.string());
            return false;
        }

        // name may fill all 32 bytes with no NUL terminator; find its
        // actual length rather than trusting it's NUL-terminated.
        size_t name_len = 0;
        while (name_len < sizeof(raw.name) && raw.name[name_len] != '\0') {
            ++name_len;
        }

        UcpFileEntry entry;
        entry.name.assign(raw.name, name_len);
        entry.offset = raw.offset;
        entry.size = raw.size;

        if (entry.offset > file_size || entry.size > file_size - entry.offset) {
            LOG_WARNING(Common_Filesystem,
                        "UCP entry '{}' in {} has an out-of-bounds offset/size, skipping",
                        entry.name, ucpPath.string());
            continue;
        }

        entries.push_back(std::move(entry));
    }

    m_path = ucpPath;
    return true;
}

std::optional<UcpFileEntry> UCP::FindEntry(std::string_view name) const {
    for (const auto& entry : entries) {
        if (entry.name == name) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<u8>> UCP::ReadEntry(const UcpFileEntry& entry) const {
    if (m_path.empty()) {
        return std::nullopt;
    }

    Common::FS::IOFile file(m_path, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        return std::nullopt;
    }

    if (!file.Seek(static_cast<s64>(entry.offset))) {
        return std::nullopt;
    }

    std::vector<u8> data(entry.size);
    if (entry.size > 0 && file.ReadRaw<u8>(data.data(), data.size()) != data.size()) {
        LOG_ERROR(Common_Filesystem, "Failed to read UCP entry '{}' from {}", entry.name,
                  m_path.string());
        return std::nullopt;
    }

    return data;
}

std::optional<std::vector<u8>> UCP::ReadEntry(std::string_view name) const {
    const auto entry = FindEntry(name);
    if (!entry) {
        return std::nullopt;
    }
    return ReadEntry(*entry);
}

bool UCP::ExtractEntry(const UcpFileEntry& entry, const std::filesystem::path& outputPath) const {
    const auto data = ReadEntry(entry);
    if (!data) {
        return false;
    }

    Common::FS::IOFile out(outputPath, Common::FS::FileAccessMode::Write);
    if (!out.IsOpen()) {
        LOG_ERROR(Common_Filesystem, "Failed to create output file: {}", outputPath.string());
        return false;
    }

    return out.WriteSpan<u8>(*data) == data->size();
}

bool UCP::ExtractAll(const std::filesystem::path& outputDir) const {
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        LOG_ERROR(Common_Filesystem, "Failed to create output directory {}: {}", outputDir.string(),
                  ec.message());
        return false;
    }

    bool all_ok = true;
    for (const auto& entry : entries) {
        if (entry.name.empty()) {
            continue;
        }
        if (!ExtractEntry(entry, outputDir / entry.name)) {
            all_ok = false;
        }
    }
    return all_ok;
}

bool UCP::ExtractTrophyFiles(const std::filesystem::path& outputDir) const {
    std::error_code ec;
    std::filesystem::create_directories(outputDir / "Icons", ec);
    if (ec) {
        LOG_ERROR(Common_Filesystem, "Failed to create trophy output directory {}: {}",
                  outputDir.string(), ec.message());
        return false;
    }

    bool have_conf = false;
    for (const auto& entry : entries) {
        if (entry.name.ends_with(".png")) {
            if (!ExtractEntry(entry, outputDir / "Icons" / entry.name)) {
                LOG_WARNING(Common_Filesystem, "Failed to extract UCP icon entry: {}", entry.name);
            }
            continue;
        }

        const bool is_conf = entry.name == "tropconf.json";
        const bool is_meta = !TrophySet::LocaleFromMetaFileName(entry.name).empty();
        if (!is_conf && !is_meta) {
            continue;
        }
        if (!ExtractEntry(entry, outputDir / entry.name)) {
            LOG_WARNING(Common_Filesystem, "Failed to extract UCP entry: {}", entry.name);
            continue;
        }
        have_conf = have_conf || is_conf;
    }

    if (!have_conf) {
        LOG_ERROR(Common_Filesystem, "UCP trophy container has no tropconf.json: {}",
                  m_path.string());
        return false;
    }
    return true;
}

bool UCP::IsContainerFileName(std::string_view name) {
    if (name.size() < 4) {
        return false;
    }
    std::string ext(name.substr(name.size() - 4));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".ucp";
}

std::vector<std::filesystem::path> UCP::ListContainers(const std::filesystem::path& trophyDir) {
    std::vector<std::filesystem::path> found;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(trophyDir, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file(ec) && IsContainerFileName(entry.path().filename().string())) {
            found.push_back(entry.path());
        }
    }

    // directory_iterator order is unspecified, so sort for a stable result.
    std::sort(found.begin(), found.end());
    return found;
}

std::string UCP::ReadNpCommId() const {
    const auto conf = ReadEntry("tropconf.json");
    if (!conf) {
        return {};
    }
    try {
        const auto j = nlohmann::json::parse(
            std::string_view(reinterpret_cast<const char*>(conf->data()), conf->size()));
        if (j.is_object()) {
            return GetString(j, "trophyNpCommId");
        }
    } catch (const nlohmann::json::exception&) {
        // Fall through: an unreadable tropconf just means this container
        // can't identify itself, and the caller falls back to naming.
    }
    return {};
}

std::optional<std::filesystem::path> UCP::SelectContainerFor(
    const std::vector<std::filesystem::path>& candidates, std::string_view npCommId, int index) {
    if (candidates.empty()) {
        return std::nullopt;
    }

    // Preferred: ask each container which trophy set it actually holds.
    if (!npCommId.empty()) {
        for (const auto& path : candidates) {
            UCP ucp;
            if (!ucp.Open(path)) {
                continue;
            }
            if (ucp.ReadNpCommId() == npCommId) {
                return path;
            }
        }
    }

    // Fallback: the conventional Trophy<NN>.ucp / uds<NN>.ucp naming, for
    // containers whose tropconf couldn't be read.
    if (index >= 0) {
        for (const char* stem : {"Trophy", "uds"}) {
            const auto wanted = fmt::format("{}{:02}.ucp", stem, index);
            for (const auto& path : candidates) {
                if (path.filename().string() == wanted) {
                    return path;
                }
            }
        }
    }

    // Single-set titles are the common case: if there is exactly one
    // container and nothing matched, it is unambiguously the right one.
    if (candidates.size() == 1) {
        LOG_WARNING(Common_Filesystem, "Using sole trophy container {} for NPComm ID '{}'",
                    candidates.front().string(), std::string(npCommId));
        return candidates.front();
    }

    LOG_ERROR(Common_Filesystem, "No trophy container matches NPComm ID '{}' among {} candidates",
              std::string(npCommId), candidates.size());
    return std::nullopt;
}

std::optional<std::filesystem::path> UCP::FindContainerFor(const std::filesystem::path& trophyDir,
                                                           std::string_view npCommId, int index) {
    return SelectContainerFor(ListContainers(trophyDir), npCommId, index);
}

bool UCP::LoadTrophySet(TrophySet& out, int language_index) const {
    const auto conf = ReadEntry("tropconf.json");
    if (!conf) {
        LOG_ERROR(Common_Filesystem, "UCP trophy container has no tropconf.json: {}",
                  m_path.string());
        return false;
    }
    if (!out.ParseConf(
            std::string_view(reinterpret_cast<const char*>(conf->data()), conf->size()))) {
        return false;
    }

    // Same preference order LoadFromDir uses: the requested language, the
    // set's own default, then anything the container actually packs.
    std::vector<std::string> candidates;
    if (const auto locale = TrophySet::LocaleForLanguage(language_index); !locale.empty()) {
        candidates.emplace_back(locale);
    }
    if (!out.default_language.empty()) {
        candidates.push_back(out.default_language);
    }
    for (const auto& entry : entries) {
        if (auto locale = TrophySet::LocaleFromMetaFileName(entry.name); !locale.empty()) {
            candidates.push_back(std::move(locale));
        }
    }

    for (const auto& locale : candidates) {
        const auto meta = ReadEntry(TrophySet::MetaFileNameForLocale(locale));
        if (!meta) {
            continue;
        }
        if (out.ApplyMeta(
                std::string_view(reinterpret_cast<const char*>(meta->data()), meta->size()))) {
            out.loaded_locale = locale;
            return true;
        }
    }

    // Definitions without display strings are still a usable set.
    LOG_WARNING(Common_Filesystem, "No usable tropmeta in UCP container: {}", m_path.string());
    return true;
}

bool TrophyProgress::Parse(std::string_view json_text) {
    m_entries.clear();
    np_comm_id.clear();
    set_version.clear();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Loader, "Failed to parse trophy progress: {}", e.what());
        return false;
    }
    if (!j.is_object()) {
        return false;
    }

    np_comm_id = j.value("trophyNpCommId", "");
    set_version = j.value("trophySetVersion", "");

    const auto list = j.find("trophies");
    if (list == j.end() || !list->is_array()) {
        // A file with no trophies array is still a valid "nothing earned yet"
        // state; don't treat it as corrupt.
        return true;
    }

    for (const auto& t : *list) {
        if (!t.is_object()) {
            continue;
        }
        const auto id_it = t.find("id");
        if (id_it == t.end() || !id_it->is_string()) {
            continue;
        }
        TrophyProgressEntry entry;
        entry.unlocked = t.value("unlocked", false);
        if (const auto ts = t.find("timestamp"); ts != t.end() && ts->is_number_integer()) {
            entry.timestamp = ts->get<s64>();
        }
        m_entries[id_it->get<std::string>()] = entry;
    }

    return true;
}

std::string TrophyProgress::Serialize() const {
    nlohmann::json j;
    j["trophyNpCommId"] = np_comm_id;
    j["trophySetVersion"] = set_version;

    // Sorted by id so the file is stable across saves and diffs cleanly.
    std::vector<std::string> ids;
    ids.reserve(m_entries.size());
    for (const auto& [id, _] : m_entries) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());

    nlohmann::json list = nlohmann::json::array();
    for (const auto& id : ids) {
        const auto& entry = m_entries.at(id);
        nlohmann::json t;
        t["id"] = id;
        t["unlocked"] = entry.unlocked;
        if (entry.timestamp != 0) {
            t["timestamp"] = entry.timestamp;
        }
        list.push_back(std::move(t));
    }
    j["trophies"] = std::move(list);

    return j.dump(4);
}

bool TrophyProgress::Load(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        m_entries.clear();
        return true;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR(Loader, "Failed to open trophy progress: {}", path.string());
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    return Parse(text);
}

bool TrophyProgress::Save(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        LOG_ERROR(Loader, "Failed to write trophy progress: {}", path.string());
        return false;
    }
    file << Serialize();
    return file.good();
}

TrophyProgressEntry TrophyProgress::Get(const std::string& trophy_id) const {
    const auto it = m_entries.find(trophy_id);
    return it != m_entries.end() ? it->second : TrophyProgressEntry{};
}

void TrophyProgress::Set(const std::string& trophy_id, TrophyProgressEntry entry) {
    m_entries[trophy_id] = entry;
}

void TrophyProgress::SeedFrom(const TrophySet& set) {
    if (np_comm_id.empty()) {
        np_comm_id = set.np_comm_id;
    }
    if (set_version.empty()) {
        set_version = set.set_version;
    }
    for (const auto& def : set.trophies) {
        if (m_entries.find(def.id) == m_entries.end()) {
            m_entries[def.id] = TrophyProgressEntry{};
        }
    }
}

std::size_t TrophyProgress::UnlockedCount() const {
    std::size_t count = 0;
    for (const auto& [_, entry] : m_entries) {
        if (entry.unlocked) {
            ++count;
        }
    }
    return count;
}
