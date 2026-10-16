// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/crypto.h"
#include "core/file_format/ps5_pkg.h"

namespace PS5Pkg {

// The passcode almost every debug package is built with: 32 ASCII zeros.
inline constexpr std::string_view DEFAULT_PASSCODE = "00000000000000000000000000000000";

enum class BlockCheck {
    NotChecked,
    Ok,
    Mismatch,
    Unreadable,
};

struct OuterFile {
    std::string path; // "uroot/pfs_image.dat"
    u32 inode = 0;
    bool is_dir = false;
    u16 mode = 0;
    u32 flags = 0;
    u64 size = 0;
    u64 logical_size = 0; // the inode's second size field
    u32 block_count = 0;
    // Direct blocks only (the first 12); `all_direct` says whether that is all of them.
    std::vector<u32> blocks;
    std::vector<Common::Crypto::Digest256> hashes;
    bool all_direct = false;
    // How the first block decrypted, and with which sector numbering.
    // Indirect pointers (single, double, ...) for files beyond 12 blocks.
    std::vector<u64> indirect_blocks;
    std::vector<Common::Crypto::Digest256> indirect_hashes;
    bool wide_pointers = false;
    BlockCheck first_block = BlockCheck::NotChecked;
    bool data_sectors = false; // true: sector = block; false: sector = block | 1 << 47
};

struct KeyCheck {
    bool verified = false;
    bool plaintext = false; // the image is not encrypted at all
    std::string error;      // why verification failed or was not possible
    std::string scheme;
    std::string ekpfs_fingerprint; // first 4 bytes, hex
    std::string passcode;
    u32 blocks_checked = 0;
    u32 blocks_matched = 0;
    std::vector<OuterFile> files;
    std::string game_data_hint; // what the first block of the game data looks like

    // Key material for reading outer files after a successful check.
    std::array<u8, 16> data_key{};
    std::array<u8, 16> tweak_key{};
    Common::Crypto::Digest256 sign_key{}; // for HMAC-signed blocks
    u64 image_offset = 0;
    u32 block_size = 0;
};

[[nodiscard]] KeyCheck VerifyKeys(const PkgInfo& info,
                                  std::string_view passcode = DEFAULT_PASSCODE);

[[nodiscard]] std::optional<std::vector<u8>> ReadOuterFile(const PkgInfo& info,
                                                           const KeyCheck& keys,
                                                           const OuterFile& file,
                                                           std::string* problem = nullptr);
class OuterFileReader {
public:
    OuterFileReader(const PkgInfo& info, const KeyCheck& keys, const OuterFile& file);
    ~OuterFileReader();
    OuterFileReader(const OuterFileReader&) = delete;
    OuterFileReader& operator=(const OuterFileReader&) = delete;

    bool Open(std::string* problem = nullptr);
    bool Read(u64 offset, std::span<u8> out, std::string* problem = nullptr);
    [[nodiscard]] u64 Size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Slot in the package key table that an encrypted metadata entry uses.
[[nodiscard]] inline u32 EntryKeyIndex(const CntEntry& entry) {
    return (entry.flags2 & 0xF000) >> 12;
}
[[nodiscard]] std::optional<std::vector<u8>> DecryptEntry(
    const PkgInfo& info, const CntEntry& entry, std::string_view passcode = DEFAULT_PASSCODE,
    std::string* problem = nullptr);

struct NpInfo {
    std::vector<std::string> np_comm_ids; // from every npbind.dat, de-duplicated
    std::string np_title_id;              // nptitle.dat, e.g. "PPSA34875_00"

    bool has_license = false; // license.dat (RIF)
    u16 rif_version = 0;
    u64 account_id = 0;  // 0: not bound to an account
    u64 start_time = 0;  // seconds since 1970
    u64 expiry_time = 0; // 0x7FFFFFFFFFFFFFFF: never
    std::string license_content_id;
    std::string license_info_content_id;

    std::vector<std::string> problems; // entries that could not be read
    [[nodiscard]] bool IsEmpty() const {
        return np_comm_ids.empty() && np_title_id.empty() && !has_license &&
               license_info_content_id.empty();
    }
};

[[nodiscard]] NpInfo ReadNpInfo(const PkgInfo& info, std::string_view passcode = DEFAULT_PASSCODE);

} // namespace PS5Pkg
