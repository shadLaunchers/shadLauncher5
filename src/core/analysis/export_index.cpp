// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "export_index.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include "core/analysis/middleware.h"
#include "core/file_format/elf_info.h"
#include "core/file_format/nid_catalog.h"
#include "core/file_sys/game_backend.h"

namespace Core::Analysis {

namespace {

std::string ToLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool IsSysModuleFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    unsigned char magic[4] = {};
    if (!f.read(reinterpret_cast<char*>(magic), sizeof(magic))) {
        return false;
    }
    static constexpr unsigned char kElf[4] = {0x7F, 'E', 'L', 'F'};
    static constexpr unsigned char kSelf[4] = {0x4F, 0x15, 0x3D, 0x1D};
    static constexpr unsigned char kSelf2[4] = {0x54, 0x14, 0xF5, 0xEE};
    return std::equal(magic, magic + 4, kElf) || std::equal(magic, magic + 4, kSelf) ||
           std::equal(magic, magic + 4, kSelf2);
}

std::vector<std::filesystem::path> ListSysModuleFiles(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(
        dir, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 4) {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code type_ec;
        if (it->is_regular_file(type_ec) && IsSysModuleFile(it->path())) {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<u8> ReadHostFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::string FolderSignature(const std::vector<std::filesystem::path>& files) {
    u64 total_size = 0;
    std::filesystem::file_time_type newest{};
    for (const auto& f : files) {
        std::error_code ec;
        total_size += std::filesystem::file_size(f, ec);
        const auto t = std::filesystem::last_write_time(f, ec);
        if (!ec && t > newest) {
            newest = t;
        }
    }
    return std::to_string(files.size()) + ":" + std::to_string(total_size) + ":" +
           std::to_string(newest.time_since_epoch().count());
}

bool Cancelled(const IndexProgress& p) {
    return p.cancel != nullptr && p.cancel->load();
}

} // namespace

std::vector<std::filesystem::path> FindSysModuleFiles(const std::filesystem::path& dir) {
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) {
        return {};
    }
    return ListSysModuleFiles(dir);
}

void ExportIndex::AddModule(ExportSource source, const std::vector<u8>& data) {
    const size_t source_index = sources.size();
    if (data.empty()) {
        source.problem = "could not be read";
    } else if (const auto info = Loader::ElfInfo::Parse(data); !info || !info->is_valid_elf) {
        source.problem = "not a readable SELF/ELF (encrypted or not a module)";
    } else if (!info->dynamic.readable) {
        source.problem = info->dynamic.unavailable_reason.empty()
                             ? "no readable dynamic section"
                             : info->dynamic.unavailable_reason;
    } else {
        source.readable = true;
        for (const auto& m : info->dynamic.export_modules) {
            source.declared_modules.push_back(m.name);
        }
        for (const auto& l : info->dynamic.export_libs) {
            source.declared_libraries.push_back(l.name);
        }
        for (const auto& sym : info->dynamic.symbols) {
            if (!sym.is_export || sym.nid.empty()) {
                continue;
            }
            ExportEntry e;
            e.nid = sym.nid;
            e.library = sym.library;
            e.module = sym.module;
            e.address = sym.value;
            e.size = sym.size;
            e.source = source_index;
            by_nid.emplace(e.nid, entries.size());
            entries.push_back(std::move(e));
            source.export_count++;
        }
    }
    sources.push_back(std::move(source));
}

ExportIndex BuildGameExportIndex(const std::filesystem::path& game_root,
                                 const IndexProgress& progress) {
    ExportIndex index;
    const auto backend = FileSys::OpenGameBackend(game_root);
    if (!backend || !backend->IsOpen()) {
        return index;
    }
    std::vector<std::string> files;
    if (backend->Exists("eboot.bin")) {
        files.emplace_back("eboot.bin");
    }
    for (auto& f : ListGameModuleFiles(game_root)) {
        files.push_back(std::move(f));
    }
    for (size_t i = 0; i < files.size(); i++) {
        if (Cancelled(progress)) {
            index.cancelled = true;
            return index;
        }
        if (progress.on_step) {
            progress.on_step(files[i], i, files.size());
        }
        ExportSource src;
        src.display_path = files[i];
        src.rel_path = files[i];
        index.AddModule(std::move(src), backend->ReadFile(files[i]).value_or(std::vector<u8>{}));
    }
    if (progress.on_step) {
        progress.on_step({}, files.size(), files.size());
    }
    return index;
}

std::shared_ptr<const ExportIndex> GetSysModulesExportIndex(const std::filesystem::path& dir,
                                                            const IndexProgress& progress) {
    static std::mutex s_mutex;
    static std::map<std::string, std::pair<std::string, std::shared_ptr<const ExportIndex>>>
        s_cache; // folder -> (signature, index)

    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) {
        return std::make_shared<const ExportIndex>();
    }
    const auto files = ListSysModuleFiles(dir);
    const std::string key = std::filesystem::absolute(dir, ec).generic_string();
    const std::string signature = FolderSignature(files);
    {
        std::lock_guard lock(s_mutex);
        if (const auto it = s_cache.find(key);
            it != s_cache.end() && it->second.first == signature) {
            return it->second.second;
        }
    }

    auto index = std::make_shared<ExportIndex>();
    for (size_t i = 0; i < files.size(); i++) {
        if (Cancelled(progress)) {
            index->cancelled = true;
            return index; // not cached
        }
        const std::string rel = std::filesystem::relative(files[i], dir, ec).generic_string();
        if (progress.on_step) {
            progress.on_step(rel, i, files.size());
        }
        ExportSource src;
        src.display_path = "sys_modules/" + rel;
        src.rel_path = rel;
        src.host_path = files[i];
        src.from_sys_modules = true;
        index->AddModule(std::move(src), ReadHostFile(files[i]));
    }
    if (progress.on_step) {
        progress.on_step({}, files.size(), files.size());
    }
    std::lock_guard lock(s_mutex);
    s_cache[key] = {signature, index};
    return index;
}

std::string ExportName(const ExportEntry& entry) {
    return Loader::ElfInfo::GetDefaultNidCatalog().Resolve(entry.nid).value_or(std::string());
}

std::vector<ExportHit> SearchExports(const std::vector<const ExportIndex*>& indexes,
                                     std::string_view query, size_t limit, size_t* total_matches) {
    // Trim surrounding whitespace.
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.front()))) {
        query.remove_prefix(1);
    }
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.back()))) {
        query.remove_suffix(1);
    }
    std::vector<ExportHit> exact;
    std::vector<ExportHit> partial;
    if (query.empty()) {
        if (total_matches != nullptr) {
            *total_matches = 0;
        }
        return {};
    }

    const std::string q(query);
    const bool identifier =
        std::none_of(q.begin(), q.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const std::string hashed = identifier ? Loader::ElfInfo::ComputeNid(q) : std::string();
    const std::string needle = ToLower(q);

    for (const ExportIndex* index : indexes) {
        if (index == nullptr) {
            continue;
        }
        std::set<size_t> taken;
        auto add_exact = [&](const std::string& nid, MatchKind kind) {
            const auto [begin, end] = index->by_nid.equal_range(nid);
            for (auto it = begin; it != end; ++it) {
                if (taken.insert(it->second).second) {
                    exact.push_back({index, it->second, kind});
                }
            }
        };
        add_exact(q, MatchKind::ExactNid);
        if (!hashed.empty() && hashed != q) {
            add_exact(hashed, MatchKind::HashedName);
        }
        if (needle.size() >= 2) {
            for (size_t i = 0; i < index->entries.size(); i++) {
                if (taken.count(i) != 0) {
                    continue;
                }
                const std::string name = ExportName(index->entries[i]);
                if (!name.empty() && ToLower(name).find(needle) != std::string::npos) {
                    partial.push_back({index, i, MatchKind::NameContains});
                }
            }
        }
    }

    if (total_matches != nullptr) {
        *total_matches = exact.size() + partial.size();
    }
    exact.insert(exact.end(), partial.begin(), partial.end());
    if (exact.size() > limit) {
        exact.resize(limit);
    }
    return exact;
}

} // namespace Core::Analysis
