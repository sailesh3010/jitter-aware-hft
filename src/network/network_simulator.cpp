/// @file network_simulator.cpp
/// @brief Discrete-event network simulator.
///
/// ## UDP-like mode
/// Each message gets an independent latency sample.  Messages may be dropped,
/// duplicated, or delivered out of order depending on LatencyModel decisions.
///
/// ## TCP-like mode
/// Dropped messages are retransmitted (at retransmit_timeout_ns cost).
/// Messages are reordered in the tcp_reorder_buf_ and delivered in sequence-
/// number order — mimicking TCP's in-order delivery guarantee with HOL
/// blocking.

#include "network/network_simulator.h"
#include <algorithm>

namespace njt {

NetworkSimulator::NetworkSimulator(const NetworkConfig& config)
    : config_(config)
    , latency_model_(config)
{}

void NetworkSimulator::send(NetworkMessage message) {
    ++stats_.total_sent;
    message.send_timestamp = message.market_update.exchange_timestamp;

    // ── Impairment decisions ───────────────────────────────────────────
    bool dropped = latency_model_.shouldDrop();

    if (dropped) {
        ++stats_.total_dropped;

        if (config_.transport == TransportMode::TcpLike) {
            // TCP: retransmit after timeout (adds extra latency)
            for (uint32_t r = 0; r < config_.max_retransmits; ++r) {
                Timestamp retx_latency =
                    latency_model_.sampleLatency() +
                    config_.retransmit_timeout_ns * (r + 1);

                // Assume retransmission eventually succeeds
                if (!latency_model_.shouldDrop()) {
                    message.delivery_timestamp =
                        message.send_timestamp + retx_latency;
                    message.is_retransmit    = true;
                    message.retransmit_count = r + 1;
                    ++stats_.total_retransmitted;
                    delivery_queue_.push(message);
                    return;
                }
            }
            // All retransmits failed — truly lost even in TCP mode
            return;
        }
        // UDP: just drop
        return;
    }

    // ── Latency ────────────────────────────────────────────────────────
    Timestamp latency = latency_model_.sampleLatency();

    // Reorder: add extra jitter
    if (latency_model_.shouldReorder()) {
        ++stats_.total_reordered;
        latency += latency_model_.sampleLatency(); // additional random delay
    }

    message.delivery_timestamp = message.send_timestamp + latency;
    delivery_queue_.push(message);

    // Record latency sample
    stats_.latency_samples.push_back(latency);
    stats_.sum_latency_ns += static_cast<double>(latency);
    stats_.max_latency_ns  = std::max(stats_.max_latency_ns, latency);
    stats_.min_latency_ns  = std::min(stats_.min_latency_ns, latency);

    // ── Duplication ────────────────────────────────────────────────────
    if (latency_model_.shouldDuplicate()) {
        ++stats_.total_duplicated;
        NetworkMessage dup = message;
        dup.is_duplicate         = true;
        dup.delivery_timestamp  += latency_model_.sampleLatency(); // slightly later
        delivery_queue_.push(dup);
    }
}

std::vector<NetworkMessage> NetworkSimulator::receive(Timestamp current_time) {
    std::vector<NetworkMessage> output;

    while (!delivery_queue_.empty() &&
           delivery_queue_.top().delivery_timestamp <= current_time)
    {
        output.push_back(delivery_queue_.top());
        delivery_queue_.pop();
    }

    if (config_.transport == TransportMode::TcpLike && !output.empty()) {
        handleTcpReorder(output);
    }

    stats_.total_delivered += output.size();
    return output;
}

void NetworkSimulator::resetStats() {
    stats_ = Stats{};
}

// ────────────────────────────────────────────────────────────────────────────
// TCP-like in-order delivery with head-of-line blocking
// ────────────────────────────────────────────────────────────────────────────

void NetworkSimulator::handleTcpReorder(std::vector<NetworkMessage>& output) {
    // Merge with buffered out-of-order messages
    for (auto& msg : output) {
        tcp_reorder_buf_.push_back(std::move(msg));
    }

    // Sort by sequence number
    std::sort(tcp_reorder_buf_.begin(), tcp_reorder_buf_.end(),
              [](const NetworkMessage& a, const NetworkMessage& b) {
                  return a.sequence < b.sequence;
              });

    output.clear();

    // Deliver in order
    auto it = tcp_reorder_buf_.begin();
    while (it != tcp_reorder_buf_.end()) {
        if (it->sequence == last_delivered_seq_ + 1) {
            last_delivered_seq_ = it->sequence;
            output.push_back(std::move(*it));
            it = tcp_reorder_buf_.erase(it);
        } else {
            break; // head-of-line block
        }
    }
}

} // namespace njt
