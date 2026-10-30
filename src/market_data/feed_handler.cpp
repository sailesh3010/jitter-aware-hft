/// @file feed_handler.cpp
/// @brief Market-data feed handler with gap detection and reorder buffering.

#include "market_data/feed_handler.h"

namespace njt {

FeedHandler::FeedHandler(const Config& config) : config_(config) {}

bool FeedHandler::processMessage(const MarketUpdate& update, Timestamp receive_time) {
    ++stats_.messages_processed;

    const SequenceNumber seq = update.sequence;

    // ── Duplicate detection ────────────────────────────────────────────
    if (seq < expected_sequence_ && seq != 0) {
        ++stats_.duplicates_detected;
        return false;
    }

    // ── Stale check ────────────────────────────────────────────────────
    if (update.exchange_timestamp + config_.stale_threshold_ns < current_state_.exchange_timestamp) {
        ++stats_.stale_messages;
        return false;
    }

    // ── In-order delivery ──────────────────────────────────────────────
    if (seq == expected_sequence_) {
        applyUpdate(update);
        ++expected_sequence_;

        // Drain anything buffered that is now contiguous
        processReorderBuffer();

        stats_.total_processing_latency += (receive_time - update.exchange_timestamp);
        return true;
    }

    // ── Gap detected → buffer ──────────────────────────────────────────
    ++stats_.gaps_detected;
    reorder_buffer_[seq] = update;

    // Track which sequence numbers are missing
    if (seq > expected_sequence_) {
        for (SequenceNumber s = expected_sequence_; s < seq; ++s) {
            if (reorder_buffer_.find(s) == reorder_buffer_.end()) {
                missing_sequences_.insert(s);
            }
        }
    }

    if (seq > highest_seen_) highest_seen_ = seq;

    // Trigger recovery if gap is too wide
    if (missing_sequences_.size() > config_.max_gap_before_recovery) {
        needs_recovery_ = true;
        ++stats_.recoveries_triggered;
    }

    return false;
}

void FeedHandler::applySnapshot(const MarketUpdate& snapshot) {
    current_state_     = snapshot;
    expected_sequence_ = snapshot.sequence + 1;
    highest_seen_      = snapshot.sequence;
    needs_recovery_    = false;
    missing_sequences_.clear();
    reorder_buffer_.clear();
}

// ────────────────────────────────────────────────────────────────────────────
// Private
// ────────────────────────────────────────────────────────────────────────────

void FeedHandler::processReorderBuffer() {
    while (true) {
        auto it = reorder_buffer_.find(expected_sequence_);
        if (it == reorder_buffer_.end()) break;

        applyUpdate(it->second);
        missing_sequences_.erase(expected_sequence_);
        reorder_buffer_.erase(it);
        ++expected_sequence_;
        ++stats_.reorders_handled;
    }

    // If we drained all gaps, clear recovery flag
    if (missing_sequences_.empty()) {
        needs_recovery_ = false;
    }
}

void FeedHandler::applyUpdate(const MarketUpdate& update) {
    current_state_ = update;
    ++stats_.messages_applied;
}

} // namespace njt
