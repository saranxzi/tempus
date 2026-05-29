#include "pomodoro/config_manager.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace tempus {

static std::string trim(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(), ::isspace);
    auto end = std::find_if_not(str.rbegin(), str.rend(), ::isspace).base();
    return start < end ? std::string(start, end) : "";
}

Result<AppConfig> ConfigManager::load(const std::filesystem::path& filepath) {
    AppConfig config;
    if (!std::filesystem::exists(filepath)) {
        return Result<AppConfig>::ok(config); // return defaults if file doesn't exist
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        return Result<AppConfig>::err("Failed to open configuration file for reading");
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue; // skip empty lines and comments

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq_pos));
        std::string val_str = trim(line.substr(eq_pos + 1));
        if (key.empty() || val_str.empty()) continue;

        try {
            if (key == "focus_duration_sec") {
                config.focus_duration_sec = std::stoi(val_str);
            } else if (key == "short_break_sec") {
                config.short_break_sec = std::stoi(val_str);
            } else if (key == "long_break_sec") {
                config.long_break_sec = std::stoi(val_str);
            } else if (key == "sessions_before_long_break") {
                config.sessions_before_long_break = std::stoi(val_str);
            } else if (key == "weekly_goal_hours") {
                config.weekly_goal_hours = std::stoi(val_str);
            } else if (key == "theme_index") {
                config.theme_index = std::stoi(val_str);
            } else if (key == "sound_enabled") {
                config.sound_enabled = (std::stoi(val_str) != 0);
            }
        } catch (...) {
            // Ignore parse errors for individual values and keep defaults
        }
    }

    return Result<AppConfig>::ok(config);
}

VoidR ConfigManager::save(const AppConfig& config, const std::filesystem::path& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return VoidR::err("Failed to open configuration file for writing");
    }

    file << "# Tempus Configuration Settings\n"
         << "focus_duration_sec=" << config.focus_duration_sec << "\n"
         << "short_break_sec=" << config.short_break_sec << "\n"
         << "long_break_sec=" << config.long_break_sec << "\n"
         << "sessions_before_long_break=" << config.sessions_before_long_break << "\n"
         << "weekly_goal_hours=" << config.weekly_goal_hours << "\n"
         << "theme_index=" << config.theme_index << "\n"
         << "sound_enabled=" << (config.sound_enabled ? 1 : 0) << "\n";

    return VoidR::ok();
}

} // namespace tempus
