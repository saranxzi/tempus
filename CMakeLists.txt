cmake_minimum_required(VERSION 3.20)

# Static CRT linking (MSVC) — must be set before project()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Tempus
    VERSION   0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Lightweight timer engine with structured time logging"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Compiler / linker flags for minimal release binary
# ---------------------------------------------------------------------------
include(cmake/CompilerFlags.cmake)

# ---------------------------------------------------------------------------
# SQLite3 — static library from amalgamation
# ---------------------------------------------------------------------------
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_DQS=0
    SQLITE_LIKE_DOESNT_MATCH_BLOBS=1
    SQLITE_OMIT_DEPRECATED=1
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_OMIT_COMPLETE=1
    SQLITE_OMIT_DECLTYPE=1
    SQLITE_OMIT_PROGRESS_CALLBACK=1
    SQLITE_OMIT_SHARED_CACHE=1
)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /O1 /W3)
else()
    target_compile_options(sqlite3 PRIVATE -Os -w)
endif()

# ---------------------------------------------------------------------------
# Dear ImGui — fetch and build
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
if(MSVC)
    target_compile_options(imgui PRIVATE /O1 /W3)
else()
    target_compile_options(imgui PRIVATE -Os -w)
endif()

# ---------------------------------------------------------------------------
# Tempus engine — main executable
# ---------------------------------------------------------------------------
set(TEMPUS_SOURCES
    src/main.cpp
    src/gui.cpp
    src/logger.cpp
    src/app_paths.cpp
    src/database_manager.cpp
    src/state_recovery.cpp
    src/timer_engine.cpp
)

add_executable(Tempus WIN32 ${TEMPUS_SOURCES})
target_include_directories(Tempus PRIVATE include)
target_link_libraries(Tempus PRIVATE sqlite3 shell32 imgui d3d11 dxgi d3dcompiler uuid dwmapi)
tempus_apply_release_flags(Tempus)
tempus_apply_release_flags(sqlite3)
tempus_apply_release_flags(imgui)


# ---------------------------------------------------------------------------
# Tests (opt-in via -DTEMPUS_BUILD_TESTS=ON)
# ---------------------------------------------------------------------------
option(TEMPUS_BUILD_TESTS "Build the Tempus test suite" OFF)
if(TEMPUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
