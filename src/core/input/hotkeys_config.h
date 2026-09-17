// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reads and writes <user dir>/hotkeys.json (docs/input-bindings.md, mainly
// sections 3 and 7). Deliberately narrow: this only edits hotkeys.json. It
// never touches default.json, global.json, or input_config/<title id>.json --
// those belong to the full per-game/global binding editor, not this one.
//
// Design choices that follow directly from the spec:
//  - A hotkey binding is a LIST of ways to press it, not a single chord
//    (section 6: modelling it as one-binding-per-output "will silently
//    delete" any second way somebody already had bound).
//  - Only hotkey names the person actually edits in a session are rewritten;
//    everything else -- entries for names not in kKnownHotkeys, and known
//    entries never touched this session -- is carried through untouched
//    (section 7: "preserve unknown entries... rather than dropping them").
//  - "Reset to defaults" deletes the file outright (section 3: the emulator
//    only ever appends a hotkey it doesn't mention, so it can't be asked to
//    restore one that was changed).

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
    // Loads hotkeys.json from the user directory. A missing or invalid file
    // is not an error -- every known hotkey simply has no bindings loaded,
    // and the editor should show its documented default as unset.
    bool Load();

    // Every way this session currently has bound to press hotkey_name --
    // either what was loaded from disk, or what SetBindings() last set, in
    // this process. Empty if the file had no entries for it.
    [[nodiscard]] const std::vector<HotkeyBinding>& GetBindings(
        const std::string& hotkey_name) const;

    // Replaces every way to press hotkey_name with exactly the given list.
    // hotkey_name must be one of kKnownHotkeys; this marks it dirty so
    // Save() rewrites its entries.
    void SetBindings(const std::string& hotkey_name, std::vector<HotkeyBinding> bindings);

    // True if hotkey_name was edited via SetBindings() this session.
    [[nodiscard]] bool IsDirty(const std::string& hotkey_name) const;

    // Writes hotkeys.json by editing its ORIGINAL TEXT in place (section 3:
    // a full JSON re-serialize destroys every comment). Only the specific
    // hotkey entries touched this session are replaced; every other byte
    // of the file, comments included, survives untouched.
    bool Save() const;

    // Deletes hotkeys.json so the emulator regenerates all ten defaults on
    // its next launch. Returns true if the file didn't exist or was removed.
    bool ResetToDefaults();

    [[nodiscard]] const std::filesystem::path& FilePath() const {
        return m_path;
    }

private:
    std::filesystem::path m_path;
    std::string m_raw_text; // the file's original text, or a fresh template if it didn't exist
    nlohmann::ordered_json m_root; // parsed from m_raw_text; used for reads only, never for Save()
    bool m_valid = false;
    std::vector<std::string> m_dirty_names;
    mutable std::map<std::string, std::vector<HotkeyBinding>> m_bindings_cache;

    static std::vector<HotkeyBinding> ParseBindingsFor(const nlohmann::ordered_json& bindings_array,
                                                        const std::string& hotkey_name);
};

} // namespace Core::Input
