// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "hotkeys_config.h"
#include "input_ids.h"
#include "text_preserving_json.h"

namespace Core::Input {

using Common::FS::GetUserPath;
using Common::FS::PathType;

namespace {
constexpr const char* kFreshFileTemplate =
    "{\n    \"version\": 1,\n\n    \"bindings\": [\n    ]\n}\n";

std::string FormatHotkeyEntry(const std::string& name, const HotkeyBinding& binding) {
    nlohmann::ordered_json entry;
    entry["output"] = name;
    entry["input"] = binding.input.size() == 1 ? nlohmann::ordered_json(binding.input.front())
                                               : nlohmann::ordered_json(binding.input);
    if (binding.gamepad != 0) {
        entry["gamepad"] = binding.gamepad;
    }
    return entry.dump();
}

// True if `trivia` ends part-way through a "//" comment, so whatever is
// appended next would be swallowed by that comment's line.
bool EndsInsideLineComment(const std::string& trivia) {
    const auto slashes = trivia.rfind("//");
    return slashes != std::string::npos && trivia.find('\n', slashes) == std::string::npos;
}

bool HasComment(const std::string& trivia) {
    return trivia.find("//") != std::string::npos || trivia.find("/*") != std::string::npos;
}
} // namespace

bool HotkeysConfig::Load() {
    m_path = GetUserPath(PathType::UserDir) / "hotkeys.json";
    m_bindings_cache.clear();
    m_dirty_names.clear();
    m_valid = false;

    if (!std::filesystem::exists(m_path)) {
        // Not an error: the emulator writes this file itself on first run.
        // Every known hotkey just has no bindings loaded yet.
        m_raw_text = kFreshFileTemplate;
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
    m_raw_text = ss.str();

    try {
        // ignore_comments: matches the emulator's own tolerant parser
        // (docs/input-bindings.md section 4).
        m_root = TextJson::ParseTolerant<nlohmann::ordered_json>(m_raw_text);
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
                LOG_WARNING(Common_Filesystem, "hotkeys.json: hotkey '{}' chord has more than 3 "
                                               "keys, keeping the first 3",
                           hotkey_name);
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

    const TextJson::RootScan root = TextJson::ScanRoot(m_raw_text);
    if (!root.ok) {
        LOG_ERROR(Common_Filesystem, "{}: couldn't re-locate its own structure to edit it safely",
                 m_path.string());
        return false;
    }

    std::string inner;
    bool had_array = false;
    for (const auto& e : root.entries) {
        if (e.key == "bindings") {
            const std::string value_text = m_raw_text.substr(e.value_start, e.value_end - e.value_start);
            if (value_text.size() >= 2 && value_text.front() == '[' && value_text.back() == ']') {
                inner = value_text.substr(1, value_text.size() - 2);
                had_array = true;
            }
            break;
        }
    }
    TextJson::ArrayContent existing = had_array ? TextJson::SplitArray(inner) : TextJson::ArrayContent{};
    if (had_array && !existing.ok) {
        LOG_ERROR(Common_Filesystem, "{}: couldn't parse its own \"bindings\" array structure to "
                                     "edit it safely",
                 m_path.string());
        return false;
    }

    const std::set<std::string> dirty_set(m_dirty_names.begin(), m_dirty_names.end());

    std::string rebuilt;
    bool first = true;
    for (const auto& elem : existing.elements) {
        std::string output;
        try {
            const auto parsed_elem = TextJson::ParseTolerant<nlohmann::ordered_json>(elem.text);
            if (parsed_elem.is_object() && parsed_elem.contains("output") &&
                parsed_elem["output"].is_string()) {
                output = parsed_elem["output"].get<std::string>();
            }
        } catch (const nlohmann::json::exception&) {
            // Unparseable -- can't identify it, so never something we'd
            // replace; keep it verbatim below.
        }
        if (!output.empty() && dirty_set.count(output)) {
            continue; // dropped -- replaced by fresh entries below
        }
        // The separator is ours to write: SplitArray hands back only the
        // trivia before each element, never the comma. It goes before the
        // leading trivia so a comment sitting above an element stays above
        // it.
        if (!first) {
            rebuilt += ",";
        }
        rebuilt += elem.leading;
        rebuilt += elem.text;
        first = false;
    }

    for (const auto& name : m_dirty_names) {
        const auto cache_it = m_bindings_cache.find(name);
        if (cache_it == m_bindings_cache.end()) {
            continue;
        }
        for (const auto& binding : cache_it->second) {
            if (binding.input.empty()) {
                continue;
            }
            if (!first) {
                rebuilt += ",\n        ";
            } else {
                rebuilt += "\n        ";
                first = false;
            }
            rebuilt += FormatHotkeyEntry(name, binding);
        }
    }
    // A note written after the last entry is trivia belonging to nobody, so
    // nothing above would have carried it -- put it back rather than
    // deleting it.
    if (HasComment(existing.trailing)) {
        rebuilt += existing.trailing;
        if (EndsInsideLineComment(existing.trailing)) {
            rebuilt += "\n    ";
        }
    } else if (!rebuilt.empty()) {
        rebuilt += "\n    ";
    }

    const std::string new_array_text = "[" + rebuilt + "]";
    const std::string new_text = TextJson::ReplaceOrInsertTopLevelValue(m_raw_text, "bindings", new_array_text);

    std::error_code ec;
    std::filesystem::create_directories(m_path.parent_path(), ec);

    std::ofstream file(m_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        LOG_ERROR(Common_Filesystem, "Failed to write {}", m_path.string());
        return false;
    }
    file << new_text;
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

std::vector<std::string> HotkeysConfig::Validate() const {
    std::vector<std::string> warnings;
    if (!m_valid || !m_root.contains("bindings") || !m_root["bindings"].is_array()) {
        return warnings;
    }

    size_t index = 0;
    for (const auto& entry : m_root["bindings"]) {
        const std::string tag = "Entry #" + std::to_string(index++);

        if (!entry.is_object()) {
            warnings.push_back(tag + " isn't a JSON object, ignored.");
            continue;
        }
        const auto output_it = entry.find("output");
        if (output_it == entry.end() || !output_it->is_string() ||
            output_it->get<std::string>().empty()) {
            warnings.push_back(tag + " has no usable \"output\" name, ignored.");
            continue;
        }
        const std::string hotkey_name = output_it->get<std::string>();
        bool is_known = false;
        for (const auto& h : kKnownHotkeys) {
            if (h.name == hotkey_name) {
                is_known = true;
                break;
            }
        }
        if (!is_known) {
            warnings.push_back(tag + ": \"" + hotkey_name +
                              "\" isn't one of the ten hotkeys this build knows -- kept as-is, "
                              "but not shown in the editor.");
        }

        const auto input_it = entry.find("input");
        if (input_it == entry.end()) {
            warnings.push_back(tag + " (\"" + hotkey_name + "\") has no \"input\", ignored.");
            continue;
        }
        std::vector<std::string> names;
        if (input_it->is_string()) {
            names.push_back(input_it->get<std::string>());
        } else if (input_it->is_array()) {
            for (const auto& v : *input_it) {
                if (v.is_string()) {
                    names.push_back(v.get<std::string>());
                }
            }
            if (names.empty()) {
                warnings.push_back(tag + " (\"" + hotkey_name +
                                  "\") has an empty or unusable \"input\" array, ignored.");
                continue;
            }
            if (names.size() > 3) {
                warnings.push_back(tag + " (\"" + hotkey_name + "\") has more than 3 keys; "
                                                                 "only the first 3 are kept.");
            }
        } else {
            warnings.push_back(tag + " (\"" + hotkey_name +
                              "\") has an \"input\" that's neither a string nor an array, "
                              "ignored.");
            continue;
        }
        for (const auto& name : names) {
            if (name != "unmapped" && !IsKnownInput(name)) {
                warnings.push_back(tag + " (\"" + hotkey_name + "\"): input \"" + name +
                                  "\" isn't a name this build recognizes.");
            }
        }

        if (const auto gp_it = entry.find("gamepad"); gp_it != entry.end()) {
            if (!gp_it->is_number_integer()) {
                warnings.push_back(tag + " (\"" + hotkey_name +
                                  "\"): \"gamepad\" isn't an integer, ignored.");
            } else {
                const int gp = gp_it->get<int>();
                if (gp < 1 || gp > 4) {
                    warnings.push_back(tag + " (\"" + hotkey_name + "\"): \"gamepad\" " +
                                      std::to_string(gp) + " is outside 1-4, clamped to " +
                                      std::to_string(std::clamp(gp, 1, 4)) + ".");
                }
            }
        }
    }

    return warnings;
}

} // namespace Core::Input
