// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <fstream>
#include <sstream>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "hotkeys_config.h"

namespace Core::Input {

using Common::FS::GetUserPath;
using Common::FS::PathType;

bool HotkeysConfig::Load() {
    m_path = GetUserPath(PathType::UserDir) / "hotkeys.json";
    m_bindings_cache.clear();
    m_dirty_names.clear();
    m_valid = false;

    if (!std::filesystem::exists(m_path)) {
        // Not an error: the emulator writes this file itself on first run.
        // Every known hotkey just has no bindings loaded yet.
        m_root = nlohmann::ordered_json::object();
        m_root["version"] = 1;
        m_root["bindings"] = nlohmann::ordered_json::array();
        m_valid = true;
        return true;
    }

    std::ifstream file(m_path, std::ios::binary);
    if (!file) {
        LOG_ERROR(Common_Filesystem, "Failed to open {}", m_path.string());
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string text = ss.str();

    try {
        // ignore_comments: matches the emulator's own tolerant parser
        // (docs/input-bindings.md section 4). Trailing-comma tolerance
        // there is a property of the emulator's vendored nlohmann::json;
        // this parse call does not additionally guarantee it.
        m_root = nlohmann::ordered_json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/true,
                                               /*ignore_comments=*/true);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Common_Filesystem, "Failed to parse {}: {}", m_path.string(), e.what());
        return false;
    }
    if (!m_root.is_object()) {
        LOG_ERROR(Common_Filesystem, "{} root is not an object", m_path.string());
        return false;
    }
    if (!m_root.contains("bindings") || !m_root["bindings"].is_array()) {
        m_root["bindings"] = nlohmann::ordered_json::array();
    }
    m_valid = true;
    return true;
}

std::vector<HotkeyBinding> HotkeysConfig::ParseBindingsFor(
    const nlohmann::ordered_json& bindings_array, const std::string& hotkey_name) {
    std::vector<HotkeyBinding> result;
    for (const auto& entry : bindings_array) {
        if (!entry.is_object()) {
            continue; // "entry is not an object" -> skip (section 9)
        }
        const auto output_it = entry.find("output");
        if (output_it == entry.end() || !output_it->is_string() ||
            output_it->get<std::string>() != hotkey_name) {
            continue;
        }
        const auto input_it = entry.find("input");
        if (input_it == entry.end()) {
            continue; // "input missing" -> skip
        }

        HotkeyBinding binding;
        if (input_it->is_string()) {
            binding.input.push_back(input_it->get<std::string>());
        } else if (input_it->is_array()) {
            for (const auto& v : *input_it) {
                if (v.is_string()) {
                    binding.input.push_back(v.get<std::string>());
                }
            }
            if (binding.input.empty()) {
                continue; // "input array is empty, or has no usable name" -> skip
            }
            if (binding.input.size() > 3) {
                // "input names more than 3 keys" -> keep the first 3, warn
                LOG_WARNING(Common_Filesystem, "{}: hotkey '{}' chord has more than 3 keys, "
                                               "keeping the first 3",
                           "hotkeys.json", hotkey_name);
                binding.input.resize(3);
            }
        } else {
            continue; // "input is neither string nor array" -> skip
        }

        if (const auto gp_it = entry.find("gamepad"); gp_it != entry.end() && gp_it->is_number_integer()) {
            binding.gamepad = gp_it->get<int>();
        }
        result.push_back(std::move(binding));
    }
    return result;
}

const std::vector<HotkeyBinding>& HotkeysConfig::GetBindings(const std::string& hotkey_name) const {
    if (const auto it = m_bindings_cache.find(hotkey_name); it != m_bindings_cache.end()) {
        return it->second;
    }
    auto [it, _] = m_bindings_cache.emplace(
        hotkey_name, m_valid ? ParseBindingsFor(m_root["bindings"], hotkey_name)
                             : std::vector<HotkeyBinding>{});
    return it->second;
}

void HotkeysConfig::SetBindings(const std::string& hotkey_name, std::vector<HotkeyBinding> bindings) {
    m_bindings_cache[hotkey_name] = std::move(bindings);
    if (std::find(m_dirty_names.begin(), m_dirty_names.end(), hotkey_name) ==
        m_dirty_names.end()) {
        m_dirty_names.push_back(hotkey_name);
    }
}

bool HotkeysConfig::IsDirty(const std::string& hotkey_name) const {
    return std::find(m_dirty_names.begin(), m_dirty_names.end(), hotkey_name) !=
           m_dirty_names.end();
}

bool HotkeysConfig::Save() const {
    if (!m_valid) {
        LOG_ERROR(Common_Filesystem, "Refusing to write {}: it was never successfully loaded",
                  m_path.string());
        return false;
    }
    if (m_dirty_names.empty()) {
        return true; // nothing to do
    }

    nlohmann::ordered_json new_bindings = nlohmann::ordered_json::array();

    // Carry through every existing entry whose output isn't one of the
    // hotkeys this session touched -- other known hotkeys never edited,
    // AND any entry with an output we don't recognize at all (section 7).
    if (m_root.contains("bindings") && m_root["bindings"].is_array()) {
        for (const auto& entry : m_root["bindings"]) {
            if (!entry.is_object()) {
                continue;
            }
            const auto output_it = entry.find("output");
            const std::string output = (output_it != entry.end() && output_it->is_string())
                                           ? output_it->get<std::string>()
                                           : std::string{};
            const bool touched_this_session =
                std::find(m_dirty_names.begin(), m_dirty_names.end(), output) !=
                m_dirty_names.end();
            if (!touched_this_session) {
                new_bindings.push_back(entry);
            }
        }
    }

    // Append fresh entries for every hotkey actually edited this session.
    for (const auto& name : m_dirty_names) {
        const auto cache_it = m_bindings_cache.find(name);
        if (cache_it == m_bindings_cache.end()) {
            continue;
        }
        for (const auto& binding : cache_it->second) {
            if (binding.input.empty()) {
                continue;
            }
            nlohmann::ordered_json entry;
            entry["output"] = name;
            entry["input"] =
                binding.input.size() == 1 ? nlohmann::ordered_json(binding.input.front())
                                          : nlohmann::ordered_json(binding.input);
            if (binding.gamepad != 0) {
                entry["gamepad"] = binding.gamepad;
            }
            new_bindings.push_back(std::move(entry));
        }
    }

    nlohmann::ordered_json out = m_root;
    out["bindings"] = std::move(new_bindings);
    if (!out.contains("version")) {
        out["version"] = 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(m_path.parent_path(), ec);

    std::ofstream file(m_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        LOG_ERROR(Common_Filesystem, "Failed to write {}", m_path.string());
        return false;
    }
    file << out.dump(4);
    return static_cast<bool>(file);
}

bool HotkeysConfig::ResetToDefaults() {
    std::error_code ec;
    if (!std::filesystem::exists(m_path, ec)) {
        return true;
    }
    std::filesystem::remove(m_path, ec);
    if (ec) {
        LOG_ERROR(Common_Filesystem, "Failed to remove {}: {}", m_path.string(), ec.message());
        return false;
    }
    return Load();
}

} // namespace Core::Input
