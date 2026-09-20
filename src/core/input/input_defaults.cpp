// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/input/input_defaults.h"

#include <fstream>
#include <system_error>

#include "common/logging/log.h"
#include "common/path_util.h"

namespace fs = std::filesystem;

namespace Core::Input {

namespace {

// The emulator's UserRoot(): the user directory itself, where the three files
// that are not about one particular game live.
fs::path UserRoot() {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir);
}

bool WriteText(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        LOG_ERROR(Common_Filesystem, "Could not write {}", path.string());
        return false;
    }
    out << text;
    if (!out.good()) {
        LOG_ERROR(Common_Filesystem, "Failed while writing {}", path.string());
        return false;
    }
    return true;
}

} // namespace

// ── copied verbatim from the emulator's input_config.cpp ─────────────
//
// Byte for byte, deliberately. The cross-check test compares the two, so a
// change on that side that is not mirrored here is a test failure rather than
// two emulators that disagree about what a fresh install looks like.

std::string DefaultBindingsJson() {
    // Written once, when the file is missing, and never rewritten -- so these
    // comments survive whatever else happens. That is the whole reason nothing
    // in this file serialises a bindings file back out.
    return R"({
    // Input bindings. Lost? Every name on the left is a PS5 pad control; every
    // name on the right is a key on your keyboard or a control on your pad.
    //
    // The same output can appear as often as you like -- each line is another
    // way to press it. "input" takes one name, or a list of up to three names
    // that must be held together.
    //
    // Comments and trailing commas are allowed. This file is never rewritten by
    // the emulator, so anything you put here stays put.
    "version": 1,

    "bindings": [
        // ── keyboard ────────────────────────────────────────────────
        { "output": "triangle", "input": "kp8" },
        { "output": "circle",   "input": "kp6" },
        { "output": "cross",    "input": "kp2" },
        { "output": "square",   "input": "kp4" },

        // Alternatives for keyboards without a keypad.
        { "output": "triangle", "input": "c" },
        { "output": "circle",   "input": "b" },
        { "output": "cross",    "input": "n" },
        { "output": "square",   "input": "v" },

        { "output": "l1", "input": "q" },
        { "output": "r1", "input": "u" },
        { "output": "l2", "input": "e" },
        { "output": "r2", "input": "o" },
        { "output": "l3", "input": "x" },
        { "output": "r3", "input": "m" },

        { "output": "options",         "input": "enter" },
        { "output": "touchpad_center", "input": "space" },

        { "output": "pad_up",    "input": "up" },
        { "output": "pad_down",  "input": "down" },
        { "output": "pad_left",  "input": "left" },
        { "output": "pad_right", "input": "right" },

        { "output": "axis_left_x_minus", "input": "a" },
        { "output": "axis_left_x_plus",  "input": "d" },
        { "output": "axis_left_y_minus", "input": "w" },
        { "output": "axis_left_y_plus",  "input": "s" },

        { "output": "axis_right_x_minus", "input": "j" },
        { "output": "axis_right_x_plus",  "input": "l" },
        { "output": "axis_right_y_minus", "input": "i" },
        { "output": "axis_right_y_plus",  "input": "k" },

        // ── controller ──────────────────────────────────────────────
        { "output": "triangle", "input": "triangle" },
        { "output": "circle",   "input": "circle" },
        { "output": "cross",    "input": "cross" },
        { "output": "square",   "input": "square" },

        { "output": "l1", "input": "l1" },
        { "output": "l2", "input": "l2" },
        { "output": "l3", "input": "l3" },
        { "output": "r1", "input": "r1" },
        { "output": "r2", "input": "r2" },
        { "output": "r3", "input": "r3" },

        { "output": "options",         "input": "options" },
        { "output": "touchpad_center", "input": "back" },

        // A pad that has a real touchpad, such as a DualSense. The touchpad can
        // only be bound to itself.
        { "output": "touchpad_center", "input": "touchpad_center" },

        { "output": "pad_up",    "input": "pad_up" },
        { "output": "pad_down",  "input": "pad_down" },
        { "output": "pad_left",  "input": "pad_left" },
        { "output": "pad_right", "input": "pad_right" },

        { "output": "axis_left_x",  "input": "axis_left_x" },
        { "output": "axis_left_y",  "input": "axis_left_y" },
        { "output": "axis_right_x", "input": "axis_right_x" },
        { "output": "axis_right_y", "input": "axis_right_y" }
    ],

    // ── settings ────────────────────────────────────────────────
    //
    // Commented out on purpose, and the values shown are exactly what the
    // emulator uses when nobody says otherwise -- so uncommenting one as it
    // stands changes nothing. They are here to be copied and edited.
    //
    // Leaving them commented also keeps global.json useful. A setting in this
    // file beats the same setting in global.json, this file being the more
    // specific of the two; if these blocks were live, a dead zone you put in
    // global.json would be quietly overruled by a file you never touched.
    //
    // "mouse" is the mouse-as-a-stick, switched on and off with
    // hotkey_toggle_mouse_to_joystick. to_joystick is "right", "left" or
    // "none"; deadzone_offset is how far the stick is pushed before the mouse
    // has moved at all, and speed scales the rest.
    //
    // "mouse": {
    //     "to_joystick": "right",
    //     "deadzone_offset": 0.5,
    //     "speed": 1.0,
    //     "speed_offset": 0.125
    // },

    // Analog dead zones, 0..127. Anything up to min reads as centred, and the
    // travel from there to max is stretched back over the whole range.
    //
    // "deadzones": {
    //     "left_stick":    { "min": 1, "max": 127 },
    //     "right_stick":   { "min": 1, "max": 127 },
    //     "left_trigger":  { "min": 1, "max": 127 },
    //     "right_trigger": { "min": 1, "max": 127 }
    // }
}
)";
}

std::string DefaultGlobalJson() {
    // Upstream writes the same thing into global.ini when it is missing, and
    // for the same reason: a file nobody creates is a feature nobody finds.
    // Empty of bindings on purpose -- it adds to whatever the game already has
    // rather than replacing it.
    return R"({
    // Anything here is loaded for every game, on top of that game's own
    // bindings or default.json. A good place for something you always want,
    // whatever you are playing.
    //
    // These add to the other file rather than replacing it: two bindings for
    // one output are two ways to press it, not a conflict.
    //
    // A "mouse" or "deadzones" block works here too, and applies to every game.
    // Those are settings rather than bindings, so they do not add up: a game
    // that sets the same one in its own file wins.
    "version": 1,

    "bindings": [
    ]
}
)";
}

// ── the file-creating half of the emulator's EnsureFiles() ───────────

bool EnsureBindingsFiles() {
    std::error_code ec;

    // Created even when empty, so it is an obvious place to put a per-game
    // file -- and so the editor has somewhere to save one.
    const auto config_dir = UserRoot() / Common::FS::CUSTOM_INPUT_CONFIGS;
    fs::create_directories(config_dir, ec);
    if (ec) {
        LOG_ERROR(Common_Filesystem, "Could not create {}: {}", config_dir.string(), ec.message());
    }

    bool ok = true;

    const auto default_file = UserRoot() / "default.json";
    if (!fs::exists(default_file, ec) || ec) {
        if (WriteText(default_file, DefaultBindingsJson())) {
            LOG_INFO(Common_Filesystem, "Wrote the default input bindings to {}",
                     default_file.string());
        } else {
            ok = false;
        }
    }

    const auto global_file = UserRoot() / "global.json";
    if (!fs::exists(global_file, ec) || ec) {
        if (WriteText(global_file, DefaultGlobalJson())) {
            LOG_INFO(Common_Filesystem, "Wrote an empty all-games bindings file to {}",
                     global_file.string());
        } else {
            ok = false;
        }
    }

    return ok;
}

} // namespace Core::Input
