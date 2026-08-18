// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <QString>

#include "common/path_util.h"
#include "common/types.h"
#include "core/file_format/param.h"
#include "core/file_sys/game_backend.h"

struct GameInfo {
    std::string path; // root path of game directory (normaly directory that contains eboot.bin)
    std::string icon_path;   // path of icon0.png
    std::string update_path; // path of update directory if any
    std::string pic_path;    // path of pic0.png, used as the game list background
    std::string snd0_path;   // path of snd0.at9
    std::string name;
    std::string serial;
    std::string app_ver;
    std::string region;
    std::string fw;
    std::string category;
    std::string sdk_ver;
    std::vector<std::string>
        np_comm_ids; // normally there is only one np_comm_id, but found games with multiple ids

    u64 size_on_disk = UINT64_MAX;
};

namespace GameInfoTools {
static QString GetRegion(char region) {
    switch (region) {
    case 'U':
        return "USA";
    case 'E':
        return "Europe";
    case 'J':
        return "Japan";
    case 'H':
        return "Asia";
    case 'I':
        return "World";
    default:
        return "Unknown";
    }
}

// Strips a trailing ".zar" extension so overlay suffixes ("-UPDATE", "-patch")
// can be appended to the stem of an archive the same way they are to a folder.
static std::filesystem::path StripZArchiveExtension(const std::filesystem::path& path) {
    if (path.extension() == ".zar") {
        std::filesystem::path stripped = path;
        stripped.replace_extension();
        return stripped;
    }
    return path;
}

static void SceUpdateChecker(const std::string sceItem, std::string& gameItem,
                             std::filesystem::path& update_folder,
                             std::filesystem::path& patch_folder, std::string& game_folder) {

    std::filesystem::path game_folder_path = game_folder;
    const std::string rel_path = "sce_sys/" + sceItem;

    // Each candidate may be either a plain folder or a ".zar" archive;
    // ResolveGameRoot picks whichever exists and ResolveGameFilePath extracts
    // the entry to the cache dir when it lives inside an archive.
    for (const auto& candidate : {update_folder, patch_folder, game_folder_path}) {
        if (const auto root = Core::FileSys::ResolveGameRoot(candidate)) {
            if (const auto resolved = Core::FileSys::ResolveGameFilePath(*root, rel_path)) {
                gameItem = resolved->string();
                return;
            }
        }
    }
    gameItem = (game_folder_path / "sce_sys" / sceItem).string();
}

static GameInfo readGameInfo(const std::filesystem::path& filePath) {
    GameInfo game;
    game.path = filePath.string();
    const std::filesystem::path stem_path = StripZArchiveExtension(filePath);
    std::filesystem::path game_update_path = stem_path;
    game_update_path += "-UPDATE";
    std::filesystem::path game_patch_path = stem_path;
    game_patch_path += "-patch";

    // PS5 titles use "param.json" for title metadata.
    std::filesystem::path param_path;
    {
        std::string resolved_param;
        SceUpdateChecker("param.json", resolved_param, game_update_path, game_patch_path,
                         game.path);
        if (std::error_code ec; std::filesystem::is_regular_file(resolved_param, ec) && !ec) {
            param_path = resolved_param;
        }
    }

    Param param;
    if (!param_path.empty() && param.Open(param_path)) {
        SceUpdateChecker("icon0.png", game.icon_path, game_update_path, game_patch_path, game.path);
        SceUpdateChecker("pic0.png", game.pic_path, game_update_path, game_patch_path, game.path);
        SceUpdateChecker("snd0.at9", game.snd0_path, game_update_path, game_patch_path, game.path);

        game.name = param.title;
        game.serial = param.title_id;
        if (!param.content_id.empty()) {
            game.region = GetRegion(param.content_id.front()).toStdString();
        }
        game.fw = param.system_ver_string;
        game.app_ver = param.app_ver;
    }
    return game;
}

} // namespace GameInfoTools
