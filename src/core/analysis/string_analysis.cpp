// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "string_analysis.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <set>
#include <string_view>
#include <utility>

namespace Core::Analysis {

namespace {

constexpr size_t kMaxStringLength = 4096;

bool Contains(const std::string& s, std::string_view needle) {
    return s.find(needle) != std::string::npos;
}

bool ContainsAny(const std::string& s, std::initializer_list<std::string_view> needles) {
    return std::any_of(needles.begin(), needles.end(),
                       [&](std::string_view n) { return Contains(s, n); });
}

bool IsPrintable(u8 b) {
    return (b >= 0x21 && b <= 0x7E) || b == ' ';
}

void DetectPattern(const std::vector<std::string>& strings,
                   std::initializer_list<std::string_view> patterns, std::string_view name,
                   std::vector<Detection>& out, size_t keep = 10) {
    Detection d;
    for (const auto& s : strings) {
        if (ContainsAny(s, patterns)) {
            d.evidence.push_back(s);
        }
    }
    if (!d.evidence.empty()) {
        if (d.evidence.size() > keep) {
            d.evidence.resize(keep);
        }
        d.value = name;
        out.push_back(std::move(d));
    }
}

struct EngineFingerprint {
    std::string_view name;
    std::vector<std::pair<std::string_view, u8>> patterns;
};

// Weights are ps5rs' (ps5-signatures/src/engine.rs). Each pattern counts once.
const std::vector<EngineFingerprint>& EngineFingerprints() {
    static const std::vector<EngineFingerprint> kAll = {
        {"Unreal Engine 4",
         {{"UnrealEngine4Runtime", 100},
          {"UnrealEngine4Editor", 100},
          {"P4Damascus", 95},
          {"UObject", 80},
          {"FName", 70},
          {"GEngine", 70},
          {"GWorld", 70},
          {"FPlatformProcess", 60},
          {"UE4Runtime", 50},
          {"UE4Game", 50},
          {"FShaderPipelineCache", 8},
          {"SlateRHIRenderer", 5},
          {"QuickHullConvexHullLib", 3},
          {"Engine/Source/Runtime", 3},
          {"Engine/Plugins", 3},
          {"PhysXCooking", 2}}},
        {"Unreal Engine 5",
         {{"UnrealEngine5Runtime", 100},
          {"UnrealEngine5Editor", 100},
          {"Nanite", 10},
          {"Lumen", 10},
          {"UE5Runtime", 50},
          {"UE5Game", 50}}},
        {"Unity",
         {{"UnityEngine", 90},
          {"UnityPlayer", 5},
          {"UnityMain", 2},
          {"global-metadata.dat", 5},
          {"il2cpp", 5},
          {"Assembly-CSharp", 3}}},
        {"Godot",
         {{"project.godot", 100}, {".godot", 60}, {"Godot Engine", 100}, {"GDNative", 60}}},
        {"RE Engine", {{"RE Engine", 100}, {".pak", 25}}},
        {"Frostbite", {{"Frostbite", 100}, {".toc", 50}}},
        {"Decima", {{"Decima", 100}, {".core", 50}}},
        {"Dragon Engine", {{"Dragon Engine", 100}, {".par", 50}}},
        {"REDEngine", {{"REDEngine", 100}, {".archive", 50}}},
        {"Creation Engine 2", {{"Creation Engine 2", 100}, {".ba2", 60}}},
        {"RAGE Engine", {{"RAGE Engine", 100}, {".rpf", 100}}},
        {"CryEngine", {{"CryEngine", 100}, {".cgf", 50}}},
        {"Source Engine", {{"Source Engine", 100}, {".vpk", 100}}},
        {"LithTech", {{"LithTech", 100}, {".rez", 100}}},
        {"Havok Vision Engine", {{"Havok Vision Engine", 100}, {".vmesh", 90}}},
        {"Gamebryo", {{"Gamebryo", 100}, {".nif", 100}}},
        {"Snowdrop", {{"Snowdrop", 100}, {".forge", 100}}},
        {"AnvilNext", {{"AnvilNext", 100}}},
        {"id Tech", {{"id Tech", 100}, {".pk3", 100}, {".pk4", 100}}},
    };
    return kAll;
}

Detection ScoreEngine(const EngineFingerprint& fp, const std::vector<std::string>& strings) {
    Detection d;
    d.value = fp.name;
    for (const auto& [pattern, weight] : fp.patterns) {
        for (const auto& s : strings) {
            if (Contains(s, pattern)) {
                d.evidence.push_back(s);
                d.score += weight;
                break;
            }
        }
    }
    d.confidence = d.evidence.empty() ? 0 : static_cast<u8>(std::min<u32>(d.score, 100));
    return d;
}

} // namespace

std::vector<FoundString> ExtractStrings(const std::vector<u8>& data, size_t min_length) {
    std::vector<FoundString> out;
    size_t start = 0;
    bool in_run = false;
    auto flush = [&](size_t end) {
        const size_t len = end - start;
        if (len >= min_length && len <= kMaxStringLength) {
            out.push_back(
                {start, std::string(reinterpret_cast<const char*>(data.data()) + start, len)});
        }
    };
    for (size_t i = 0; i < data.size(); i++) {
        if (IsPrintable(data[i])) {
            if (!in_run) {
                start = i;
                in_run = true;
            }
        } else if (in_run) {
            flush(i);
            in_run = false;
        }
    }
    if (in_run) {
        flush(data.size());
    }
    return out;
}

std::vector<std::string> ExtractStringTexts(const std::vector<u8>& data, size_t min_length) {
    std::vector<std::string> out;
    for (auto& s : ExtractStrings(data, min_length)) {
        out.push_back(std::move(s.text));
    }
    return out;
}

std::vector<std::string> DetectSceLibraries(const std::vector<std::string>& strings) {
    std::set<std::string> found;
    for (const auto& s : strings) {
        if (Contains(s, "libSce") &&
            (s.ends_with(".prx") || s.ends_with(".sprx") || s.ends_with(".so"))) {
            found.insert(s);
        }
    }
    return {found.begin(), found.end()};
}

std::vector<Detection> DetectThirdParty(const std::vector<std::string>& strings) {
    std::vector<Detection> d;
    DetectPattern(strings, {"PhysX", "PX_", "PhysXScene", "PxRigidActor", "PhysXCooking"}, "PhysX",
                  d);
    DetectPattern(strings, {"libVorbis", "Xiph.Org libVorbis"}, "libVorbis", d);
    DetectPattern(strings, {"libpng version", "libpng "}, "libpng", d);
    DetectPattern(strings, {"libopus", "Opus audio codec"}, "libopus", d);
    DetectPattern(strings, {"OpenSSL"}, "OpenSSL", d);
    DetectPattern(strings, {"Bink Video", "Bink2"}, "Bink", d);
    DetectPattern(strings, {"libsamplerate", "Secret Rabbit Code"}, "libsamplerate", d);
    DetectPattern(strings, {"zlib"}, "zlib", d);
    DetectPattern(strings, {"libcrunch", "Crunch"}, "Crunch", d);
    DetectPattern(strings, {"Oodle", "Kraken", "Leviathan"}, "Oodle", d);
    DetectPattern(strings, {"FMOD", "Firelight Technologies"}, "FMOD", d);
    DetectPattern(strings, {"Wwise", "Audiokinetic", "AK::"}, "Wwise", d);
    DetectPattern(strings, {"Gameface", "Coherent GT", "CoherentGT"}, "Coherent Gameface", d);
    DetectPattern(strings, {"International Components for Unicode", "icu_"}, "ICU", d);
    return d;
}

std::optional<Detection> DetectEngine(const std::vector<std::string>& strings) {
    std::optional<Detection> best;
    for (const auto& fp : EngineFingerprints()) {
        Detection d = ScoreEngine(fp, strings);
        if (d.confidence == 0) {
            continue;
        }
        // Ties go to the later entry (the newer engine), as in ps5rs.
        if (!best || d.confidence >= best->confidence) {
            best = std::move(d);
        }
    }
    return best;
}

std::vector<Detection> DetectCustomForks(const std::vector<std::string>& strings) {
    struct Fork {
        std::string_view pattern;
        std::string_view label;
        u8 confidence;
    };
    static constexpr Fork kForks[] = {
        {"P4Damascus", "Unreal Engine 4 custom fork (P4Damascus depot)", 90},
        {"HK_Project_Delivery", "Custom project build (HK_Project_Delivery)", 85},
        {"HK_EngineSources", "Custom engine sources (HK_EngineSources)", 85},
    };
    std::vector<Detection> out;
    for (const auto& fork : kForks) {
        Detection d;
        for (const auto& s : strings) {
            if (Contains(s, fork.pattern)) {
                d.evidence.push_back(s);
            }
        }
        if (!d.evidence.empty()) {
            if (d.evidence.size() > 10) {
                d.evidence.resize(10);
            }
            d.value = fork.label;
            d.score = fork.confidence;
            d.confidence = fork.confidence;
            out.push_back(std::move(d));
        }
    }
    return out;
}

std::optional<Detection> DetectBuildSystem(const std::vector<std::string>& strings) {
    struct System {
        std::string_view name;
        std::string_view a;
        std::string_view b;
    };
    static constexpr System kSystems[] = {
        {"Jenkins", "Jenkins", "jenkins"},
        {"BuildServer", "BuildServer", "build_server"},
    };
    for (const auto& sys : kSystems) {
        const auto name = sys.name;
        Detection d;
        for (const auto& s : strings) {
            if (Contains(s, sys.a) || Contains(s, sys.b)) {
                d.evidence.push_back(s);
            }
        }
        if (!d.evidence.empty()) {
            d.value = name;
            return d;
        }
    }
    return std::nullopt;
}

std::optional<Detection> DetectDepot(const std::vector<std::string>& strings) {
    Detection d;
    std::optional<std::string> depot;
    for (const auto& s : strings) {
        if (s.size() < 4 || !std::isalpha(static_cast<unsigned char>(s[0])) || s[1] != ':' ||
            (s[2] != '/' && s[2] != '\\')) {
            continue;
        }
        const std::string_view rest = std::string_view(s).substr(3);
        const auto first = rest.substr(0, rest.find_first_of("/\\"));
        const bool ok =
            first.size() >= 2 && std::all_of(first.begin(), first.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
            });
        if (ok) {
            d.evidence.push_back(s);
            if (!depot) {
                depot = std::string(first);
            }
        }
    }
    if (d.evidence.empty()) {
        return std::nullopt;
    }
    if (d.evidence.size() > 10) {
        d.evidence.resize(10);
    }
    d.value = depot.value_or("Unknown");
    return d;
}

std::vector<Detection> DetectProjectPaths(const std::vector<std::string>& strings) {
    Detection d;
    std::optional<std::string> project;
    for (const auto& s : strings) {
        size_t idx = s.find("sharedspace/");
        if (idx == std::string::npos) {
            idx = s.find("sharedspace\\");
        }
        if (idx == std::string::npos) {
            continue;
        }
        d.evidence.push_back(s);
        if (!project) {
            const std::string_view rest = std::string_view(s).substr(idx + 12);
            const auto name = rest.substr(0, rest.find_first_of("/\\"));
            if (!name.empty()) {
                project = std::string(name);
            }
        }
    }
    if (d.evidence.empty()) {
        return {};
    }
    if (d.evidence.size() > 10) {
        d.evidence.resize(10);
    }
    d.value = project.value_or("Unknown");
    return {std::move(d)};
}

std::vector<Detection> DetectSdkHints(const std::vector<std::string>& strings) {
    std::vector<Detection> d;
    DetectPattern(strings, {"Prospero SDK", "prospero"}, "Prospero SDK", d);
    DetectPattern(strings, {"ORBIS SDK", "orbis"}, "ORBIS SDK", d);
    DetectPattern(strings, {"SCE SDK", "sce_"}, "SCE SDK", d);
    return d;
}

std::vector<Detection> DetectVersions(const std::vector<std::string>& strings) {
    std::vector<Detection> d;
    DetectPattern(strings, {"PhysX ", "PhysX"}, "PhysX", d, 5);
    DetectPattern(strings, {"Xiph.Org libVorbis "}, "libVorbis", d, 5);
    DetectPattern(strings, {"libpng version ", "libpng "}, "libpng", d, 5);
    DetectPattern(strings, {"OpenSSL "}, "OpenSSL", d, 5);
    DetectPattern(strings, {"zlib "}, "zlib", d, 5);
    DetectPattern(strings, {"libopus "}, "libopus", d, 5);
    DetectPattern(strings, {"libsamplerate-"}, "libsamplerate", d, 5);
    return d;
}

std::vector<std::string> DetectSourcePaths(const std::vector<std::string>& strings) {
    std::set<std::string> paths;
    for (const auto& s : strings) {
        if (ContainsAny(s, {"Engine/Source/", "Engine\\Source\\", "Engine/Plugins/",
                            "Engine\\Plugins\\"})) {
            paths.insert(s);
        }
    }
    std::vector<std::string> out;
    for (const auto& p : paths) {
        if (out.size() >= 20) {
            break;
        }
        out.push_back(p);
    }
    return out;
}

StringAnalysis AnalyzeStrings(const std::vector<std::string>& strings) {
    StringAnalysis a;
    a.string_count = strings.size();
    a.sce_libraries = DetectSceLibraries(strings);
    a.third_party_libs = DetectThirdParty(strings);
    a.engine = DetectEngine(strings);
    a.build_system = DetectBuildSystem(strings);
    a.source_depot = DetectDepot(strings);
    a.sdk_hints = DetectSdkHints(strings);
    a.detected_versions = DetectVersions(strings);
    a.source_paths = DetectSourcePaths(strings);
    a.project_paths = DetectProjectPaths(strings);
    a.custom_forks = DetectCustomForks(strings);
    return a;
}

StringAnalysis AnalyzeStrings(const std::vector<u8>& data) {
    return AnalyzeStrings(ExtractStringTexts(data, 4));
}

} // namespace Core::Analysis
