package main

import (
	"context"
	"errors"
	"regexp"
	"strconv"
	"strings"
	"time"

	wailsRuntime "github.com/wailsapp/wails/v2/pkg/runtime"
)

// timeLayout is the single local-time format used for every stored timestamp.
const timeLayout = "2006-01-02T15:04:05"

// App struct holds the application state.
type App struct {
	ctx        context.Context
	db         *Database
	isMiniMode bool
}

// NewApp creates a new App backed by the given database.
func NewApp(db *Database) *App {
	return &App{db: db}
}

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
}

// domReady restores the last normal window size.
func (a *App) domReady(ctx context.Context) {
	if w, h, ok := a.savedSize("window"); ok {
		wailsRuntime.WindowSetSize(ctx, w, h)
	}
}

// shutdown persists the window size and closes the database.
func (a *App) shutdown(ctx context.Context) {
	a.rememberSize()
	a.db.Close()
}

// ──── Dashboard ──────────────────────────────────────────────────────────────

// DashboardData is the aggregate payload sent to the frontend dashboard.
type DashboardData struct {
	TodaySeconds   int64        `json:"todaySeconds"`
	WeekSeconds    int64        `json:"weekSeconds"`
	TotalSeconds   int64        `json:"totalSeconds"`
	TaskCount      int          `json:"taskCount"`
	RecentEntries  []TimeEntry  `json:"recentEntries"`
	WeekDaySummary []DaySummary `json:"weekDaySummary"`
}

// GetDashboardData returns all the data needed by the dashboard in one call.
func (a *App) GetDashboardData() (DashboardData, error) {
	var data DashboardData
	var err error

	now := time.Now()
	monday := now.AddDate(0, 0, -((int(now.Weekday()) + 6) % 7))
	sunday := monday.AddDate(0, 0, 6)
	if data.WeekDaySummary, err = a.db.GetDailySummary(monday.Format("2006-01-02"), sunday.Format("2006-01-02")+"T23:59:59"); err != nil {
		return data, err
	}
	today := now.Format("2006-01-02")
	for _, d := range data.WeekDaySummary {
		data.WeekSeconds += d.TotalSeconds
		if d.Date == today {
			data.TodaySeconds = d.TotalSeconds
		}
	}

	if data.TotalSeconds, err = a.db.GetTotalTime(); err != nil {
		return data, err
	}
	tasks, err := a.db.GetTasks()
	if err != nil {
		return data, err
	}
	data.TaskCount = len(tasks)
	data.RecentEntries, err = a.db.GetRecentEntries(6)
	return data, err
}

// ──── Tasks ──────────────────────────────────────────────────────────────────

// GetTasks returns all tasks.
func (a *App) GetTasks() ([]Task, error) {
	return a.db.GetTasks()
}

// AddTask creates a new task.
func (a *App) AddTask(name, color string) (int64, error) {
	name = strings.TrimSpace(name)
	if err := validateTask(name, color); err != nil {
		return 0, err
	}
	return a.db.AddTask(name, color)
}

// UpdateTask modifies a task.
func (a *App) UpdateTask(id int64, name, color string) error {
	name = strings.TrimSpace(name)
	if err := validateTask(name, color); err != nil {
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

// AddTimeEntry creates a new time entry; the duration is derived from the times.
func (a *App) AddTimeEntry(taskID int64, startTime, endTime, note string) (int64, error) {
	e, err := newEntry(taskID, startTime, endTime, note)
	if err != nil {
		return 0, err
	}
	return a.db.AddTimeEntry(e)
}

// UpdateTimeEntry modifies an existing time entry.
func (a *App) UpdateTimeEntry(id, taskID int64, startTime, endTime, note string) error {
	e, err := newEntry(taskID, startTime, endTime, note)
	if err != nil {
		return err
	}
	e.ID = id
	return a.db.UpdateTimeEntry(e)
}

// DeleteTimeEntry removes a time entry.
func (a *App) DeleteTimeEntry(id int64) error {
	return a.db.DeleteTimeEntry(id)
}

// SaveStopwatchEntry logs a session of the given length that ends now.
func (a *App) SaveStopwatchEntry(taskID int64, durationSeconds int64, note string) (int64, error) {
	end := time.Now()
	start := end.Add(-time.Duration(durationSeconds) * time.Second)
	return a.AddTimeEntry(taskID, start.Format(timeLayout), end.Format(timeLayout), note)
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
	var data AnalyticsData
	var err error
	endDate += "T23:59:59"
	if data.DailySummary, err = a.db.GetDailySummary(startDate, endDate); err != nil {
		return data, err
	}
	if data.TaskSummary, err = a.db.GetTaskSummary(startDate, endDate); err != nil {
		return data, err
	}
	for _, d := range data.DailySummary {
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
func (a *App) SavePomodoroSettings(s PomodoroSettings) error {
	if s.FocusDuration < 1 || s.ShortBreakDuration < 1 || s.LongBreakDuration < 1 || s.LongBreakInterval < 1 {
		return errors.New("pomodoro settings must be at least 1")
	}
	return a.db.SavePomodoroSettings(s)
}

// ──── Window Controls ────────────────────────────────────────────────────────

// ToggleAlwaysOnTop pins or unpins the window above other windows.
func (a *App) ToggleAlwaysOnTop(onTop bool) {
	wailsRuntime.WindowSetAlwaysOnTop(a.ctx, onTop)
}

// EnterMiniMode shrinks the window into an always-on-top floating timer.
func (a *App) EnterMiniMode() {
	if a.isMiniMode {
		return
	}
	a.rememberSize()
	a.isMiniMode = true
	wailsRuntime.WindowUnmaximise(a.ctx)
	wailsRuntime.WindowSetMinSize(a.ctx, 100, 50)
	w, h, ok := a.savedSize("window_mini")
	if !ok {
		w, h = 340, 80
	}
	wailsRuntime.WindowSetSize(a.ctx, w, h)
	wailsRuntime.WindowSetAlwaysOnTop(a.ctx, true)
}

// ExitMiniMode restores the normal window and unpins it.
func (a *App) ExitMiniMode() {
	if !a.isMiniMode {
		return
	}
	a.rememberSize()
	a.isMiniMode = false
	wailsRuntime.WindowSetAlwaysOnTop(a.ctx, false)
	wailsRuntime.WindowSetMinSize(a.ctx, 900, 600)
	w, h, ok := a.savedSize("window")
	if !ok {
		w, h = 1100, 720
	}
	wailsRuntime.WindowSetSize(a.ctx, w, h)
}

// rememberSize stores the current window size for the current mode.
func (a *App) rememberSize() {
	if wailsRuntime.WindowIsMaximised(a.ctx) || wailsRuntime.WindowIsMinimised(a.ctx) {
		return
	}
	key := "window"
	if a.isMiniMode {
		key = "window_mini"
	}
	w, h := wailsRuntime.WindowGetSize(a.ctx)
	a.db.SaveSetting(key+"_width", strconv.Itoa(w))
	a.db.SaveSetting(key+"_height", strconv.Itoa(h))
}

// savedSize returns the stored window size for a settings key prefix.
func (a *App) savedSize(key string) (int, int, bool) {
	ws, _ := a.db.GetSetting(key + "_width")
	hs, _ := a.db.GetSetting(key + "_height")
	w, errW := strconv.Atoi(ws)
	h, errH := strconv.Atoi(hs)
	return w, h, errW == nil && errH == nil && w > 0 && h > 0
}

// ──── Validation ─────────────────────────────────────────────────────────────

var hexColorRegex = regexp.MustCompile(`^#[0-9a-fA-F]{3}([0-9a-fA-F]{3})?$`)

func validateTask(name, color string) error {
	switch {
	case name == "":
		return errors.New("task name cannot be empty")
	case len(name) > 100:
		return errors.New("task name is too long")
	case !hexColorRegex.MatchString(color):
		return errors.New("invalid task color format")
	}
	return nil
}

// newEntry validates input and builds a normalized TimeEntry.
func newEntry(taskID int64, startTime, endTime, note string) (TimeEntry, error) {
	if taskID <= 0 {
		return TimeEntry{}, errors.New("invalid task ID")
	}
	if len(note) > 500 {
		return TimeEntry{}, errors.New("note is too long")
	}
	start, errS := time.ParseInLocation(timeLayout, startTime, time.Local)
	end, errE := time.ParseInLocation(timeLayout, endTime, time.Local)
	if errS != nil || errE != nil {
		return TimeEntry{}, errors.New("invalid time format")
	}
	if !end.After(start) {
		return TimeEntry{}, errors.New("end time must be after start time")
	}
	return TimeEntry{
		TaskID:    taskID,
		StartTime: start.Format(timeLayout),
		EndTime:   end.Format(timeLayout),
		Duration:  int64(end.Sub(start) / time.Second),
		Note:      strings.TrimSpace(note),
	}, nil
}
