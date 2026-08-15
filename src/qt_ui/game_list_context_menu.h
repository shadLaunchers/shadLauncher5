// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QMenu>
#include <QPoint>

#include "gui_game_info.h"

class GameListFrame;

// Builds and shows the right-click context menu for a single game entry in
// the game list. Split out of GameListFrame to keep that class focused on
// list management; this class is a friend of GameListFrame and reaches back
// into it (via m_frame) for shared state (settings, hidden list, notes,
// caches, etc.) and for the handful of actions it needs to trigger
// (Refresh, RequestBoot, ShowCustomConfigIcon, ...).
class GameListContextMenu : public QMenu {
    Q_OBJECT
public:
    explicit GameListContextMenu(GameListFrame* frame);

    // Builds the menu for the given game and shows it (blocking) at
    // global_pos.
    void Show(const game_info& gameinfo, const QPoint& global_pos);

private:
    GameListFrame* m_frame = nullptr;
};
