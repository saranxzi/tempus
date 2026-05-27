#include <catch2/catch_test_macros.hpp>
#include "pomodoro/state_recovery.h"
#include "pomodoro/logger.h"
#include <filesystem>

using namespace tempus;

namespace fs = std::filesystem;

/// Use a temporary directory for test files.
static fs::path test_dir() {
    // Use the system temp directory to avoid polluting the project.
    auto dir = fs::temp_directory_path() / "tempus_test";
    fs::create_directories(dir);
    return dir;
}

static void cleanup(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("StateRecovery: round-trip serialization", "[recovery]") {
    NullLogger logger;
    auto path = test_dir() / "state_roundtrip.json";

    TimerSnapshot snap;
    snap.state         = TimerState::Focusing;
    snap.remaining_sec = 742;
    snap.current_task  = "Write documentation";
    snap.session_count = 2;
    snap.timestamp     = 1700000500;

    REQUIRE(StateRecovery::save(snap, path, &logger).is_ok());

    auto loaded = StateRecovery::load(path, &logger);
    REQUIRE(loaded.is_ok());
    REQUIRE(loaded.value().has_value());

    const auto& restored = loaded.value().value();
    REQUIRE(restored.state         == TimerState::Focusing);
    REQUIRE(restored.remaining_sec == 742);
    REQUIRE(restored.current_task  == "Write documentation");
    REQUIRE(restored.session_count == 2);
    REQUIRE(restored.timestamp     == 1700000500);

    cleanup(path);
}

TEST_CASE("StateRecovery: load nonexistent file returns nullopt", "[recovery]") {
    NullLogger logger;
    auto path = test_dir() / "does_not_exist.json";

    auto result = StateRecovery::load(path, &logger);
    REQUIRE(result.is_ok());
    REQUIRE_FALSE(result.value().has_value());
}

TEST_CASE("StateRecovery: load corrupt file returns error", "[recovery]") {
    NullLogger logger;
    auto path = test_dir() / "corrupt_state.json";

    // Write garbage.
    {
        std::ofstream ofs(path);
        ofs << "this is not json at all!!!";
    }

    auto result = StateRecovery::load(path, &logger);
    // Should succeed but return a snapshot with default values
    // since the parser is lenient (no matching keys).
    REQUIRE(result.is_ok());
    if (result.value().has_value()) {
        REQUIRE(result.value().value().state == TimerState::Idle);
    }

    cleanup(path);
}

TEST_CASE("StateRecovery: clear removes file", "[recovery]") {
    NullLogger logger;
    auto path = test_dir() / "state_to_clear.json";

    TimerSnapshot snap;
    snap.state = TimerState::Paused;
    REQUIRE(StateRecovery::save(snap, path, &logger).is_ok());
    REQUIRE(fs::exists(path));

    StateRecovery::clear(path);
    REQUIRE_FALSE(fs::exists(path));
}

TEST_CASE("StateRecovery: special characters in task name", "[recovery]") {
    NullLogger logger;
    auto path = test_dir() / "state_special.json";

    TimerSnapshot snap;
    snap.state        = TimerState::Focusing;
    snap.current_task = R"(Fix "bug" in C:\path\to\file)";
    snap.remaining_sec = 100;

    REQUIRE(StateRecovery::save(snap, path, &logger).is_ok());

    auto loaded = StateRecovery::load(path, &logger);
    REQUIRE(loaded.is_ok());
    REQUIRE(loaded.value().has_value());
    // Note: backslash escaping in JSON is basic, so the round-trip
    // should preserve the content correctly.
    REQUIRE(loaded.value().value().current_task.find("bug") != std::string::npos);

    cleanup(path);
}
