# Tempus

A lightweight, native Windows timer engine with structured time logging backed by SQLite.

## Features

- **Focus timer** with configurable work / short-break / long-break durations
- **Persistent session logging** to a local SQLite database (WAL mode)
- **Crash recovery** — periodic state snapshots enable seamless resume after unexpected shutdowns
- **Rotating file logger** — structured logs with automatic rotation (3 × 1 MB)
- **Deterministic testability** — injected clock interface allows instant simulation of elapsed time

## Architecture

```
┌─────────────┐     ┌──────────────────┐     ┌────────────┐
│ TimerEngine  │────▶│ DatabaseManager  │────▶│  SQLite3   │
│ (state logic)│     │ (persistence)    │     │  (WAL)     │
└──────┬───────┘     └──────────────────┘     └────────────┘
       │
       ├──▶ IClock  (abstracts steady_clock for testability)
       ├──▶ ILogger (rotating file logger)
       └──▶ StateRecovery (state.json crash recovery)
```

All components are wired via **constructor injection** — no singletons, no globals.

## Build

Requires **CMake ≥ 3.20** and a C++17 compiler (MSVC or MinGW-w64).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The release binary targets **< 2 MB** (SQLite statically linked, LTO enabled, symbols stripped).

### Run tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DTEMPUS_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
```

## Data storage

All runtime data lives under `%LOCALAPPDATA%\Tempus\`:

| File | Purpose |
|---|---|
| `tempus.db` | SQLite database (session log) |
| `tempus.log` | Active log file |
| `tempus.log.1‑3` | Rotated log archives |
| `state.json` | Timer snapshot for crash recovery |

## Project structure

```
Tempus/
├── CMakeLists.txt
├── cmake/CompilerFlags.cmake
├── third_party/sqlite3/          # Amalgamation (sqlite3.c + sqlite3.h)
├── include/pomodoro/
│   ├── types.h                   # Result<T>, enums, data structs
│   ├── clock.h                   # IClock interface + SystemClock
│   ├── logger.h                  # ILogger + RotatingFileLogger
│   ├── app_paths.h               # %LOCALAPPDATA% resolution
│   ├── database_manager.h        # IDatabaseManager + SqliteDatabaseManager
│   ├── timer_engine.h            # TimerEngine (state machine)
│   └── state_recovery.h          # StateRecovery (JSON snapshot)
├── src/
│   ├── main.cpp                  # Entry point, DI wiring
│   ├── logger.cpp
│   ├── app_paths.cpp
│   ├── database_manager.cpp
│   ├── state_recovery.cpp
│   └── timer_engine.cpp
└── tests/
    ├── CMakeLists.txt
    ├── mock_clock.h
    ├── test_timer_engine.cpp
    ├── test_database_manager.cpp
    └── test_state_recovery.cpp
```

## License

Private — all rights reserved.
