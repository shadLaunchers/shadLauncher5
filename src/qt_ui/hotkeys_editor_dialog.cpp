// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "core/input/input_ids.h"
#include "hotkeys_editor_dialog.h"

namespace {

QString FriendlyHotkeyName(const std::string& id) {
    // "hotkey_toggle_mouse_to_joystick" -> "Toggle Mouse To Joystick"
    QString s = QString::fromStdString(id);
    s.remove(0, s.startsWith("hotkey_") ? 7 : 0);
    s.replace('_', ' ');
    QStringList words = s.split(' ', Qt::SkipEmptyParts);
    for (QString& w : words) {
        if (!w.isEmpty()) {
            w[0] = w[0].toUpper();
        }
    }
    return words.join(' ');
}

} // namespace

// ---------------------------------------------------------------------
// KeyCaptureDialog
// ---------------------------------------------------------------------

KeyCaptureDialog::KeyCaptureDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Press Keys"));
    setModal(true);
    setFocusPolicy(Qt::StrongFocus);

    // Same lifecycle shadLauncher4's ControlSettings uses (control_settings.cpp):
    // init the subsystems this dialog needs, scoped to its own lifetime.
    SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
    OpenFirstGamepad();

    auto* layout = new QVBoxLayout(this);
    auto* instructions =
        new QLabel(tr("Press up to 3 keys, mouse buttons, or pad buttons together, then "
                      "click Done."),
                   this);
    instructions->setWordWrap(true);
    layout->addWidget(instructions);

    m_gamepad_label = new QLabel(this);
    m_gamepad_label->setAlignment(Qt::AlignCenter);
    m_gamepad_label->setText(m_gamepad ? tr("Pad detected: %1").arg(
                                             QString::fromUtf8(SDL_GetGamepadName(m_gamepad)))
                                       : tr("No pad detected"));
    layout->addWidget(m_gamepad_label);

    m_preview_label = new QLabel(tr("(nothing captured yet)"), this);
    m_preview_label->setAlignment(Qt::AlignCenter);
    QFont f = m_preview_label->font();
    f.setPointSize(f.pointSize() + 4);
    f.setBold(true);
    m_preview_label->setFont(f);
    layout->addWidget(m_preview_label);

    auto* buttons = new QHBoxLayout();
    auto* clear_btn = new QPushButton(tr("Clear"), this);
    auto* done_btn = new QPushButton(tr("Done"), this);
    auto* cancel_btn = new QPushButton(tr("Cancel"), this);
    connect(clear_btn, &QPushButton::clicked, this, [this] {
        m_captured.clear();
        UpdatePreview();
    });
    connect(done_btn, &QPushButton::clicked, this, [this] {
        if (!m_captured.empty()) {
            accept();
        }
    });
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(clear_btn);
    buttons->addStretch();
    buttons->addWidget(cancel_btn);
    buttons->addWidget(done_btn);
    layout->addLayout(buttons);

    resize(420, 200);

    // Poll on the UI thread instead of shadLauncher4's background
    // SdlEventWrapper thread -- this dialog is short-lived and modal, so
    // there's no persistent window that needs SDL events delivered while
    // the rest of the UI stays responsive.
    m_poll_timer = new QTimer(this);
    connect(m_poll_timer, &QTimer::timeout, this, &KeyCaptureDialog::PollGamepad);
    m_poll_timer->start(16);
}

KeyCaptureDialog::~KeyCaptureDialog() {
    if (m_poll_timer) {
        m_poll_timer->stop();
    }
    if (m_gamepad) {
        SDL_CloseGamepad(m_gamepad);
        m_gamepad = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
}

void KeyCaptureDialog::OpenFirstGamepad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0) {
        m_gamepad = SDL_OpenGamepad(ids[0]);
    }
    if (ids) {
        SDL_free(ids);
    }
}

std::string KeyCaptureDialog::NameForGamepadButton(SDL_GamepadButton button) {
    // Matches shadLauncher4's ControlSettings::processSDLEvents mapping
    // (control_settings.cpp) and docs/input-bindings.md section 7's pad
    // vocabulary.
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "cross";
    case SDL_GAMEPAD_BUTTON_EAST: return "circle";
    case SDL_GAMEPAD_BUTTON_WEST: return "square";
    case SDL_GAMEPAD_BUTTON_NORTH: return "triangle";
    case SDL_GAMEPAD_BUTTON_BACK: return "back";
    case SDL_GAMEPAD_BUTTON_START: return "options";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "l3";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "r3";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "l1";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "r1";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "pad_up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "pad_down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "pad_left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "pad_right";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "touchpad_center";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "lpaddle_high";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "lpaddle_low";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "rpaddle_high";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "rpaddle_low";
    default: return {};
    }
}

void KeyCaptureDialog::TryCapture(const std::string& name) {
    if (name.empty() || m_captured.size() >= 3) {
        return;
    }
    if (std::find(m_captured.begin(), m_captured.end(), name) == m_captured.end()) {
        m_captured.push_back(name);
        UpdatePreview();
    }
}

void KeyCaptureDialog::PollGamepad() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!m_gamepad) {
                OpenFirstGamepad();
                if (m_gamepad_label) {
                    m_gamepad_label->setText(
                        m_gamepad ? tr("Pad detected: %1").arg(
                                        QString::fromUtf8(SDL_GetGamepadName(m_gamepad)))
                                 : tr("No pad detected"));
                }
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            TryCapture(NameForGamepadButton(
                static_cast<SDL_GamepadButton>(event.gbutton.button)));
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            // L2/R2 are axes, not buttons (input-bindings.md section 7).
            // Same half-press threshold shadLauncher4 uses.
            if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                const bool pressed = event.gaxis.value > 16000;
                if (pressed && !m_l2_pressed) {
                    TryCapture("l2");
                }
                m_l2_pressed = pressed;
            } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                const bool pressed = event.gaxis.value > 16000;
                if (pressed && !m_r2_pressed) {
                    TryCapture("r2");
                }
                m_r2_pressed = pressed;
            }
            break;
        default:
            break;
        }
    }
}

std::string KeyCaptureDialog::NameForKeyEvent(QKeyEvent* event) {
    const int key = event->key();

    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return std::string(1, 'a' + (key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        // Numpad digits are distinguished by the keypad modifier, not a
        // different Qt::Key -- handled below before this generic digit case
        // would otherwise be reached, so this only fires for the top-row keys.
        return std::string(1, '0' + (key - Qt::Key_0));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return "f" + std::to_string(key - Qt::Key_F1 + 1);
    }

    if (event->modifiers() & Qt::KeypadModifier) {
        switch (key) {
        case Qt::Key_0: case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4:
        case Qt::Key_5: case Qt::Key_6: case Qt::Key_7: case Qt::Key_8: case Qt::Key_9:
            return "kp" + std::to_string(key - Qt::Key_0);
        case Qt::Key_Period:
            return "kpperiod";
        case Qt::Key_Comma:
            return "kpcomma";
        case Qt::Key_Slash:
            return "kpslash";
        case Qt::Key_Asterisk:
            return "kpasterisk";
        case Qt::Key_Minus:
            return "kpminus";
        case Qt::Key_Plus:
            return "kpplus";
        case Qt::Key_Equal:
            return "kpequals";
        case Qt::Key_Enter:
            return "kpenter";
        default:
            break;
        }
    }

    switch (key) {
    case Qt::Key_QuoteLeft: return "grave";
    case Qt::Key_Exclam: return "exclamation";
    case Qt::Key_At: return "at";
    case Qt::Key_NumberSign: return "hash";
    case Qt::Key_Dollar: return "dollar";
    case Qt::Key_Percent: return "percent";
    case Qt::Key_AsciiCircum: return "caret";
    case Qt::Key_Ampersand: return "ampersand";
    case Qt::Key_Asterisk: return "asterisk";
    case Qt::Key_ParenLeft: return "lparen";
    case Qt::Key_ParenRight: return "rparen";
    case Qt::Key_Minus: return "minus";
    case Qt::Key_Underscore: return "underscore";
    case Qt::Key_Equal: return "equals";
    case Qt::Key_Plus: return "plus";
    case Qt::Key_BracketLeft: return "lbracket";
    case Qt::Key_BracketRight: return "rbracket";
    case Qt::Key_BraceLeft: return "lbrace";
    case Qt::Key_BraceRight: return "rbrace";
    case Qt::Key_Backslash: return "backslash";
    case Qt::Key_Bar: return "pipe";
    case Qt::Key_Semicolon: return "semicolon";
    case Qt::Key_Colon: return "colon";
    case Qt::Key_Apostrophe: return "apostrophe";
    case Qt::Key_QuoteDbl: return "quote";
    case Qt::Key_Comma: return "comma";
    case Qt::Key_Less: return "less";
    case Qt::Key_Period: return "period";
    case Qt::Key_Greater: return "greater";
    case Qt::Key_Slash: return "slash";
    case Qt::Key_Question: return "question";

    case Qt::Key_Escape: return "escape";
    case Qt::Key_Print: return "printscreen";
    case Qt::Key_ScrollLock: return "scrolllock";
    case Qt::Key_Pause: return "pausebreak";
    case Qt::Key_Backspace: return "backspace";
    case Qt::Key_Delete: return "delete";
    case Qt::Key_Insert: return "insert";
    case Qt::Key_Home: return "home";
    case Qt::Key_End: return "end";
    case Qt::Key_PageUp: return "pgup";
    case Qt::Key_PageDown: return "pgdown";
    case Qt::Key_Tab: return "tab";
    case Qt::Key_CapsLock: return "capslock";
    case Qt::Key_Return:
    case Qt::Key_Enter: return "enter";
    case Qt::Key_Space: return "space";
    case Qt::Key_Up: return "up";
    case Qt::Key_Down: return "down";
    case Qt::Key_Left: return "left";
    case Qt::Key_Right: return "right";

    case Qt::Key_Shift:
        // Qt doesn't reliably tell left/right apart across platforms from
        // the portable API alone; nativeVirtualKey() would (VK_LSHIFT vs
        // VK_RSHIFT on Windows), but isn't used here to keep this platform-
        // neutral. Left is assumed -- if this becomes a problem in practice,
        // add a platform-specific check here.
        return "lshift";
    case Qt::Key_Control: return "lctrl";
    case Qt::Key_Alt: return "lalt";
    case Qt::Key_Meta: return "lmeta";
    default:
        return {};
    }
}

void KeyCaptureDialog::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        return;
    }
    const std::string name = NameForKeyEvent(event);
    if (name.empty()) {
        return;
    }
    if (m_captured.size() >= 3) {
        return; // section 6: a chord is up to 3 keys
    }
    if (std::find(m_captured.begin(), m_captured.end(), name) == m_captured.end()) {
        m_captured.push_back(name);
        UpdatePreview();
    }
}

void KeyCaptureDialog::keyReleaseEvent(QKeyEvent* event) {
    Q_UNUSED(event);
    // Deliberately not clearing on release: the person may be pressing keys
    // one at a time to build the chord (e.g. hold ctrl, then tap f9). They
    // confirm with Done or start over with Clear.
}

void KeyCaptureDialog::mousePressEvent(QMouseEvent* event) {
    if (m_captured.size() >= 3) {
        return;
    }
    std::string name;
    switch (event->button()) {
    case Qt::LeftButton: name = "leftbutton"; break;
    case Qt::RightButton: name = "rightbutton"; break;
    case Qt::MiddleButton: name = "middlebutton"; break;
    case Qt::BackButton: name = "sidebuttonback"; break;
    case Qt::ForwardButton: name = "sidebuttonforward"; break;
    default: return;
    }
    if (std::find(m_captured.begin(), m_captured.end(), name) == m_captured.end()) {
        m_captured.push_back(name);
        UpdatePreview();
    }
}

void KeyCaptureDialog::UpdatePreview() {
    if (m_captured.empty()) {
        m_preview_label->setText(tr("(nothing captured yet)"));
        return;
    }
    QStringList parts;
    for (const auto& n : m_captured) {
        parts << QString::fromStdString(n);
    }
    m_preview_label->setText(parts.join(" + "));
}

// ---------------------------------------------------------------------
// HotkeysEditorDialog
// ---------------------------------------------------------------------

HotkeysEditorDialog::HotkeysEditorDialog(QWidget* parent)
    : QDialog(parent), m_config(std::make_unique<Core::Input::HotkeysConfig>()) {
    setWindowTitle(tr("Hotkeys"));
    resize(560, 420);

    m_config->Load();

    auto* outer = new QVBoxLayout(this);

    auto* main_layout = new QHBoxLayout();

    auto* left_layout = new QVBoxLayout();
    left_layout->addWidget(new QLabel(tr("Hotkey"), this));
    m_hotkey_list = new QListWidget(this);
    left_layout->addWidget(m_hotkey_list);
    main_layout->addLayout(left_layout, 1);

    auto* right_layout = new QVBoxLayout();
    right_layout->addWidget(new QLabel(tr("Ways to trigger it"), this));
    m_bindings_list = new QListWidget(this);
    right_layout->addWidget(m_bindings_list);

    m_default_label = new QLabel(this);
    m_default_label->setWordWrap(true);
    right_layout->addWidget(m_default_label);

    auto* row_buttons = new QHBoxLayout();
    auto* add_btn = new QPushButton(tr("Add a way..."), this);
    auto* remove_btn = new QPushButton(tr("Remove selected"), this);
    connect(add_btn, &QPushButton::clicked, this, &HotkeysEditorDialog::OnAddWay);
    connect(remove_btn, &QPushButton::clicked, this, &HotkeysEditorDialog::OnRemoveSelected);
    row_buttons->addWidget(add_btn);
    row_buttons->addWidget(remove_btn);
    right_layout->addLayout(row_buttons);

    main_layout->addLayout(right_layout, 2);
    outer->addLayout(main_layout);

    auto* bottom_buttons = new QHBoxLayout();
    auto* reset_btn = new QPushButton(tr("Reset All to Defaults..."), this);
    auto* save_btn = new QPushButton(tr("Save"), this);
    auto* close_btn = new QPushButton(tr("Close"), this);
    connect(reset_btn, &QPushButton::clicked, this, &HotkeysEditorDialog::OnResetToDefaults);
    connect(save_btn, &QPushButton::clicked, this, &HotkeysEditorDialog::OnSave);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    bottom_buttons->addWidget(reset_btn);
    bottom_buttons->addStretch();
    bottom_buttons->addWidget(save_btn);
    bottom_buttons->addWidget(close_btn);
    outer->addLayout(bottom_buttons);

    connect(m_hotkey_list, &QListWidget::currentRowChanged, this,
            [this](int) { RefreshBindingsList(); });

    PopulateHotkeyList();
    if (m_hotkey_list->count() > 0) {
        m_hotkey_list->setCurrentRow(0);
    }
}

void HotkeysEditorDialog::PopulateHotkeyList() {
    m_hotkey_list->clear();
    for (const auto& def : Core::Input::kKnownHotkeys) {
        auto* item = new QListWidgetItem(FriendlyHotkeyName(std::string(def.name)));
        item->setData(Qt::UserRole, QString::fromLatin1(def.name.data(), int(def.name.size())));
        m_hotkey_list->addItem(item);
    }
}

std::string HotkeysEditorDialog::CurrentHotkeyName() const {
    const auto* item = m_hotkey_list->currentItem();
    if (!item) {
        return {};
    }
    return item->data(Qt::UserRole).toString().toStdString();
}

QString HotkeysEditorDialog::DisplayChord(const std::vector<std::string>& input) {
    QStringList parts;
    for (const auto& n : input) {
        parts << QString::fromStdString(n);
    }
    return parts.join(" + ");
}

void HotkeysEditorDialog::RefreshBindingsList() {
    m_bindings_list->clear();
    const std::string name = CurrentHotkeyName();
    if (name.empty()) {
        m_default_label->clear();
        return;
    }
    for (const auto& binding : m_config->GetBindings(name)) {
        m_bindings_list->addItem(DisplayChord(binding.input));
    }
    m_default_label->setText(
        tr("Default: %1").arg(QString::fromUtf8(Core::Input::DefaultDisplayFor(name).data(),
                                                 int(Core::Input::DefaultDisplayFor(name).size()))));
}

void HotkeysEditorDialog::OnAddWay() {
    const std::string name = CurrentHotkeyName();
    if (name.empty()) {
        return;
    }
    KeyCaptureDialog capture(this);
    if (capture.exec() != QDialog::Accepted) {
        return;
    }
    auto bindings = m_config->GetBindings(name);
    Core::Input::HotkeyBinding new_binding;
    new_binding.input = capture.CapturedInput();
    bindings.push_back(new_binding);
    m_config->SetBindings(name, bindings);
    RefreshBindingsList();
}

void HotkeysEditorDialog::OnRemoveSelected() {
    const std::string name = CurrentHotkeyName();
    const int row = m_bindings_list->currentRow();
    if (name.empty() || row < 0) {
        return;
    }
    auto bindings = m_config->GetBindings(name);
    if (row >= static_cast<int>(bindings.size())) {
        return;
    }
    bindings.erase(bindings.begin() + row);
    m_config->SetBindings(name, bindings);
    RefreshBindingsList();
}

void HotkeysEditorDialog::OnResetToDefaults() {
    const auto answer = QMessageBox::question(
        this, tr("Reset Hotkeys"),
        tr("This deletes hotkeys.json entirely -- the emulator will regenerate all ten "
          "defaults the next time it runs. Any custom rebinding you've made will be lost. "
          "Continue?"));
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (!m_config->ResetToDefaults()) {
        QMessageBox::critical(this, tr("Reset Hotkeys"), tr("Failed to remove hotkeys.json."));
        return;
    }
    RefreshBindingsList();
}

void HotkeysEditorDialog::OnSave() {
    if (!m_config->Save()) {
        QMessageBox::critical(this, tr("Hotkeys"), tr("Failed to write hotkeys.json."));
        return;
    }
    QMessageBox::information(this, tr("Hotkeys"), tr("Saved."));
}
