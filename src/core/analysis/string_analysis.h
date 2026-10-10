// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>
#include "common/types.h"

namespace Core::Analysis {

struct FoundString {
    u64 offset = 0;
    std::string text;
};

struct Detection {
    std::string value;
    u32 score = 0;
    u8 confidence = 0;
    std::vector<std::string> evidence;
};

struct StringAnalysis {
    std::vector<std::string> sce_libraries;
    std::vector<Detection> third_party_libs;
    std::optional<Detection> engine;
    std::optional<Detection> build_system;
    std::optional<Detection> source_depot;
    std::vector<Detection> sdk_hints;
    std::vector<Detection> detected_versions;
    std::vector<std::string> source_paths;
    std::vector<Detection> project_paths;
    std::vector<Detection> custom_forks;
    size_t string_count = 0;
};

std::vector<FoundString> ExtractStrings(const std::vector<u8>& data, size_t min_length = 4);
std::vector<std::string> ExtractStringTexts(const std::vector<u8>& data, size_t min_length = 4);

std::vector<std::string> DetectSceLibraries(const std::vector<std::string>& strings);
std::vector<Detection> DetectThirdParty(const std::vector<std::string>& strings);
std::optional<Detection> DetectEngine(const std::vector<std::string>& strings);
std::vector<Detection> DetectCustomForks(const std::vector<std::string>& strings);
std::optional<Detection> DetectBuildSystem(const std::vector<std::string>& strings);
std::optional<Detection> DetectDepot(const std::vector<std::string>& strings);
std::vector<Detection> DetectProjectPaths(const std::vector<std::string>& strings);
std::vector<Detection> DetectSdkHints(const std::vector<std::string>& strings);
std::vector<Detection> DetectVersions(const std::vector<std::string>& strings);
std::vector<std::string> DetectSourcePaths(const std::vector<std::string>& strings);

StringAnalysis AnalyzeStrings(const std::vector<std::string>& strings);
StringAnalysis AnalyzeStrings(const std::vector<u8>& data);

} // namespace Core::Analysis
