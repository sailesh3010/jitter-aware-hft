#pragma once
/// @file exchange_simulator.h
/// @brief Simulated exchange: price process, order book, market-data generation.
///
/// ## Price process
///   dP = θ(μ − P)dt + σ dW
///     • θ  = mean-reversion strength
///     • μ  = long-run mean (mean_reversion_target)
///     • σ  = per-step volatility (regime-dependent)
///     • dW = standard normal increment (seeded RNG)
///
/// ## Regime switching
/// With probability `regime_change_probability` per step the volatility
/// alternates between `low_volatility` and `high_volatility`.

#include "exchange/order_book.h"
#include "exchange/market_data.h"

#include <optional>
#include <random>
#include <vector>

namespace njt {

/// All tunable exchange parameters.
struct ExchangeConfig {
    // Price process
    Price  initial_mid_price        = 10000;    // $100.00
    double volatility               = 0.0002;   // per-step σ
    double mean_reversion_strength  = 0.01;     // θ
    Price  mean_reversion_target    = 10000;    // μ
    double trend_strength           = 0.0;      // directional drift

    // Book shape
    size_t   depth_levels  = 5;
    Quantity base_quantity  = 100;
    Price    tick_spread    = 1;                 // minimum half-spread in ticks

    // Market activity probabilities per step
    double trade_probability   = 0.3;
    double cancel_probability  = 0.1;

    // Volatility regime
    double regime_change_probability = 0.001;
    double low_volatility            = 0.0001;
    double high_volatility           = 0.001;

    // Timing
    uint64_t  seed              = 42;
    Timestamp event_interval_ns = 100'000;          // 100 μs between events
    Timestamp duration_ns       = 10'000'000'000ULL; // 10 s total
};

class ExchangeSimulator {
public:
    explicit ExchangeSimulator(const ExchangeConfig& config);

    /// Generate **all** market events for the configured duration.
    std::vector<MarketUpdate> generateMarketData();

    /// Advance one discrete time-step; returns the resulting MarketUpdate.
    std::optional<MarketUpdate> step();

    /// Submit an external order; returns execution reports (ack + fills).
    std::vector<ExecutionReport> submitOrder(Order order);

    /// Cancel an external order.
    std::optional<ExecutionReport> cancelOrder(OrderId id);

    // ── Accessors ──────────────────────────────────────────────────────
    [[nodiscard]] const OrderBook& orderBook()       const { return book_; }
    [[nodiscard]] Price            currentMidPrice()  const { return mid_price_; }
    [[nodiscard]] Timestamp        currentTime()      const { return current_time_; }
    [[nodiscard]] SequenceNumber   currentSequence()  const { return sequence_; }
    [[nodiscard]] bool             isFinished()       const { return current_time_ >= config_.duration_ns; }

private:
    void         initializeBook();
    void         updateMidPrice();
    void         refreshBook();
    void         checkRegimeChange();
    void         simulateTradeActivity();
    MarketUpdate createMarketUpdate(MarketUpdate::Type type);

    ExchangeConfig config_;
    OrderBook      book_;

    Price   mid_price_;
    double  current_volatility_;

    Timestamp      current_time_   = 0;
    SequenceNumber sequence_       = 0;
    OrderId        next_order_id_  = 1;

    // Deterministic RNG
    std::mt19937_64                       rng_;
    std::normal_distribution<double>      normal_dist_{0.0, 1.0};
    std::uniform_real_distribution<double> uniform_dist_{0.0, 1.0};
};

} // namespace njt
