/// @file order_book_imbalance_strategy.cpp
/// @brief Implementation of depth-weighted order book imbalance trading strategy.

#include "strategy/order_book_imbalance_strategy.h"
#include <algorithm>
#include <cmath>

namespace njt {

OrderBookImbalanceStrategy::OrderBookImbalanceStrategy(const ImbalanceStrategyConfig& config)
    : config_(config)
{}

double OrderBookImbalanceStrategy::calculateImbalance(const MarketUpdate& update) const {
    if (update.bid_levels == 0 && update.ask_levels == 0) return 0.0;

    double weighted_bid_qty = 0.0;
    double weighted_ask_qty = 0.0;

    size_t levels = std::min({static_cast<size_t>(update.bid_levels),
                              static_cast<size_t>(update.ask_levels),
                              config_.depth_levels,
                              MarketUpdate::kMaxDepth});

    if (levels == 0) return 0.0;

    for (size_t i = 0; i < levels; ++i) {
        double weight = std::exp(-config_.decay_lambda * static_cast<double>(i));
        weighted_bid_qty += weight * static_cast<double>(update.bids[i].quantity);
        weighted_ask_qty += weight * static_cast<double>(update.asks[i].quantity);
    }

    double total_qty = weighted_bid_qty + weighted_ask_qty;
    if (total_qty <= 0.0) return 0.0;

    return (weighted_bid_qty - weighted_ask_qty) / total_qty;
}

double OrderBookImbalanceStrategy::calculateMicroPrice(const MarketUpdate& update) const {
    if (update.bid_levels == 0 || update.ask_levels == 0) return 0.0;

    double numerator   = 0.0;
    double denominator = 0.0;

    size_t levels = std::min({static_cast<size_t>(update.bid_levels),
                              static_cast<size_t>(update.ask_levels),
                              config_.depth_levels,
                              MarketUpdate::kMaxDepth});

    if (levels == 0) return static_cast<double>(update.midPrice());

    for (size_t i = 0; i < levels; ++i) {
        double weight = std::exp(-config_.decay_lambda * static_cast<double>(i));
        double q_b = weight * static_cast<double>(update.bids[i].quantity);
        double q_a = weight * static_cast<double>(update.asks[i].quantity);
        double p_b = static_cast<double>(update.bids[i].price);
        double p_a = static_cast<double>(update.asks[i].price);

        numerator   += (q_b * p_a + q_a * p_b);
        denominator += (q_b + q_a);
    }

    if (denominator <= 0.0) return static_cast<double>(update.midPrice());
    return numerator / denominator;
}

std::optional<Signal> OrderBookImbalanceStrategy::onMarketUpdate(
    const MarketUpdate& update, Timestamp current_time)
{
    ++stats_.updates_processed;

    if (update.bid_levels == 0 || update.ask_levels == 0) {
        return std::nullopt;
    }

    Price best_bid = update.bestBid();
    Price best_ask = update.bestAsk();
    if (best_bid >= best_ask || best_bid <= 0) {
        return std::nullopt;
    }

    // Spread filter
    Price spread = best_ask - best_bid;
    if (spread > config_.max_spread_ticks) {
        ++stats_.skipped_spread_too_wide;
        return std::nullopt;
    }

    // Cooldown check
    if (last_signal_time_ > 0 &&
        (current_time - last_signal_time_) < config_.cooldown_ns)
    {
        ++stats_.skipped_cooldown;
        return std::nullopt;
    }

    double imbalance = calculateImbalance(update);

    // BUY condition
    if (imbalance >= config_.threshold_buy) {
        if (position_ + config_.order_quantity > config_.max_position) {
            ++stats_.skipped_position_limit;
            return std::nullopt;
        }

        Price target_px = (config_.order_type == OrderType::Market)
                              ? best_ask
                              : best_bid; // passive join best bid or cross at best_ask

        last_signal_time_ = current_time;
        ++stats_.signals_generated;
        ++stats_.buy_signals;

        return Signal{
            .timestamp       = current_time,
            .instrument      = update.instrument,
            .type           = SignalType::Buy,
            .conviction     = std::clamp(imbalance, 0.0, 1.0),
            .target_price    = target_px,
            .target_quantity = config_.order_quantity,
            .feature_value  = imbalance,
        };
    }

    // SELL condition
    if (imbalance <= config_.threshold_sell) {
        if (position_ - config_.order_quantity < -config_.max_position) {
            ++stats_.skipped_position_limit;
            return std::nullopt;
        }

        Price target_px = (config_.order_type == OrderType::Market)
                              ? best_bid
                              : best_ask; // passive join best ask or cross at best_bid

        last_signal_time_ = current_time;
        ++stats_.signals_generated;
        ++stats_.sell_signals;

        return Signal{
            .timestamp       = current_time,
            .instrument      = update.instrument,
            .type           = SignalType::Sell,
            .conviction     = std::clamp(-imbalance, 0.0, 1.0),
            .target_price    = target_px,
            .target_quantity = config_.order_quantity,
            .feature_value  = imbalance,
        };
    }

    return std::nullopt;
}

void OrderBookImbalanceStrategy::onExecutionReport(const ExecutionReport& report) {
    if (report.status == OrderStatus::Filled || report.status == OrderStatus::PartiallyFilled) {
        if (report.side == Side::Buy) {
            position_ += report.fill_quantity;
        } else {
            position_ -= report.fill_quantity;
        }
        ++stats_.fills_received;
        stats_.total_volume_traded += report.fill_quantity;
    }
}

void OrderBookImbalanceStrategy::reset() {
    position_         = 0;
    last_signal_time_ = 0;
    stats_            = Stats{};
}

} // namespace njt
