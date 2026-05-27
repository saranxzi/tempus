#pragma once
#include "pomodoro/types.h"
#include <filesystem>

namespace tempus {

/// Resolves and creates the application data directory under
/// %LOCALAPPDATA%\Tempus\. All paths are guaranteed to exist
/// after a successful call to initialize().
class AppPaths {
public:
    /// Resolve %LOCALAPPDATA% and create the Tempus data directory.
    /// Must be called once at startup before accessing any paths.
    static Result<AppPaths> initialize();

    const std::filesystem::path& data_dir()       const { return data_dir_; }
    std::filesystem::path        db_path()         const { return data_dir_ / "tempus.db"; }
    std::filesystem::path        log_path()        const { return data_dir_ / "tempus.log"; }
    std::filesystem::path        state_file_path() const { return data_dir_ / "state.json"; }

private:
    explicit AppPaths(std::filesystem::path dir) : data_dir_(std::move(dir)) {}
    std::filesystem::path data_dir_;
};

} // namespace tempus
