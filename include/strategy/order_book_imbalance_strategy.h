#pragma once
/// @file order_book_imbalance_strategy.h
/// @brief Multi-level Order Book Imbalance (OBI) and micro-price strategy.
///
/// ## Quantitative Model
/// 1. Depth-weighted Order Book Imbalance:
///    I = sum_{i=0}^{K-1} w_i * (Q_{b,i} - Q_{a,i}) / sum_{i=0}^{K-1} w_i * (Q_{b,i} + Q_{a,i})
///    where w_i = exp(-lambda * i) represents the exponential decay of information with book depth.
///
/// 2. Depth-weighted Micro-Price:
///    P_{micro} = sum_{i=0}^{K-1} w_i * (Q_{b,i} * P_{a,i} + Q_{a,i} * P_{b,i}) / sum_{i=0}^{K-1} w_i * (Q_{b,i} + Q_{a,i})
///
/// 3. Signal Logic:
///    - If I > threshold_buy and spread <= max_spread and position < max_position: BUY
///    - If I < threshold_sell and spread <= max_spread and position > -max_position: SELL
///    - Enforces cooldown between orders to prevent high-frequency churn and self-cross.

#include "strategy/strategy.h"
#include <cmath>
#include <array>

namespace njt {

struct ImbalanceStrategyConfig {
    double    threshold_buy     = 0.30;       // I > +0.30 triggers BUY
    double    threshold_sell    = -0.30;      // I < -0.30 triggers SELL
    double    decay_lambda      = 0.5;        // depth weighting factor
    size_t    depth_levels      = 5;          // depth levels to evaluate (up to 5)
    Quantity  order_quantity    = 10;         // fixed order size
    Quantity  max_position      = 100;        // maximum absolute inventory limit
    Timestamp cooldown_ns       = 500'000;    // 500 μs minimum time between signals
    Price     max_spread_ticks  = 3;          // maximum spread allowed for entry
    OrderType order_type        = OrderType::Limit; // Limit or Market execution
};

class OrderBookImbalanceStrategy : public IStrategy {
public:
    explicit OrderBookImbalanceStrategy(const ImbalanceStrategyConfig& config = {});

    std::optional<Signal> onMarketUpdate(const MarketUpdate& update, Timestamp current_time) override;
    void onExecutionReport(const ExecutionReport& report) override;

    [[nodiscard]] Quantity position() const noexcept override { return position_; }
    void reset() override;

    /// Calculate order book imbalance [-1.0, 1.0] from a MarketUpdate snapshot.
    [[nodiscard]] double calculateImbalance(const MarketUpdate& update) const;

    /// Calculate depth-weighted micro-price in ticks.
    [[nodiscard]] double calculateMicroPrice(const MarketUpdate& update) const;

    struct Stats {
        uint64_t updates_processed        = 0;
        uint64_t signals_generated        = 0;
        uint64_t buy_signals              = 0;
        uint64_t sell_signals             = 0;
        uint64_t skipped_cooldown         = 0;
        uint64_t skipped_position_limit   = 0;
        uint64_t skipped_spread_too_wide  = 0;
        uint64_t fills_received           = 0;
        Quantity total_volume_traded      = 0;
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    [[nodiscard]] const ImbalanceStrategyConfig& config() const noexcept { return config_; }

private:
    ImbalanceStrategyConfig config_;
    Quantity position_          = 0;
    Timestamp last_signal_time_ = 0;
    Stats stats_;
};

} // namespace njt
