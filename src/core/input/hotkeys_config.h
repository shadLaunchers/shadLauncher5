// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Core::Input {

struct HotkeyBinding {
    std::vector<std::string> input; // 1-3 input names, held together
    int gamepad = 0;                // 0 = not set (no "gamepad" key written)
};

class HotkeysConfig {
public:
    // Loads hotkeys.json from the user directory.
    bool Load();

    // Every way this session currently has bound to press hotkey_name
    [[nodiscard]] const std::vector<HotkeyBinding>& GetBindings(
        const std::string& hotkey_name) const;

    // Replaces every way to press hotkey_name with exactly the given list.
    void SetBindings(const std::string& hotkey_name, std::vector<HotkeyBinding> bindings);

    // True if hotkey_name was edited via SetBindings() this session.
    [[nodiscard]] bool IsDirty(const std::string& hotkey_name) const;

    [[nodiscard]] bool HasUnsavedChanges() const {
        return !m_dirty_names.empty();
    }
    bool Save();
    bool ResetToDefaults();

    [[nodiscard]] std::vector<std::string> Validate() const;

    [[nodiscard]] const std::filesystem::path& FilePath() const {
        return m_path;
    }

private:
    std::filesystem::path m_path;
    std::string m_raw_text;
    nlohmann::ordered_json m_root;
    bool m_valid = false;
    std::vector<std::string> m_dirty_names;
    mutable std::map<std::string, std::vector<HotkeyBinding>> m_bindings_cache;

    static std::vector<HotkeyBinding> ParseBindingsFor(const nlohmann::ordered_json& bindings_array,
                                                       const std::string& hotkey_name);
};

} // namespace Core::Input
