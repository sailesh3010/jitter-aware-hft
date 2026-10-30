#pragma once
/// @file latency_model.h
/// @brief Samples latency, packet loss, duplication, and reordering events.

#include "network/network_config.h"
#include <random>

namespace njt {

class LatencyModel {
public:
    explicit LatencyModel(const NetworkConfig& config);

    /// Sample a one-way latency in nanoseconds.
    [[nodiscard]] Timestamp sampleLatency();

    /// Should this packet be dropped?
    [[nodiscard]] bool shouldDrop();

    /// Should this packet be duplicated?
    [[nodiscard]] bool shouldDuplicate();

    /// Should this packet be reordered (given extra random delay)?
    [[nodiscard]] bool shouldReorder();

    /// Start or continue a burst-loss event.  Returns number of consecutive
    /// packets to drop (0 = no burst).
    [[nodiscard]] uint32_t burstLossLength();

    void reset(uint64_t seed);

private:
    NetworkConfig config_;
    std::mt19937_64 rng_;

    std::uniform_real_distribution<double>  uniform_{0.0, 1.0};
    std::normal_distribution<double>        normal_;
    std::lognormal_distribution<double>     lognormal_;

    bool     in_burst_     = false;
    uint32_t burst_remaining_ = 0;
};

} // namespace njt
