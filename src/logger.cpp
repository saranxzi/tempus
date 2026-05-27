#include "pomodoro/logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace tempus {

RotatingFileLogger::RotatingFileLogger(const std::filesystem::path& log_path,
                                       std::size_t max_file_size,
                                       std::size_t max_files)
    : base_path_(log_path)
    , max_file_size_(max_file_size)
    , max_files_(max_files)
{
    std::filesystem::create_directories(base_path_.parent_path());
    open_file();
}

RotatingFileLogger::~RotatingFileLogger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.close();
    }
}

void RotatingFileLogger::log(LogLevel level, const std::string& component,
                              const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Rotate if current file exceeds the size limit.
    if (file_.tellp() >= static_cast<std::streampos>(max_file_size_)) {
        rotate();
    }

    // Format: [2026-05-28 20:30:00] [INFO ] [TimerEngine] Session started
    file_ << "[" << timestamp() << "] "
          << "[" << level_str(level) << "] "
          << "[" << component << "] "
          << message << "\n";
    file_.flush();
}

void RotatingFileLogger::open_file() {
    file_.open(base_path_, std::ios::app | std::ios::ate);
}

void RotatingFileLogger::rotate() {
    file_.close();

    namespace fs = std::filesystem;

    // Delete the oldest archive if it exists (e.g., tempus.log.3)
    fs::path oldest = base_path_;
    oldest += "." + std::to_string(max_files_);
    if (fs::exists(oldest)) {
        fs::remove(oldest);
    }

    // Shift existing archives: .2 → .3, .1 → .2
    for (std::size_t i = max_files_ - 1; i >= 1; --i) {
        fs::path src = base_path_;
        src += "." + std::to_string(i);
        fs::path dst = base_path_;
        dst += "." + std::to_string(i + 1);
        if (fs::exists(src)) {
            fs::rename(src, dst);
        }
    }

    // Current file becomes .1
    fs::path rotated = base_path_;
    rotated += ".1";
    fs::rename(base_path_, rotated);

    // Open a fresh file.
    open_file();
}

const char* RotatingFileLogger::level_str(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default:              return "?????";
    }
}

std::string RotatingFileLogger::timestamp() {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace tempus
