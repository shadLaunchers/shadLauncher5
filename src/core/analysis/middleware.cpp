// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "middleware.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <nlohmann/json.hpp>
#include "core/file_sys/game_backend.h"

namespace Core::Analysis {

namespace {

constexpr MiddlewareId FMOD{"Firelight Technologies", "FMOD", "FMOD low-level audio engine"};
constexpr MiddlewareId FMOD_STUDIO{"Firelight Technologies", "FMOD Studio",
                                   "FMOD Studio audio engine (banks and events)"};
constexpr MiddlewareId WWISE_EFFECT{"Audiokinetic", "Wwise", "Wwise audio effect plugin"};
constexpr MiddlewareId AURO{"Auro Technologies", "Auro-3D",
                            "Auro-3D audio plugin (Wwise integration)"};
constexpr MiddlewareId IZOTOPE{"iZotope", "iZotope",
                               "iZotope audio processing plugin (Wwise integration)"};
constexpr MiddlewareId MCDSP{"McDSP", "McDSP", "McDSP DSP audio plugin (Wwise integration)"};
constexpr MiddlewareId MASTERING_SUITE{"iZotope", "Ozone Mastering Suite",
                                       "iZotope Ozone mastering suite (Wwise integration)"};
constexpr MiddlewareId GAMEFACE{"Coherent Labs", "Gameface", "Gameface UI runtime"};
constexpr MiddlewareId GAMEFACE_DEV{"Coherent Labs", "Gameface", "Gameface development build"};
constexpr MiddlewareId GAMEFACE_CORE{"Coherent Labs", "Gameface", "Gameface core library"};
constexpr MiddlewareId GAMEFACE_JS{"Coherent Labs", "Gameface", "Gameface JavaScript engine"};
constexpr MiddlewareId ICU{"Unicode Consortium", "ICU", "International Components for Unicode"};
constexpr MiddlewareId RENOIR{"SN Systems", "RENOIR", "GPU/CPU performance capture runtime"};
constexpr MiddlewareId WTF{"Unknown", "WTF", "Unidentified third-party library"};
constexpr MiddlewareId UNITY_IL2CPP{"Unity Technologies", "Unity IL2CPP",
                                    "Unity IL2CPP user assemblies (AOT-compiled C#)"};
constexpr MiddlewareId UNITY_BURST{"Unity Technologies", "Unity Burst",
                                   "Unity Burst-compiled job system code"};
constexpr MiddlewareId RESONANCE_AUDIO{"Google", "Resonance Audio",
                                       "Google Resonance Audio spatializer"};
constexpr MiddlewareId CRIWARE_UNITY{"CRI Middleware", "CRIWARE",
                                     "CRIWARE Unity plugin (ADX audio)"};
constexpr MiddlewareId WEBKIT_KITT{"Apple", "WebKit (Kitt)", "WebKit-based embedded browser"};
constexpr MiddlewareId EOS_SDK{"Epic Games", "Epic Online Services", "Epic Online Services SDK"};
constexpr MiddlewareId UNITY_PS5_PLATFORM{"Unity Technologies", "Unity PS5 Platform",
                                          "Unity PS5 player support module"};
constexpr MiddlewareId UNITY_PSN{"Unity Technologies", "Unity PSN",
                                 "Unity PSN package (com.unity.psn.ps5)"};
constexpr MiddlewareId UNITY_SAVE_DATA{"Unity Technologies", "Unity PS5 Save Data",
                                       "Unity SaveData package (com.unity.savedata.ps5)"};
constexpr MiddlewareId UNITY_COMMON_DIALOG{"Unity Technologies", "Unity PS5 Common Dialog",
                                           "Unity PS5 common dialog module"};
constexpr MiddlewareId UNITY_PS5_SPATIALIZER{"Unity Technologies", "Unity PS5 Audio Spatializer",
                                             "Unity PS5 audio spatializer plugin"};

struct CatalogEntry {
    std::string_view prefix;
    const MiddlewareId* id;
};

constexpr CatalogEntry kCatalog[] = {
    {"libfmodstudio", &FMOD_STUDIO},
    {"libfmod", &FMOD},
    {"libcoherentgtcore", &GAMEFACE_CORE},
    {"libcoherentgtjs", &GAMEFACE_JS},
    {"libcoherentuigt", &GAMEFACE},
    {"coherentuigtdevelopment", &GAMEFACE_DEV},
    {"coherentuigt", &GAMEFACE},
    {"libicuin", &ICU},
    {"librenoircore", &RENOIR},
    {"libwtf", &WTF},
    {"masteringsuite", &MASTERING_SUITE},
    {"mcdsp", &MCDSP},
    {"izotope", &IZOTOPE},
    {"auro", &AURO},
    {"ak", &WWISE_EFFECT},
    {"il2cppuserassemblies", &UNITY_IL2CPP},
    {"lib_burst_generated", &UNITY_BURST},
    {"libresonanceaudio", &RESONANCE_AUDIO},
    {"cri_ware_unity", &CRIWARE_UNITY},
    {"kitt", &WEBKIT_KITT},
    {"libeossdk", &EOS_SDK},
    {"eossdk", &EOS_SDK},
    {"eosnatlib", &EOS_SDK},
    {"ps5util", &UNITY_PS5_PLATFORM},
    {"psncore", &UNITY_PSN},
    {"psncommon", &UNITY_PSN},
    {"psn", &UNITY_PSN},
    {"savedata", &UNITY_SAVE_DATA},
    {"commondialog", &UNITY_COMMON_DIALOG},
    {"ps5audiospatializer", &UNITY_PS5_SPATIALIZER},
};

std::string ToLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool StartsWithIgnoreCase(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && ToLower(s.substr(0, prefix.size())) == ToLower(prefix);
}

bool EndsWithIgnoreCase(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           ToLower(s.substr(s.size() - suffix.size())) == ToLower(suffix);
}

bool IsSonyStem(std::string_view stem) {
    if (StartsWithIgnoreCase(stem, "libSce") || StartsWithIgnoreCase(stem, "libkernel")) {
        return true;
    }
    const std::string lower = ToLower(stem);
    return lower == "libc" || lower == "libc++" || lower == "libc++abi" || lower == "libm" ||
           lower == "sceaudio3d";
}

int KindOrder(ModuleKind k) {
    switch (k) {
    case ModuleKind::ThirdParty:
        return 0;
    case ModuleKind::Unknown:
        return 1;
    case ModuleKind::Sony:
    default:
        return 2;
    }
}

void WalkModules(const FileSys::IGameBackend& backend, const std::string& rel, int depth,
                 std::vector<std::string>& out) {
    if (depth > 4) {
        return;
    }
    for (const auto& entry : backend.ListDir(rel)) {
        const std::string child = rel.empty() ? entry.name : rel + "/" + entry.name;
        if (entry.is_directory) {
            const std::string lower = ToLower(entry.name);
            if (lower == "sce_sys" || lower == "decrypted") {
                continue;
            }
            WalkModules(backend, child, depth + 1, out);
        } else if (IsModuleFileName(entry.name)) {
            out.push_back(child);
        }
    }
}

nlohmann::json DetectionJson(const Detection& d) {
    nlohmann::json j = {{"value", d.value}, {"evidence", d.evidence}};
    if (d.confidence != 0 || d.score != 0) {
        j["confidence"] = d.confidence;
        j["score"] = d.score;
    }
    return j;
}

nlohmann::json DetectionsJson(const std::vector<Detection>& v) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : v) {
        arr.push_back(DetectionJson(d));
    }
    return arr;
}

} // namespace

std::string_view ModuleKindName(ModuleKind kind) {
    switch (kind) {
    case ModuleKind::ThirdParty:
        return "third-party";
    case ModuleKind::Sony:
        return "sony";
    case ModuleKind::Unknown:
    default:
        return "unknown";
    }
}

const MiddlewareId* MatchMiddleware(std::string_view stem) {
    const MiddlewareId* best = nullptr;
    size_t best_len = 0;
    for (const auto& entry : kCatalog) {
        if (entry.prefix.size() > best_len && StartsWithIgnoreCase(stem, entry.prefix)) {
            best = entry.id;
            best_len = entry.prefix.size();
        }
    }
    return best;
}

std::pair<ModuleKind, const MiddlewareId*> ClassifyStem(std::string_view stem) {
    if (IsSonyStem(stem)) {
        return {ModuleKind::Sony, nullptr};
    }
    if (const auto* id = MatchMiddleware(stem)) {
        return {ModuleKind::ThirdParty, id};
    }
    return {ModuleKind::Unknown, nullptr};
}

bool IsModuleFileName(std::string_view file_name) {
    return EndsWithIgnoreCase(file_name, ".prx") || EndsWithIgnoreCase(file_name, ".sprx") ||
           EndsWithIgnoreCase(file_name, ".so") || EndsWithIgnoreCase(file_name, ".native");
}

std::string ModuleStem(std::string_view file_name) {
    for (std::string_view ext : {".prx", ".sprx", ".so", ".native"}) {
        if (EndsWithIgnoreCase(file_name, ext)) {
            return std::string(file_name.substr(0, file_name.size() - ext.size()));
        }
    }
    return std::string(file_name);
}

ModuleReport AnalyzeModule(std::string rel_path, const std::vector<u8>& data) {
    ModuleReport m;
    m.rel_path = std::move(rel_path);
    const auto slash = m.rel_path.find_last_of('/');
    m.file_name = slash == std::string::npos ? m.rel_path : m.rel_path.substr(slash + 1);
    m.module_name = ModuleStem(m.file_name);
    m.size = data.size();

    const auto [kind, id] = ClassifyStem(m.module_name);
    m.kind = kind;
    if (id != nullptr) {
        m.vendor = id->vendor;
        m.product = id->product;
        m.description = id->description;
    }

    if (const auto info = Loader::ElfInfo::Parse(data)) {
        m.parseable = info->is_valid_elf;
        std::set<std::string> libs;
        for (const auto& lib : info->dynamic.import_libs) {
            libs.insert(lib.name);
        }
        for (const auto& sym : info->dynamic.symbols) {
            if (sym.is_export) {
                m.exports++;
            } else {
                m.imports++;
                if (!sym.library.empty()) {
                    libs.insert(sym.library);
                }
            }
        }
        m.import_libs.assign(libs.begin(), libs.end());
    }
    return m;
}

std::vector<std::string> ListGameModuleFiles(const std::filesystem::path& game_root) {
    std::vector<std::string> out;
    const auto backend = FileSys::OpenGameBackend(game_root);
    if (backend && backend->IsOpen()) {
        WalkModules(*backend, "", 0, out);
        std::sort(out.begin(), out.end());
    }
    return out;
}

GameTechReport AnalyzeGame(const std::filesystem::path& game_root,
                           const AnalyzeProgress& progress) {
    GameTechReport report;
    const auto backend = FileSys::OpenGameBackend(game_root);
    if (!backend || !backend->IsOpen()) {
        return report;
    }
    auto cancelled = [&] { return progress.cancel != nullptr && progress.cancel->load(); };

    std::vector<std::string> module_paths;
    WalkModules(*backend, "", 0, module_paths);
    std::sort(module_paths.begin(), module_paths.end());
    const size_t total = module_paths.size() + 1;

    if (progress.on_step) {
        progress.on_step("eboot.bin", 0, total);
    }
    if (auto eboot = backend->ReadFile("eboot.bin")) {
        report.eboot_found = true;
        report.eboot_size = eboot->size();
        report.eboot_strings = AnalyzeStrings(*eboot);
        if (const auto info = Loader::ElfInfo::Parse(*eboot)) {
            report.eboot_is_self = info->is_self;
            report.lib_versions = info->lib_versions;
        }
    }

    bool has_unity_module = false;
    for (size_t i = 0; i < module_paths.size(); i++) {
        if (cancelled()) {
            report.cancelled = true;
            return report;
        }
        if (progress.on_step) {
            progress.on_step(module_paths[i], i + 1, total);
        }
        const auto data = backend->ReadFile(module_paths[i]);
        ModuleReport m = AnalyzeModule(module_paths[i], data.value_or(std::vector<u8>{}));
        if (m.vendor == "Unity Technologies") {
            has_unity_module = true;
        }
        report.modules.push_back(std::move(m));
    }
    std::stable_sort(report.modules.begin(), report.modules.end(),
                     [](const ModuleReport& a, const ModuleReport& b) {
                         if (KindOrder(a.kind) != KindOrder(b.kind)) {
                             return KindOrder(a.kind) < KindOrder(b.kind);
                         }
                         return ToLower(a.module_name) < ToLower(b.module_name);
                     });

    for (const auto& entry : backend->ListDir("")) {
        if (entry.is_directory && ToLower(entry.name) == "engine") {
            report.layout_engine_hints.emplace_back("Unreal Engine (\"Engine\" folder present)");
            break;
        }
    }
    if (has_unity_module) {
        report.layout_engine_hints.emplace_back("Unity (Unity runtime modules present)");
    }
    if (progress.on_step) {
        progress.on_step({}, total, total);
    }
    return report;
}

std::string EngineSummary(const GameTechReport& report) {
    if (const auto& e = report.eboot_strings.engine) {
        return e->value + " (" + std::to_string(e->confidence) + "% confidence)";
    }
    if (!report.layout_engine_hints.empty()) {
        return report.layout_engine_hints.front();
    }
    return "Unknown / proprietary";
}

std::string ReportToJson(const GameTechReport& report, const std::string& title,
                         const std::string& serial) {
    const auto& s = report.eboot_strings;
    nlohmann::json j;
    j["title"] = title;
    j["serial"] = serial;
    j["engine_summary"] = EngineSummary(report);
    j["eboot"] = {{"found", report.eboot_found},
                  {"size", report.eboot_size},
                  {"is_self", report.eboot_is_self},
                  {"string_count", s.string_count}};
    if (s.engine) {
        j["engine"] = DetectionJson(*s.engine);
    }
    j["layout_engine_hints"] = report.layout_engine_hints;
    j["third_party"] = DetectionsJson(s.third_party_libs);
    j["sdk_hints"] = DetectionsJson(s.sdk_hints);
    j["detected_versions"] = DetectionsJson(s.detected_versions);
    j["custom_forks"] = DetectionsJson(s.custom_forks);
    j["project_paths"] = DetectionsJson(s.project_paths);
    if (s.build_system) {
        j["build_system"] = DetectionJson(*s.build_system);
    }
    if (s.source_depot) {
        j["source_depot"] = DetectionJson(*s.source_depot);
    }
    j["sce_libraries"] = s.sce_libraries;
    j["source_paths"] = s.source_paths;

    nlohmann::json versions = nlohmann::json::array();
    for (const auto& v : report.lib_versions) {
        versions.push_back({{"library", v.name},
                            {"version_raw", v.version_raw},
                            {"version_guess", v.GuessedVersionString()}});
    }
    j["lib_versions"] = versions;

    nlohmann::json modules = nlohmann::json::array();
    for (const auto& m : report.modules) {
        nlohmann::json mj = {{"path", m.rel_path},
                             {"module", m.module_name},
                             {"kind", std::string(ModuleKindName(m.kind))},
                             {"size", m.size},
                             {"parseable", m.parseable},
                             {"imports", m.imports},
                             {"exports", m.exports},
                             {"import_libs", m.import_libs}};
        if (!m.product.empty()) {
            mj["vendor"] = m.vendor;
            mj["product"] = m.product;
            mj["description"] = m.description;
        }
        modules.push_back(std::move(mj));
    }
    j["modules"] = modules;
    // Strings come straight from game binaries and may hold any bytes;
    // replace rather than throw on invalid UTF-8.
    return j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace Core::Analysis
