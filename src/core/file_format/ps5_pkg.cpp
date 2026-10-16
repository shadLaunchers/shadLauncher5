// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps5_pkg.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <fmt/format.h>
#include <libdeflate.h>

namespace PS5Pkg {

namespace {

constexpr std::array<u8, 4> kFihMagic = {0x7F, 'F', 'I', 'H'};
constexpr std::array<u8, 4> kCntMagic = {0x7F, 'C', 'N', 'T'};
constexpr std::array<u8, 4> kLihMagic = {0x7F, 'L', 'I', 'H'};

constexpr u64 kFihHeaderRead = 0x100;
constexpr u64 kCntHeaderSize = 0x5A0;
constexpr u64 kCntContentIdOffset = 0x40;
constexpr u64 kCntContentIdSize = 0x30;
constexpr u64 kEntrySize = 0x20;
constexpr u32 kMaxEntries = 0x10000;
constexpr u32 kMaxNameTable = 16u << 20;
constexpr u32 kMaxParamJson = 16u << 20;
constexpr u64 kFihRegionSize = 0x10000;
constexpr u64 kMaxSiMember = 64u << 20;
constexpr u32 kMaxArtwork = 64u << 20;

constexpr s64 kPfsMagic = 20130315;
constexpr u64 kSuperblockRead = 0x48;

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
u16 Be16(const u8* p) {
    return static_cast<u16>((p[0] << 8) | p[1]);
}
u32 Be32(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}
u64 Be64(const u8* p) {
    return (static_cast<u64>(Be32(p)) << 32) | Be32(p + 4);
}

bool ReadAt(std::ifstream& f, u64 offset, void* dst, u64 size) {
    f.clear();
    f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    return f && f.read(static_cast<char*>(dst), static_cast<std::streamsize>(size)) &&
           static_cast<u64>(f.gcount()) == size;
}

// offset + size <= limit, without overflowing.
bool InRange(u64 offset, u64 size, u64 limit) {
    return offset <= limit && size <= limit - offset;
}

std::string NulTrimmed(const u8* p, size_t max) {
    size_t n = 0;
    while (n < max && p[n] != 0) {
        n++;
    }
    return {reinterpret_cast<const char*>(p), n};
}

bool ReadSuperblock(std::ifstream& f, u64 offset, u64 file_size, PfsSuperblockInfo& out) {
    if (offset == 0 || !InRange(offset, kSuperblockRead, file_size)) {
        return false;
    }
    std::array<u8, kSuperblockRead> sb{};
    if (!ReadAt(f, offset, sb.data(), sb.size())) {
        return false;
    }
    const s64 version = static_cast<s64>(Le64(sb.data()));
    const s64 magic = static_cast<s64>(Le64(sb.data() + 0x08));
    if (magic != kPfsMagic || version < 1 || version > 2) {
        return false;
    }
    out.found = true;
    out.offset = offset;
    out.version = version;
    out.mode = Le16(sb.data() + 0x1C);
    out.block_size = Le32(sb.data() + 0x20);
    out.block_count = static_cast<s64>(Le64(sb.data() + 0x28));
    out.dinode_count = static_cast<s64>(Le64(sb.data() + 0x30));
    return true;
}

PatchKind ClassifyPatch(u32 flags) {
    constexpr u32 kFirst = 0x00100000;
    constexpr u32 kSubsequent = 0x40000000;
    constexpr u32 kDelta = 0x41000000;
    constexpr u32 kCumulative = 0x60000000;
    if ((flags & kCumulative) == kCumulative) {
        return PatchKind::Cumulative;
    }
    if ((flags & kDelta) == kDelta) {
        return PatchKind::Delta;
    }
    if ((flags & kSubsequent) != 0) {
        return PatchKind::Subsequent;
    }
    if ((flags & kFirst) != 0) {
        return PatchKind::First;
    }
    return PatchKind::None;
}

// Lists the stored zip that trails the image (SI segment). Only the plain
// (non-ZIP64) format is handled, which is what packages carry.
void ReadSi(std::ifstream& f, PkgInfo& info, u64 search_from) {
    constexpr u32 kEocdSig = 0x06054b50;
    constexpr u32 kCdSig = 0x02014b50;
    constexpr u32 kLocalSig = 0x04034b50;
    constexpr u64 kEocdSize = 22;
    if (search_from >= info.file_size || info.file_size - search_from < kEocdSize) {
        return;
    }
    const u64 window = std::min<u64>(info.file_size - search_from, 0xFFFF + kEocdSize);
    std::vector<u8> tail(window);
    if (!ReadAt(f, info.file_size - window, tail.data(), tail.size())) {
        return;
    }
    s64 found = -1;
    for (s64 i = static_cast<s64>(window - kEocdSize); i >= 0; i--) {
        if (Le32(&tail[static_cast<size_t>(i)]) == kEocdSig) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        return;
    }
    const u8* eocd = &tail[static_cast<size_t>(found)];
    const u64 eocd_abs = info.file_size - window + static_cast<u64>(found);
    const u16 count = Le16(eocd + 10);
    const u32 cd_size = Le32(eocd + 12);
    const u32 cd_offset = Le32(eocd + 16);
    if (u64{cd_size} + cd_offset > eocd_abs) {
        info.si_problem = "The install-metadata archive is malformed.";
        return;
    }
    const u64 zip_start = eocd_abs - cd_size - cd_offset;
    if (zip_start < search_from) {
        return; // the directory points before the metadata container: not ours
    }
    info.si_offset = zip_start;
    info.si_size = info.file_size - zip_start;

    std::vector<u8> cd(cd_size);
    if (!cd.empty() && !ReadAt(f, zip_start + cd_offset, cd.data(), cd.size())) {
        info.si_problem = "The install-metadata directory could not be read.";
        return;
    }
    size_t pos = 0;
    for (u16 i = 0; i < count; i++) {
        if (pos + 46 > cd.size() || Le32(&cd[pos]) != kCdSig) {
            info.si_problem = "The install-metadata directory is truncated.";
            return;
        }
        const u8* h = &cd[pos];
        const u16 name_len = Le16(h + 28);
        const u16 extra_len = Le16(h + 30);
        const u16 comment_len = Le16(h + 32);
        if (pos + 46 + name_len > cd.size()) {
            info.si_problem = "The install-metadata directory is truncated.";
            return;
        }
        SiMember m;
        m.method = Le16(h + 10);
        m.crc32 = Le32(h + 16);
        m.compressed_size = Le32(h + 20);
        m.size = Le32(h + 24);
        m.local_header_offset = zip_start + Le32(h + 42);
        m.name.assign(reinterpret_cast<const char*>(h + 46), name_len);
        pos += 46u + name_len + extra_len + comment_len;
        if (m.name.empty() || m.name.back() == '/') {
            continue; // directory entry
        }
        // Sanity-check the local header now so reads can trust it.
        std::array<u8, 30> local{};
        if (!ReadAt(f, m.local_header_offset, local.data(), local.size()) ||
            Le32(local.data()) != kLocalSig) {
            info.si_problem = fmt::format("The local header of {} is invalid.", m.name);
            continue;
        }
        info.si_members.push_back(std::move(m));
    }
}

void BuildSegments(PkgInfo& info) {
    if (info.has_fih) {
        info.segments.push_back({"FIH", "Finalized image header and digest table", 0,
                                 std::min(kFihRegionSize, info.file_size)});
        if (info.pfs_size != 0) {
            info.segments.push_back(
                {"PFS", "Encrypted filesystem image (game files)", info.pfs_offset, info.pfs_size});
        }
    }
    if (info.has_lih) {
        info.segments.push_back({"LIH", "Patch image header and data", 0, info.cnt_offset});
    }
    if (info.has_cnt) {
        info.segments.push_back({"SC", "Metadata container (CNT)", info.cnt_offset,
                                 info.si_offset > info.cnt_offset
                                     ? info.si_offset - info.cnt_offset
                                     : std::min(info.cnt_size, info.file_size - info.cnt_offset)});
    }
    if (info.si_size != 0) {
        info.segments.push_back(
            {"SI", "Install-metadata archive (zip)", info.si_offset, info.si_size});
    }
    // Anything after the last known segment is listed rather than hidden.
    u64 end = 0;
    for (const auto& seg : info.segments) {
        end = std::max(end, seg.offset + seg.size);
    }
    if (!info.segments.empty() && end < info.file_size) {
        info.segments.push_back({"?", "Unidentified trailing data", end, info.file_size - end});
    }
}

bool ReadCnt(std::ifstream& f, PkgInfo& info) {
    const u64 base = info.cnt_offset;
    if (!InRange(base, kCntHeaderSize, info.file_size)) {
        return false;
    }
    std::vector<u8> hdr(kCntHeaderSize);
    if (!ReadAt(f, base, hdr.data(), hdr.size()) ||
        std::memcmp(hdr.data(), kCntMagic.data(), kCntMagic.size()) != 0) {
        return false;
    }
    info.has_cnt = true;
    info.cnt_flags = Be32(&hdr[0x04]);
    info.entry_count = Be32(&hdr[0x10]);
    const u32 table_offset = Be32(&hdr[0x18]);
    info.body_offset = Be64(&hdr[0x20]);
    info.body_size = Be64(&hdr[0x28]);
    info.content_id = NulTrimmed(&hdr[kCntContentIdOffset], kCntContentIdSize);
    info.drm_type = Be32(&hdr[0x70]);
    info.content_type = Be32(&hdr[0x74]);
    info.content_flags = Be32(&hdr[0x78]);

    if (info.entry_count > kMaxEntries ||
        !InRange(base + table_offset, u64{info.entry_count} * kEntrySize, info.file_size)) {
        info.error = fmt::format("The entry table is out of range ({} entries at 0x{:X}).",
                                 info.entry_count, table_offset);
        return true;
    }
    std::vector<u8> table(u64{info.entry_count} * kEntrySize);
    if (!table.empty() && !ReadAt(f, base + table_offset, table.data(), table.size())) {
        info.error = "The entry table could not be read.";
        return true;
    }
    info.entries.reserve(info.entry_count);
    for (u32 i = 0; i < info.entry_count; i++) {
        const u8* r = &table[u64{i} * kEntrySize];
        CntEntry e;
        e.id = Be32(r);
        e.name_offset = Be32(r + 0x04);
        e.flags1 = Be32(r + 0x08);
        e.flags2 = Be32(r + 0x0C);
        e.offset = Be32(r + 0x10);
        e.size = Be32(r + 0x14);
        info.entries.push_back(std::move(e));
    }

    // Resolve names from the entry-name table.
    if (const CntEntry* names = info.FindEntry(ENTRY_NAMES);
        names != nullptr && !names->IsEncrypted() && names->size > 0 &&
        names->size <= kMaxNameTable &&
        InRange(base + names->offset, names->size, info.file_size)) {
        std::vector<u8> blob(names->size);
        if (ReadAt(f, base + names->offset, blob.data(), blob.size())) {
            for (auto& e : info.entries) {
                if (e.name_offset != 0 && e.name_offset < blob.size()) {
                    e.name = NulTrimmed(&blob[e.name_offset], blob.size() - e.name_offset);
                }
            }
        }
    }
    info.cnt_size = std::max<u64>(kCntHeaderSize, u64{table_offset} + table.size());
    if (info.body_size != 0 && info.body_offset <= info.file_size) {
        info.cnt_size = std::max(info.cnt_size, info.body_offset + info.body_size);
    }
    for (auto& e : info.entries) {
        if (e.name.empty()) {
            e.name = std::string(WellKnownEntryName(e.id));
        }
        if (e.IsEncrypted()) {
            info.encrypted_entry_count++;
        }
        info.cnt_size = std::max(info.cnt_size, u64{e.offset} + e.size);
    }
    info.patch_kind = ClassifyPatch(info.content_flags);
    return true;
}

} // namespace

const CntEntry* PkgInfo::FindEntry(u32 id) const {
    for (const auto& e : entries) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

bool LooksLikePackage(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::array<u8, 4> magic{};
    if (!f.read(reinterpret_cast<char*>(magic.data()), magic.size())) {
        return false;
    }
    return magic == kFihMagic || magic == kLihMagic || magic == kCntMagic;
}

PkgInfo ReadPkgInfo(const std::filesystem::path& path) {
    PkgInfo info;
    info.path = path;

    std::error_code ec;
    info.file_size = std::filesystem::file_size(path, ec);
    std::ifstream f(path, std::ios::binary);
    if (ec || !f) {
        info.error = "The file could not be opened.";
        return info;
    }
    if (info.file_size < 8) {
        info.error = "The file is too small to be a package.";
        return info;
    }

    std::array<u8, kFihHeaderRead> fih{};
    const u64 head = std::min<u64>(fih.size(), info.file_size);
    if (!ReadAt(f, 0, fih.data(), head)) {
        info.error = "The file header could not be read.";
        return info;
    }

    if (std::memcmp(fih.data(), kFihMagic.data(), 4) == 0) {
        if (head < kFihHeaderRead) {
            info.error = "The finalized image header is truncated.";
            return info;
        }
        info.has_fih = true;
        info.signed_byte = fih[0x05];
        info.format_version = Le16(&fih[0x06]);
        info.pfs_offset = Le64(&fih[0x10]);
        info.pfs_size = Le64(&fih[0x18]);
        info.superblock_offset = Le64(&fih[0x20]);
        info.cnt_offset = Le64(&fih[0x58]);
        info.inner_image_blocks = Le32(&fih[0x90]);
        info.content_inode_count = Le32(&fih[0x94]);
        info.content_version_echo = Le32(&fih[0x9C]);
        info.inner_image_size = Le64(&fih[0xA0]);
        info.layout_table_size = Le64(&fih[0xA8]);
        info.app_file_count = Le32(&fih[0xF0]);
        switch (info.signed_byte) {
        case 0x00:
            info.kind = PackageKind::Debug;
            break;
        case 0x80:
            info.kind = PackageKind::Retail;
            break;
        default:
            info.kind = PackageKind::Unknown;
            info.error = fmt::format("Unrecognized signed byte 0x{:02X}.", info.signed_byte);
            break;
        }

        // Data-first images keep the plaintext superblock near the end
        // (FIH 0x20); older layouts keep it at the start of the PFS segment.
        if (!ReadSuperblock(f, info.superblock_offset, info.file_size, info.outer_pfs)) {
            ReadSuperblock(f, info.pfs_offset, info.file_size, info.outer_pfs);
        }
        if (info.cnt_offset != 0 && !ReadCnt(f, info) && info.error.empty()) {
            info.error = "The embedded metadata container could not be read.";
        }
    } else if (std::memcmp(fih.data(), kLihMagic.data(), 4) == 0) {
        if (head < 0x38) {
            info.error = "The patch image header is truncated.";
            return info;
        }
        info.has_lih = true;
        info.kind = PackageKind::Patch;
        info.lih_version = Le16(&fih[0x06]);
        info.cnt_offset = Le64(&fih[0x30]);
        if (info.lih_version != 1) {
            info.error = fmt::format("Unknown patch header version {}.", info.lih_version);
        }
        if ((info.cnt_offset == 0 || !ReadCnt(f, info)) && info.error.empty()) {
            info.error = "The embedded metadata container could not be read.";
        }
    } else if (std::memcmp(fih.data(), kCntMagic.data(), 4) == 0) {
        info.cnt_offset = 0;
        ReadCnt(f, info);
        const bool ps4 = info.FindEntry(ENTRY_PARAM_SFO) != nullptr ||
                         (info.content_type >= 0x1A && info.content_type <= 0x1E);
        info.kind = ps4 ? PackageKind::PS4 : PackageKind::MetadataOnly;
    } else {
        info.error = "Not a PS5 package (unknown file magic).";
        return info;
    }

    if (info.has_cnt && (info.has_fih || info.has_lih)) {
        ReadSi(f, info, info.cnt_offset + info.cnt_size);
    }
    BuildSegments(info);
    if (info.kind == PackageKind::PS4) {
        return info;
    }
    if (const CntEntry* pj = info.FindEntry(ENTRY_PARAM_JSON); pj == nullptr) {
        info.param_problem = info.has_cnt ? "The package has no param.json entry."
                                          : "The metadata container was not found.";
    } else if (pj->size > kMaxParamJson) {
        info.param_problem = "param.json is unexpectedly large.";
    } else {
        std::string problem;
        if (const auto data = ReadEntryData(info, *pj, &problem)) {
            Param param;
            if (param.Parse(
                    std::string_view(reinterpret_cast<const char*>(data->data()), data->size()))) {
                info.param = std::move(param);
            } else {
                info.param_problem = "param.json could not be parsed.";
            }
        } else {
            info.param_problem = problem;
        }
    }
    return info;
}

std::optional<std::vector<u8>> ReadEntryData(const PkgInfo& info, const CntEntry& entry,
                                             std::string* problem) {
    auto fail = [&](std::string why) -> std::optional<std::vector<u8>> {
        if (problem != nullptr) {
            *problem = std::move(why);
        }
        return std::nullopt;
    };
    if (!info.has_cnt) {
        return fail("The package has no readable metadata container.");
    }
    if (entry.IsEncrypted()) {
        return fail("This entry is encrypted.");
    }
    const u64 abs = info.cnt_offset + entry.offset;
    if (!InRange(abs, entry.size, info.file_size)) {
        return fail("The entry data lies outside the file.");
    }
    std::ifstream f(info.path, std::ios::binary);
    std::vector<u8> data(entry.size);
    if (!f || (!data.empty() && !ReadAt(f, abs, data.data(), data.size()))) {
        return fail("The entry data could not be read.");
    }
    return data;
}

std::optional<std::vector<u8>> ReadSiMember(const PkgInfo& info, const SiMember& member,
                                            std::string* problem) {
    auto fail = [&](std::string why) -> std::optional<std::vector<u8>> {
        if (problem != nullptr) {
            *problem = std::move(why);
        }
        return std::nullopt;
    };
    if (member.size > kMaxSiMember || member.compressed_size > kMaxSiMember) {
        return fail("The file is too large to read here.");
    }
    if (member.method != 0 && member.method != 8) {
        return fail(fmt::format("Unsupported compression method {}.", member.method));
    }
    std::ifstream f(info.path, std::ios::binary);
    std::array<u8, 30> local{};
    if (!f || !ReadAt(f, member.local_header_offset, local.data(), local.size())) {
        return fail("The file header could not be read.");
    }
    const u64 data_offset = member.local_header_offset + 30 + Le16(&local[26]) + Le16(&local[28]);
    if (!InRange(data_offset, member.compressed_size, info.file_size)) {
        return fail("The file data lies outside the package.");
    }
    std::vector<u8> raw(member.compressed_size);
    if (!raw.empty() && !ReadAt(f, data_offset, raw.data(), raw.size())) {
        return fail("The file data could not be read.");
    }
    if (member.method == 0) {
        return raw;
    }
    std::vector<u8> out(member.size);
    libdeflate_decompressor* d = libdeflate_alloc_decompressor();
    if (d == nullptr) {
        return fail("Out of memory.");
    }
    size_t actual = 0;
    const auto rc =
        libdeflate_deflate_decompress(d, raw.data(), raw.size(), out.data(), out.size(), &actual);
    libdeflate_free_decompressor(d);
    if (rc != LIBDEFLATE_SUCCESS || actual != out.size()) {
        return fail("The file could not be decompressed.");
    }
    return out;
}

std::optional<ArtworkData> ReadArtwork(const PkgInfo& info, const ArtworkSlot& slot) {
    for (const auto [id, is_dds] : {std::pair{slot.png_id, false}, std::pair{slot.dds_id, true}}) {
        if (id == 0) {
            continue;
        }
        const CntEntry* e = info.FindEntry(id);
        if (e == nullptr || e->IsEncrypted() || e->size == 0 || e->size > kMaxArtwork) {
            continue;
        }
        if (auto data = ReadEntryData(info, *e)) {
            return ArtworkData{std::move(*data), is_dds, e->name};
        }
    }
    return std::nullopt;
}

std::string_view RegionFromContentId(std::string_view content_id) {
    if (content_id.empty()) {
        return "Unknown";
    }
    switch (content_id.front()) {
    case 'U':
        return "Americas";
    case 'E':
        return "Europe";
    case 'J':
        return "Japan";
    case 'K':
        return "Korea";
    case 'H':
        return "Asia (Hong Kong)";
    case 'A':
        return "Asia";
    case 'I':
        return "Internal";
    default:
        return "Other";
    }
}

std::string_view PatchKindName(PatchKind kind) {
    switch (kind) {
    case PatchKind::First:
        return "First patch";
    case PatchKind::Subsequent:
        return "Subsequent patch";
    case PatchKind::Delta:
        return "Delta patch";
    case PatchKind::Cumulative:
        return "Cumulative patch";
    case PatchKind::None:
    default:
        return "Not a patch";
    }
}

std::string_view KindName(PackageKind kind) {
    switch (kind) {
    case PackageKind::Patch:
        return "Patch package";
    case PackageKind::Debug:
        return "Debug / fake package";
    case PackageKind::Retail:
        return "Retail package";
    case PackageKind::MetadataOnly:
        return "Metadata container only";
    case PackageKind::PS4:
        return "PS4 package";
    case PackageKind::Unknown:
    default:
        return "Unknown";
    }
}

std::string_view WellKnownEntryName(u32 id) {
    switch (id) {
    case 0x0001:
        return "digests";
    case 0x0010:
        return "entry_keys";
    case 0x0020:
        return "image_key";
    case 0x0080:
        return "general_digests";
    case 0x0100:
        return "metas";
    case 0x0200:
        return "entry_names";
    case 0x0400:
        return "license.dat";
    case 0x0401:
        return "license.info";
    case 0x0402:
        return "nptitle.dat";
    case 0x0403:
        return "npbind.dat";
    case 0x0404:
        return "selfinfo.dat";
    case 0x0407:
        return "target-deltainfo.dat";
    case 0x0408:
        return "origin-deltainfo.dat";
    case 0x040A:
        return "imagedigs.dat";
    case 0x1000:
        return "param.sfo";
    case 0x1001:
        return "playgo-chunk.dat";
    case 0x1004:
        return "pronunciation.xml";
    case 0x1006:
        return "pic1.png";
    case 0x1005:
        return "pronunciation.sig";
    case 0x1007:
        return "pubtoolinfo.dat";
    case 0x1200:
        return "icon0.png";
    case 0x1220:
        return "pic0.png";
    case 0x1240:
        return "snd0.at9";
    case 0x1280:
        return "icon0.dds";
    case 0x12A0:
        return "pic0.dds";
    case 0x12C0:
        return "pic1.dds";
    case 0x2000:
        return "param.json";
    case 0x2010:
        return "playgo-hash-table.dat";
    case 0x2011:
        return "playgo-ficm.dat";
    case 0x2060:
        return "pic2.dds";
    default:
        return {};
    }
}

std::string ContentTypeName(u32 content_type) {
    switch (content_type) {
    case 0x20:
        return "Game data (PS5GD)";
    case 0x21:
        return "Additional content with data (PS5AC)";
    case 0x22:
        return "Additional content, license only (PS5AL)";
    case 0x1A:
        return "PS4 game data (GD)";
    case 0x1B:
        return "PS4 additional content (AC)";
    case 0x1C:
        return "PS4 additional license (AL)";
    case 0x1E:
        return "PS4 delta patch (DP)";
    default:
        return fmt::format("Unknown (0x{:X})", content_type);
    }
}

std::string DrmTypeName(u32 drm_type) {
    switch (drm_type) {
    case 0x0:
        return "None";
    case 0xF:
        return "PS4";
    case 0x10:
        return "PS5 (0x10)";
    default:
        return fmt::format("0x{:X}", drm_type);
    }
}

std::string FormatContentVersion(u32 bcd) {
    if (bcd == 0) {
        return {};
    }
    const std::string digits = fmt::format("{:08X}", bcd);
    return digits.substr(0, 2) + "." + digits.substr(2, 3) + "." + digits.substr(5, 3);
}

} // namespace PS5Pkg
