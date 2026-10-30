#pragma once
/// @file performance_analyzer.h
/// @brief Quantitative analytics: PnL tracking, Sharpe, Sortino, Drawdown,
///        latency distribution percentiles, and CSV export.

#include "common/types.h"
#include "execution/execution_simulator.h"
#include <string>
#include <vector>

namespace njt {

struct PnLPoint {
    Timestamp timestamp      = 0;
    Price     mid_price      = 0;
    Quantity  position       = 0;
    double    realized_pnl   = 0.0;
    double    unrealized_pnl = 0.0;
    double    total_pnl      = 0.0;
    double    drawdown       = 0.0;
};

struct TradeRecord {
    OrderId   order_id      = 0;
    Timestamp timestamp     = 0;
    Side      side          = Side::Buy;
    Price     price         = 0;
    Quantity  quantity      = 0;
    double    pnl           = 0.0;
    Price     slippage      = 0;
    double    latency_us    = 0.0;
};

struct PerformanceMetrics {
    double total_pnl          = 0.0;
    double realized_pnl       = 0.0;
    double unrealized_pnl     = 0.0;
    double max_drawdown       = 0.0;
    double max_drawdown_pct   = 0.0;
    double sharpe_ratio       = 0.0;
    double sortino_ratio      = 0.0;
    double win_rate           = 0.0;
    double profit_factor      = 0.0;

    size_t   total_trades     = 0;
    Quantity total_volume     = 0;
    Quantity max_long_pos     = 0;
    Quantity max_short_pos    = 0;

    double fill_rate          = 0.0;
    double avg_slippage_ticks = 0.0;
    double avg_slippage_bps   = 0.0;

    // Latency statistics (microseconds)
    double latency_p50_us     = 0.0;
    double latency_p90_us     = 0.0;
    double latency_p99_us     = 0.0;
    double latency_p999_us    = 0.0;
    double latency_max_us     = 0.0;
    double latency_mean_us    = 0.0;

    // Markout curve (in ticks)
    std::array<double, Fill::kNumMarkoutHorizons> markout_curve{};
};

class PerformanceAnalyzer {
public:
    PerformanceAnalyzer();

    /// Record a trade fill.
    void recordFill(const Fill& fill, Price current_mid_price);

    /// Sample periodic mark-to-market PnL.
    void samplePnL(Timestamp current_time, Price current_mid_price);

    /// Record a measured tick-to-trade latency sample (in nanoseconds).
    void recordLatency(Timestamp latency_ns);

    /// Compute full quantitative performance statistics.
    [[nodiscard]] PerformanceMetrics computeMetrics(const ExecutionSimulator::SummaryStats& exec_stats) const;

    // ── CSV Exporters ──────────────────────────────────────────────────
    bool exportPnLTimeSeries(const std::string& filepath) const;
    bool exportTrades(const std::string& filepath) const;
    bool exportLatencySamples(const std::string& filepath) const;
    bool exportSummaryMetrics(const std::string& filepath, const PerformanceMetrics& m) const;

    void reset();

private:
    Quantity position_       = 0;
    double   avg_cost_       = 0.0;  // Average entry price in dollars
    double   realized_pnl_   = 0.0;  // in dollars
    double   peak_pnl_       = 0.0;
    double   max_drawdown_   = 0.0;
    Quantity max_long_       = 0;
    Quantity max_short_      = 0;

    std::vector<PnLPoint>    pnl_history_;
    std::vector<TradeRecord> trade_records_;
    std::vector<double>      latency_samples_us_;
};

} // namespace njt
