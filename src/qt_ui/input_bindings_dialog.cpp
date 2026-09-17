// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "common/path_util.h"
#include "core/input/input_ids.h"
#include "gamepad_diagram_widget.h"
#include "hotkeys_editor_dialog.h" // reuses KeyCaptureDialog
#include "input_bindings_dialog.h"
#include "qt_utils.h"

namespace {

QString FriendlyOutputName(const std::string& id) {
    QString s = QString::fromStdString(id);
    s.replace('_', ' ');
    QStringList words = s.split(' ', Qt::SkipEmptyParts);
    for (QString& w : words) {
        if (!w.isEmpty()) {
            w[0] = w[0].toUpper();
        }
    }
    return words.join(' ');
}

// A non-selectable, bold row used to group the output list -- Buttons,
// Touchpad, Axes, Analog Sticks (input-bindings.md section 7's own
// grouping).
void AddSectionHeader(QListWidget* list, const QString& title) {
    auto* item = new QListWidgetItem(title);
    QFont f = item->font();
    f.setBold(true);
    item->setFont(f);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
    list->addItem(item);
}

void AddOutputRow(QListWidget* list, std::string_view name) {
    const std::string id(name);
    auto* item = new QListWidgetItem("    " + FriendlyOutputName(id));
    item->setData(Qt::UserRole, QString::fromStdString(id));
    list->addItem(item);
}

} // namespace

// ---------------------------------------------------------------------
// PortBindingsPage
// ---------------------------------------------------------------------

PortBindingsPage::PortBindingsPage(int port_number, Core::Input::BindingsConfig* config,
                                   QWidget* parent)
    : QWidget(parent), m_port_number(port_number), m_config(config) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(10);

    m_assigned_check =
        new QCheckBox(tr("This port is assigned (edit its bindings)"), this);
    QFont check_font = m_assigned_check->font();
    check_font.setBold(true);
    m_assigned_check->setFont(check_font);
    // Port 1 on by default -- the common single-player case; the person
    // turns on more as they actually use them.
    m_assigned_check->setChecked(port_number == 1);
    connect(m_assigned_check, &QCheckBox::toggled, this, &PortBindingsPage::UpdateEnabledState);
    outer->addWidget(m_assigned_check);

    auto* diagram_box = new QGroupBox(tr("Layout"), this);
    auto* diagram_layout = new QVBoxLayout(diagram_box);
    m_diagram = new GamepadDiagramWidget(diagram_box);
    diagram_layout->addWidget(m_diagram);
    auto* diagram_hint = new QLabel(
        tr("Click a control on the diagram, or pick one from the list below."), diagram_box);
    diagram_hint->setStyleSheet("color: palette(placeholder-text);");
    diagram_hint->setAlignment(Qt::AlignCenter);
    diagram_layout->addWidget(diagram_hint);
    connect(m_diagram, &GamepadDiagramWidget::OutputClicked, this,
            [this](const QString& output) {
                for (int i = 0; i < m_output_list->count(); i++) {
                    auto* item = m_output_list->item(i);
                    if (item->data(Qt::UserRole).toString() == output) {
                        m_output_list->setCurrentRow(i);
                        break;
                    }
                }
            });
    outer->addWidget(diagram_box);

    auto* main_layout = new QHBoxLayout();
    main_layout->setSpacing(12);

    auto* output_box = new QGroupBox(tr("Pad Control"), this);
    auto* left_layout = new QVBoxLayout(output_box);
    m_output_list = new QListWidget(output_box);
    left_layout->addWidget(m_output_list);
    main_layout->addWidget(output_box, 1);

    auto* ways_box = new QGroupBox(tr("Ways to Press It"), this);
    auto* right_layout = new QVBoxLayout(ways_box);
    m_bindings_list = new QListWidget(ways_box);
    right_layout->addWidget(m_bindings_list);

    m_hint_label = new QLabel(ways_box);
    m_hint_label->setWordWrap(true);
    m_hint_label->setStyleSheet("color: palette(placeholder-text);");
    right_layout->addWidget(m_hint_label);

    auto* row_buttons = new QHBoxLayout();
    m_add_btn = new QPushButton(tr("Add a way..."), ways_box);
    m_remove_btn = new QPushButton(tr("Remove selected"), ways_box);
    connect(m_add_btn, &QPushButton::clicked, this, &PortBindingsPage::OnAddWay);
    connect(m_remove_btn, &QPushButton::clicked, this, &PortBindingsPage::OnRemoveSelected);
    row_buttons->addWidget(m_add_btn);
    row_buttons->addWidget(m_remove_btn);
    right_layout->addLayout(row_buttons);

    main_layout->addWidget(ways_box, 2);
    outer->addLayout(main_layout);

    connect(m_output_list, &QListWidget::currentRowChanged, this, [this](int) {
        RefreshBindingsList();
        m_diagram->SetHighlightedOutput(QString::fromStdString(CurrentOutputName()));
    });

    PopulateOutputList();
    UpdateEnabledState();
}

void PortBindingsPage::PopulateOutputList() {
    AddSectionHeader(m_output_list, QObject::tr("Buttons"));
    for (const auto& n : Core::Input::kPadButtonNames) {
        AddOutputRow(m_output_list, n);
    }
    AddSectionHeader(m_output_list, QObject::tr("Touchpad"));
    for (const auto& n : Core::Input::kTouchpadOutputNames) {
        AddOutputRow(m_output_list, n);
    }
    AddSectionHeader(m_output_list, QObject::tr("Axes (digital edges, plus L2/R2)"));
    for (const auto& n : Core::Input::kAxisEdgeOutputNames) {
        AddOutputRow(m_output_list, n);
    }
    AddSectionHeader(m_output_list, QObject::tr("Analog Sticks (stick input only)"));
    for (const auto& n : Core::Input::kAnalogToAnalogOutputNames) {
        AddOutputRow(m_output_list, n);
    }

    // Select the first real row, not a header.
    for (int i = 0; i < m_output_list->count(); i++) {
        if (m_output_list->item(i)->flags() & Qt::ItemIsSelectable) {
            m_output_list->setCurrentRow(i);
            break;
        }
    }
}

bool PortBindingsPage::IsAssigned() const {
    return m_assigned_check->isChecked();
}

void PortBindingsPage::UpdateEnabledState() {
    const bool enabled = IsAssigned();
    m_output_list->setEnabled(enabled);
    m_bindings_list->setEnabled(enabled);
    m_add_btn->setEnabled(enabled);
    m_remove_btn->setEnabled(enabled);
    RefreshBindingsList();
}

std::string PortBindingsPage::CurrentOutputName() const {
    const auto* item = m_output_list->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsSelectable)) {
        return {};
    }
    return item->data(Qt::UserRole).toString().toStdString();
}

QString PortBindingsPage::DisplayChord(const std::vector<std::string>& input) {
    QStringList parts;
    for (const auto& n : input) {
        parts << QString::fromStdString(n);
    }
    return parts.join(" + ");
}

void PortBindingsPage::RefreshBindingsList() {
    m_bindings_list->clear();
    if (!IsAssigned()) {
        m_hint_label->setText(tr("Turn on \"This port is assigned\" above to edit its bindings."));
        return;
    }
    const std::string name = CurrentOutputName();
    if (name.empty()) {
        m_hint_label->clear();
        return;
    }
    int shown = 0;
    for (const auto& binding : m_config->GetBindings(name)) {
        // This page only shows/edits bindings scoped to this exact port.
        // Bindings that name no port (port == 0, "belongs to every port",
        // section 5) drive every port including this one, but aren't
        // listed per-port here to avoid the same line appearing edited
        // four times over; they still work at runtime regardless.
        if (binding.port == m_port_number) {
            m_bindings_list->addItem(DisplayChord(binding.input));
            shown++;
        }
    }
    m_hint_label->setText(
        shown > 0
            ? tr("Showing this port's own bindings for %1. Bindings with no port named also "
                "drive this port and aren't listed here.")
                  .arg(FriendlyOutputName(name))
            : tr("No binding scoped specifically to port %1 for %2 yet -- it may still work "
                "via a binding that names no port.")
                  .arg(m_port_number)
                  .arg(FriendlyOutputName(name)));
}

void PortBindingsPage::OnAddWay() {
    const std::string name = CurrentOutputName();
    if (name.empty()) {
        return;
    }
    KeyCaptureDialog capture(this);
    if (capture.exec() != QDialog::Accepted) {
        return;
    }
    auto bindings = m_config->GetBindings(name);
    Core::Input::PortedBinding new_binding;
    new_binding.input = capture.CapturedInput();
    new_binding.port = m_port_number;
    bindings.push_back(new_binding);
    m_config->SetBindings(name, bindings);
    RefreshBindingsList();
    emit BindingsChanged();
}

void PortBindingsPage::OnRemoveSelected() {
    const std::string name = CurrentOutputName();
    const int row = m_bindings_list->currentRow();
    if (name.empty() || row < 0) {
        return;
    }
    // The bindings list only shows this port's own entries, so map the
    // selected row back to its position among all of this output's
    // bindings before erasing.
    auto bindings = m_config->GetBindings(name);
    int seen = -1;
    for (size_t i = 0; i < bindings.size(); i++) {
        if (bindings[i].port == m_port_number) {
            seen++;
            if (seen == row) {
                bindings.erase(bindings.begin() + static_cast<long>(i));
                break;
            }
        }
    }
    m_config->SetBindings(name, bindings);
    RefreshBindingsList();
    emit BindingsChanged();
}

// ---------------------------------------------------------------------
// InputBindingsDialog
// ---------------------------------------------------------------------

InputBindingsDialog::InputBindingsDialog(const std::filesystem::path& targetFile, QWidget* parent)
    : QDialog(parent), m_config(std::make_unique<Core::Input::BindingsConfig>()) {
    m_config->Load(targetFile);
    BuildUi();
}

InputBindingsDialog::InputBindingsDialog(QWidget* parent)
    : InputBindingsDialog(Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "global.json",
                         parent) {}

void InputBindingsDialog::BuildUi() {
    setWindowTitle(tr("Input Bindings -- %1").arg(
        QString::fromStdString(m_config->FilePath().filename().string())));
    setWindowIcon(GUI::Utils::TintIcon(QIcon(":/images/menu/gamepad.svg"), GUI::Utils::ThemeTextColor()));
    resize(800, 600);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    // Header: icon + title + which file this session edits.
    auto* header = new QHBoxLayout();
    auto* icon_label = new QLabel(this);
    icon_label->setPixmap(
        GUI::Utils::TintIcon(QIcon(":/images/menu/gamepad.svg"), GUI::Utils::ThemeTextColor())
            .pixmap(32, 32));
    header->addWidget(icon_label);

    auto* title_layout = new QVBoxLayout();
    auto* title_label = new QLabel(tr("Input Bindings"), this);
    QFont title_font = title_label->font();
    title_font.setPointSize(title_font.pointSize() + 4);
    title_font.setBold(true);
    title_label->setFont(title_font);
    title_layout->addWidget(title_label);

    auto* subtitle_label =
        new QLabel(tr("Editing %1").arg(QString::fromStdString(m_config->FilePath().string())), this);
    subtitle_label->setStyleSheet("color: palette(placeholder-text);");
    title_layout->addWidget(subtitle_label);
    header->addLayout(title_layout);
    header->addStretch();
    outer->addLayout(header);

    if (!m_config->IsLoaded()) {
        auto* warning = new QLabel(
            tr("Couldn't read %1 -- it may not be valid JSON. Editing is disabled to avoid "
              "overwriting whatever's actually in it.")
                .arg(QString::fromStdString(m_config->FilePath().string())),
            this);
        warning->setWordWrap(true);
        warning->setStyleSheet("color: #E05555; font-weight: bold;");
        outer->addWidget(warning);
    }

    m_tabs = new QTabWidget(this);
    const QIcon tab_icon =
        GUI::Utils::TintIcon(QIcon(":/images/menu/gamepad.svg"), GUI::Utils::ThemeTextColor());
    for (int port = 1; port <= 4; port++) {
        auto* page = new PortBindingsPage(port, m_config.get(), this);
        m_pages[static_cast<size_t>(port - 1)] = page;
        m_tabs->addTab(page, tab_icon, tr("Port %1").arg(port));
        page->setEnabled(m_config->IsLoaded());
        connect(page, &PortBindingsPage::BindingsChanged, this, &InputBindingsDialog::RefreshConflicts);
    }
    m_tabs->addTab(BuildSettingsPage(), tr("Settings"));
    outer->addWidget(m_tabs, 3);

    auto* conflicts_box = new QGroupBox(tr("Conflicts"), this);
    auto* conflicts_layout = new QVBoxLayout(conflicts_box);
    m_conflicts_summary = new QLabel(conflicts_box);
    m_conflicts_summary->setWordWrap(true);
    conflicts_layout->addWidget(m_conflicts_summary);
    m_conflicts_list = new QListWidget(conflicts_box);
    m_conflicts_list->setMaximumHeight(110);
    m_conflicts_list->setAlternatingRowColors(true);
    conflicts_layout->addWidget(m_conflicts_list);
    outer->addWidget(conflicts_box, 1);
    RefreshConflicts();

    auto* buttons = new QHBoxLayout();
    auto* save_btn = new QPushButton(tr("Save"), this);
    save_btn->setDefault(true);
    save_btn->setMinimumWidth(100);
    auto* close_btn = new QPushButton(tr("Close"), this);
    close_btn->setMinimumWidth(100);
    save_btn->setEnabled(m_config->IsLoaded());
    connect(save_btn, &QPushButton::clicked, this, &InputBindingsDialog::OnSave);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addStretch();
    buttons->addWidget(save_btn);
    buttons->addWidget(close_btn);
    outer->addLayout(buttons);
}

void InputBindingsDialog::RefreshConflicts() {
    m_conflicts_list->clear();
    if (!m_config->IsLoaded()) {
        m_conflicts_summary->clear();
        return;
    }

    const auto conflicts = Core::Input::FindConflicts(m_config->GetAllBindings());
    if (conflicts.empty()) {
        m_conflicts_summary->setText(tr("\u2713 No conflicts: no two bindings share the exact "
                                       "same keys for different controls."));
        m_conflicts_summary->setStyleSheet("color: #4CAF50;");
        return;
    }

    m_conflicts_summary->setStyleSheet("color: #E0A030; font-weight: bold;");
    m_conflicts_summary->setText(
        tr("%n conflict(s): these bindings press the same keys but drive different "
          "controls -- both fire together, which is probably not what you want.",
          "", static_cast<int>(conflicts.size())));

    for (const auto& c : conflicts) {
        QStringList keys;
        for (const auto& k : c.keys) {
            keys << QString::fromStdString(k);
        }
        const QString port_text =
            c.port == 0 ? tr("every port") : tr("port %1").arg(c.port);
        auto* item = new QListWidgetItem(tr("%1 vs %2: both use %3 (%4)")
                                             .arg(QString::fromStdString(c.output_a))
                                             .arg(QString::fromStdString(c.output_b))
                                             .arg(keys.join(" + "))
                                             .arg(port_text));
        item->setForeground(QColor("#E0A030"));
        m_conflicts_list->addItem(item);
    }
}

void InputBindingsDialog::OnSave() {
    if (!m_config->Save()) {
        QMessageBox::critical(this, tr("Input Bindings"),
                              tr("Failed to write %1.").arg(
                                  QString::fromStdString(m_config->FilePath().string())));
        return;
    }
    QMessageBox::information(this, tr("Input Bindings"), tr("Saved."));
}

QWidget* InputBindingsDialog::BuildSettingsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // --- Mouse to Joystick (section 8) ---
    auto* mouse_box = new QGroupBox(tr("Mouse to Joystick"), page);
    auto* mouse_form = new QFormLayout(mouse_box);
    m_mouse_to_joystick = new QComboBox(mouse_box);
    m_mouse_to_joystick->addItem(tr("Right stick"), QStringLiteral("right"));
    m_mouse_to_joystick->addItem(tr("Left stick"), QStringLiteral("left"));
    m_mouse_to_joystick->addItem(tr("Off"), QStringLiteral("none"));
    m_mouse_deadzone_offset = new QDoubleSpinBox(mouse_box);
    m_mouse_deadzone_offset->setRange(0.0, 1.0);
    m_mouse_deadzone_offset->setSingleStep(0.05);
    m_mouse_speed = new QDoubleSpinBox(mouse_box);
    m_mouse_speed->setRange(0.01, 10.0);
    m_mouse_speed->setSingleStep(0.1);
    m_mouse_speed_offset = new QDoubleSpinBox(mouse_box);
    m_mouse_speed_offset->setRange(0.0, 5.0);
    m_mouse_speed_offset->setSingleStep(0.05);
    mouse_form->addRow(tr("Drives:"), m_mouse_to_joystick);
    mouse_form->addRow(tr("Deadzone offset:"), m_mouse_deadzone_offset);
    mouse_form->addRow(tr("Speed:"), m_mouse_speed);
    mouse_form->addRow(tr("Speed offset:"), m_mouse_speed_offset);
    layout->addWidget(mouse_box);

    bool mouse_present = false;
    const auto mouse = m_config->GetMouseSettings(&mouse_present);
    const int mouse_idx =
        m_mouse_to_joystick->findData(QString::fromStdString(mouse.to_joystick));
    m_mouse_to_joystick->setCurrentIndex(mouse_idx >= 0 ? mouse_idx : 0);
    m_mouse_deadzone_offset->setValue(mouse.deadzone_offset);
    m_mouse_speed->setValue(mouse.speed);
    m_mouse_speed_offset->setValue(mouse.speed_offset);
    if (!mouse_present) {
        mouse_box->setTitle(tr("Mouse to Joystick (not set in this file -- showing defaults)"));
    }

    auto commit_mouse = [this] {
        Core::Input::MouseSettings s;
        s.to_joystick = m_mouse_to_joystick->currentData().toString().toStdString();
        s.deadzone_offset = m_mouse_deadzone_offset->value();
        s.speed = m_mouse_speed->value();
        s.speed_offset = m_mouse_speed_offset->value();
        m_config->SetMouseSettings(s);
    };
    connect(m_mouse_to_joystick, qOverload<int>(&QComboBox::currentIndexChanged), this, commit_mouse);
    connect(m_mouse_deadzone_offset, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            commit_mouse);
    connect(m_mouse_speed, qOverload<double>(&QDoubleSpinBox::valueChanged), this, commit_mouse);
    connect(m_mouse_speed_offset, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            commit_mouse);

    // --- Deadzones (section 8) ---
    auto* dz_box = new QGroupBox(tr("Deadzones"), page);
    auto* dz_form = new QFormLayout(dz_box);
    bool dz_present = false;
    const auto dz = m_config->GetDeadzoneSettings(&dz_present);
    const std::array<std::pair<QString, Core::Input::DeadzoneRange>, 4> rows{{
        {tr("Left stick"), dz.left_stick},
        {tr("Right stick"), dz.right_stick},
        {tr("Left trigger"), dz.left_trigger},
        {tr("Right trigger"), dz.right_trigger},
    }};
    size_t spin_idx = 0;
    for (const auto& [label, range] : rows) {
        auto* row_layout = new QHBoxLayout();
        auto* min_spin = new QSpinBox(dz_box);
        min_spin->setRange(0, 255);
        min_spin->setValue(range.min);
        auto* max_spin = new QSpinBox(dz_box);
        max_spin->setRange(0, 255);
        max_spin->setValue(range.max);
        row_layout->addWidget(new QLabel(tr("min"), dz_box));
        row_layout->addWidget(min_spin);
        row_layout->addWidget(new QLabel(tr("max"), dz_box));
        row_layout->addWidget(max_spin);
        dz_form->addRow(label, row_layout);
        m_deadzone_spins[spin_idx++] = min_spin;
        m_deadzone_spins[spin_idx++] = max_spin;
    }
    if (!dz_present) {
        dz_box->setTitle(tr("Deadzones (not set in this file -- showing defaults)"));
    }
    layout->addWidget(dz_box);
    layout->addStretch();

    auto commit_deadzones = [this] {
        Core::Input::DeadzoneSettings s;
        s.left_stick = {m_deadzone_spins[0]->value(), m_deadzone_spins[1]->value()};
        s.right_stick = {m_deadzone_spins[2]->value(), m_deadzone_spins[3]->value()};
        s.left_trigger = {m_deadzone_spins[4]->value(), m_deadzone_spins[5]->value()};
        s.right_trigger = {m_deadzone_spins[6]->value(), m_deadzone_spins[7]->value()};
        m_config->SetDeadzoneSettings(s);
    };
    for (auto* spin : m_deadzone_spins) {
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, commit_deadzones);
    }

    return page;
}
