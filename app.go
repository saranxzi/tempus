package main

import (
	"context"
	"fmt"
	"time"

	wailsRuntime "github.com/wailsapp/wails/v2/pkg/runtime"
)

// App struct holds the application state.
type App struct {
	ctx context.Context
	db  *Database
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

// shutdown is called at application termination.
func (a *App) shutdown(ctx context.Context) {
	if a.db != nil {
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
	return a.db.AddTask(name, color)
}

// UpdateTask modifies a task.
func (a *App) UpdateTask(id int64, name, color string) error {
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
	return a.db.AddTimeEntry(taskID, startTime, endTime, duration, note)
}

// UpdateTimeEntry modifies an existing time entry.
func (a *App) UpdateTimeEntry(id, taskID int64, startTime, endTime string, duration int64, note string) error {
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
	wailsRuntime.WindowSetAlwaysOnTop(a.ctx, true)
	wailsRuntime.WindowSetMinSize(a.ctx, 100, 50)
	wailsRuntime.WindowSetSize(a.ctx, 320, 80)
}

// ExitMiniMode restores the window to normal size and unpins it.
func (a *App) ExitMiniMode() {
	wailsRuntime.WindowSetAlwaysOnTop(a.ctx, false)
	wailsRuntime.WindowSetMinSize(a.ctx, 900, 600)
	wailsRuntime.WindowSetSize(a.ctx, 1100, 720)
}
