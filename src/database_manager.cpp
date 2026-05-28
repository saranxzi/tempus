#include "pomodoro/database_manager.h"
#include "sqlite3.h"
#include <cassert>

namespace tempus {

// ─── RAII wrapper for sqlite3_stmt* ──────────────────────────────────────────

class StmtGuard {
public:
    explicit StmtGuard(sqlite3_stmt* stmt) : stmt_(stmt) {}
    ~StmtGuard() { if (stmt_) sqlite3_finalize(stmt_); }
    StmtGuard(const StmtGuard&) = delete;
    StmtGuard& operator=(const StmtGuard&) = delete;
    sqlite3_stmt* get() const { return stmt_; }
private:
    sqlite3_stmt* stmt_;
};

// ─── Construction / Destruction ──────────────────────────────────────────────

SqliteDatabaseManager::SqliteDatabaseManager(std::string db_path, ILogger* logger)
    : db_path_(std::move(db_path))
    , logger_(logger)
{}

SqliteDatabaseManager::~SqliteDatabaseManager() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

// ─── Initialization ─────────────────────────────────────────────────────────

VoidR SqliteDatabaseManager::initialize() {
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "out of memory";
        return VoidR::err("Failed to open database '" + db_path_ + "': " + err);
    }
    logger_->info("DatabaseManager", "Opened database: " + db_path_);

    auto pragmas = apply_pragmas();
    if (pragmas.is_err()) return pragmas;

    auto schema = ensure_schema();
    if (schema.is_err()) return schema;

    logger_->info("DatabaseManager", "Database initialized successfully");
    return VoidR::ok();
}

VoidR SqliteDatabaseManager::apply_pragmas() {
    // WAL mode: better concurrency, readers don't block writers.
    // synchronous=NORMAL: safe against app crashes; tiny risk on power loss.
    auto r1 = exec("PRAGMA journal_mode=WAL;");
    if (r1.is_err()) return r1;

    auto r2 = exec("PRAGMA synchronous=NORMAL;");
    if (r2.is_err()) return r2;

    auto r3 = exec("PRAGMA foreign_keys=ON;");
    if (r3.is_err()) return r3;

    logger_->info("DatabaseManager", "Pragmas applied: WAL, synchronous=NORMAL, foreign_keys=ON");
    return VoidR::ok();
}

VoidR SqliteDatabaseManager::ensure_schema() {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS sessions (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            task_name    TEXT    NOT NULL,
            tags         TEXT    NOT NULL DEFAULT '',
            details      TEXT    NOT NULL DEFAULT '',
            started_at   INTEGER NOT NULL,
            ended_at     INTEGER NOT NULL,
            duration_sec INTEGER NOT NULL,
            state        TEXT    NOT NULL CHECK(state IN ('completed','interrupted'))
        );

        CREATE INDEX IF NOT EXISTS idx_sessions_started
            ON sessions(started_at);

        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER PRIMARY KEY
        );

        INSERT OR IGNORE INTO schema_version(version) VALUES (1);
    )SQL";

    auto r = exec(sql);
    if (r.is_err()) return r;

    // Defensively migrate existing databases to add missing columns.
    exec("ALTER TABLE sessions ADD COLUMN tags TEXT NOT NULL DEFAULT '';");
    exec("ALTER TABLE sessions ADD COLUMN details TEXT NOT NULL DEFAULT '';");

    return VoidR::ok();
}

// ─── CRUD Operations ────────────────────────────────────────────────────────

Result<int64_t> SqliteDatabaseManager::log_session(const TaskEntry& entry) {
    const char* sql =
        "INSERT INTO sessions (task_name, tags, details, started_at, ended_at, duration_sec, state) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr);
    StmtGuard stmt(raw);

    if (rc != SQLITE_OK) {
        return Result<int64_t>::err(
            std::string("Prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt.get(), 1, entry.task_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, entry.tags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, entry.details.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 4, static_cast<int64_t>(entry.started_at));
    sqlite3_bind_int64(stmt.get(), 5, static_cast<int64_t>(entry.ended_at));
    sqlite3_bind_int(stmt.get(), 6, entry.duration_sec);
    sqlite3_bind_text(stmt.get(), 7, entry.state.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return Result<int64_t>::err(
            std::string("Insert failed: ") + sqlite3_errmsg(db_));
    }

    int64_t row_id = sqlite3_last_insert_rowid(db_);
    logger_->info("DatabaseManager",
                  "Logged session id=" + std::to_string(row_id) +
                  " task='" + entry.task_name + "' state=" + entry.state);
    return Result<int64_t>::ok(row_id);
}

Result<std::vector<TaskEntry>> SqliteDatabaseManager::get_sessions(
    std::time_t from, std::time_t to)
{
    const char* sql =
        "SELECT id, task_name, tags, details, started_at, ended_at, duration_sec, state "
        "FROM sessions WHERE started_at >= ? AND started_at <= ? "
        "ORDER BY started_at DESC;";

    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr);
    StmtGuard stmt(raw);

    if (rc != SQLITE_OK) {
        return Result<std::vector<TaskEntry>>::err(
            std::string("Prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_int64(stmt.get(), 1, static_cast<int64_t>(from));
    sqlite3_bind_int64(stmt.get(), 2, static_cast<int64_t>(to));

    std::vector<TaskEntry> results;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        TaskEntry e;
        e.id           = sqlite3_column_int64(stmt.get(), 0);
        e.task_name    = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        
        const char* t_raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
        e.tags         = t_raw ? t_raw : "";

        const char* d_raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
        e.details      = d_raw ? d_raw : "";

        e.started_at   = static_cast<std::time_t>(sqlite3_column_int64(stmt.get(), 4));
        e.ended_at     = static_cast<std::time_t>(sqlite3_column_int64(stmt.get(), 5));
        e.duration_sec = sqlite3_column_int(stmt.get(), 6);
        e.state        = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 7));
        results.push_back(std::move(e));
    }

    if (rc != SQLITE_DONE) {
        return Result<std::vector<TaskEntry>>::err(
            std::string("Query failed: ") + sqlite3_errmsg(db_));
    }

    return Result<std::vector<TaskEntry>>::ok(std::move(results));
}

Result<std::vector<TaskEntry>> SqliteDatabaseManager::get_recent_sessions(int limit) {
    const char* sql =
        "SELECT id, task_name, tags, details, started_at, ended_at, duration_sec, state "
        "FROM sessions ORDER BY started_at DESC LIMIT ?;";

    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr);
    StmtGuard stmt(raw);

    if (rc != SQLITE_OK) {
        return Result<std::vector<TaskEntry>>::err(
            std::string("Prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_int(stmt.get(), 1, limit);

    std::vector<TaskEntry> results;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        TaskEntry e;
        e.id           = sqlite3_column_int64(stmt.get(), 0);
        e.task_name    = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        
        const char* t_raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
        e.tags         = t_raw ? t_raw : "";

        const char* d_raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
        e.details      = d_raw ? d_raw : "";

        e.started_at   = static_cast<std::time_t>(sqlite3_column_int64(stmt.get(), 4));
        e.ended_at     = static_cast<std::time_t>(sqlite3_column_int64(stmt.get(), 5));
        e.duration_sec = sqlite3_column_int(stmt.get(), 6);
        e.state        = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 7));
        results.push_back(std::move(e));
    }

    if (rc != SQLITE_DONE) {
        return Result<std::vector<TaskEntry>>::err(
            std::string("Query failed: ") + sqlite3_errmsg(db_));
    }

    return Result<std::vector<TaskEntry>>::ok(std::move(results));
}

VoidR SqliteDatabaseManager::delete_session(int64_t id) {
    const char* sql = "DELETE FROM sessions WHERE id = ?;";

    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr);
    StmtGuard stmt(raw);

    if (rc != SQLITE_OK) {
        return VoidR::err(std::string("Prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_int64(stmt.get(), 1, id);

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return VoidR::err(std::string("Delete failed: ") + sqlite3_errmsg(db_));
    }

    logger_->info("DatabaseManager", "Deleted session id=" + std::to_string(id));
    return VoidR::ok();
}

VoidR SqliteDatabaseManager::update_session(const TaskEntry& entry) {
    const char* sql =
        "UPDATE sessions SET task_name = ?, tags = ?, details = ?, "
        "started_at = ?, ended_at = ?, duration_sec = ?, state = ? "
        "WHERE id = ?;";

    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr);
    StmtGuard stmt(raw);

    if (rc != SQLITE_OK) {
        return VoidR::err(std::string("Prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt.get(), 1, entry.task_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, entry.tags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, entry.details.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 4, static_cast<int64_t>(entry.started_at));
    sqlite3_bind_int64(stmt.get(), 5, static_cast<int64_t>(entry.ended_at));
    sqlite3_bind_int(stmt.get(), 6, entry.duration_sec);
    sqlite3_bind_text(stmt.get(), 7, entry.state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 8, entry.id);

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return VoidR::err(std::string("Update failed: ") + sqlite3_errmsg(db_));
    }

    logger_->info("DatabaseManager", "Updated session id=" + std::to_string(entry.id));
    return VoidR::ok();
}

// ─── Helpers ────────────────────────────────────────────────────────────────

VoidR SqliteDatabaseManager::exec(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "unknown error";
        sqlite3_free(err_msg);
        logger_->error("DatabaseManager", "SQL exec failed: " + err);
        return VoidR::err("SQL exec failed: " + err);
    }
    return VoidR::ok();
}

} // namespace tempus
