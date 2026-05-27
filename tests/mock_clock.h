#pragma once
#include "pomodoro/clock.h"
#include <chrono>

namespace tempus {

/// Deterministic mock clock for unit testing.
/// Allows instant simulation of elapsed time without real waiting.
class MockClock final : public IClock {
public:
    MockClock()
        : now_(std::chrono::steady_clock::time_point(
              std::chrono::steady_clock::duration(0)))
        , wall_(1700000000)  // Arbitrary fixed epoch for reproducibility.
    {}

    std::chrono::steady_clock::time_point now() const override {
        return now_;
    }

    std::time_t wall_time() const override {
        return wall_;
    }

    /// Advance the monotonic clock by the given number of seconds.
    void advance(int seconds) {
        now_  += std::chrono::seconds(seconds);
        wall_ += seconds;
    }

    /// Advance the monotonic clock by an exact duration.
    void advance(std::chrono::seconds dur) {
        now_  += dur;
        wall_ += static_cast<std::time_t>(dur.count());
    }

private:
    std::chrono::steady_clock::time_point now_;
    std::time_t wall_;
};

} // namespace tempus
