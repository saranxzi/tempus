package main

import (
	"database/sql"
	"fmt"
	"os"
	"path/filepath"

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

// TaskSummary is used for the per-task analytics breakdown.
type TaskSummary struct {
	TaskName     string `json:"taskName"`
	TaskColor    string `json:"taskColor"`
	TotalSeconds int64  `json:"totalSeconds"`
}

// ──── Constructor ────────────────────────────────────────────────────────────

// NewDatabase opens (or creates) the SQLite database in the user's config directory.
func NewDatabase() (*Database, error) {
	dataDir, err := os.UserConfigDir()
	if err != nil {
		dataDir = "."
	}
	dbDir := filepath.Join(dataDir, "Tempus")
	if err := os.MkdirAll(dbDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create data directory: %w", err)
	}

	db, err := sql.Open("sqlite3", filepath.Join(dbDir, "tempus.db")+"?_journal_mode=WAL&_foreign_keys=on")
	if err != nil {
		return nil, fmt.Errorf("failed to open database: %w", err)
	}

	d := &Database{db: db}
	if err := d.migrate(); err != nil {
		db.Close()
		return nil, fmt.Errorf("migration failed: %w", err)
	}
	return d, nil
}

// Close closes the underlying database connection.
func (d *Database) Close() error {
	return d.db.Close()
}

// ──── Migrations ─────────────────────────────────────────────────────────────

func (d *Database) migrate() error {
	_, err := d.db.Exec(`
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

	-- Older builds stored some timestamps as "YYYY-MM-DD HH:MM:SS"; string range
	-- queries need the single "YYYY-MM-DDTHH:MM:SS" form.
	UPDATE time_entries
	SET start_time = replace(start_time, ' ', 'T'), end_time = replace(end_time, ' ', 'T')
	WHERE instr(start_time, ' ') > 0 OR instr(end_time, ' ') > 0;

	INSERT OR IGNORE INTO settings (key, value) VALUES
		('focus_duration', '25'), ('short_break_duration', '5'),
		('long_break_duration', '15'), ('long_break_interval', '4');
	`)
	return err
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

// GetTasks returns all tasks, newest first.
func (d *Database) GetTasks() ([]Task, error) {
	rows, err := d.db.Query(`SELECT id, name, color, created_at FROM tasks ORDER BY id DESC`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	tasks := []Task{}
	for rows.Next() {
		var t Task
		if err := rows.Scan(&t.ID, &t.Name, &t.Color, &t.CreatedAt); err != nil {
			return nil, err
		}
		tasks = append(tasks, t)
	}
	return tasks, rows.Err()
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
func (d *Database) AddTimeEntry(e TimeEntry) (int64, error) {
	res, err := d.db.Exec(
		`INSERT INTO time_entries (task_id, start_time, end_time, duration, note) VALUES (?, ?, ?, ?, ?)`,
		e.TaskID, e.StartTime, e.EndTime, e.Duration, e.Note,
	)
	if err != nil {
		return 0, err
	}
	return res.LastInsertId()
}

// UpdateTimeEntry modifies an existing time entry.
func (d *Database) UpdateTimeEntry(e TimeEntry) error {
	_, err := d.db.Exec(
		`UPDATE time_entries SET task_id = ?, start_time = ?, end_time = ?, duration = ?, note = ? WHERE id = ?`,
		e.TaskID, e.StartTime, e.EndTime, e.Duration, e.Note, e.ID,
	)
	return err
}

// DeleteTimeEntry removes a time entry.
func (d *Database) DeleteTimeEntry(id int64) error {
	_, err := d.db.Exec(`DELETE FROM time_entries WHERE id = ?`, id)
	return err
}

// GetTimeEntries returns time entries, optionally filtered by date range.
// Pass empty strings to get all entries.
func (d *Database) GetTimeEntries(startDate, endDate string) ([]TimeEntry, error) {
	if startDate != "" && endDate != "" {
		return d.queryEntries(`WHERE te.start_time BETWEEN ? AND ? ORDER BY te.start_time DESC`, startDate, endDate)
	}
	return d.queryEntries(`ORDER BY te.start_time DESC`)
}

// GetRecentEntries returns the N most recent time entries (for dashboard).
func (d *Database) GetRecentEntries(limit int) ([]TimeEntry, error) {
	return d.queryEntries(`ORDER BY te.start_time DESC LIMIT ?`, limit)
}

// queryEntries runs the shared entry SELECT with the given filter/order clause.
func (d *Database) queryEntries(clause string, args ...any) ([]TimeEntry, error) {
	rows, err := d.db.Query(`
		SELECT te.id, te.task_id, t.name, t.color, te.start_time, te.end_time, te.duration, te.note
		FROM time_entries te
		JOIN tasks t ON te.task_id = t.id
	`+clause, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	entries := []TimeEntry{}
	for rows.Next() {
		var e TimeEntry
		if err := rows.Scan(&e.ID, &e.TaskID, &e.TaskName, &e.TaskColor, &e.StartTime, &e.EndTime, &e.Duration, &e.Note); err != nil {
			return nil, err
		}
		entries = append(entries, e)
	}
	return entries, rows.Err()
}

// ──── Analytics Queries ──────────────────────────────────────────────────────

// GetDailySummary returns per-day total duration and task count for a date range.
func (d *Database) GetDailySummary(startDate, endDate string) ([]DaySummary, error) {
	rows, err := d.db.Query(`
		SELECT date(start_time) as day, SUM(duration), COUNT(DISTINCT task_id)
		FROM time_entries
		WHERE start_time BETWEEN ? AND ?
		GROUP BY day
		ORDER BY day
	`, startDate, endDate)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	summaries := []DaySummary{}
	for rows.Next() {
		var s DaySummary
		if err := rows.Scan(&s.Date, &s.TotalSeconds, &s.TaskCount); err != nil {
			return nil, err
		}
		summaries = append(summaries, s)
	}
	return summaries, rows.Err()
}

// GetTaskSummary returns per-task total duration for a date range.
func (d *Database) GetTaskSummary(startDate, endDate string) ([]TaskSummary, error) {
	rows, err := d.db.Query(`
		SELECT t.name, t.color, SUM(te.duration) AS total
		FROM time_entries te
		JOIN tasks t ON te.task_id = t.id
		WHERE te.start_time BETWEEN ? AND ?
		GROUP BY te.task_id
		ORDER BY total DESC
	`, startDate, endDate)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	summaries := []TaskSummary{}
	for rows.Next() {
		var s TaskSummary
		if err := rows.Scan(&s.TaskName, &s.TaskColor, &s.TotalSeconds); err != nil {
			return nil, err
		}
		summaries = append(summaries, s)
	}
	return summaries, rows.Err()
}

// GetTotalTime returns total tracked seconds overall.
func (d *Database) GetTotalTime() (int64, error) {
	var total sql.NullInt64
	err := d.db.QueryRow(`SELECT SUM(duration) FROM time_entries`).Scan(&total)
	return total.Int64, err
}

// ──── Settings ───────────────────────────────────────────────────────────────

// pomodoroKeys maps settings keys to the PomodoroSettings field they fill.
func pomodoroKeys(s *PomodoroSettings) map[string]*int {
	return map[string]*int{
		"focus_duration":       &s.FocusDuration,
		"short_break_duration": &s.ShortBreakDuration,
		"long_break_duration":  &s.LongBreakDuration,
		"long_break_interval":  &s.LongBreakInterval,
	}
}

// GetPomodoroSettings reads pomodoro settings, keeping defaults for invalid values.
func (d *Database) GetPomodoroSettings() (PomodoroSettings, error) {
	s := PomodoroSettings{FocusDuration: 25, ShortBreakDuration: 5, LongBreakDuration: 15, LongBreakInterval: 4}
	for key, field := range pomodoroKeys(&s) {
		val, err := d.GetSetting(key)
		if err != nil {
			return s, err
		}
		var v int
		if _, err := fmt.Sscan(val, &v); err == nil && v > 0 {
			*field = v
		}
	}
	return s, nil
}

// SavePomodoroSettings writes pomodoro settings to the database.
func (d *Database) SavePomodoroSettings(s PomodoroSettings) error {
	tx, err := d.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()

	for key, field := range pomodoroKeys(&s) {
		if _, err := tx.Exec(`INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)`, key, fmt.Sprint(*field)); err != nil {
			return err
		}
	}
	return tx.Commit()
}

// GetSetting returns a stored setting, or "" if it is not set.
func (d *Database) GetSetting(key string) (string, error) {
	var val string
	err := d.db.QueryRow(`SELECT value FROM settings WHERE key = ?`, key).Scan(&val)
	if err == sql.ErrNoRows {
		return "", nil
	}
	return val, err
}

// SaveSetting stores a setting.
func (d *Database) SaveSetting(key, value string) error {
	_, err := d.db.Exec(`INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)`, key, value)
	return err
}
