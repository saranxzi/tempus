#pragma once
#include "pomodoro/timer_engine.h"
#include "pomodoro/database_manager.h"
#include "pomodoro/logger.h"
#include "pomodoro/types.h"
#include "pomodoro/config_manager.h"
#include <string>
#include <vector>
#include <filesystem>

struct ImFont;

namespace tempus {

/// Manages the ImGui user interface, styles, navigation, and view rendering.
class GuiManager {
public:
    GuiManager(TimerEngine* engine, SqliteDatabaseManager* db, ILogger* logger, std::filesystem::path config_path);
    ~GuiManager() = default;

    /// Initialize ImGui dark style theme and fonts.
    void initialize_style();

    /// Render the main frame container (collapsible sidebar + switchable main view).
    /// @param running Out-parameter to signal application exit.
    /// @param hwnd Native window handle (HWND on Windows).
    void render(bool& running, void* hwnd = nullptr);

private:
    // ─── Individual view renderers ───────────────────────────────────────────
    void render_sidebar();
    void render_dashboard();
    void render_time_tracking();
    void render_pomodoro();
    void render_analytics();
    void render_settings(void* hwnd);

    // ─── Style & Styling presets ─────────────────────────────────────────────
    void apply_theme(int theme_idx);

    // ─── Details editing panel helper ────────────────────────────────────────
    void render_details_panel();
    void save_current_details();

    // ─── Core references ─────────────────────────────────────────────────────
    TimerEngine*           engine_;
    SqliteDatabaseManager* db_;
    ILogger*               logger_;
    std::filesystem::path  config_path_;
    AppConfig              config_;

    // ─── Navigation state ────────────────────────────────────────────────────
    int active_tab_ = 0;   // 0 = Dashboard, 1 = Time Tracking, 2 = Pomodoro, 3 = Analytics, 4 = Settings
    int last_tab_ = -1;
    float fade_alpha_ = 0.0f;

    // Fonts
    ImFont* font_regular_ = nullptr;
    ImFont* font_large_ = nullptr;
    ImFont* font_header_ = nullptr;

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
    int                    edit_year_ = 2026;
    int                    edit_month_ = 5;
    int                    edit_day_ = 29;
    std::time_t            edit_started_at_     = 0;

    // ─── Pomodoro View state ─────────────────────────────────────────────────
    char                   task_name_input_[128] = "Focus Session";
    char                   tags_input_[128]      = "";
    char                   details_input_[512]   = "";

    // ─── Soundscape & Focus Checklist state ──────────────────────────────────
    bool                   soundscape_playing_ = false;
    int                    soundscape_track_ = 0;       // 0=Rain, 1=Forest, 2=Waves, 3=White Noise
    float                  soundscape_volume_ = 0.5f;
    char                   new_todo_input_[128] = "";
    std::vector<std::string> focus_todos_;
    std::vector<bool>      focus_todos_done_;

    // ─── Settings View state ─────────────────────────────────────────────────
    int                    focus_min_ = 25;
    int                    short_break_min_ = 5;
    int                    long_break_min_ = 15;
    int                    sessions_before_long_ = 4;
    bool                   db_reset_confirm_ = false;
    char                   backup_path_[256] = "";

    // ─── Stopwatch & Mini View state ─────────────────────────────────────────
    int                    timer_mode_ = 0;             // 0 = Pomodoro, 1 = Stopwatch
    bool                   stopwatch_active_ = false;
    bool                   stopwatch_paused_ = false;
    int                    stopwatch_elapsed_sec_ = 0;
    std::time_t            stopwatch_last_time_ = 0;
    char                   stopwatch_task_name_[128] = "Stopwatch Session";
    char                   stopwatch_tags_[128]      = "";
    char                   stopwatch_details_[512]   = "";
    bool                   mini_mode_ = false;
    bool                   launch_float_popup_open_ = false;
    char                   float_task_name_[128] = "Floating Session";
    char                   float_tags_[128] = "Focus";

    void render_mini_view(void* hwnd);
};

} // namespace tempus
