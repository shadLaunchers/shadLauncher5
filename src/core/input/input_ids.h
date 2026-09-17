// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The subset of the emulator's input vocabulary (docs/input-bindings.md,
// section 7) that a hotkey binding can use: the ten known hotkey names with
// their documented defaults, and the keyboard/mouse input names a hotkey can
// bind to. This intentionally does not include the pad-control output
// vocabulary (triangle, axis_left_x_plus, ...) -- that belongs to the
// per-game/global binding editor, not this one.

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

} // namespace Core::Input
