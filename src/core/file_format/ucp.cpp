// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>

#include "common/io_file.h"
#include "common/logging/log.h"
#include "core/file_format/ucp.h"

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
        LOG_ERROR(Common_Filesystem, "Failed to create output directory {}: {}",
                  outputDir.string(), ec.message());
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
