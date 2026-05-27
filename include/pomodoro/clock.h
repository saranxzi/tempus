#pragma once
#include <chrono>
#include <ctime>

namespace tempus {

/// Abstract clock interface. Injected into TimerEngine to allow
/// deterministic testing via MockClock.
class IClock {
public:
    virtual ~IClock() = default;

    /// Monotonic time point for measuring elapsed durations.
    virtual std::chrono::steady_clock::time_point now() const = 0;

    /// Wall-clock time for database timestamps (Unix epoch).
    virtual std::time_t wall_time() const = 0;
};

/// Production clock that delegates to the real system clocks.
class SystemClock final : public IClock {
public:
    std::chrono::steady_clock::time_point now() const override {
        return std::chrono::steady_clock::now();
    }

    std::time_t wall_time() const override {
        return std::time(nullptr);
    }
};

} // namespace tempus
