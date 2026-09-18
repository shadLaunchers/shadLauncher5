// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The full pad-control binding editor (docs/input-bindings.md), as opposed
// to hotkeys_editor_dialog.h which only handles hotkeys.json. One QTabWidget
// page per port (1-4); a port's page is only editable once its "this port
// is assigned" checkbox is on.
//
// FIRST PASS -- scope deliberately narrower than the full spec for now:
//  - A port's bindings are the ones scoped to it via the "gamepad" field
//    (bindings_config.h's PortedBinding); the *other* way a file can name a
//    port -- an ":n" suffix on "output" itself -- is read and preserved if
//    already present, but this editor doesn't offer writing one. See
//    PortedBinding's comment for why.
//  - "Port assigned" is this editor's own concept, not read from the
//    emulator: docs/multi-user.md (which this editor hasn't been given)
//    covers how a *device* actually gets a port at runtime. Here it just
//    means "show and let me edit this port's bindings" -- unchecking it
//    does not delete anything already saved for that port, it only hides
//    the tab's controls.
//
// File picker (section 2): the person can switch between global.json and a
// specific game's custom_input_configs/<serial>.json. When editing a game's
// file, global.json is also loaded read-only as an overlay -- its bindings
// are shown alongside the game file's own (clearly marked, not editable
// here) and folded into conflict detection, since that's what the emulator
// actually sees at runtime (both files' bindings are concatenated). Editing
// global.json itself has no overlay: it *is* one of the two layers.

#pragma once

#include <QDialog>
#include <QListWidget>
#include <array>
#include <filesystem>
#include <memory>

#include "core/input/bindings_config.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;
class GamepadDiagramWidget;

// One port's page: an output list on the left, that output's ways-to-press
// on the right, gated by an "assigned" checkbox. Mirrors
// HotkeysEditorDialog's layout in hotkeys_editor_dialog.h.
class PortBindingsPage : public QWidget {
    Q_OBJECT
public:
    PortBindingsPage(int port_number, Core::Input::BindingsConfig* config, QWidget* parent = nullptr);

    [[nodiscard]] int PortNumber() const {
        return m_port_number;
    }
    [[nodiscard]] bool IsAssigned() const;

    // Read-only global.json bindings to show alongside this page's own,
    // when this page belongs to a dialog editing a specific game's file.
    // Pass nullptr to clear it (editing global.json itself has no overlay).
    void SetGlobalOverlay(Core::Input::BindingsConfig* overlay);

    // Call after m_config has been reloaded in place (a new target path)
    // to re-render with the fresh data.
    void Reload();

signals:
    // Emitted after a way-to-press is added or removed, so the dialog can
    // refresh conflict detection (which needs every port's bindings, not
    // just this page's).
    void BindingsChanged();

private slots:
    void OnAddWay();
    void OnRemoveSelected();

private:
    void PopulateOutputList();
    void RefreshBindingsList();
    void UpdateEnabledState();
    [[nodiscard]] std::string CurrentOutputName() const;
    [[nodiscard]] static QString DisplayChord(const std::vector<std::string>& input);

    int m_port_number;
    Core::Input::BindingsConfig* m_config; // not owned
    Core::Input::BindingsConfig* m_global_overlay = nullptr; // not owned; read-only display

    QCheckBox* m_assigned_check = nullptr;
    GamepadDiagramWidget* m_diagram = nullptr;
    QListWidget* m_output_list = nullptr;
    QListWidget* m_bindings_list = nullptr;
    QLabel* m_hint_label = nullptr;
    QPushButton* m_add_btn = nullptr;
    QPushButton* m_remove_btn = nullptr;
};

class InputBindingsDialog : public QDialog {
    Q_OBJECT
public:
    // targetFile: the bindings file this dialog edits directly (global.json
    // by default -- see the file comment above). Never pass default.json's
    // path here (section 3). The in-dialog file picker can still switch to
    // a different target after construction.
    explicit InputBindingsDialog(const std::filesystem::path& targetFile,
                                 QWidget* parent = nullptr);
    explicit InputBindingsDialog(QWidget* parent = nullptr);

private slots:
    void OnSave();
    void RefreshConflicts();
    void OnFilePickerChanged(int index);
    void OnBrowseForGame();

private:
    void BuildUi();
    QWidget* BuildSettingsPage();
    void PopulateFilePicker();
    // Switches the dialog to edit `path` in place -- reloads m_config (and
    // the global.json overlay, if `path` isn't global.json itself), then
    // tells every page and the settings tab to re-render.
    void SwitchTarget(const std::filesystem::path& path);
    void ReloadSettingsTab();

    std::filesystem::path m_global_json_path;
    std::unique_ptr<Core::Input::BindingsConfig> m_config;
    std::unique_ptr<Core::Input::BindingsConfig> m_global_overlay; // set only when editing a game file

    QComboBox* m_file_picker = nullptr;
    QLabel* m_subtitle_label = nullptr;
    QTabWidget* m_tabs = nullptr;
    std::array<PortBindingsPage*, 4> m_pages{};
    QListWidget* m_conflicts_list = nullptr;
    QLabel* m_conflicts_summary = nullptr;

    // Settings tab (section 8: mouse-to-joystick and per-stick/trigger
    // deadzones -- global.json-level settings, not per-output bindings).
    QComboBox* m_mouse_to_joystick = nullptr;
    class QDoubleSpinBox* m_mouse_deadzone_offset = nullptr;
    class QDoubleSpinBox* m_mouse_speed = nullptr;
    class QDoubleSpinBox* m_mouse_speed_offset = nullptr;
    std::array<class QSpinBox*, 8> m_deadzone_spins{}; // {ls_min,ls_max,rs_min,rs_max,lt_min,lt_max,rt_min,rt_max}
};
