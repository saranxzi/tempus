#include "pomodoro/timer_engine.h"
#include <cassert>

namespace tempus {

TimerEngine::TimerEngine(IClock* clock, IDatabaseManager* db,
                         ILogger* logger, TimerConfig config)
    : clock_(clock)
    , db_(db)
    , logger_(logger)
    , config_(config)
{}

// ─── Commands ───────────────────────────────────────────────────────────────

VoidR TimerEngine::start(const std::string& task_name) {
    if (state_ != TimerState::Idle) {
        return VoidR::err("Cannot start: timer is not idle (current state: " +
                          std::string(to_string(state_)) + ")");
    }
    if (task_name.empty()) {
        return VoidR::err("Task name must not be empty");
    }

    current_task_       = task_name;
    session_wall_start_ = clock_->wall_time();
    paused_duration_    = std::chrono::seconds(0);
    transition_to(TimerState::Focusing);

    logger_->info("TimerEngine",
                  "Focus session started: '" + task_name + "'");
    return VoidR::ok();
}

VoidR TimerEngine::pause() {
    if (state_ != TimerState::Focusing) {
        return VoidR::err("Cannot pause: not in Focusing state");
    }

    pause_start_ = clock_->now();
    state_       = TimerState::Paused;

    logger_->info("TimerEngine", "Session paused");
    return VoidR::ok();
}

VoidR TimerEngine::resume() {
    if (state_ != TimerState::Paused) {
        return VoidR::err("Cannot resume: not in Paused state");
    }

    // Accumulate the time spent paused so it doesn't count as focus time.
    auto now = clock_->now();
    paused_duration_ += std::chrono::duration_cast<std::chrono::seconds>(
        now - pause_start_);
    state_ = TimerState::Focusing;

    logger_->info("TimerEngine", "Session resumed");
    return VoidR::ok();
}

VoidR TimerEngine::stop() {
    if (state_ == TimerState::Idle) {
        return VoidR::err("Cannot stop: timer is already idle");
    }

    if (state_ == TimerState::Focusing || state_ == TimerState::Paused) {
        // Log the session as interrupted.
        auto r = log_session("interrupted");
        if (r.is_err()) return r;
    }

    current_task_.clear();
    session_count_ = 0;
    state_         = TimerState::Idle;

    logger_->info("TimerEngine", "Timer stopped");
    return VoidR::ok();
}

// ─── Tick (main loop driver) ────────────────────────────────────────────────

Result<TimerState> TimerEngine::tick() {
    if (state_ == TimerState::Idle || state_ == TimerState::Paused) {
        return Result<TimerState>::ok(state_);
    }

    auto now     = clock_->now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - period_start_);

    // Subtract paused time only during Focusing.
    if (state_ == TimerState::Focusing) {
        elapsed -= paused_duration_;
    }

    int period = current_period_sec();

    if (elapsed.count() >= period) {
        // Current period has expired — transition.
        switch (state_) {
            case TimerState::Focusing: {
                // Log a completed focus session.
                auto r = log_session("completed");
                if (r.is_err()) return Result<TimerState>::err(r.error());

                ++session_count_;
                paused_duration_ = std::chrono::seconds(0);

                if (session_count_ >= config_.sessions_before_long_break) {
                    transition_to(TimerState::LongBreak);
                    logger_->info("TimerEngine",
                                  "Focus complete → Long break (" +
                                  std::to_string(config_.long_break_sec / 60) + " min)");
                } else {
                    transition_to(TimerState::ShortBreak);
                    logger_->info("TimerEngine",
                                  "Focus complete → Short break (" +
                                  std::to_string(config_.short_break_sec / 60) + " min)");
                }
                // Record new wall start for the break period.
                session_wall_start_ = clock_->wall_time();
                break;
            }
            case TimerState::ShortBreak:
                transition_to(TimerState::Focusing);
                session_wall_start_ = clock_->wall_time();
                paused_duration_    = std::chrono::seconds(0);
                logger_->info("TimerEngine", "Short break over → Focusing");
                break;

            case TimerState::LongBreak:
                session_count_ = 0;
                current_task_.clear();
                state_ = TimerState::Idle;
                logger_->info("TimerEngine", "Long break over → Idle");
                break;

            default:
                break;
        }
    }

    return Result<TimerState>::ok(state_);
}

// ─── Snapshot / Restore ─────────────────────────────────────────────────────

TimerSnapshot TimerEngine::snapshot() const {
    TimerSnapshot snap;
    snap.state         = state_;
    snap.remaining_sec = remaining_sec();
    snap.current_task  = current_task_;
    snap.session_count = session_count_;
    snap.timestamp     = clock_->wall_time();
    return snap;
}

VoidR TimerEngine::restore(const TimerSnapshot& snap) {
    if (snap.state == TimerState::Idle) {
        // Nothing to restore.
        return VoidR::ok();
    }

    state_         = snap.state;
    current_task_  = snap.current_task;
    session_count_ = snap.session_count;

    // Reconstruct the period_start_ so that remaining_sec() returns
    // approximately the saved value.
    int period       = current_period_sec();
    int elapsed      = period - snap.remaining_sec;
    period_start_    = clock_->now() - std::chrono::seconds(elapsed);
    paused_duration_ = std::chrono::seconds(0);
    session_wall_start_ = clock_->wall_time() - elapsed;

    logger_->info("TimerEngine",
                  "Restored state=" + std::string(to_string(state_)) +
                  " remaining=" + std::to_string(snap.remaining_sec) + "s" +
                  " task='" + current_task_ + "'");
    return VoidR::ok();
}

// ─── Accessors ──────────────────────────────────────────────────────────────

int TimerEngine::remaining_sec() const {
    if (state_ == TimerState::Idle) return 0;

    auto now     = clock_->now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - period_start_);

    if (state_ == TimerState::Paused) {
        // Don't count time since pause started.
        elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            pause_start_ - period_start_);
    }
    elapsed -= paused_duration_;

    int period    = current_period_sec();
    int remaining = period - static_cast<int>(elapsed.count());
    return (remaining > 0) ? remaining : 0;
}

int TimerEngine::current_period_sec() const {
    switch (state_) {
        case TimerState::Focusing:   return config_.focus_duration_sec;
        case TimerState::ShortBreak: return config_.short_break_sec;
        case TimerState::LongBreak:  return config_.long_break_sec;
        case TimerState::Paused:     return config_.focus_duration_sec;
        default:                     return 0;
    }
}

// ─── Private helpers ────────────────────────────────────────────────────────

VoidR TimerEngine::log_session(const std::string& state_str) {
    TaskEntry entry;
    entry.task_name    = current_task_;
    entry.started_at   = session_wall_start_;
    entry.ended_at     = clock_->wall_time();
    entry.duration_sec = static_cast<int>(entry.ended_at - entry.started_at);
    entry.state        = state_str;

    auto r = db_->log_session(entry);
    if (r.is_err()) {
        logger_->error("TimerEngine", "Failed to log session: " + r.error());
        return VoidR::err(r.error());
    }
    return VoidR::ok();
}

void TimerEngine::transition_to(TimerState new_state) {
    state_        = new_state;
    period_start_ = clock_->now();
}

} // namespace tempus
