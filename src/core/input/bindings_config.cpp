// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

#include "bindings_config.h"
#include "common/logging/log.h"
#include "input_ids.h"
#include "text_preserving_json.h"

namespace Core::Input {

namespace {

// Splits "cross:2" into ("cross", 2) and "cross" into ("cross", nothing). A
// ":n" that isn't a number is ignored per input-bindings.md section 9 ("':n'
// suffix is not a number -> ignores the suffix, warns").
//
// The result is optional rather than 0-for-absent because the emulator's own
// SplitGamepadId is, and section 5 is explicit that the two differ: "`0` is
// below the range, not 'unspecified': it becomes port 1." Reading "cross:0" as
// "named no port" would have shown it as every player's, when the emulator
// gives it to player 1 alone.
std::pair<std::string, std::optional<int>> SplitPortSuffix(const std::string& value) {
    const auto colon = value.rfind(':');
    if (colon == std::string::npos) {
        return {value, std::nullopt};
    }
    // The name is stripped at the colon either way, which is what the
    // emulator's SplitGamepadId does: a suffix it can't read is dropped and
    // the binding kept, not turned into an output called "l1:two" that no
    // table knows.
    std::string name = value.substr(0, colon);
    const std::string suffix = value.substr(colon + 1);
    if (suffix.empty() ||
        !std::all_of(suffix.begin(), suffix.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return {std::move(name), std::nullopt};
    }
    try {
        return {std::move(name), std::stoi(suffix)};
    } catch (const std::exception&) {
        return {std::move(name), std::nullopt}; // more digits than an int holds
    }
}

// Section 5/9: a named port is clamped into 1-4, and 0 becomes 1.
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
    std::optional<int> input_port;
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

    // All three are read, none overwrites another -- the emulator reads them
    // the same way, and a rewrite has to be able to put back exactly what it
    // found. Which one wins where is PortedBinding's OutputPlayer() and
    // InputDevice().
    if (output_port) {
        binding.output_port = ClampPort(*output_port);
    }
    if (input_port) {
        binding.input_port = ClampPort(*input_port);
    }
    if (const auto gp_it = entry.find("gamepad");
        gp_it != entry.end() && gp_it->is_number_integer()) {
        binding.gamepad_field = ClampPort(gp_it->get<int>());
    }

    return BindingsConfig::FlatBinding{base_output, std::move(binding)};
}

// One binding's entry text, plain-formatted (no comments -- this is only
// used for entries an edit is actively adding/replacing this session).
std::string FormatBindingEntry(const std::string& output, const PortedBinding& binding) {
    // Each of the three port fields goes back where it came from. A field the
    // file did not have stays absent rather than being invented from one of
    // the others.
    const auto suffixed = [](const std::string& name, int port) {
        return port == 0 ? name : name + ":" + std::to_string(port);
    };

    nlohmann::ordered_json entry;
    entry["output"] = suffixed(output, binding.output_port);

    if (binding.input.size() == 1) {
        entry["input"] = suffixed(binding.input.front(), binding.input_port);
    } else {
        entry["input"] = binding.input;
    }

    if (binding.gamepad_field != 0) {
        entry["gamepad"] = binding.gamepad_field;
    }
    return entry.dump();
}

constexpr const char* kFreshFileTemplate =
    "{\n    \"version\": 1,\n\n    \"bindings\": [\n    ]\n}\n";

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

bool BindingsConfig::Load(const std::filesystem::path& path) {
    m_path = path;
    m_bindings_cache.clear();
    m_dirty_names.clear();
    m_mouse_dirty = false;
    m_deadzones_dirty = false;
    m_valid = false;

    if (!std::filesystem::exists(m_path)) {
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
        m_root = nlohmann::ordered_json::parse(m_raw_text, /*cb=*/nullptr, /*allow_exceptions=*/true,
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

MouseSettings BindingsConfig::GetMouseSettings(bool* present) const {
    if (m_mouse_dirty) {
        if (present) *present = true;
        return m_mouse_edit;
    }
    MouseSettings s;
    if (present) *present = false;
    if (!m_valid || !m_root.contains("mouse") || !m_root["mouse"].is_object()) {
        return s;
    }
    if (present) *present = true;
    const auto& m = m_root["mouse"];
    if (const auto it = m.find("to_joystick"); it != m.end() && it->is_string()) {
        const std::string v = it->get<std::string>();
        // section 8: an unrecognized value keeps the default and warns --
        // it does not fall through to "none".
        if (v == "right" || v == "left" || v == "none") {
            s.to_joystick = v;
        } else {
            LOG_WARNING(Common_Filesystem, "mouse.to_joystick '{}' is not recognized, keeping "
                                           "default",
                       v);
        }
    }
    if (const auto it = m.find("deadzone_offset"); it != m.end() && it->is_number()) {
        s.deadzone_offset = it->get<double>();
    }
    if (const auto it = m.find("speed"); it != m.end() && it->is_number()) {
        s.speed = it->get<double>();
    }
    if (const auto it = m.find("speed_offset"); it != m.end() && it->is_number()) {
        s.speed_offset = it->get<double>();
    }
    return s;
}

void BindingsConfig::SetMouseSettings(const MouseSettings& settings) {
    m_mouse_edit = settings;
    m_mouse_dirty = true;
}

namespace {
DeadzoneRange ParseDeadzoneRange(const nlohmann::ordered_json& obj, const DeadzoneRange& fallback) {
    DeadzoneRange r = fallback;
    if (!obj.is_object()) {
        return r;
    }
    if (const auto it = obj.find("min"); it != obj.end() && it->is_number_integer()) {
        r.min = it->get<int>();
    }
    if (const auto it = obj.find("max"); it != obj.end() && it->is_number_integer()) {
        r.max = it->get<int>();
    }
    return r;
}
} // namespace

DeadzoneSettings BindingsConfig::GetDeadzoneSettings(bool* present) const {
    if (m_deadzones_dirty) {
        if (present) *present = true;
        return m_deadzones_edit;
    }
    DeadzoneSettings s;
    if (present) *present = false;
    if (!m_valid || !m_root.contains("deadzones") || !m_root["deadzones"].is_object()) {
        return s;
    }
    if (present) *present = true;
    const auto& d = m_root["deadzones"];
    if (const auto it = d.find("left_stick"); it != d.end()) {
        s.left_stick = ParseDeadzoneRange(*it, s.left_stick);
    }
    if (const auto it = d.find("right_stick"); it != d.end()) {
        s.right_stick = ParseDeadzoneRange(*it, s.right_stick);
    }
    if (const auto it = d.find("left_trigger"); it != d.end()) {
        s.left_trigger = ParseDeadzoneRange(*it, s.left_trigger);
    }
    if (const auto it = d.find("right_trigger"); it != d.end()) {
        s.right_trigger = ParseDeadzoneRange(*it, s.right_trigger);
    }
    return s;
}

void BindingsConfig::SetDeadzoneSettings(const DeadzoneSettings& settings) {
    m_deadzones_edit = settings;
    m_deadzones_dirty = true;
}

std::vector<BindingsConfig::FlatBinding> BindingsConfig::GetAllBindings() const {
    std::vector<FlatBinding> result;
    if (!m_valid) {
        return result;
    }

    std::set<std::string> dirty_set(m_dirty_names.begin(), m_dirty_names.end());
    if (m_root.contains("bindings") && m_root["bindings"].is_array()) {
        for (const auto& entry : m_root["bindings"]) {
            auto parsed = ParseBindingEntry(entry);
            if (parsed && !dirty_set.count(parsed->output)) {
                result.push_back(std::move(*parsed));
            }
        }
    }

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
    if (m_dirty_names.empty() && !m_mouse_dirty && !m_deadzones_dirty) {
        return true; // nothing to do
    }

    std::string text = m_raw_text;

    if (!m_dirty_names.empty()) {
        const TextJson::RootScan root = TextJson::ScanRoot(text);
        if (!root.ok) {
            LOG_ERROR(Common_Filesystem, "{}: couldn't re-locate its own structure to edit it "
                                         "safely",
                     m_path.string());
            return false;
        }

        // Locate the existing "bindings" array's inner content (between the
        // brackets), if there is one.
        std::string inner;
        bool had_array = false;
        for (const auto& e : root.entries) {
            if (e.key == "bindings") {
                const std::string value_text = text.substr(e.value_start, e.value_end - e.value_start);
                if (value_text.size() >= 2 && value_text.front() == '[' && value_text.back() == ']') {
                    inner = value_text.substr(1, value_text.size() - 2);
                    had_array = true;
                }
                break;
            }
        }
        TextJson::ArrayContent existing = had_array ? TextJson::SplitArray(inner) : TextJson::ArrayContent{};
        if (had_array && !existing.ok) {
            LOG_ERROR(Common_Filesystem, "{}: couldn't parse its own \"bindings\" array structure "
                                         "to edit it safely",
                     m_path.string());
            return false;
        }

        const std::set<std::string> dirty_set(m_dirty_names.begin(), m_dirty_names.end());

        // Keep every existing element whose output isn't one we're touching
        // this session, verbatim (leading trivia -- including any comment --
        // and all).
        std::string rebuilt;
        bool first = true;
        for (const auto& elem : existing.elements) {
            nlohmann::ordered_json parsed_elem;
            std::string base_output;
            try {
                parsed_elem = nlohmann::ordered_json::parse(elem.text, nullptr, true, true);
                if (parsed_elem.is_object() && parsed_elem.contains("output") &&
                    parsed_elem["output"].is_string()) {
                    base_output = SplitPortSuffix(parsed_elem["output"].get<std::string>()).first;
                }
            } catch (const nlohmann::json::exception&) {
                // Unparseable element -- can't identify it, so it's never
                // something we'd be replacing; keep it verbatim.
            }
            if (!base_output.empty() && dirty_set.count(base_output)) {
                continue; // dropped -- replaced by fresh entries below
            }
            // The separator is ours to write: SplitArray hands back only the
            // trivia before each element, never the comma. It goes before
            // the leading trivia so a comment sitting above an element stays
            // above it.
            if (!first) {
                rebuilt += ",";
            }
            rebuilt += elem.leading;
            rebuilt += elem.text;
            first = false;
        }

        // Append fresh entries for every output actually edited this session.
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
                rebuilt += FormatBindingEntry(name, binding);
            }
        }
        // A note written after the last entry is trivia belonging to nobody,
        // so nothing above would have carried it -- put it back rather than
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
        text = TextJson::ReplaceOrInsertTopLevelValue(text, "bindings", new_array_text);
    }

    if (m_mouse_dirty) {
        nlohmann::ordered_json m;
        m["to_joystick"] = m_mouse_edit.to_joystick;
        m["deadzone_offset"] = m_mouse_edit.deadzone_offset;
        m["speed"] = m_mouse_edit.speed;
        m["speed_offset"] = m_mouse_edit.speed_offset;
        text = TextJson::ReplaceOrInsertTopLevelValue(text, "mouse", m.dump(4));
    }

    if (m_deadzones_dirty) {
        auto range_json = [](const DeadzoneRange& r) {
            nlohmann::ordered_json j;
            j["min"] = r.min;
            j["max"] = r.max;
            return j;
        };
        nlohmann::ordered_json d;
        d["left_stick"] = range_json(m_deadzones_edit.left_stick);
        d["right_stick"] = range_json(m_deadzones_edit.right_stick);
        d["left_trigger"] = range_json(m_deadzones_edit.left_trigger);
        d["right_trigger"] = range_json(m_deadzones_edit.right_trigger);
        text = TextJson::ReplaceOrInsertTopLevelValue(text, "deadzones", d.dump(4));
    }

    std::error_code ec;
    std::filesystem::create_directories(m_path.parent_path(), ec);

    std::ofstream file(m_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        LOG_ERROR(Common_Filesystem, "Failed to write {}", m_path.string());
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

namespace {

bool PortsOverlap(int a, int b) {
    return a == 0 || b == 0 || a == b;
}

// Two bindings can only fire together if they reach the same player AND some
// one device can press them both. "cross:2" and a "gamepad": 3 binding of
// circle share no device, so the same chord on both is not a conflict.
bool CanFireTogether(const PortedBinding& a, const PortedBinding& b) {
    return PortsOverlap(a.OutputPlayer(), b.OutputPlayer()) &&
           PortsOverlap(a.InputDevice(), b.InputDevice());
}

int CollisionPort(int a, int b) {
    if (a != 0) return a;
    if (b != 0) return b;
    return 0;
}

} // namespace

std::vector<BindingConflict> FindConflicts(const std::vector<BindingsConfig::FlatBinding>& all_bindings) {
    std::vector<BindingConflict> conflicts;

    for (size_t i = 0; i < all_bindings.size(); i++) {
        const auto& a = all_bindings[i];
        if (a.binding.input.size() == 1 && a.binding.input.front() == "unmapped") {
            continue;
        }
        const std::set<std::string> a_keys(a.binding.input.begin(), a.binding.input.end());

        for (size_t j = i + 1; j < all_bindings.size(); j++) {
            const auto& b = all_bindings[j];
            if (a.output == b.output) {
                continue;
            }
            if (b.binding.input.size() == 1 && b.binding.input.front() == "unmapped") {
                continue;
            }
            if (a.binding.input.size() != b.binding.input.size()) {
                continue;
            }
            if (!CanFireTogether(a.binding, b.binding)) {
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
                CollisionPort(a.binding.OutputPlayer(), b.binding.OutputPlayer()),
            });
        }
    }

    return conflicts;
}

std::vector<std::string> BindingsConfig::Validate() const {
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
        const std::string raw_output = output_it->get<std::string>();
        const auto [base_output, output_port] = SplitPortSuffix(raw_output);
        if (!output_port && raw_output.rfind(':') != std::string::npos) {
            warnings.push_back(tag + " (\"" + raw_output +
                              "\"): the \":n\" port suffix isn't a number, ignored.");
        }
        if (!IsKnownPadControlOutput(base_output)) {
            warnings.push_back(tag + ": output \"" + base_output +
                              "\" isn't a pad control this build recognizes.");
        }

        const auto input_it = entry.find("input");
        if (input_it == entry.end()) {
            warnings.push_back(tag + " (\"" + base_output + "\") has no \"input\", ignored.");
            continue;
        }
        std::vector<std::string> names;
        if (input_it->is_string()) {
            names.push_back(SplitPortSuffix(input_it->get<std::string>()).first);
        } else if (input_it->is_array()) {
            for (const auto& v : *input_it) {
                if (v.is_string()) {
                    names.push_back(v.get<std::string>());
                }
            }
            if (names.empty()) {
                warnings.push_back(tag + " (\"" + base_output +
                                  "\") has an empty or unusable \"input\" array, ignored.");
                continue;
            }
            if (names.size() > 3) {
                warnings.push_back(tag + " (\"" + base_output + "\") has more than 3 keys; "
                                                                  "only the first 3 are kept.");
            }
        } else {
            warnings.push_back(tag + " (\"" + base_output +
                              "\") has an \"input\" that's neither a string nor an array, "
                              "ignored.");
            continue;
        }
        for (const auto& name : names) {
            if (name != "unmapped" && !IsKnownInput(name)) {
                warnings.push_back(tag + " (\"" + base_output + "\"): input \"" + name +
                                  "\" isn't a name this build recognizes.");
            }
        }

        if (const auto gp_it = entry.find("gamepad"); gp_it != entry.end()) {
            if (!gp_it->is_number_integer()) {
                warnings.push_back(tag + " (\"" + base_output +
                                  "\"): \"gamepad\" isn't an integer, ignored.");
            } else {
                const int gp = gp_it->get<int>();
                if (gp < 1 || gp > 4) {
                    warnings.push_back(tag + " (\"" + base_output + "\"): \"gamepad\" " +
                                      std::to_string(gp) + " is outside 1-4, clamped to " +
                                      std::to_string(std::clamp(gp, 1, 4)) + ".");
                }
            }
        }
    }

    return warnings;
}

} // namespace Core::Input
