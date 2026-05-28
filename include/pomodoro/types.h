#pragma once
#include <string>
#include <variant>
#include <vector>
#include <cstdint>
#include <chrono>

namespace tempus {

/// Lightweight error-or-value type. Every fallible operation returns Result<T>.
/// On success, holds a T. On failure, holds a std::string error message.
struct VoidResult {};

template <typename T = VoidResult>
class Result {
public:
    /// Construct a success result.
    static Result ok(T value) { return Result(std::move(value)); }
    /// Construct a success result (void specialization convenience).
    static Result ok() { return Result(VoidResult{}); }
    /// Construct an error result.
    static Result err(std::string message) { return Result(std::move(message)); }

    bool is_ok() const { return std::holds_alternative<T>(data_); }
    bool is_err() const { return !is_ok(); }

    const T& value() const { return std::get<T>(data_); }
    T& value() { return std::get<T>(data_); }
    const std::string& error() const { return std::get<std::string>(data_); }

private:
    explicit Result(T val) : data_(std::move(val)) {}
    explicit Result(std::string err) : data_(std::move(err)) {}
    std::variant<T, std::string> data_;
};

// Convenience alias
using VoidR = Result<VoidResult>;

/// Timer state machine states.
enum class TimerState {
    Idle,          ///< No active session
    Focusing,      ///< Active focus/work period
    ShortBreak,    ///< Short rest between focus sessions
    LongBreak,     ///< Extended rest after N focus sessions
    Paused         ///< Focus session paused by user
};

/// Returns a human-readable string for a TimerState value.
inline const char* to_string(TimerState s) {
    switch (s) {
        case TimerState::Idle:       return "Idle";
        case TimerState::Focusing:   return "Focusing";
        case TimerState::ShortBreak: return "ShortBreak";
        case TimerState::LongBreak:  return "LongBreak";
        case TimerState::Paused:     return "Paused";
        default:                     return "Unknown";
    }
}

/// Configurable durations for the timer engine.
struct TimerConfig {
    int focus_duration_sec       = 25 * 60;   ///< Default: 25 minutes
    int short_break_sec          = 5  * 60;   ///< Default: 5 minutes
    int long_break_sec           = 15 * 60;   ///< Default: 15 minutes
    int sessions_before_long_break = 4;       ///< Long break after N focus sessions
};

/// A completed or interrupted focus session persisted to the database.
struct TaskEntry {
    int64_t     id            = 0;
    std::string task_name;
    std::string tags;                ///< Comma-separated categories
    std::string details;             ///< Detailed task notes
    std::time_t started_at    = 0;   ///< Unix epoch seconds
    std::time_t ended_at      = 0;   ///< Unix epoch seconds
    int         duration_sec  = 0;
    std::string state;               ///< "completed" or "interrupted"
};

/// Serializable snapshot of the timer state for crash recovery.
struct TimerSnapshot {
    TimerState  state          = TimerState::Idle;
    int         remaining_sec  = 0;
    std::string current_task;
    std::string tags;
    std::string details;
    int         session_count  = 0;
    std::time_t timestamp      = 0;   ///< When this snapshot was taken
};


} // namespace tempus
