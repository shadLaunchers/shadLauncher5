// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps5_pkg_extract.h"

#include <fstream>
#include <system_error>

namespace PS5Pkg {

namespace {

constexpr u32 kFirstContentEntry = 0x0400;
constexpr u32 kEntryImageDigests = 0x040A;

constexpr u32 kContentTypeAddOn = 0x21;
constexpr u32 kContentTypeAddOnLicense = 0x22;

bool IsSafeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 200 || path.front() == '/') {
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
            const auto u = static_cast<unsigned char>(c);
            if (u < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                c == '>' || c == '|') {
                return false;
            }
        }
        start = end + 1;
    }
    return true;
}

} // namespace

InstallKind GetInstallKind(const PkgInfo& info) {
    if (info.content_type == kContentTypeAddOn || info.content_type == kContentTypeAddOnLicense) {
        return InstallKind::AddOn;
    }
    if (info.kind == PackageKind::Patch || info.patch_kind != PatchKind::None) {
        return InstallKind::Patch;
    }
    return InstallKind::Game;
}

std::string_view InstallKindName(InstallKind kind) {
    switch (kind) {
    case InstallKind::Patch:
        return "Patch";
    case InstallKind::AddOn:
        return "Add-on";
    case InstallKind::Game:
    default:
        return "Game";
    }
}

std::string EntitlementLabel(std::string_view content_id) {
    const size_t first = content_id.find('-');
    if (first == std::string_view::npos) {
        return {};
    }
    const size_t second = content_id.find('-', first + 1);
    if (second == std::string_view::npos) {
        return {};
    }
    return std::string(content_id.substr(second + 1));
}

std::string PackageTitleId(const PkgInfo& info) {
    if (info.param && !info.param->title_id.empty()) {
        return info.param->title_id;
    }
    // "UP9000-PPSA01234_00-..." -> "PPSA01234"
    if (info.content_id.size() >= 16 && info.content_id[6] == '-') {
        return info.content_id.substr(7, 9);
    }
    return {};
}

bool CanInstallSceSys(const PkgInfo& info, std::string* why) {
    auto fail = [&](std::string reason) {
        if (why != nullptr) {
            *why = std::move(reason);
        }
        return false;
    };
    if (!info.error.empty()) {
        return fail(info.error);
    }
    switch (info.kind) {
    case PackageKind::Debug:
    case PackageKind::Retail:
    case PackageKind::Patch:
        break;
    case PackageKind::PS4:
        return fail("This is a PS4 package.");
    case PackageKind::MetadataOnly:
        return fail("The package holds metadata only.");
    case PackageKind::Unknown:
    default:
        return fail("This is not a PS5 package.");
    }
    if (!info.has_cnt || info.entries.empty()) {
        return fail("The package has no metadata container.");
    }
    if (PackageTitleId(info).empty()) {
        return fail("The package has no title ID.");
    }
    return true;
}

std::vector<SceSysItem> ListSceSysItems(const PkgInfo& info) {
    std::vector<SceSysItem> items;
    for (const CntEntry& entry : info.entries) {
        if (entry.id < kFirstContentEntry || entry.id == kEntryImageDigests || entry.size == 0) {
            continue;
        }
        if (!IsSafeRelativePath(entry.name)) {
            continue;
        }
        items.push_back({entry, entry.name});
    }
    return items;
}

SceSysResult ExtractSceSysItem(const PkgInfo& info, const SceSysItem& item,
                               const std::filesystem::path& sce_sys_dir,
                               std::string_view passcode) {
    SceSysResult result;
    std::string problem;
    std::optional<std::vector<u8>> data = item.entry.IsEncrypted()
                                              ? DecryptEntry(info, item.entry, passcode, &problem)
                                              : ReadEntryData(info, item.entry, &problem);
    if (!data) {
        result.problem = problem.empty() ? "The entry could not be read." : problem;
        return result;
    }
    result.decrypted = item.entry.IsEncrypted();

    const std::filesystem::path target = sce_sys_dir / std::filesystem::path(item.path);
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        result.problem = "Could not create " + target.parent_path().string() + ": " + ec.message();
        return result;
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(reinterpret_cast<const char*>(data->data()),
                           static_cast<std::streamsize>(data->size()))) {
        result.problem = "Could not write " + target.string() + ".";
        return result;
    }
    result.size = data->size();
    result.ok = true;
    return result;
}

} // namespace PS5Pkg
