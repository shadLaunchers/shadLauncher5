// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Core::Input {

struct PortedBinding {
    std::vector<std::string> input; // 1-3 input names, held together

    int output_port = 0;   // "output": "cross:2"
    int input_port = 0;    // "input": "cross:2"
    int gamepad_field = 0; // "gamepad": 2

    // Whose pad this binding drives
    [[nodiscard]] int OutputPlayer() const {
        if (output_port != 0) {
            return output_port;
        }
        return InputDevice() == 0 ? 0 : 1;
    }

    // Which player's device may press it, 0 for any
    [[nodiscard]] int InputDevice() const {
        return gamepad_field != 0 ? gamepad_field : input_port;
    }
};

struct MouseSettings {
    std::string to_joystick = "right"; // "right" | "left" | "none"
    double deadzone_offset = 0.5;
    double speed = 1.0;
    double speed_offset = 0.125;
};

struct DeadzoneRange {
    int min = 1;
    int max = 127;
};

struct DeadzoneSettings {
    DeadzoneRange left_stick;
    DeadzoneRange right_stick;
    DeadzoneRange left_trigger;
    DeadzoneRange right_trigger;
};

class BindingsConfig {
public:
    // Loads path.
    bool Load(const std::filesystem::path& path);

    [[nodiscard]] const std::vector<PortedBinding>& GetBindings(
        const std::string& output_name) const;

    // Replaces every way to drive output_name with the given list, scoped to
    // port (0 = every port, matching PortedBinding::port). Marks output_name
    // dirty so Save() rewrites its entries.
    void SetBindings(const std::string& output_name, std::vector<PortedBinding> bindings);

    [[nodiscard]] bool IsDirty(const std::string& output_name) const;

    // Anything at all edited this session and not yet written -- bindings,
    // the mouse block or the dead zones. What Save() would actually act on.
    [[nodiscard]] bool HasUnsavedChanges() const {
        return !m_dirty_names.empty() || m_mouse_dirty || m_deadzones_dirty;
    }

    [[nodiscard]] MouseSettings GetMouseSettings(bool* present = nullptr) const;
    void SetMouseSettings(const MouseSettings& settings);

    [[nodiscard]] DeadzoneSettings GetDeadzoneSettings(bool* present = nullptr) const;
    void SetDeadzoneSettings(const DeadzoneSettings& settings);

    bool Save() const;

    [[nodiscard]] const std::filesystem::path& FilePath() const {
        return m_path;
    }
    [[nodiscard]] bool IsLoaded() const {
        return m_valid;
    }

    struct FlatBinding {
        std::string output;
        PortedBinding binding;
    };

    // Every (output, binding) pair as the session currently stands
    [[nodiscard]] std::vector<FlatBinding> GetAllBindings() const;
    [[nodiscard]] std::vector<std::string> Validate() const;

private:
    std::filesystem::path m_path;
    std::string m_raw_text;
    nlohmann::ordered_json m_root;
    bool m_valid = false;
    std::vector<std::string> m_dirty_names;
    mutable std::map<std::string, std::vector<PortedBinding>> m_bindings_cache;

    bool m_mouse_dirty = false;
    MouseSettings m_mouse_edit;
    bool m_deadzones_dirty = false;
    DeadzoneSettings m_deadzones_edit;

    static std::vector<PortedBinding> ParseBindingsFor(const nlohmann::ordered_json& bindings_array,
                                                       const std::string& output_name);
};

struct BindingConflict {
    std::string output_a;
    std::string output_b;
    std::vector<std::string> keys; // the exact chord both share
    int port = 0;
};

[[nodiscard]] std::vector<BindingConflict> FindConflicts(
    const std::vector<BindingsConfig::FlatBinding>& all_bindings);

} // namespace Core::Input
