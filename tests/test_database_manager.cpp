#include <catch2/catch_test_macros.hpp>
#include "pomodoro/database_manager.h"
#include "pomodoro/logger.h"

using namespace tempus;

/// Helper: create an in-memory database for each test case.
struct DbHarness {
    NullLogger            logger;
    SqliteDatabaseManager db{":memory:", &logger};

    DbHarness() {
        REQUIRE(db.initialize().is_ok());
    }
};

static TaskEntry make_entry(const std::string& name, std::time_t start,
                             int duration, const std::string& state) {
    TaskEntry e;
    e.task_name    = name;
    e.started_at   = start;
    e.ended_at     = start + duration;
    e.duration_sec = duration;
    e.state        = state;
    return e;
}

TEST_CASE("Database: initialize creates schema", "[db]") {
    DbHarness h;
    // If we got here, initialize() succeeded — tables exist.
    auto sessions = h.db.get_recent_sessions(10);
    REQUIRE(sessions.is_ok());
    REQUIRE(sessions.value().empty());
}

TEST_CASE("Database: log and retrieve session", "[db]") {
    DbHarness h;

    auto entry = make_entry("Code review", 1700000000, 1500, "completed");
    auto id_result = h.db.log_session(entry);
    REQUIRE(id_result.is_ok());
    REQUIRE(id_result.value() > 0);

    auto sessions = h.db.get_recent_sessions(10);
    REQUIRE(sessions.is_ok());
    REQUIRE(sessions.value().size() == 1);

    const auto& s = sessions.value()[0];
    REQUIRE(s.task_name == "Code review");
    REQUIRE(s.started_at == 1700000000);
    REQUIRE(s.ended_at == 1700001500);
    REQUIRE(s.duration_sec == 1500);
    REQUIRE(s.state == "completed");
}

TEST_CASE("Database: range query", "[db]") {
    DbHarness h;

    // Insert 3 sessions at different times.
    h.db.log_session(make_entry("Task A", 1000, 60, "completed"));
    h.db.log_session(make_entry("Task B", 2000, 60, "completed"));
    h.db.log_session(make_entry("Task C", 3000, 60, "interrupted"));

    // Query for sessions between 1500 and 2500.
    auto result = h.db.get_sessions(1500, 2500);
    REQUIRE(result.is_ok());
    REQUIRE(result.value().size() == 1);
    REQUIRE(result.value()[0].task_name == "Task B");
}

TEST_CASE("Database: delete session", "[db]") {
    DbHarness h;

    auto id = h.db.log_session(make_entry("To delete", 5000, 300, "completed"));
    REQUIRE(id.is_ok());

    REQUIRE(h.db.delete_session(id.value()).is_ok());

    auto sessions = h.db.get_recent_sessions(10);
    REQUIRE(sessions.is_ok());
    REQUIRE(sessions.value().empty());
}

TEST_CASE("Database: invalid state rejected by CHECK constraint", "[db]") {
    DbHarness h;

    auto entry = make_entry("Bad state", 1000, 60, "invalid_state");
    auto result = h.db.log_session(entry);
    REQUIRE(result.is_err());
}
