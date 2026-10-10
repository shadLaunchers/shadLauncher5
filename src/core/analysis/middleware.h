// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Third-party middleware classification of a game's PRX modules, and a
// per-game "tech report" combining it with eboot string fingerprinting.
// Ported from ps5rs (ps5-signatures/middleware.rs + ps5-analysis/middleware.rs).

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "core/analysis/string_analysis.h"
#include "core/file_format/elf_info.h"

namespace Core::Analysis {

enum class ModuleKind { ThirdParty, Sony, Unknown };

struct MiddlewareId {
    std::string_view vendor;
    std::string_view product;
    std::string_view description;
};

std::string_view ModuleKindName(ModuleKind kind);

const MiddlewareId* MatchMiddleware(std::string_view stem);
std::pair<ModuleKind, const MiddlewareId*> ClassifyStem(std::string_view stem);
std::string ModuleStem(std::string_view file_name);
bool IsModuleFileName(std::string_view file_name);
std::vector<std::string> ListGameModuleFiles(const std::filesystem::path& game_root);

struct ModuleReport {
    std::string rel_path;
    std::string file_name;
    std::string module_name;
    ModuleKind kind = ModuleKind::Unknown;
    std::string vendor;
    std::string product;
    std::string description;
    u64 size = 0;
    bool parseable = false;
    size_t imports = 0;
    size_t exports = 0;
    std::vector<std::string> import_libs;
};

ModuleReport AnalyzeModule(std::string rel_path, const std::vector<u8>& data);

struct GameTechReport {
    bool eboot_found = false;
    u64 eboot_size = 0;
    bool eboot_is_self = false;
    StringAnalysis eboot_strings;
    std::vector<Loader::ElfInfo::LibVersionEntry> lib_versions;
    std::vector<ModuleReport> modules;
    std::vector<std::string> layout_engine_hints;
    bool cancelled = false;
};

struct AnalyzeProgress {
    std::function<void(const std::string& current, size_t done, size_t total)> on_step;
    const std::atomic_bool* cancel = nullptr;
};

GameTechReport AnalyzeGame(const std::filesystem::path& game_root,
                           const AnalyzeProgress& progress = {});
std::string EngineSummary(const GameTechReport& report);
std::string ReportToJson(const GameTechReport& report, const std::string& title,
                         const std::string& serial);

} // namespace Core::Analysis
