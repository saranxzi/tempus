# Fetch Catch2 v3 at configure time (no vendored test code).
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# ── Test sources ──────────────────────────────────────────────────────
set(TEST_SOURCES
    test_timer_engine.cpp
    test_database_manager.cpp
    test_state_recovery.cpp
)

# Engine sources (recompiled for tests — avoids linking WinMain).
set(ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/logger.cpp
    ${CMAKE_SOURCE_DIR}/src/app_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/database_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/state_recovery.cpp
    ${CMAKE_SOURCE_DIR}/src/timer_engine.cpp
)

add_executable(tempus_tests ${TEST_SOURCES} ${ENGINE_SOURCES})
target_include_directories(tempus_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(tempus_tests PRIVATE Catch2::Catch2WithMain sqlite3 shell32)

# Register with CTest.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
include(CTest)
catch_discover_tests(tempus_tests)

