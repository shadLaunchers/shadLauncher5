// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

namespace Common::Log::Class {
// clang-format off
/// Listing all log classes, if you add here, dont forget ALL_LOGGERS
constexpr auto Common = "Common";                                   ///< Library routines
constexpr auto Common_Filesystem = "Common.Filesystem";             ///< Filesystem interface library
constexpr auto Config = "Config";                                   ///< Emulator configuration (including commandline)
constexpr auto Core = "Core";                                       ///< LLE emulation core
constexpr auto Debug = "Debug";                                     ///< Debugging tools
constexpr auto Frontend = "Frontend";                               ///< Emulator UI
constexpr auto IPC = "IPC";                                         ///< IPC
constexpr auto Lib = "Lib";                                         ///< HLE implementation of system library. Each major library  should have its own subclass.
constexpr auto Loader = "Loader";                                   ///< ROM loader
constexpr auto Log = "Log";                                         ///< Messages about the log system itself
constexpr auto Tty = "Tty";                                         ///< Debug output from emu
// clang-format on
} // namespace Common::Log::Class
