#include <catch2/catch_test_macros.hpp>
#include "mock_clock.h"
#include "pomodoro/timer_engine.h"
#include "pomodoro/database_manager.h"
#include "pomodoro/logger.h"

using namespace tempus;

/// Helper: create a fully-wired engine with in-memory DB and mock clock.
struct TestHarness {
    MockClock               clock;
    NullLogger              logger;
    SqliteDatabaseManager   db{":memory:", &logger};
    TimerConfig             config;
    std::unique_ptr<TimerEngine> engine;

    TestHarness() {
        // Use short durations for fast tests.
        config.focus_duration_sec       = 10;   // 10 seconds
        config.short_break_sec          = 3;
        config.long_break_sec           = 5;
        config.sessions_before_long_break = 2;

        REQUIRE(db.initialize().is_ok());
        engine = std::make_unique<TimerEngine>(&clock, &db, &logger, config);
    }
};

TEST_CASE("TimerEngine: start and tick to completion", "[engine]") {
    TestHarness h;

    REQUIRE(h.engine->start("Write tests").is_ok());
    REQUIRE(h.engine->state() == TimerState::Focusing);

    // Advance past the focus duration.
    h.clock.advance(11);
    auto result = h.engine->tick();
    REQUIRE(result.is_ok());

    // Should transition to ShortBreak (session_count < 2).
    REQUIRE(result.value() == TimerState::ShortBreak);
    REQUIRE(h.engine->session_count() == 1);
}

TEST_CASE("TimerEngine: pause and resume", "[engine]") {
    TestHarness h;

    REQUIRE(h.engine->start("Design API").is_ok());

    // Advance 3 seconds, then pause.
    h.clock.advance(3);
    REQUIRE(h.engine->pause().is_ok());
    REQUIRE(h.engine->state() == TimerState::Paused);

    // Advance 100 seconds while paused (should not count).
    h.clock.advance(100);
    REQUIRE(h.engine->resume().is_ok());
    REQUIRE(h.engine->state() == TimerState::Focusing);

    // Only ~3s of focus time should have elapsed.
    // remaining_sec should be close to 10 - 3 = 7.
    REQUIRE(h.engine->remaining_sec() == 7);
}

TEST_CASE("TimerEngine: full cycle triggers long break", "[engine]") {
    TestHarness h;

    // Session 1: focus → short break → focus
    REQUIRE(h.engine->start("Session 1").is_ok());
    h.clock.advance(11);
    REQUIRE(h.engine->tick().value() == TimerState::ShortBreak);
    h.clock.advance(4);
    REQUIRE(h.engine->tick().value() == TimerState::Focusing);

    // Session 2: focus → long break (sessions_before_long_break == 2)
    h.clock.advance(11);
    REQUIRE(h.engine->tick().value() == TimerState::LongBreak);
    REQUIRE(h.engine->session_count() == 2);

    // Long break ends → Idle, session_count resets.
    h.clock.advance(6);
    REQUIRE(h.engine->tick().value() == TimerState::Idle);
    REQUIRE(h.engine->session_count() == 0);
}

TEST_CASE("TimerEngine: stop logs interrupted session", "[engine]") {
    TestHarness h;

    REQUIRE(h.engine->start("Interrupted task").is_ok());
    h.clock.advance(5);  // Midway through focus period.
    REQUIRE(h.engine->stop().is_ok());
    REQUIRE(h.engine->state() == TimerState::Idle);

    // Verify the session was persisted as interrupted.
    auto sessions = h.db.get_recent_sessions(1);
    REQUIRE(sessions.is_ok());
    REQUIRE(sessions.value().size() == 1);
    REQUIRE(sessions.value()[0].state == "interrupted");
    REQUIRE(sessions.value()[0].task_name == "Interrupted task");
}

TEST_CASE("TimerEngine: snapshot and restore", "[engine]") {
    TestHarness h;

    REQUIRE(h.engine->start("Snapshot test").is_ok());
    h.clock.advance(4);  // 4s into focus.
    auto snap = h.engine->snapshot();

    REQUIRE(snap.state == TimerState::Focusing);
    REQUIRE(snap.remaining_sec == 6);  // 10 - 4 = 6
    REQUIRE(snap.current_task == "Snapshot test");

    // Create a fresh engine and restore the snapshot.
    TestHarness h2;
    REQUIRE(h2.engine->restore(snap).is_ok());
    REQUIRE(h2.engine->state() == TimerState::Focusing);
    REQUIRE(h2.engine->remaining_sec() == 6);
    REQUIRE(h2.engine->current_task() == "Snapshot test");
}

TEST_CASE("TimerEngine: invalid transitions return errors", "[engine]") {
    TestHarness h;

    // Cannot pause when idle.
    REQUIRE(h.engine->pause().is_err());
    // Cannot resume when idle.
    REQUIRE(h.engine->resume().is_err());
    // Cannot stop when idle.
    REQUIRE(h.engine->stop().is_err());
    // Cannot start with empty name.
    REQUIRE(h.engine->start("").is_err());
}
