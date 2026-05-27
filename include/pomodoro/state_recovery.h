#pragma once
#include "pomodoro/types.h"
#include "pomodoro/logger.h"
#include <filesystem>
#include <optional>

namespace tempus {

/// Serializes/deserializes TimerSnapshot to a JSON file for crash recovery.
/// Uses atomic write (temp file + rename) to prevent partial-write corruption.
class StateRecovery {
public:
    /// Save a timer snapshot to disk. Writes to a temp file first,
    /// then atomically renames to the target path.
    static VoidR save(const TimerSnapshot& snapshot,
                      const std::filesystem::path& path,
                      ILogger* logger);

    /// Load a timer snapshot from disk.
    /// Returns std::nullopt if the file does not exist.
    /// Returns an error if the file exists but is corrupt/unreadable.
    static Result<std::optional<TimerSnapshot>> load(
        const std::filesystem::path& path,
        ILogger* logger);

    /// Delete the state file (called after successful restore).
    static void clear(const std::filesystem::path& path);
};

} // namespace tempus
