#pragma once
/// @file network_simulator.h
/// @brief Discrete-event network simulator (priority-queue based).
///
/// Messages are enqueued with a computed delivery timestamp.  The consumer
/// calls `receive(t)` to drain every message whose delivery time ≤ t.
///
/// Supports two logical transport modes:
///   • **UdpLike** – messages may be lost, duplicated, or reordered.
///   • **TcpLike** – gaps are detected and retransmitted, adding overhead.

#include "network/network_config.h"
#include "network/network_message.h"
#include "network/latency_model.h"

#include <queue>
#include <vector>
#include <functional>

namespace njt {

class NetworkSimulator {
public:
    explicit NetworkSimulator(const NetworkConfig& config);

    /// Enqueue a message into the simulated network.
    void send(NetworkMessage message);

    /// Return all messages deliverable at or before `current_time`.
    std::vector<NetworkMessage> receive(Timestamp current_time);

    /// Cumulative statistics for the simulated network path.
    struct Stats {
        uint64_t total_sent         = 0;
        uint64_t total_delivered    = 0;
        uint64_t total_dropped      = 0;
        uint64_t total_duplicated   = 0;
        uint64_t total_reordered    = 0;
        uint64_t total_retransmitted = 0;
        double   sum_latency_ns     = 0;
        Timestamp max_latency_ns    = 0;
        Timestamp min_latency_ns    = UINT64_MAX;
        std::vector<Timestamp> latency_samples;
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    void resetStats();

private:
    struct DeliveryOrder {
        bool operator()(const NetworkMessage& a, const NetworkMessage& b) const noexcept {
            return a.delivery_timestamp > b.delivery_timestamp; // min-heap
        }
    };

    void handleTcpReorder(std::vector<NetworkMessage>& output);

    NetworkConfig  config_;
    LatencyModel   latency_model_;
    std::priority_queue<NetworkMessage,
                        std::vector<NetworkMessage>,
                        DeliveryOrder> delivery_queue_;
    Stats stats_;

    // TCP-like state
    SequenceNumber last_delivered_seq_ = 0;
    std::vector<NetworkMessage> tcp_reorder_buf_;
};

} // namespace njt
