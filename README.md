# Tempus — Time Tracker

A modern, lightweight desktop time tracking application built with **Go** and **Wails**.

## Features

- **Dashboard** — Overview of today's, this week's, and all-time tracked hours with a weekly bar chart
- **Pomodoro Timer** — Configurable focus/break durations with auto-advance through sessions
- **Stopwatch** — Real-time task timer with floating mini-overlay and always-on-top mode
- **Time Log** — Full CRUD for time entries with date filtering, start/end times, and notes
- **Analytics** — Daily trend charts and per-task breakdowns with progress bars
- **Task Management** — Color-coded tasks/projects with easy creation and editing
- **Keyboard Shortcuts** — Quick navigation with number keys (1-6)

## Tech Stack

| Layer     | Technology                    |
|-----------|-------------------------------|
| Backend   | Go 1.23+                      |
| Frontend  | HTML / CSS / JavaScript       |
| Framework | Wails v2                      |
| Database  | SQLite (via go-sqlite3)       |
| Platform  | Windows (native WebView2)     |

## Prerequisites

- [Go 1.23+](https://go.dev/dl/)
- [Wails CLI v2](https://wails.io/docs/gettingstarted/installation)
- GCC (for CGo / go-sqlite3) — use [MSYS2](https://www.msys2.org/) or [TDM-GCC](https://jmeubank.github.io/tdm-gcc/)

## Development

```bash
# Run in dev mode (live reload)
wails dev

# Build production executable
wails build
```

## Project Structure

```
Tempus/
├── main.go          # Entry point, Wails configuration
├── app.go           # App struct — Go ↔ JS bridge
├── database.go      # SQLite database layer (models, CRUD, analytics)
├── wails.json       # Wails project configuration
├── go.mod / go.sum  # Go module
├── frontend/
│   └── src/
│       ├── index.html   # App shell
│       ├── main.css     # Design system
│       └── main.js      # Frontend logic
└── build/           # Build assets (icons, manifests)
```

## License

MIT
