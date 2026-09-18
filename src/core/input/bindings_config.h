// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reads and writes ONE bindings file at a time -- global.json, or
// input_config/<title id>.json -- following docs/input-bindings.md. This is
// deliberately narrower than the full picture in section 2 (which file wins,
// the game-file-plus-global concatenation the emulator itself does at load
// time): this class edits a single file's own bindings list, and it is the
// caller's job to pick which file that is and never point it at
// default.json (section 3: its comments are the documentation and must not
// be destroyed by a round-trip).
//
// Same list-not-map model as hotkeys_config.h, generalized to arbitrary pad-
// control output names with a port.

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Core::Input {

struct PortedBinding {
    std::vector<std::string> input; // 1-3 input names, held together
    // 0 = binding names no port ("belongs to every port", section 5's
    // default). 1-4 = this binding is scoped to one port, written as a
    // "gamepad" field on save (the mechanism the worked example in section
    // 10 uses for one player's device driving another's output). An
    // output-side ":n" suffix -- the *other* way a file can name a port
    // (section 5) -- is preserved verbatim if present when this binding was
    // loaded, but this editor does not offer setting one: the two fields
    // answer different questions (which device may press it, vs whose
    // output fires) and conflating them in a first pass risks writing
    // something the emulator resolves differently than intended.
    int port = 0;
};

// section 8: applies while mouse-to-joystick is switched on.
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

// section 8: four sticks/triggers, each an independent {min, max}.
struct DeadzoneSettings {
    DeadzoneRange left_stick;
    DeadzoneRange right_stick;
    DeadzoneRange left_trigger;
    DeadzoneRange right_trigger;
};

class BindingsConfig {
public:
    // Loads path. A missing file is not an error -- every output simply has
    // no bindings loaded. An existing-but-unparseable file returns false;
    // the caller should refuse to edit rather than risk an overwrite.
    bool Load(const std::filesystem::path& path);

    [[nodiscard]] const std::vector<PortedBinding>& GetBindings(
        const std::string& output_name) const;

    // Replaces every way to drive output_name with the given list, scoped to
    // port (0 = every port, matching PortedBinding::port). Marks output_name
    // dirty so Save() rewrites its entries.
    void SetBindings(const std::string& output_name, std::vector<PortedBinding> bindings);

    [[nodiscard]] bool IsDirty(const std::string& output_name) const;

    // section 8. present=false if the file has no "mouse" block at all (the
    // returned values are then the built-in defaults, for display only --
    // Save() will not write a block nobody asked to change).
    [[nodiscard]] MouseSettings GetMouseSettings(bool* present = nullptr) const;
    void SetMouseSettings(const MouseSettings& settings);

    [[nodiscard]] DeadzoneSettings GetDeadzoneSettings(bool* present = nullptr) const;
    void SetDeadzoneSettings(const DeadzoneSettings& settings);

    // Writes the file by editing its ORIGINAL TEXT in place (section 3: a
    // full JSON re-serialize destroys every comment). Only the specific
    // pieces actually touched this session -- individual bindings entries,
    // and/or the whole "mouse"/"deadzones" blocks -- are replaced; every
    // other byte of the file, comments included, survives untouched.
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

    // Every (output, binding) pair as the session currently stands --
    // edited outputs from the in-memory cache, everything else parsed fresh
    // from the loaded file. Used for whole-file operations like conflict
    // detection that can't work one output at a time.
    [[nodiscard]] std::vector<FlatBinding> GetAllBindings() const;

    // section 9's validation table, applied to this session's current state
    // (edits included). Every skip/clamp/truncate this class already does
    // silently while parsing is real behavior -- this just also produces a
    // human-readable reason for each one, since nothing else in this class
    // surfaces them to the person editing the file.
    [[nodiscard]] std::vector<std::string> Validate() const;

private:
    std::filesystem::path m_path;
    std::string m_raw_text; // the file's original text, or a fresh template if it didn't exist
    nlohmann::ordered_json m_root; // parsed from m_raw_text; used for reads only, never for Save()
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

// input-bindings.md section 6: "A real conflict is two bindings with the
// same key count on the same keys driving different outputs. Both fire."
// Longer-vs-shorter overlaps (lctrl+f9 vs f9) are explicitly NOT this --
// the longer one wins its keys and the shorter one still works alone, so
// this never reports those pairs.
struct BindingConflict {
    std::string output_a;
    std::string output_b;
    std::vector<std::string> keys; // the exact chord both share
    // The port this collides at, or 0 if it's every port (both bindings
    // name no port, so the engine's per-port copies collide everywhere).
    int port = 0;
};

// O(n^2) over all bindings in the file; fine at the sizes a bindings file
// actually reaches (tens of entries, not thousands).
[[nodiscard]] std::vector<BindingConflict> FindConflicts(
    const std::vector<BindingsConfig::FlatBinding>& all_bindings);

} // namespace Core::Input
