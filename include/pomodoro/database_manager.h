#pragma once
#include "pomodoro/types.h"
#include "pomodoro/logger.h"
#include <vector>
#include <string>
#include <memory>

struct sqlite3;  // Forward declaration — avoids leaking sqlite3.h into consumers

namespace tempus {

/// Abstract persistence layer for session data.
class IDatabaseManager {
public:
    virtual ~IDatabaseManager() = default;

    /// Open connection and ensure schema is up to date.
    virtual VoidR initialize() = 0;

    /// Persist a completed or interrupted focus session.
    /// Returns the new row ID on success.
    virtual Result<int64_t> log_session(const TaskEntry& entry) = 0;

    /// Query sessions within a time range [from, to] (inclusive).
    virtual Result<std::vector<TaskEntry>> get_sessions(
        std::time_t from, std::time_t to) = 0;

    /// Query the N most recent sessions.
    virtual Result<std::vector<TaskEntry>> get_recent_sessions(int limit) = 0;

    /// Delete a session by ID.
    virtual VoidR delete_session(int64_t id) = 0;
};

/// SQLite-backed implementation of IDatabaseManager.
/// Configures WAL journal mode and synchronous=NORMAL for resilience.
class SqliteDatabaseManager final : public IDatabaseManager {
public:
    /// @param db_path  Path to the database file, or ":memory:" for tests.
    /// @param logger   Logger instance for query diagnostics.
    explicit SqliteDatabaseManager(std::string db_path, ILogger* logger);
    ~SqliteDatabaseManager() override;

    // Non-copyable, non-movable (owns the sqlite3* handle).
    SqliteDatabaseManager(const SqliteDatabaseManager&) = delete;
    SqliteDatabaseManager& operator=(const SqliteDatabaseManager&) = delete;

    VoidR initialize() override;
    Result<int64_t> log_session(const TaskEntry& entry) override;
    Result<std::vector<TaskEntry>> get_sessions(
        std::time_t from, std::time_t to) override;
    Result<std::vector<TaskEntry>> get_recent_sessions(int limit) override;
    VoidR delete_session(int64_t id) override;

private:
    VoidR exec(const std::string& sql);
    VoidR apply_pragmas();
    VoidR ensure_schema();

    std::string db_path_;
    sqlite3*    db_      = nullptr;
    ILogger*    logger_  = nullptr;
};

} // namespace tempus
