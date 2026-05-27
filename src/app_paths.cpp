#include "pomodoro/app_paths.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>   // SHGetKnownFolderPath, FOLDERID_LocalAppData
#pragma comment(lib, "shell32.lib")
#endif

#include <filesystem>
#include <system_error>

namespace tempus {

Result<AppPaths> AppPaths::initialize() {
    namespace fs = std::filesystem;

#ifdef _WIN32
    // Resolve %LOCALAPPDATA% using the modern Windows API.
    PWSTR raw_path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw_path);

    if (!SUCCEEDED(hr)) {
        CoTaskMemFree(raw_path);

        // Fallback: try the environment variable.
        const wchar_t* env = _wgetenv(L"LOCALAPPDATA");
        if (!env) {
            return Result<AppPaths>::err(
                "Failed to resolve LOCALAPPDATA (HRESULT=" +
                std::to_string(hr) + ")");
        }
        fs::path base(env);
        fs::path data_dir = base / "Tempus";

        std::error_code ec;
        fs::create_directories(data_dir, ec);
        if (ec) {
            return Result<AppPaths>::err(
                "Failed to create data directory: " + ec.message());
        }
        return Result<AppPaths>::ok(AppPaths(std::move(data_dir)));
    }

    fs::path base(raw_path);
    CoTaskMemFree(raw_path);
#else
    // Non-Windows fallback (for development/cross-compilation).
    const char* home = std::getenv("HOME");
    if (!home) {
        return Result<AppPaths>::err("HOME environment variable not set");
    }
    fs::path base = fs::path(home) / ".local" / "share";
#endif

    fs::path data_dir = base / "Tempus";

    std::error_code ec;
    fs::create_directories(data_dir, ec);
    if (ec) {
        return Result<AppPaths>::err(
            "Failed to create data directory '" +
            data_dir.string() + "': " + ec.message());
    }

    return Result<AppPaths>::ok(AppPaths(std::move(data_dir)));
}

} // namespace tempus
