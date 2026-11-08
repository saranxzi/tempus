# Tempus Time Tracker

Lightweight desktop time tracking application built with Go and Wails.

## Features

- Dashboard overview of tracked time
- Pomodoro timer with break sessions
- Stopwatch with floating widget mode
- Time log manager
- Analytics charts
- Task categories

## Development

Requires Go, the [Wails CLI](https://wails.io/docs/gettingstarted/installation), and a C compiler
(CGO is needed by `go-sqlite3`). Data is stored in `%APPDATA%\Tempus\tempus.db` on Windows.

Run in dev mode:
```bash
wails dev
```

Build the production executable:
```bash
wails build
```

## Stack

- Go
- HTML, CSS, JavaScript
- Wails v2
- SQLite
