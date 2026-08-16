// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <iostream>
#include <string>

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/logging/thread_name_formatter.h"
#include "common/types.h"
#include "core/emulator_settings.h"
#ifdef _WIN32
#include <Windows.h>
#endif

namespace Common::Log {
bool g_should_append = false;

static std::shared_ptr<spdlog_stdout> g_console_sink;
static std::shared_ptr<spdlog::sinks::basic_file_sink_mt> g_shad_file_sink;

std::unordered_map<std::string_view, std::shared_ptr<spdlog::logger>> ALL_LOGGERS{
    {Class::Common, nullptr}, {Class::Common_Filesystem, nullptr},
    {Class::Config, nullptr}, {Class::Core, nullptr},
    {Class::Debug, nullptr},  {Class::Frontend, nullptr},
    {Class::IPC, nullptr},    {Class::Lib, nullptr},
    {Class::Loader, nullptr}, {Class::Log, nullptr},
    {Class::Tty, nullptr},
};

template <typename T>
static auto UpdateColorLevels(T sink) {
#ifdef _WIN32
    using LogColor = std::uint16_t;

    const auto Grey = FOREGROUND_INTENSITY;
    const auto Cyan = FOREGROUND_GREEN | FOREGROUND_BLUE;
    const auto Bright_gray = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    const auto Bright_yellow = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    const auto Bright_red = FOREGROUND_RED | FOREGROUND_INTENSITY;
    const auto Bright_magenta = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
#else
    using LogColor = std::string_view;

#define ESC "\x1b"
    const auto Grey = ESC "[1;30m";
    const auto Cyan = ESC "[0;36m";
    const auto Bright_gray = ESC "[0;37m";
    const auto Bright_yellow = ESC "[1;33m";
    const auto Bright_red = ESC "[1;31m";
    const auto Bright_magenta = ESC "[1;35m";
#undef ESC
#endif

    const std::unordered_map<spdlog::level, LogColor> colors{
        {spdlog::level::trace, Grey},       {spdlog::level::debug, Cyan},
        {spdlog::level::info, Bright_gray}, {spdlog::level::warn, Bright_yellow},
        {spdlog::level::err, Bright_red},   {spdlog::level::critical, Bright_magenta}};

    for (const auto& [level, color] : colors) {
        sink->set_color(level, color);
    }

    return sink;
}

void Setup(std::string_view log_filename) {
    static bool already_registered = false;

    if (!already_registered) {
        already_registered = true;
        std::atexit(Shutdown);
        std::at_quick_exit(Flush);
    }

#ifdef _WIN32
    if (EmulatorSettings.GetLogType() == "wincolor") {
        g_console_sink = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>();
    } else {
        g_console_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
    }

#else
    g_console_sink = UpdateColorLevels(std::make_shared<spdlog_stdout>(spdlog::color_mode::always));
#endif

    g_console_sink->set_formatter(std::make_unique<thread_name_formatter>(UNLIMITED_SIZE));

    g_shad_file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        (GetUserPath(Common::FS::PathType::LogDir) / log_filename).string(), !g_should_append);
    g_shad_file_sink->set_formatter(
        std::make_unique<thread_name_formatter>(EmulatorSettings.GetLogSizeLimit()));

    std::initializer_list<spdlog::sink_ptr> sinks{g_console_sink, g_shad_file_sink};

    std::initializer_list<spdlog::sink_ptr> async_sink{std::make_shared<spdlog::sinks::async_sink>(
        spdlog::sinks::async_sink::config{.sinks = sinks})};

    std::initializer_list<spdlog::sink_ptr> dup_filter{
        std::make_shared<spdlog::sinks::dup_filter_sink_mt>(
            std::chrono::milliseconds(EmulatorSettings.GetLogMaxSkipDuration()),
            EmulatorSettings.IsLogSync() ? sinks : async_sink)};

    spdlog::level default_log_level = spdlog::level::info;
    std::unordered_map<std::string, spdlog::level> log_level_per_class;

    if (EmulatorSettings.IsLogEnable()) {
        for (const auto class_level : std::views::split(EmulatorSettings.GetLogFilter(), ' ')) {
            const auto class_level_pair =
                std::views::split(class_level, ':') | std::ranges::to<std::vector<std::string>>();

            if (class_level_pair.size() != 2) {
                std::cerr << "bad log filter provided" << std::endl;
                continue;
            }

            if (class_level_pair.front()[0] == '*') {
                default_log_level = spdlog::level_from_str(class_level_pair.back() |
                                                           std::ranges::to<std::string>());
            } else {
                log_level_per_class[class_level_pair.front() | std::ranges::to<std::string>()] =
                    spdlog::level_from_str(class_level_pair.back() |
                                           std::ranges::to<std::string>());
            }
        }
    }

    for (auto& [name, logger] : ALL_LOGGERS) {
        logger = std::make_shared<spdlog::logger>(
            std::string(name), EmulatorSettings.IsLogSkipDuplicate()
                                   ? dup_filter
                                   : (EmulatorSettings.IsLogSync() ? sinks : async_sink));

        if (EmulatorSettings.IsLogEnable()) {
            const auto level_it = log_level_per_class.find(std::string(name));

            logger->set_level(level_it != log_level_per_class.end() ? level_it->second
                                                                    : default_log_level);
        } else {
            logger->set_level(spdlog::level::off);
        }
    }
}

void Shutdown() {
    for (auto& logger : ALL_LOGGERS | std::views::values) {
        logger.reset();
    }

    g_shad_file_sink.reset();
    g_console_sink.reset();
}

void Flush() {
    if (g_shad_file_sink != nullptr) {
        g_shad_file_sink->flush();
    }

    if (g_console_sink != nullptr) {
        g_console_sink->flush();
    }
}
} // namespace Common::Log
