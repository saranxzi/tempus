#pragma once
#include "pomodoro/timer_engine.h"
#include "pomodoro/database_manager.h"
#include "pomodoro/logger.h"
#include "pomodoro/types.h"
#include <string>
#include <vector>

namespace tempus {

/// Manages the ImGui user interface, styles, navigation, and view rendering.
class GuiManager {
public:
    GuiManager(TimerEngine* engine, SqliteDatabaseManager* db, ILogger* logger);
    ~GuiManager() = default;

    /// Initialize ImGui dark style theme and fonts.
    void initialize_style();

    /// Render the main frame container (collapsible sidebar + switchable main view).
    /// @param running Out-parameter to signal application exit.
    void render(bool& running);

private:
    // ─── Individual view renderers ───────────────────────────────────────────
    void render_sidebar();
    void render_time_tracking();
    void render_pomodoro();
    void render_analytics();
    void render_settings();

    // ─── Details editing panel helper ────────────────────────────────────────
    void render_details_panel();
    void save_current_details();

    // ─── Core references ─────────────────────────────────────────────────────
    TimerEngine*           engine_;
    SqliteDatabaseManager* db_;
    ILogger*               logger_;

    // ─── Navigation state ────────────────────────────────────────────────────
    int active_tab_ = 0;   // 0 = Time Tracking, 1 = Pomodoro, 2 = Analytics, 3 = Settings

    // ─── Time Tracking View state ────────────────────────────────────────────
    char                   search_query_[128] = "";
    std::vector<TaskEntry> cached_sessions_;
    int                    selected_session_idx_ = -1;  // index in cached_sessions_
    bool                   details_open_ = false;

    // Details panel inputs
    char                   edit_task_name_[128] = "";
    char                   edit_tags_[128]      = "";
    char                   edit_details_[512]   = "";
    int                    edit_duration_sec_   = 0;
    int                    start_h_ = 0, start_m_ = 0;
    int                    end_h_ = 0, end_m_ = 0;
    std::time_t            edit_started_at_     = 0;

    // ─── Pomodoro View state ─────────────────────────────────────────────────
    char                   task_name_input_[128] = "Focus Session";
    char                   tags_input_[128]      = "";
    char                   details_input_[512]   = "";

    // ─── Settings View state ─────────────────────────────────────────────────
    int                    focus_min_ = 25;
    int                    short_break_min_ = 5;
    int                    long_break_min_ = 15;
    int                    sessions_before_long_ = 4;
};

} // namespace tempus
