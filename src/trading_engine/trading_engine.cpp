/// @file trading_engine.cpp
/// @brief Central coordinator implementing discrete-event simulation and concurrent pipeline.

#include "trading_engine/trading_engine.h"
#include "common/timestamp.h"
#include <chrono>
#include <iostream>

namespace njt {

TradingEngine::TradingEngine(const SimulatorConfig& config)
    : config_(config)
    , exchange_(config.exchange)
    , network_(config.network)
    , feed_handler_(config.feed_handler)
    , strategy_(config.strategy)
    , execution_(config.execution)
{}

TradingEngine::~TradingEngine() {
    stop_flag_.store(true, std::memory_order_release);
}

PerformanceMetrics TradingEngine::run() {
    if (config_.use_concurrent_pipeline) {
        return runConcurrentPipeline();
    } else {
        return runDiscreteEvent();
    }
}

PerformanceMetrics TradingEngine::runDiscreteEvent() {
    Timestamp last_sample_time = 0;
    size_t recorded_fills = 0;

    while (!exchange_.isFinished()) {
        // 1. Advance exchange
        auto opt_update = exchange_.step();
        if (opt_update) {
            NetworkMessage msg{
                .sequence      = opt_update->sequence,
                .send_timestamp= opt_update->exchange_timestamp,
                .type          = MessageType::MarketData,
                .market_update = *opt_update,
            };
            network_.send(std::move(msg));
        }

        Timestamp current_time = exchange_.currentTime();

        // 2. Deliver network messages
        auto delivered = network_.receive(current_time);
        for (const auto& msg : delivered) {
            bool applied = feed_handler_.processMessage(msg.market_update, current_time);

            // Handle recovery if gap grew too wide
            if (feed_handler_.needsRecovery()) {
                MarketUpdate snapshot{};
                snapshot.sequence           = exchange_.currentSequence();
                snapshot.exchange_timestamp = current_time;
                auto bids = exchange_.orderBook().getBidDepth(MarketUpdate::kMaxDepth);
                auto asks = exchange_.orderBook().getAskDepth(MarketUpdate::kMaxDepth);
                for (size_t i = 0; i < bids.size(); ++i) snapshot.bids[i] = bids[i];
                for (size_t i = 0; i < asks.size(); ++i) snapshot.asks[i] = asks[i];
                snapshot.bid_levels = static_cast<uint8_t>(bids.size());
                snapshot.ask_levels = static_cast<uint8_t>(asks.size());
                feed_handler_.applySnapshot(snapshot);
                applied = true;
            }

            if (applied) {
                const auto& state = feed_handler_.currentState();
                auto opt_sig = strategy_.onMarketUpdate(state, current_time);

                if (opt_sig) {
                    Order order{
                        .id          = next_client_order_id_++,
                        .instrument  = opt_sig->instrument,
                        .side        = (opt_sig->type == SignalType::Buy) ? Side::Buy : Side::Sell,
                        .type        = config_.strategy.order_type,
                        .price       = opt_sig->target_price,
                        .quantity    = opt_sig->target_quantity,
                        .submit_time = current_time,
                    };
                    execution_.submitOrder(order, current_time, state.midPrice());
                }
            }
        }

        // 3. Process execution orders and fills
        execution_.process(current_time, exchange_);

        // 4. Client receives execution reports
        auto reports = execution_.receiveReports(current_time);
        for (const auto& rep : reports) {
            strategy_.onExecutionReport(rep);
        }

        // 5. Record fills into analytics
        const auto& all_fills = execution_.fills();
        while (recorded_fills < all_fills.size()) {
            analytics_.recordFill(all_fills[recorded_fills], exchange_.currentMidPrice());
            ++recorded_fills;
        }

        // 6. Periodic PnL sample
        if (current_time - last_sample_time >= config_.sample_interval_ns) {
            analytics_.samplePnL(current_time, exchange_.currentMidPrice());
            last_sample_time = current_time;
        }
    }

    // Drain remaining in-flight orders and markouts
    Timestamp drain_end = exchange_.currentTime() + 100'000'000ULL; // +100 ms
    for (Timestamp dt = exchange_.currentTime(); dt <= drain_end; dt += 100'000) {
        execution_.process(dt, exchange_);
        auto reports = execution_.receiveReports(dt);
        for (const auto& rep : reports) {
            strategy_.onExecutionReport(rep);
        }
    }

    const auto& final_fills = execution_.fills();
    while (recorded_fills < final_fills.size()) {
        analytics_.recordFill(final_fills[recorded_fills], exchange_.currentMidPrice());
        ++recorded_fills;
    }

    analytics_.samplePnL(drain_end, exchange_.currentMidPrice());

    return analytics_.computeMetrics(execution_.computeSummaryStats());
}

PerformanceMetrics TradingEngine::runConcurrentPipeline() {
    stop_flag_.store(false, std::memory_order_release);

    // Lock-free ring buffers (Capacity = 4096)
    constexpr size_t kQueueCap = 4096;
    auto md_queue_spsc    = std::make_unique<SPSCQueue<MarketUpdate, kQueueCap>>();
    auto order_queue_spsc = std::make_unique<SPSCQueue<Order, kQueueCap>>();

    // Mutex fallback queues
    auto md_queue_mutex    = std::make_unique<MutexQueue<MarketUpdate>>(kQueueCap);
    auto order_queue_mutex = std::make_unique<MutexQueue<Order>>(kQueueCap);

    std::atomic<bool> exchange_done{false};

    // ── Thread 1: Market Data & Exchange Thread ──
    std::thread md_thread([&]() {
        pinCurrentThreadToCore(config_.md_core_id);

        while (!exchange_.isFinished() && !stop_flag_.load(std::memory_order_relaxed)) {
            auto opt_update = exchange_.step();
            if (opt_update) {
                NetworkMessage msg{
                    .sequence       = opt_update->sequence,
                    .send_timestamp = opt_update->exchange_timestamp,
                    .type           = MessageType::MarketData,
                    .market_update  = *opt_update,
                };
                network_.send(std::move(msg));
            }

            Timestamp t = exchange_.currentTime();
            auto delivered = network_.receive(t);

            for (const auto& m : delivered) {
                if (config_.use_lock_free_queue) {
                    while (!md_queue_spsc->push(m.market_update)) {
                        if (stop_flag_.load(std::memory_order_relaxed)) return;
                        std::this_thread::yield();
                    }
                } else {
                    md_queue_mutex->push(m.market_update);
                }
            }

            // Yield briefly to simulate realistic inter-arrival
            std::this_thread::yield();
        }
        exchange_done.store(true, std::memory_order_release);
    });

    // ── Thread 2: Strategy Thread ──
    std::thread strat_thread([&]() {
        pinCurrentThreadToCore(config_.strategy_core_id);

        while (!exchange_done.load(std::memory_order_acquire) ||
               (config_.use_lock_free_queue ? !md_queue_spsc->empty() : !md_queue_mutex->empty()))
        {
            std::optional<MarketUpdate> update;
            if (config_.use_lock_free_queue) {
                update = md_queue_spsc->pop();
            } else {
                update = md_queue_mutex->pop();
            }

            if (!update) {
                std::this_thread::yield();
                continue;
            }

            Timestamp t = SimClock::now();
            if (feed_handler_.processMessage(*update, t)) {
                const auto& state = feed_handler_.currentState();
                auto opt_sig = strategy_.onMarketUpdate(state, t);

                if (opt_sig) {
                    Order ord{
                        .id          = next_client_order_id_++,
                        .instrument  = opt_sig->instrument,
                        .side        = (opt_sig->type == SignalType::Buy) ? Side::Buy : Side::Sell,
                        .type        = config_.strategy.order_type,
                        .price       = opt_sig->target_price,
                        .quantity    = opt_sig->target_quantity,
                        .submit_time = t,
                    };

                    if (config_.use_lock_free_queue) {
                        while (!order_queue_spsc->push(ord)) {
                            if (stop_flag_.load(std::memory_order_relaxed)) return;
                            std::this_thread::yield();
                        }
                    } else {
                        order_queue_mutex->push(ord);
                    }
                }
            }
        }
    });

    // ── Thread 3: Order Execution Thread ──
    std::thread exec_thread([&]() {
        pinCurrentThreadToCore(config_.exec_core_id);
        size_t recorded_fills = 0;

        while (!exchange_done.load(std::memory_order_acquire) ||
               (config_.use_lock_free_queue ? !order_queue_spsc->empty() : !order_queue_mutex->empty()))
        {
            std::optional<Order> ord;
            if (config_.use_lock_free_queue) {
                ord = order_queue_spsc->pop();
            } else {
                ord = order_queue_mutex->pop();
            }

            Timestamp t = SimClock::now();

            if (ord) {
                Price expected_mid = exchange_.currentMidPrice();
                execution_.submitOrder(*ord, t, expected_mid);
            }

            execution_.process(t, exchange_);
            auto reports = execution_.receiveReports(t);
            for (const auto& rep : reports) {
                strategy_.onExecutionReport(rep);
            }

            const auto& fills = execution_.fills();
            while (recorded_fills < fills.size()) {
                analytics_.recordFill(fills[recorded_fills], exchange_.currentMidPrice());
                ++recorded_fills;
            }

            std::this_thread::yield();
        }
    });

    if (md_thread.joinable())    md_thread.join();
    if (strat_thread.joinable()) strat_thread.join();
    if (exec_thread.joinable())  exec_thread.join();

    return analytics_.computeMetrics(execution_.computeSummaryStats());
}

} // namespace njt
