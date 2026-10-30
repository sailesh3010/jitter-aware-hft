#pragma once
/// @file execution_simulator.h
/// @brief Realistic execution simulation with uplink/downlink network jitter,
///        adverse selection, slippage modeling, and markout price analysis.

#include "common/types.h"
#include "exchange/exchange_simulator.h"
#include "exchange/order.h"
#include "exchange/market_data.h"
#include "network/network_config.h"
#include "network/latency_model.h"

#include <array>
#include <deque>
#include <memory>
#include <queue>
#include <vector>

namespace njt {

/// Execution record containing full timing, slippage, and adverse-selection markouts.
struct Fill {
    OrderId     order_id            = 0;
    Side        side                = Side::Buy;
    OrderType   order_type          = OrderType::Limit;
    Price       expected_price      = 0;  // client-perceived price at submission
    Price       fill_price          = 0;  // actual fill price on exchange
    Quantity    quantity            = 0;
    Timestamp   client_submit_time  = 0;
    Timestamp   exchange_match_time = 0;
    Timestamp   client_ack_time     = 0;
    Price       slippage_ticks      = 0;  // (fill_price - expected_price) * (Buy ? 1 : -1)
    double      slippage_bps        = 0.0;

    // Markout horizons: +100μs, +1ms, +5ms, +10ms, +50ms
    static constexpr size_t kNumMarkoutHorizons = 5;
    static constexpr std::array<Timestamp, kNumMarkoutHorizons> kMarkoutHorizonsNs = {
        100'000, 1'000'000, 5'000'000, 10'000'000, 50'000'000
    };

    std::array<Price, kNumMarkoutHorizons> markout_mid_prices{};
    std::array<bool,  kNumMarkoutHorizons> markout_ready{};

    /// Compute markout PnL in ticks: (mid(t + horizon) - fill_price) * direction
    [[nodiscard]] double markoutPnL(size_t horizon_idx) const noexcept {
        if (!markout_ready[horizon_idx]) return 0.0;
        double sign = (side == Side::Buy) ? 1.0 : -1.0;
        return (static_cast<double>(markout_mid_prices[horizon_idx]) -
                static_cast<double>(fill_price)) * sign;
    }
};

struct ExecutionConfig {
    NetworkConfig uplink_config;    // Client -> Exchange latency / jitter / loss
    NetworkConfig downlink_config;  // Exchange -> Client ack latency / jitter
    bool          enable_slippage       = true;
    bool          enable_queue_modeling = true;
};

class ExecutionSimulator {
public:
    explicit ExecutionSimulator(const ExecutionConfig& config);

    /// Client submits an order. Enqueued onto the simulated uplink.
    void submitOrder(Order order, Timestamp client_time, Price expected_price);

    /// Advance simulation: deliver uplink orders to exchange, match, schedule downlink acks.
    void process(Timestamp current_time, ExchangeSimulator& exchange);

    /// Drain execution reports delivered to the client by current_time.
    std::vector<ExecutionReport> receiveReports(Timestamp client_time);

    /// Update markouts for previous fills against the current exchange mid-price.
    void updateMarkouts(Timestamp current_time, Price current_mid_price);

    // ── Inspection & Analytics ──────────────────────────────────────────
    [[nodiscard]] const std::vector<Fill>& fills() const noexcept { return fills_; }
    [[nodiscard]] size_t totalOrdersSubmitted() const noexcept { return total_orders_submitted_; }
    [[nodiscard]] size_t totalOrdersFilled() const noexcept { return fills_.size(); }
    [[nodiscard]] size_t totalOrdersDropped() const noexcept { return total_orders_dropped_; }

    struct SummaryStats {
        size_t submitted_count   = 0;
        size_t filled_count      = 0;
        size_t dropped_count     = 0;
        double fill_rate         = 0.0;
        double avg_slippage_ticks= 0.0;
        double avg_slippage_bps  = 0.0;
        std::array<double, Fill::kNumMarkoutHorizons> avg_markouts{};
    };

    [[nodiscard]] SummaryStats computeSummaryStats() const;

    void reset();

private:
    struct PendingOrder {
        Order     order;
        Timestamp client_submit_time;
        Timestamp exchange_delivery_time;
        Price     expected_price;
    };

    struct PendingReport {
        ExecutionReport report;
        Timestamp       client_delivery_time;
    };

    struct CompareUplink {
        bool operator()(const PendingOrder& a, const PendingOrder& b) const noexcept {
            return a.exchange_delivery_time > b.exchange_delivery_time;
        }
    };

    struct CompareDownlink {
        bool operator()(const PendingReport& a, const PendingReport& b) const noexcept {
            return a.client_delivery_time > b.client_delivery_time;
        }
    };

    ExecutionConfig config_;
    LatencyModel    uplink_latency_;
    LatencyModel    downlink_latency_;

    std::priority_queue<PendingOrder,  std::vector<PendingOrder>,  CompareUplink>   uplink_queue_;
    std::priority_queue<PendingReport, std::vector<PendingReport>, CompareDownlink> downlink_queue_;

    std::vector<Fill> fills_;
    size_t total_orders_submitted_ = 0;
    size_t total_orders_dropped_   = 0;
};

} // namespace njt
