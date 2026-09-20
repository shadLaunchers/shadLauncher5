// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The one place SDL's event queue is read, turning pad events into Qt
// signals. Ported from shadLauncher4's src/qt_ui/sdl_event_wrapper.h, with
// the same SDLEvent(type, input, value) signal so the handlers translate
// across unchanged.
//
// Two differences from shadLauncher4, both because of how this launcher uses
// it:
//
//  - It polls on a QTimer on the GUI thread rather than blocking in
//    SDL_WaitEvent on a QFuture worker. shadLauncher4's dialog is the only
//    SDL reader in its process; here the bindings dialog watches the pad
//    live *while* a modal KeyCaptureDialog can be open on top of it, and two
//    readers on one queue means each steals events from the other. One timer,
//    many listeners, is what fixes that -- which is also why this is a
//    singleton with a reference count rather than something each dialog owns.
//
//  - Acquire()/Release() bracket SDL_InitSubSystem/SDL_QuitSubSystem. SDL
//    reference-counts those itself, but the poll timer has to stop when the
//    last listener goes away or it keeps draining the queue for nobody.

#pragma once

#include <QObject>

class QTimer;

namespace SdlEventWrapper {

class Wrapper : public QObject {
    Q_OBJECT

public:
    [[nodiscard]] static Wrapper* GetInstance();

    // Start/stop reading SDL. Every Acquire() needs one Release(); the first
    // starts the gamepad subsystem and the poll timer, the last stops them.
    static void Acquire();
    static void Release();

    // Whether anything is currently listening.
    [[nodiscard]] static bool IsActive();

signals:
    // type is an SDL_EventType; input is the button or axis; value is the
    // axis value (0 for buttons). Same shape as shadLauncher4's.
    void SDLEvent(int type, int input, int value);

private:
    explicit Wrapper(QObject* parent = nullptr);
    void Poll();

    QTimer* m_timer = nullptr;
    int m_refs = 0;
};

} // namespace SdlEventWrapper
