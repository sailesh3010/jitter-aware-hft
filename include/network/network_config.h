#pragma once
/// @file network_config.h
/// @brief Configuration types for the network simulation layer.

#include "common/types.h"

namespace njt {

enum class LatencyDistribution : uint8_t {
    Constant,   ///< Fixed delay = base_latency_ns
    Uniform,    ///< U(min_latency_ns, max_latency_ns)
    Normal,     ///< N(base_latency_ns, jitter_stddev_ns), truncated to [min, max]
    LogNormal   ///< LogN(μ, σ) fitted to base_latency_ns, truncated
};

enum class TransportMode : uint8_t {
    UdpLike,    ///< Unreliable, unordered (simulated)
    TcpLike,    ///< Reliable, ordered with retransmission overhead (simulated)
    RealTcp,    ///< Actual TCP sockets (Phase 7)
    RealUdp     ///< Actual UDP sockets (Phase 7)
};

struct NetworkConfig {
    TransportMode       transport    = TransportMode::UdpLike;
    LatencyDistribution distribution = LatencyDistribution::LogNormal;

    Timestamp base_latency_ns   = 100'000;        // 100 μs
    Timestamp jitter_stddev_ns  =  20'000;        //  20 μs σ
    Timestamp min_latency_ns    =  10'000;        //  10 μs floor
    Timestamp max_latency_ns    = 10'000'000;     //  10 ms ceiling

    double packet_loss_rate       = 0.001;        // 0.1 %
    double duplication_rate       = 0.0001;       // 0.01 %
    double reorder_rate           = 0.001;        // 0.1 %

    double   burst_loss_probability  = 0.0001;
    uint32_t burst_loss_max_length   = 5;

    // TCP-like retransmission modelling
    Timestamp retransmit_timeout_ns = 1'000'000;  // 1 ms
    uint32_t  max_retransmits       = 3;

    uint64_t seed = 123;
};

} // namespace njt
