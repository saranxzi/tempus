#pragma once
#include "pomodoro/types.h"
#include "pomodoro/clock.h"
#include "pomodoro/database_manager.h"
#include "pomodoro/logger.h"
#include <string>

namespace tempus {

/// Core Pomodoro-style timer engine implementing a state machine:
///
///   Idle ──start()──▶ Focusing ──tick()──▶ ShortBreak / LongBreak
///                       │  ▲                       │
///                  pause() resume()           tick() (break ends)
///                       ▼  │                       │
///                      Paused               Focusing / Idle
///
/// The engine never touches the system clock directly — it receives
/// an IClock* at construction for full testability.
class TimerEngine {
public:
    TimerEngine(IClock* clock, IDatabaseManager* db,
                ILogger* logger, TimerConfig config = {});

    /// Begin a new focus session with the given task name.
    VoidR start(const std::string& task_name);

    /// Pause the current focus session.
    VoidR pause();

    /// Resume a paused focus session.
    VoidR resume();

    /// Abort the current session and log it as interrupted.
    VoidR stop();

    /// Advance the state machine. Call this from the main loop.
    /// Checks elapsed time via IClock and transitions state when
    /// a period expires. Logs completed sessions to the database.
    Result<TimerState> tick();

    /// Capture current state for crash-recovery serialization.
    TimerSnapshot snapshot() const;

    /// Restore engine state from a previously saved snapshot.
    VoidR restore(const TimerSnapshot& snap);

    // --- Accessors ---
    TimerState         state()          const { return state_; }
    int                remaining_sec()  const;
    int                session_count()  const { return session_count_; }
    const std::string& current_task()   const { return current_task_; }
    const TimerConfig& config()         const { return config_; }
    void               set_config(const TimerConfig& config) { config_ = config; }
    void               set_active_tags_and_details(std::string tags, std::string details) {
        active_tags_ = std::move(tags);
        active_details_ = std::move(details);
    }

private:
    /// Duration in seconds for the current state's period.
    int current_period_sec() const;

    /// Logs a session entry to the database.
    VoidR log_session(const std::string& state_str);

    /// Transition to a new state, resetting the period start time.
    void transition_to(TimerState new_state);

    IClock*           clock_;
    IDatabaseManager* db_;
    ILogger*          logger_;
    TimerConfig       config_;

    TimerState  state_         = TimerState::Idle;
    std::string current_task_;
    std::string active_tags_;
    std::string active_details_;
    int         session_count_ = 0;

    std::chrono::steady_clock::time_point period_start_;
    std::chrono::steady_clock::time_point pause_start_;
    std::chrono::seconds                  paused_duration_{0};
    std::time_t                           session_wall_start_ = 0;
};


} // namespace tempus
