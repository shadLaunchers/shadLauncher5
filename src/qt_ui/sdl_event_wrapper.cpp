// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QTimer>
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>

#include "sdl_event_wrapper.h"

namespace SdlEventWrapper {

namespace {
// 16ms: one poll a frame, the same cadence the capture dialog used when it
// read SDL itself.
constexpr int kPollIntervalMs = 16;
} // namespace

Wrapper::Wrapper(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &Wrapper::Poll);
}

Wrapper* Wrapper::GetInstance() {
    static Wrapper* instance = new Wrapper();
    return instance;
}

void Wrapper::Acquire() {
    Wrapper* self = GetInstance();
    if (self->m_refs++ == 0) {
        SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
        self->m_timer->start();
    }
}

void Wrapper::Release() {
    Wrapper* self = GetInstance();
    if (self->m_refs == 0) {
        return; // unbalanced; nothing to do
    }
    if (--self->m_refs == 0) {
        self->m_timer->stop();
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
    }
}

bool Wrapper::IsActive() {
    return GetInstance()->m_refs > 0;
}

void Wrapper::Poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Only the pad events are forwarded. shadLauncher4's ProcessEvent
        // also swallows window and audio events because it sits in front of a
        // running game's event loop; nothing here is downstream of us, so the
        // rest is simply dropped.
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            emit SDLEvent(static_cast<int>(event.type), 0, 0);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            emit SDLEvent(static_cast<int>(event.type), event.gbutton.button, 0);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            emit SDLEvent(static_cast<int>(event.type), event.gaxis.axis, event.gaxis.value);
            break;
        default:
            break;
        }
    }
}

} // namespace SdlEventWrapper
