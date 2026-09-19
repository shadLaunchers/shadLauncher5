// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The emulator's input vocabulary (docs/input-bindings.md, section 7), in
// two halves. First: what a hotkey binding can use -- the ten known hotkey
// names with their documented defaults, and the keyboard/mouse input names a
// hotkey can bind to. Then, below the divider: the pad-control vocabulary
// the per-game/global binding editor needs -- the output names (triangle,
// axis_left_x_plus, ...), the pad names that are input-only, and the three
// names the emulator knows but has not implemented.

#pragma once

#include <array>
#include <string_view>

namespace Core::Input {

struct HotkeyDef {
    std::string_view name;
    // Display form of the documented default, e.g. "lctrl+f9". Matches what
    // SplitChord() below would produce from the actual default chord.
    std::string_view default_display;
};

// The ten hotkeys this build knows (docs/input-bindings.md section 7), in
// the order the editor should list them. A name not in this table is an
// unknown hotkey to us -- see input-bindings.md section 7: "A hotkey name
// this build does not know is skipped rather than rejected... An editor
// should... preserve unknown entries it finds rather than dropping them."
inline constexpr std::array<HotkeyDef, 10> kKnownHotkeys{{
    {"hotkey_fullscreen", "f11"},
    {"hotkey_show_fps", "f10"},
    {"hotkey_pause", "f9"},
    {"hotkey_capture_frame", "f1"},
    {"hotkey_toggle_mouse_to_joystick", "f7"},
    {"hotkey_add_virtual_user", "f5"},
    {"hotkey_remove_virtual_user", "f4"},
    {"hotkey_port_config", "f3"},
    {"hotkey_unlock_trophy", "lctrl+f9"},
    {"hotkey_quit", "lctrl+lshift+end"},
}};

[[nodiscard]] inline bool IsKnownHotkey(std::string_view name) {
    for (const auto& h : kKnownHotkeys) {
        if (h.name == name) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline std::string_view DefaultDisplayFor(std::string_view name) {
    for (const auto& h : kKnownHotkeys) {
        if (h.name == name) {
            return h.default_display;
        }
    }
    return {};
}

// Keyboard input names a hotkey chord can be made of (input-bindings.md
// section 7, "Inputs -- keyboard"). Letters, digits, and f1-f12 are handled
// separately since they're regular (a-z, 0-9, f1-f12) rather than listed
// here individually.
inline constexpr std::array<std::string_view, 62> kSymbolAndSpecialKeyNames{{
    // Symbols
    "grave", "tilde", "exclamation", "at", "hash", "dollar", "percent", "caret", "ampersand",
    "asterisk", "lparen", "rparen", "minus", "underscore", "equals", "plus", "lbracket",
    "rbracket", "lbrace", "rbrace", "backslash", "pipe", "semicolon", "colon", "apostrophe",
    "quote", "comma", "less", "period", "greater", "slash", "question",
    // Special
    "escape", "printscreen", "scrolllock", "pausebreak", "backspace", "delete", "insert", "home",
    "end", "pgup", "pgdown", "tab", "capslock", "enter", "space", "up", "down", "left", "right",
    "lshift", "rshift", "lctrl", "rctrl", "lalt", "ralt", "lmeta", "rmeta", "lwin", "rwin",
}};

// Keypad input names (input-bindings.md section 7). kp0-kp9 are regular.
inline constexpr std::array<std::string_view, 7> kKeypadKeyNames{{
    "kpperiod", "kpcomma", "kpslash", "kpasterisk", "kpminus", "kpplus", "kpequals",
}};
inline constexpr std::string_view kKeypadEnter = "kpenter";

// Mouse input names (input-bindings.md section 7, "Inputs -- mouse").
inline constexpr std::array<std::string_view, 9> kMouseInputNames{{
    "leftbutton", "rightbutton", "middlebutton", "sidebuttonback", "sidebuttonforward",
    "mousewheelup", "mousewheeldown", "mousewheelleft", "mousewheelright",
}};

// "This control is deliberately unbound" (input-bindings.md section 6).
inline constexpr std::string_view kUnmapped = "unmapped";

[[nodiscard]] inline bool IsKnownKeyboardOrMouseInput(std::string_view name) {
    if (name.size() == 1 && ((name[0] >= 'a' && name[0] <= 'z') ||
                              (name[0] >= '0' && name[0] <= '9'))) {
        return true;
    }
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f') {
        // f1-f12
        bool digits_ok = true;
        for (size_t i = 1; i < name.size(); i++) {
            if (name[i] < '0' || name[i] > '9') {
                digits_ok = false;
                break;
            }
        }
        if (digits_ok) {
            return true;
        }
    }
    if (name.size() == 3 && name[0] == 'k' && name[1] == 'p' && name[2] >= '0' &&
        name[2] <= '9') {
        return true;
    }
    if (name == kKeypadEnter || name == kUnmapped) {
        return true;
    }
    for (const auto& n : kSymbolAndSpecialKeyNames) {
        if (n == name) {
            return true;
        }
    }
    for (const auto& n : kKeypadKeyNames) {
        if (n == name) {
            return true;
        }
    }
    for (const auto& n : kMouseInputNames) {
        if (n == name) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------
// Pad controls (input-bindings.md section 7, "Outputs -- pad controls" and
// "Inputs -- pad")
// ---------------------------------------------------------------------

// Button-shaped outputs. Also valid on the input side (section 7: "Everything
// in the output button list above works as an input").
inline constexpr std::array<std::string_view, 13> kPadButtonNames{{
    "triangle", "circle", "cross", "square", "l1", "r1", "l3", "r3", "pad_up",
    "pad_down", "pad_left", "pad_right", "options",
}};

// Output only -- all three press the one touchpad the guest sees; they never
// arrive as an input (section 7 explicitly: touchpad_left/right "will never
// arrive as inputs").
inline constexpr std::array<std::string_view, 3> kTouchpadOutputNames{{
    "touchpad_left", "touchpad_center", "touchpad_right",
}};
// touchpad_center doubles as an input (a real touchpad press).
inline constexpr std::string_view kTouchpadCenterInput = "touchpad_center";

// Digital-edge axis outputs, plus l2/r2 (axes, not buttons -- the button bits
// are derived from how far the trigger is pulled).
inline constexpr std::array<std::string_view, 10> kAxisEdgeOutputNames{{
    "axis_left_x_plus", "axis_left_x_minus", "axis_left_y_plus", "axis_left_y_minus",
    "axis_right_x_plus", "axis_right_x_minus", "axis_right_y_plus", "axis_right_y_minus",
    "l2", "r2",
}};

// Analog-to-analog outputs: only valid when the *input* is also an axis, so
// the stick keeps its range instead of being pushed fully over.
inline constexpr std::array<std::string_view, 4> kAnalogToAnalogOutputNames{{
    "axis_left_x", "axis_left_y", "axis_right_x", "axis_right_y",
}};

// Named in the emulator's table but not implemented -- binding one logs an
// error and does nothing (section 7). Never offer these in the editor.
inline constexpr std::array<std::string_view, 3> kNotImplementedOutputNames{{
    "leftjoystick_halfmode", "rightjoystick_halfmode", "mouse_gyro_roll_mode",
}};

// Input-only pad names: not valid as an output.
inline constexpr std::array<std::string_view, 6> kPadInputOnlyNames{{
    "back", "share", "qam", "lpaddle_high", "lpaddle_low", "rpaddle_high",
}};
inline constexpr std::array<std::string_view, 3> kPadInputOnlyNamesContinued{{
    "rpaddle_low", "l4", "l5",
}};
inline constexpr std::array<std::string_view, 2> kPadInputOnlyNamesTail{{
    "r4", "r5",
}};

[[nodiscard]] inline bool IsKnownPadControlOutput(std::string_view name) {
    for (const auto& n : kPadButtonNames) {
        if (n == name) return true;
    }
    for (const auto& n : kTouchpadOutputNames) {
        if (n == name) return true;
    }
    for (const auto& n : kAxisEdgeOutputNames) {
        if (n == name) return true;
    }
    for (const auto& n : kAnalogToAnalogOutputNames) {
        if (n == name) return true;
    }
    return false;
}

[[nodiscard]] inline bool IsKnownPadInputName(std::string_view name) {
    if (name == kTouchpadCenterInput) {
        return true;
    }
    for (const auto& n : kPadButtonNames) {
        if (n == name) return true;
    }
    for (const auto& n : kAxisEdgeOutputNames) {
        if (n == name) return true;
    }
    for (const auto& n : kAnalogToAnalogOutputNames) {
        if (n == name) return true;
    }
    for (const auto& n : kPadInputOnlyNames) {
        if (n == name) return true;
    }
    for (const auto& n : kPadInputOnlyNamesContinued) {
        if (n == name) return true;
    }
    for (const auto& n : kPadInputOnlyNamesTail) {
        if (n == name) return true;
    }
    return false;
}

// Any input this build's vocabulary knows -- keyboard, mouse, or pad.
[[nodiscard]] inline bool IsKnownInput(std::string_view name) {
    return IsKnownKeyboardOrMouseInput(name) || IsKnownPadInputName(name);
}

} // namespace Core::Input
