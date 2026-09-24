// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <vector>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyleHints>
#include <QTabWidget>
#include <QVBoxLayout>

#include "common/path_util.h"
#include "core/input/input_defaults.h"
#include "core/input/input_ids.h"
#include "core/user_settings.h"
#include "game_info.h"
#include "gamepad_diagram_widget.h"
#include "gamepad_selector.h"
#include "hotkeys_editor_dialog.h"
#include "input_bindings_dialog.h"

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
    auto* item = new QListWidgetItem("  " + FriendlyOutputName(id));
    item->setData(Qt::UserRole, QString::fromStdString(id));
    list->addItem(item);
}

QColor MutedColor(const QPalette& pal) {
    const QColor text = pal.color(QPalette::WindowText);
    const QColor back = pal.color(QPalette::Window);
    return QColor::fromRgbF(text.redF() * 0.55 + back.redF() * 0.45,
                            text.greenF() * 0.55 + back.greenF() * 0.45,
                            text.blueF() * 0.55 + back.blueF() * 0.45);
}

void Muted(QWidget* widget) {
    QPalette pal = widget->palette();
    pal.setColor(QPalette::WindowText, MutedColor(pal));
    widget->setPalette(pal);
}

QIcon BoundMarker(const QColor& color, const QColor& ring, bool filled, int side) {
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);
    if (filled) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Ringed, so it survives being drawn on the selection highlight --
        // a plain Highlight-coloured dot on a Highlight-coloured row is
        // invisible, which is exactly the row you are looking at.
        p.setPen(QPen(ring, std::max(1.0, side * 0.055)));
        p.setBrush(color);
        const qreal r = side * 0.20;
        p.drawEllipse(QPointF(side / 2.0, side / 2.0), r, r);
    }
    return QIcon(pm);
}

QIcon TintedGamepadIcon() {
    const QIcon source(":/images/menu/gamepad.svg");
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QColor color = dark ? QColor(0xF5, 0xF5, 0xF7) : QColor(0x1D, 0x1D, 0x1F);

    QIcon out;
    const QList<QSize> sizes = source.availableSizes();
    const QList<QSize> render_sizes = sizes.isEmpty() ? QList<QSize>{QSize(64, 64)} : sizes;
    for (const QSize& size : render_sizes) {
        QPixmap pm = source.pixmap(size);
        if (pm.isNull()) {
            continue;
        }
        QPixmap tinted(pm.size());
        tinted.setDevicePixelRatio(pm.devicePixelRatio());
        tinted.fill(Qt::transparent);
        QPainter p(&tinted);
        p.drawPixmap(0, 0, pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(tinted.rect(), color);
        p.end();
        out.addPixmap(tinted);
    }
    return out;
}

struct PortDevice {
    bool pinned = false;
    QString user_name; // the user holding this port, pinned or not
    QString guid;
    QString device_name; // empty when the pinned device is not plugged in
};

PortDevice DeviceForPort(int port) {
    PortDevice out;
    for (const auto& user : UserManagement.GetAllUsers()) {
        if (user.player_index != port) {
            continue;
        }
        out.user_name = QString::fromStdString(user.user_name);
        if (!user.device_guid.empty()) {
            out.pinned = true;
            out.guid = QString::fromStdString(user.device_guid);
            out.device_name = GamepadNameForGuid(out.guid);
        }
        break;
    }
    return out;
}

KeyCaptureDialog::Accepts AcceptsForPort(const PortDevice& device) {
    if (!device.pinned) {
        return KeyCaptureDialog::Accepts::Any;
    }
    return device.guid == QStringLiteral("keyboard") ? KeyCaptureDialog::Accepts::KeyboardAndMouse
                                                     : KeyCaptureDialog::Accepts::Gamepad;
}

QString DeviceLineFor(int port, const PortDevice& device) {
    if (device.user_name.isEmpty()) {
        return QObject::tr("Port %1: no user assigned. Assign one in User Manager.").arg(port);
    }
    if (!device.pinned) {
        return QObject::tr("Port %1: %2, no device assigned. "
                           "Controllers fill ports in the order they connect.")
            .arg(port)
            .arg(device.user_name);
    }
    if (device.device_name.isEmpty()) {
        return QObject::tr("Port %1: %2, assigned controller not connected (%3).")
            .arg(port)
            .arg(device.user_name)
            .arg(device.guid.right(16));
    }
    return QObject::tr("Port %1: %2, %3.").arg(port).arg(device.user_name).arg(device.device_name);
}

PortBindingsPage::PortBindingsPage(int port_number, Core::Input::BindingsConfig* config,
                                   QWidget* parent)
    : QWidget(parent), m_port_number(port_number), m_config(config) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 6);
    outer->setSpacing(6);
    m_device_label = new QLabel(this);
    m_device_label->setWordWrap(true);
    m_device_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont device_font = m_device_label->font();
    device_font.setBold(true);
    m_device_label->setFont(device_font);
    outer->addWidget(m_device_label);
    RefreshDeviceLabel();
    m_diagram = new GamepadDiagramWidget(this);
    m_diagram->setMaximumWidth(430);
    m_diagram->setMinimumHeight(100);
    m_diagram->setMaximumHeight(168);
    m_diagram->setToolTip(tr("Click a control to select it."));
    connect(m_diagram, &GamepadDiagramWidget::OutputClicked, this, [this](const QString& output) {
        for (int i = 0; i < m_output_list->count(); i++) {
            auto* item = m_output_list->item(i);
            if (item->data(Qt::UserRole).toString() == output) {
                m_output_list->setCurrentRow(i);
                break;
            }
        }
    });
    outer->addWidget(m_diagram, 0, Qt::AlignHCenter);

    auto* main_layout = new QHBoxLayout();
    main_layout->setSpacing(12);

    auto* output_box = new QGroupBox(tr("Controls"), this);
    auto* left_layout = new QVBoxLayout(output_box);
    m_filter = new QLineEdit(output_box);
    m_filter->setPlaceholderText(tr("Filter (e.g. \"axis\", \"pad\")"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &PortBindingsPage::OnFilterChanged);
    left_layout->addWidget(m_filter);
    m_output_list = new QListWidget(output_box);
    m_output_list->setMinimumHeight(84);
    left_layout->addWidget(m_output_list);
    main_layout->addWidget(output_box, 1);

    auto* ways_box = new QGroupBox(tr("Bindings"), this);
    auto* right_layout = new QVBoxLayout(ways_box);
    m_bindings_list = new QListWidget(ways_box);
    m_bindings_list->setMinimumHeight(84);
    m_bindings_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_bindings_list, &QListWidget::customContextMenuRequested, this,
            &PortBindingsPage::OnBindingsContextMenu);
    right_layout->addWidget(m_bindings_list);

    m_hint_label = new QLabel(ways_box);
    m_hint_label->setWordWrap(true);
    Muted(m_hint_label);
    right_layout->addWidget(m_hint_label);

    auto* row_buttons = new QHBoxLayout();
    m_add_btn = new QPushButton(tr("Add Binding..."), ways_box);
    m_unmapped_btn = new QPushButton(tr("Mark as Unbound"), ways_box);
    m_remove_btn = new QPushButton(tr("Remove Binding"), ways_box);
    connect(m_add_btn, &QPushButton::clicked, this, &PortBindingsPage::OnAddWay);
    connect(m_unmapped_btn, &QPushButton::clicked, this, &PortBindingsPage::OnSetUnmapped);
    connect(m_remove_btn, &QPushButton::clicked, this, &PortBindingsPage::OnRemoveSelected);
    row_buttons->addWidget(m_add_btn);
    row_buttons->addWidget(m_unmapped_btn);
    row_buttons->addWidget(m_remove_btn);
    right_layout->addLayout(row_buttons);

    main_layout->addWidget(ways_box, 2);
    outer->addLayout(main_layout, 1);

    connect(m_output_list, &QListWidget::currentRowChanged, this, [this](int) {
        RefreshBindingsList();
        m_diagram->SetHighlightedOutput(QString::fromStdString(CurrentOutputName()));
    });

    PopulateOutputList();
    RefreshBindingsList();
    RefreshOutputMarkers();
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

bool PortBindingsPage::HasPinnedDevice() const {
    return DeviceForPort(m_port_number).pinned;
}

void PortBindingsPage::RefreshDeviceLabel() {
    if (m_device_label == nullptr) {
        return;
    }
    const auto device = DeviceForPort(m_port_number);
    m_device_label->setText(DeviceLineFor(m_port_number, device));
    if (device.pinned) {
        m_device_label->setPalette(QPalette());
    } else {
        Muted(m_device_label);
    }
}

bool PortBindingsPage::IsAnalogOutput(const std::string& name) {
    return std::any_of(Core::Input::kAnalogToAnalogOutputNames.begin(),
                       Core::Input::kAnalogToAnalogOutputNames.end(),
                       [&](std::string_view n) { return n == name; });
}

void PortBindingsPage::OnFilterChanged(const QString& text) {
    const QString needle = text.trimmed();
    QListWidgetItem* first_visible = nullptr;
    for (int i = 0; i < m_output_list->count(); i++) {
        auto* item = m_output_list->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            continue;
        }
        const bool matches = needle.isEmpty() || id.contains(needle, Qt::CaseInsensitive) ||
                             item->text().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!matches);
        if (matches && first_visible == nullptr) {
            first_visible = item;
        }
    }
    // A header with nothing left under it is just noise.
    QListWidgetItem* header = nullptr;
    bool header_has_rows = false;
    for (int i = 0; i < m_output_list->count(); i++) {
        auto* item = m_output_list->item(i);
        if (item->data(Qt::UserRole).toString().isEmpty()) {
            if (header != nullptr) {
                header->setHidden(!header_has_rows);
            }
            header = item;
            header_has_rows = false;
        } else if (!item->isHidden()) {
            header_has_rows = true;
        }
    }
    if (header != nullptr) {
        header->setHidden(!header_has_rows);
    }
    auto* current = m_output_list->currentItem();
    if ((current == nullptr || current->isHidden()) && first_visible != nullptr) {
        m_output_list->setCurrentItem(first_visible);
    }
}

QSet<QString> PortBindingsPage::OutputsBoundIn(Core::Input::BindingsConfig* config) const {
    QSet<QString> bound;
    if (config == nullptr) {
        return bound;
    }
    for (int i = 0; i < m_output_list->count(); i++) {
        const QString id = m_output_list->item(i)->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            continue;
        }
        const auto name = id.toStdString();
        const auto& bindings = config->GetBindings(name);
        const bool any = std::any_of(bindings.begin(), bindings.end(), [&](const auto& b) {
            if (b.input.size() == 1 && b.input.front() == "unmapped") {
                return false;
            }
            const int player = b.OutputPlayer();
            return player == 0 || player == m_port_number;
        });
        if (any) {
            bound.insert(id);
        }
    }
    return bound;
}

QSet<QString> PortBindingsPage::BoundOutputs() const {
    return OutputsBoundIn(m_config);
}

void PortBindingsPage::RefreshOutputMarkers() {
    const QSet<QString> bound = BoundOutputs();
    const QSet<QString> from_overlay = OutputsBoundIn(m_global_overlay);

    const int side = std::max(8, m_output_list->fontMetrics().height());
    const QIcon dot = BoundMarker(palette().color(QPalette::Highlight),
                                  palette().color(QPalette::HighlightedText), true, side);
    const QIcon overlay_dot =
        BoundMarker(MutedColor(palette()), palette().color(QPalette::Window), true, side);
    const QIcon blank = BoundMarker(Qt::transparent, Qt::transparent, false, side);
    for (int i = 0; i < m_output_list->count(); i++) {
        auto* item = m_output_list->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            continue; // a section header
        }
        if (bound.contains(id)) {
            item->setIcon(dot);
        } else if (from_overlay.contains(id)) {
            item->setIcon(overlay_dot);
        } else {
            item->setIcon(blank);
        }
    }
    m_diagram->SetBoundOutputs(bound + from_overlay);
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

void PortBindingsPage::SetOverlay(Core::Input::BindingsConfig* overlay, const QString& label) {
    m_global_overlay = overlay;
    m_overlay_label = label;
    RefreshBindingsList();
}

void PortBindingsPage::Reload() {
    RefreshBindingsList();
    RefreshOutputMarkers();
}

void PortBindingsPage::ShowPressed(const QString& name, bool pressed) {
    m_diagram->SetPressedControl(name, pressed);
}

void PortBindingsPage::ClearPressed() {
    m_diagram->ClearPressed();
}

void PortBindingsPage::RefreshBindingsList() {
    m_bindings_list->clear();
    const std::string name = CurrentOutputName();
    if (name.empty()) {
        m_hint_label->clear();
        return;
    }
    int shown = 0;
    m_row_to_binding.clear();
    const auto& bindings = m_config->GetBindings(name);
    for (int i = 0; i < static_cast<int>(bindings.size()); i++) {
        const auto& binding = bindings[i];
        const int player = binding.OutputPlayer();
        if (player != 0 && player != m_port_number) {
            continue;
        }
        QString label = DisplayChord(binding.input);
        if (player == 0) {
            label = tr("%1  (all ports)").arg(label);
        }
        if (const int device = binding.InputDevice(); device != 0) {
            label = tr("%1  (port %2 controller only)").arg(label).arg(device);
        }
        auto* item = new QListWidgetItem(label);
        if (player == 0) {
            item->setForeground(MutedColor(palette()));
        }
        m_bindings_list->addItem(item);
        m_row_to_binding.push_back(i);
        shown++;
    }

    int shown_global = 0;
    if (m_global_overlay) {
        for (const auto& binding : m_global_overlay->GetBindings(name)) {
            if (binding.OutputPlayer() == 0 || binding.OutputPlayer() == m_port_number) {
                auto* item = new QListWidgetItem(
                    tr("(from %1) %2").arg(m_overlay_label, DisplayChord(binding.input)));
                item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
                item->setForeground(MutedColor(palette()));
                m_bindings_list->addItem(item);
                shown_global++;
            }
        }
    }
    const bool analog = IsAnalogOutput(name);
    m_add_btn->setEnabled(true);
    m_unmapped_btn->setEnabled(true);

    if (analog) {
        m_hint_label->setText(
            tr("%1 needs a stick or trigger axis. Push one past halfway to capture it.")
                .arg(FriendlyOutputName(name)));
    } else if (shown > 0 || shown_global > 0) {
        m_hint_label->setText(
            tr("Bindings for port %1. Greyed rows apply to all ports. Right-click a row to "
               "set its allowed device.")
                .arg(m_port_number));
    } else {
        m_hint_label->setText(
            tr("%1 has no binding on port %2.").arg(FriendlyOutputName(name)).arg(m_port_number));
    }
}

void PortBindingsPage::OnAddWay() {
    const std::string name = CurrentOutputName();
    if (name.empty()) {
        return;
    }
    const auto device = DeviceForPort(m_port_number);
    QString reason;
    if (device.pinned) {
        const QString named = device.device_name.isEmpty()
                                  ? (device.guid == QStringLiteral("keyboard") ? tr("the keyboard")
                                                                               : tr("a controller"))
                                  : device.device_name;
        reason = tr("Port %1 is assigned to %2.").arg(m_port_number).arg(named);
    }
    KeyCaptureDialog capture(this, AcceptsForPort(device), reason);
    if (capture.exec() != QDialog::Accepted) {
        return;
    }
    auto bindings = m_config->GetBindings(name);
    Core::Input::PortedBinding new_binding;
    new_binding.input = capture.CapturedInput();
    new_binding.output_port = m_port_number;
    new_binding.gamepad_field = m_port_number; // only this port's controller presses it
    bindings.push_back(new_binding);
    m_config->SetBindings(name, bindings);
    RefreshBindingsList();
    RefreshOutputMarkers();
    emit BindingsChanged();
}

void PortBindingsPage::OnSetUnmapped() {
    const std::string name = CurrentOutputName();
    if (name.empty()) {
        return;
    }
    auto bindings = m_config->GetBindings(name);
    std::vector<Core::Input::PortedBinding> kept;
    for (auto& b : bindings) {
        if (b.OutputPlayer() != m_port_number) {
            kept.push_back(std::move(b));
        }
    }
    Core::Input::PortedBinding unmapped;
    unmapped.input = {"unmapped"};
    unmapped.output_port = m_port_number; // see OnAddWay
    kept.push_back(unmapped);
    m_config->SetBindings(name, kept);
    RefreshBindingsList();
    RefreshOutputMarkers();
    emit BindingsChanged();
}

void PortBindingsPage::OnBindingsContextMenu(const QPoint& pos) {
    const std::string name = CurrentOutputName();
    QListWidgetItem* item = m_bindings_list->itemAt(pos);
    if (name.empty() || item == nullptr) {
        return;
    }
    const int row = m_bindings_list->row(item);
    if (row < 0 || row >= static_cast<int>(m_row_to_binding.size())) {
        return; // a read-only overlay row
    }

    auto bindings = m_config->GetBindings(name);
    const int index = m_row_to_binding[static_cast<size_t>(row)];
    if (index < 0 || index >= static_cast<int>(bindings.size())) {
        return;
    }
    const int current = bindings[static_cast<size_t>(index)].InputDevice();

    QMenu menu(this);
    auto* heading = menu.addAction(tr("Allowed Device"));
    heading->setEnabled(false);
    menu.addSeparator();
    const auto add = [&](int device, const QString& label) {
        QAction* action = menu.addAction(label);
        action->setCheckable(true);
        action->setChecked(current == device);
        connect(action, &QAction::triggered, this, [this, name, index, device] {
            auto edited = m_config->GetBindings(name);
            if (index >= static_cast<int>(edited.size())) {
                return;
            }
            auto& binding = edited[static_cast<size_t>(index)];
            // A row with no output port takes its player from the device, so
            // changing the device would move it to another player. Fix it to
            // this port first; one that drove every port asks before narrowing.
            if (binding.output_port == 0) {
                if (binding.OutputPlayer() == 0 && device != 0 &&
                    QMessageBox::question(
                        this, tr("Allowed Device"),
                        tr("This binding applies to all ports. Limiting it to one controller "
                           "makes it apply to port %1 only. Continue?")
                            .arg(m_port_number),
                        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
                    return;
                }
                if (binding.OutputPlayer() != 0 || device != 0) {
                    binding.output_port = m_port_number;
                }
            }
            binding.gamepad_field = device;
            binding.input_port = 0;
            m_config->SetBindings(name, edited);
            RefreshBindingsList();
            RefreshOutputMarkers();
            emit BindingsChanged();
        });
    };
    add(0, tr("Any Device"));
    for (int device = 1; device <= 4; device++) {
        add(device, tr("Port %1 Controller Only").arg(device));
    }

    menu.exec(m_bindings_list->viewport()->mapToGlobal(pos));
}

void PortBindingsPage::OnRemoveSelected() {
    const std::string name = CurrentOutputName();
    const int row = m_bindings_list->currentRow();
    if (name.empty() || row < 0) {
        return;
    }
    if (row >= static_cast<int>(m_row_to_binding.size())) {
        return;
    }
    auto bindings = m_config->GetBindings(name);
    const int index = m_row_to_binding[static_cast<size_t>(row)];
    if (index < 0 || index >= static_cast<int>(bindings.size())) {
        return;
    }
    if (bindings[static_cast<size_t>(index)].OutputPlayer() == 0 &&
        QMessageBox::question(
            this, tr("Remove Binding"),
            tr("This binding applies to all ports, not just port %1. Remove it from all "
               "ports?")
                .arg(m_port_number),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    bindings.erase(bindings.begin() + index);
    m_config->SetBindings(name, bindings);
    RefreshBindingsList();
    RefreshOutputMarkers();
    emit BindingsChanged();
}

InputBindingsDialog::InputBindingsDialog(const std::filesystem::path& targetFile, QWidget* parent)
    : QDialog(parent),
      m_global_json_path(Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "global.json"),
      m_config(std::make_unique<Core::Input::BindingsConfig>()) {
    Core::Input::EnsureBindingsFiles();

    m_config->Load(targetFile);
    SeedFromDefaultsIfNew(targetFile);
    LoadOverlayFor(targetFile);
    BuildUi();
}

InputBindingsDialog::InputBindingsDialog(QWidget* parent)
    : InputBindingsDialog(Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "global.json",
                          parent) {}

void InputBindingsDialog::BuildUi() {
    setWindowTitle(tr("Input Bindings - %1")
                       .arg(QString::fromStdString(m_config->FilePath().filename().string())));
    setWindowIcon(TintedGamepadIcon());
    resize(860, 620);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(8);

    m_subtitle_label = new QLabel(this);
    m_subtitle_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    Muted(m_subtitle_label);
    outer->addWidget(m_subtitle_label);

    // File picker (section 2): global.json, or a specific game's own file.
    auto* picker_row = new QHBoxLayout();
    picker_row->addWidget(new QLabel(tr("Editing:"), this));
    m_file_picker = new QComboBox(this);
    picker_row->addWidget(m_file_picker, 1);
    auto* browse_btn = new QPushButton(tr("Choose Game..."), this);
    connect(browse_btn, &QPushButton::clicked, this, &InputBindingsDialog::OnBrowseForGame);
    picker_row->addWidget(browse_btn);
    outer->addLayout(picker_row);

    m_gamepad = new GamepadSelector(this);
    m_gamepad->HideChooser();
    m_gamepad->hide();

    connect(m_gamepad, &GamepadSelector::ControlPressed, this, [this](const QString& name) {
        if (auto* page = CurrentPage(); page != nullptr) {
            page->ShowPressed(name, true);
        }
    });
    connect(m_gamepad, &GamepadSelector::ControlReleased, this, [this](const QString& name) {
        if (auto* page = CurrentPage(); page != nullptr) {
            page->ShowPressed(name, false);
        }
    });

    connect(m_gamepad, &GamepadSelector::SelectionChanged, this, [this] {
        for (auto* page : m_pages) {
            if (page != nullptr) {
                page->ClearPressed();
                page->RefreshDeviceLabel();
            }
        }
        RefreshPortTabs();
    });

    PopulateFilePicker();
    connect(m_file_picker, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &InputBindingsDialog::OnFilePickerChanged);
    m_subtitle_label->setText(
        tr("Editing %1").arg(QString::fromStdString(m_config->FilePath().string())));

    m_unreadable_label = new QLabel(this);
    m_unreadable_label->setWordWrap(true);
    m_unreadable_label->setStyleSheet("color: #E05555; font-weight: bold;");
    outer->addWidget(m_unreadable_label);

    m_seeded_label = new QLabel(this);
    m_seeded_label->setWordWrap(true);
    Muted(m_seeded_label);
    m_seeded_label->setVisible(false);
    outer->addWidget(m_seeded_label);
    RefreshSeededBanner();

    m_tabs = new QTabWidget(this);
    const QIcon tab_icon = TintedGamepadIcon();
    for (int port = 1; port <= 4; port++) {
        auto* page = new PortBindingsPage(port, m_config.get(), this);
        m_pages[static_cast<size_t>(port - 1)] = page;
        page->SetOverlay(m_global_overlay.get(), m_overlay_label);
        m_tabs->addTab(page, tab_icon, tr("Port %1").arg(port));
        connect(page, &PortBindingsPage::BindingsChanged, this,
                &InputBindingsDialog::RefreshConflicts);
        connect(page, &PortBindingsPage::BindingsChanged, this,
                &InputBindingsDialog::RefreshProblemsList);
    }
    m_tabs->addTab(BuildSettingsPage(), tr("Settings"));
    outer->addWidget(m_tabs, 1);

    const auto follow_port_device = [this] {
        if (auto* page = CurrentPage(); page != nullptr) {
            page->ClearPressed();
            const auto device = DeviceForPort(page->PortNumber());
            if (device.pinned) {
                m_gamepad->SelectByGuid(device.guid);
            }
        }
    };
    connect(m_tabs, &QTabWidget::currentChanged, this, follow_port_device);
    follow_port_device(); // the tab open at start never emits currentChanged
    RefreshPortTabs();

    m_issues = new QTabWidget(this);
    m_issues->setMaximumHeight(112);

    auto* conflicts_page = new QWidget(m_issues);
    auto* conflicts_layout = new QVBoxLayout(conflicts_page);
    conflicts_layout->setContentsMargins(6, 6, 6, 6);
    conflicts_layout->setSpacing(4);
    m_conflicts_summary = new QLabel(conflicts_page);
    m_conflicts_summary->setWordWrap(true);
    conflicts_layout->addWidget(m_conflicts_summary);
    m_conflicts_list = new QListWidget(conflicts_page);
    m_conflicts_list->setAlternatingRowColors(true);
    conflicts_layout->addWidget(m_conflicts_list);
    m_issues->addTab(conflicts_page, tr("Conflicts"));

    m_problems_list = new QListWidget(m_issues);
    m_issues->addTab(m_problems_list, tr("Problems"));

    outer->addWidget(m_issues, 0);
    RefreshConflicts();
    RefreshProblemsList();

    auto* buttons = new QHBoxLayout();
    m_revert_btn = new QPushButton(tr("Revert"), this);
    m_revert_btn->setMinimumWidth(100);
    m_revert_btn->setToolTip(tr("Discard unsaved changes and reload the file."));
    m_save_btn = new QPushButton(tr("Save"), this);
    m_save_btn->setDefault(true);
    m_save_btn->setMinimumWidth(100);
    auto* close_btn = new QPushButton(tr("Close"), this);
    close_btn->setMinimumWidth(100);
    connect(m_revert_btn, &QPushButton::clicked, this, &InputBindingsDialog::OnRevert);
    connect(m_save_btn, &QPushButton::clicked, this, &InputBindingsDialog::OnSave);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(m_revert_btn);
    buttons->addStretch();
    buttons->addWidget(m_save_btn);
    buttons->addWidget(close_btn);
    outer->addLayout(buttons);

    RefreshLoadedState();
}

void InputBindingsDialog::RefreshLoadedState() {
    const bool loaded = m_config->IsLoaded();
    const QString path = QString::fromStdString(m_config->FilePath().string());

    m_unreadable_label->setVisible(!loaded);
    m_unreadable_label->setText(
        loaded ? QString()
               : tr("Could not read %1. It may not be valid JSON. Editing is disabled so "
                    "the file is not overwritten.")
                     .arg(path));
    m_save_btn->setEnabled(loaded);
    m_revert_btn->setEnabled(loaded);
    for (auto* page : m_pages) {
        if (page != nullptr) {
            page->setEnabled(loaded);
        }
    }
}

bool InputBindingsDialog::ConfirmDiscardingEdits() {
    if (!m_config->IsLoaded() || !m_config->HasUnsavedChanges()) {
        return true;
    }
    const auto answer = QMessageBox::question(
        this, tr("Input Bindings"),
        tr("%1 has unsaved changes. Save them?")
            .arg(QString::fromStdString(m_config->FilePath().filename().string())),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Save) {
        return m_config->Save();
    }
    return true;
}

void InputBindingsDialog::OnRevert() {
    if (m_config->HasUnsavedChanges() &&
        QMessageBox::question(
            this, tr("Revert"),
            tr("Discard unsaved changes to %1 and reload it?")
                .arg(QString::fromStdString(m_config->FilePath().filename().string())),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    // Load() clears the edit cache, so re-loading the same path is the revert.
    // A file not yet on disk goes back to where it started: seeded from default.json.
    const auto path = m_config->FilePath();
    m_config->Load(path);
    SeedFromDefaultsIfNew(path);
    for (auto* page : m_pages) {
        page->Reload();
    }
    ReloadSettingsTab();
    RefreshLoadedState();
    RefreshSeededBanner();
    RefreshConflicts();
    RefreshProblemsList();
}

void InputBindingsDialog::closeEvent(QCloseEvent* event) {
    if (ConfirmDiscardingEdits()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void InputBindingsDialog::reject() {
    if (ConfirmDiscardingEdits()) {
        QDialog::reject();
    }
}

bool InputBindingsDialog::SeedFromDefaultsIfNew(const std::filesystem::path& path) {
    m_seeded_from_defaults = false;

    const auto defaults_path =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "default.json";
    if (path == m_global_json_path || path == defaults_path) {
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return false; // an existing file is the person's; leave it alone
    }
    if (!std::filesystem::is_regular_file(defaults_path, ec)) {
        return false;
    }

    Core::Input::BindingsConfig defaults;
    if (!defaults.Load(defaults_path) || !defaults.IsLoaded()) {
        return false;
    }

    std::map<std::string, std::vector<Core::Input::PortedBinding>> by_output;
    for (const auto& flat : defaults.GetAllBindings()) {
        by_output[flat.output].push_back(flat.binding);
    }
    if (by_output.empty()) {
        return false;
    }
    for (auto& [output, list] : by_output) {
        m_config->SetBindings(output, std::move(list));
    }
    m_seeded_from_defaults = true;
    return true;
}

void InputBindingsDialog::RefreshSeededBanner() {
    if (m_seeded_label == nullptr) {
        return;
    }
    m_seeded_label->setVisible(m_seeded_from_defaults);
    if (m_seeded_from_defaults) {
        m_seeded_label->setText(
            tr("New file, based on default.json. A game's file replaces the defaults instead "
               "of adding to them. Save to keep these bindings."));
    }
}

void InputBindingsDialog::LoadOverlayFor(const std::filesystem::path& path) {
    const auto defaults_path =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "default.json";

    std::filesystem::path overlay_path;
    if (path == m_global_json_path) {
        overlay_path = defaults_path;
        m_overlay_label = tr("default.json");
    } else {
        overlay_path = m_global_json_path;
        m_overlay_label = tr("global.json");
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(overlay_path, ec)) {
        m_global_overlay.reset();
        m_overlay_label.clear();
        return;
    }
    if (!m_global_overlay) {
        m_global_overlay = std::make_unique<Core::Input::BindingsConfig>();
    }
    if (!m_global_overlay->Load(overlay_path)) {
        m_global_overlay.reset();
        m_overlay_label.clear();
    }
}

PortBindingsPage* InputBindingsDialog::CurrentPage() const {
    for (auto* page : m_pages) {
        if (page != nullptr && page == m_tabs->currentWidget()) {
            return page;
        }
    }
    return nullptr; // the Settings tab
}

void InputBindingsDialog::RefreshPortTabs() {
    for (int port = 1; port <= 4; port++) {
        auto* page = m_pages[static_cast<size_t>(port - 1)];
        if (page == nullptr) {
            continue;
        }
        const int index = m_tabs->indexOf(page);
        if (index < 0) {
            continue;
        }
        const bool pinned = page->HasPinnedDevice();
        m_tabs->tabBar()->setTabTextColor(index, pinned ? QColor() : MutedColor(m_tabs->palette()));
        const auto device = DeviceForPort(port);
        m_tabs->setTabToolTip(index, DeviceLineFor(port, device));
    }
}

void InputBindingsDialog::RefreshConflicts() {
    if (m_conflicts_list == nullptr) {
        return; // called from BuildUi before the box exists
    }
    m_conflicts_list->clear();
    if (!m_config->IsLoaded()) {
        m_conflicts_summary->clear();
        return;
    }

    auto all = m_config->GetAllBindings();
    if (m_global_overlay && m_global_overlay->IsLoaded()) {
        auto global_bindings = m_global_overlay->GetAllBindings();
        all.insert(all.end(), global_bindings.begin(), global_bindings.end());
    }
    const auto conflicts = Core::Input::FindConflicts(all);
    if (m_issues != nullptr) {
        m_issues->setTabText(0, conflicts.empty() ? tr("Conflicts")
                                                  : tr("Conflicts (%1)").arg(conflicts.size()));
    }
    if (conflicts.empty()) {
        m_conflicts_summary->setText(tr("\u2713 No conflicts: no two bindings share the exact "
                                        "same keys for different controls."));
        m_conflicts_summary->setStyleSheet("color: #4CAF50;");
        return;
    }

    m_conflicts_summary->setStyleSheet("color: #E0A030; font-weight: bold;");
    m_conflicts_summary->setText(
        tr("%n conflict(s): these bindings use the same keys for different controls, so "
           "both fire together.",
           "", static_cast<int>(conflicts.size())));

    for (const auto& c : conflicts) {
        QStringList keys;
        for (const auto& k : c.keys) {
            keys << QString::fromStdString(k);
        }
        const QString port_text = c.port == 0 ? tr("all ports") : tr("port %1").arg(c.port);
        auto* item = new QListWidgetItem(tr("%1 vs %2: both use %3 (%4)")
                                             .arg(QString::fromStdString(c.output_a))
                                             .arg(QString::fromStdString(c.output_b))
                                             .arg(keys.join(" + "))
                                             .arg(port_text));
        item->setForeground(QColor("#E0A030"));
        m_conflicts_list->addItem(item);
    }
}

void InputBindingsDialog::RefreshProblemsList() {
    if (m_problems_list == nullptr) {
        return;
    }
    m_problems_list->clear();
    if (!m_config->IsLoaded()) {
        return;
    }
    for (const auto& w : m_config->Validate()) {
        m_problems_list->addItem(QString::fromStdString(w));
    }
    if (m_global_overlay && m_global_overlay->IsLoaded()) {
        for (const auto& w : m_global_overlay->Validate()) {
            m_problems_list->addItem(tr("(%1) %2").arg(m_overlay_label, QString::fromStdString(w)));
        }
    }
    if (m_issues != nullptr) {
        const int count = m_problems_list->count();
        m_issues->setTabText(1, count == 0 ? tr("Problems") : tr("Problems (%1)").arg(count));
    }
}

void InputBindingsDialog::OnSave() {
    if (!m_config->Save()) {
        QMessageBox::critical(
            this, tr("Input Bindings"),
            tr("Failed to write %1.").arg(QString::fromStdString(m_config->FilePath().string())));
        return;
    }
    m_seeded_from_defaults = false;
    RefreshSeededBanner();
    QMessageBox::information(this, tr("Input Bindings"), tr("Saved."));
}

void InputBindingsDialog::PopulateFilePicker() {
    m_file_picker->blockSignals(true);
    m_file_picker->clear();
    m_file_picker->addItem(tr("All games — extra bindings (global.json)"),
                           QString::fromStdString(m_global_json_path.string()));

    const auto configs_dir = Common::FS::GetUserPath(Common::FS::PathType::CustomInputConfigs);
    std::error_code ec;
    if (std::filesystem::exists(configs_dir, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(configs_dir, ec)) {
            if (entry.path().extension() != ".json") {
                continue;
            }
            const QString serial = QString::fromStdString(entry.path().stem().string());
            m_file_picker->addItem(tr("Game: %1").arg(serial),
                                   QString::fromStdString(entry.path().string()));
        }
    }

    const QString current_path = QString::fromStdString(m_config->FilePath().string());
    const int idx = m_file_picker->findData(current_path);
    if (idx >= 0) {
        m_file_picker->setCurrentIndex(idx);
    } else {
        const QString label =
            m_config->FilePath() == m_global_json_path
                ? tr("All games — extra bindings (global.json)")
                : tr("Game: %1").arg(QString::fromStdString(m_config->FilePath().stem().string()));
        m_file_picker->addItem(label, current_path);
        m_file_picker->setCurrentIndex(m_file_picker->count() - 1);
    }
    m_file_picker->blockSignals(false);
}

void InputBindingsDialog::OnFilePickerChanged(int index) {
    if (index < 0) {
        return;
    }
    const std::filesystem::path path =
        Common::FS::PathFromQString(m_file_picker->itemData(index).toString());
    if (path == m_config->FilePath()) {
        return;
    }
    SwitchTarget(path);
}

void InputBindingsDialog::OnBrowseForGame() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select a Game Folder"));
    if (dir.isEmpty()) {
        return;
    }
    const GameInfo info = GameInfoTools::readGameInfo(Common::FS::PathFromQString(dir));
    if (info.serial.empty()) {
        QMessageBox::warning(this, tr("Input Bindings"),
                             tr("Could not read a title ID from that folder."));
        return;
    }
    const auto target =
        Common::FS::GetUserPath(Common::FS::PathType::CustomInputConfigs) / (info.serial + ".json");
    SwitchTarget(target);
}

void InputBindingsDialog::SwitchTarget(const std::filesystem::path& path) {
    if (!ConfirmDiscardingEdits()) {
        // Put the picker back on the file we are still editing.
        PopulateFilePicker();
        return;
    }
    m_config->Load(path);
    SeedFromDefaultsIfNew(path);
    LoadOverlayFor(path);

    setWindowTitle(tr("Input Bindings - %1").arg(QString::fromStdString(path.filename().string())));
    m_subtitle_label->setText(tr("Editing %1").arg(QString::fromStdString(path.string())));

    for (auto* page : m_pages) {
        page->SetOverlay(m_global_overlay.get(), m_overlay_label);
        page->Reload();
    }
    ReloadSettingsTab();
    RefreshLoadedState();
    RefreshSeededBanner();
    RefreshConflicts();
    RefreshProblemsList();
    PopulateFilePicker();
}

void InputBindingsDialog::ReloadSettingsTab() {
    const QSignalBlocker block_joystick(m_mouse_to_joystick);
    const QSignalBlocker block_deadzone_offset(m_mouse_deadzone_offset);
    const QSignalBlocker block_speed(m_mouse_speed);
    const QSignalBlocker block_speed_offset(m_mouse_speed_offset);
    std::vector<std::unique_ptr<QSignalBlocker>> block_spins;
    block_spins.reserve(m_deadzone_spins.size());
    for (auto* spin : m_deadzone_spins) {
        block_spins.push_back(std::make_unique<QSignalBlocker>(spin));
    }

    bool mouse_present = false;
    const auto mouse = m_config->GetMouseSettings(&mouse_present);
    const int mouse_idx = m_mouse_to_joystick->findData(QString::fromStdString(mouse.to_joystick));
    m_mouse_to_joystick->setCurrentIndex(mouse_idx >= 0 ? mouse_idx : 0);
    m_mouse_deadzone_offset->setValue(mouse.deadzone_offset);
    m_mouse_speed->setValue(mouse.speed);
    m_mouse_speed_offset->setValue(mouse.speed_offset);

    bool dz_present = false;
    const auto dz = m_config->GetDeadzoneSettings(&dz_present);
    m_deadzone_spins[0]->setValue(dz.left_stick.min);
    m_deadzone_spins[1]->setValue(dz.left_stick.max);
    m_deadzone_spins[2]->setValue(dz.right_stick.min);
    m_deadzone_spins[3]->setValue(dz.right_stick.max);
    m_deadzone_spins[4]->setValue(dz.left_trigger.min);
    m_deadzone_spins[5]->setValue(dz.left_trigger.max);
    m_deadzone_spins[6]->setValue(dz.right_trigger.min);
    m_deadzone_spins[7]->setValue(dz.right_trigger.max);
    RefreshSettingsTitles(mouse_present, dz_present);
}

void InputBindingsDialog::RefreshSettingsTitles(bool mouse_present, bool deadzones_present) {
    m_mouse_box->setTitle(mouse_present ? tr("Mouse to Joystick")
                                        : tr("Mouse to Joystick (defaults, not set in this file)"));
    m_deadzone_box->setTitle(deadzones_present ? tr("Deadzones")
                                               : tr("Deadzones (defaults, not set in this file)"));
}

QWidget* InputBindingsDialog::BuildSettingsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // --- Mouse to Joystick (section 8) ---
    auto* mouse_box = new QGroupBox(page);
    m_mouse_box = mouse_box;
    auto* mouse_form = new QFormLayout(mouse_box);
    m_mouse_to_joystick = new QComboBox(mouse_box);
    m_mouse_to_joystick->addItem(tr("Right stick"), QStringLiteral("right"));
    m_mouse_to_joystick->addItem(tr("Left stick"), QStringLiteral("left"));
    m_mouse_to_joystick->addItem(tr("Off"), QStringLiteral("none"));
    m_mouse_deadzone_offset = new QDoubleSpinBox(mouse_box);
    m_mouse_deadzone_offset->setDecimals(3);
    m_mouse_deadzone_offset->setRange(0.0, 1.0);
    m_mouse_deadzone_offset->setSingleStep(0.05);
    m_mouse_speed = new QDoubleSpinBox(mouse_box);
    m_mouse_speed->setDecimals(3);
    m_mouse_speed->setRange(0.01, 10.0);
    m_mouse_speed->setSingleStep(0.1);
    m_mouse_speed_offset = new QDoubleSpinBox(mouse_box);
    m_mouse_speed_offset->setDecimals(3);
    m_mouse_speed_offset->setRange(0.0, 5.0);
    m_mouse_speed_offset->setSingleStep(0.05);
    mouse_form->addRow(tr("Drives:"), m_mouse_to_joystick);
    mouse_form->addRow(tr("Deadzone offset:"), m_mouse_deadzone_offset);
    mouse_form->addRow(tr("Speed:"), m_mouse_speed);
    mouse_form->addRow(tr("Speed offset:"), m_mouse_speed_offset);
    layout->addWidget(mouse_box);

    bool mouse_present = false;
    const auto mouse = m_config->GetMouseSettings(&mouse_present);
    const int mouse_idx = m_mouse_to_joystick->findData(QString::fromStdString(mouse.to_joystick));
    m_mouse_to_joystick->setCurrentIndex(mouse_idx >= 0 ? mouse_idx : 0);
    m_mouse_deadzone_offset->setValue(mouse.deadzone_offset);
    m_mouse_speed->setValue(mouse.speed);
    m_mouse_speed_offset->setValue(mouse.speed_offset);

    auto commit_mouse = [this] {
        Core::Input::MouseSettings s;
        s.to_joystick = m_mouse_to_joystick->currentData().toString().toStdString();
        s.deadzone_offset = m_mouse_deadzone_offset->value();
        s.speed = m_mouse_speed->value();
        s.speed_offset = m_mouse_speed_offset->value();
        m_config->SetMouseSettings(s);
    };
    connect(m_mouse_to_joystick, qOverload<int>(&QComboBox::currentIndexChanged), this,
            commit_mouse);
    connect(m_mouse_deadzone_offset, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            commit_mouse);
    connect(m_mouse_speed, qOverload<double>(&QDoubleSpinBox::valueChanged), this, commit_mouse);
    connect(m_mouse_speed_offset, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            commit_mouse);

    // --- Deadzones (section 8) ---
    auto* dz_box = new QGroupBox(page);
    m_deadzone_box = dz_box;
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
        // 0-127, not 0-255 (input-bindings.md section 8). The engine's axes are
        // signed -128..127, so a max above 127 is a value the stick can never
        // reach: the dead zone would swallow the whole travel.
        auto* min_spin = new QSpinBox(dz_box);
        min_spin->setRange(0, 127);
        min_spin->setValue(range.min);
        auto* max_spin = new QSpinBox(dz_box);
        max_spin->setRange(0, 127);
        max_spin->setValue(range.max);
        row_layout->addWidget(new QLabel(tr("min"), dz_box));
        row_layout->addWidget(min_spin);
        row_layout->addWidget(new QLabel(tr("max"), dz_box));
        row_layout->addWidget(max_spin);
        dz_form->addRow(label, row_layout);
        m_deadzone_spins[spin_idx++] = min_spin;
        m_deadzone_spins[spin_idx++] = max_spin;
    }
    RefreshSettingsTitles(mouse_present, dz_present);
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
