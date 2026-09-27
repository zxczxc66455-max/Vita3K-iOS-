// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <chrono>
#include <cstdint>

namespace util {
// Zero removes only the host presentation cap, never Vita's vblank timing.
constexpr int normalize_fps_limit(int64_t value) {
    return value == 0 || value == 30 || value == 60 ? static_cast<int>(value) : 60;
}

class PresentationLimiter {
public:
    using Clock = std::chrono::steady_clock;

    bool due(int requested, Clock::time_point now = Clock::now()) {
        const int limit = normalize_fps_limit(requested);
        if (limit == 0) {
            initialized = false;
            return true;
        }
        const auto period = std::chrono::microseconds(1000000 / limit);
        if (!initialized || limit != previous_limit) {
            initialized = true;
            previous_limit = limit;
            next_present = now + period;
            return true;
        }
        if (now < next_present)
            return false;
        // Keep the phase across small scheduling delays. After a long stall,
        // start a new interval instead of emitting a burst of catch-up frames.
        next_present += period;
        if (next_present <= now)
            next_present = now + period;
        return true;
    }

private:
    bool initialized = false;
    int previous_limit = 60;
    Clock::time_point next_present{};
};
} // namespace util
