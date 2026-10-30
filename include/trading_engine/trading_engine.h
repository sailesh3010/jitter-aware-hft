#pragma once
/// @file trading_engine.h
/// @brief Central coordinator for exchange, network, feed handler, strategy,
///        execution simulator, and performance analytics.

#include "analytics/performance_analyzer.h"
#include "concurrency/affinity.h"
#include "concurrency/mutex_queue.h"
#include "concurrency/spsc_queue.h"
#include "exchange/exchange_simulator.h"
#include "execution/execution_simulator.h"
#include "market_data/feed_handler.h"
#include "network/network_simulator.h"
#include "strategy/order_book_imbalance_strategy.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace njt {

struct SimulatorConfig {
    ExchangeConfig          exchange;
    NetworkConfig           network;
    FeedHandler::Config     feed_handler;
    ImbalanceStrategyConfig strategy;
    ExecutionConfig         execution;

    // Simulation run settings
    bool     use_concurrent_pipeline = false; // true = multi-threaded, false = discrete-event
    bool     use_lock_free_queue     = true;  // true = SPSC, false = MutexQueue
    uint32_t md_core_id              = 0;
    uint32_t strategy_core_id        = 1;
    uint32_t exec_core_id            = 2;
    uint64_t sample_interval_ns      = 1'000'000; // 1 ms PnL sampling
};

class TradingEngine {
public:
    explicit TradingEngine(const SimulatorConfig& config);
    ~TradingEngine();

    /// Run full simulation to completion.
    PerformanceMetrics run();

    /// Accessors
    [[nodiscard]] const PerformanceAnalyzer& analytics() const noexcept { return analytics_; }
    [[nodiscard]] const ExecutionSimulator& execution() const noexcept { return execution_; }
    [[nodiscard]] const FeedHandler& feedHandler() const noexcept { return feed_handler_; }
    [[nodiscard]] const NetworkSimulator& network() const noexcept { return network_; }
    [[nodiscard]] const OrderBookImbalanceStrategy& strategy() const noexcept { return strategy_; }

private:
    PerformanceMetrics runDiscreteEvent();
    PerformanceMetrics runConcurrentPipeline();

    SimulatorConfig            config_;
    ExchangeSimulator          exchange_;
    NetworkSimulator           network_;
    FeedHandler                feed_handler_;
    OrderBookImbalanceStrategy strategy_;
    ExecutionSimulator         execution_;
    PerformanceAnalyzer        analytics_;

    std::atomic<bool> stop_flag_{false};
    OrderId           next_client_order_id_ = 100'000;
};

} // namespace njt
