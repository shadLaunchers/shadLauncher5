// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps5_pkg_keys.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <fmt/format.h>

#include "core/file_format/npbind.h"

namespace PS5Pkg {

namespace {

using Common::Crypto::Digest256;

constexpr u64 kSignedSector = u64{1} << 47;
constexpr size_t kSeedOffset = 0x370;
constexpr char kPlainSeed[] = "PPRPLAIN-NOAUTH!"; // 16 bytes, no terminator compared
constexpr size_t kSuperblockSize = 0x400;
// Superblock fields (little-endian).
constexpr size_t kSbBlockSize = 0x20;
constexpr size_t kSbInodeCount = 0x30;
constexpr size_t kSbInodeBlockCount = 0x40;
// The superblock embeds a 64-bit signed dinode describing the inode table.
constexpr size_t kSbInodeTableDinode = 0x50;
constexpr size_t kDirectBlocks = 12;
constexpr size_t kIndirectLevels = 5;
constexpr u32 kMaxInodes = 1u << 20;
constexpr size_t kMaxFiles = 100000;
constexpr int kMaxDepth = 32;

// Dinode layouts, chosen by the superblock's 64-bit mode flag. Both are the
// "signed" forms: each block pointer carries the block's SHA3-256 hash.
struct DinodeLayout {
    size_t size;
    size_t blocks_offset;
    size_t pointers_offset;
    size_t pointer_size;
    bool wide;
};
constexpr DinodeLayout kSigned32 = {0x2C8, 0x60, 0x64, 36, false};
constexpr DinodeLayout kSigned64 = {0x310, 0x60, 0x68, 40, true};

constexpr u32 kDirentFile = 2;
constexpr u32 kDirentDir = 3;

u16 Le16(const u8* p) {
    return static_cast<u16>(p[0] | (p[1] << 8));
}
u32 Le32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}
u64 Le64(const u8* p) {
    return static_cast<u64>(Le32(p)) | (static_cast<u64>(Le32(p + 4)) << 32);
}

std::string HexPrefix(std::span<const u8> d, size_t n) {
    std::string s;
    for (size_t i = 0; i < std::min(n, d.size()); i++) {
        s += fmt::format("{:02X}", d[i]);
    }
    return s;
}

std::string DescribeData(std::span<const u8> d) {
    if (d.size() >= 4 && Le32(d.data()) == 0xEEF51454) {
        return "PS5 SELF executable header";
    }
    if (d.size() >= 4 && std::memcmp(d.data(),
                                     "\x7F"
                                     "ELF",
                                     4) == 0) {
        return "ELF executable header";
    }
    size_t printable = 0;
    while (printable < d.size() && printable < 32 && d[printable] >= 0x20 && d[printable] < 0x7F) {
        printable++;
    }
    if (printable >= 4) {
        return fmt::format("text \"{}\"",
                           std::string(reinterpret_cast<const char*>(d.data()), printable));
    }
    return fmt::format("bytes {}", HexPrefix(d, 16));
}

class Reader {
public:
    Reader(const PkgInfo& info, u64 image_offset, u32 block_size)
        : file_(info.path, std::ios::binary), file_size_(info.file_size),
          image_offset_(image_offset), block_size_(block_size) {}

    bool IsOpen() const {
        return static_cast<bool>(file_);
    }

    // Raw (still encrypted) block.
    bool ReadBlock(u64 block, std::vector<u8>& out) {
        const u64 offset = image_offset_ + block * block_size_;
        if (offset < image_offset_ || offset > file_size_ || file_size_ - offset < block_size_) {
            return false;
        }
        out.resize(block_size_);
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset));
        return static_cast<bool>(file_.read(reinterpret_cast<char*>(out.data()), block_size_));
    }

private:
    std::ifstream file_;
    u64 file_size_;
    u64 image_offset_;
    u32 block_size_;
};

struct Keys {
    Common::Crypto::Aes128 data;
    Common::Crypto::Aes128 tweak;
    Digest256 sign{};       // HMAC key for images signed the classic way
    bool plaintext = false; // the image is stored unencrypted
};

bool BlockMatches(const Keys& keys, std::span<const u8> plain, const Digest256& stored,
                  bool* hmac) {
    if (Common::Crypto::Sha3_256(plain) == stored) {
        if (hmac != nullptr) {
            *hmac = false;
        }
        return true;
    }
    if (Common::Crypto::HmacSha256(keys.sign, plain) == stored) {
        if (hmac != nullptr) {
            *hmac = true;
        }
        return true;
    }
    return false;
}

BlockCheck DecryptChecked(const Keys& keys, u64 block, const std::vector<u8>& raw,
                          const Digest256& hash, std::vector<u8>& plain, bool* data_sectors,
                          bool* hmac = nullptr) {
    if (keys.plaintext) {
        plain = raw;
        if (data_sectors != nullptr) {
            *data_sectors = true;
        }
        return BlockMatches(keys, plain, hash, hmac) ? BlockCheck::Ok : BlockCheck::Mismatch;
    }
    for (const bool data : {false, true}) {
        plain = raw;
        Common::Crypto::XtsDecrypt(keys.data, keys.tweak, data ? block : block | kSignedSector,
                                   plain);
        if (BlockMatches(keys, plain, hash, hmac)) {
            if (data_sectors != nullptr) {
                *data_sectors = data;
            }
            return BlockCheck::Ok;
        }
    }
    return BlockCheck::Mismatch;
}

struct Pointer {
    Digest256 hash{};
    u64 block = 0;
};

Pointer ReadPointer(const u8* p, bool wide) {
    Pointer ptr;
    std::memcpy(ptr.hash.data(), p, ptr.hash.size());
    ptr.block = wide ? Le64(p + 32) : Le32(p + 32);
    return ptr;
}

struct Inode {
    u16 mode = 0;
    u32 flags = 0;
    u64 size = 0;
    u64 logical_size = 0;
    u64 block_count = 0;
    std::vector<Pointer> direct;
    std::vector<Pointer> indirect; // single, double, triple, ... indirect blocks
};

Inode ParseInode(const u8* p, const DinodeLayout& layout) {
    Inode ino;
    ino.mode = Le16(p);
    ino.flags = Le32(p + 4);
    ino.size = Le64(p + 8);
    ino.logical_size = Le64(p + 16);
    ino.block_count = layout.wide ? Le64(p + layout.blocks_offset) : Le32(p + layout.blocks_offset);
    const size_t n = static_cast<size_t>(std::min<u64>(ino.block_count, kDirectBlocks));
    for (size_t i = 0; i < n; i++) {
        ino.direct.push_back(
            ReadPointer(p + layout.pointers_offset + i * layout.pointer_size, layout.wide));
    }
    if (ino.block_count > kDirectBlocks) {
        for (size_t i = 0; i < kIndirectLevels; i++) {
            ino.indirect.push_back(
                ReadPointer(p + layout.pointers_offset + (kDirectBlocks + i) * layout.pointer_size,
                            layout.wide));
        }
    }
    return ino;
}

} // namespace

KeyCheck VerifyKeys(const PkgInfo& info, std::string_view passcode) {
    KeyCheck result;
    result.passcode = std::string(passcode);
    auto fail = [&](std::string why) {
        result.error = std::move(why);
        return result;
    };

    // Retail-signed packages are tried too: fake packages often keep a retail
    // header, and the hash check below tells the two apart.
    const bool retail = info.kind == PackageKind::Retail;
    if (!info.has_fih) {
        return fail("Only finalized packages (FIH) can be verified.");
    }
    if (!info.outer_pfs.found || info.outer_pfs.offset <= info.pfs_offset) {
        return fail(retail ? "The filesystem header is encrypted, so this is a genuine retail "
                             "package; its keys are console keys."
                           : "The package's filesystem header was not found where expected.");
    }
    if (info.content_id.size() != 36) {
        return fail("The package has no valid 36-character content ID.");
    }
    if (passcode.size() != 32) {
        return fail("The passcode must be exactly 32 characters.");
    }

    const u64 image = info.pfs_offset;
    std::ifstream f(info.path, std::ios::binary);
    std::vector<u8> sb(kSuperblockSize);
    f.seekg(static_cast<std::streamoff>(info.outer_pfs.offset));
    if (!f || !f.read(reinterpret_cast<char*>(sb.data()), sb.size())) {
        return fail("The filesystem header could not be read.");
    }
    const u32 block_size = Le32(&sb[kSbBlockSize]);
    if (block_size < 0x1000 || block_size > (1u << 20) || (block_size & 0xFFF) != 0) {
        return fail(fmt::format("Unexpected filesystem block size 0x{:X}.", block_size));
    }
    if ((info.outer_pfs.mode & 0x1) == 0) {
        return fail("The filesystem is not signed, so its blocks carry no hashes to check.");
    }
    const DinodeLayout& layout = info.outer_pfs.Is64Bit() ? kSigned64 : kSigned32;
    const u64 inode_count = Le64(&sb[kSbInodeCount]);
    const u64 inode_blocks = Le64(&sb[kSbInodeBlockCount]);
    if (inode_count == 0 || inode_count > kMaxInodes || inode_blocks == 0 ||
        inode_blocks > kDirectBlocks) {
        return fail("The filesystem header has an unexpected inode table size.");
    }
    std::array<u8, 16> seed{};
    std::memcpy(seed.data(), &sb[kSeedOffset], seed.size());

    // The superblock's embedded dinode lists the inode table blocks and their hashes.
    std::vector<Pointer> table;
    for (u64 i = 0; i < inode_blocks; i++) {
        table.push_back(ReadPointer(
            &sb[kSbInodeTableDinode + kSigned64.pointers_offset + i * kSigned64.pointer_size],
            true));
    }

    Reader reader(info, image, block_size);
    if (!reader.IsOpen()) {
        return fail("The package could not be opened.");
    }
    std::vector<u8> raw, plain;
    if (!reader.ReadBlock(table[0].block, raw)) {
        return fail("The inode table lies outside the file.");
    }

    // Candidate keys: the PS5 SHA3-256 derivation first, then the SHA-256
    // variant; each with the HMAC key schedule and the older direct one.
    const std::string cid = info.content_id;
    auto derive = [&](bool sha3) {
        auto h = [&](std::span<const u8> d) {
            return sha3 ? Common::Crypto::Sha3_256(d) : Common::Crypto::Sha256(d);
        };
        const u8 index_be[4] = {0, 0, 0, 1};
        std::array<u8, 48> cid_padded{};
        std::memcpy(cid_padded.data(), cid.data(), cid.size());
        std::array<u8, 96> buf{};
        const auto a = h(index_be);
        const auto b = h(cid_padded);
        std::memcpy(buf.data(), a.data(), 32);
        std::memcpy(buf.data() + 32, b.data(), 32);
        std::memcpy(buf.data() + 64, passcode.data(), 32);
        return h(buf);
    };
    std::optional<Keys> keys;
    // Some fake-package builders leave the image unencrypted, mark the
    // superblock's seed with "PPRPLAIN-NOAUTH!" and still set the encrypted
    // mode bit. Such an image is recognised by its inode table hashing as is.
    if (Common::Crypto::Sha3_256(raw) == table[0].hash) {
        const Common::Crypto::Aes128 unused(std::span<const u8, 16>(seed.data(), 16));
        keys.emplace(Keys{unused, unused, {}, true});
        plain = raw;
        result.plaintext = true;
        result.scheme =
            std::memcmp(seed.data(), kPlainSeed, sizeof(seed)) == 0
                ? "none: unencrypted image (seed \"PPRPLAIN-NOAUTH!\"), SHA3-256 block hashes"
                : "none: unencrypted image, SHA3-256 block hashes";
        result.ekpfs_fingerprint = "-";
    }
    for (const bool sha3 : {true, false}) {
        if (keys) {
            break;
        }
        const Digest256 ekpfs = derive(sha3);
        for (const bool hmac_schedule : {true, false}) {
            const Digest256 base = hmac_schedule ? Common::Crypto::HmacSha256(ekpfs, seed) : ekpfs;
            std::array<u8, 20> msg{};
            msg[0] = 1; // index 1, little-endian
            std::memcpy(msg.data() + 4, seed.data(), seed.size());
            const Digest256 enc = Common::Crypto::HmacSha256(base, msg);
            msg[0] = 2; // index 2: the sign key
            const Digest256 sign = Common::Crypto::HmacSha256(base, msg);
            Keys candidate{Common::Crypto::Aes128(std::span<const u8, 16>(enc.data() + 16, 16)),
                           Common::Crypto::Aes128(std::span<const u8, 16>(enc.data(), 16)), sign};
            bool hmac_signed = false;
            if (DecryptChecked(candidate, table[0].block, raw, table[0].hash, plain, nullptr,
                               &hmac_signed) == BlockCheck::Ok) {
                keys.emplace(std::move(candidate));
                std::memcpy(result.tweak_key.data(), enc.data(), 16);
                std::memcpy(result.data_key.data(), enc.data() + 16, 16);
                result.sign_key = sign;
                result.scheme = fmt::format(
                    "{} key, {} key schedule, {}", sha3 ? "SHA3-256" : "SHA-256",
                    hmac_schedule ? "HMAC" : "direct",
                    hmac_signed ? "HMAC-SHA256 block signatures" : "SHA3-256 block hashes");
                result.ekpfs_fingerprint = HexPrefix(ekpfs, 4);
                break;
            }
        }
        if (keys) {
            break;
        }
    }
    result.blocks_checked = 1;
    if (!keys) {
        return fail(retail ? "The keys derived from the content ID do not decrypt this "
                             "package: it is a genuine retail package (console keys), or a "
                             "fake package built with a different passcode."
                           : "The derived keys do not decrypt this package. It may use a "
                             "different passcode, or it is not a debug package.");
    }
    result.blocks_matched = 1;
    result.image_offset = image;
    result.block_size = block_size;

    // Inode table.
    std::vector<u8> inode_table = plain;
    for (size_t i = 1; i < table.size(); i++) {
        result.blocks_checked++;
        if (!reader.ReadBlock(table[i].block, raw) ||
            DecryptChecked(*keys, table[i].block, raw, table[i].hash, plain, nullptr) !=
                BlockCheck::Ok) {
            return fail(fmt::format("Inode table block {} failed its hash check.", table[i].block));
        }
        result.blocks_matched++;
        inode_table.insert(inode_table.end(), plain.begin(), plain.end());
    }
    // Inodes never cross a block: each block holds block_size / inode size.
    const u64 per_block = block_size / layout.size;
    auto inode_at = [&](u64 n) -> std::optional<Inode> {
        const u64 at = (n / per_block) * block_size + (n % per_block) * layout.size;
        if (n >= inode_count || at + layout.size > inode_table.size()) {
            return std::nullopt;
        }
        return ParseInode(&inode_table[at], layout);
    };

    // Walk the directory tree from the super-root (inode 0).
    std::set<u64> visited;
    std::vector<u8> first_game_block;
    u64 largest = 0;
    auto walk = [&](auto&& self, u64 dir_ino, const std::string& prefix, int depth) -> void {
        if (depth > kMaxDepth || !visited.insert(dir_ino).second) {
            return;
        }
        const auto dir = inode_at(dir_ino);
        if (!dir) {
            return;
        }
        for (const auto& ptr : dir->direct) {
            result.blocks_checked++;
            if (!reader.ReadBlock(ptr.block, raw) ||
                DecryptChecked(*keys, ptr.block, raw, ptr.hash, plain, nullptr) != BlockCheck::Ok) {
                continue;
            }
            result.blocks_matched++;
            const std::vector<u8> listing = plain;
            size_t off = 0;
            while (off + 16 <= listing.size() && result.files.size() < kMaxFiles) {
                const u32 ino = Le32(&listing[off]);
                const u32 type = Le32(&listing[off + 4]);
                const u32 name_len = Le32(&listing[off + 8]);
                const u32 entry_size = Le32(&listing[off + 12]);
                if (entry_size < 16 || entry_size > listing.size() - off ||
                    name_len > entry_size - 16) {
                    break;
                }
                const std::string name(reinterpret_cast<const char*>(&listing[off + 16]), name_len);
                off += entry_size;
                if (type != kDirentFile && type != kDirentDir) {
                    continue; // "." and ".."
                }
                const auto node = inode_at(ino);
                if (!node) {
                    continue;
                }
                OuterFile file;
                file.path = prefix.empty() ? name : prefix + "/" + name;
                file.inode = ino;
                file.is_dir = type == kDirentDir;
                file.mode = node->mode;
                file.flags = node->flags;
                file.size = node->size;
                file.logical_size = node->logical_size;
                file.block_count = static_cast<u32>(node->block_count);
                file.all_direct = node->block_count <= kDirectBlocks;
                for (const auto& p : node->direct) {
                    file.blocks.push_back(static_cast<u32>(p.block));
                    file.hashes.push_back(p.hash);
                }
                for (const auto& p : node->indirect) {
                    file.indirect_blocks.push_back(p.block);
                    file.indirect_hashes.push_back(p.hash);
                }
                file.wide_pointers = layout.wide;
                if (!file.is_dir && !node->direct.empty()) {
                    result.blocks_checked++;
                    if (!reader.ReadBlock(node->direct[0].block, raw)) {
                        file.first_block = BlockCheck::Unreadable;
                    } else {
                        file.first_block =
                            DecryptChecked(*keys, node->direct[0].block, raw, node->direct[0].hash,
                                           plain, &file.data_sectors);
                        if (file.first_block == BlockCheck::Ok) {
                            result.blocks_matched++;
                            if (file.size > largest) {
                                largest = file.size;
                                first_game_block = plain;
                            }
                        }
                    }
                }
                result.files.push_back(std::move(file));
                if (type == kDirentDir) {
                    // Copy: the vector may reallocate while the subtree is added.
                    const std::string child = result.files.back().path;
                    self(self, ino, child, depth + 1);
                }
            }
        }
    };
    walk(walk, 0, {}, 0);

    if (result.files.empty()) {
        return fail("The keys matched, but the directory tree could not be read.");
    }
    if (!first_game_block.empty()) {
        result.game_data_hint = DescribeData(first_game_block);
    }
    result.verified = result.blocks_matched == result.blocks_checked;
    if (!result.verified) {
        result.error =
            fmt::format("{} of {} checked blocks failed their hash check.",
                        result.blocks_checked - result.blocks_matched, result.blocks_checked);
    }
    return result;
}

std::optional<std::vector<u8>> ReadOuterFile(const PkgInfo& info, const KeyCheck& keys,
                                             const OuterFile& file, std::string* problem) {
    auto fail = [&](std::string why) -> std::optional<std::vector<u8>> {
        if (problem != nullptr) {
            *problem = std::move(why);
        }
        return std::nullopt;
    };
    if (keys.block_size == 0 || file.is_dir) {
        return fail("Nothing to read.");
    }
    if (!file.all_direct || file.size > u64{keys.block_size} * file.blocks.size()) {
        return fail("This file is too large to read here.");
    }
    Reader reader(info, keys.image_offset, keys.block_size);
    const Keys k{Common::Crypto::Aes128(keys.data_key), Common::Crypto::Aes128(keys.tweak_key),
                 keys.sign_key, keys.plaintext};
    std::vector<u8> out, raw, plain;
    for (size_t i = 0; i < file.blocks.size(); i++) {
        if (!reader.ReadBlock(file.blocks[i], raw)) {
            return fail("The file's data lies outside the package.");
        }
        if (DecryptChecked(k, file.blocks[i], raw, file.hashes[i], plain, nullptr) !=
            BlockCheck::Ok) {
            return fail(fmt::format("Block {} failed its hash check.", file.blocks[i]));
        }
        out.insert(out.end(), plain.begin(), plain.end());
    }
    out.resize(static_cast<size_t>(file.size));
    return out;
}

namespace {

// SHA3-256( SHA3(index BE) || SHA3(content ID padded to 48) || passcode ).
Digest256 PasscodeKey(std::string_view content_id, std::string_view passcode, u32 index) {
    const u8 index_be[4] = {static_cast<u8>(index >> 24), static_cast<u8>(index >> 16),
                            static_cast<u8>(index >> 8), static_cast<u8>(index)};
    std::array<u8, 48> cid{};
    std::memcpy(cid.data(), content_id.data(), std::min<size_t>(content_id.size(), cid.size()));
    std::array<u8, 96> buf{};
    const auto a = Common::Crypto::Sha3_256(index_be);
    const auto b = Common::Crypto::Sha3_256(cid);
    std::memcpy(buf.data(), a.data(), 32);
    std::memcpy(buf.data() + 32, b.data(), 32);
    std::memcpy(buf.data() + 64, passcode.data(), std::min<size_t>(passcode.size(), 32));
    return Common::Crypto::Sha3_256(buf);
}

void StoreBe32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v >> 24);
    p[1] = static_cast<u8>(v >> 16);
    p[2] = static_cast<u8>(v >> 8);
    p[3] = static_cast<u8>(v);
}

} // namespace

std::optional<std::vector<u8>> DecryptEntry(const PkgInfo& info, const CntEntry& entry,
                                            std::string_view passcode, std::string* problem) {
    auto fail = [&](std::string why) -> std::optional<std::vector<u8>> {
        if (problem != nullptr) {
            *problem = std::move(why);
        }
        return std::nullopt;
    };
    if (!entry.IsEncrypted()) {
        return ReadEntryData(info, entry, problem);
    }
    // Retail-signed fake packages use the same scheme; the key-table check
    // below rejects genuine retail packages.
    if (info.kind != PackageKind::Debug && info.kind != PackageKind::Retail) {
        return fail("Only finalized packages' encrypted entries can be decrypted.");
    }
    if (info.content_id.size() != 36 || passcode.size() != 32) {
        return fail("A 36-character content ID and a 32-character passcode are needed.");
    }
    constexpr u32 kMaxEncryptedEntry = 16u << 20;
    if (entry.size == 0 || entry.size > kMaxEncryptedEntry) {
        return fail("The entry is empty or unexpectedly large.");
    }
    const u32 index = EntryKeyIndex(entry);
    const Digest256 key = PasscodeKey(info.content_id, passcode, index);

    // The key table stores, per slot, SHA3(key) XOR key: check the passcode against it.
    if (const CntEntry* keys = info.FindEntry(0x0010); keys != nullptr && !keys->IsEncrypted()) {
        if (const auto table = ReadEntryData(info, *keys);
            table && table->size() >= 32 + 32 * (size_t{index} + 1)) {
            Digest256 expect = Common::Crypto::Sha3_256(key);
            for (size_t i = 0; i < expect.size(); i++) {
                expect[i] ^= key[i];
            }
            if (std::memcmp(expect.data(), table->data() + 32 + 32 * index, 32) != 0) {
                return fail("The passcode does not match this package's key table.");
            }
        }
    }

    // Stored data is padded to the cipher block size.
    const u64 padded = (u64{entry.size} + 15) & ~u64{15};
    const u64 abs = info.cnt_offset + entry.offset;
    if (abs > info.file_size || info.file_size - abs < padded) {
        return fail("The entry data lies outside the file.");
    }
    std::vector<u8> data(static_cast<size_t>(padded));
    std::ifstream f(info.path, std::ios::binary);
    f.seekg(static_cast<std::streamoff>(abs));
    if (!f || !f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(padded))) {
        return fail("The entry data could not be read.");
    }

    // Key and IV: SHA3-256 of the entry's 32-byte table record followed by the slot key.
    std::array<u8, 64> seed{};
    StoreBe32(&seed[0x00], entry.id);
    StoreBe32(&seed[0x04], entry.name_offset);
    StoreBe32(&seed[0x08], entry.flags1);
    StoreBe32(&seed[0x0C], entry.flags2);
    StoreBe32(&seed[0x10], entry.offset);
    StoreBe32(&seed[0x14], entry.size);
    std::memcpy(seed.data() + 32, key.data(), key.size());
    const Digest256 iv_key = Common::Crypto::Sha3_256(seed);
    const Common::Crypto::Aes128 aes(std::span<const u8, 16>(iv_key.data() + 16, 16));
    Common::Crypto::CbcDecrypt(aes, std::span<const u8, 16>(iv_key.data(), 16), data);
    data.resize(entry.size);
    return data;
}

namespace {

u64 Be64At(const std::vector<u8>& d, size_t off) {
    u64 v = 0;
    for (size_t i = 0; i < 8; i++) {
        v = (v << 8) | d[off + i];
    }
    return v;
}

std::string AsciiAt(const std::vector<u8>& d, size_t off, size_t max) {
    std::string s;
    for (size_t i = off; i < d.size() && i < off + max && d[i] != 0; i++) {
        if (d[i] < 0x20 || d[i] >= 0x7F) {
            return {};
        }
        s.push_back(static_cast<char>(d[i]));
    }
    return s;
}

} // namespace

NpInfo ReadNpInfo(const PkgInfo& info, std::string_view passcode) {
    NpInfo np;
    for (const auto& e : info.entries) {
        const bool npbind = e.name.ends_with("npbind.dat") || e.id == 0x0403;
        const bool nptitle = e.id == 0x0402;
        const bool license = e.id == 0x0400;
        const bool license_info = e.id == 0x0401;
        if (!npbind && !nptitle && !license && !license_info) {
            continue;
        }
        std::string problem;
        const auto data = DecryptEntry(info, e, passcode, &problem);
        if (!data) {
            np.problems.push_back(fmt::format(
                "{}: {}", e.name.empty() ? fmt::format("0x{:04X}", e.id) : e.name, problem));
            continue;
        }
        if (npbind) {
            NPBindFile file;
            if (file.Parse(*data)) {
                for (auto& id : file.GetNpCommIds()) {
                    if (!id.empty() && std::find(np.np_comm_ids.begin(), np.np_comm_ids.end(),
                                                 id) == np.np_comm_ids.end()) {
                        np.np_comm_ids.push_back(std::move(id));
                    }
                }
            }
        } else if (nptitle) {
            if (data->size() >= 0x1C && std::memcmp(data->data(), "NPTD", 4) == 0) {
                np.np_title_id = AsciiAt(*data, 0x10, 12);
            }
        } else if (license) {
            // RIF: magic, u16 version, account ID, validity window, content ID.
            if (data->size() >= 0x50 && std::memcmp(data->data(), "RIF\0", 4) == 0) {
                np.has_license = true;
                np.rif_version = static_cast<u16>(((*data)[4] << 8) | (*data)[5]);
                np.account_id = Be64At(*data, 0x08);
                np.start_time = Be64At(*data, 0x10);
                np.expiry_time = Be64At(*data, 0x18);
                np.license_content_id = AsciiAt(*data, 0x20, 0x30);
            }
        } else if (license_info) {
            np.license_info_content_id = AsciiAt(*data, 0x00, 0x30);
        }
    }
    return np;
}

} // namespace PS5Pkg

// ---------------------------------------------------------------------------
// Random access to an outer file of any size.

namespace PS5Pkg {

struct OuterFileReader::Impl {
    const PkgInfo& info;
    const KeyCheck& keys;
    const OuterFile& file;
    Keys k;
    Reader reader;
    std::vector<u64> blocks;
    u64 cached = ~u64{0};
    std::vector<u8> raw;
    std::vector<u8> plain;

    Impl(const PkgInfo& i, const KeyCheck& c, const OuterFile& f)
        : info(i), keys(c), file(f),
          k{Common::Crypto::Aes128(c.data_key), Common::Crypto::Aes128(c.tweak_key), c.sign_key,
            c.plaintext},
          reader(i, c.image_offset, c.block_size) {}

    // Appends the data block numbers below an indirect block of `level`
    // (0: it lists data blocks), stopping once `want` blocks are known.
    bool Expand(u64 block, const Digest256& hash, int level, u64 want, std::string& why) {
        if (!reader.ReadBlock(block, raw)) {
            why = fmt::format("Indirect block {} lies outside the package.", block);
            return false;
        }
        std::vector<u8> table;
        if (DecryptChecked(k, block, raw, hash, table, nullptr) != BlockCheck::Ok) {
            why = fmt::format("Indirect block {} failed its hash check.", block);
            return false;
        }
        const DinodeLayout& layout = file.wide_pointers ? kSigned64 : kSigned32;
        const size_t per_block = table.size() / layout.pointer_size;
        for (size_t i = 0; i < per_block && blocks.size() < want; i++) {
            const Pointer ptr = ReadPointer(&table[i * layout.pointer_size], layout.wide);
            if (level == 0) {
                blocks.push_back(ptr.block);
            } else if (!Expand(ptr.block, ptr.hash, level - 1, want, why)) {
                return false;
            }
        }
        return true;
    }
};

OuterFileReader::OuterFileReader(const PkgInfo& info, const KeyCheck& keys, const OuterFile& file)
    : impl_(std::make_unique<Impl>(info, keys, file)) {}

OuterFileReader::~OuterFileReader() = default;

u64 OuterFileReader::Size() const {
    return impl_->file.size;
}

bool OuterFileReader::Open(std::string* problem) {
    auto& d = *impl_;
    std::string why;
    if (d.keys.block_size == 0 || d.file.is_dir || !d.reader.IsOpen()) {
        why = "The file cannot be read.";
    } else {
        const u64 want = d.file.block_count;
        d.blocks.assign(d.file.blocks.begin(), d.file.blocks.end());
        for (size_t level = 0;
             level < d.file.indirect_blocks.size() && d.blocks.size() < want && why.empty();
             level++) {
            d.Expand(d.file.indirect_blocks[level], d.file.indirect_hashes[level],
                     static_cast<int>(level), want, why);
        }
        if (why.empty() && d.blocks.size() < want) {
            why = "The file's block list is incomplete.";
        }
        if (why.empty() && d.file.size > want * d.keys.block_size) {
            why = "The file is larger than its blocks.";
        }
    }
    if (!why.empty() && problem != nullptr) {
        *problem = why;
    }
    return why.empty();
}

bool OuterFileReader::Read(u64 offset, std::span<u8> out, std::string* problem) {
    auto& d = *impl_;
    const u64 bs = d.keys.block_size;
    if (offset > d.file.size || out.size() > d.file.size - offset) {
        if (problem != nullptr) {
            *problem = "Read past the end of the file.";
        }
        return false;
    }
    size_t done = 0;
    while (done < out.size()) {
        const u64 pos = offset + done;
        const u64 index = pos / bs;
        if (index != d.cached) {
            const u64 block = d.blocks[static_cast<size_t>(index)];
            if (!d.reader.ReadBlock(block, d.raw)) {
                if (problem != nullptr) {
                    *problem = fmt::format("Block {} lies outside the package.", block);
                }
                return false;
            }
            d.plain = d.raw;
            if (!d.k.plaintext) {
                Common::Crypto::XtsDecrypt(d.k.data, d.k.tweak,
                                           d.file.data_sectors ? block : block | kSignedSector,
                                           d.plain);
            }
            d.cached = index;
        }
        const size_t in_block = static_cast<size_t>(pos % bs);
        const size_t n = std::min<size_t>(out.size() - done, bs - in_block);
        std::memcpy(out.data() + done, d.plain.data() + in_block, n);
        done += n;
    }
    return true;
}

} // namespace PS5Pkg
