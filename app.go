package main

import (
	"context"
	"errors"
	"fmt"
	"regexp"
	"time"

	wailsRuntime "github.com/wailsapp/wails/v2/pkg/runtime"
)

// App struct holds the application state.
type App struct {
	ctx          context.Context
	db           *Database
	isMiniMode   bool
	normalWidth  int
	normalHeight int
	miniWidth    int
	miniHeight   int
}

// NewApp creates a new App application struct.
func NewApp() *App {
	return &App{}
}

// startup is called when the app starts. It initializes the database.
func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
	db, err := NewDatabase()
	if err != nil {
		fmt.Println("FATAL: Failed to initialize database:", err)
		return
	}
	a.db = db
}

// restore window
func (a *App) domReady(ctx context.Context) {
	if a.db == nil {
		return
	}
	
	// load normal size
	widthStr, _ := a.db.GetSetting("window_width")
	heightStr, _ := a.db.GetSetting("window_height")
	var w, h int
	if widthStr != "" && heightStr != "" {
		fmt.Sscanf(widthStr, "%d", &w)
		fmt.Sscanf(heightStr, "%d", &h)
		if w >= 900 && h >= 600 {
			a.normalWidth = w
			a.normalHeight = h
			wailsRuntime.WindowSetSize(ctx, w, h)
		}
	}

	miniWidthStr, _ := a.db.GetSetting("window_mini_width")
	miniHeightStr, _ := a.db.GetSetting("window_mini_height")
	var mw, mh int
	if miniWidthStr != "" && miniHeightStr != "" {
		fmt.Sscanf(miniWidthStr, "%d", &mw)
		fmt.Sscanf(miniHeightStr, "%d", &mh)
		if mw >= 100 && mh >= 50 {
			a.miniWidth = mw
			a.miniHeight = mh
		}
	}
}

// shutdown is called at application termination.
func (a *App) shutdown(ctx context.Context) {
	if a.db != nil {
		if !a.isMiniMode {
			w, h := wailsRuntime.WindowGetSize(ctx)
			if w >= 900 && h >= 600 {
				a.db.SaveSetting("window_width", fmt.Sprintf("%d", w))
				a.db.SaveSetting("window_height", fmt.Sprintf("%d", h))
			}
		} else {
			w, h := wailsRuntime.WindowGetSize(ctx)
			if w < 900 && h < 600 {
				a.db.SaveSetting("window_mini_width", fmt.Sprintf("%d", w))
				a.db.SaveSetting("window_mini_height", fmt.Sprintf("%d", h))
			}
		}
		a.db.Close()
	}
}

// ──── Dashboard ──────────────────────────────────────────────────────────────

// DashboardData is the aggregate payload sent to the frontend dashboard.
type DashboardData struct {
	TodaySeconds   int64       `json:"todaySeconds"`
	WeekSeconds    int64       `json:"weekSeconds"`
	TotalSeconds   int64       `json:"totalSeconds"`
	TaskCount      int         `json:"taskCount"`
	RecentEntries  []TimeEntry `json:"recentEntries"`
	WeekDaySummary []DaySummary `json:"weekDaySummary"`
}

// GetDashboardData returns all the data needed by the dashboard in one call.
func (a *App) GetDashboardData() (DashboardData, error) {
	data := DashboardData{}

	todaySec, err := a.db.GetTodayTime()
	if err != nil {
		return data, err
	}
	data.TodaySeconds = todaySec

	weekSec, err := a.db.GetWeekTime()
	if err != nil {
		return data, err
	}
	data.WeekSeconds = weekSec

	totalSec, err := a.db.GetTotalTime()
	if err != nil {
		return data, err
	}
	data.TotalSeconds = totalSec

	tasks, err := a.db.GetTasks()
	if err != nil {
		return data, err
	}
	data.TaskCount = len(tasks)

	recent, err := a.db.GetRecentEntries(10)
	if err != nil {
		return data, err
	}
	data.RecentEntries = recent

	// Get current week's daily summary
	now := time.Now()
	weekday := int(now.Weekday())
	if weekday == 0 {
		weekday = 7
	}
	monday := now.AddDate(0, 0, -(weekday - 1))
	sunday := monday.AddDate(0, 0, 6)

	daySummary, err := a.db.GetDailySummary(monday.Format("2006-01-02"), sunday.Format("2006-01-02")+"T23:59:59")
	if err != nil {
		return data, err
	}
	data.WeekDaySummary = daySummary

	return data, nil
}

// ──── Tasks ──────────────────────────────────────────────────────────────────

// GetTasks returns all tasks.
func (a *App) GetTasks() ([]Task, error) {
	return a.db.GetTasks()
}

// AddTask creates a new task.
func (a *App) AddTask(name, color string) (int64, error) {
	if err := a.validateTask(name, color); err != nil {
		return 0, err
	}
	return a.db.AddTask(name, color)
}

// UpdateTask modifies a task.
func (a *App) UpdateTask(id int64, name, color string) error {
	if id <= 0 {
		return errors.New("invalid task ID")
	}
	if err := a.validateTask(name, color); err != nil {
		return err
	}
	return a.db.UpdateTask(id, name, color)
}

// DeleteTask removes a task.
func (a *App) DeleteTask(id int64) error {
	return a.db.DeleteTask(id)
}

// ──── Time Entries ───────────────────────────────────────────────────────────

// GetTimeEntries returns time entries, optionally filtered.
func (a *App) GetTimeEntries(startDate, endDate string) ([]TimeEntry, error) {
	return a.db.GetTimeEntries(startDate, endDate)
}

// AddTimeEntry creates a new time entry.
func (a *App) AddTimeEntry(taskID int64, startTime, endTime string, duration int64, note string) (int64, error) {
	if err := a.validateTimeEntry(taskID, startTime, endTime, duration, note); err != nil {
		return 0, err
	}
	return a.db.AddTimeEntry(taskID, startTime, endTime, duration, note)
}

// UpdateTimeEntry modifies an existing time entry.
func (a *App) UpdateTimeEntry(id, taskID int64, startTime, endTime string, duration int64, note string) error {
	if id <= 0 {
		return errors.New("invalid entry ID")
	}
	if err := a.validateTimeEntry(taskID, startTime, endTime, duration, note); err != nil {
		return err
	}
	return a.db.UpdateTimeEntry(id, taskID, startTime, endTime, duration, note)
}

// DeleteTimeEntry removes a time entry.
func (a *App) DeleteTimeEntry(id int64) error {
	return a.db.DeleteTimeEntry(id)
}

// ──── Analytics ──────────────────────────────────────────────────────────────

// AnalyticsData is the aggregate payload for analytics.
type AnalyticsData struct {
	DailySummary []DaySummary  `json:"dailySummary"`
	TaskSummary  []TaskSummary `json:"taskSummary"`
	TotalSeconds int64         `json:"totalSeconds"`
}

// GetAnalyticsData returns analytics for a given date range.
func (a *App) GetAnalyticsData(startDate, endDate string) (AnalyticsData, error) {
	data := AnalyticsData{}

	daily, err := a.db.GetDailySummary(startDate, endDate+"T23:59:59")
	if err != nil {
		return data, err
	}
	data.DailySummary = daily

	taskSummary, err := a.db.GetTaskSummary(startDate, endDate+"T23:59:59")
	if err != nil {
		return data, err
	}
	data.TaskSummary = taskSummary

	// Sum total
	for _, d := range daily {
		data.TotalSeconds += d.TotalSeconds
	}
	return data, nil
}

// ──── Pomodoro Settings ──────────────────────────────────────────────────────

// GetPomodoroSettings returns the current pomodoro settings.
func (a *App) GetPomodoroSettings() (PomodoroSettings, error) {
	return a.db.GetPomodoroSettings()
}

// SavePomodoroSettings saves pomodoro settings.
func (a *App) SavePomodoroSettings(settings PomodoroSettings) error {
	return a.db.SavePomodoroSettings(settings)
}

// ──── Stopwatch / Live Timer ─────────────────────────────────────────────────

// SaveStopwatchEntry creates a time entry from a completed stopwatch session.
func (a *App) SaveStopwatchEntry(taskID int64, durationSeconds int64, note string) (int64, error) {
	if taskID <= 0 {
		return 0, errors.New("invalid task ID")
	}
	if durationSeconds < 0 {
		return 0, errors.New("duration cannot be negative")
	}
	if len(note) > 500 {
		return 0, errors.New("note is too long")
	}
	now := time.Now()
	endTime := now.Format("2006-01-02T15:04:05")
	startTime := now.Add(-time.Duration(durationSeconds) * time.Second).Format("2006-01-02T15:04:05")
	return a.db.AddTimeEntry(taskID, startTime, endTime, durationSeconds, note)
}

// ──── Window Controls ────────────────────────────────────────────────────────

// ToggleAlwaysOnTop toggles the always-on-top state of the window.
func (a *App) ToggleAlwaysOnTop(onTop bool) {
	wailsRuntime.WindowSetAlwaysOnTop(a.ctx, onTop)
}

// SetWindowSize resizes the window (for mini-mode).
func (a *App) SetWindowSize(width, height int) {
	wailsRuntime.WindowSetSize(a.ctx, width, height)
}

// SetWindowMinSize sets the minimum window size.
func (a *App) SetWindowMinSize(width, height int) {
	wailsRuntime.WindowSetMinSize(a.ctx, width, height)
}

// EnterMiniMode shrinks the window and sets it to always on top.
func (a *App) EnterMiniMode() {
	// store normal size
	w, h := wailsRuntime.WindowGetSize(a.ctx)
	if w >= 900 && h >= 600 {
		a.normalWidth = w
		a.normalHeight = h
		a.db.SaveSetting("window_width", fmt.Sprintf("%d", w))
		a.db.SaveSetting("window_height", fmt.Sprintf("%d", h))
	}

	a.isMiniMode = true
	wailsRuntime.WindowSetAlwaysOnTop(a.ctx, true)
	wailsRuntime.WindowSetMinSize(a.ctx, 100, 50)
	
	mw, mh := 320, 80
	if a.miniWidth >= 100 && a.miniHeight >= 50 {
		mw = a.miniWidth
		mh = a.miniHeight
	}
	wailsRuntime.WindowSetSize(a.ctx, mw, mh)
}

// ExitMiniMode restores the window to normal size and unpins it.
func (a *App) ExitMiniMode() {
	// store mini size
	w, h := wailsRuntime.WindowGetSize(a.ctx)
	if w < 900 && h < 600 {
		a.miniWidth = w
		a.miniHeight = h
		a.db.SaveSetting("window_mini_width", fmt.Sprintf("%d", w))
		a.db.SaveSetting("window_mini_height", fmt.Sprintf("%d", h))
	}

	a.isMiniMode = false
	wailsRuntime.WindowSetAlwaysOnTop(a.ctx, false)
	wailsRuntime.WindowSetMinSize(a.ctx, 900, 600)
	if a.normalWidth >= 900 && a.normalHeight >= 600 {
		wailsRuntime.WindowSetSize(a.ctx, a.normalWidth, a.normalHeight)
	} else {
		wailsRuntime.WindowSetSize(a.ctx, 1100, 720)
	}
}

// validators

var hexColorRegex = regexp.MustCompile(`^#[0-9a-fA-F]{3}([0-9a-fA-F]{3})?$`)
var dateTimeRegex = regexp.MustCompile(`^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}$`)

func (a *App) validateTask(name, color string) error {
	if len(name) == 0 {
		return errors.New("task name cannot be empty")
	}
	if len(name) > 100 {
		return errors.New("task name is too long")
	}
	if !hexColorRegex.MatchString(color) {
		return errors.New("invalid task color format")
	}
	return nil
}

func (a *App) validateTimeEntry(taskID int64, startTime, endTime string, duration int64, note string) error {
	if taskID <= 0 {
		return errors.New("invalid task ID")
	}
	if !dateTimeRegex.MatchString(startTime) {
		return errors.New("invalid start time format")
	}
	if !dateTimeRegex.MatchString(endTime) {
		return errors.New("invalid end time format")
	}
	if duration < 0 {
		return errors.New("duration cannot be negative")
	}
	if len(note) > 500 {
		return errors.New("note is too long")
	}
	return nil
}
