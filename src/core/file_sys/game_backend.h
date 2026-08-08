// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"

namespace Core::FileSys {

struct DirEntry {
    std::string name;
    bool is_directory{false};
};

class IGameBackend {
public:
    virtual ~IGameBackend() = default;

    [[nodiscard]] virtual bool Exists(std::string_view rel_path) const = 0;
    [[nodiscard]] virtual bool IsDirectory(std::string_view rel_path) const = 0;

    // Reads an entire file into memory. Returns nullopt if it doesn't
    // exist or isn't a file.
    [[nodiscard]] virtual std::optional<std::vector<u8>> ReadFile(
        std::string_view rel_path) const = 0;

    // Lists immediate children of rel_path ("" for the root). Returns an
    // empty vector if rel_path doesn't exist or isn't a directory.
    [[nodiscard]] virtual std::vector<DirEntry> ListDir(std::string_view rel_path) const = 0;

    [[nodiscard]] virtual std::optional<std::filesystem::path> HostRootPath() const = 0;
    [[nodiscard]] virtual std::filesystem::path RootPath() const = 0;
    [[nodiscard]] virtual bool IsArchive() const = 0;
    [[nodiscard]] virtual bool IsOpen() const = 0;
};

// Relative path of the PS5 title metadata file inside a game root. shadLauncher5
// targets PS5 titles, which use "param.json" rather than PS4's binary
// "param.sfo" (see Common::FS::FindParamPath).
inline constexpr std::string_view ParamRelPath = "sce_sys/param.json";

// True if path is a regular file with a ".zar" extension
[[nodiscard]] bool IsZArchiveFile(const std::filesystem::path& path);

// Resolves a candidate game/overlay root to an existing directory or .zar
// archive file
[[nodiscard]] std::optional<std::filesystem::path> ResolveGameRoot(
    const std::filesystem::path& root);

// Opens the appropriate backend for root
[[nodiscard]] std::unique_ptr<IGameBackend> OpenGameBackend(const std::filesystem::path& root);

[[nodiscard]] std::optional<std::vector<u8>> ReadGameFile(const std::filesystem::path& game_root,
                                                          std::string_view rel_path);

[[nodiscard]] std::optional<std::filesystem::path> ResolveGameFilePath(
    const std::filesystem::path& game_root, std::string_view rel_path);

// True if game_root (a directory or a .zar archive) holds "sce_sys/param.json".
// This is the archive-aware counterpart of Common::FS::FindParamPath and is what
// decides whether a scanned entry is a real title.
[[nodiscard]] bool HasParamFile(const std::filesystem::path& game_root);

// Resolves "sce_sys/param.json" to a real on-disk path. Directories return the
// file itself; archives return an extracted copy under the cache dir, so the
// result can be handed straight to PSF::Open().
[[nodiscard]] std::optional<std::filesystem::path> ResolveParamPath(
    const std::filesystem::path& game_root);

} // namespace Core::FileSys
