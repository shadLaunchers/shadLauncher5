// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps5_pkg_image.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <system_error>
#include <fmt/format.h>

#include "common/kraken.h"

namespace PS5Pkg {

namespace {

constexpr u64 kBlockSize = 0x40000; // one layout block of the inner image
constexpr u64 kHalfSize = 0x20000;  // one Kraken chunk
constexpr u32 kRegionHole = 0x40;   // region type flag: no stored data
constexpr u32 kMaxInodes = 1u << 22;
constexpr size_t kInodeSize = 0xA8;
constexpr u64 kMaxMetadata = 256ull << 20;
constexpr int kMaxDepth = 64;
constexpr u32 kDirentFile = 2;
constexpr u32 kDirentDir = 3;

u32 Le32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}
u64 Le64(const u8* p) {
    return static_cast<u64>(Le32(p)) | (static_cast<u64>(Le32(p + 4)) << 32);
}
u64 LeN(const u8* p, int n) {
    u64 v = 0;
    for (int i = n - 1; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

const OuterFile* FindOuter(const KeyCheck& keys, std::string_view path) {
    for (const auto& f : keys.files) {
        if (f.path == path && !f.is_dir) {
            return &f;
        }
    }
    return nullptr;
}

} // namespace

bool IsSafeInnerPath(std::string_view path) {
    if (path.empty() || path.size() > 1024 || path.front() == '/') {
        return false;
    }
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        for (const char c : part) {
            if (static_cast<unsigned char>(c) < 0x20 || c == '\\' || c == ':') {
                return false;
            }
        }
        start = end + 1;
    }
    return true;
}

bool NapsLayout::Parse(std::span<const u8> data, u64 stored_size, std::string* problem) {
    auto fail = [&](std::string why) {
        if (problem != nullptr) {
            *problem = std::move(why);
        }
        return false;
    };
    *this = {};
    if (data.size() < 16) {
        return fail("The layout file is too small.");
    }
    const u64 w0 = Le64(data.data());
    const u64 w1 = Le64(data.data() + 8);
    const u64 entries = (w0 & 0xFFFFFF) + 1;
    const u64 shuffles = (w0 >> 28) & 0xF;
    const u64 ublocks = (w0 >> 32) & 0xFFFFFF;
    const u64 outer = w1 & 0xFFFFFF;
    const u64 records = ((w1 >> 24) & 0xFFFFFF) + 2;
    if (((w0 >> 24) & 3) != 2) {
        return fail("The layout uses an unknown compression type.");
    }
    const u64 region_pos = 16 + outer * 8 + shuffles * 8;
    const u64 index_pos = region_pos + entries * 6;
    const u64 record_pos = (index_pos + ((ublocks + 8) >> 3) * 10 + 7) & ~u64{7};
    if (record_pos + records * 9 > data.size()) {
        return fail("The layout file is truncated.");
    }

    // Regions.
    std::vector<std::pair<u8, u64>> marks;
    for (u64 i = 0; i < entries; i++) {
        const u8* e = data.data() + region_pos + i * 6;
        marks.emplace_back(e[5], LeN(e, 5));
    }
    image_size = marks.back().second;
    for (size_t i = 0; i + 1 < marks.size(); i++) {
        if (marks[i + 1].second < marks[i].second) {
            return fail("The layout's file offsets are out of order.");
        }
        NapsRegion r;
        r.offset = marks[i].second;
        r.size = marks[i + 1].second - marks[i].second;
        r.hole = (marks[i].first & kRegionHole) != 0;
        regions.push_back(r);
    }

    // Records -> blocks.
    struct Raw {
        bool run;
        u64 lo;
        u64 hi;
    };
    std::vector<Raw> raw(records);
    for (u64 i = 0; i < records; i++) {
        const u8* p = data.data() + record_pos + i * 9;
        raw[i] = {((p[2] >> 2) & 1) != 0, Le64(p), p[8]};
    }
    std::vector<u32> block_uoff;
    u64 base = 0;
    bool have_prev = false;
    u64 prev = 0;
    for (u64 i = 0; i < records; i++) {
        const Raw& r = raw[i];
        if (r.run) {
            base = ((r.lo >> 49) | (r.hi << 15)) << 17;
            have_prev = false;
            continue;
        }
        const u64 coff = r.lo & 0x3FFFF;
        if (have_prev) {
            if (coff <= prev) {
                base += kBlockSize;
            }
        } else {
            u64 window = base & ~(kBlockSize - 1);
            if (window + coff < base) {
                window += kBlockSize;
            }
            base = window;
        }
        prev = coff;
        have_prev = true;
        NapsBlock b;
        b.disk = (base & ~(kBlockSize - 1)) + coff;
        b.packed_first = static_cast<u32>(((r.lo >> 38) & 0x1FFFF) + 1);
        b.flags = static_cast<u8>((r.lo >> 55) & 0x3F);
        // The stored size runs to the next block, or to where the next run
        // record says this run ended.
        if (i + 1 < records) {
            const Raw& n = raw[i + 1];
            u64 end;
            if (n.run) {
                end = (b.disk & ~(kBlockSize - 1)) + (n.lo & 0x3FFFF);
                if (end <= b.disk) {
                    end += kBlockSize;
                }
            } else {
                u64 next_base = base;
                if ((n.lo & 0x3FFFF) <= coff) {
                    next_base += kBlockSize;
                }
                end = (next_base & ~(kBlockSize - 1)) + (n.lo & 0x3FFFF);
            }
            if (end > b.disk && end - b.disk <= kBlockSize) {
                b.packed = static_cast<u32>(end - b.disk);
            }
        }
        blocks.push_back(b);
        block_uoff.push_back(static_cast<u32>((r.lo >> 20) & 0x3FFFF));
    }

    // Each region with data takes one block per 256 KiB, in order.
    size_t next = 0;
    for (auto& r : regions) {
        if (r.hole || r.size == 0) {
            continue;
        }
        const u64 count = (r.size + kBlockSize - 1) / kBlockSize;
        if (next + count > blocks.size()) {
            return fail("The layout has fewer blocks than its files need.");
        }
        r.first_block = next;
        r.block_count = static_cast<size_t>(count);
        for (size_t k = 0; k < count; k++) {
            const NapsBlock& b = blocks[next + k];
            if (block_uoff[next + k] != (r.offset & (kBlockSize - 1))) {
                return fail("The layout's blocks do not line up with its files.");
            }
            const u64 out = std::min(kBlockSize, r.size - k * kBlockSize);
            const u64 stored = b.packed != 0 ? b.packed : b.packed_first;
            if (b.disk > stored_size || stored > stored_size - b.disk ||
                (out > kHalfSize && b.packed == 0)) {
                return fail(fmt::format("Block {} lies outside pfs_image.dat.", next + k));
            }
        }
        next += static_cast<size_t>(count);
    }
    return true;
}

bool ParseInnerTree(std::span<const u8> m, u64 meta_offset,
                    const std::function<bool(u64, std::span<u8>)>& read,
                    std::vector<InnerFile>& files, std::string* problem) {
    auto fail = [&](std::string why) {
        if (problem != nullptr) {
            *problem = std::move(why);
        }
        return false;
    };
    files.clear();
    if (m.size() < 0x50) {
        return fail("The game image's filesystem header is not valid.");
    }
    const u32 block_size = Le32(&m[0x20]);
    const u64 inode_count = Le64(&m[0x30]);
    if (block_size < 0x1000 || block_size > (1u << 20) || inode_count == 0 ||
        inode_count > kMaxInodes) {
        return fail("The game image's filesystem header is not valid.");
    }
    const u64 per_block = block_size / kInodeSize;
    const u64 last = inode_count - 1;
    const u64 table_end =
        block_size + (last / per_block) * block_size + (last % per_block + 1) * kInodeSize;
    if (table_end > m.size()) {
        return fail("The game image's inode table is truncated.");
    }
    struct Node {
        u16 mode;
        u64 size;
        u64 offset;
    };
    std::vector<Node> nodes(static_cast<size_t>(inode_count));
    for (u64 i = 0; i < inode_count; i++) {
        const u8* e = &m[static_cast<size_t>(block_size + (i / per_block) * block_size +
                                             (i % per_block) * kInodeSize)];
        nodes[static_cast<size_t>(i)] = {static_cast<u16>(e[0] | (e[1] << 8)), Le64(e + 8),
                                         Le64(e + 0x60)};
    }
    auto read_dir = [&](const Node& n, std::vector<u8>& out) {
        if (n.size > (64u << 20)) {
            return false;
        }
        out.resize(static_cast<size_t>(n.size));
        if (n.offset >= meta_offset && n.offset - meta_offset <= m.size() &&
            n.size <= m.size() - (n.offset - meta_offset)) {
            if (!out.empty()) {
                std::memcpy(out.data(), &m[static_cast<size_t>(n.offset - meta_offset)],
                            out.size());
            }
            return true;
        }
        return read(n.offset, out);
    };
    std::set<u64> seen;
    std::string walk_error;
    auto walk = [&](auto&& self, u64 dir, const std::string& prefix, bool in_uroot,
                    int depth) -> void {
        if (depth > kMaxDepth || dir >= nodes.size() || !seen.insert(dir).second) {
            return;
        }
        std::vector<u8> listing;
        if (!read_dir(nodes[static_cast<size_t>(dir)], listing)) {
            walk_error = fmt::format("Directory inode {} could not be read.", dir);
            return;
        }
        size_t off = 0;
        while (off + 16 <= listing.size()) {
            const u32 ino = Le32(&listing[off]);
            const u32 type = Le32(&listing[off + 4]);
            const u32 name_len = Le32(&listing[off + 8]);
            const u32 entry_size = Le32(&listing[off + 12]);
            const size_t block_end = (off / block_size + 1) * block_size;
            if (entry_size < 16 || entry_size > listing.size() - off ||
                name_len > entry_size - 16) {
                // Padding up to the end of the block.
                off = block_end;
                continue;
            }
            const std::string name(reinterpret_cast<const char*>(&listing[off + 16]), name_len);
            off += entry_size;
            if (name.empty() || name == "." || name == ".." || ino >= nodes.size()) {
                continue;
            }
            if (type == kDirentDir) {
                const bool child_uroot = in_uroot || (prefix.empty() && name == "uroot");
                const std::string child =
                    !in_uroot ? std::string{} : (prefix.empty() ? name : prefix + "/" + name);
                if (in_uroot || child_uroot) {
                    self(self, ino, child, true, depth + 1);
                }
            } else if (type == kDirentFile && in_uroot) {
                const std::string path = prefix.empty() ? name : prefix + "/" + name;
                if (IsSafeInnerPath(path)) {
                    files.push_back({path, nodes[ino].size, nodes[ino].offset});
                }
            }
        }
    };
    walk(walk, 0, {}, false, 0);
    if (files.empty()) {
        return fail(walk_error.empty() ? "The game image lists no files." : walk_error);
    }
    return true;
}

InnerImage::InnerImage() = default;
InnerImage::~InnerImage() = default;

const NapsRegion* InnerImage::RegionAt(u64 offset) const {
    auto it = std::upper_bound(layout_.regions.begin(), layout_.regions.end(), offset,
                               [](u64 v, const NapsRegion& r) { return v < r.offset; });
    if (it == layout_.regions.begin()) {
        return nullptr;
    }
    --it;
    return offset < it->offset + it->size ? &*it : nullptr;
}

bool InnerImage::DecodeBlock(size_t index, size_t out_size, std::vector<u8>& out,
                             std::string& why) {
    const NapsBlock& b = layout_.blocks[index];
    out.resize(out_size);
    const size_t first_out = std::min<size_t>(out_size, kHalfSize);
    const size_t second_out = out_size - first_out;
    const size_t stored = second_out != 0 ? b.packed : b.packed_first;
    const size_t first_in = std::min<size_t>(b.packed_first, stored);
    packed_.resize(stored);
    if (!image_->Read(b.disk, packed_, &why)) {
        return false;
    }
    auto half = [&](const u8* src, size_t in, size_t pos, size_t n, bool lz, bool delta) {
        if (in == n) {
            std::memcpy(out.data() + pos, src, n);
            return true;
        }
        const auto r = Common::Kraken::DecompressChunk(std::span<const u8>(src, in), out, pos, n,
                                                       lz, delta ? 0 : 1);
        if (!r.ok) {
            why = fmt::format("Block {} ({}): {}", index, pos == 0 ? "first half" : "second half",
                              r.error);
        }
        return r.ok;
    };
    if (!half(packed_.data(), first_in, 0, first_out, b.flags & FirstLz, b.flags & FirstDelta)) {
        return false;
    }
    if (second_out != 0 && !half(packed_.data() + first_in, stored - first_in, first_out,
                                 second_out, b.flags & SecondLz, b.flags & SecondDelta)) {
        return false;
    }
    return true;
}

bool InnerImage::Read(u64 offset, std::span<u8> out, std::string* problem) {
    std::string why;
    std::vector<u8> block;
    size_t done = 0;
    while (done < out.size() && why.empty()) {
        const u64 pos = offset + done;
        const NapsRegion* r = RegionAt(pos);
        if (r == nullptr) {
            why = fmt::format("Offset 0x{:X} is outside the image's files.", pos);
            break;
        }
        const u64 in_region = pos - r->offset;
        const u64 k = in_region / kBlockSize;
        const size_t in_block = static_cast<size_t>(in_region % kBlockSize);
        const size_t block_out =
            static_cast<size_t>(std::min(kBlockSize, r->size - k * kBlockSize));
        const size_t n = std::min(out.size() - done, block_out - in_block);
        if (r->hole) {
            std::memset(out.data() + done, 0, n);
        } else if (!DecodeBlock(r->first_block + static_cast<size_t>(k), block_out, block, why)) {
            break;
        } else {
            std::memcpy(out.data() + done, block.data() + in_block, n);
        }
        done += n;
    }
    if (!why.empty() && problem != nullptr) {
        *problem = why;
    }
    return why.empty();
}

bool InnerImage::Open(const PkgInfo& info, const KeyCheck& keys, std::string* problem) {
    auto fail = [&](std::string why) {
        if (problem != nullptr) {
            *problem = std::move(why);
        }
        return false;
    };
    files_.clear();
    const OuterFile* image = FindOuter(keys, "uroot/pfs_image.dat");
    const OuterFile* naps = FindOuter(keys, "uroot/naps_pkg_layout.dat");
    if (image == nullptr || naps == nullptr) {
        return fail("The package has no pfs_image.dat / naps_pkg_layout.dat.");
    }
    std::string why;
    OuterFileReader naps_reader(info, keys, *naps);
    std::vector<u8> naps_data(static_cast<size_t>(naps->size));
    if (!naps_reader.Open(&why) || !naps_reader.Read(0, naps_data, &why)) {
        return fail("naps_pkg_layout.dat: " + why);
    }
    if (!layout_.Parse(naps_data, image->size, &why)) {
        return fail("naps_pkg_layout.dat: " + why);
    }
    image_ = std::make_unique<OuterFileReader>(info, keys, *image);
    if (!image_->Open(&why)) {
        return fail("pfs_image.dat: " + why);
    }

    // The metadata is the last region that starts with a PFS superblock.
    const NapsRegion* meta = nullptr;
    for (auto it = layout_.regions.rbegin(); it != layout_.regions.rend() && meta == nullptr;
         ++it) {
        if (it->hole || it->size < 0x10000 || it->size > kMaxMetadata) {
            continue;
        }
        u8 head[16];
        if (Read(it->offset, head) && Le64(head) == 2 && Le64(head + 8) == 20130315) {
            meta = &*it;
        }
    }
    if (meta == nullptr) {
        return fail("The game image's filesystem header was not found.");
    }
    std::vector<u8> m(static_cast<size_t>(meta->size));
    if (!Read(meta->offset, m, &why)) {
        return fail("Filesystem metadata: " + why);
    }
    auto read = [this](u64 offset, std::span<u8> out) { return Read(offset, out); };
    if (!ParseInnerTree(m, meta->offset, read, files_, &why)) {
        return fail(why);
    }
    // Every file with data starts a region of the layout.
    size_t stray = 0;
    for (const auto& f : files_) {
        const NapsRegion* r = f.size == 0 ? nullptr : RegionAt(f.offset);
        if (f.size != 0 && (r == nullptr || r->offset != f.offset || r->size < f.size)) {
            stray++;
        }
    }
    if (stray != 0) {
        return fail(fmt::format("{} of the game image's {} files do not match its layout.", stray,
                                files_.size()));
    }
    std::sort(files_.begin(), files_.end(),
              [](const InnerFile& a, const InnerFile& b) { return a.offset < b.offset; });
    return true;
}

bool InnerImage::ExtractFile(const InnerFile& file, const std::filesystem::path& dest,
                             const std::function<bool(u64)>& progress, std::string* problem) {
    auto fail = [&](std::string why) {
        if (problem != nullptr) {
            *problem = std::move(why);
        }
        return false;
    };
    const NapsRegion* r = file.size == 0 ? nullptr : RegionAt(file.offset);
    if (file.size != 0 && (r == nullptr || r->offset != file.offset || r->size < file.size)) {
        return fail("The file's data is not in the image layout.");
    }
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
        return fail("Could not create the file.");
    }
    if (file.size == 0) {
        return true;
    }
    std::vector<u8> block;
    u64 written = 0;
    std::string why;
    for (size_t k = 0; written < file.size; k++) {
        const size_t block_out =
            static_cast<size_t>(std::min(kBlockSize, r->size - k * kBlockSize));
        if (r->hole) {
            block.assign(block_out, 0);
        } else if (!DecodeBlock(r->first_block + k, block_out, block, why)) {
            return fail(why);
        }
        const size_t n = static_cast<size_t>(std::min<u64>(block_out, file.size - written));
        if (!out.write(reinterpret_cast<const char*>(block.data()),
                       static_cast<std::streamsize>(n))) {
            return fail("Could not write the file (disk full?).");
        }
        written += n;
        if (progress && !progress(written)) {
            return fail("Cancelled.");
        }
    }
    out.close();
    if (!out) {
        return fail("Could not write the file (disk full?).");
    }
    return true;
}

} // namespace PS5Pkg
