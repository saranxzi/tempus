#include <catch2/catch_test_macros.hpp>
#include "pomodoro/config_manager.h"
#include <filesystem>
#include <fstream>

using namespace tempus;

namespace fs = std::filesystem;

static fs::path test_dir() {
    auto dir = fs::temp_directory_path() / "tempus_config_test";
    fs::create_directories(dir);
    return dir;
}

static void cleanup(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("ConfigManager: default config when missing", "[config]") {
    auto path = test_dir() / "nonexistent_config.txt";
    cleanup(path);

    auto result = ConfigManager::load(path);
    REQUIRE(result.is_ok());
    
    // Check that defaults are returned
    const auto& config = result.value();
    REQUIRE(config.focus_duration_sec == 25 * 60);
    REQUIRE(config.short_break_sec == 5 * 60);
    REQUIRE(config.weekly_goal_hours == 40);
    REQUIRE(config.theme_index == 0);
    REQUIRE(config.sound_enabled == true);
    REQUIRE(config.always_on_top == false);
}

TEST_CASE("ConfigManager: round-trip serialization", "[config]") {
    auto path = test_dir() / "config_roundtrip.txt";
    cleanup(path);

    AppConfig config;
    config.focus_duration_sec = 45 * 60;
    config.short_break_sec = 10 * 60;
    config.long_break_sec = 20 * 60;
    config.sessions_before_long_break = 3;
    config.weekly_goal_hours = 35;
    config.theme_index = 2;
    config.sound_enabled = false;
    config.always_on_top = true;

    REQUIRE(ConfigManager::save(config, path).is_ok());

    auto result = ConfigManager::load(path);
    REQUIRE(result.is_ok());

    const auto& loaded = result.value();
    REQUIRE(loaded.focus_duration_sec == 45 * 60);
    REQUIRE(loaded.short_break_sec == 10 * 60);
    REQUIRE(loaded.long_break_sec == 20 * 60);
    REQUIRE(loaded.sessions_before_long_break == 3);
    REQUIRE(loaded.weekly_goal_hours == 35);
    REQUIRE(loaded.theme_index == 2);
    REQUIRE(loaded.sound_enabled == false);
    REQUIRE(loaded.always_on_top == true);

    cleanup(path);
}

TEST_CASE("ConfigManager: malformed lines ignored", "[config]") {
    auto path = test_dir() / "config_malformed.txt";
    cleanup(path);

    // Write a manual corrupted file
    std::ofstream file(path);
    file << "focus_duration_sec=abc\n" // corrupted integer
         << "short_break_sec=600\n"
         << "weekly_goal_hours=invalid\n"
         << "theme_index=1\n"
         << "some_unknown_key=55\n"; // unknown key
    file.close();

    auto result = ConfigManager::load(path);
    REQUIRE(result.is_ok());

    const auto& loaded = result.value();
    // Corrupted should keep default
    REQUIRE(loaded.focus_duration_sec == 25 * 60);
    // Valid should update
    REQUIRE(loaded.short_break_sec == 600);
    // Corrupted goal should keep default
    REQUIRE(loaded.weekly_goal_hours == 40);
    // Valid theme should update
    REQUIRE(loaded.theme_index == 1);

    cleanup(path);
}
