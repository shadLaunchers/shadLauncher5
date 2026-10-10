// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Loader::ElfInfo {

std::string ComputeNid(std::string_view name);

class NidCatalog {
public:
    NidCatalog();
    [[nodiscard]] std::optional<std::string> Resolve(const std::string& nid) const;
    [[nodiscard]] size_t size() const {
        std::shared_lock lock(mutex_);
        return by_nid_.size();
    }
    size_t LoadNamesFile(const std::filesystem::path& path);
    size_t LoadNidsCsv(std::string_view content);
    size_t LoadNidsCsvFile(const std::filesystem::path& path);
    size_t LoadSyncedCatalog();

private:
    void AddBuiltins();
    void Add(std::string_view name);
    size_t LoadNidsCsvRich(std::string_view first_line, std::string_view content);
    size_t LoadNidsCsvLegacy(std::string_view content);
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> by_nid_;
};
std::filesystem::path SyncedCatalogDir();

NidCatalog& GetMutableDefaultNidCatalog();
const NidCatalog& GetDefaultNidCatalog();

} // namespace Loader::ElfInfo
