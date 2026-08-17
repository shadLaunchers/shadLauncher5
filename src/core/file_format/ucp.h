// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/endian.h"
#include "common/types.h"

// Also used for the near-identical "uds00.ucp" variant.
static constexpr u32 UCP_MAGIC = 0xB228C60A;

static constexpr u32 UCP_TOC_RESERVED_SIZE = 0x20;

struct UcpHeader {
    u32_be magic;      // 0xB228C60A
    u32_be version;    // 1
    u64_be file_size;  // total file size
    u32_be num_files;  // number of contained files
    u32_be toc_offset; // location of the table of contents (usually 0x40)
    u64_be unknown;    // unknown, purpose not documented
    unsigned char hmac[0x10];
};
static_assert(sizeof(UcpHeader) == 0x30);

struct UcpTocEntry {
    char name[0x20]; // file name, NUL-padded (not necessarily NUL-terminated
                     // if it fills all 32 bytes)
    u64_be offset;   // absolute file offset
    u64_be size;     // file size in bytes
    unsigned char reserved[0x10];
};
static_assert(sizeof(UcpTocEntry) == 0x40);

struct UcpFileEntry {
    std::string name;
    u64 offset;
    u64 size;
};

enum class TrophyGrade {
    Unknown,
    Platinum,
    Gold,
    Silver,
    Bronze,
};

// "unlockCondition": how the UDS stat backing this trophy triggers it.
struct TrophyUnlockCondition {
    std::string uds_stat_id;
    std::string comparator; // "ge", "gt", "le", "lt", "eq"
    std::string target_value;
    bool progressive = false;
};

struct TrophyDefinition {
    std::string id; // zero-padded, e.g. "0000"
    TrophyGrade grade = TrophyGrade::Unknown;
    bool hidden = false;
    bool has_reward = false;
    // "platinumTrophyId": the platinum this trophy counts toward. Empty on the
    // platinum itself, and on sets that have no platinum at all.
    std::string platinum_id;

    bool has_unlock_condition = false;
    TrophyUnlockCondition unlock_condition;

    // From tropmeta_<locale>.json; empty when that locale has no entry for
    // this id, which is why the definition list is what drives iteration.
    std::string name;
    std::string detail;

    // Icon file name inside the container, derived from `id`: "trop0000.png".
    [[nodiscard]] std::string IconFileName() const;
};

class TrophySet {
public:
    // Loads tropconf.json plus the best-matching tropmeta for
    // `language_index` (a Prospero language index, see LocaleForLanguage).
    // Locale resolution: the requested language, then defaultLanguage, then
    // whichever tropmeta_*.json is present.
    bool LoadFromDir(const std::filesystem::path& dir, int language_index);

    // Parses a tropconf.json buffer. Resets the whole object first.
    bool ParseConf(std::string_view json_text);
    // Layers a tropmeta_<locale>.json buffer's strings onto the definitions
    // already loaded by ParseConf. Unknown ids are ignored.
    bool ApplyMeta(std::string_view json_text);

    void Reset();

    // From tropconf.json
    std::string schema_version;
    s64 definition_revision = 0;
    std::vector<std::string> platform; // e.g. ["PS5"]
    std::string np_comm_id;            // "trophyNpCommId"
    std::string uds_np_comm_id;
    std::string set_version; // "trophySetVersion"
    std::string default_language;
    std::vector<std::string> languages;
    std::vector<TrophyDefinition> trophies;

    // From tropmeta_<locale>.json
    std::string title_name; // metadata.titleMetadata.name
    std::string loaded_locale;

    [[nodiscard]] bool IsValid() const {
        return !trophies.empty();
    }

    // Index into `trophies`, or -1.
    [[nodiscard]] int FindById(std::string_view id) const;

    [[nodiscard]] static TrophyGrade GradeFromCode(std::string_view code);
    [[nodiscard]] static std::string_view GradeCode(TrophyGrade grade);
    // Human-readable, for tooltips and exports.
    [[nodiscard]] static std::string_view GradeName(TrophyGrade grade);
    // Bundled badge image name, e.g. "platinum.png".
    [[nodiscard]] static std::string_view GradeIconFileName(TrophyGrade grade);

    // Prospero language index <-> tropmeta locale code ("en-US"), the same
    // indexing param.json's localizedParameters uses.
    [[nodiscard]] static std::string_view LocaleForLanguage(int language_index);
    [[nodiscard]] static int LanguageForLocale(std::string_view locale);

    // "tropmeta_en-US.json" -> "en-US". Empty if the name doesn't match.
    [[nodiscard]] static std::string LocaleFromMetaFileName(std::string_view file_name);
    [[nodiscard]] static std::string MetaFileNameForLocale(std::string_view locale);
};

class UCP {
public:
    UCP();
    ~UCP();

    // Parses the header and table of contents. Does not read file contents.
    bool Open(const std::filesystem::path& ucpPath);

    [[nodiscard]] const std::vector<UcpFileEntry>& GetEntries() const {
        return entries;
    }

    // Finds a contained file by exact name (as stored in the TOC, e.g.
    // "tropconf.json" or a PNG file name). Returns nullopt if not found.
    [[nodiscard]] std::optional<UcpFileEntry> FindEntry(std::string_view name) const;

    // Reads a contained file's raw bytes into memory.
    [[nodiscard]] std::optional<std::vector<u8>> ReadEntry(const UcpFileEntry& entry) const;
    [[nodiscard]] std::optional<std::vector<u8>> ReadEntry(std::string_view name) const;

    // Extracts a contained file to disk, preserving its raw bytes as-is.
    bool ExtractEntry(const UcpFileEntry& entry, const std::filesystem::path& outputPath) const;

    // Extracts every contained file into outputDir, using each entry's
    // stored name as the output filename.
    bool ExtractAll(const std::filesystem::path& outputDir) const;

    // Unpacks only what a trophy viewer needs: PNG icons into
    // outputDir/"Icons", and the definition JSON into outputDir itself, both
    // byte-for-byte as packed. Fails if the container has no tropconf.json.
    bool ExtractTrophyFiles(const std::filesystem::path& outputDir) const;

    // Parses the trophy set straight out of the container, with no extraction
    // step -- useful for inspecting a title's trophies without writing
    // anything to disk.
    bool LoadTrophySet(TrophySet& out, int language_index) const;

    // "trophyNpCommId" from the container's tropconf.json, without parsing
    // the rest of it. Empty if the container has no readable tropconf.
    [[nodiscard]] std::string ReadNpCommId() const;

    // Picks the container that belongs to `npCommId` out of `candidates`.
    [[nodiscard]] static std::optional<std::filesystem::path> SelectContainerFor(
        const std::vector<std::filesystem::path>& candidates, std::string_view npCommId, int index);

    [[nodiscard]] static std::optional<std::filesystem::path> FindContainerFor(
        const std::filesystem::path& trophyDir, std::string_view npCommId, int index);

    // Every *.ucp in trophyDir, sorted by name for a stable order.
    [[nodiscard]] static std::vector<std::filesystem::path> ListContainers(
        const std::filesystem::path& trophyDir);

    // True if a file name looks like a trophy container ("*.ucp", any case).
    [[nodiscard]] static bool IsContainerFileName(std::string_view name);

private:
    std::filesystem::path m_path;
    std::vector<UcpFileEntry> entries;
};

struct TrophyProgressEntry {
    bool unlocked = false;
    s64 timestamp = 0;
};

class TrophyProgress {
public:
    bool Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;

    bool Parse(std::string_view json_text);
    [[nodiscard]] std::string Serialize() const;

    [[nodiscard]] TrophyProgressEntry Get(const std::string& trophy_id) const;
    void Set(const std::string& trophy_id, TrophyProgressEntry entry);

    [[nodiscard]] bool IsUnlocked(const std::string& trophy_id) const {
        return Get(trophy_id).unlocked;
    }

    [[nodiscard]] std::size_t UnlockedCount() const;
    void SeedFrom(const TrophySet& set);

    std::string np_comm_id;
    std::string set_version;

private:
    std::unordered_map<std::string, TrophyProgressEntry> m_entries;
};
