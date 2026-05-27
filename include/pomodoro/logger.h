#pragma once
#include <string>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstddef>

namespace tempus {

/// Log severity levels.
enum class LogLevel { Debug, Info, Warn, Error };

/// Abstract logger interface. Injected into all components.
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, const std::string& component,
                     const std::string& message) = 0;

    void debug(const std::string& comp, const std::string& msg) { log(LogLevel::Debug, comp, msg); }
    void info (const std::string& comp, const std::string& msg) { log(LogLevel::Info,  comp, msg); }
    void warn (const std::string& comp, const std::string& msg) { log(LogLevel::Warn,  comp, msg); }
    void error(const std::string& comp, const std::string& msg) { log(LogLevel::Error, comp, msg); }
};

/// Rotating file logger. Writes structured log lines to disk,
/// rotating when a file exceeds max_file_size. Keeps at most
/// max_files rotated archives. Thread-safe.
class RotatingFileLogger final : public ILogger {
public:
    /// @param log_path   Full path to the active log file (e.g., tempus.log).
    /// @param max_file_size  Rotate after this many bytes (default 1 MB).
    /// @param max_files  Number of rotated archives to keep (default 3).
    explicit RotatingFileLogger(const std::filesystem::path& log_path,
                                std::size_t max_file_size = 1 * 1024 * 1024,
                                std::size_t max_files = 3);
    ~RotatingFileLogger() override;

    void log(LogLevel level, const std::string& component,
             const std::string& message) override;

private:
    void open_file();
    void rotate();
    static const char* level_str(LogLevel level);
    static std::string timestamp();

    std::filesystem::path base_path_;
    std::ofstream         file_;
    std::size_t           max_file_size_;
    std::size_t           max_files_;
    std::mutex            mutex_;
};

/// No-op logger for tests that don't need log output.
class NullLogger final : public ILogger {
public:
    void log(LogLevel, const std::string&, const std::string&) override {}
};

} // namespace tempus
