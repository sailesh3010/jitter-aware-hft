#pragma once
/// @file timestamp.h
/// @brief High-resolution clock utilities and RAII latency measurement.

#include "common/types.h"
#include <chrono>

namespace njt {

/// Thin wrapper around std::chrono providing nanosecond timestamps and
/// unit-conversion helpers.  All simulation times are in nanoseconds.
struct SimClock {
    static Timestamp now() noexcept {
        return static_cast<Timestamp>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

    static constexpr Timestamp fromMicros(uint64_t us)  noexcept { return us * 1'000ULL; }
    static constexpr Timestamp fromMillis(uint64_t ms)  noexcept { return ms * 1'000'000ULL; }
    static constexpr Timestamp fromSeconds(uint64_t s)  noexcept { return s  * 1'000'000'000ULL; }

    static constexpr double toMicros (Timestamp ns) noexcept { return static_cast<double>(ns) / 1'000.0; }
    static constexpr double toMillis (Timestamp ns) noexcept { return static_cast<double>(ns) / 1'000'000.0; }
    static constexpr double toSeconds(Timestamp ns) noexcept { return static_cast<double>(ns) / 1'000'000'000.0; }
};

/// RAII helper: records a start time at construction and returns elapsed ns.
class LatencyMeasure {
public:
    LatencyMeasure() noexcept : start_(SimClock::now()) {}

    [[nodiscard]] Timestamp elapsed() const noexcept { return SimClock::now() - start_; }
    void reset() noexcept { start_ = SimClock::now(); }

private:
    Timestamp start_;
};

} // namespace njt
