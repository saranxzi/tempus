package main

import (
	"database/sql"
	"fmt"
	"os"
	"path/filepath"
	"time"

	_ "github.com/mattn/go-sqlite3"
)

// Database wraps the SQLite connection and provides all data operations.
type Database struct {
	db *sql.DB
}

// ──── Models ─────────────────────────────────────────────────────────────────

// Task represents a tracked task / project.
type Task struct {
	ID        int64  `json:"id"`
	Name      string `json:"name"`
	Color     string `json:"color"`
	CreatedAt string `json:"createdAt"`
}

// TimeEntry represents a single time tracking entry.
type TimeEntry struct {
	ID        int64  `json:"id"`
	TaskID    int64  `json:"taskId"`
	TaskName  string `json:"taskName"`
	TaskColor string `json:"taskColor"`
	StartTime string `json:"startTime"`
	EndTime   string `json:"endTime"`
	Duration  int64  `json:"duration"` // seconds
	Note      string `json:"note"`
}

// PomodoroSettings holds user-configurable pomodoro timer settings.
type PomodoroSettings struct {
	FocusDuration      int `json:"focusDuration"`      // minutes
	ShortBreakDuration int `json:"shortBreakDuration"` // minutes
	LongBreakDuration  int `json:"longBreakDuration"`  // minutes
	LongBreakInterval  int `json:"longBreakInterval"`  // sessions before long break
}

// DaySummary is a per-day aggregation used for analytics.
type DaySummary struct {
	Date         string `json:"date"`
	TotalSeconds int64  `json:"totalSeconds"`
	TaskCount    int    `json:"taskCount"`
}

// TaskSummary is used for pie-chart analytics.
type TaskSummary struct {
	TaskName     string `json:"taskName"`
	TaskColor    string `json:"taskColor"`
	TotalSeconds int64  `json:"totalSeconds"`
}

// ──── Constructor ────────────────────────────────────────────────────────────

// NewDatabase opens (or creates) the SQLite database in the user's AppData directory.
func NewDatabase() (*Database, error) {
	dataDir, err := os.UserConfigDir()
	if err != nil {
		dataDir = "."
	}
	dbDir := filepath.Join(dataDir, "Tempus")
	if err := os.MkdirAll(dbDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create data directory: %w", err)
	}
	dbPath := filepath.Join(dbDir, "tempus.db")

	db, err := sql.Open("sqlite3", dbPath+"?_journal_mode=WAL&_foreign_keys=on")
	if err != nil {
		return nil, fmt.Errorf("failed to open database: %w", err)
	}

	d := &Database{db: db}
	if err := d.migrate(); err != nil {
		return nil, fmt.Errorf("migration failed: %w", err)
	}
	return d, nil
}

// Close closes the underlying database connection.
func (d *Database) Close() error {
	if d.db != nil {
		return d.db.Close()
	}
	return nil
}

// ──── Migrations ─────────────────────────────────────────────────────────────

func (d *Database) migrate() error {
	schema := `
	CREATE TABLE IF NOT EXISTS tasks (
		id         INTEGER PRIMARY KEY AUTOINCREMENT,
		name       TEXT    NOT NULL UNIQUE,
		color      TEXT    NOT NULL DEFAULT '#6366f1',
		created_at TEXT    NOT NULL DEFAULT (datetime('now'))
	);

	CREATE TABLE IF NOT EXISTS time_entries (
		id         INTEGER PRIMARY KEY AUTOINCREMENT,
		task_id    INTEGER NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
		start_time TEXT    NOT NULL,
		end_time   TEXT    NOT NULL,
		duration   INTEGER NOT NULL, -- seconds
		note       TEXT    NOT NULL DEFAULT ''
	);

	CREATE TABLE IF NOT EXISTS settings (
		key   TEXT PRIMARY KEY,
		value TEXT NOT NULL
	);

	CREATE INDEX IF NOT EXISTS idx_time_entries_task   ON time_entries(task_id);
	CREATE INDEX IF NOT EXISTS idx_time_entries_start  ON time_entries(start_time);
	`
	_, err := d.db.Exec(schema)
	if err != nil {
		return err
	}

	// Seed default pomodoro settings if not present.
	defaults := map[string]string{
		"focus_duration":       "25",
		"short_break_duration": "5",
		"long_break_duration":  "15",
		"long_break_interval":  "4",
	}
	for k, v := range defaults {
		_, err := d.db.Exec(`INSERT OR IGNORE INTO settings (key, value) VALUES (?, ?)`, k, v)
		if err != nil {
			return err
		}
	}
	return nil
}

// ──── Task CRUD ──────────────────────────────────────────────────────────────

// AddTask creates a new task and returns its ID.
func (d *Database) AddTask(name, color string) (int64, error) {
	res, err := d.db.Exec(`INSERT INTO tasks (name, color) VALUES (?, ?)`, name, color)
	if err != nil {
		return 0, err
	}
	return res.LastInsertId()
}

// GetTasks returns all tasks ordered by creation date descending.
func (d *Database) GetTasks() ([]Task, error) {
	rows, err := d.db.Query(`SELECT id, name, color, created_at FROM tasks ORDER BY created_at DESC`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var tasks []Task
	for rows.Next() {
		var t Task
		if err := rows.Scan(&t.ID, &t.Name, &t.Color, &t.CreatedAt); err != nil {
			return nil, err
		}
		tasks = append(tasks, t)
	}
	if tasks == nil {
		tasks = []Task{}
	}
	return tasks, nil
}

// UpdateTask updates a task's name and color.
func (d *Database) UpdateTask(id int64, name, color string) error {
	_, err := d.db.Exec(`UPDATE tasks SET name = ?, color = ? WHERE id = ?`, name, color, id)
	return err
}

// DeleteTask removes a task and all its time entries (via CASCADE).
func (d *Database) DeleteTask(id int64) error {
	_, err := d.db.Exec(`DELETE FROM tasks WHERE id = ?`, id)
	return err
}

// ──── Time Entry CRUD ────────────────────────────────────────────────────────

// AddTimeEntry creates a new time entry.
func (d *Database) AddTimeEntry(taskID int64, startTime, endTime string, duration int64, note string) (int64, error) {
	res, err := d.db.Exec(
		`INSERT INTO time_entries (task_id, start_time, end_time, duration, note) VALUES (?, ?, ?, ?, ?)`,
		taskID, startTime, endTime, duration, note,
	)
	if err != nil {
		return 0, err
	}
	return res.LastInsertId()
}

// GetTimeEntries returns time entries, optionally filtered by date range.
// Pass empty strings to get all entries.
func (d *Database) GetTimeEntries(startDate, endDate string) ([]TimeEntry, error) {
	query := `
		SELECT te.id, te.task_id, t.name, t.color, te.start_time, te.end_time, te.duration, te.note
		FROM time_entries te
		JOIN tasks t ON te.task_id = t.id
	`
	var args []interface{}
	if startDate != "" && endDate != "" {
		query += ` WHERE te.start_time >= ? AND te.start_time <= ?`
		args = append(args, startDate, endDate)
	}
	query += ` ORDER BY te.start_time DESC`

	rows, err := d.db.Query(query, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var entries []TimeEntry
	for rows.Next() {
		var e TimeEntry
		if err := rows.Scan(&e.ID, &e.TaskID, &e.TaskName, &e.TaskColor, &e.StartTime, &e.EndTime, &e.Duration, &e.Note); err != nil {
			return nil, err
		}
		entries = append(entries, e)
	}
	if entries == nil {
		entries = []TimeEntry{}
	}
	return entries, nil
}

// UpdateTimeEntry modifies an existing time entry.
func (d *Database) UpdateTimeEntry(id, taskID int64, startTime, endTime string, duration int64, note string) error {
	_, err := d.db.Exec(
		`UPDATE time_entries SET task_id = ?, start_time = ?, end_time = ?, duration = ?, note = ? WHERE id = ?`,
		taskID, startTime, endTime, duration, note, id,
	)
	return err
}

// DeleteTimeEntry removes a time entry.
func (d *Database) DeleteTimeEntry(id int64) error {
	_, err := d.db.Exec(`DELETE FROM time_entries WHERE id = ?`, id)
	return err
}

// ──── Analytics Queries ──────────────────────────────────────────────────────

// GetDailySummary returns per-day total duration and task count for a date range.
func (d *Database) GetDailySummary(startDate, endDate string) ([]DaySummary, error) {
	rows, err := d.db.Query(`
		SELECT date(start_time) as day, SUM(duration), COUNT(DISTINCT task_id)
		FROM time_entries
		WHERE start_time >= ? AND start_time <= ?
		GROUP BY day
		ORDER BY day
	`, startDate, endDate)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var summaries []DaySummary
	for rows.Next() {
		var s DaySummary
		if err := rows.Scan(&s.Date, &s.TotalSeconds, &s.TaskCount); err != nil {
			return nil, err
		}
		summaries = append(summaries, s)
	}
	if summaries == nil {
		summaries = []DaySummary{}
	}
	return summaries, nil
}

// GetTaskSummary returns per-task total duration for a date range (for pie chart).
func (d *Database) GetTaskSummary(startDate, endDate string) ([]TaskSummary, error) {
	rows, err := d.db.Query(`
		SELECT t.name, t.color, SUM(te.duration)
		FROM time_entries te
		JOIN tasks t ON te.task_id = t.id
		WHERE te.start_time >= ? AND te.start_time <= ?
		GROUP BY te.task_id
		ORDER BY SUM(te.duration) DESC
	`, startDate, endDate)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var summaries []TaskSummary
	for rows.Next() {
		var s TaskSummary
		if err := rows.Scan(&s.TaskName, &s.TaskColor, &s.TotalSeconds); err != nil {
			return nil, err
		}
		summaries = append(summaries, s)
	}
	if summaries == nil {
		summaries = []TaskSummary{}
	}
	return summaries, nil
}

// GetTotalTime returns total tracked seconds overall.
func (d *Database) GetTotalTime() (int64, error) {
	var total sql.NullInt64
	err := d.db.QueryRow(`SELECT SUM(duration) FROM time_entries`).Scan(&total)
	if err != nil {
		return 0, err
	}
	return total.Int64, nil
}

// GetTodayTime returns total tracked seconds for today.
func (d *Database) GetTodayTime() (int64, error) {
	today := time.Now().Format("2006-01-02")
	var total sql.NullInt64
	err := d.db.QueryRow(`SELECT SUM(duration) FROM time_entries WHERE date(start_time) = ?`, today).Scan(&total)
	if err != nil {
		return 0, err
	}
	return total.Int64, nil
}

// GetWeekTime returns total tracked seconds for the current week.
func (d *Database) GetWeekTime() (int64, error) {
	now := time.Now()
	weekday := int(now.Weekday())
	if weekday == 0 {
		weekday = 7
	}
	monday := now.AddDate(0, 0, -(weekday - 1))
	startOfWeek := monday.Format("2006-01-02")

	var total sql.NullInt64
	err := d.db.QueryRow(`SELECT SUM(duration) FROM time_entries WHERE date(start_time) >= ?`, startOfWeek).Scan(&total)
	if err != nil {
		return 0, err
	}
	return total.Int64, nil
}

// GetRecentEntries returns the N most recent time entries (for dashboard).
func (d *Database) GetRecentEntries(limit int) ([]TimeEntry, error) {
	rows, err := d.db.Query(`
		SELECT te.id, te.task_id, t.name, t.color, te.start_time, te.end_time, te.duration, te.note
		FROM time_entries te
		JOIN tasks t ON te.task_id = t.id
		ORDER BY te.start_time DESC
		LIMIT ?
	`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var entries []TimeEntry
	for rows.Next() {
		var e TimeEntry
		if err := rows.Scan(&e.ID, &e.TaskID, &e.TaskName, &e.TaskColor, &e.StartTime, &e.EndTime, &e.Duration, &e.Note); err != nil {
			return nil, err
		}
		entries = append(entries, e)
	}
	if entries == nil {
		entries = []TimeEntry{}
	}
	return entries, nil
}

// ──── Settings ───────────────────────────────────────────────────────────────

// GetPomodoroSettings reads pomodoro settings from the database.
func (d *Database) GetPomodoroSettings() (PomodoroSettings, error) {
	settings := PomodoroSettings{
		FocusDuration:      25,
		ShortBreakDuration: 5,
		LongBreakDuration:  15,
		LongBreakInterval:  4,
	}
	rows, err := d.db.Query(`SELECT key, value FROM settings WHERE key IN ('focus_duration', 'short_break_duration', 'long_break_duration', 'long_break_interval')`)
	if err != nil {
		return settings, err
	}
	defer rows.Close()

	for rows.Next() {
		var key, value string
		if err := rows.Scan(&key, &value); err != nil {
			continue
		}
		var v int
		fmt.Sscanf(value, "%d", &v)
		switch key {
		case "focus_duration":
			settings.FocusDuration = v
		case "short_break_duration":
			settings.ShortBreakDuration = v
		case "long_break_duration":
			settings.LongBreakDuration = v
		case "long_break_interval":
			settings.LongBreakInterval = v
		}
	}
	return settings, nil
}

// SavePomodoroSettings writes pomodoro settings to the database.
func (d *Database) SavePomodoroSettings(s PomodoroSettings) error {
	tx, err := d.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()

	updates := map[string]int{
		"focus_duration":       s.FocusDuration,
		"short_break_duration": s.ShortBreakDuration,
		"long_break_duration":  s.LongBreakDuration,
		"long_break_interval":  s.LongBreakInterval,
	}
	for k, v := range updates {
		_, err := tx.Exec(`INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)`, k, fmt.Sprintf("%d", v))
		if err != nil {
			return err
		}
	}
	return tx.Commit()
}
