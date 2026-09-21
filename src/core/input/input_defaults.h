// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace Core::Input {

[[nodiscard]] std::string DefaultBindingsJson();
[[nodiscard]] std::string DefaultGlobalJson();

bool EnsureBindingsFiles();

} // namespace Core::Input
