/// @file exchange_simulator.cpp
/// @brief Exchange simulator: OU price process, book refresh, trade generation.

#include "exchange/exchange_simulator.h"
#include <algorithm>
#include <cmath>

namespace njt {

ExchangeSimulator::ExchangeSimulator(const ExchangeConfig& config)
    : config_(config)
    , mid_price_(config.initial_mid_price)
    , current_volatility_(config.volatility)
    , rng_(config.seed)
{
    initializeBook();
}

// ────────────────────────────────────────────────────────────────────────────
// Public API
// ────────────────────────────────────────────────────────────────────────────

std::vector<MarketUpdate> ExchangeSimulator::generateMarketData() {
    std::vector<MarketUpdate> events;
    events.reserve(static_cast<size_t>(config_.duration_ns / config_.event_interval_ns) + 1);

    while (!isFinished()) {
        auto update = step();
        if (update) events.push_back(std::move(*update));
    }
    return events;
}

std::optional<MarketUpdate> ExchangeSimulator::step() {
    if (isFinished()) return std::nullopt;

    current_time_ += config_.event_interval_ns;

    // 1. Evolve the mid-price (OU process)
    updateMidPrice();

    // 2. Possibly switch volatility regime
    checkRegimeChange();

    // 3. Rebuild resting liquidity around the new mid-price
    refreshBook();

    // 4. Simulate background trade activity
    simulateTradeActivity();

    // 5. Publish a market-data update
    return createMarketUpdate(MarketUpdate::Type::BookUpdate);
}

std::vector<ExecutionReport> ExchangeSimulator::submitOrder(Order order) {
    if (order.id == 0) {
        order.id = next_order_id_++;
    }
    auto trades = book_.addOrder(std::move(order), current_time_);

    std::vector<ExecutionReport> reports;
    for (const auto& t : trades) {
        reports.push_back(ExecutionReport{
            .order_id           = t.aggressor_order_id,
            .side               = t.aggressor_side,
            .status             = OrderStatus::Filled,
            .fill_price         = t.price,
            .fill_quantity      = t.quantity,
            .remaining_quantity = 0,
            .exchange_timestamp = current_time_,
        });
    }
    return reports;
}

std::optional<ExecutionReport> ExchangeSimulator::cancelOrder(OrderId id) {
    if (book_.cancelOrder(id)) {
        return ExecutionReport{
            .order_id           = id,
            .status             = OrderStatus::Cancelled,
            .exchange_timestamp = current_time_,
        };
    }
    return std::nullopt;
}

// ────────────────────────────────────────────────────────────────────────────
// Private helpers
// ────────────────────────────────────────────────────────────────────────────

void ExchangeSimulator::initializeBook() {
    book_.clear();
    refreshBook();
}

void ExchangeSimulator::updateMidPrice() {
    // Ornstein-Uhlenbeck: dP = θ(μ − P)dt + σ dW + drift
    double mean_rev = config_.mean_reversion_strength *
                      static_cast<double>(config_.mean_reversion_target - mid_price_);
    double noise    = current_volatility_ * normal_dist_(rng_) *
                      static_cast<double>(mid_price_);
    double drift    = config_.trend_strength * static_cast<double>(mid_price_);

    auto delta = static_cast<Price>(mean_rev + noise + drift);
    mid_price_ += delta;

    // Clamp to positive
    if (mid_price_ < 1) mid_price_ = 1;
}

void ExchangeSimulator::checkRegimeChange() {
    if (uniform_dist_(rng_) < config_.regime_change_probability) {
        current_volatility_ =
            (current_volatility_ == config_.low_volatility)
                ? config_.high_volatility
                : config_.low_volatility;
    }
}

void ExchangeSimulator::refreshBook() {
    // Clear existing simulated liquidity and rebuild around mid-price.
    // In a more sophisticated model we would only cancel/replace changed levels.
    book_.clear();

    std::uniform_int_distribution<Quantity> qty_jitter(
        config_.base_quantity / 2, config_.base_quantity * 2);

    for (size_t i = 0; i < config_.depth_levels; ++i) {
        Price offset = config_.tick_spread + static_cast<Price>(i);

        // Bid side
        Order bid{};
        bid.id       = next_order_id_++;
        bid.side     = Side::Buy;
        bid.type     = OrderType::Limit;
        bid.price    = mid_price_ - offset;
        bid.quantity = qty_jitter(rng_);
        bid.submit_time = current_time_;
        book_.addOrder(std::move(bid), current_time_);

        // Ask side
        Order ask{};
        ask.id       = next_order_id_++;
        ask.side     = Side::Sell;
        ask.type     = OrderType::Limit;
        ask.price    = mid_price_ + offset;
        ask.quantity = qty_jitter(rng_);
        ask.submit_time = current_time_;
        book_.addOrder(std::move(ask), current_time_);
    }
}

void ExchangeSimulator::simulateTradeActivity() {
    if (uniform_dist_(rng_) >= config_.trade_probability) return;

    // Simulate an aggressive market order from a random background participant
    Side aggressor_side = (uniform_dist_(rng_) < 0.5) ? Side::Buy : Side::Sell;

    // Size: 1× to 3× base_quantity
    std::uniform_int_distribution<Quantity> size_dist(1, config_.base_quantity * 3);

    Order mkt{};
    mkt.id          = next_order_id_++;
    mkt.side        = aggressor_side;
    mkt.type        = OrderType::Market;
    mkt.quantity    = size_dist(rng_);
    mkt.submit_time = current_time_;

    book_.addOrder(std::move(mkt), current_time_);
}

MarketUpdate ExchangeSimulator::createMarketUpdate(MarketUpdate::Type type) {
    ++sequence_;

    MarketUpdate update{};
    update.sequence           = sequence_;
    update.exchange_timestamp = current_time_;
    update.instrument         = kDefaultInstrument;
    update.type               = type;

    // Fill depth snapshot
    auto bid_depth = book_.getBidDepth(MarketUpdate::kMaxDepth);
    auto ask_depth = book_.getAskDepth(MarketUpdate::kMaxDepth);

    for (size_t i = 0; i < bid_depth.size(); ++i) {
        update.bids[i] = bid_depth[i];
    }
    update.bid_levels = static_cast<uint8_t>(bid_depth.size());

    for (size_t i = 0; i < ask_depth.size(); ++i) {
        update.asks[i] = ask_depth[i];
    }
    update.ask_levels = static_cast<uint8_t>(ask_depth.size());

    return update;
}

} // namespace njt
