// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>

namespace Core::Input {

struct HotkeyDef {
    std::string_view name;
    std::string_view default_display;
};

inline constexpr std::array<HotkeyDef, 9> kKnownHotkeys{{
    {"hotkey_fullscreen", "f11"},
    {"hotkey_show_fps", "f10"},
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

inline constexpr std::array<std::string_view, 62> kSymbolAndSpecialKeyNames{{
    // Symbols
    "grave",
    "tilde",
    "exclamation",
    "at",
    "hash",
    "dollar",
    "percent",
    "caret",
    "ampersand",
    "asterisk",
    "lparen",
    "rparen",
    "minus",
    "underscore",
    "equals",
    "plus",
    "lbracket",
    "rbracket",
    "lbrace",
    "rbrace",
    "backslash",
    "pipe",
    "semicolon",
    "colon",
    "apostrophe",
    "quote",
    "comma",
    "less",
    "period",
    "greater",
    "slash",
    "question",
    // Special
    "escape",
    "printscreen",
    "scrolllock",
    "pausebreak",
    "backspace",
    "delete",
    "insert",
    "home",
    "end",
    "pgup",
    "pgdown",
    "tab",
    "capslock",
    "enter",
    "space",
    "up",
    "down",
    "left",
    "right",
    "lshift",
    "rshift",
    "lctrl",
    "rctrl",
    "lalt",
    "ralt",
    "lmeta",
    "rmeta",
    "lwin",
    "rwin",
}};

inline constexpr std::array<std::string_view, 7> kKeypadKeyNames{{
    "kpperiod",
    "kpcomma",
    "kpslash",
    "kpasterisk",
    "kpminus",
    "kpplus",
    "kpequals",
}};
inline constexpr std::string_view kKeypadEnter = "kpenter";

inline constexpr std::array<std::string_view, 9> kMouseInputNames{{
    "leftbutton",
    "rightbutton",
    "middlebutton",
    "sidebuttonback",
    "sidebuttonforward",
    "mousewheelup",
    "mousewheeldown",
    "mousewheelleft",
    "mousewheelright",
}};

inline constexpr std::string_view kUnmapped = "unmapped";

[[nodiscard]] inline bool IsKnownKeyboardOrMouseInput(std::string_view name) {
    if (name.size() == 1 &&
        ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9'))) {
        return true;
    }
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f') {
        bool digits_ok = true;
        for (size_t i = 1; i < name.size(); i++) {
            if (name[i] < '0' || name[i] > '9') {
                digits_ok = false;
                break;
            }
        }
        if (digits_ok) {
            const int number = std::atoi(std::string(name.substr(1)).c_str());
            return number >= 1 && number <= 12;
        }
    }
    if (name.size() == 3 && name[0] == 'k' && name[1] == 'p' && name[2] >= '0' && name[2] <= '9') {
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

inline constexpr std::array<std::string_view, 13> kPadButtonNames{{
    "triangle",
    "circle",
    "cross",
    "square",
    "l1",
    "r1",
    "l3",
    "r3",
    "pad_up",
    "pad_down",
    "pad_left",
    "pad_right",
    "options",
}};

inline constexpr std::array<std::string_view, 3> kTouchpadOutputNames{{
    "touchpad_left",
    "touchpad_center",
    "touchpad_right",
}};
// touchpad_center doubles as an input (a real touchpad press).
inline constexpr std::string_view kTouchpadCenterInput = "touchpad_center";

// Digital-edge axis outputs, plus l2/r2 (axes, not buttons the button bits
// are derived from how far the trigger is pulled).
inline constexpr std::array<std::string_view, 10> kAxisEdgeOutputNames{{
    "axis_left_x_plus",
    "axis_left_x_minus",
    "axis_left_y_plus",
    "axis_left_y_minus",
    "axis_right_x_plus",
    "axis_right_x_minus",
    "axis_right_y_plus",
    "axis_right_y_minus",
    "l2",
    "r2",
}};

// Analog-to-analog outputs: only valid when the *input* is also an axis, so
// the stick keeps its range instead of being pushed fully over.
inline constexpr std::array<std::string_view, 4> kAnalogToAnalogOutputNames{{
    "axis_left_x",
    "axis_left_y",
    "axis_right_x",
    "axis_right_y",
}};

inline constexpr std::array<std::string_view, 3> kNotImplementedOutputNames{{
    "leftjoystick_halfmode",
    "rightjoystick_halfmode",
    "mouse_gyro_roll_mode",
}};

inline constexpr std::array<std::string_view, 6> kPadInputOnlyNames{{
    "back",
    "share",
    "qam",
    "lpaddle_high",
    "lpaddle_low",
    "rpaddle_high",
}};
inline constexpr std::array<std::string_view, 3> kPadInputOnlyNamesContinued{{
    "rpaddle_low",
    "l4",
    "l5",
}};
inline constexpr std::array<std::string_view, 2> kPadInputOnlyNamesTail{{
    "r4",
    "r5",
}};

[[nodiscard]] inline bool IsKnownPadControlOutput(std::string_view name) {
    for (const auto& n : kPadButtonNames) {
        if (n == name)
            return true;
    }
    for (const auto& n : kTouchpadOutputNames) {
        if (n == name)
            return true;
    }
    for (const auto& n : kAxisEdgeOutputNames) {
        if (n == name)
            return true;
    }
    for (const auto& n : kAnalogToAnalogOutputNames) {
        if (n == name)
            return true;
    }
    return false;
}

[[nodiscard]] inline bool IsKnownPadInputName(std::string_view name) {
    if (name == kTouchpadCenterInput) {
        return true;
    }
    for (const auto& n : kPadButtonNames) {
        if (n == name)
            return true;
    }
    for (const auto& n : kAxisEdgeOutputNames) {
        if (n == name)
            return true;
    }
    for (const auto& n : kAnalogToAnalogOutputNames) {
        if (n == name)
            return true;
    }
    for (const auto& n : kPadInputOnlyNames) {
        if (n == name)
            return true;
    }
    for (const auto& n : kPadInputOnlyNamesContinued) {
        if (n == name)
            return true;
    }
    for (const auto& n : kPadInputOnlyNamesTail) {
        if (n == name)
            return true;
    }
    return false;
}

// Any input this build's vocabulary knows .keyboard, mouse, or pad.
[[nodiscard]] inline bool IsKnownInput(std::string_view name) {
    return IsKnownKeyboardOrMouseInput(name) || IsKnownPadInputName(name);
}

} // namespace Core::Input
