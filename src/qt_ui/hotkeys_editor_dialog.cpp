// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include <QFont>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyleHints>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "common/input.h"
#include "core/input/input_ids.h"
#include "gamepad_selector.h"
#include "hotkeys_editor_dialog.h"
#include "sdl_event_wrapper.h"

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

QString FriendlyHotkeyName(const std::string& id) {
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

KeyCaptureDialog::KeyCaptureDialog(QWidget* parent, Accepts accepts, const QString& reason)
    : QDialog(parent), m_accepts(accepts), m_reason(reason) {
    setWindowTitle(tr("Press Keys"));
    setModal(true);
    setFocusPolicy(Qt::StrongFocus);

    // One SDL reader for the whole process; this dialog just listens.
    SdlEventWrapper::Wrapper::Acquire();
    connect(SdlEventWrapper::Wrapper::GetInstance(), &SdlEventWrapper::Wrapper::SDLEvent, this,
            &KeyCaptureDialog::OnSdlEvent);
    OpenSelectedGamepad();

    auto* layout = new QVBoxLayout(this);
    QString what;
    switch (m_accepts) {
    case Accepts::KeyboardAndMouse:
        what = tr("Press up to 3 keys or mouse buttons together, then click Done.");
        break;
    case Accepts::Gamepad:
        what = tr("Press up to 3 pad buttons, or a stick or trigger, then click Done.");
        break;
    case Accepts::Any:
        what = tr("Press up to 3 keys, mouse buttons, pad buttons or a stick together, "
                  "then click Done.");
        break;
    }
    auto* instructions = new QLabel(what, this);
    instructions->setWordWrap(true);
    layout->addWidget(instructions);

    m_rule_label = new QLabel(this);
    m_rule_label->setWordWrap(true);
    m_rule_label->setAlignment(Qt::AlignCenter);
    {
        QPalette pal = m_rule_label->palette();
        const QColor text = pal.color(QPalette::WindowText);
        const QColor back = pal.color(QPalette::Window);
        pal.setColor(QPalette::WindowText,
                     QColor::fromRgbF(text.redF() * 0.55 + back.redF() * 0.45,
                                      text.greenF() * 0.55 + back.greenF() * 0.45,
                                      text.blueF() * 0.55 + back.blueF() * 0.45));
        m_rule_label->setPalette(pal);
    }
    m_rule_label->setText(m_reason);
    m_rule_label->setVisible(!m_reason.isEmpty());
    layout->addWidget(m_rule_label);

    auto* pad_row = new QHBoxLayout();
    auto* pad_icon = new QLabel(this);
    pad_icon->setPixmap(TintedGamepadIcon().pixmap(20, 20));
    pad_row->addStretch();
    pad_row->addWidget(pad_icon);
    m_gamepad_label = new QLabel(this);
    m_gamepad_label->setText(
        m_gamepad ? tr("Pad detected: %1").arg(QString::fromUtf8(SDL_GetGamepadName(m_gamepad)))
                  : tr("No pad detected"));
    pad_row->addWidget(m_gamepad_label);
    pad_row->addStretch();
    layout->addLayout(pad_row);

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
}

KeyCaptureDialog::~KeyCaptureDialog() {
    if (m_gamepad) {
        SDL_CloseGamepad(m_gamepad);
        m_gamepad = nullptr;
    }
    SdlEventWrapper::Wrapper::Release();
}

void KeyCaptureDialog::OpenSelectedGamepad() {
    if (m_gamepad) {
        SDL_CloseGamepad(m_gamepad);
        m_gamepad = nullptr;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0) {
        int index =
            GamepadSelect::GetIndexfromGUID(ids, count, GamepadSelect::GetSelectedGamepad());
        if (index == -1) {
            index = 0;
        }
        m_gamepad = SDL_OpenGamepad(ids[index]);
    }
    if (ids) {
        SDL_free(ids);
    }
    if (m_gamepad_label) {
        m_gamepad_label->setText(
            m_gamepad ? tr("Pad detected: %1").arg(QString::fromUtf8(SDL_GetGamepadName(m_gamepad)))
                      : tr("No pad detected"));
    }
}

void KeyCaptureDialog::OnSdlEvent(int type, int input, int value) {
    switch (type) {
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
        OpenSelectedGamepad();
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        TryCapture(GamepadButtonName(static_cast<SDL_GamepadButton>(input)).toStdString());
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const auto axis = static_cast<SDL_GamepadAxis>(input);
        if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            bool& held = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? m_l2_pressed : m_r2_pressed;
            const bool pressed = value > 16000;
            if (pressed && !held) {
                TryCapture(GamepadAxisName(axis).toStdString());
            }
            held = pressed;
            break;
        }
        if (value > 16000 || value < -16000) {
            if (!m_axis_captured) {
                m_axis_captured = true;
                TryCapture(GamepadAxisName(axis).toStdString());
            }
        } else if (value > -8000 && value < 8000) {
            m_axis_captured = false;
        }
        break;
    }
    default:
        break;
    }
}

bool KeyCaptureDialog::Allows(const std::string& name) const {
    switch (m_accepts) {
    case Accepts::KeyboardAndMouse:
        return Core::Input::IsKnownKeyboardOrMouseInput(name);
    case Accepts::Gamepad:
        return Core::Input::IsKnownPadInputName(name);
    case Accepts::Any:
        break;
    }
    return true;
}

void KeyCaptureDialog::TryCapture(const std::string& name) {
    if (name.empty() || m_captured.size() >= 3) {
        return;
    }
    if (!Allows(name)) {
        if (m_rule_label != nullptr) {
            const QString what = m_accepts == Accepts::KeyboardAndMouse
                                     ? tr("a key or mouse button")
                                     : tr("a pad control");
            m_rule_label->setText(
                tr("\"%1\" cannot reach this port. %2Press %3.")
                    .arg(QString::fromStdString(name),
                         m_reason.isEmpty() ? QString() : m_reason + QStringLiteral(" "), what));
            m_rule_label->setVisible(true);
        }
        return;
    }
    if (std::find(m_captured.begin(), m_captured.end(), name) == m_captured.end()) {
        m_captured.push_back(name);
        UpdatePreview();
    }
}

std::string KeyCaptureDialog::NameForKeyEvent(QKeyEvent* event) {
    const int key = event->key();

    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return std::string(1, 'a' + (key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return std::string(1, '0' + (key - Qt::Key_0));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return "f" + std::to_string(key - Qt::Key_F1 + 1);
    }

    if (event->modifiers() & Qt::KeypadModifier) {
        switch (key) {
        case Qt::Key_0:
        case Qt::Key_1:
        case Qt::Key_2:
        case Qt::Key_3:
        case Qt::Key_4:
        case Qt::Key_5:
        case Qt::Key_6:
        case Qt::Key_7:
        case Qt::Key_8:
        case Qt::Key_9:
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
    case Qt::Key_QuoteLeft:
        return "grave";
    // Distinct ids in the emulator: grave is SDLK_BACKQUOTE, tilde is '~'.
    case Qt::Key_AsciiTilde:
        return "tilde";
    case Qt::Key_Exclam:
        return "exclamation";
    case Qt::Key_At:
        return "at";
    case Qt::Key_NumberSign:
        return "hash";
    case Qt::Key_Dollar:
        return "dollar";
    case Qt::Key_Percent:
        return "percent";
    case Qt::Key_AsciiCircum:
        return "caret";
    case Qt::Key_Ampersand:
        return "ampersand";
    case Qt::Key_Asterisk:
        return "asterisk";
    case Qt::Key_ParenLeft:
        return "lparen";
    case Qt::Key_ParenRight:
        return "rparen";
    case Qt::Key_Minus:
        return "minus";
    case Qt::Key_Underscore:
        return "underscore";
    case Qt::Key_Equal:
        return "equals";
    case Qt::Key_Plus:
        return "plus";
    case Qt::Key_BracketLeft:
        return "lbracket";
    case Qt::Key_BracketRight:
        return "rbracket";
    case Qt::Key_BraceLeft:
        return "lbrace";
    case Qt::Key_BraceRight:
        return "rbrace";
    case Qt::Key_Backslash:
        return "backslash";
    case Qt::Key_Bar:
        return "pipe";
    case Qt::Key_Semicolon:
        return "semicolon";
    case Qt::Key_Colon:
        return "colon";
    case Qt::Key_Apostrophe:
        return "apostrophe";
    case Qt::Key_QuoteDbl:
        return "quote";
    case Qt::Key_Comma:
        return "comma";
    case Qt::Key_Less:
        return "less";
    case Qt::Key_Period:
        return "period";
    case Qt::Key_Greater:
        return "greater";
    case Qt::Key_Slash:
        return "slash";
    case Qt::Key_Question:
        return "question";

    case Qt::Key_Escape:
        return "escape";
    case Qt::Key_Print:
        return "printscreen";
    case Qt::Key_ScrollLock:
        return "scrolllock";
    case Qt::Key_Pause:
        return "pausebreak";
    case Qt::Key_Backspace:
        return "backspace";
    case Qt::Key_Delete:
        return "delete";
    case Qt::Key_Insert:
        return "insert";
    case Qt::Key_Home:
        return "home";
    case Qt::Key_End:
        return "end";
    case Qt::Key_PageUp:
        return "pgup";
    case Qt::Key_PageDown:
        return "pgdown";
    case Qt::Key_Tab:
        return "tab";
    case Qt::Key_CapsLock:
        return "capslock";
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return "enter";
    case Qt::Key_Space:
        return "space";
    case Qt::Key_Up:
        return "up";
    case Qt::Key_Down:
        return "down";
    case Qt::Key_Left:
        return "left";
    case Qt::Key_Right:
        return "right";

    case Qt::Key_Shift:
        return event->nativeScanCode() == LSHIFT_KEY ? "lshift" : "rshift";
    case Qt::Key_Control:
        return event->nativeScanCode() == LCTRL_KEY ? "lctrl" : "rctrl";
    case Qt::Key_Alt:
        return event->nativeScanCode() == LALT_KEY ? "lalt" : "ralt";
    case Qt::Key_Meta:
#ifdef _WIN32
        return event->nativeVirtualKey() == 0x5C /* VK_RWIN */ ? "rwin" : "lwin";
#elif defined(__linux__) || defined(__FreeBSD__)
        switch (event->nativeVirtualKey()) {
        case 0xffec: // XK_Super_R
        case 0xffe8: // XK_Meta_R
            return "rmeta";
        default:
            return "lmeta";
        }
#else
        return "lmeta";
#endif
    default:
        return {};
    }
}

void KeyCaptureDialog::wheelEvent(QWheelEvent* event) {
    const QPoint delta = event->angleDelta();
    const bool alt = (event->modifiers() & Qt::AltModifier) != 0;

    if (delta.y() > 5) {
        TryCapture("mousewheelup");
    } else if (delta.y() < -5) {
        TryCapture("mousewheeldown");
    }

    if (delta.x() > 5) {
        TryCapture(alt ? "mousewheelup" : "mousewheelright");
    } else if (delta.x() < -5) {
        TryCapture(alt ? "mousewheeldown" : "mousewheelleft");
    }
    event->accept();
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
}

void KeyCaptureDialog::mousePressEvent(QMouseEvent* event) {
    if (m_captured.size() >= 3) {
        return;
    }
    std::string name;
    switch (event->button()) {
    case Qt::LeftButton:
        name = "leftbutton";
        break;
    case Qt::RightButton:
        name = "rightbutton";
        break;
    case Qt::MiddleButton:
        name = "middlebutton";
        break;
    case Qt::BackButton:
        name = "sidebuttonback";
        break;
    case Qt::ForwardButton:
        name = "sidebuttonforward";
        break;
    default:
        return;
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
HotkeysEditorDialog::HotkeysEditorDialog(QWidget* parent)
    : QDialog(parent), m_config(std::make_unique<Core::Input::HotkeysConfig>()) {
    setWindowTitle(tr("Hotkeys"));
    resize(560, 420);

    m_config->Load();

    auto* outer = new QVBoxLayout(this);

    outer->addWidget(new GamepadSelector(this));

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
    auto* unmapped_btn = new QPushButton(tr("Set to Unmapped"), this);
    auto* remove_btn = new QPushButton(tr("Remove selected"), this);
    connect(add_btn, &QPushButton::clicked, this, &HotkeysEditorDialog::OnAddWay);
    connect(unmapped_btn, &QPushButton::clicked, this, &HotkeysEditorDialog::OnSetUnmapped);
    connect(remove_btn, &QPushButton::clicked, this, &HotkeysEditorDialog::OnRemoveSelected);
    row_buttons->addWidget(add_btn);
    row_buttons->addWidget(unmapped_btn);
    row_buttons->addWidget(remove_btn);
    right_layout->addLayout(row_buttons);

    main_layout->addLayout(right_layout, 2);
    outer->addLayout(main_layout);

    auto* problems_box = new QGroupBox(tr("Problems"), this);
    auto* problems_layout = new QVBoxLayout(problems_box);
    m_problems_list = new QListWidget(this);
    m_problems_list->setMaximumHeight(90);
    problems_layout->addWidget(m_problems_list);
    outer->addWidget(problems_box);

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
    RefreshProblemsList();
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
        tr("Default: %1")
            .arg(QString::fromUtf8(Core::Input::DefaultDisplayFor(name).data(),
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
    RefreshProblemsList();
}

void HotkeysEditorDialog::OnSetUnmapped() {
    const std::string name = CurrentHotkeyName();
    if (name.empty()) {
        return;
    }
    Core::Input::HotkeyBinding binding;
    binding.input = {"unmapped"};
    m_config->SetBindings(name, {binding});
    RefreshBindingsList();
    RefreshProblemsList();
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
    RefreshProblemsList();
}

void HotkeysEditorDialog::RefreshProblemsList() {
    m_problems_list->clear();
    const auto warnings = m_config->Validate();
    for (const auto& w : warnings) {
        m_problems_list->addItem(QString::fromStdString(w));
    }
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
    RefreshProblemsList();
}

void HotkeysEditorDialog::OnSave() {
    if (!m_config->Save()) {
        QMessageBox::critical(this, tr("Hotkeys"), tr("Failed to write hotkeys.json."));
        return;
    }
    QMessageBox::information(this, tr("Hotkeys"), tr("Saved."));
}
