#include "pomodoro/state_recovery.h"
#include <fstream>
#include <sstream>
#include <string>

// ─── Minimal JSON helpers (fixed schema, no external library) ───────────────
// The TimerSnapshot has only 5 fields with simple types, so hand-rolling
// avoids pulling in nlohmann/json (~500KB of header bloat).

namespace tempus {
namespace {

/// Escape a string for JSON output (handles quotes and backslashes).
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

/// Serialize a TimerSnapshot to a JSON string.
std::string snapshot_to_json(const TimerSnapshot& snap) {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"state\": \""       << to_string(snap.state) << "\",\n"
        << "  \"remaining_sec\": " << snap.remaining_sec     << ",\n"
        << "  \"current_task\": \"" << json_escape(snap.current_task) << "\",\n"
        << "  \"session_count\": " << snap.session_count     << ",\n"
        << "  \"timestamp\": "     << snap.timestamp         << "\n"
        << "}";
    return oss.str();
}

/// Trim leading/trailing whitespace and quotes from a string value.
std::string trim_json_string(const std::string& s) {
    std::size_t start = s.find('"');
    std::size_t end   = s.rfind('"');
    if (start == std::string::npos || end == std::string::npos || start == end) {
        return "";
    }
    return s.substr(start + 1, end - start - 1);
}

/// Extract the value portion after the colon in a JSON line.
std::string extract_value(const std::string& line) {
    auto colon = line.find(':');
    if (colon == std::string::npos) return "";
    std::string val = line.substr(colon + 1);
    // Strip trailing comma, whitespace.
    while (!val.empty() && (val.back() == ',' || val.back() == ' ' ||
           val.back() == '\r' || val.back() == '\n')) {
        val.pop_back();
    }
    // Strip leading whitespace.
    auto first = val.find_first_not_of(" \t");
    return (first == std::string::npos) ? "" : val.substr(first);
}

/// Parse a TimerState from its string representation.
TimerState parse_state(const std::string& s) {
    if (s == "Focusing")   return TimerState::Focusing;
    if (s == "ShortBreak") return TimerState::ShortBreak;
    if (s == "LongBreak")  return TimerState::LongBreak;
    if (s == "Paused")     return TimerState::Paused;
    return TimerState::Idle;
}

/// Deserialize a JSON string into a TimerSnapshot.
Result<TimerSnapshot> snapshot_from_json(const std::string& json) {
    TimerSnapshot snap;
    std::istringstream stream(json);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.find("\"state\"") != std::string::npos) {
            snap.state = parse_state(trim_json_string(extract_value(line)));
        } else if (line.find("\"remaining_sec\"") != std::string::npos) {
            try { snap.remaining_sec = std::stoi(extract_value(line)); }
            catch (...) { return Result<TimerSnapshot>::err("Invalid remaining_sec"); }
        } else if (line.find("\"current_task\"") != std::string::npos) {
            snap.current_task = trim_json_string(extract_value(line));
        } else if (line.find("\"session_count\"") != std::string::npos) {
            try { snap.session_count = std::stoi(extract_value(line)); }
            catch (...) { return Result<TimerSnapshot>::err("Invalid session_count"); }
        } else if (line.find("\"timestamp\"") != std::string::npos) {
            try { snap.timestamp = static_cast<std::time_t>(std::stoll(extract_value(line))); }
            catch (...) { return Result<TimerSnapshot>::err("Invalid timestamp"); }
        }
    }

    return Result<TimerSnapshot>::ok(std::move(snap));
}

} // anonymous namespace

// ─── Public API ─────────────────────────────────────────────────────────────

VoidR StateRecovery::save(const TimerSnapshot& snapshot,
                          const std::filesystem::path& path,
                          ILogger* logger) {
    namespace fs = std::filesystem;

    // Write to a temporary file first, then atomically rename.
    // This prevents partial writes if the process is killed mid-write.
    fs::path tmp_path = path;
    tmp_path += ".tmp";

    {
        std::ofstream ofs(tmp_path, std::ios::trunc);
        if (!ofs.is_open()) {
            return VoidR::err("Failed to open temp state file: " + tmp_path.string());
        }
        ofs << snapshot_to_json(snapshot);
        ofs.flush();
        if (ofs.fail()) {
            return VoidR::err("Failed to write state file: " + tmp_path.string());
        }
    }

    // Atomic rename (overwrites target on Windows via rename).
    std::error_code ec;
    fs::rename(tmp_path, path, ec);
    if (ec) {
        // Fallback: on some Windows configurations rename fails if target exists.
        fs::remove(path, ec);
        fs::rename(tmp_path, path, ec);
        if (ec) {
            return VoidR::err("Failed to rename state file: " + ec.message());
        }
    }

    logger->debug("StateRecovery", "Saved snapshot: remaining=" +
                  std::to_string(snapshot.remaining_sec) + "s");
    return VoidR::ok();
}

Result<std::optional<TimerSnapshot>> StateRecovery::load(
    const std::filesystem::path& path, ILogger* logger)
{
    namespace fs = std::filesystem;

    if (!fs::exists(path)) {
        logger->info("StateRecovery", "No state file found, starting fresh");
        return Result<std::optional<TimerSnapshot>>::ok(std::nullopt);
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return Result<std::optional<TimerSnapshot>>::err(
            "State file exists but cannot be opened: " + path.string());
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();

    if (content.empty()) {
        logger->warn("StateRecovery", "State file is empty, ignoring");
        return Result<std::optional<TimerSnapshot>>::ok(std::nullopt);
    }

    auto result = snapshot_from_json(content);
    if (result.is_err()) {
        logger->error("StateRecovery", "Corrupt state file: " + result.error());
        return Result<std::optional<TimerSnapshot>>::err(result.error());
    }

    logger->info("StateRecovery",
                 "Loaded snapshot: state=" + std::string(to_string(result.value().state)) +
                 " remaining=" + std::to_string(result.value().remaining_sec) + "s");
    return Result<std::optional<TimerSnapshot>>::ok(std::move(result.value()));
}

void StateRecovery::clear(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    // Best-effort — if the file doesn't exist, that's fine.
}

} // namespace tempus
