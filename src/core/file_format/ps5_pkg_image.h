// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/file_format/ps5_pkg.h"
#include "core/file_format/ps5_pkg_keys.h"

namespace PS5Pkg {

struct NapsBlock {
    u64 disk = 0;         // offset in pfs_image.dat
    u32 packed = 0;       // stored size of both halves
    u32 packed_first = 0; // stored size of the first half
    u8 flags = 0;         // NapsBlockFlag bits
};

enum NapsBlockFlag : u8 {
    FirstDelta = 1 << 1,  // first half: delta literals (else raw literals)
    FirstLz = 1 << 2,     // first half: LZ chunk (else a single entropy array)
    SecondDelta = 1 << 4, // second half: delta literals
    SecondLz = 1 << 5,    // second half: LZ chunk
};

// A file's (or the metadata's) span of the inner image.
struct NapsRegion {
    u64 offset = 0; // in the inner image
    u64 size = 0;
    bool hole = false; // no stored data: reads as zeros
    size_t first_block = 0;
    size_t block_count = 0;
};

struct NapsLayout {
    u64 image_size = 0; // size of the inner image
    std::vector<NapsRegion> regions;
    std::vector<NapsBlock> blocks;

    // Parses naps_pkg_layout.dat; `stored_size` is the size of pfs_image.dat.
    bool Parse(std::span<const u8> data, u64 stored_size, std::string* problem = nullptr);
};

struct InnerFile {
    std::string path; // relative to uroot, '/'-separated
    u64 size = 0;
    u64 offset = 0; // in the inner image
};

// Reads the inner image of a package whose keys verified (or that needs none).
class InnerImage {
public:
    InnerImage();
    ~InnerImage();
    InnerImage(const InnerImage&) = delete;
    InnerImage& operator=(const InnerImage&) = delete;

    bool Open(const PkgInfo& info, const KeyCheck& keys, std::string* problem = nullptr);

    [[nodiscard]] const std::vector<InnerFile>& Files() const {
        return files_;
    }
    [[nodiscard]] const NapsLayout& Layout() const {
        return layout_;
    }

    bool ExtractFile(const InnerFile& file, const std::filesystem::path& dest,
                     const std::function<bool(u64)>& progress, std::string* problem = nullptr);
    bool Read(u64 offset, std::span<u8> out, std::string* problem = nullptr);

private:
    bool DecodeBlock(size_t index, size_t out_size, std::vector<u8>& out, std::string& why);
    const NapsRegion* RegionAt(u64 offset) const;

    std::unique_ptr<OuterFileReader> image_;
    NapsLayout layout_;
    std::vector<InnerFile> files_;
    std::vector<u8> packed_;
};

bool ParseInnerTree(std::span<const u8> meta, u64 meta_offset,
                    const std::function<bool(u64, std::span<u8>)>& read,
                    std::vector<InnerFile>& files, std::string* problem = nullptr);
[[nodiscard]] bool IsSafeInnerPath(std::string_view path);

} // namespace PS5Pkg
