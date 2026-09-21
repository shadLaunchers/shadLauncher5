// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/input/input_defaults.h"

#include <fstream>
#include <system_error>

#include "common/logging/log.h"
#include "common/path_util.h"

namespace fs = std::filesystem;

namespace Core::Input {

fs::path UserRoot() {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir);
}

bool WriteText(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        LOG_ERROR(Input, "Could not write {}", path.string());
        return false;
    }
    out << text;
    if (!out.good()) {
        LOG_ERROR(Input, "Failed while writing {}", path.string());
        return false;
    }
    return true;
}

std::string DefaultBindingsJson() {
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
}
)";
}

std::string DefaultGlobalJson() {
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

bool EnsureBindingsFiles() {
    std::error_code ec;
    const auto config_dir = UserRoot() / Common::FS::CUSTOM_INPUT_CONFIGS;
    fs::create_directories(config_dir, ec);
    if (ec) {
        LOG_ERROR(Input, "Could not create {}: {}", config_dir.string(), ec.message());
    }

    bool ok = true;

    const auto default_file = UserRoot() / "default.json";
    if (!fs::exists(default_file, ec) || ec) {
        if (WriteText(default_file, DefaultBindingsJson())) {
            LOG_INFO(Input, "Wrote the default input bindings to {}", default_file.string());
        } else {
            ok = false;
        }
    }

    const auto global_file = UserRoot() / "global.json";
    if (!fs::exists(global_file, ec) || ec) {
        if (WriteText(global_file, DefaultGlobalJson())) {
            LOG_INFO(Input, "Wrote an empty all-games bindings file to {}", global_file.string());
        } else {
            ok = false;
        }
    }

    return ok;
}

} // namespace Core::Input
