// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The full pad-control binding editor (docs/input-bindings.md), as opposed
// to hotkeys_editor_dialog.h which only handles hotkeys.json. One QTabWidget
// page per port (1-4); a port's page is only editable once its "this port
// is assigned" checkbox is on.
//
// FIRST PASS -- scope deliberately narrower than the full spec for now:
//  - A page shows the bindings that drive *that player's pad*
//    (PortedBinding::OutputPlayer()), and a binding it creates is written as
//    an output-side suffix -- "output": "cross:2". That is the only spelling
//    that means what this page's tab says: a "gamepad" field names the device
//    allowed to press the binding and leaves the pad on player 1.
//  - A binding that arrived in one of the other two spellings keeps it when
//    rewritten, so a file that routes one player's device to another player's
//    pad survives a round-trip. Right-clicking a row sets which device may
//    press it (the "gamepad" field); an input-side ":n" is preserved but
//    still not something the editor writes from scratch.
//  - "Port assigned" is this editor's own concept, not read from the
//    emulator: docs/multi-user.md (which this editor hasn't been given)
//    covers how a *device* actually gets a port at runtime. Here it just
//    means "show and let me edit this port's bindings" -- unchecking it
//    does not delete anything already saved for that port, it only hides
//    the tab's controls.
//
// File picker (section 2): the person can switch between global.json and a
// specific game's input_config/<title id>.json. When editing a game's
// file, global.json is also loaded read-only as an overlay -- its bindings
// are shown alongside the game file's own (clearly marked, not editable
// here) and folded into conflict detection, since that's what the emulator
// actually sees at runtime (both files' bindings are concatenated). Editing
// global.json itself has no overlay: it *is* one of the two layers.

#pragma once

#include <QDialog>
#include <QListWidget>
#include <QSet>
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
class GamepadSelector;

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

    // Live feedback from the selected pad: light the control being held.
    void ShowPressed(const QString& name, bool pressed);
    void ClearPressed();

    // The outputs on this page that have at least one binding, for the
    // diagram's dots and the list's markers.
    [[nodiscard]] QSet<QString> BoundOutputs() const;

signals:
    // Emitted after a way-to-press is added or removed, so the dialog can
    // refresh conflict detection (which needs every port's bindings, not
    // just this page's).
    void BindingsChanged();

private slots:
    void OnAddWay();
    void OnRemoveSelected();
    void OnSetUnmapped();
    void OnFilterChanged(const QString& text);
    // Right-click a way-to-press: which device may press it. The only part
    // of section 5 the editor could previously show but not set.
    void OnBindingsContextMenu(const QPoint& pos);

private:
    void PopulateOutputList();
    void RefreshBindingsList();
    void RefreshOutputMarkers();
    void UpdateEnabledState();
    [[nodiscard]] std::string CurrentOutputName() const;
    [[nodiscard]] static QString DisplayChord(const std::vector<std::string>& input);
    // True for axis_left_x and friends: only an axis may drive them, which
    // changes what the hint tells you to press.
    [[nodiscard]] static bool IsAnalogOutput(const std::string& name);

    int m_port_number;
    Core::Input::BindingsConfig* m_config; // not owned
    Core::Input::BindingsConfig* m_global_overlay = nullptr; // not owned; read-only display

    QCheckBox* m_assigned_check = nullptr;
    GamepadDiagramWidget* m_diagram = nullptr;
    class QLineEdit* m_filter = nullptr;
    QListWidget* m_output_list = nullptr;
    QListWidget* m_bindings_list = nullptr;
    QLabel* m_hint_label = nullptr;
    QPushButton* m_add_btn = nullptr;
    QPushButton* m_unmapped_btn = nullptr;
    QPushButton* m_remove_btn = nullptr;

    // Row -> index into this output's binding list, so a row the person
    // selected can be mapped back to the entry it came from. The list mixes
    // this player's own bindings with the ones that belong to every player,
    // so the row number is not the index.
    std::vector<int> m_row_to_binding;
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
    void OnRevert();
    void RefreshConflicts();
    void RefreshProblemsList();
    void OnFilePickerChanged(int index);
    void OnBrowseForGame();

protected:
    // Closing with edits in hand asks first, however the dialog is closed.
    void closeEvent(class QCloseEvent* event) override;
    void reject() override;

private:
    void BuildUi();
    QWidget* BuildSettingsPage();
    void PopulateFilePicker();
    // Switches the dialog to edit `path` in place -- reloads m_config (and
    // the global.json overlay, if `path` isn't global.json itself), then
    // tells every page and the settings tab to re-render.
    void SwitchTarget(const std::filesystem::path& path);
    void ReloadSettingsTab();
    // Save button, the unreadable-file banner and the per-page enabled state
    // all follow whether the current file actually loaded.
    void RefreshLoadedState();
    // Returns false if the person cancelled out of the "you have unsaved
    // changes" prompt. Called before anything that would discard them.
    [[nodiscard]] bool ConfirmDiscardingEdits();

    std::filesystem::path m_global_json_path;
    std::unique_ptr<Core::Input::BindingsConfig> m_config;
    std::unique_ptr<Core::Input::BindingsConfig> m_global_overlay; // set only when editing a game file

    QComboBox* m_file_picker = nullptr;
    GamepadSelector* m_gamepad = nullptr;
    QLabel* m_subtitle_label = nullptr;
    QLabel* m_unreadable_label = nullptr;
    QPushButton* m_save_btn = nullptr;
    QPushButton* m_revert_btn = nullptr;
    QTabWidget* m_tabs = nullptr;
    std::array<PortBindingsPage*, 4> m_pages{};
    // Conflicts and problems share one small tabbed box at the bottom:
    // two stacked group boxes cost ~240px of height for two lists that are
    // usually empty.
    class QTabWidget* m_issues = nullptr;
    QListWidget* m_conflicts_list = nullptr;
    QLabel* m_conflicts_summary = nullptr;
    QListWidget* m_problems_list = nullptr;

    // Settings tab (section 8: mouse-to-joystick and per-stick/trigger
    // deadzones -- global.json-level settings, not per-output bindings).
    QComboBox* m_mouse_to_joystick = nullptr;
    class QDoubleSpinBox* m_mouse_deadzone_offset = nullptr;
    class QDoubleSpinBox* m_mouse_speed = nullptr;
    class QDoubleSpinBox* m_mouse_speed_offset = nullptr;
    std::array<class QSpinBox*, 8> m_deadzone_spins{}; // {ls_min,ls_max,rs_min,rs_max,lt_min,lt_max,rt_min,rt_max}
};
