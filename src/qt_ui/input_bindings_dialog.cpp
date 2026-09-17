// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "common/path_util.h"
#include "core/input/input_ids.h"
#include "hotkeys_editor_dialog.h" // reuses KeyCaptureDialog
#include "input_bindings_dialog.h"

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

    m_assigned_check =
        new QCheckBox(tr("This port is assigned (edit its bindings)"), this);
    // Port 1 on by default -- the common single-player case; the person
    // turns on more as they actually use them.
    m_assigned_check->setChecked(port_number == 1);
    connect(m_assigned_check, &QCheckBox::toggled, this, &PortBindingsPage::UpdateEnabledState);
    outer->addWidget(m_assigned_check);

    auto* main_layout = new QHBoxLayout();

    auto* left_layout = new QVBoxLayout();
    left_layout->addWidget(new QLabel(tr("Pad control"), this));
    m_output_list = new QListWidget(this);
    left_layout->addWidget(m_output_list);
    main_layout->addLayout(left_layout, 1);

    auto* right_layout = new QVBoxLayout();
    right_layout->addWidget(new QLabel(tr("Ways to press it"), this));
    m_bindings_list = new QListWidget(this);
    right_layout->addWidget(m_bindings_list);

    m_hint_label = new QLabel(this);
    m_hint_label->setWordWrap(true);
    right_layout->addWidget(m_hint_label);

    auto* row_buttons = new QHBoxLayout();
    m_add_btn = new QPushButton(tr("Add a way..."), this);
    m_remove_btn = new QPushButton(tr("Remove selected"), this);
    connect(m_add_btn, &QPushButton::clicked, this, &PortBindingsPage::OnAddWay);
    connect(m_remove_btn, &QPushButton::clicked, this, &PortBindingsPage::OnRemoveSelected);
    row_buttons->addWidget(m_add_btn);
    row_buttons->addWidget(m_remove_btn);
    right_layout->addLayout(row_buttons);

    main_layout->addLayout(right_layout, 2);
    outer->addLayout(main_layout);

    connect(m_output_list, &QListWidget::currentRowChanged, this,
            [this](int) { RefreshBindingsList(); });

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
    resize(760, 560);

    auto* outer = new QVBoxLayout(this);

    if (!m_config->IsLoaded()) {
        outer->addWidget(new QLabel(
            tr("Couldn't read %1 -- it may not be valid JSON. Editing is disabled to avoid "
              "overwriting whatever's actually in it.")
                .arg(QString::fromStdString(m_config->FilePath().string())),
            this));
    }

    m_tabs = new QTabWidget(this);
    for (int port = 1; port <= 4; port++) {
        auto* page = new PortBindingsPage(port, m_config.get(), this);
        m_pages[static_cast<size_t>(port - 1)] = page;
        m_tabs->addTab(page, tr("Port %1").arg(port));
        page->setEnabled(m_config->IsLoaded());
        connect(page, &PortBindingsPage::BindingsChanged, this, &InputBindingsDialog::RefreshConflicts);
    }
    outer->addWidget(m_tabs, 3);

    m_conflicts_summary = new QLabel(this);
    m_conflicts_summary->setWordWrap(true);
    outer->addWidget(m_conflicts_summary);
    m_conflicts_list = new QListWidget(this);
    m_conflicts_list->setMaximumHeight(120);
    outer->addWidget(m_conflicts_list, 1);
    RefreshConflicts();

    auto* buttons = new QHBoxLayout();
    auto* save_btn = new QPushButton(tr("Save"), this);
    auto* close_btn = new QPushButton(tr("Close"), this);
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
        m_conflicts_summary->setText(tr("No conflicts: no two bindings share the exact same "
                                       "keys for different controls."));
        return;
    }

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
        m_conflicts_list->addItem(tr("%1 vs %2: both use %3 (%4)")
                                      .arg(QString::fromStdString(c.output_a))
                                      .arg(QString::fromStdString(c.output_b))
                                      .arg(keys.join(" + "))
                                      .arg(port_text));
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
