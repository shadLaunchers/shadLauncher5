// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Editor for <user dir>/hotkeys.json (docs/input-bindings.md). Deliberately
// scoped to just the ten known hotkeys -- see hotkeys_config.h for why this
// is a separate, narrower thing from the full per-game/global binding
// editor.
//
// Not wired into any menu yet: see the note where MainWindow would normally
// gain the menu action.

#pragma once

#include <QDialog>
#include <QListWidget>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>
#include <memory>

#include "core/input/hotkeys_config.h"

class QLabel;

// Which side of the keyboard a modifier is on. Qt's portable API reports
// Key_Shift for both shifts, so the only way to tell them apart is the
// platform scancode -- these are shadLauncher4's constants, verbatim from
// its src/qt_ui/kbm_gui.h. Without them every right-hand modifier was
// recorded as the left one, silently: the binding you saved was not the key
// you pressed, and the emulator treats the two as different keys.
#ifdef _WIN32
#define LCTRL_KEY 29
#define LALT_KEY 56
#define LSHIFT_KEY 42
#else
#define LCTRL_KEY 37
#define LALT_KEY 64
#define LSHIFT_KEY 50
#endif

// Shown when the person clicks "Add a way..." for a hotkey. Captures up to
// three keys/mouse buttons/pad buttons held together and reports them as an
// ordered list of input-vocabulary names (docs/input-bindings.md section 7).
//
// Pad events arrive through SdlEventWrapper, the one SDL reader in the
// process (sdl_event_wrapper.h), rather than this dialog polling SDL itself.
// That is not tidiness: the bindings editor watches the selected pad live
// while this dialog is open on top of it, and two readers on one event queue
// take turns stealing each other's events.
//
// The pad it captures from is the one GamepadSelect has selected
// (src/common/input.h), the same choice shadLauncher4's ActiveGamepadBox
// makes -- not simply the first one plugged in.
class KeyCaptureDialog : public QDialog {
    Q_OBJECT
public:
    // What this capture will accept. A port whose device is pinned in the user
    // manager can only ever be pressed by that device, so offering to capture
    // anything else produces a binding that cannot fire -- the editor would be
    // writing a line that does nothing and saying nothing about it.
    //
    // Any is for callers with no port in hand (the hotkeys editor, which is
    // not per-port at all) and for a port with nothing pinned, where pads fill
    // the port in plug order and either kind may end up driving it.
    enum class Accepts {
        Any,
        KeyboardAndMouse,
        Gamepad,
    };

    explicit KeyCaptureDialog(QWidget* parent = nullptr, Accepts accepts = Accepts::Any,
                              const QString& reason = {});
    ~KeyCaptureDialog() override;

    // Valid only if the dialog was accepted.
    [[nodiscard]] const std::vector<std::string>& CapturedInput() const {
        return m_captured;
    }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(class QWheelEvent* event) override;

private:
    void UpdatePreview();
    void OpenSelectedGamepad();
    void OnSdlEvent(int type, int input, int value);
    void TryCapture(const std::string& name);
    // True if `name` is a kind this capture accepts. Both the keyboard path
    // and the SDL path funnel through TryCapture, so this is the only place
    // the rule has to live.
    [[nodiscard]] bool Allows(const std::string& name) const;
    // Returns the input-vocabulary name for a key press, or empty if this
    // key isn't one the emulator's input vocabulary can name.
    static std::string NameForKeyEvent(QKeyEvent* event);
    // The pad name tables live in gamepad_selector.h so this dialog and the
    // live view cannot disagree about what a control is called.

    QLabel* m_preview_label = nullptr;
    QLabel* m_gamepad_label = nullptr;
    QLabel* m_rule_label = nullptr; // what this capture takes, and why
    Accepts m_accepts = Accepts::Any;
    QString m_reason;              // the port/device sentence, for the label
    std::vector<std::string> m_captured;

    SDL_Gamepad* m_gamepad = nullptr;
    bool m_l2_pressed = false;
    bool m_r2_pressed = false;
    // A stick has to return to centre before it can be captured again,
    // otherwise one push writes the same axis three times over.
    bool m_axis_captured = false;
};

class HotkeysEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit HotkeysEditorDialog(QWidget* parent = nullptr);

private slots:
    void OnAddWay();
    void OnRemoveSelected();
    void OnSetUnmapped();
    void OnResetToDefaults();
    void OnSave();

private:
    void PopulateHotkeyList();
    void RefreshBindingsList();
    void RefreshProblemsList();
    // The hotkey name currently selected in the left-hand list, or empty.
    [[nodiscard]] std::string CurrentHotkeyName() const;
    [[nodiscard]] static QString DisplayChord(const std::vector<std::string>& input);

    QListWidget* m_hotkey_list = nullptr;
    QListWidget* m_bindings_list = nullptr;
    QLabel* m_default_label = nullptr;
    QListWidget* m_problems_list = nullptr;

    std::unique_ptr<Core::Input::HotkeysConfig> m_config;
};
