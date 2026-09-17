// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

#include "bindings_config.h"
#include "common/logging/log.h"

namespace Core::Input {

namespace {

// Splits "cross:2" into ("cross", 2); "cross" into ("cross", 0). A ":n" that
// isn't a number is ignored per input-bindings.md section 9 ("':n' suffix is
// not a number -> ignores the suffix, warns").
std::pair<std::string, int> SplitPortSuffix(const std::string& value) {
    const auto colon = value.rfind(':');
    if (colon == std::string::npos) {
        return {value, 0};
    }
    const std::string suffix = value.substr(colon + 1);
    if (suffix.empty() ||
        !std::all_of(suffix.begin(), suffix.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return {value, 0};
    }
    return {value.substr(0, colon), std::stoi(suffix)};
}

// Section 5/9: a named port is clamped into 1-4, 0 becomes 1.
int ClampPort(int port) {
    if (port <= 0) {
        return 1;
    }
    if (port > 4) {
        return 4;
    }
    return port;
}

std::optional<BindingsConfig::FlatBinding> ParseBindingEntry(const nlohmann::ordered_json& entry) {
    if (!entry.is_object()) {
        return std::nullopt;
    }
    const auto output_it = entry.find("output");
    if (output_it == entry.end() || !output_it->is_string()) {
        return std::nullopt;
    }
    const auto [base_output, output_port] = SplitPortSuffix(output_it->get<std::string>());
    if (base_output.empty()) {
        return std::nullopt;
    }

    const auto input_it = entry.find("input");
    if (input_it == entry.end()) {
        return std::nullopt;
    }

    PortedBinding binding;
    int input_port = 0;
    if (input_it->is_string()) {
        auto [base_input, p] = SplitPortSuffix(input_it->get<std::string>());
        binding.input.push_back(std::move(base_input));
        input_port = p;
    } else if (input_it->is_array()) {
        for (const auto& v : *input_it) {
            if (v.is_string()) {
                binding.input.push_back(v.get<std::string>());
            }
        }
        if (binding.input.empty()) {
            return std::nullopt;
        }
        if (binding.input.size() > 3) {
            LOG_WARNING(Common_Filesystem,
                        "{}: '{}' chord has more than 3 keys, keeping the first 3", "bindings",
                        base_output);
            binding.input.resize(3);
        }
    } else {
        return std::nullopt;
    }

    // Priority: output ":n" suffix, then "gamepad" field, then input ":n"
    // suffix -- see bindings_config.h's PortedBinding comment.
    if (output_port != 0) {
        binding.port = ClampPort(output_port);
    } else if (const auto gp_it = entry.find("gamepad");
               gp_it != entry.end() && gp_it->is_number_integer()) {
        binding.port = ClampPort(gp_it->get<int>());
    } else if (input_port != 0) {
        binding.port = ClampPort(input_port);
    }

    return BindingsConfig::FlatBinding{base_output, std::move(binding)};
}

} // namespace

bool BindingsConfig::Load(const std::filesystem::path& path) {
    m_path = path;
    m_bindings_cache.clear();
    m_dirty_names.clear();
    m_valid = false;

    if (!std::filesystem::exists(m_path)) {
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

    try {
        m_root = nlohmann::ordered_json::parse(ss.str(), /*cb=*/nullptr, /*allow_exceptions=*/true,
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

std::vector<PortedBinding> BindingsConfig::ParseBindingsFor(
    const nlohmann::ordered_json& bindings_array, const std::string& output_name) {
    std::vector<PortedBinding> result;
    for (const auto& entry : bindings_array) {
        auto parsed = ParseBindingEntry(entry);
        if (parsed && parsed->output == output_name) {
            result.push_back(std::move(parsed->binding));
        }
    }
    return result;
}

const std::vector<PortedBinding>& BindingsConfig::GetBindings(const std::string& output_name) const {
    if (const auto it = m_bindings_cache.find(output_name); it != m_bindings_cache.end()) {
        return it->second;
    }
    auto [it, _] = m_bindings_cache.emplace(
        output_name, m_valid ? ParseBindingsFor(m_root["bindings"], output_name)
                              : std::vector<PortedBinding>{});
    return it->second;
}

void BindingsConfig::SetBindings(const std::string& output_name, std::vector<PortedBinding> bindings) {
    m_bindings_cache[output_name] = std::move(bindings);
    if (std::find(m_dirty_names.begin(), m_dirty_names.end(), output_name) == m_dirty_names.end()) {
        m_dirty_names.push_back(output_name);
    }
}

bool BindingsConfig::IsDirty(const std::string& output_name) const {
    return std::find(m_dirty_names.begin(), m_dirty_names.end(), output_name) !=
           m_dirty_names.end();
}

std::vector<BindingsConfig::FlatBinding> BindingsConfig::GetAllBindings() const {
    std::vector<FlatBinding> result;
    if (!m_valid) {
        return result;
    }

    // Non-dirty outputs: parse straight from the loaded file.
    std::set<std::string> dirty_set(m_dirty_names.begin(), m_dirty_names.end());
    if (m_root.contains("bindings") && m_root["bindings"].is_array()) {
        for (const auto& entry : m_root["bindings"]) {
            auto parsed = ParseBindingEntry(entry);
            if (parsed && !dirty_set.count(parsed->output)) {
                result.push_back(std::move(*parsed));
            }
        }
    }

    // Dirty outputs: reflect this session's edits, not what's on disk.
    for (const auto& name : m_dirty_names) {
        const auto cache_it = m_bindings_cache.find(name);
        if (cache_it == m_bindings_cache.end()) {
            continue;
        }
        for (const auto& binding : cache_it->second) {
            if (!binding.input.empty()) {
                result.push_back(FlatBinding{name, binding});
            }
        }
    }

    return result;
}

bool BindingsConfig::Save() const {
    if (!m_valid) {
        LOG_ERROR(Common_Filesystem, "Refusing to write {}: it was never successfully loaded",
                  m_path.string());
        return false;
    }
    if (m_dirty_names.empty()) {
        return true;
    }

    nlohmann::ordered_json new_bindings = nlohmann::ordered_json::array();

    if (m_root.contains("bindings") && m_root["bindings"].is_array()) {
        for (const auto& entry : m_root["bindings"]) {
            if (!entry.is_object()) {
                continue;
            }
            const auto output_it = entry.find("output");
            const std::string base_output =
                (output_it != entry.end() && output_it->is_string())
                    ? SplitPortSuffix(output_it->get<std::string>()).first
                    : std::string{};
            const bool touched_this_session =
                std::find(m_dirty_names.begin(), m_dirty_names.end(), base_output) !=
                m_dirty_names.end();
            if (!touched_this_session) {
                new_bindings.push_back(entry);
            }
        }
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
            nlohmann::ordered_json entry;
            entry["output"] = name;
            entry["input"] = binding.input.size() == 1
                                  ? nlohmann::ordered_json(binding.input.front())
                                  : nlohmann::ordered_json(binding.input);
            if (binding.port != 0) {
                entry["gamepad"] = binding.port;
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

namespace {

// Whether a and b's port scopes could ever both be "live" for the same
// event: 0 means "every port" (the engine's own per-port copies), so it
// overlaps with anything; two specific ports only overlap with each other.
bool PortsOverlap(int a, int b) {
    return a == 0 || b == 0 || a == b;
}

// The port a collision between a and b actually happens at, for reporting.
int CollisionPort(int a, int b) {
    if (a != 0) {
        return a;
    }
    if (b != 0) {
        return b;
    }
    return 0; // both unqualified -- collides at every port
}

} // namespace

std::vector<BindingConflict> FindConflicts(const std::vector<BindingsConfig::FlatBinding>& all_bindings) {
    std::vector<BindingConflict> conflicts;

    for (size_t i = 0; i < all_bindings.size(); i++) {
        const auto& a = all_bindings[i];
        // "unmapped" is section 6's "deliberately unbound" marker -- by
        // definition it never matches an event, so it can never conflict.
        if (a.binding.input.size() == 1 && a.binding.input.front() == "unmapped") {
            continue;
        }
        const std::set<std::string> a_keys(a.binding.input.begin(), a.binding.input.end());

        for (size_t j = i + 1; j < all_bindings.size(); j++) {
            const auto& b = all_bindings[j];
            if (a.output == b.output) {
                continue; // same output, not "different outputs" -- not this rule
            }
            if (b.binding.input.size() == 1 && b.binding.input.front() == "unmapped") {
                continue;
            }
            // Section 6: same KEY COUNT and same keys. A different count
            // (lctrl+f9 vs f9) is explicitly not a conflict, even though
            // f9's keys are a subset of lctrl+f9's.
            if (a.binding.input.size() != b.binding.input.size()) {
                continue;
            }
            if (!PortsOverlap(a.binding.port, b.binding.port)) {
                continue;
            }
            const std::set<std::string> b_keys(b.binding.input.begin(), b.binding.input.end());
            if (a_keys != b_keys) {
                continue;
            }

            conflicts.push_back(BindingConflict{
                a.output,
                b.output,
                a.binding.input,
                CollisionPort(a.binding.port, b.binding.port),
            });
        }
    }

    return conflicts;
}

} // namespace Core::Input
