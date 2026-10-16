// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"
#include "core/file_format/param.h"

// Read-only inspection of PS5 package files. Only plaintext structures are
// read: the finalized image header (FIH), the metadata container (CNT) with
// its entry table, and the outer PFS superblock. Nothing is decrypted.
namespace PS5Pkg {

enum class PackageKind {
    Unknown,
    Debug,        // "\x7FFIH", signed byte 0x00 (fake / debug package)
    Retail,       // "\x7FFIH", signed byte 0x80
    Patch,        // "\x7FLIH": update package
    MetadataOnly, // bare "\x7FCNT" container with no PS4 markers
    PS4,          // "\x7FCNT" carrying param.sfo: a PS4 package
};

// From the CNT content flags (header 0x78).
enum class PatchKind {
    None,
    First,
    Subsequent,
    Delta,
    Cumulative,
};

// Well-known CNT entry ids.
inline constexpr u32 ENTRY_DIGESTS = 0x0001;
inline constexpr u32 ENTRY_NAMES = 0x0200;
inline constexpr u32 ENTRY_PARAM_SFO = 0x1000;
inline constexpr u32 ENTRY_ICON0_PNG = 0x1200;
inline constexpr u32 ENTRY_PIC0_PNG = 0x1220;
inline constexpr u32 ENTRY_PIC1_PNG = 0x1006;
inline constexpr u32 ENTRY_ICON0_DDS = 0x1280;
inline constexpr u32 ENTRY_PIC0_DDS = 0x12A0;
inline constexpr u32 ENTRY_PIC1_DDS = 0x12C0;
inline constexpr u32 ENTRY_PARAM_JSON = 0x2000;
inline constexpr u32 ENTRY_PIC2_PNG = 0x2040;
inline constexpr u32 ENTRY_PIC2_DDS = 0x2060;

inline constexpr u32 ENTRY_FLAG_ENCRYPTED = 0x80000000;

struct CntEntry {
    u32 id = 0;
    u32 name_offset = 0;
    u32 flags1 = 0;
    u32 flags2 = 0;
    u32 offset = 0; // relative to the start of the CNT container
    u32 size = 0;
    std::string name; // from the entry-name table, else a well-known name, else empty

    [[nodiscard]] bool IsEncrypted() const {
        return (flags1 & ENTRY_FLAG_ENCRYPTED) != 0;
    }
};

struct PfsSuperblockInfo {
    bool found = false;
    u64 offset = 0; // absolute file offset of the superblock
    s64 version = 0;
    u16 mode = 0;
    u32 block_size = 0;
    s64 block_count = 0;
    s64 dinode_count = 0;

    [[nodiscard]] bool IsSigned() const {
        return (mode & 0x1) != 0;
    }
    [[nodiscard]] bool Is64Bit() const {
        return (mode & 0x2) != 0;
    }
    [[nodiscard]] bool IsEncrypted() const {
        return (mode & 0x4) != 0;
    }
};

// A top-level region of the file.
struct Segment {
    std::string name;        // "FIH", "PFS", "SC", "SI", "LIH"
    std::string description; // human-readable
    u64 offset = 0;
    u64 size = 0;
};

// A member of the install-metadata zip (SI segment).
struct SiMember {
    std::string name;
    u16 method = 0; // 0 = stored, 8 = deflate
    u32 crc32 = 0;
    u64 compressed_size = 0;
    u64 size = 0;
    u64 local_header_offset = 0; // absolute file offset of the local header
};

// One artwork image, stored as PNG and/or DDS in the CNT.
struct ArtworkSlot {
    std::string_view name; // "icon0"
    u32 png_id;            // 0 when the slot has no PNG form
    u32 dds_id;
};
inline constexpr ArtworkSlot ARTWORK_SLOTS[] = {
    {"icon0", ENTRY_ICON0_PNG, ENTRY_ICON0_DDS},
    {"pic0", ENTRY_PIC0_PNG, ENTRY_PIC0_DDS},
    {"pic1", ENTRY_PIC1_PNG, ENTRY_PIC1_DDS},
    {"pic2", ENTRY_PIC2_PNG, ENTRY_PIC2_DDS},
};

struct PkgInfo {
    std::filesystem::path path;
    u64 file_size = 0;
    PackageKind kind = PackageKind::Unknown;
    std::string error; // set when the file could not be read as a package

    // Finalized image header (FIH, little-endian). Zero for a bare CNT.
    bool has_fih = false;
    u8 signed_byte = 0;
    u16 format_version = 0;
    u64 pfs_offset = 0;
    u64 pfs_size = 0;
    u64 superblock_offset = 0; // FIH 0x20, absolute
    u64 cnt_offset = 0;
    u32 inner_image_blocks = 0;   // 0x90
    u32 content_inode_count = 0;  // 0x94: folders + files in the game image
    u32 content_version_echo = 0; // 0x9C: BCD content version, e.g. 0x01000007
    u64 inner_image_size = 0;     // 0xA0: size of the game data region
    u64 layout_table_size = 0;    // 0xA8: length of naps_pkg_layout.dat
    u32 app_file_count = 0;       // 0xF0: files outside sce_sys

    // Patch image header (LIH, little-endian).
    bool has_lih = false;
    u16 lih_version = 0;

    // Metadata container (CNT, big-endian).
    bool has_cnt = false;
    u32 cnt_flags = 0;
    u32 entry_count = 0;
    u64 body_offset = 0;
    u64 body_size = 0;
    std::string content_id;
    u32 drm_type = 0;
    u32 content_type = 0;
    u32 content_flags = 0;
    std::vector<CntEntry> entries;
    u64 cnt_size = 0; // end of the furthest entry, relative to cnt_offset
    size_t encrypted_entry_count = 0;
    PatchKind patch_kind = PatchKind::None;

    std::vector<Segment> segments;

    // Install-metadata zip at the end of the image, if present.
    u64 si_offset = 0;
    u64 si_size = 0;
    std::vector<SiMember> si_members;
    std::string si_problem;

    PfsSuperblockInfo outer_pfs;

    // Parsed when the param.json entry is present and not encrypted.
    std::optional<Param> param;
    std::string param_problem; // why param is empty, if it is

    [[nodiscard]] const CntEntry* FindEntry(u32 id) const;

    // Whether the files inside this package could, in principle, be extracted
    // without console keys (debug packages only).
    [[nodiscard]] bool IsExtractableKind() const {
        return kind == PackageKind::Debug;
    }
};

// True when the file starts with a PS5 finalized image or a CNT container.
[[nodiscard]] bool LooksLikePackage(const std::filesystem::path& path);

// Reads every plaintext structure. Always returns; check `error` and `kind`.
[[nodiscard]] PkgInfo ReadPkgInfo(const std::filesystem::path& path);

// Reads one entry's payload. Fails for encrypted entries and out-of-range data.
[[nodiscard]] std::optional<std::vector<u8>> ReadEntryData(const PkgInfo& info,
                                                           const CntEntry& entry,
                                                           std::string* problem = nullptr);

// Reads and, when needed, inflates one SI member.
[[nodiscard]] std::optional<std::vector<u8>> ReadSiMember(const PkgInfo& info,
                                                          const SiMember& member,
                                                          std::string* problem = nullptr);

// The first readable form of an artwork slot. `is_dds` says which form it is.
struct ArtworkData {
    std::vector<u8> bytes;
    bool is_dds = false;
    std::string entry_name;
};
[[nodiscard]] std::optional<ArtworkData> ReadArtwork(const PkgInfo& info, const ArtworkSlot& slot);

// "Americas", "Europe", ... from the first letter of a content ID.
[[nodiscard]] std::string_view RegionFromContentId(std::string_view content_id);

[[nodiscard]] std::string_view PatchKindName(PatchKind kind);
[[nodiscard]] std::string_view KindName(PackageKind kind);
[[nodiscard]] std::string_view WellKnownEntryName(u32 id);
[[nodiscard]] std::string ContentTypeName(u32 content_type);
[[nodiscard]] std::string DrmTypeName(u32 drm_type);
// 0x01000007 -> "01.000.007"; empty for 0.
[[nodiscard]] std::string FormatContentVersion(u32 bcd);

} // namespace PS5Pkg
