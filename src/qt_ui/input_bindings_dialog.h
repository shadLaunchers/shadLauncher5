// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <QDialog>
#include <QListWidget>
#include <QSet>

#include "core/input/bindings_config.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;
class GamepadDiagramWidget;
class GamepadSelector;

class PortBindingsPage : public QWidget {
    Q_OBJECT
public:
    PortBindingsPage(int port_number, Core::Input::BindingsConfig* config,
                     QWidget* parent = nullptr);

    [[nodiscard]] int PortNumber() const {
        return m_port_number;
    }
    [[nodiscard]] bool HasPinnedDevice() const;
    void RefreshDeviceLabel();
    void SetOverlay(Core::Input::BindingsConfig* overlay, const QString& label);
    void Reload();
    void ShowPressed(const QString& name, bool pressed);
    void ClearPressed();
    [[nodiscard]] QSet<QString> BoundOutputs() const;

signals:
    void BindingsChanged();

private slots:
    void OnAddWay();
    void OnRemoveSelected();
    void OnSetUnmapped();
    void OnFilterChanged(const QString& text);
    void OnBindingsContextMenu(const QPoint& pos);

private:
    void PopulateOutputList();
    void RefreshBindingsList();
    void RefreshOutputMarkers();
    [[nodiscard]] std::string CurrentOutputName() const;
    [[nodiscard]] static QString DisplayChord(const std::vector<std::string>& input);
    [[nodiscard]] static bool IsAnalogOutput(const std::string& name);
    [[nodiscard]] QSet<QString> OutputsBoundIn(Core::Input::BindingsConfig* config) const;

    int m_port_number;
    Core::Input::BindingsConfig* m_config;
    Core::Input::BindingsConfig* m_global_overlay = nullptr;
    QString m_overlay_label;

    QLabel* m_device_label = nullptr;
    GamepadDiagramWidget* m_diagram = nullptr;
    class QLineEdit* m_filter = nullptr;
    QListWidget* m_output_list = nullptr;
    QListWidget* m_bindings_list = nullptr;
    QLabel* m_hint_label = nullptr;
    QPushButton* m_add_btn = nullptr;
    QPushButton* m_unmapped_btn = nullptr;
    QPushButton* m_remove_btn = nullptr;
    std::vector<int> m_row_to_binding;
};

class InputBindingsDialog : public QDialog {
    Q_OBJECT
public:
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
    void closeEvent(class QCloseEvent* event) override;
    void reject() override;

private:
    void BuildUi();
    QWidget* BuildSettingsPage();
    [[nodiscard]] PortBindingsPage* CurrentPage() const;
    void RefreshPortTabs();
    bool SeedFromDefaultsIfNew(const std::filesystem::path& path);
    void LoadOverlayFor(const std::filesystem::path& path);
    void RefreshSeededBanner();
    void PopulateFilePicker();
    void SwitchTarget(const std::filesystem::path& path);
    void ReloadSettingsTab();
    void RefreshLoadedState();
    [[nodiscard]] bool ConfirmDiscardingEdits();

    std::filesystem::path m_global_json_path;
    std::unique_ptr<Core::Input::BindingsConfig> m_config;
    std::unique_ptr<Core::Input::BindingsConfig> m_global_overlay;
    QString m_overlay_label;

    QComboBox* m_file_picker = nullptr;
    GamepadSelector* m_gamepad = nullptr;
    QLabel* m_subtitle_label = nullptr;
    QLabel* m_unreadable_label = nullptr;
    QLabel* m_seeded_label = nullptr;
    bool m_seeded_from_defaults = false;
    QPushButton* m_save_btn = nullptr;
    QPushButton* m_revert_btn = nullptr;
    QTabWidget* m_tabs = nullptr;
    std::array<PortBindingsPage*, 4> m_pages{};
    class QTabWidget* m_issues = nullptr;
    QListWidget* m_conflicts_list = nullptr;
    QLabel* m_conflicts_summary = nullptr;
    QListWidget* m_problems_list = nullptr;
    QComboBox* m_mouse_to_joystick = nullptr;
    class QDoubleSpinBox* m_mouse_deadzone_offset = nullptr;
    class QDoubleSpinBox* m_mouse_speed = nullptr;
    class QDoubleSpinBox* m_mouse_speed_offset = nullptr;
    std::array<class QSpinBox*, 8>
        m_deadzone_spins{}; // {ls_min,ls_max,rs_min,rs_max,lt_min,lt_max,rt_min,rt_max}
};
