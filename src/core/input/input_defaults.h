// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The bindings files as the emulator writes them when they are missing.
//
// Copied from the emulator's src/core/input/input_config.cpp -- DefaultBindings-
// Json(), DefaultGlobalJson() and the file-creating half of EnsureFiles(). The
// emulator creates these on its first run; the launcher could not, so anyone
// who opened the bindings editor before ever launching a game found no
// default.json to start a game's file from, and the editor's own "start from
// the defaults" had nothing to copy.
//
// The two texts are duplicated rather than shared because the two trees are
// built separately and neither includes the other. That duplication is exactly
// the kind that rots, so the cross-check test compiles both copies into one
// program and compares them byte for byte -- if the emulator's text changes and
// this one does not, that test fails.
//
// Only ever creates a file that is absent. Nothing here rewrites one that
// exists, which is what lets the comments in them -- the documentation someone
// editing them reads -- survive.

#pragma once

#include <filesystem>
#include <string>

namespace Core::Input {

// The two files as the emulator writes them when missing. Comments included:
// this is the documentation someone editing them reads.
[[nodiscard]] std::string DefaultBindingsJson();
[[nodiscard]] std::string DefaultGlobalJson();

// Writes default.json and global.json into the user directory if they are
// missing, and creates input_config/ so it is an obvious place to put a
// per-game file. Safe to call repeatedly. Returns true if everything that
// should exist now does.
//
// This is the emulator's EnsureFiles() minus its hotkeys half: hotkeys.json is
// the one file that gets merged rather than only created, and the hotkeys
// editor already owns that.
bool EnsureBindingsFiles();

} // namespace Core::Input
