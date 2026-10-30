#pragma once
/// @file feed_handler.h
/// @brief Reconstructs a consistent order-book view from a potentially
///        lossy and reordered market-data stream.
///
/// ## Gap detection
/// The handler tracks the next expected sequence number.  Any gap triggers
/// buffering of out-of-order messages and, when the gap exceeds a threshold,
/// a snapshot-recovery request.
///
/// ## Stale-message filtering
/// Messages whose exchange_timestamp is older than the last applied update
/// are discarded.

#include "exchange/market_data.h"

#include <map>
#include <set>
#include <cstdint>

namespace njt {

class FeedHandler {
public:
    struct Config {
        size_t    max_gap_before_recovery = 10;       ///< trigger snapshot recovery
        Timestamp stale_threshold_ns       = 1'000'000; ///< 1 ms
    };

    FeedHandler() : FeedHandler(Config{}) {}
    explicit FeedHandler(const Config& config);

    /// Process an incoming market-data message.
    /// @return true if the update was applied (not stale / duplicate).
    bool processMessage(const MarketUpdate& update, Timestamp receive_time);

    /// Current reconstructed book state.
    [[nodiscard]] const MarketUpdate& currentState() const noexcept { return current_state_; }

    /// True if a gap has grown large enough to warrant snapshot recovery.
    [[nodiscard]] bool needsRecovery() const noexcept { return needs_recovery_; }

    /// Set of sequence numbers we know are missing.
    [[nodiscard]] const std::set<SequenceNumber>& missingSequences() const noexcept {
        return missing_sequences_;
    }

    /// Reset state from a full snapshot.
    void applySnapshot(const MarketUpdate& snapshot);

    struct Stats {
        uint64_t  messages_processed       = 0;
        uint64_t  messages_applied         = 0;
        uint64_t  duplicates_detected      = 0;
        uint64_t  gaps_detected            = 0;
        uint64_t  reorders_handled         = 0;
        uint64_t  recoveries_triggered     = 0;
        uint64_t  stale_messages           = 0;
        Timestamp total_processing_latency = 0;
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    void processReorderBuffer();
    void applyUpdate(const MarketUpdate& update);

    Config         config_;
    MarketUpdate   current_state_{};
    SequenceNumber expected_sequence_  = 1;
    SequenceNumber highest_seen_       = 0;
    bool           needs_recovery_     = false;

    std::set<SequenceNumber>              missing_sequences_;
    std::map<SequenceNumber, MarketUpdate> reorder_buffer_;
    Stats stats_;
};

} // namespace njt
