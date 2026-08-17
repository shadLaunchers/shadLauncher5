// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "common/logging/log.h"
#include "common/types.h"

#define EmulatorSettings (*EmulatorSettingsImpl::GetInstance())

enum class ConfigMode {
    Default,
    Global,
    Clean,
};

template <typename T>
struct Setting {
    T default_value{};
    T value{};
    std::optional<T> game_specific_value{};

    Setting() = default;
    // Single-argument ctor: initialises both default_value and value so
    // that CleanMode can always recover the intended factory default.
    /*implicit*/ Setting(T init) : default_value(std::move(init)), value(default_value) {}

    /// Return the active value under the given mode.
    T get(ConfigMode mode = ConfigMode::Default) const {
        switch (mode) {
        case ConfigMode::Default:
            return game_specific_value.value_or(value);
        case ConfigMode::Global:
            return value;
        case ConfigMode::Clean:
            return default_value;
        }
        return value;
    }

    /// Write v to the base layer.
    /// Set proper value as base or game_specific
    void set(const T& v, bool game_specific = false) {
        if (game_specific) {
            game_specific_value = v;
        } else {
            value = v;
        }
    }

    /// Discard the game-specific override; subsequent get(Default) will
    /// fall back to the base value.
    void reset_game_specific() {
        game_specific_value = std::nullopt;
    }
};

template <typename T>
void to_json(nlohmann::json& j, const Setting<T>& s) {
    j = s.value;
}

template <typename T>
void from_json(const nlohmann::json& j, Setting<T>& s) {
    s.value = j.get<T>();
}

struct OverrideItem {
    const char* key;
    std::function<void(void* group_ptr, const nlohmann::json& entry,
                       std::vector<std::string>& changed)>
        apply;
    /// Return the value that should be written to the per-game config file.
    /// Falls back to base value if no game-specific override is set.
    std::function<nlohmann::json(const void* group_ptr)> get_for_save;

    /// Clear game_specific_value for this field.
    std::function<void(void* group_ptr)> reset_game_specific;
};

template <typename Struct, typename T>
inline OverrideItem make_override(const char* key, Setting<T> Struct::*member) {
    return OverrideItem{
        key,
        [member, key](void* base, const nlohmann::json& entry, std::vector<std::string>& changed) {
            Struct* obj = reinterpret_cast<Struct*>(base);
            Setting<T>& dst = obj->*member;
            try {
                T newValue = entry.get<T>();
                if (dst.value != newValue) {
                    std::ostringstream oss;
                    oss << key << " ( " << dst.value << " -> " << newValue << " )";
                    changed.push_back(oss.str());
                }
                dst.game_specific_value = newValue;
            } catch (const std::exception& e) {
                LOG_ERROR(Config, "[make_override] error parsing {}: {}", key, e.what());
                LOG_ERROR(Config, "[make_override] Entry was: {}", entry.dump());
                LOG_ERROR(Config, "[make_override] Type name: {}", entry.type_name());
            }
        },

        // --- get_for_save -------------------------------------------
        // Returns game_specific_value when present, otherwise base value.
        // This means a freshly-opened game-specific dialog still shows
        // useful (current-global) values rather than empty entries.
        [member](const void* base) -> nlohmann::json {
            const Struct* obj = reinterpret_cast<const Struct*>(base);
            const Setting<T>& src = obj->*member;
            return nlohmann::json(src.game_specific_value.value_or(src.value));
        },

        // --- reset_game_specific ------------------------------------
        [member](void* base) {
            Struct* obj = reinterpret_cast<Struct*>(base);
            (obj->*member).reset_game_specific();
        }};
}

// -------------------------------
// Support types
// -------------------------------
struct GameInstallDir {
    std::filesystem::path path;
    bool enabled;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GameInstallDir, path, enabled)

// -------------------------------
// General settings
// -------------------------------
struct GeneralSettings {
    Setting<std::vector<GameInstallDir>> install_dirs;
    Setting<std::filesystem::path> addon_install_dir;
    Setting<std::filesystem::path> home_dir;
    Setting<std::filesystem::path> sys_modules_dir;

    Setting<int> console_language{1};

    // Nothing in this group is overrideable per game yet.
    std::vector<OverrideItem> GetOverrideableFields() const {
        return {};
    }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GeneralSettings, install_dirs, addon_install_dir, home_dir,
                                   sys_modules_dir, console_language)

// -------------------------------
// Log settings
// -------------------------------
struct LogSettings {
    Setting<bool> append{false}; // specific
    Setting<bool> enable{true};  // specific
    Setting<std::string> filter{""};
    Setting<u32> max_skip_duration{5'000};
    Setting<bool> separate{false}; // specific
    Setting<unsigned long long> size_limit{100_MB};
    Setting<bool> skip_duplicate{true};
    Setting<bool> sync{true};
#ifdef _WIN32
    Setting<std::string> type{"wincolor"};
#endif

    // return a vector of override descriptors (runtime, but tiny)
    std::vector<OverrideItem> GetOverrideableFields() const {
        return std::vector<OverrideItem>{
            make_override<LogSettings>("append", &LogSettings::append),
            make_override<LogSettings>("enable", &LogSettings::enable),
            make_override<LogSettings>("filter", &LogSettings::filter),
            make_override<LogSettings>("max_skip_duration", &LogSettings::max_skip_duration),
            make_override<LogSettings>("separate", &LogSettings::separate),
            make_override<LogSettings>("size_limit", &LogSettings::size_limit),
            make_override<LogSettings>("skip_duplicate", &LogSettings::skip_duplicate),
            make_override<LogSettings>("sync", &LogSettings::sync),
#ifdef _WIN32
            make_override<LogSettings>("type", &LogSettings::type),
#endif
        };
    }
};
#ifdef _WIN32
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LogSettings, append, enable, filter, max_skip_duration, separate,
                                   size_limit, skip_duplicate, sync, type)
#else
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LogSettings, append, enable, filter, max_skip_duration, separate,
                                   size_limit, skip_duplicate, sync)
#endif

// -------------------------------
// Debug settings
// -------------------------------
struct DebugSettings {
    Setting<std::string> config_version{""}; // specific

    // config_version is bookkeeping, not a per-game override.
    std::vector<OverrideItem> GetOverrideableFields() const {
        return {};
    }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DebugSettings, config_version)

// -------------------------------
// Main manager
// -------------------------------
class EmulatorSettingsImpl {
public:
    EmulatorSettingsImpl();
    ~EmulatorSettingsImpl();

    static std::shared_ptr<EmulatorSettingsImpl> GetInstance();
    static void SetInstance(std::shared_ptr<EmulatorSettingsImpl> instance);

    bool Save(const std::string& serial = "");
    bool Load(const std::string& serial = "");
    void SetDefaultValues();

    // Config mode
    ConfigMode GetConfigMode() const {
        return m_configMode;
    }
    void SetConfigMode(ConfigMode mode) {
        m_configMode = mode;
    }

    //
    // Game-specific override management
    /// Clears all per-game overrides.  Call this when a game exits so
    /// the emulator reverts to global settings.
    void ClearGameSpecificOverrides();

    /// Reset a single field's game-specific override by its JSON ke
    void ResetGameSpecificValue(const std::string& key);

    // general accessors
    bool AddGameInstallDir(const std::filesystem::path& dir, bool enabled = true);
    std::vector<std::filesystem::path> GetGameInstallDirs() const;
    void SetAllGameInstallDirs(const std::vector<GameInstallDir>& dirs);
    void RemoveGameInstallDir(const std::filesystem::path& dir);
    void SetGameInstallDirEnabled(const std::filesystem::path& dir, bool enabled);
    void SetGameInstallDirs(const std::vector<std::filesystem::path>& dirs_config);
    const std::vector<bool> GetGameInstallDirsEnabled();
    const std::vector<GameInstallDir>& GetAllGameInstallDirs() const;

    std::filesystem::path GetHomeDir();
    void SetHomeDir(const std::filesystem::path& dir);
    std::filesystem::path GetSysModulesDir();
    void SetSysModulesDir(const std::filesystem::path& dir);
    std::filesystem::path GetAddonInstallDir();
    void SetAddonInstallDir(const std::filesystem::path& dir);

private:
    GeneralSettings m_general{};
    LogSettings m_log{};
    DebugSettings m_debug{};
    ConfigMode m_configMode{ConfigMode::Default};

    bool m_loaded{false};

    static std::shared_ptr<EmulatorSettingsImpl> s_instance;
    static std::mutex s_mutex;

    /// Apply overrideable fields from groupJson into group.game_specific_value.
    template <typename Group>
    void ApplyGroupOverrides(Group& group, const nlohmann::json& groupJson,
                             std::vector<std::string>& changed) {
        for (auto& item : group.GetOverrideableFields()) {
            if (!groupJson.contains(item.key))
                continue;
            item.apply(&group, groupJson.at(item.key), changed);
        }
    }

    // Write all overrideable fields from group into out (for game-specific save).
    template <typename Group>
    static void SaveGroupGameSpecific(const Group& group, nlohmann::json& out) {
        for (auto& item : group.GetOverrideableFields())
            out[item.key] = item.get_for_save(&group);
    }

    // Discard every game-specific override in group.
    template <typename Group>
    static void ClearGroupOverrides(Group& group) {
        for (auto& item : group.GetOverrideableFields())
            item.reset_game_specific(&group);
    }

    static void PrintChangedSummary(const std::vector<std::string>& changed);

public:
    // Add these getters to access overrideable fields
    std::vector<OverrideItem> GetGeneralOverrideableFields() const {
        return m_general.GetOverrideableFields();
    }
    std::vector<OverrideItem> GetLogOverrideableFields() const {
        return m_log.GetOverrideableFields();
    }
    std::vector<OverrideItem> GetDebugOverrideableFields() const {
        return m_debug.GetOverrideableFields();
    }
    std::vector<std::string> GetAllOverrideableKeys() const;

#define SETTING_FORWARD(group, Name, field)                                                        \
    auto Get##Name() const {                                                                       \
        return (group).field.get(m_configMode);                                                    \
    }                                                                                              \
    void Set##Name(const decltype((group).field.value)& v, bool specific = false) {                \
        (group).field.set(v, specific);                                                            \
    }
#define SETTING_FORWARD_BOOL(group, Name, field)                                                   \
    bool Is##Name() const {                                                                        \
        return (group).field.get(m_configMode);                                                    \
    }                                                                                              \
    void Set##Name(bool v, bool specific = false) {                                                \
        (group).field.set(v, specific);                                                            \
    }
#define SETTING_FORWARD_BOOL_READONLY(group, Name, field)                                          \
    bool Is##Name() const {                                                                        \
        return (group).field.get(m_configMode);                                                    \
    }

    // General settings
    SETTING_FORWARD(m_general, ConsoleLanguage, console_language)

    // Log settings
    SETTING_FORWARD_BOOL(m_log, LogAppend, append)
    SETTING_FORWARD_BOOL(m_log, LogEnable, enable)
    SETTING_FORWARD(m_log, LogFilter, filter)
    SETTING_FORWARD(m_log, LogMaxSkipDuration, max_skip_duration)
    SETTING_FORWARD_BOOL(m_log, LogSeparate, separate)
    SETTING_FORWARD(m_log, LogSizeLimit, size_limit)
    SETTING_FORWARD_BOOL(m_log, LogSkipDuplicate, skip_duplicate)
    SETTING_FORWARD_BOOL(m_log, LogSync, sync)
#ifdef _WIN32
    SETTING_FORWARD(m_log, LogType, type)
#endif

    // Debug settings
    SETTING_FORWARD(m_debug, ConfigVersion, config_version)

#undef SETTING_FORWARD
#undef SETTING_FORWARD_BOOL
#undef SETTING_FORWARD_BOOL_READONLY
};
