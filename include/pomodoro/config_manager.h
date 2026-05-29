#pragma once
#include "pomodoro/types.h"
#include <filesystem>
#include <string>

namespace tempus {

/// Persistent application configuration settings.
struct AppConfig {
    int focus_duration_sec       = 25 * 60;   ///< Default: 25 minutes
    int short_break_sec          = 5 * 60;    ///< Default: 5 minutes
    int long_break_sec           = 15 * 60;   ///< Default: 15 minutes
    int sessions_before_long_break = 4;       ///< Focus sessions before long break
    int weekly_goal_hours        = 40;        ///< Target weekly focus hours
    int theme_index              = 0;         ///< 0 = Midnight, 1 = Obsidian, 2 = Forest Mint, 3 = Slate
    bool sound_enabled           = true;      ///< Play standard system audio notifications
    bool always_on_top           = false;     ///< Keep window always on top of other windows
};

/// Handles loading and saving AppConfig from a simple key-value configuration file.
class ConfigManager {
public:
    /// Load settings from the configuration file. Falls back to defaults if missing or corrupted.
    static Result<AppConfig> load(const std::filesystem::path& filepath);

    /// Persist settings to the configuration file.
    static VoidR save(const AppConfig& config, const std::filesystem::path& filepath);
};

} // namespace tempus
