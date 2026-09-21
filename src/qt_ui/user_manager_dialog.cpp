// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QHeaderView>
#include <QtWidgets>
#include <common/path_util.h>
#include <core/user_settings.h>
#include "core/emulator_settings.h"
#include "gui_settings.h"
#include "table_item_delegate.h"
#include "common/input.h"
#include "gamepad_selector.h"
#include "user_manager_dialog.h"

UserManagerDialog::UserManagerDialog(std::shared_ptr<GUISettings> gui_settings,
                                     std::shared_ptr<EmulatorSettingsImpl> emulator_settings,
                                     QWidget* parent)
    : QDialog(parent), m_gui_settings(std::move(gui_settings)),
      m_emu_settings(std::move(emulator_settings)) {
    setWindowTitle(tr("User Manager"));
    setMinimumSize(QSize(900, 400));
    setModal(true);

    // Table
    m_table = new QTableWidget(this);
    m_table->setItemDelegate(new TableItemDelegate(this));
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setColumnCount(5); // User ID, Name, Color, Port, Pinned device
    m_table->setCornerButtonEnabled(false);
    m_table->setAlternatingRowColors(true);
    m_table->setHorizontalHeaderLabels(
        {"User ID", "User Name", "Color", "Controller Port", "Pinned Device"});
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setDefaultSectionSize(150);
    m_table->installEventFilter(this);

    // Buttons
    push_create_user = new QPushButton(tr("&Create User"), this);
    push_create_user->setAutoDefault(false);

    push_remove_user = new QPushButton(tr("&Delete User"), this);
    push_remove_user->setAutoDefault(false);

    push_rename_user = new QPushButton(tr("&Rename User"), this);
    push_rename_user->setAutoDefault(false);

    push_set_default = new QPushButton(tr("&Set Default User"), this);
    push_set_default->setAutoDefault(false);

    push_set_color = new QPushButton(tr("&Set Color"), this);
    push_set_color->setAutoDefault(false);

    push_set_controller = new QPushButton(tr("&Set Controller Port"), this);
    push_set_controller->setAutoDefault(false);
    push_assign_device = new QPushButton(tr("&Assign Device"), this);
    push_assign_device->setAutoDefault(false);
    push_assign_device->setToolTip(
        tr("Pin a physical controller, or the keyboard, to this user's port."));
    push_clear_device = new QPushButton(tr("&Clear Pinned Device"), this);
    push_clear_device->setAutoDefault(false);
    push_clear_device->setToolTip(
        tr("Forget which physical controller drives this user's port, so the port is filled "
          "in plug order again."));

    push_close = new QPushButton(tr("&Close"), this);
    push_close->setAutoDefault(false);

    // Button Layout
    QHBoxLayout* hbox_buttons = new QHBoxLayout();
    hbox_buttons->addWidget(push_create_user);
    hbox_buttons->addWidget(push_remove_user);
    hbox_buttons->addWidget(push_rename_user);
    hbox_buttons->addWidget(push_set_default);
    hbox_buttons->addWidget(push_set_color);
    hbox_buttons->addWidget(push_set_controller);
    hbox_buttons->addWidget(push_assign_device);
    hbox_buttons->addWidget(push_clear_device);
    hbox_buttons->addStretch();
    hbox_buttons->addWidget(push_close);

    // Main Layout
    QVBoxLayout* vbox_main = new QVBoxLayout();
    vbox_main->addWidget(m_table);
    vbox_main->addLayout(hbox_buttons);
    setLayout(vbox_main);

    m_active_user = UserManagement.GetDefaultUser().user_id;
    UpdateTable();

    restoreGeometry(m_gui_settings->GetValue(GUI::user_manager_geometry).toByteArray());

    // Button enabling lambda
    auto enable_buttons = [this]() {
        const u32 key = GetUserKey();
        bool valid = key != 0;
        push_remove_user->setEnabled(valid && key != m_active_user);
        push_rename_user->setEnabled(valid);
        push_set_default->setEnabled(valid && key != m_active_user);
        push_set_color->setEnabled(valid);
        push_set_controller->setEnabled(valid);
        push_assign_device->setEnabled(valid);
        // Only meaningful when there is actually a pin to clear.
        const User* selected = valid ? UserManagement.GetUserByID(GetUserKey()) : nullptr;
        push_clear_device->setEnabled(selected != nullptr &&
                                      !DescribePinnedDevice(*selected).isEmpty());
    };

    enable_buttons();

    connect(push_create_user, &QAbstractButton::clicked, this, &UserManagerDialog::OnUserCreate);
    connect(push_remove_user, &QAbstractButton::clicked, this, &UserManagerDialog::OnUserRemove);
    connect(push_rename_user, &QAbstractButton::clicked, this, &UserManagerDialog::OnUserRename);
    connect(push_set_default, &QAbstractButton::clicked, this,
            &UserManagerDialog::OnUserSetDefault);
    connect(push_set_color, &QAbstractButton::clicked, this, &UserManagerDialog::OnUserSetColor);
    connect(push_set_controller, &QAbstractButton::clicked, this,
            &UserManagerDialog::OnUserSetControllerPort);
    connect(push_assign_device, &QAbstractButton::clicked, this,
            &UserManagerDialog::OnUserAssignDevice);
    connect(push_clear_device, &QAbstractButton::clicked, this,
            &UserManagerDialog::OnUserClearPinnedDevice);
    connect(push_close, &QAbstractButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, enable_buttons);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this,
            &UserManagerDialog::OnSort);
    connect(m_table, &QTableWidget::customContextMenuRequested, this,
            &UserManagerDialog::ShowContextMenu);
}

void UserManagerDialog::UpdateTable(bool mark_only) {
    QFont bold_font;
    bold_font.setBold(true);

    const auto& users = UserManagement.GetAllUsers();

    if (mark_only) {
        for (int i = 0; i < m_table->rowCount(); ++i) {
            for (int col = 0; col < m_table->columnCount(); ++col) {
                QTableWidgetItem* item = m_table->item(i, col);
                if (item) {
                    bool is_active =
                        (m_active_user == m_table->item(i, 0)->data(Qt::UserRole).toUInt());
                    item->setFont(is_active ? bold_font : QFont());
                }
            }
        }
        return;
    }

    m_table->setRowCount(static_cast<int>(users.size()));

    for (int row = 0; row < users.size(); ++row) {
        const User& u = users[row];

        // User ID
        QTableWidgetItem* id_item = new QTableWidgetItem(QString::number(u.user_id));
        id_item->setData(Qt::UserRole, u.user_id);
        id_item->setFlags(id_item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, id_item);

        // Username
        QTableWidgetItem* username_item = new QTableWidgetItem(QString::fromStdString(u.user_name));
        username_item->setData(Qt::UserRole, u.user_id);
        username_item->setFlags(username_item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 1, username_item);

        // Color
        QTableWidgetItem* color_item = new QTableWidgetItem();
        color_item->setFlags(color_item->flags() & ~Qt::ItemIsEditable);
        color_item->setData(Qt::DecorationRole, GetQColorFromIndex(u.user_color));
        m_table->setItem(row, 2, color_item);

        // Controller port
        QString controller_text =
            (u.player_index >= 1 && u.player_index <= 4) ? QString::number(u.player_index) : "-";
        QTableWidgetItem* controller_item = new QTableWidgetItem(controller_text);
        controller_item->setFlags(controller_item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 3, controller_item);

        // Pinned device
        const QString pinned = DescribePinnedDevice(u);
        QTableWidgetItem* device_item = new QTableWidgetItem(pinned.isEmpty() ? "-" : pinned);
        device_item->setFlags(device_item->flags() & ~Qt::ItemIsEditable);
        if (!pinned.isEmpty()) {
            device_item->setToolTip(tr("GUID: %1\nSerial: %2\nPath: %3")
                                        .arg(QString::fromStdString(u.device_guid),
                                             QString::fromStdString(u.device_serial),
                                             QString::fromStdString(u.device_path)));
        }
        m_table->setItem(row, 4, device_item);

        // Bold if active
        bool is_active = (m_active_user == u.user_id);
        if (is_active) {
            id_item->setFont(bold_font);
            username_item->setFont(bold_font);
            color_item->setFont(bold_font);
            controller_item->setFont(bold_font);
            device_item->setFont(bold_font);
        }
    }

    // Resize headers
    m_table->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
}

u32 UserManagerDialog::GetUserKey() const {
    int row = m_table->currentRow();
    if (row < 0)
        return 0;
    const QTableWidgetItem* item = m_table->item(row, 0);
    if (!item)
        return 0;

    bool ok = false;
    u32 id = item->data(Qt::UserRole).toUInt(&ok);
    if (!ok)
        return 0;

    const auto& users = UserManagement.GetAllUsers();
    auto it =
        std::find_if(users.begin(), users.end(), [id](const User& u) { return u.user_id == id; });
    return (it != users.end()) ? id : 0;
}

void UserManagerDialog::OnUserCreate() {
    const auto& users = UserManagement.GetAllUsers();

    if (users.size() >= 16) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot add more users."));
        return;
    }

    s32 new_id = 1000;
    for (const auto& u : users) {
        if (u.user_id >= new_id)
            new_id = u.user_id + 1;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Create New User"));
    dialog.setModal(true);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("New User ID: %1").arg(new_id)));
    layout->addWidget(new QLabel(tr("Username (3–16 chars, letters, numbers, _, -)")));
    QLineEdit* edit = new QLineEdit(&dialog);
    edit->setValidator(
        new QRegularExpressionValidator(QRegularExpression("^[A-Za-z0-9_-]{3,16}$")));
    layout->addWidget(edit);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, [&]() {
        QString name = edit->text().trimmed();
        if (!edit->hasAcceptableInput()) {
            QMessageBox::warning(&dialog, tr("Invalid Username"),
                                 tr("Username must be 3–16 chars and valid."));
            return;
        }
        User u;
        u.user_id = new_id;
        u.user_name = name.toStdString();
        u.user_color = 1; // 1-based palette: 1 = Blue
        u.player_index = -1;
        UserManagement.AddUser(u);
        UpdateTable();
        dialog.accept();
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void UserManagerDialog::OnUserRemove() {
    u32 id = GetUserKey();
    if (id == 0)
        return;
    if (QMessageBox::question(this, tr("Delete Confirmation"), tr("Delete user ID %1?").arg(id),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) == QMessageBox::Yes) {
        UserManagement.RemoveUser(id);
        UpdateTable();
    }
}

void UserManagerDialog::OnUserRename() {
    u32 id = GetUserKey();
    if (id == 0)
        return;
    User* user = UserManagement.GetUserByID(id);
    if (!user)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Rename User"));
    dialog.setMinimumWidth(300);
    QVBoxLayout layout(&dialog);
    layout.addWidget(
        new QLabel(tr("Old Username: %1").arg(QString::fromStdString(user->user_name))));
    QLineEdit edit(QString::fromStdString(user->user_name));
    layout.addWidget(&edit);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);

    auto ok_button = buttons.button(QDialogButtonBox::Ok);
    ok_button->setEnabled(false);
    QRegularExpression regex("^[A-Za-z0-9_-]{3,16}$");
    QObject::connect(&edit, &QLineEdit::textChanged, [&]() {
        ok_button->setEnabled(regex.match(edit.text().trimmed()).hasMatch());
    });
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        UserManagement.RenameUser(id, edit.text().trimmed().toStdString());
        UpdateTable();
    }
}

void UserManagerDialog::OnUserSetDefault() {
    u32 id = GetUserKey();
    if (id == 0)
        return;
    UserManagement.SetDefaultUser(id);
    m_active_user = id;
    UpdateTable();
}

void UserManagerDialog::OnUserSetColor() {
    u32 id = GetUserKey();
    if (id == 0)
        return;
    User* user = UserManagement.GetUserByID(id);
    if (!user)
        return;

    QStringList colors = {"Blue", "Red", "Green", "Pink"};
    bool ok = false;
    // user_color is 1-based; the picker list is 0-based. Convert both ways.
    const int current =
        std::clamp(static_cast<int>(user->user_color) - 1, 0, static_cast<int>(colors.size()) - 1);
    QString color = QInputDialog::getItem(this, tr("Set User Color"), tr("Select color:"), colors,
                                          current, false, &ok);
    if (ok) {
        user->user_color = static_cast<u32>(colors.indexOf(color) + 1);
        UserManagement.Save(); // persist immediately
        UpdateTable();
    }
}

void UserManagerDialog::OnUserSetControllerPort() {
    const u32 user_id = GetUserKey();
    if (user_id == 0)
        return;

    // Current port of the selected user
    User* user = UserManagement.GetUserByID(user_id);
    if (!user)
        return;

    bool ok = false;
    int new_port =
        QInputDialog::getInt(this, tr("Set Controller Port"), tr("Assign port (1-4) to this user:"),
                             user->player_index > 0 ? user->player_index : 1, // default
                             1, 4, 1, &ok);

    if (ok) {
        UserManagement.SetControllerPort(user_id, new_port);
        UpdateTable();
    }
}

QString UserManagerDialog::DescribePinnedDevice(const User& user) {
    if (user.device_guid.empty()) {
        return {};
    }
    if (user.device_guid == "keyboard") {
        return tr("Keyboard");
    }
    const QString guid = QString::fromStdString(user.device_guid).right(16);
    if (!user.device_serial.empty()) {
        return tr("%1 (serial %2)").arg(guid, QString::fromStdString(user.device_serial));
    }
    if (!user.device_path.empty()) {
        return tr("%1 (port %2)").arg(guid, QString::fromStdString(user.device_path));
    }
    return guid;
}

void UserManagerDialog::OnUserAssignDevice() {
    const u32 user_id = GetUserKey();
    if (user_id == 0) {
        return;
    }
    User* user = UserManagement.GetUserByID(user_id);
    if (user == nullptr) {
        return;
    }

    if (user->player_index < 1 || user->player_index > 4) {
        QMessageBox::information(
            this, tr("Assign Device"),
            tr("%1 holds no controller port, so a device pinned to them would never match "
              "anything.\n\nSet a controller port first.")
                .arg(QString::fromStdString(user->user_name)));
        return;
    }

    QDialog picker(this);
    picker.setWindowTitle(tr("Assign Device to %1").arg(QString::fromStdString(user->user_name)));
    auto* layout = new QVBoxLayout(&picker);

    auto* keyboard_radio = new QRadioButton(tr("Keyboard"), &picker);
    auto* pad_radio = new QRadioButton(tr("Controller:"), &picker);
    pad_radio->setChecked(true);
    auto* selector = new GamepadSelector(&picker);

    layout->addWidget(new QLabel(tr("Which device should drive port %1?").arg(user->player_index),
                                 &picker));
    layout->addWidget(pad_radio);
    layout->addWidget(selector);
    layout->addWidget(keyboard_radio);

    auto* note = new QLabel(&picker);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &picker);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &picker, &QDialog::reject);

    const auto refresh_note = [&] {
        const bool pad = pad_radio->isChecked();
        selector->setEnabled(pad);
        if (!pad) {
            note->clear();
            buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
            return;
        }
        buttons->button(QDialogButtonBox::Ok)->setEnabled(selector->HasGamepad());
        if (!selector->HasGamepad()) {
            note->setText(tr("No controller is connected."));
        } else if (selector->SelectionIsAmbiguous()) {
            note->setText(tr("This pad reports no serial number and no port path, so another "
                            "one of the same model would match this pin too."));
        } else {
            note->clear();
        }
    };
    connect(pad_radio, &QRadioButton::toggled, &picker, refresh_note);
    connect(selector, &GamepadSelector::SelectionChanged, &picker, refresh_note);
    refresh_note();

    if (picker.exec() != QDialog::Accepted) {
        return;
    }

    std::string guid;
    std::string serial;
    std::string path;
    if (keyboard_radio->isChecked()) {
        guid = "keyboard";
    } else {
        guid = selector->SelectedGuid().toStdString();
        serial = selector->SelectedSerial().toStdString();
        path = selector->SelectedPath().toStdString();
    }
    if (guid.empty()) {
        return;
    }

    // One device, one port: warn before quietly taking it off someone else.
    for (const auto& other : UserManagement.GetAllUsers()) {
        if (other.user_id != static_cast<s32>(user_id) && other.device_guid == guid) {
            if (QMessageBox::question(
                    this, tr("Assign Device"),
                    tr("That device is currently pinned to %1. Move it to %2?")
                        .arg(QString::fromStdString(other.user_name),
                             QString::fromStdString(user->user_name)),
                    QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
            break;
        }
    }

    UserManagement.SetPinnedDevice(user_id, guid, serial, path);
    UpdateTable();
}

void UserManagerDialog::OnUserClearPinnedDevice() {
    const u32 user_id = GetUserKey();
    if (user_id == 0) {
        return;
    }
    const User* user = UserManagement.GetUserByID(user_id);
    if (user == nullptr || DescribePinnedDevice(*user).isEmpty()) {
        return;
    }

    if (QMessageBox::question(
            this, tr("Clear Pinned Device"),
            tr("Forget that %1 drives %2's port?\n\nThe port will be filled in plug order "
              "again until something claims it.")
                .arg(DescribePinnedDevice(*user), QString::fromStdString(user->user_name)),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    UserManagement.ClearPinnedDevice(user_id);
    UpdateTable();
}

void UserManagerDialog::OnSort(int logicalIndex) {
    if (logicalIndex < 0) {
        return;
    } else if (logicalIndex == m_sort_column) {
        m_sort_ascending ^= true;
    } else {
        m_sort_ascending = true;
    }
    m_sort_column = logicalIndex;
    m_table->sortByColumn(m_sort_column,
                          m_sort_ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
}

void UserManagerDialog::closeEvent(QCloseEvent* event) {
    UserSettings.Save();
    m_gui_settings->SetValue(GUI::user_manager_geometry, saveGeometry());
    QDialog::closeEvent(event);
}

void UserManagerDialog::ShowContextMenu(const QPoint& pos) {
    const u32 key = GetUserKey();
    if (key == 0) {
        return;
    }

    QMenu* context_menu = new QMenu();

    QAction* remove_act = context_menu->addAction(tr("&Delete User"));
    QAction* rename_act = context_menu->addAction(tr("&Rename User"));
    QAction* default_user_act = context_menu->addAction(tr("&Set Default User"));
    QAction* color_act = context_menu->addAction(tr("&Set Color"));
    QAction* port_act = context_menu->addAction(tr("&Set Controller Port"));
    QAction* assign_device_act = context_menu->addAction(tr("&Assign Device"));
    QAction* clear_device_act = context_menu->addAction(tr("&Clear Pinned Device"));
    QAction* show_dir_act = context_menu->addAction(tr("&Open User Directory"));

    bool enabled = key != m_active_user; // don't allow removing or setting default on active user

    remove_act->setEnabled(enabled);
    rename_act->setEnabled(enabled);

    const User* selected = UserManagement.GetUserByID(key);
    clear_device_act->setEnabled(selected != nullptr &&
                                 !DescribePinnedDevice(*selected).isEmpty());

    // Connects and Events
    connect(remove_act, &QAction::triggered, this, &UserManagerDialog::OnUserRemove);
    connect(rename_act, &QAction::triggered, this, &UserManagerDialog::OnUserRename);
    connect(default_user_act, &QAction::triggered, this, &UserManagerDialog::OnUserSetDefault);
    connect(color_act, &QAction::triggered, this, &UserManagerDialog::OnUserSetColor);
    connect(port_act, &QAction::triggered, this, &UserManagerDialog::OnUserSetControllerPort);
    connect(assign_device_act, &QAction::triggered, this,
            &UserManagerDialog::OnUserAssignDevice);
    connect(clear_device_act, &QAction::triggered, this,
            &UserManagerDialog::OnUserClearPinnedDevice);

    connect(show_dir_act, &QAction::triggered, this, [this, key]() {
        QString userDirPath;
        Common::FS::PathToQString(userDirPath, EmulatorSettings.GetHomeDir() / std::to_string(key));
        QDesktopServices::openUrl(QUrl::fromLocalFile(userDirPath));
    });

    context_menu->exec(m_table->viewport()->mapToGlobal(pos));
}