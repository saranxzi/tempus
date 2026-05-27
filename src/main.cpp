/// @file main.cpp
/// @brief Application entry point. Wires the DI graph and runs the idle loop.
///
/// The UI layer (Dear ImGui / DirectX) is intentionally stubbed out in this
/// phase. The main loop simply ticks the timer engine and periodically saves
/// state for crash recovery.

#include "pomodoro/app_paths.h"
#include "pomodoro/logger.h"
#include "pomodoro/database_manager.h"
#include "pomodoro/timer_engine.h"
#include "pomodoro/state_recovery.h"
#include "pomodoro/clock.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdlib>
#include <string>

/// WinMain entry point (no console window in release builds).
/// Falls through to run() which contains the actual logic.
static int run();

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return run();
}
#else
int main() {
    return run();
}
#endif

static int run() {
    using namespace tempus;

    // ── 1. Resolve data directory ───────────────────────────────────────────
    auto paths_result = AppPaths::initialize();
    if (paths_result.is_err()) {
        // Cannot even log yet — write to stderr as a last resort.
        // (Will not be visible in the WIN32 release build.)
        return EXIT_FAILURE;
    }
    const AppPaths& paths = paths_result.value();

    // ── 2. Logger ───────────────────────────────────────────────────────────
    RotatingFileLogger logger(paths.log_path());
    logger.info("Main", "=== Tempus v0.1.0 starting ===");
    logger.info("Main", "Data directory: " + paths.data_dir().string());

    // ── 3. Database ─────────────────────────────────────────────────────────
    SqliteDatabaseManager db(paths.db_path().string(), &logger);
    auto db_init = db.initialize();
    if (db_init.is_err()) {
        logger.error("Main", "Database initialization failed: " + db_init.error());
        return EXIT_FAILURE;
    }

    // ── 4. Timer Engine ─────────────────────────────────────────────────────
    SystemClock clock;
    TimerEngine engine(&clock, &db, &logger);

    // ── 5. State Recovery ───────────────────────────────────────────────────
    auto snap_result = StateRecovery::load(paths.state_file_path(), &logger);
    if (snap_result.is_ok() && snap_result.value().has_value()) {
        auto restore = engine.restore(snap_result.value().value());
        if (restore.is_ok()) {
            StateRecovery::clear(paths.state_file_path());
            logger.info("Main", "Timer state restored from previous session");
        } else {
            logger.warn("Main", "State restore failed: " + restore.error());
        }
    }

    // ── 6. Main loop (UI stub) ──────────────────────────────────────────────
    logger.info("Main", "UI layer not yet implemented — entering idle loop");
    logger.info("Main", "Press Ctrl+C to terminate (debug builds only)");

    constexpr int TICK_INTERVAL_MS   = 100;   // 10 Hz tick rate
    constexpr int STATE_SAVE_TICKS   = 600;   // Save state every 60 seconds
    int tick_counter = 0;

    bool running = true;
    while (running) {
        auto tick_result = engine.tick();
        if (tick_result.is_err()) {
            logger.error("Main", "Tick error: " + tick_result.error());
        }

        // Periodic state snapshot for crash recovery.
        if (++tick_counter >= STATE_SAVE_TICKS) {
            tick_counter = 0;
            if (engine.state() != TimerState::Idle) {
                auto save = StateRecovery::save(
                    engine.snapshot(), paths.state_file_path(), &logger);
                if (save.is_err()) {
                    logger.error("Main", "State save failed: " + save.error());
                }
            }
        }

#ifdef _WIN32
        Sleep(TICK_INTERVAL_MS);
#else
        struct timespec ts = { 0, TICK_INTERVAL_MS * 1000000L };
        nanosleep(&ts, nullptr);
#endif
    }

    logger.info("Main", "=== Tempus shutting down ===");
    return EXIT_SUCCESS;
}
