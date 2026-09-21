// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>

class QTimer;

namespace SdlEventWrapper {

class Wrapper : public QObject {
    Q_OBJECT

public:
    [[nodiscard]] static Wrapper* GetInstance();

    static void Acquire();
    static void Release();
    [[nodiscard]] static bool IsActive();

signals:
    void SDLEvent(int type, int input, int value);

private:
    explicit Wrapper(QObject* parent = nullptr);
    void Poll();

    QTimer* m_timer = nullptr;
    int m_refs = 0;
};

} // namespace SdlEventWrapper
