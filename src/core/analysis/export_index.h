// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "common/types.h"

namespace Core::Analysis {

struct ExportSource {
    std::string display_path;
    std::string rel_path;
    std::filesystem::path host_path;
    bool from_sys_modules = false;
    bool readable = false;
    std::string problem;
    size_t export_count = 0;
    std::vector<std::string> declared_modules;
    std::vector<std::string> declared_libraries;
};

struct ExportEntry {
    std::string nid;
    std::string library;
    std::string module;
    u64 address = 0;
    u64 size = 0;
    size_t source = 0;
};

struct ExportIndex {
    std::vector<ExportSource> sources;
    std::vector<ExportEntry> entries;
    std::unordered_multimap<std::string, size_t> by_nid;
    bool cancelled = false;

    void AddModule(ExportSource source, const std::vector<u8>& data);
};

struct IndexProgress {
    std::function<void(const std::string& current, size_t done, size_t total)> on_step;
    const std::atomic_bool* cancel = nullptr;
};

ExportIndex BuildGameExportIndex(const std::filesystem::path& game_root,
                                 const IndexProgress& progress = {});
std::vector<std::filesystem::path> FindSysModuleFiles(const std::filesystem::path& dir);
std::shared_ptr<const ExportIndex> GetSysModulesExportIndex(const std::filesystem::path& dir,
                                                            const IndexProgress& progress = {});

enum class MatchKind {
    ExactNid,
    HashedName,
    NameContains,
};

struct ExportHit {
    const ExportIndex* index = nullptr;
    size_t entry = 0;
    MatchKind kind = MatchKind::NameContains;
};

std::string ExportName(const ExportEntry& entry);
std::vector<ExportHit> SearchExports(const std::vector<const ExportIndex*>& indexes,
                                     std::string_view query, size_t limit,
                                     size_t* total_matches = nullptr);

} // namespace Core::Analysis
