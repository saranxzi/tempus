#include "pomodoro/gui.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <map>

namespace tempus {

// Helper to convert time_t to YYYY-MM-DD
static std::string to_date_string(std::time_t t) {
    std::tm tm_val;
#ifdef _WIN32
    localtime_s(&tm_val, &t);
#else
    localtime_r(&t, &tm_val);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %m/%d/%Y", &tm_val);
    return std::string(buf);
}

// Helper to format duration (sec -> "Xh Ym Zs" or "Y min")
static std::string format_duration(int total_sec) {
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;
    std::string res;
    if (h > 0) res += std::to_string(h) + " h ";
    if (m > 0 || h > 0) res += std::to_string(m) + " min ";
    if (h == 0 && m == 0) res += std::to_string(s) + " sec";
    return res;
}

// Constructor
GuiManager::GuiManager(TimerEngine* engine, SqliteDatabaseManager* db, ILogger* logger)
    : engine_(engine)
    , db_(db)
    , logger_(logger)
{
    // Pre-populate configuration from engine defaults
    focus_min_ = engine_->config().focus_duration_sec / 60;
    short_break_min_ = engine_->config().short_break_sec / 60;
    long_break_min_ = engine_->config().long_break_sec / 60;
    sessions_before_long_ = engine_->config().sessions_before_long_break;

    // Load initial list from database
    auto list = db_->get_recent_sessions(100);
    if (list.is_ok()) {
        cached_sessions_ = list.value();
    }
}

// Styling initialization (WorkingHours style)
void GuiManager::initialize_style() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounding & Spacing
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;

    // Dark Color Palette
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f); // Slate black
    colors[ImGuiCol_ChildBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f); // Deeper black sidebar/accent
    colors[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_Border]                 = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.00f, 0.47f, 0.83f, 1.00f); // Blue
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.00f, 0.47f, 0.83f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.00f, 0.35f, 0.62f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.00f, 0.47f, 0.83f, 1.00f); // Accent Hover
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.00f, 0.35f, 0.62f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.00f, 0.47f, 0.83f, 0.70f);
    colors[ImGuiCol_Separator]              = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
}

// Main render entry point
void GuiManager::render(bool& running) {
    // We update database cached records if the engine logged a session.
    static TimerState last_state = TimerState::Idle;
    TimerState current_state = engine_->state();
    if (last_state != TimerState::Idle && current_state == TimerState::Idle) {
        auto list = db_->get_recent_sessions(100);
        if (list.is_ok()) cached_sessions_ = list.value();
    }
    last_state = current_state;

    // Main full-screen viewport container
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    
    ImGui::Begin("Tempus Main Window", nullptr, window_flags);

    // Left Navigation Sidebar
    render_sidebar();

    ImGui::SameLine();

    // Right Content Panel
    ImGui::BeginGroup();
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("MainContent", ImVec2(content_size.x, content_size.y), false);

    switch (active_tab_) {
        case 0: render_time_tracking(); break;
        case 1: render_pomodoro();      break;
        case 2: render_analytics();     break;
        case 3: render_settings();      break;
        default: break;
    }

    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::End();
}

// Left Sidebar Navigation Renderer
void GuiManager::render_sidebar() {
    ImGui::BeginChild("Sidebar", ImVec2(180, 0), true, ImGuiWindowFlags_NoScrollbar);

    // Title / Logo
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.00f), "  TEMPUS");
    ImGui::TextDisabled("   Time Utility");
    ImGui::Separator();
    ImGui::Spacing();

    const char* tabs[] = { "Time tracking", "Pomodoro", "Analytics", "Settings" };
    for (int i = 0; i < 4; ++i) {
        bool selected = (active_tab_ == i);
        
        // Active accent highlight bar on left edge
        if (selected) {
            ImVec2 p_min = ImGui::GetCursorScreenPos();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(p_min, ImVec2(p_min.x + 3.0f, p_min.y + 24.0f), IM_COL32(0, 120, 212, 255));
        }

        ImGui::Indent(8.0f);
        if (ImGui::Selectable(tabs[i], selected, 0, ImVec2(150, 24))) {
            active_tab_ = i;
            details_open_ = false; // close details editor
            selected_session_idx_ = -1;
        }
        ImGui::Unindent(8.0f);
        ImGui::Spacing();
    }

    ImGui::EndChild();
}

// ─── Time Tracking View ──────────────────────────────────────────────────────
void GuiManager::render_time_tracking() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));

    ImGui::Text("Time tracking");
    ImGui::Spacing();

    // Top action row
    ImGui::SetNextItemWidth(250);
    ImGui::InputTextWithHint("##Search", "Search tasks...", search_query_, sizeof(search_query_));
    ImGui::SameLine();

    if (ImGui::Button("+ Add work unit")) {
        TaskEntry entry;
        entry.task_name = "Manual Entry";
        entry.started_at = std::time(nullptr) - 1800; // 30 min ago
        entry.ended_at = std::time(nullptr);
        entry.duration_sec = 1800;
        entry.state = "completed";
        entry.tags = "Work";
        
        auto log = db_->log_session(entry);
        if (log.is_ok()) {
            auto list = db_->get_recent_sessions(100);
            if (list.is_ok()) cached_sessions_ = list.value();
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float list_width = details_open_ ? (avail.x - 280.0f) : avail.x;

    // Scrollable session logs list
    ImGui::BeginChild("SessionList", ImVec2(list_width, 0), false);

    std::string current_date;
    bool in_header = false;
    std::string filter = search_query_;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    // Grouping and displaying logs by date
    for (size_t i = 0; i < cached_sessions_.size(); ++i) {
        const auto& session = cached_sessions_[i];

        // Apply search filter
        if (!filter.empty()) {
            std::string t_name = session.task_name;
            std::transform(t_name.begin(), t_name.end(), t_name.begin(), ::tolower);
            std::string tags = session.tags;
            std::transform(tags.begin(), tags.end(), tags.begin(), ::tolower);
            if (t_name.find(filter) == std::string::npos && tags.find(filter) == std::string::npos) {
                continue;
            }
        }

        std::string s_date = to_date_string(session.started_at);
        if (s_date != current_date) {
            if (in_header) {
                ImGui::Spacing();
            }
            current_date = s_date;
            ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.00f), "%s", current_date.c_str());
            ImGui::Separator();
            in_header = true;
        }

        // Render card
        std::string label_id = "##sess_" + std::to_string(session.id);
        bool selected = (selected_session_idx_ == static_cast<int>(i));

        ImGuiStyle& style = ImGui::GetStyle();
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 avail_card = ImGui::GetContentRegionAvail();
        
        // Render item select box
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.12f, 0.12f, 1.00f));
        bool clicked = ImGui::Selectable(label_id.c_str(), selected, 0, ImVec2(avail_card.x, 36.0f));
        ImGui::PopStyleColor();

        // Draw left status accent border (Green = Completed, Red = Interrupted)
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImU32 status_color = (session.state == "completed") ? IM_COL32(46, 125, 50, 255) : IM_COL32(198, 40, 40, 255);
        draw_list->AddRectFilled(p_min, ImVec2(p_min.x + 4.0f, p_min.y + 36.0f), status_color);

        // Draw text on card overlay
        draw_list->AddText(ImVec2(p_min.x + 12.0f, p_min.y + 10.0f), IM_COL32(240, 240, 240, 255), session.task_name.c_str());
        
        // Tags Pill
        if (!session.tags.empty()) {
            ImVec2 text_sz = ImGui::CalcTextSize(session.task_name.c_str());
            ImVec2 pill_pos = ImVec2(p_min.x + 20.0f + text_sz.x, p_min.y + 9.0f);
            draw_list->AddRectFilled(pill_pos, ImVec2(pill_pos.x + 60.0f, pill_pos.y + 18.0f), IM_COL32(0, 120, 212, 100), 3.0f);
            draw_list->AddText(ImVec2(pill_pos.x + 6.0f, pill_pos.y + 2.0f), IM_COL32(180, 210, 255, 255), session.tags.c_str());
        }

        // Time and Duration
        std::string dur = format_duration(session.duration_sec);
        ImVec2 dur_sz = ImGui::CalcTextSize(dur.c_str());
        draw_list->AddText(ImVec2(p_min.x + avail_card.x - dur_sz.x - 12.0f, p_min.y + 10.0f), IM_COL32(200, 200, 200, 255), dur.c_str());

        // Context right-click menu
        std::string popup_name = "SessionContextPopup_" + std::to_string(session.id);
        if (ImGui::BeginPopupContextItem(popup_name.c_str())) {
            if (ImGui::MenuItem("Resume session")) {
                engine_->start(session.task_name);
                active_tab_ = 1; // Go to Pomodoro view
            }
            if (ImGui::MenuItem("Delete entry")) {
                db_->delete_session(session.id);
                selected_session_idx_ = -1;
                details_open_ = false;
                auto list = db_->get_recent_sessions(100);
                if (list.is_ok()) cached_sessions_ = list.value();
            }
            ImGui::EndPopup();
        }

        if (clicked) {
            selected_session_idx_ = static_cast<int>(i);
            details_open_ = true;

            // Load values to editor inputs
            strncpy_s(edit_task_name_, session.task_name.c_str(), _TRUNCATE);
            strncpy_s(edit_tags_, session.tags.c_str(), _TRUNCATE);
            strncpy_s(edit_details_, session.details.c_str(), _TRUNCATE);
            edit_duration_sec_ = session.duration_sec;
            edit_started_at_ = session.started_at;

            std::tm tm_val;
#ifdef _WIN32
            localtime_s(&tm_val, &session.started_at);
#else
            localtime_r(&session.started_at, &tm_val);
#endif
            start_h_ = tm_val.tm_hour;
            start_m_ = tm_val.tm_min;
            
            std::time_t end_t = session.ended_at;
#ifdef _WIN32
            localtime_s(&tm_val, &end_t);
#else
            localtime_r(&end_t, &tm_val);
#endif
            end_h_ = tm_val.tm_hour;
            end_m_ = tm_val.tm_min;
        }

        ImGui::Spacing();
    }

    ImGui::EndChild();

    // Right-aligned Details Panel
    if (details_open_) {
        ImGui::SameLine();
        render_details_panel();
    }

    ImGui::PopStyleVar();
}

// Details Panel view
void GuiManager::render_details_panel() {
    ImGui::BeginChild("DetailsPanel", ImVec2(270, 0), true);

    ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "Edit session details");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Task Title:");
    ImGui::InputText("##Title", edit_task_name_, sizeof(edit_task_name_));
    ImGui::Spacing();

    ImGui::Text("Tag:");
    ImGui::InputText("##Tags", edit_tags_, sizeof(edit_tags_));
    ImGui::Spacing();

    ImGui::Text("Details / Notes:");
    ImGui::InputTextMultiline("##Details", edit_details_, sizeof(edit_details_), ImVec2(0, 80));
    ImGui::Spacing();

    // Time Pickers
    ImGui::Text("Start time:");
    ImGui::PushItemWidth(50);
    ImGui::InputInt("h##start", &start_h_); ImGui::SameLine();
    ImGui::InputInt("m##start", &start_m_);
    ImGui::PopItemWidth();
    start_h_ = std::clamp(start_h_, 0, 23);
    start_m_ = std::clamp(start_m_, 0, 59);
    ImGui::Spacing();

    ImGui::Text("End time:");
    ImGui::PushItemWidth(50);
    ImGui::InputInt("h##end", &end_h_); ImGui::SameLine();
    ImGui::InputInt("m##end", &end_m_);
    ImGui::PopItemWidth();
    end_h_ = std::clamp(end_h_, 0, 23);
    end_m_ = std::clamp(end_m_, 0, 59);
    ImGui::Spacing();

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Save details", ImVec2(100, 28))) {
        save_current_details();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 28))) {
        details_open_ = false;
        selected_session_idx_ = -1;
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Delete Entry", ImVec2(240, 28))) {
        if (selected_session_idx_ >= 0) {
            db_->delete_session(cached_sessions_[selected_session_idx_].id);
            selected_session_idx_ = -1;
            details_open_ = false;
            auto list = db_->get_recent_sessions(100);
            if (list.is_ok()) cached_sessions_ = list.value();
        }
    }
    ImGui::PopStyleColor(2);

    ImGui::EndChild();
}

// Persist edited details to database
void GuiManager::save_current_details() {
    if (selected_session_idx_ < 0) return;
    TaskEntry& entry = cached_sessions_[selected_session_idx_];

    entry.task_name = edit_task_name_;
    entry.tags      = edit_tags_;
    entry.details   = edit_details_;

    // Calculate dates
    std::tm tm_start;
#ifdef _WIN32
    localtime_s(&tm_start, &edit_started_at_);
#else
    localtime_r(&edit_started_at_, &tm_start);
#endif
    tm_start.tm_hour = start_h_;
    tm_start.tm_min  = start_m_;
    tm_start.tm_sec  = 0;
    std::time_t new_start = std::mktime(&tm_start);

    std::tm tm_end = tm_start;
    tm_end.tm_hour = end_h_;
    tm_end.tm_min  = end_m_;
    tm_end.tm_sec  = 0;
    std::time_t new_end = std::mktime(&tm_end);

    // Swap if end time is before start time (e.g. overnight)
    if (new_end < new_start) {
        new_end += 24 * 3600;
    }

    entry.started_at = new_start;
    entry.ended_at   = new_end;
    entry.duration_sec = static_cast<int>(new_end - new_start);

    auto result = db_->update_session(entry);
    if (result.is_ok()) {
        logger_->info("GuiManager", "Saved entry id=" + std::to_string(entry.id));
        details_open_ = false;
        selected_session_idx_ = -1;

        // Reload from database
        auto list = db_->get_recent_sessions(100);
        if (list.is_ok()) cached_sessions_ = list.value();
    }
}

// ─── Pomodoro View (with custom drawing) ─────────────────────────────────────
void GuiManager::render_pomodoro() {
    ImGui::Text("Pomodoro");
    ImGui::Separator();
    ImGui::Spacing();

    // Horizontal Layout: Left = inputs & buttons, Right = Large circular timer widget
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float col_width = avail.x * 0.5f;

    ImGui::BeginGroup(); // Left column (Controls)
    ImGui::SetNextItemWidth(col_width - 40.0f);
    ImGui::InputTextWithHint("##TaskInput", "What are you working on?", task_name_input_, sizeof(task_name_input_));
    ImGui::Spacing();
    
    ImGui::SetNextItemWidth(col_width - 40.0f);
    ImGui::InputTextWithHint("##TagsInput", "Category tags (comma-separated)...", tags_input_, sizeof(tags_input_));
    ImGui::Spacing();

    ImGui::SetNextItemWidth(col_width - 40.0f);
    ImGui::InputTextWithHint("##DetailsInput", "Rich session notes...", details_input_, sizeof(details_input_));
    ImGui::Spacing();
    ImGui::Spacing();

    TimerState state = engine_->state();
    
    if (state == TimerState::Idle) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.47f, 0.83f, 1.00f));
        if (ImGui::Button("Start Focus", ImVec2(col_width - 40.0f, 40.0f))) {
            engine_->start(task_name_input_);
            // Set details / tags into active tracking session
            engine_->set_active_tags_and_details(tags_input_, details_input_);
        }
        ImGui::PopStyleColor();
    } else {
        if (state == TimerState::Paused) {
            if (ImGui::Button("Resume Focus", ImVec2(col_width - 40.0f, 32.0f))) {
                engine_->resume();
            }
        } else {
            if (ImGui::Button("Pause", ImVec2(col_width - 40.0f, 32.0f))) {
                engine_->pause();
            }
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Stop Focus (Log Session)", ImVec2(col_width - 40.0f, 32.0f))) {
            engine_->stop();
            // Clear inputs
            tags_input_[0] = '\0';
            details_input_[0] = '\0';
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    // Right Column (Circular progress widget)
    ImGui::BeginChild("CircleWidget", ImVec2(0, 300), false);
    ImVec2 center = ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 120.0f);
    float radius = 80.0f;

    // Calculate fraction
    int total_duration = 1;
    if (state == TimerState::Focusing || state == TimerState::Paused) {
        total_duration = engine_->config().focus_duration_sec;
    } else if (state == TimerState::ShortBreak) {
        total_duration = engine_->config().short_break_sec;
    } else if (state == TimerState::LongBreak) {
        total_duration = engine_->config().long_break_sec;
    }
    float fraction = 1.0f - (static_cast<float>(engine_->remaining_sec()) / static_cast<float>(total_duration));
    fraction = std::clamp(fraction, 0.0f, 1.0f);

    // Format remaining time text
    int m = engine_->remaining_sec() / 60;
    int s = engine_->remaining_sec() % 60;
    std::stringstream ss;
    ss << std::setw(2) << std::setfill('0') << m << ":" << std::setw(2) << std::setfill('0') << s;
    std::string text = ss.str();

    // Determine circular ring color based on state
    ImU32 ring_col = IM_COL32(180, 180, 180, 255); // grey
    if (state == TimerState::Focusing)     ring_col = IM_COL32(46, 125, 50, 255);  // green
    else if (state == TimerState::ShortBreak) ring_col = IM_COL32(21, 101, 192, 255); // blue
    else if (state == TimerState::LongBreak)  ring_col = IM_COL32(106, 27, 154, 255); // purple
    else if (state == TimerState::Paused)     ring_col = IM_COL32(239, 108, 0, 255);   // orange

    // Draw using ImDrawList PathArcTo & PathStroke
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 screen_center = ImVec2(ImGui::GetCursorScreenPos().x + center.x, ImGui::GetCursorScreenPos().y + center.y);

    // Draw background track ring
    draw_list->AddCircle(screen_center, radius, IM_COL32(43, 43, 43, 255), 64, 5.0f);

    // Draw progress arc
    if (fraction > 0.001f) {
        const float IM_PI = 3.1415926535f;
        float start_angle = -IM_PI / 2.0f; // 12 o'clock top
        float end_angle = start_angle + fraction * 2.0f * IM_PI;
        draw_list->PathClear();
        draw_list->PathArcTo(screen_center, radius, start_angle, end_angle, 64);
        draw_list->PathStroke(ring_col, 0, 5.0f);
    }

    // Large Countdown Text
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // default font
    ImGui::SetWindowFontScale(2.4f);
    ImVec2 text_sz = ImGui::CalcTextSize(text.c_str());
    draw_list->AddText(ImVec2(screen_center.x - text_sz.x * 0.5f, screen_center.y - text_sz.y * 0.5f - 5.0f), IM_COL32(245, 245, 245, 255), text.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();

    // Active state title below text
    std::string state_text = to_string(state);
    ImVec2 st_sz = ImGui::CalcTextSize(state_text.c_str());
    draw_list->AddText(ImVec2(screen_center.x - st_sz.x * 0.5f, screen_center.y + 20.0f), IM_COL32(180, 180, 180, 255), state_text.c_str());

    ImGui::EndChild();
}

// ─── Analytics View (Stacked Bar Chart Drawing) ──────────────────────────────
void GuiManager::render_analytics() {
    ImGui::Text("Analytics");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Active hours in the last 7 days:");
    ImGui::Spacing();

    // Fetch and aggregate focus hours per day for the last 7 days
    std::time_t now = std::time(nullptr);
    std::time_t seven_days_ago = now - 7 * 24 * 3600;

    std::map<std::string, int> daily_sec;
    std::vector<std::string> order;
    
    // Initialize map keys for last 7 days
    for (int i = 6; i >= 0; --i) {
        std::time_t day = now - i * 24 * 3600;
        std::string date_str = to_date_string(day);
        // Shorten label to just date: "Mon, 05/28"
        size_t comma = date_str.find(',');
        std::string label = (comma != std::string::npos) ? date_str.substr(0, comma + 7) : date_str;
        daily_sec[label] = 0;
        order.push_back(label);
    }

    // Retrieve sessions within range
    auto sessions = db_->get_sessions(seven_days_ago, now);
    if (sessions.is_ok()) {
        for (const auto& s : sessions.value()) {
            if (s.state == "completed") {
                std::string s_date = to_date_string(s.started_at);
                size_t comma = s_date.find(',');
                std::string label = (comma != std::string::npos) ? s_date.substr(0, comma + 7) : s_date;
                if (daily_sec.find(label) != daily_sec.end()) {
                    daily_sec[label] += s.duration_sec;
                }
            }
        }
    }

    // Find maximum focus hours to scale the height of the chart bars
    int max_sec = 1;
    for (const auto& pair : daily_sec) {
        if (pair.second > max_sec) max_sec = pair.second;
    }

    // Render bar chart using ImDrawList
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 chart_pos = ImGui::GetCursorScreenPos();
    ImVec2 chart_sz = ImVec2(ImGui::GetContentRegionAvail().x - 20.0f, 200.0f);
    
    // Draw background grid frame
    draw_list->AddRect(chart_pos, ImVec2(chart_pos.x + chart_sz.x, chart_pos.y + chart_sz.y), IM_COL32(40, 40, 40, 255), 4.0f);

    float bar_spacing = chart_sz.x / 7.0f;
    float bar_width   = bar_spacing * 0.5f;

    for (size_t i = 0; i < order.size(); ++i) {
        const std::string& day_label = order[i];
        int seconds = daily_sec[day_label];

        // Draw bar
        float height_ratio = static_cast<float>(seconds) / static_cast<float>(max_sec);
        float bar_height = height_ratio * (chart_sz.y - 40.0f); // leave margin for labels

        ImVec2 bar_min = ImVec2(chart_pos.x + i * bar_spacing + bar_spacing * 0.25f, chart_pos.y + chart_sz.y - 25.0f - bar_height);
        ImVec2 bar_max = ImVec2(bar_min.x + bar_width, chart_pos.y + chart_sz.y - 25.0f);

        // Highlight bar if hovered, and display tooltip
        ImVec2 mouse_pos = ImGui::GetMousePos();
        bool hovered = (mouse_pos.x >= bar_min.x && mouse_pos.x <= bar_max.x && mouse_pos.y >= bar_min.y && mouse_pos.y <= bar_max.y);
        
        ImU32 bar_color = hovered ? IM_COL32(0, 150, 255, 255) : IM_COL32(0, 120, 212, 200);
        draw_list->AddRectFilled(bar_min, bar_max, bar_color, 2.0f);

        // Date label under bar
        ImVec2 lbl_sz = ImGui::CalcTextSize(day_label.c_str());
        draw_list->AddText(ImVec2(bar_min.x + bar_width * 0.5f - lbl_sz.x * 0.5f, chart_pos.y + chart_sz.y - 20.0f), IM_COL32(180, 180, 180, 255), day_label.c_str());

        // Hover tooltip
        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", day_label.c_str());
            ImGui::Text("Focus Time: %s", format_duration(seconds).c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::Dummy(ImVec2(0.0f, chart_sz.y + 10.0f));
    ImGui::Spacing();
}

// ─── Settings View ───────────────────────────────────────────────────────────
void GuiManager::render_settings() {
    ImGui::Text("Settings");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Adjust Timer Config (Minutes):");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(200);
    ImGui::SliderInt("Focus duration", &focus_min_, 1, 60);
    ImGui::Spacing();

    ImGui::SetNextItemWidth(200);
    ImGui::SliderInt("Short break", &short_break_min_, 1, 30);
    ImGui::Spacing();

    ImGui::SetNextItemWidth(200);
    ImGui::SliderInt("Long break", &long_break_min_, 1, 60);
    ImGui::Spacing();

    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("Sessions before long break", &sessions_before_long_);
    sessions_before_long_ = std::clamp(sessions_before_long_, 1, 20);
    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Save Configuration", ImVec2(160, 32))) {
        TimerConfig config;
        config.focus_duration_sec = focus_min_ * 60;
        config.short_break_sec    = short_break_min_ * 60;
        config.long_break_sec     = long_break_min_ * 60;
        config.sessions_before_long_break = sessions_before_long_;

        engine_->set_config(config);
        logger_->info("GuiManager", "Updated timer configuration values");
    }
}

} // namespace tempus
