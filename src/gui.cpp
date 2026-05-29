#include "pomodoro/gui.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <iomanip>
#include <sstream>
#include <fstream>
#include <ctime>
#include <algorithm>
#include <map>
#include <filesystem>
#include <set>
#include <cmath>
#include <cstdlib>

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

static std::string format_time_range(std::time_t start, std::time_t end) {
    std::tm tm_start, tm_end;
#ifdef _WIN32
    localtime_s(&tm_start, &start);
    localtime_s(&tm_end, &end);
#else
    localtime_r(&start, &tm_start);
    localtime_r(&end, &tm_end);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%H:%M", &tm_start);
    std::string s_str(buf);
    std::strftime(buf, sizeof(buf), "%H:%M", &tm_end);
    std::string e_str(buf);
    return s_str + " - " + e_str;
}

static std::string to_datetime_string(std::time_t t) {
    std::tm tm_val;
#ifdef _WIN32
    localtime_s(&tm_val, &t);
#else
    localtime_r(&t, &tm_val);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
    return std::string(buf);
}

// Constructor
GuiManager::GuiManager(TimerEngine* engine, SqliteDatabaseManager* db, ILogger* logger, std::filesystem::path config_path)
    : engine_(engine)
    , db_(db)
    , logger_(logger)
    , config_path_(std::move(config_path))
{
    // Load configuration from path
    auto load_result = ConfigManager::load(config_path_);
    if (load_result.is_ok()) {
        config_ = load_result.value();
    }

    // Sync loaded configuration to engine
    TimerConfig engine_cfg;
    engine_cfg.focus_duration_sec = config_.focus_duration_sec;
    engine_cfg.short_break_sec = config_.short_break_sec;
    engine_cfg.long_break_sec = config_.long_break_sec;
    engine_cfg.sessions_before_long_break = config_.sessions_before_long_break;
    engine_->set_config(engine_cfg);

    // Sync loaded configuration to UI member variables
    focus_min_ = config_.focus_duration_sec / 60;
    short_break_min_ = config_.short_break_sec / 60;
    long_break_min_ = config_.long_break_sec / 60;
    sessions_before_long_ = config_.sessions_before_long_break;

    // Initialize default checklist items
    focus_todos_ = { "Deep Work Block", "Code Implementation", "Self-Review & Verify" };
    focus_todos_done_ = { false, false, false };

    // Initialize default backup path
    char* userprofile = std::getenv("USERPROFILE");
    if (userprofile != nullptr) {
        sprintf_s(backup_path_, "%s\\Downloads\\tempus_backup.db", userprofile);
    } else {
        strcpy_s(backup_path_, "tempus_backup.db");
    }

    // Load initial list from database
    auto list = db_->get_recent_sessions(100);
    if (list.is_ok()) {
        cached_sessions_ = list.value();
    }
}

// Styling initialization (WorkingHours style)
void GuiManager::initialize_style() {
    ImGuiIO& io = ImGui::GetIO();
    std::string font_path = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (std::filesystem::exists(font_path)) {
        font_regular_ = io.Fonts->AddFontFromFileTTF(font_path.c_str(), 18.0f);
        font_header_  = io.Fonts->AddFontFromFileTTF(font_path.c_str(), 24.0f);
        font_large_   = io.Fonts->AddFontFromFileTTF(font_path.c_str(), 56.0f);
    } else {
        font_regular_ = io.Fonts->AddFontDefault();
        font_header_  = font_regular_;
        font_large_   = font_regular_;
    }

    apply_theme(config_.theme_index);
}

void GuiManager::apply_theme(int theme_idx) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;

    if (theme_idx == 0) {
        // Midnight Blue Theme (Deep Slate Blue background, Navy panels, Indigo Accent)
        colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.55f, 0.60f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.09f, 0.10f, 0.15f, 1.00f); 
        colors[ImGuiCol_ChildBg]                = ImVec4(0.06f, 0.07f, 0.10f, 1.00f); 
        colors[ImGuiCol_PopupBg]                = ImVec4(0.11f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_Border]                 = ImVec4(0.18f, 0.22f, 0.33f, 1.00f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.14f, 0.16f, 0.24f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.24f, 0.35f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.25f, 0.30f, 0.45f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.06f, 0.07f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.08f, 0.10f, 0.15f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.06f, 0.07f, 0.10f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.06f, 0.07f, 0.10f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.22f, 0.27f, 0.40f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.30f, 0.36f, 0.52f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.38f, 0.45f, 0.65f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.39f, 0.44f, 0.96f, 1.00f); 
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.39f, 0.44f, 0.96f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.25f, 0.30f, 0.85f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.18f, 0.22f, 0.38f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.39f, 0.44f, 0.96f, 1.00f); 
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.25f, 0.30f, 0.85f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.15f, 0.18f, 0.30f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.22f, 0.27f, 0.42f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.39f, 0.44f, 0.96f, 0.70f);
        colors[ImGuiCol_Separator]              = ImVec4(0.18f, 0.22f, 0.33f, 1.00f);
    } else if (theme_idx == 1) {
        // Obsidian Dark Theme (Pitch Black background, Charcoal, Silver Accent)
        colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.05f, 0.05f, 0.05f, 1.00f); 
        colors[ImGuiCol_ChildBg]                = ImVec4(0.02f, 0.02f, 0.02f, 1.00f); 
        colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_Border]                 = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.70f, 0.70f, 0.70f, 1.00f); 
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    } else if (theme_idx == 2) {
        // Forest Mint Theme (Sage Green and Emerald Highlights on deep charcoal)
        colors[ImGuiCol_Text]                   = ImVec4(0.92f, 0.95f, 0.93f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.45f, 0.52f, 0.48f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.11f, 0.13f, 0.12f, 1.00f); 
        colors[ImGuiCol_ChildBg]                = ImVec4(0.08f, 0.10f, 0.09f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.13f, 0.16f, 0.14f, 1.00f);
        colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.25f, 0.22f, 1.00f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.16f, 0.20f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.28f, 0.25f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.28f, 0.35f, 0.31f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.10f, 0.09f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.11f, 0.13f, 0.12f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.08f, 0.10f, 0.09f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.08f, 0.10f, 0.09f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.22f, 0.28f, 0.25f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.30f, 0.38f, 0.34f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.38f, 0.48f, 0.43f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.10f, 0.72f, 0.50f, 1.00f); 
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.10f, 0.72f, 0.50f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.08f, 0.58f, 0.40f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.18f, 0.25f, 0.21f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.10f, 0.72f, 0.50f, 1.00f); 
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.08f, 0.58f, 0.40f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.15f, 0.22f, 0.18f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.22f, 0.30f, 0.25f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.10f, 0.72f, 0.50f, 0.70f);
        colors[ImGuiCol_Separator]              = ImVec4(0.20f, 0.25f, 0.22f, 1.00f);
    } else {
        // Slate Classic (default ImGui Dark themed Slate)
        colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f); 
        colors[ImGuiCol_ChildBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f); 
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
        colors[ImGuiCol_CheckMark]              = ImVec4(0.00f, 0.47f, 0.83f, 1.00f); 
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.00f, 0.47f, 0.83f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.00f, 0.35f, 0.62f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.00f, 0.47f, 0.83f, 1.00f); 
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.00f, 0.35f, 0.62f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.00f, 0.47f, 0.83f, 0.70f);
        colors[ImGuiCol_Separator]              = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    }
}

// Main render entry point
void GuiManager::render(bool& running, void* hwnd) {
    // Stopwatch Ticking Logic
    if (stopwatch_active_ && !stopwatch_paused_) {
        std::time_t now = std::time(nullptr);
        if (stopwatch_last_time_ == 0) {
            stopwatch_last_time_ = now;
        }
        if (now > stopwatch_last_time_) {
            stopwatch_elapsed_sec_ += static_cast<int>(now - stopwatch_last_time_);
            stopwatch_last_time_ = now;
        }
    } else {
        stopwatch_last_time_ = 0;
    }

    // Handle mini mode window size adjustments dynamically
    static bool was_mini = false;
    if (mini_mode_ != was_mini) {
        was_mini = mini_mode_;
        if (hwnd) {
#ifdef _WIN32
            if (mini_mode_) {
                ::SetWindowPos((HWND)hwnd, HWND_TOPMOST, 0, 0, 280, 165, SWP_NOMOVE);
            } else {
                HWND topmost_handle = config_.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST;
                ::SetWindowPos((HWND)hwnd, topmost_handle, 0, 0, 960, 600, SWP_NOMOVE);
            }
#endif
        }
    }

    // Render compact Mini View if active
    if (mini_mode_) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("Tempus Mini Window", nullptr, window_flags);
        render_mini_view(hwnd);
        ImGui::End();
        return;
    }

    // Set topmost window state on first frame if config is set (fallback)
    static bool first_frame = true;
    if (first_frame) {
        first_frame = false;
        if (hwnd && config_.always_on_top) {
#ifdef _WIN32
            ::SetWindowPos((HWND)hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
#endif
        }
    }

    // We update database cached records and trigger system sound alerts if state transitions
    static TimerState sound_last_state = TimerState::Idle;
    TimerState current_state = engine_->state();
    if (sound_last_state != current_state) {
        if (config_.sound_enabled) {
            #ifdef _WIN32
            // Focus ended -> transition to Break
            if (sound_last_state == TimerState::Focusing && 
                (current_state == TimerState::ShortBreak || current_state == TimerState::LongBreak)) {
                ::MessageBeep(MB_ICONINFORMATION);
            }
            // Break ended -> transition to Idle or Focusing
            else if ((sound_last_state == TimerState::ShortBreak || sound_last_state == TimerState::LongBreak) && 
                     (current_state == TimerState::Idle || current_state == TimerState::Focusing)) {
                ::MessageBeep(MB_ICONEXCLAMATION);
            }
            #endif
        }

        // If a session ended, reload cached sessions from DB
        if (sound_last_state != TimerState::Idle && current_state == TimerState::Idle) {
            auto list = db_->get_recent_sessions(100);
            if (list.is_ok()) cached_sessions_ = list.value();
        }
        sound_last_state = current_state;
    }

    // Main full-screen viewport container
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    
    ImGui::Begin("Tempus Main Window", nullptr, window_flags);

    // Launch Floating Stopwatch Modal
    if (ImGui::BeginPopupModal("Launch Floating Stopwatch", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter Task Details:");
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Text("Task Name:");
        ImGui::InputText("##float_task", float_task_name_, sizeof(float_task_name_));
        ImGui::Spacing();
        
        ImGui::Text("Tags (comma separated):");
        ImGui::InputText("##float_tags", float_tags_, sizeof(float_tags_));
        ImGui::Spacing();
        
        if (ImGui::Button("Start Floating Clock", ImVec2(160, 30))) {
            stopwatch_active_ = true;
            stopwatch_paused_ = false;
            stopwatch_elapsed_sec_ = 0;
            stopwatch_last_time_ = std::time(nullptr);
            timer_mode_ = 1; // switch to stopwatch
            
            strcpy_s(stopwatch_task_name_, float_task_name_);
            strcpy_s(stopwatch_tags_, float_tags_);
            stopwatch_details_[0] = '\0';
            
            mini_mode_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 30))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Left Navigation Sidebar
    render_sidebar();

    ImGui::SameLine();

    // Tab Switch Transition Fade-in Animation
    if (active_tab_ != last_tab_) {
        last_tab_ = active_tab_;
        fade_alpha_ = 0.0f;
    }
    if (fade_alpha_ < 1.0f) {
        fade_alpha_ += ImGui::GetIO().DeltaTime * 4.0f; // 250ms fade-in
        if (fade_alpha_ > 1.0f) fade_alpha_ = 1.0f;
    }

    // Right Content Panel
    ImGui::BeginGroup();
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("MainContent", ImVec2(content_size.x, content_size.y), false);
    
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade_alpha_);

    switch (active_tab_) {
        case 0: render_dashboard();     break;
        case 1: render_time_tracking(); break;
        case 2: render_pomodoro();      break;
        case 3: render_analytics();     break;
        case 4: render_settings(hwnd);  break;
        default: break;
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::End();
}

// Left Sidebar Navigation Renderer
void GuiManager::render_sidebar() {
    ImGui::BeginChild("Sidebar", ImVec2(185, 0), true, ImGuiWindowFlags_NoScrollbar);

    // Title / Logo with custom vector clock
    ImGui::Spacing();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImVec2 clock_center = ImVec2(cursor_pos.x + 22.0f, cursor_pos.y + 16.0f);
    float clock_rad = 10.0f;
    
    // Draw clock face
    draw_list->AddCircle(clock_center, clock_rad, IM_COL32(0, 120, 212, 255), 32, 2.0f);
    // Draw clock hands (minute and hour)
    draw_list->AddLine(clock_center, ImVec2(clock_center.x, clock_center.y - 6.0f), IM_COL32(240, 240, 240, 255), 1.5f);
    draw_list->AddLine(clock_center, ImVec2(clock_center.x + 4.5f, clock_center.y), IM_COL32(240, 240, 240, 255), 1.5f);
    
    ImGui::Indent(38.0f);
    ImFont* header_font = font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0];
    ImGui::PushFont(header_font);
    ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.00f), "TEMPUS");
    ImGui::PopFont();
    ImGui::Unindent(38.0f);
    
    ImGui::TextDisabled("   Time Utility");
    ImGui::Separator();
    ImGui::Spacing();

    const char* tabs[] = { "Dashboard", "Time tracking", "Pomodoro", "Analytics", "Settings" };
    for (int i = 0; i < 5; ++i) {
        bool selected = (active_tab_ == i);
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        
        // Active accent highlight bar on left edge
        if (selected) {
            draw_list->AddRectFilled(p_min, ImVec2(p_min.x + 3.0f, p_min.y + 24.0f), IM_COL32(0, 120, 212, 255));
        }

        // Draw soft color dot indicator for each view
        ImU32 dot_color = IM_COL32(107, 114, 128, 255); // default grey
        if (i == 0)      dot_color = IM_COL32(59, 130, 246, 255);  // blue
        else if (i == 1) dot_color = IM_COL32(13, 148, 136, 255);  // teal
        else if (i == 2) dot_color = IM_COL32(16, 185, 129, 255);  // green
        else if (i == 3) dot_color = IM_COL32(139, 92, 246, 255);  // purple

        ImVec2 dot_center = ImVec2(p_min.x + 16.0f, p_min.y + 12.0f);
        draw_list->AddCircleFilled(dot_center, 4.0f, dot_color);

        ImGui::Indent(24.0f);
        if (ImGui::Selectable(tabs[i], selected, 0, ImVec2(135, 24))) {
            active_tab_ = i;
            details_open_ = false; // close details editor
            selected_session_idx_ = -1;
        }
        ImGui::Unindent(24.0f);
        ImGui::Spacing();
    }

    ImGui::EndChild();
}

static std::time_t get_start_of_day(std::time_t t) {
    std::tm tm_val;
#ifdef _WIN32
    localtime_s(&tm_val, &t);
#else
    localtime_r(&t, &tm_val);
#endif
    tm_val.tm_hour = 0;
    tm_val.tm_min = 0;
    tm_val.tm_sec = 0;
    return std::mktime(&tm_val);
}

static std::time_t get_start_of_week(std::time_t t) {
    std::tm tm_val;
#ifdef _WIN32
    localtime_s(&tm_val, &t);
#else
    localtime_r(&t, &tm_val);
#endif
    int days_to_subtract = tm_val.tm_wday == 0 ? 6 : tm_val.tm_wday - 1;
    std::time_t start_of_day = get_start_of_day(t);
    return start_of_day - days_to_subtract * 24 * 3600;
}

void GuiManager::render_dashboard() {
    ImFont* header_font = font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0];
    ImGui::PushFont(header_font);
    ImGui::Text("Dashboard");
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::Spacing();

    // 1. Calculate time tracked Today and This Week
    std::time_t now = std::time(nullptr);
    std::time_t start_of_today = get_start_of_day(now);
    std::time_t start_of_week = get_start_of_week(now);

    int today_seconds = 0;
    int week_seconds = 0;
    int today_sessions_count = 0;

    for (const auto& s : cached_sessions_) {
        if (s.state == "completed") {
            if (s.started_at >= start_of_today) {
                today_seconds += s.duration_sec;
                today_sessions_count++;
            }
            if (s.started_at >= start_of_week) {
                week_seconds += s.duration_sec;
            }
        }
    }

    // 2. Display summary cards in columns
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float card_width = (avail.x - 30.0f) / 3.0f; // 3 columns
    
    // Draw columns
    ImGui::BeginGroup();
    
    // --- COLUMN 1: Today's Time ---
    ImGui::BeginChild("TodayCard", ImVec2(card_width, 100), true);
    ImGui::TextDisabled("TODAY'S WORK");
    ImGui::Spacing();
    ImGui::PushFont(font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0]);
    std::string today_dur_str = format_duration(today_seconds);
    ImGui::TextColored(ImVec4(0.10f, 0.72f, 0.50f, 1.00f), "%s", today_dur_str.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("Sessions: %d", today_sessions_count);
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // --- COLUMN 2: Weekly Goal Progress ---
    ImGui::BeginChild("WeekCard", ImVec2(card_width, 100), true);
    ImGui::TextDisabled("WEEKLY GOAL PROGRESS");
    ImGui::Spacing();
    
    int goal_sec = config_.weekly_goal_hours * 3600;
    float week_hours = static_cast<float>(week_seconds) / 3600.0f;
    float pct = goal_sec > 0 ? static_cast<float>(week_seconds) / static_cast<float>(goal_sec) : 0.0f;
    pct = std::clamp(pct, 0.0f, 1.0f);
    
    ImGui::PushFont(font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.00f), "%.1f / %d h", week_hours, config_.weekly_goal_hours);
    ImGui::PopFont();
    
    // Progress bar
    ImGui::SetNextItemWidth(card_width - 24.0f);
    char buf[32];
    sprintf_s(buf, "%.0f%%", pct * 100.0f);
    ImGui::ProgressBar(pct, ImVec2(0, 16), buf);
    ImGui::EndChild();
    
    ImGui::SameLine();

    // --- COLUMN 3: Active Status Card ---
    ImGui::BeginChild("ActiveCard", ImVec2(card_width, 100), true);
    TimerState state = engine_->state();
    if (state == TimerState::Idle) {
        ImGui::TextDisabled("STATUS: INACTIVE");
        ImGui::Spacing();
        ImGui::Text("No running timers.");
        if (ImGui::Button("Go to Timer", ImVec2(card_width - 24.0f, 26.0f))) {
            active_tab_ = 2; // switch to Pomodoro view
        }
    } else {
        std::string state_name = to_string(state);
        ImGui::TextDisabled("STATUS: %s", state_name.c_str());
        ImGui::Spacing();
        
        // remaining countdown
        int m = engine_->remaining_sec() / 60;
        int s = engine_->remaining_sec() % 60;
        char count_buf[32];
        sprintf_s(count_buf, "%02d:%02d remaining", m, s);
        
        ImGui::PushFont(font_regular_ ? font_regular_ : ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::TextColored(ImVec4(0.95f, 0.60f, 0.10f, 1.00f), "%s", count_buf);
        ImGui::PopFont();
        
        // quick control button
        if (ImGui::Button("Pause/Stop", ImVec2(card_width - 24.0f, 26.0f))) {
            active_tab_ = 2; // go to Pomodoro view
        }
    }
    ImGui::EndChild();
    
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 3. Lower Grid: Left = Quick Start / Log, Right = Recent Activity
    float bottom_height = 190.0f;
    float col_w = (avail.x - 20.0f) * 0.5f;
 
     ImGui::BeginGroup();
     // Left Box: Quick Task Initiation
     ImGui::BeginChild("QuickStart", ImVec2(col_w, bottom_height), true);
     ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.00f), "Quick Logger");
     ImGui::Spacing();
     
     ImGui::Text("Start tracking instantly:");
     ImGui::Spacing();
 
     static char quick_task_[128] = "";
     ImGui::InputTextWithHint("Task name##quick", "What are you working on?", quick_task_, sizeof(quick_task_));
     ImGui::Spacing();
     
     static char quick_tag_[128] = "";
     ImGui::InputTextWithHint("Tags##quick", "Comma-separated tags...", quick_tag_, sizeof(quick_tag_));
     ImGui::Spacing();
 
     if (state == TimerState::Idle && !stopwatch_active_) {
         if (ImGui::Button("Start Pomodoro Focus", ImVec2(col_w - 24.0f, 26.0f))) {
             std::string t_name = quick_task_[0] == '\0' ? "Focus Session" : quick_task_;
             engine_->start(t_name);
             engine_->set_active_tags_and_details(quick_tag_, "Quick logged session from Dashboard.");
             strncpy_s(task_name_input_, t_name.c_str(), _TRUNCATE);
             strncpy_s(tags_input_, quick_tag_, _TRUNCATE);
             details_input_[0] = '\0';
             active_tab_ = 2; // Jump to Pomodoro view
         }
         ImGui::Spacing();
         if (ImGui::Button("Launch Floating Stopwatch", ImVec2(col_w - 24.0f, 26.0f))) {
             strncpy_s(float_task_name_, quick_task_[0] == '\0' ? "Floating Session" : quick_task_, _TRUNCATE);
             strncpy_s(float_tags_, quick_tag_[0] == '\0' ? "Focus" : quick_tag_, _TRUNCATE);
             ImGui::OpenPopup("Launch Floating Stopwatch");
         }
     } else {
         ImGui::TextColored(ImVec4(0.95f, 0.60f, 0.10f, 1.00f), "A session is active!");
         if (ImGui::Button("View Timer Status", ImVec2(col_w - 24.0f, 32.0f))) {
             active_tab_ = 2;
         }
     }
     ImGui::EndChild();
 
     ImGui::SameLine();
 
     // Right Box: Recent Activity Stream (each item is clickable to go to Time Tracking view)
     ImGui::BeginChild("ActivityStream", ImVec2(col_w, bottom_height), true);
     ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.00f), "Recent Activity (Click to edit)");
     ImGui::Spacing();
 
     int shown_count = 0;
     for (const auto& s : cached_sessions_) {
         if (shown_count >= 3) break;
         shown_count++;
 
         // Draw individual item as a soft card using Selectable for click detection
         std::string card_lbl = "##recent_card_" + std::to_string(s.id);
         ImVec2 p_min = ImGui::GetCursorScreenPos();
         ImVec2 card_sz = ImVec2(col_w - 24.0f, 44.0f);
         
         bool clicked = ImGui::Selectable(card_lbl.c_str(), false, 0, card_sz);
         ImDrawList* d_list = ImGui::GetWindowDrawList();
         
         ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_FrameBg);
         if (ImGui::IsItemHovered()) {
             bg_col = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
         }
         d_list->AddRectFilled(p_min, ImVec2(p_min.x + card_sz.x, p_min.y + card_sz.y), bg_col, 6.0f);
         d_list->AddRect(p_min, ImVec2(p_min.x + card_sz.x, p_min.y + card_sz.y), ImGui::GetColorU32(ImGuiCol_Border), 6.0f);
         
         // draw a small color bar on left depending on tag/state
         ImU32 col = (s.state == "completed") ? IM_COL32(16, 185, 129, 255) : IM_COL32(239, 68, 68, 255);
         d_list->AddRectFilled(p_min, ImVec2(p_min.x + 3.0f, p_min.y + card_sz.y), col, 6.0f, ImDrawFlags_RoundCornersLeft);
         
         d_list->AddText(ImVec2(p_min.x + 12.0f, p_min.y + 6.0f), IM_COL32(245, 245, 245, 255), s.task_name.c_str());
         std::string subtitle = format_time_range(s.started_at, s.ended_at) + " | " + format_duration(s.duration_sec);
         d_list->AddText(ImVec2(p_min.x + 12.0f, p_min.y + 24.0f), IM_COL32(180, 180, 180, 255), subtitle.c_str());
         
         if (clicked) {
             // Go to time tracking tab (index 1) and select the item
             active_tab_ = 1;
             for (size_t idx = 0; idx < cached_sessions_.size(); ++idx) {
                 if (cached_sessions_[idx].id == s.id) {
                     selected_session_idx_ = static_cast<int>(idx);
                     details_open_ = true;
                     
                     // Load to details panel
                     strncpy_s(edit_task_name_, cached_sessions_[idx].task_name.c_str(), _TRUNCATE);
                     strncpy_s(edit_tags_, cached_sessions_[idx].tags.c_str(), _TRUNCATE);
                     strncpy_s(edit_details_, cached_sessions_[idx].details.c_str(), _TRUNCATE);
                     edit_duration_sec_ = cached_sessions_[idx].duration_sec;
                     edit_started_at_ = cached_sessions_[idx].started_at;
                     
                     std::tm tm_val;
 #ifdef _WIN32
                     localtime_s(&tm_val, &cached_sessions_[idx].started_at);
 #else
                     localtime_r(&cached_sessions_[idx].started_at, &tm_val);
 #endif
                     start_h_ = tm_val.tm_hour;
                     start_m_ = tm_val.tm_min;
                     edit_year_ = tm_val.tm_year + 1900;
                     edit_month_ = tm_val.tm_mon + 1;
                     edit_day_ = tm_val.tm_mday;
                     
                     std::time_t end_t = cached_sessions_[idx].ended_at;
 #ifdef _WIN32
                     localtime_s(&tm_val, &end_t);
 #else
                     localtime_r(&end_t, &tm_val);
 #endif
                     end_h_ = tm_val.tm_hour;
                     end_m_ = tm_val.tm_min;
                     break;
                 }
             }
         }
         
         ImGui::Spacing();
     }
     if (shown_count == 0) {
         ImGui::TextDisabled("No recent activity logged.");
     }
     ImGui::EndChild();
     ImGui::EndGroup();
 
     // 4. Fill bottom half: Focus Heatmap (Activity Stream) and Insights & Streaks
     ImGui::Spacing();
     ImGui::Separator();
     ImGui::Spacing();
 
     ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.00f), "Focus Heatmap & Streaks");
     ImGui::Spacing();
 
     // Calculate streaks
     int current_streak = 0;
     int longest_streak = 0;
     int total_active_days = 0;
     std::time_t now = std::time(nullptr);
     
     std::set<std::string> active_days_set;
     for (const auto& s : cached_sessions_) {
         if (s.state == "completed") {
             active_days_set.insert(to_date_string(s.started_at));
         }
     }
     
     std::string today_str = to_date_string(now);
     std::string yesterday_str = to_date_string(now - 24 * 3600);
     
     bool active_today = (active_days_set.find(today_str) != active_days_set.end());
     bool active_yesterday = (active_days_set.find(yesterday_str) != active_days_set.end());
     
     if (active_today || active_yesterday) {
         int check_day = active_today ? 0 : 1;
         while (true) {
             std::string check_str = to_date_string(now - check_day * 24 * 3600);
             if (active_days_set.find(check_str) != active_days_set.end()) {
                 current_streak++;
                 check_day++;
             } else {
                 break;
             }
         }
     }
     
     int temp_streak = 0;
     for (int i = 365; i >= 0; --i) {
         std::string check_str = to_date_string(now - i * 24 * 3600);
         if (active_days_set.find(check_str) != active_days_set.end()) {
             temp_streak++;
             if (temp_streak > longest_streak) {
                 longest_streak = temp_streak;
             }
         } else {
             temp_streak = 0;
         }
     }
     total_active_days = static_cast<int>(active_days_set.size());
 
     // Completed/interrupted today metrics
     int completed_count = 0;
     int interrupted_count = 0;
     for (const auto& s : cached_sessions_) {
         if (s.started_at >= start_of_today && s.state == "completed") {
             completed_count++;
         } else if (s.started_at >= start_of_today && s.state == "interrupted") {
             interrupted_count++;
         }
     }
 
     float completion_rate = (completed_count + interrupted_count) > 0 ? 
         (static_cast<float>(completed_count) / (completed_count + interrupted_count)) * 100.0f : 100.0f;
 
     // Draw two panels side-by-side
     ImVec2 bottom_avail = ImGui::GetContentRegionAvail();
     float col_w_panel = (bottom_avail.x - 12.0f) * 0.5f;
 
     ImGui::BeginGroup();
     
     // LEFT: Weekly Focus Heatmap (4 Weeks)
     ImGui::BeginChild("HeatmapPanel", ImVec2(col_w_panel, 130), true);
     ImGui::TextDisabled("FOCUS HEATMAP (LAST 4 WEEKS)");
     ImGui::Spacing();
     
     ImDrawList* h_draw = ImGui::GetWindowDrawList();
     ImVec2 h_start = ImGui::GetCursorScreenPos();
     float square_sz = 11.0f;
     float gap = 3.0f;
     
     // Draw row labels
     const char* day_labels[] = { "M", "W", "F" };
     for (int d = 0; d < 3; ++d) {
         ImGui::SetCursorScreenPos(ImVec2(h_start.x, h_start.y + (d * 2 + 1) * (square_sz + gap) + 1.0f));
         ImGui::TextDisabled("%s", day_labels[d]);
     }
     
     float grid_x = h_start.x + 22.0f;
     float grid_y = h_start.y;
     
     // 4 weeks, 7 rows (days). Column 3 is this week.
     for (int col = 0; col < 4; ++col) {
         for (int row = 0; row < 7; ++row) {
             std::tm tm_now;
             #ifdef _WIN32
             localtime_s(&tm_now, &now);
             #else
             localtime_r(&now, &tm_now);
             #endif
             int today_wday = tm_now.tm_wday == 0 ? 6 : tm_now.tm_wday - 1; // 0 = Mon, ..., 6 = Sun
             
             int days_ago = (3 - col) * 7 + (today_wday - row);
             if (days_ago < 0) continue; // Future
             
             std::time_t cell_start = start_of_today - days_ago * 24 * 3600;
             std::time_t cell_end = cell_start + 24 * 3600;
             
             int cell_focus_sec = 0;
             for (const auto& s : cached_sessions_) {
                 if (s.state == "completed" && s.started_at >= cell_start && s.started_at < cell_end) {
                     cell_focus_sec += s.duration_sec;
                 }
             }
             
             ImU32 cell_color = IM_COL32(30, 30, 42, 255);
             if (cell_focus_sec > 0) {
                 if (cell_focus_sec < 1800)       cell_color = IM_COL32(13, 70, 100, 255);
                 else if (cell_focus_sec < 7200)  cell_color = IM_COL32(13, 120, 180, 255);
                 else                             cell_color = IM_COL32(13, 180, 240, 255);
             }
             
             ImVec2 sq_min = ImVec2(grid_x + col * (square_sz + gap), grid_y + row * (square_sz + gap));
             ImVec2 sq_max = ImVec2(sq_min.x + square_sz, sq_min.y + square_sz);
             
             h_draw->AddRectFilled(sq_min, sq_max, cell_color, 2.0f);
             
             // Hover check & Tooltip
             ImVec2 m_pos = ImGui::GetMousePos();
             if (m_pos.x >= sq_min.x && m_pos.x <= sq_max.x && m_pos.y >= sq_min.y && m_pos.y <= sq_max.y) {
                 h_draw->AddRect(sq_min, sq_max, IM_COL32(255, 255, 255, 255), 2.0f, 0, 1.0f);
                 ImGui::BeginTooltip();
                 ImGui::Text("%s", to_date_string(cell_start).c_str());
                 ImGui::Text("Focus Time: %s", format_duration(cell_focus_sec).c_str());
                 ImGui::EndTooltip();
             }
         }
     }
     
     // Draw legend
     ImGui::SetCursorScreenPos(ImVec2(h_start.x + 100.0f, h_start.y + 4.0f));
     ImGui::TextDisabled("Less"); ImGui::SameLine();
     ImVec2 leg_pos = ImGui::GetCursorScreenPos();
     leg_pos.y += 2.0f;
     h_draw->AddRectFilled(ImVec2(leg_pos.x, leg_pos.y), ImVec2(leg_pos.x + 8, leg_pos.y + 8), IM_COL32(30, 30, 42, 255), 1.0f);
     h_draw->AddRectFilled(ImVec2(leg_pos.x + 12, leg_pos.y), ImVec2(leg_pos.x + 20, leg_pos.y + 8), IM_COL32(13, 70, 100, 255), 1.0f);
     h_draw->AddRectFilled(ImVec2(leg_pos.x + 24, leg_pos.y), ImVec2(leg_pos.x + 32, leg_pos.y + 8), IM_COL32(13, 120, 180, 255), 1.0f);
     h_draw->AddRectFilled(ImVec2(leg_pos.x + 36, leg_pos.y), ImVec2(leg_pos.x + 44, leg_pos.y + 8), IM_COL32(13, 180, 240, 255), 1.0f);
     ImGui::Dummy(ImVec2(50, 8)); ImGui::SameLine();
     ImGui::TextDisabled("More");
     
     ImGui::EndChild();
     
     ImGui::SameLine();
     
     // RIGHT: Streaks & Productivity Insights
     ImGui::BeginChild("InsightsPanel", ImVec2(col_w_panel, 130), true);
     ImGui::TextDisabled("STREAKS & PRODUCTIVITY INSIGHTS");
     ImGui::Spacing();
     
     ImGui::BeginGroup();
     ImGui::Text("Current Streak:"); ImGui::SameLine();
     ImGui::TextColored(ImVec4(0.10f, 0.72f, 0.50f, 1.00f), "%d days", current_streak);
     ImGui::Text("Longest Streak:"); ImGui::SameLine();
     ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.00f), "%d days", longest_streak);
     ImGui::Text("Total Focus Days:"); ImGui::SameLine();
     ImGui::TextColored(ImVec4(0.80f, 0.40f, 0.90f, 1.00f), "%d days", total_active_days);
     ImGui::EndGroup();
     
     ImGui::SameLine(col_w_panel * 0.45f);
     ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
     ImGui::SameLine(col_w_panel * 0.48f);
     
     ImGui::BeginGroup();
     ImGui::PushTextWrapPos(col_w_panel * 0.95f);
     ImGui::TextDisabled("INSIGHT:");
     if (completion_rate < 80.0f) {
         ImGui::Text("Multiple interruptions detected. Try 'Always on Top' floating clock to minimize tab switching!");
     } else if (completed_count == 0) {
         ImGui::Text("Kickoff your focus session! Launch the floating stopwatch to track open-ended work.");
     } else {
         ImGui::Text("Excellent focus rate (%.0f%%) today! Take deep breath breaks in between.", completion_rate);
     }
     ImGui::PopTextWrapPos();
     ImGui::EndGroup();
     
     ImGui::EndChild();
     ImGui::EndGroup();
 }

// ─── Time Tracking View ──────────────────────────────────────────────────────
void GuiManager::render_time_tracking() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));

    ImFont* header_font = font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0];
    ImGui::PushFont(header_font);
    ImGui::Text("Time tracking");
    ImGui::PopFont();
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
        bool clicked = ImGui::Selectable(label_id.c_str(), selected, 0, ImVec2(avail_card.x, 44.0f));
        ImGui::PopStyleColor();

        // Draw left status accent border (Green = Completed, Red = Interrupted)
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImU32 status_color = (session.state == "completed") ? IM_COL32(46, 125, 50, 255) : IM_COL32(198, 40, 40, 255);
        draw_list->AddRectFilled(p_min, ImVec2(p_min.x + 4.0f, p_min.y + 44.0f), status_color);

        // Draw text on card overlay (task name and time range)
        draw_list->AddText(ImVec2(p_min.x + 12.0f, p_min.y + 6.0f), IM_COL32(240, 240, 240, 255), session.task_name.c_str());
        
        std::string time_range = format_time_range(session.started_at, session.ended_at);
        draw_list->AddText(ImVec2(p_min.x + 12.0f, p_min.y + 24.0f), IM_COL32(160, 160, 160, 255), time_range.c_str());
        
        // Tags Pill
        if (!session.tags.empty()) {
            ImVec2 text_sz = ImGui::CalcTextSize(session.task_name.c_str());
            ImVec2 tag_text_sz = ImGui::CalcTextSize(session.tags.c_str());
            ImVec2 pill_pos = ImVec2(p_min.x + 20.0f + text_sz.x, p_min.y + 5.0f);
            float pill_width = tag_text_sz.x + 12.0f;
            draw_list->AddRectFilled(pill_pos, ImVec2(pill_pos.x + pill_width, pill_pos.y + 16.0f), IM_COL32(0, 120, 212, 100), 3.0f);
            draw_list->AddText(ImVec2(pill_pos.x + 6.0f, pill_pos.y + 1.0f), IM_COL32(180, 210, 255, 255), session.tags.c_str());
        }

        // Duration (centered vertically)
        std::string dur = format_duration(session.duration_sec);
        ImVec2 dur_sz = ImGui::CalcTextSize(dur.c_str());
        draw_list->AddText(ImVec2(p_min.x + avail_card.x - dur_sz.x - 12.0f, p_min.y + 14.0f), IM_COL32(200, 200, 200, 255), dur.c_str());

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
            edit_year_ = tm_val.tm_year + 1900;
            edit_month_ = tm_val.tm_mon + 1;
            edit_day_ = tm_val.tm_mday;
            
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

    ImFont* header_font = font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0];
    ImGui::PushFont(header_font);
    ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "Edit Session");
    ImGui::PopFont();
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

    // Date Inputs
    ImGui::Text("Date (YYYY-MM-DD):");
    ImGui::PushItemWidth(55);
    if (ImGui::InputInt("##Year", &edit_year_, 0, 0)) {
        edit_year_ = std::clamp(edit_year_, 2020, 2100);
    }
    ImGui::SameLine();
    ImGui::PushItemWidth(35);
    if (ImGui::InputInt("##Month", &edit_month_, 0, 0)) {
        edit_month_ = std::clamp(edit_month_, 1, 12);
    }
    ImGui::SameLine();
    if (ImGui::InputInt("##Day", &edit_day_, 0, 0)) {
        edit_day_ = std::clamp(edit_day_, 1, 31);
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();

    // Time Pickers
    ImGui::Text("Start Time (HH:MM):");
    ImGui::PushItemWidth(35);
    ImGui::InputInt("h##start", &start_h_, 0, 0); ImGui::SameLine();
    ImGui::Text(":"); ImGui::SameLine();
    ImGui::InputInt("m##start", &start_m_, 0, 0);
    ImGui::PopItemWidth();
    start_h_ = std::clamp(start_h_, 0, 23);
    start_m_ = std::clamp(start_m_, 0, 59);

    // Resolve start datetime string
    std::tm tm_start = {};
    tm_start.tm_year = edit_year_ - 1900;
    tm_start.tm_mon = edit_month_ - 1;
    tm_start.tm_mday = edit_day_;
    tm_start.tm_hour = start_h_;
    tm_start.tm_min = start_m_;
    tm_start.tm_sec = 0;
    tm_start.tm_isdst = -1;
    std::time_t start_time_t = std::mktime(&tm_start);
    ImGui::TextDisabled("Start: %s", to_datetime_string(start_time_t).c_str());
    ImGui::Spacing();

    ImGui::Text("End Time (HH:MM):");
    ImGui::PushItemWidth(35);
    ImGui::InputInt("h##end", &end_h_, 0, 0); ImGui::SameLine();
    ImGui::Text(":"); ImGui::SameLine();
    ImGui::InputInt("m##end", &end_m_, 0, 0);
    ImGui::PopItemWidth();
    end_h_ = std::clamp(end_h_, 0, 23);
    end_m_ = std::clamp(end_m_, 0, 59);

    // Resolve end datetime string (checking overnight wrapping)
    std::tm tm_end = tm_start;
    tm_end.tm_hour = end_h_;
    tm_end.tm_min = end_m_;
    std::time_t end_time_t = std::mktime(&tm_end);
    if (end_time_t < start_time_t) {
        end_time_t += 24 * 3600; // wrapped to next day
    }
    ImGui::TextDisabled("End:   %s", to_datetime_string(end_time_t).c_str());
    ImGui::Spacing();

    // Dynamic duration preview
    int preview_sec = static_cast<int>(end_time_t - start_time_t);
    std::string preview_text = "Total Time: " + format_duration(preview_sec);
    ImGui::TextColored(ImVec4(0.00f, 0.75f, 1.00f, 1.00f), "%s", preview_text.c_str());
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
    std::tm tm_start = {};
    tm_start.tm_year = edit_year_ - 1900;
    tm_start.tm_mon  = edit_month_ - 1;
    tm_start.tm_mday = edit_day_;
    tm_start.tm_hour = start_h_;
    tm_start.tm_min  = start_m_;
    tm_start.tm_sec  = 0;
    tm_start.tm_isdst = -1;
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
    ImFont* header_font = font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0];
    
    // Top Row: Mode Selection and Mini View button
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Active Mode:"); ImGui::SameLine();
    ImGui::RadioButton("Pomodoro Timer", &timer_mode_, 0); ImGui::SameLine();
    ImGui::RadioButton("Task Stopwatch", &timer_mode_, 1);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.0f);
    if (ImGui::Button("Mini View", ImVec2(90, 24))) {
        mini_mode_ = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Switch to compact floating window");
    }

    ImGui::Separator();
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float col_width = avail.x * 0.5f;

    // --- Autocomplete Suggestions Helper (used by both modes) ---
    std::vector<std::string> suggestions;
    for (const auto& s : cached_sessions_) {
        if (suggestions.size() >= 5) break;
        if (s.task_name.empty() || s.task_name == "Manual Entry" || s.task_name == "Focus Session" || s.task_name == "Stopwatch Session") continue;
        if (std::find(suggestions.begin(), suggestions.end(), s.task_name) == suggestions.end()) {
            suggestions.push_back(s.task_name);
        }
    }

    const char* quick_tags[] = { "Work", "Study", "Coding", "Meeting", "Personal" };

    if (timer_mode_ == 0) {
        // ─── POMODORO TIMER MODE ───
        TimerState state = engine_->state();

        ImGui::BeginGroup(); // Left column (Controls)
        ImGui::Text("Task Name:");
        ImGui::SetNextItemWidth(col_width - 40.0f);
        ImGui::InputTextWithHint("##TaskInput", "What are you working on?", task_name_input_, sizeof(task_name_input_));
        ImGui::Spacing();
        
        if (!suggestions.empty()) {
            ImGui::TextDisabled("Recent tasks:");
            ImGui::Indent(4.0f);
            for (size_t i = 0; i < suggestions.size(); ++i) {
                if (i > 0) ImGui::SameLine();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
                if (ImGui::Button((suggestions[i] + "##pomo").c_str())) {
                    strncpy_s(task_name_input_, suggestions[i].c_str(), _TRUNCATE);
                }
                ImGui::PopStyleVar();
            }
            ImGui::Unindent(4.0f);
            ImGui::Spacing();
        }

        ImGui::Text("Tags:");
        ImGui::SetNextItemWidth(col_width - 40.0f);
        ImGui::InputTextWithHint("##TagsInput", "Category tags (comma-separated)...", tags_input_, sizeof(tags_input_));
        ImGui::Spacing();

        ImGui::TextDisabled("Quick tags:");
        ImGui::Indent(4.0f);
        for (size_t i = 0; i < 5; ++i) {
            if (i > 0) ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
            if (ImGui::Button((std::string(quick_tags[i]) + "##pomo_tag").c_str())) {
                std::string current_tags = tags_input_;
                if (current_tags.empty()) {
                    current_tags = quick_tags[i];
                } else {
                    size_t pos = current_tags.find(quick_tags[i]);
                    if (pos == std::string::npos) {
                        if (current_tags.back() == ',' || current_tags.back() == ' ') {
                            if (current_tags.back() == ',') current_tags += " ";
                            current_tags += quick_tags[i];
                        } else {
                            current_tags += ", ";
                            current_tags += quick_tags[i];
                        }
                    }
                }
                strncpy_s(tags_input_, current_tags.c_str(), _TRUNCATE);
            }
            ImGui::PopStyleVar();
        }
        ImGui::Unindent(4.0f);
        ImGui::Spacing();

        ImGui::Text("Details / Notes:");
        ImGui::SetNextItemWidth(col_width - 40.0f);
        ImGui::InputTextWithHint("##DetailsInput", "Rich session notes...", details_input_, sizeof(details_input_));
        ImGui::Spacing();
        ImGui::Spacing();

        if (state == TimerState::Idle) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.47f, 0.83f, 1.00f));
            if (ImGui::Button("Start Focus", ImVec2(col_width - 40.0f, 40.0f))) {
                engine_->start(task_name_input_);
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
                tags_input_[0] = '\0';
                details_input_[0] = '\0';
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        // Right Column (Circular progress widget & settings below)
        ImGui::BeginGroup();
        ImGui::BeginChild("CircleWidget", ImVec2(0, 240), false);
        ImVec2 center = ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 110.0f);
        float radius = 80.0f;

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

        int m = engine_->remaining_sec() / 60;
        int s = engine_->remaining_sec() % 60;
        char buf[32];
        sprintf_s(buf, "%02d:%02d", m, s);
        std::string text = buf;

        ImU32 ring_col = IM_COL32(180, 180, 180, 255); // grey
        if (state == TimerState::Focusing)     ring_col = IM_COL32(46, 125, 50, 255);  // green
        else if (state == TimerState::ShortBreak) ring_col = IM_COL32(21, 101, 192, 255); // blue
        else if (state == TimerState::LongBreak)  ring_col = IM_COL32(106, 27, 154, 255); // purple
        else if (state == TimerState::Paused)     ring_col = IM_COL32(239, 108, 0, 255);   // orange

        float pulse_thickness = 5.0f;
        ImU32 arc_col = ring_col;
        if (state == TimerState::Focusing) {
            float t = static_cast<float>(ImGui::GetTime());
            float pulse = sinf(t * 2.5f);
            float pulse_norm = (pulse + 1.0f) * 0.5f;
            pulse_thickness = 4.5f + pulse_norm * 2.0f;
            int alpha = static_cast<int>(180.0f + pulse_norm * 75.0f);
            arc_col = IM_COL32(46, 125, 50, alpha);
        } else if (state == TimerState::Paused) {
            float t = static_cast<float>(ImGui::GetTime());
            float pulse = sinf(t * 4.0f);
            float pulse_norm = (pulse + 1.0f) * 0.5f;
            int alpha = static_cast<int>(100.0f + pulse_norm * 155.0f);
            arc_col = IM_COL32(239, 108, 0, alpha);
        } else if (state == TimerState::ShortBreak || state == TimerState::LongBreak) {
            float t = static_cast<float>(ImGui::GetTime());
            float pulse = sinf(t * 1.5f);
            float pulse_norm = (pulse + 1.0f) * 0.5f;
            pulse_thickness = 4.8f + pulse_norm * 1.0f;
            int alpha = static_cast<int>(200.0f + pulse_norm * 55.0f);
            arc_col = (state == TimerState::ShortBreak) ? IM_COL32(21, 101, 192, alpha) : IM_COL32(106, 27, 154, alpha);
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 screen_center = ImVec2(ImGui::GetCursorScreenPos().x + center.x, ImGui::GetCursorScreenPos().y + center.y);
        draw_list->AddCircle(screen_center, radius, IM_COL32(43, 43, 43, 255), 64, 5.0f);

        if (fraction > 0.001f) {
            const float IM_PI = 3.1415926535f;
            float start_angle = -IM_PI / 2.0f;
            float end_angle = start_angle + fraction * 2.0f * IM_PI;
            draw_list->PathClear();
            draw_list->PathArcTo(screen_center, radius, start_angle, end_angle, 64);
            draw_list->PathStroke(arc_col, 0, pulse_thickness);
        }

        ImFont* count_font = font_large_ ? font_large_ : ImGui::GetIO().Fonts->Fonts[0];
        ImGui::PushFont(count_font);
        ImVec2 text_sz = ImGui::CalcTextSize(text.c_str());
        draw_list->AddText(count_font, count_font->FontSize, ImVec2(screen_center.x - text_sz.x * 0.5f, screen_center.y - text_sz.y * 0.5f - 10.0f), IM_COL32(245, 245, 245, 255), text.c_str());
        ImGui::PopFont();

        std::string state_text = to_string(state);
        ImVec2 st_sz = ImGui::CalcTextSize(state_text.c_str());
        draw_list->AddText(ImVec2(screen_center.x - st_sz.x * 0.5f, screen_center.y + 25.0f), IM_COL32(180, 180, 180, 255), state_text.c_str());
        ImGui::EndChild();

        // Pomodoro durations configuration below the clock (requested)
        if (ImGui::CollapsingHeader("Timer Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (state == TimerState::Idle) {
                ImGui::SetNextItemWidth(180);
                if (ImGui::SliderInt("Focus duration##cfg", &focus_min_, 1, 60, "%d min")) {
                    config_.focus_duration_sec = focus_min_ * 60;
                    ConfigManager::save(config_, config_path_);
                    TimerConfig engine_cfg = engine_->config();
                    engine_cfg.focus_duration_sec = config_.focus_duration_sec;
                    engine_->set_config(engine_cfg);
                }
                ImGui::SetNextItemWidth(180);
                if (ImGui::SliderInt("Short break##cfg", &short_break_min_, 1, 30, "%d min")) {
                    config_.short_break_sec = short_break_min_ * 60;
                    ConfigManager::save(config_, config_path_);
                    TimerConfig engine_cfg = engine_->config();
                    engine_cfg.short_break_sec = config_.short_break_sec;
                    engine_->set_config(engine_cfg);
                }
                ImGui::SetNextItemWidth(180);
                if (ImGui::SliderInt("Long break##cfg", &long_break_min_, 1, 60, "%d min")) {
                    config_.long_break_sec = long_break_min_ * 60;
                    ConfigManager::save(config_, config_path_);
                    TimerConfig engine_cfg = engine_->config();
                    engine_cfg.long_break_sec = config_.long_break_sec;
                    engine_->set_config(engine_cfg);
                }
                ImGui::SetNextItemWidth(180);
                if (ImGui::InputInt("Sessions before long##cfg", &sessions_before_long_)) {
                    sessions_before_long_ = std::clamp(sessions_before_long_, 1, 20);
                    config_.sessions_before_long_break = sessions_before_long_;
                    ConfigManager::save(config_, config_path_);
                    TimerConfig engine_cfg = engine_->config();
                    engine_cfg.sessions_before_long_break = config_.sessions_before_long_break;
                    engine_->set_config(engine_cfg);
                }
            } else {
                ImGui::TextDisabled("Timer settings locked while running.");
            }
        }

        // Sub-tasks checklist
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.00f), "Focus Sub-Tasks Checklist");
        ImGui::Spacing();
        
        ImGui::SetNextItemWidth(140);
        ImGui::InputTextWithHint("##new_todo", "New sub-task...", new_todo_input_, sizeof(new_todo_input_));
        ImGui::SameLine();
        if (ImGui::Button("+##add_todo")) {
            if (new_todo_input_[0] != '\0') {
                focus_todos_.push_back(new_todo_input_);
                focus_todos_done_.push_back(false);
                new_todo_input_[0] = '\0';
            }
        }
        
        ImGui::BeginChild("TodoList", ImVec2(0, 95), true);
        for (size_t i = 0; i < focus_todos_.size(); ++i) {
            bool done = focus_todos_done_[i];
            std::string check_lbl = focus_todos_[i] + "##check_" + std::to_string(i);
            if (ImGui::Checkbox(check_lbl.c_str(), &done)) {
                focus_todos_done_[i] = done;
            }
            if (ImGui::BeginPopupContextItem(("todo_ctx_" + std::to_string(i)).c_str())) {
                if (ImGui::MenuItem("Remove task")) {
                    focus_todos_.erase(focus_todos_.begin() + i);
                    focus_todos_done_.erase(focus_todos_done_.begin() + i);
                    i--;
                }
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();

        ImGui::EndGroup();

    } else {
        // ─── STOPWATCH COUNT-UP MODE ───
        ImGui::BeginGroup(); // Left Column (Controls)
        ImGui::Text("Task Name:");
        ImGui::SetNextItemWidth(col_width - 40.0f);
        ImGui::InputTextWithHint("##StopwatchTask", "What task are you tracking?", stopwatch_task_name_, sizeof(stopwatch_task_name_));
        ImGui::Spacing();

        if (!suggestions.empty()) {
            ImGui::TextDisabled("Recent tasks:");
            ImGui::Indent(4.0f);
            for (size_t i = 0; i < suggestions.size(); ++i) {
                if (i > 0) ImGui::SameLine();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
                if (ImGui::Button((suggestions[i] + "##sw").c_str())) {
                    strncpy_s(stopwatch_task_name_, suggestions[i].c_str(), _TRUNCATE);
                }
                ImGui::PopStyleVar();
            }
            ImGui::Unindent(4.0f);
            ImGui::Spacing();
        }

        ImGui::Text("Tags:");
        ImGui::SetNextItemWidth(col_width - 40.0f);
        ImGui::InputTextWithHint("##StopwatchTags", "Category tags (comma-separated)...", stopwatch_tags_, sizeof(stopwatch_tags_));
        ImGui::Spacing();

        ImGui::TextDisabled("Quick tags:");
        ImGui::Indent(4.0f);
        for (size_t i = 0; i < 5; ++i) {
            if (i > 0) ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
            if (ImGui::Button((std::string(quick_tags[i]) + "##sw_tag").c_str())) {
                std::string current_tags = stopwatch_tags_;
                if (current_tags.empty()) {
                    current_tags = quick_tags[i];
                } else {
                    size_t pos = current_tags.find(quick_tags[i]);
                    if (pos == std::string::npos) {
                        if (current_tags.back() == ',' || current_tags.back() == ' ') {
                            if (current_tags.back() == ',') current_tags += " ";
                            current_tags += quick_tags[i];
                        } else {
                            current_tags += ", ";
                            current_tags += quick_tags[i];
                        }
                    }
                }
                strncpy_s(stopwatch_tags_, current_tags.c_str(), _TRUNCATE);
            }
            ImGui::PopStyleVar();
        }
        ImGui::Unindent(4.0f);
        ImGui::Spacing();

        ImGui::Text("Details / Notes:");
        ImGui::SetNextItemWidth(col_width - 40.0f);
        ImGui::InputTextWithHint("##StopwatchDetails", "Rich session notes...", stopwatch_details_, sizeof(stopwatch_details_));
        ImGui::Spacing();
        ImGui::Spacing();

        if (!stopwatch_active_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.62f, 0.45f, 1.00f));
            if (ImGui::Button("Start Stopwatch", ImVec2(col_width - 40.0f, 32.0f))) {
                stopwatch_active_ = true;
                stopwatch_paused_ = false;
                stopwatch_elapsed_sec_ = 0;
                stopwatch_last_time_ = std::time(nullptr);
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();
            if (ImGui::Button("Launch Floating Stopwatch", ImVec2(col_width - 40.0f, 32.0f))) {
                stopwatch_active_ = true;
                stopwatch_paused_ = false;
                stopwatch_elapsed_sec_ = 0;
                stopwatch_last_time_ = std::time(nullptr);
                mini_mode_ = true;
            }
        } else {
            if (stopwatch_paused_) {
                if (ImGui::Button("Resume Stopwatch", ImVec2(col_width - 40.0f, 32.0f))) {
                    stopwatch_paused_ = false;
                    stopwatch_last_time_ = std::time(nullptr);
                }
            } else {
                if (ImGui::Button("Pause Stopwatch", ImVec2(col_width - 40.0f, 32.0f))) {
                    stopwatch_paused_ = true;
                }
            }
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Stop & Log Task Session", ImVec2(col_width - 40.0f, 32.0f))) {
                // Log session
                TaskEntry entry;
                entry.task_name = stopwatch_task_name_[0] == '\0' ? "Stopwatch Session" : stopwatch_task_name_;
                entry.tags = stopwatch_tags_;
                entry.details = stopwatch_details_;
                entry.started_at = std::time(nullptr) - stopwatch_elapsed_sec_;
                entry.ended_at = std::time(nullptr);
                entry.duration_sec = stopwatch_elapsed_sec_;
                entry.state = "completed";
                
                db_->log_session(entry);
                
                // Reload
                auto list = db_->get_recent_sessions(100);
                if (list.is_ok()) cached_sessions_ = list.value();
                
                stopwatch_active_ = false;
                stopwatch_paused_ = false;
                stopwatch_elapsed_sec_ = 0;
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        // Right Column (Spinning count-up stopwatch clock)
        ImGui::BeginGroup();
        ImGui::BeginChild("StopwatchCircleWidget", ImVec2(0, 240), false);
        ImVec2 center = ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 110.0f);
        float radius = 80.0f;
        
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 screen_center = ImVec2(ImGui::GetCursorScreenPos().x + center.x, ImGui::GetCursorScreenPos().y + center.y);
        
        // Draw track circle
        draw_list->AddCircle(screen_center, radius, IM_COL32(43, 43, 43, 255), 64, 5.0f);
        
        // Draw spinning indicator arc
        if (stopwatch_active_ && !stopwatch_paused_) {
            float t = static_cast<float>(ImGui::GetTime());
            float start_angle = t * 3.0f; // spinning speed
            float end_angle = start_angle + 1.2f; // arc length
            draw_list->PathClear();
            draw_list->PathArcTo(screen_center, radius, start_angle, end_angle, 32);
            draw_list->PathStroke(IM_COL32(13, 148, 136, 255), 0, 5.0f); // Teal spinning arc
        } else if (stopwatch_active_ && stopwatch_paused_) {
            // Draw solid orange ring
            draw_list->AddCircle(screen_center, radius, IM_COL32(239, 108, 0, 255), 64, 5.0f);
        }
        
        int h = stopwatch_elapsed_sec_ / 3600;
        int m = (stopwatch_elapsed_sec_ % 3600) / 60;
        int s = stopwatch_elapsed_sec_ % 60;
        char time_str[64];
        sprintf_s(time_str, "%02d:%02d:%02d", h, m, s);
        
        ImFont* count_font = font_large_ ? font_large_ : ImGui::GetIO().Fonts->Fonts[0];
        ImGui::PushFont(count_font);
        ImVec2 text_sz = ImGui::CalcTextSize(time_str);
        draw_list->AddText(count_font, count_font->FontSize - 8.0f, ImVec2(screen_center.x - text_sz.x * 0.5f, screen_center.y - text_sz.y * 0.5f - 10.0f), IM_COL32(245, 245, 245, 255), time_str);
        ImGui::PopFont();
        
        std::string st_text = stopwatch_active_ ? (stopwatch_paused_ ? "PAUSED" : "STOPWATCH ACTIVE") : "STOPWATCH READY";
        ImVec2 st_sz = ImGui::CalcTextSize(st_text.c_str());
        draw_list->AddText(ImVec2(screen_center.x - st_sz.x * 0.5f, screen_center.y + 25.0f), stopwatch_paused_ ? IM_COL32(239, 108, 0, 255) : IM_COL32(13, 148, 136, 255), st_text.c_str());
        ImGui::EndChild();

        // Stopwatch Tip card to fill right column bottom
        if (ImGui::CollapsingHeader("Stopwatch Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::TextWrapped("The count-up stopwatch is ideal for open-ended tasks where you want to track total billable hours without set break limits.");
        }
        ImGui::EndGroup();
    }
}

// ─── Analytics View (Stacked Bar Chart & Donut Chart Drawing) ────────────────
void GuiManager::render_analytics() {
    ImFont* header_font = font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0];
    ImGui::PushFont(header_font);
    ImGui::Text("Analytics");
    ImGui::PopFont();
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
    ImVec2 chart_sz = ImVec2(ImGui::GetContentRegionAvail().x - 20.0f, 180.0f);
    
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
    
    // --- Category Breakdown (Donut Chart) ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Time by Category Tag (Last 7 Days):");
    ImGui::Spacing();

    std::map<std::string, int> tag_secs;
    int total_tagged_secs = 0;
    
    if (sessions.is_ok()) {
        for (const auto& s : sessions.value()) {
            if (s.state == "completed") {
                std::string tag_str = s.tags;
                if (tag_str.empty()) {
                    tag_secs["Untagged"] += s.duration_sec;
                    total_tagged_secs += s.duration_sec;
                } else {
                    // split comma-separated tags
                    std::stringstream ss(tag_str);
                    std::string token;
                    while (std::getline(ss, token, ',')) {
                        // Trim token
                        auto trim_start = std::find_if_not(token.begin(), token.end(), ::isspace);
                        auto trim_end = std::find_if_not(token.rbegin(), token.rend(), ::isspace).base();
                        std::string clean_tag = trim_start < trim_end ? std::string(trim_start, trim_end) : "";
                        if (!clean_tag.empty()) {
                            tag_secs[clean_tag] += s.duration_sec;
                            total_tagged_secs += s.duration_sec;
                        }
                    }
                }
            }
        }
    }

    if (total_tagged_secs > 0) {
        // Left part: donut chart, Right part: legends
        ImGui::BeginGroup();
        
        ImVec2 donut_pos = ImGui::GetCursorScreenPos();
        ImVec2 donut_size = ImVec2(150, 150);
        ImDrawList* d_list = ImGui::GetWindowDrawList();
        
        // Draw spacing dummy
        ImGui::Dummy(donut_size);
        
        ImVec2 donut_center = ImVec2(donut_pos.x + donut_size.x * 0.5f, donut_pos.y + donut_size.y * 0.5f);
        float mid_radius = 50.0f;
        float thickness = 18.0f;
        
        ImU32 colors_list[] = {
            IM_COL32(59, 130, 246, 255),  // blue
            IM_COL32(16, 185, 129, 255),  // green
            IM_COL32(139, 92, 246, 255),  // purple
            IM_COL32(245, 158, 11, 255),  // amber
            IM_COL32(236, 72, 153, 255),  // pink
            IM_COL32(107, 114, 128, 255)   // grey
        };
        
        float cur_angle = -3.14159265f / 2.0f; // start at 12 o'clock
        int color_idx = 0;
        
        struct TagItem {
            std::string name;
            int seconds;
            ImU32 color;
        };
        std::vector<TagItem> items;
        for (const auto& pair : tag_secs) {
            TagItem item;
            item.name = pair.first;
            item.seconds = pair.second;
            item.color = colors_list[color_idx % 6];
            color_idx++;
            items.push_back(item);
        }
        
        // Sort items by seconds descending
        std::sort(items.begin(), items.end(), [](const TagItem& a, const TagItem& b) {
            return a.seconds > b.seconds;
        });

        // Draw donut sectors
        for (const auto& item : items) {
            float sweep = (static_cast<float>(item.seconds) / static_cast<float>(total_tagged_secs)) * 2.0f * 3.14159265f;
            if (sweep > 0.001f) {
                d_list->PathClear();
                d_list->PathArcTo(donut_center, mid_radius, cur_angle, cur_angle + sweep, 64);
                d_list->PathStroke(item.color, 0, thickness);
                cur_angle += sweep;
            }
        }
        
        // Draw inner and outer border rings
        d_list->AddCircle(donut_center, mid_radius - thickness * 0.5f, IM_COL32(40, 40, 40, 255), 64, 1.0f);
        d_list->AddCircle(donut_center, mid_radius + thickness * 0.5f, IM_COL32(40, 40, 40, 255), 64, 1.0f);
        
        // Draw total hours inside
        float total_hours = static_cast<float>(total_tagged_secs) / 3600.0f;
        char hrs_buf[32];
        sprintf_s(hrs_buf, "%.1f h", total_hours);
        ImVec2 text_sz = ImGui::CalcTextSize(hrs_buf);
        d_list->AddText(ImVec2(donut_center.x - text_sz.x * 0.5f, donut_center.y - text_sz.y * 0.5f), IM_COL32(240, 240, 240, 255), hrs_buf);
        
        ImGui::EndGroup();
        
        ImGui::SameLine(220.0f);
        
        // Right part: Legend list
        ImGui::BeginGroup();
        ImGui::Text("Categories:");
        ImGui::Spacing();
        for (const auto& item : items) {
            ImVec2 legend_pos = ImGui::GetCursorScreenPos();
            d_list->AddRectFilled(ImVec2(legend_pos.x, legend_pos.y + 4.0f), ImVec2(legend_pos.x + 12.0f, legend_pos.y + 16.0f), item.color, 2.0f);
            
            ImGui::Indent(20.0f);
            float tag_pct = (static_cast<float>(item.seconds) / static_cast<float>(total_tagged_secs)) * 100.0f;
            ImGui::Text("%s: %s (%.1f%%)", item.name.c_str(), format_duration(item.seconds).c_str(), tag_pct);
            ImGui::Unindent(20.0f);
            ImGui::Spacing();
        }
        ImGui::EndGroup();
    } else {
        ImGui::TextDisabled("No categorised session data found for this period.");
    }

    // CSV Export Section
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Data Export");
    ImGui::Spacing();

    static std::string export_status_msg = "";
    static float export_status_timer = 0.0f;
    static ImVec4 export_status_color = ImVec4(0.10f, 0.72f, 0.50f, 1.00f); // green

    if (ImGui::Button("Export Time Logs to CSV", ImVec2(200, 32))) {
        // Query database
        auto all_sessions_res = db_->get_recent_sessions(10000);
        if (all_sessions_res.is_err()) {
            export_status_msg = "Error querying logs: " + all_sessions_res.error();
            export_status_color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // red
            export_status_timer = 5.0f;
        } else {
            // Find user profile downloads folder
            std::string path_str = "";
            char* userprofile = std::getenv("USERPROFILE");
            if (userprofile != nullptr) {
                path_str = std::string(userprofile) + "\\Downloads\\tempus_export.csv";
            } else {
                path_str = "tempus_export.csv";
            }

            std::ofstream csv(path_str);
            if (!csv.is_open()) {
                export_status_msg = "Failed to create file: " + path_str;
                export_status_color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // red
                export_status_timer = 5.0f;
            } else {
                // Escape function inline helper
                auto escape_csv_str = [](std::string s) -> std::string {
                    size_t pos = 0;
                    while ((pos = s.find('"', pos)) != std::string::npos) {
                        s.replace(pos, 1, "\"\"");
                        pos += 2;
                    }
                    return s;
                };

                // Datetime format helper
                auto to_datetime_string = [](std::time_t t) -> std::string {
                    std::tm tm_val;
#ifdef _WIN32
                    localtime_s(&tm_val, &t);
#else
                    localtime_r(&t, &tm_val);
#endif
                    char buf[64];
                    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
                    return std::string(buf);
                };

                // Write header
                csv << "ID,Task Name,Started At,Ended At,Duration (Seconds),Duration (Formatted),State,Tags,Details\n";
                
                // Write entries
                for (const auto& s : all_sessions_res.value()) {
                    csv << s.id << ","
                        << "\"" << escape_csv_str(s.task_name) << "\","
                        << "\"" << to_datetime_string(s.started_at) << "\","
                        << "\"" << to_datetime_string(s.ended_at) << "\","
                        << s.duration_sec << ","
                        << "\"" << format_duration(s.duration_sec) << "\","
                        << "\"" << escape_csv_str(s.state) << "\","
                        << "\"" << escape_csv_str(s.tags) << "\","
                        << "\"" << escape_csv_str(s.details) << "\"\n";
                }
                csv.close();

                export_status_msg = "Logs exported to " + path_str;
                export_status_color = ImVec4(0.10f, 0.72f, 0.50f, 1.00f); // green
                export_status_timer = 5.0f;
            }
        }
    }

    if (export_status_timer > 0.0f) {
        export_status_timer -= ImGui::GetIO().DeltaTime;
        ImGui::SameLine();
        ImGui::TextColored(export_status_color, "%s", export_status_msg.c_str());
    }
}

// ─── Settings View ───────────────────────────────────────────────────────────
void GuiManager::render_settings(void* hwnd) {
    ImFont* header_font = font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0];
    ImGui::PushFont(header_font);
    ImGui::Text("Settings");
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::Spacing();

    // 1. Color Theme Selection
    ImGui::Text("Interface Theme Preset:");
    const char* themes[] = { "Midnight Blue (Default)", "Obsidian Dark", "Forest Mint", "Slate Classic" };
    int current_theme = config_.theme_index;
    ImGui::SetNextItemWidth(300);
    if (ImGui::Combo("Theme##select", &current_theme, themes, IM_ARRAYSIZE(themes))) {
        config_.theme_index = current_theme;
        apply_theme(current_theme);
        ConfigManager::save(config_, config_path_);
    }
    ImGui::Spacing();

    // 2. Behavior Options
    ImGui::Text("Application Behaviour:");
    bool sounds = config_.sound_enabled;
    if (ImGui::Checkbox("Enable Sound Alerts", &sounds)) {
        config_.sound_enabled = sounds;
        ConfigManager::save(config_, config_path_);
    }
    ImGui::SameLine(220.0f);

    bool always_on_top = config_.always_on_top;
    if (ImGui::Checkbox("Always on Top", &always_on_top)) {
        config_.always_on_top = always_on_top;
        ConfigManager::save(config_, config_path_);
        if (hwnd) {
#ifdef _WIN32
            ::SetWindowPos((HWND)hwnd, always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
#endif
        }
    }
    ImGui::Spacing();

    // 3. Weekly Hours Target
    ImGui::Text("Weekly Goal Target:");
    int weekly_hours = config_.weekly_goal_hours;
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderInt("Hours/week", &weekly_hours, 1, 100)) {
        config_.weekly_goal_hours = weekly_hours;
        ConfigManager::save(config_, config_path_);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Diagnostics & System Specs
    ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "System Info & Diagnostics");
    ImGui::Spacing();

    ImGui::BeginChild("DiagnosticsPanel", ImVec2(0, 160), true);
    ImGui::Text("Tempus Version: v1.0.0 Premium Edition");
    ImGui::Text("Config File Path:  %s", config_path_.string().c_str());
    
    // Get database statistics
    auto total_sessions_res = db_->get_recent_sessions(10000);
    int total_sessions = total_sessions_res.is_ok() ? static_cast<int>(total_sessions_res.value().size()) : 0;
    ImGui::Text("Total logged sessions in DB: %d", total_sessions);
    
    // Show system specs
    ImGui::Text("Operating System: Windows (DirectX 11)");
    ImGui::Text("ImGui Version: %s", IMGUI_VERSION);
    ImGui::EndChild();
}

void GuiManager::render_mini_view(void* hwnd) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    
    std::string task_name = "";
    std::string time_str = "";
    bool active = false;
    bool paused = false;
    
    if (timer_mode_ == 0) { // Pomodoro
        TimerState state = engine_->state();
        active = (state != TimerState::Idle);
        paused = (state == TimerState::Paused);
        task_name = engine_->current_task().empty() ? "Focus Session" : engine_->current_task();
        int m = engine_->remaining_sec() / 60;
        int s = engine_->remaining_sec() % 60;
        char buf[32];
        sprintf_s(buf, "%02d:%02d", m, s);
        time_str = buf;
    } else { // Stopwatch
        active = stopwatch_active_;
        paused = stopwatch_paused_;
        task_name = stopwatch_task_name_[0] == '\0' ? "Stopwatch Session" : stopwatch_task_name_;
        int h = stopwatch_elapsed_sec_ / 3600;
        int m = (stopwatch_elapsed_sec_ % 3600) / 60;
        int s = stopwatch_elapsed_sec_ % 60;
        char buf[32];
        sprintf_s(buf, "%02d:%02d:%02d", h, m, s);
        time_str = buf;
    }
    
    if (task_name.size() > 22) {
        task_name = task_name.substr(0, 19) + "...";
    }
    
    ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "%s", task_name.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 22.0f);
    
    if (ImGui::Button("[ ]##Restore", ImVec2(22, 22))) {
        mini_mode_ = false;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Expand to full view");
    }
    
    ImGui::Spacing();
    
    ImGui::PushFont(font_header_ ? font_header_ : ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::Text("%s", time_str.c_str());
    ImGui::PopFont();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    if (!active) {
        ImGui::TextDisabled("Timer is inactive");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
        if (ImGui::Button("Expand", ImVec2(60, 24))) {
            mini_mode_ = false;
        }
    } else {
        if (timer_mode_ == 0) { // Pomodoro active controls
            if (paused) {
                if (ImGui::Button("Resume", ImVec2(70, 26))) {
                    engine_->resume();
                }
            } else {
                if (ImGui::Button("Pause", ImVec2(70, 26))) {
                    engine_->pause();
                }
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Stop", ImVec2(70, 26))) {
                engine_->stop();
                tags_input_[0] = '\0';
                details_input_[0] = '\0';
            }
            ImGui::PopStyleColor();
        } else { // Stopwatch active controls
            if (paused) {
                if (ImGui::Button("Resume", ImVec2(70, 26))) {
                    stopwatch_paused_ = false;
                    stopwatch_last_time_ = std::time(nullptr);
                }
            } else {
                if (ImGui::Button("Pause", ImVec2(70, 26))) {
                    stopwatch_paused_ = true;
                }
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Stop", ImVec2(70, 26))) {
                TaskEntry entry;
                entry.task_name = stopwatch_task_name_[0] == '\0' ? "Stopwatch Session" : stopwatch_task_name_;
                entry.tags = stopwatch_tags_;
                entry.details = stopwatch_details_;
                entry.started_at = std::time(nullptr) - stopwatch_elapsed_sec_;
                entry.ended_at = std::time(nullptr);
                entry.duration_sec = stopwatch_elapsed_sec_;
                entry.state = "completed";
                
                db_->log_session(entry);
                
                auto list = db_->get_recent_sessions(100);
                if (list.is_ok()) cached_sessions_ = list.value();
                
                stopwatch_active_ = false;
                stopwatch_paused_ = false;
                stopwatch_elapsed_sec_ = 0;
            }
            ImGui::PopStyleColor();
        }
    }
    
    ImGui::PopStyleVar();
}

} // namespace tempus
