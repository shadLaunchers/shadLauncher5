// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/file_format/ps5_pkg.h"
#include "core/file_format/ps5_pkg_keys.h"

namespace PS5Pkg {

enum class InstallKind {
    Game,  // base game: <install dir>/<title id>
    Patch, // update: <game folder>-patch
    AddOn, // additional content: <add-on dir>/<title id>/<entitlement label>
};

[[nodiscard]] InstallKind GetInstallKind(const PkgInfo& info);
[[nodiscard]] std::string_view InstallKindName(InstallKind kind);

// The third part of a content ID ("UP9000-PPSA01234_00-LABEL" -> "LABEL").
[[nodiscard]] std::string EntitlementLabel(std::string_view content_id);

// Title ID from param.json, else from the content ID.
[[nodiscard]] std::string PackageTitleId(const PkgInfo& info);

// Whether this package's sce_sys files can be installed at all.
[[nodiscard]] bool CanInstallSceSys(const PkgInfo& info, std::string* why = nullptr);

struct SceSysItem {
    CntEntry entry;
    std::string path;
};

[[nodiscard]] std::vector<SceSysItem> ListSceSysItems(const PkgInfo& info);

struct SceSysResult {
    bool ok = false;
    bool decrypted = false;
    u64 size = 0;
    std::string problem;
};

[[nodiscard]] SceSysResult ExtractSceSysItem(const PkgInfo& info, const SceSysItem& item,
                                             const std::filesystem::path& sce_sys_dir,
                                             std::string_view passcode = DEFAULT_PASSCODE);

} // namespace PS5Pkg
