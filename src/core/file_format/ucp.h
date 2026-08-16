// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// History:
//   2026-07-23  Added support for PS5's "NpTrophy V2" trophy pack container
//               (Trophy00.ucp / uds00.ucp), which replaces the PS3/PS4/Vita
//               "TRP" format handled by TRP (trp.h). Structure per
//               psdevwiki.com/ps5/Trophy00.ucp.
//
// This class only understands the *container* -- the header and table of
// contents that let us list and extract the raw files a .ucp packs. The
// internal schema of the packed files (confirmed against a real
// "Ratchet & Clank: Rift Apart" Trophy00.ucp sample) is:
//
//   icon0_<locale>.png       - per-locale trophy-set icon (e.g. icon0_en-US.png)
//   trop00XX.png              - per-trophy icon, XX = zero-padded trophy id
//   tropconf.json              - language-independent trophy set definition:
//       { schemaVersion, trophyDefinitionRevision, platform: ["PS5"],
//         trophyNpCommId, udsNpCommId, trophySetVersion, defaultLanguage,
//         languages: [...],
//         trophies: [ { id, hidden, grade: "P"|"G"|"S"|"B", hasReward,
//                       platinumTrophyId?,       // present on non-platinum trophies
//                       unlockCondition?: { udsStatId, comparator, targetValue,
//                                           progressive } } ] }
//   tropmeta_<locale>.json    - per-locale display strings:
//       { schemaVersion, trophyDefinitionRevision, trophySetVersion,
//         trophyNpCommId,
//         metadata: { titleMetadata: { name },
//                     trophyMetadata: [ { id, name, detail } ] } }
//
// This class still only extracts raw bytes -- parsing/interpreting this
// JSON is left to the caller (e.g. trophy_viewer.cpp), the same division of
// labor param.json has with its callers elsewhere in this codebase.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "common/endian.h"

// Also used for the near-identical "uds00.ucp" variant.
static constexpr u32 UCP_MAGIC = 0xB228C60A;

// Size of the unused block at the very start of the table of contents,
// before the first real entry. Confirmed against a real Trophy00.ucp
// sample: entries start at toc_offset + 0x20, not toc_offset + sizeof(entry)
// as the raw psdevwiki table layout might suggest at a glance.
static constexpr u32 UCP_TOC_RESERVED_SIZE = 0x20;

struct UcpHeader {
    u32_be magic;       // 0xB228C60A
    u32_be version;     // 1
    u64_be file_size;   // total file size
    u32_be num_files;   // number of contained files
    u32_be toc_offset;  // location of the table of contents (usually 0x40)
    u64_be unknown;     // unknown, purpose not documented
    unsigned char hmac[0x10];
};
static_assert(sizeof(UcpHeader) == 0x30);

// One 0x40-byte slot in the table of contents. The TOC begins with a
// 0x20-byte reserved/unused block (not a full entry), immediately followed
// by num_files real entries packed at this stride with no gap.
struct UcpTocEntry {
    char name[0x20];      // file name, NUL-padded (not necessarily NUL-terminated
                          // if it fills all 32 bytes)
    u64_be offset;         // absolute file offset
    u64_be size;            // file size in bytes
    unsigned char reserved[0x10];
};
static_assert(sizeof(UcpTocEntry) == 0x40);

struct UcpFileEntry {
    std::string name;
    u64 offset;
    u64 size;
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

    // Extracts a contained file to disk, preserving its raw bytes as-is
    // (PNG entries are already plain PNG data; no decryption is needed for
    // this format, unlike PS3/PS4's TRP).
    bool ExtractEntry(const UcpFileEntry& entry, const std::filesystem::path& outputPath) const;

    // Extracts every contained file into outputDir, using each entry's
    // stored name as the output filename.
    bool ExtractAll(const std::filesystem::path& outputDir) const;

private:
    std::filesystem::path m_path;
    std::vector<UcpFileEntry> entries;
};
