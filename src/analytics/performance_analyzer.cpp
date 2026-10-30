/// @file performance_analyzer.cpp
/// @brief Implementation of quantitative performance analysis and reporting.

#include "analytics/performance_analyzer.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>

namespace njt {

PerformanceAnalyzer::PerformanceAnalyzer() = default;

void PerformanceAnalyzer::recordFill(const Fill& fill, Price current_mid_price) {
    double fill_px_d = toDouble(fill.fill_price);
    Quantity qty     = fill.quantity;
    double trade_pnl = 0.0;

    if (fill.side == Side::Buy) {
        if (position_ >= 0) {
            double total_val = static_cast<double>(position_) * avg_cost_ +
                               static_cast<double>(qty) * fill_px_d;
            position_ += qty;
            avg_cost_ = total_val / static_cast<double>(position_);
        } else {
            // Covering short
            Quantity close_qty = std::min(qty, -position_);
            trade_pnl = static_cast<double>(close_qty) * (avg_cost_ - fill_px_d);
            realized_pnl_ += trade_pnl;

            Quantity rem_qty = qty - close_qty;
            if (rem_qty > 0) {
                position_ = rem_qty;
                avg_cost_ = fill_px_d;
            } else {
                position_ += close_qty;
                if (position_ == 0) avg_cost_ = 0.0;
            }
        }
    } else {
        // Sell
        if (position_ <= 0) {
            double total_val = static_cast<double>(-position_) * avg_cost_ +
                               static_cast<double>(qty) * fill_px_d;
            position_ -= qty;
            avg_cost_ = total_val / static_cast<double>(-position_);
        } else {
            // Closing long
            Quantity close_qty = std::min(qty, position_);
            trade_pnl = static_cast<double>(close_qty) * (fill_px_d - avg_cost_);
            realized_pnl_ += trade_pnl;

            Quantity rem_qty = qty - close_qty;
            if (rem_qty > 0) {
                position_ = -rem_qty;
                avg_cost_ = fill_px_d;
            } else {
                position_ -= close_qty;
                if (position_ == 0) avg_cost_ = 0.0;
            }
        }
    }

    max_long_  = std::max(max_long_, position_);
    max_short_ = std::min(max_short_, position_);

    double mid_d = toDouble(current_mid_price);
    double unrl_pnl = 0.0;
    if (position_ > 0) {
        unrl_pnl = static_cast<double>(position_) * (mid_d - avg_cost_);
    } else if (position_ < 0) {
        unrl_pnl = static_cast<double>(-position_) * (avg_cost_ - mid_d);
    }

    double tot_pnl = realized_pnl_ + unrl_pnl;
    peak_pnl_      = std::max(peak_pnl_, tot_pnl);
    double dd      = std::max(0.0, peak_pnl_ - tot_pnl);
    max_drawdown_  = std::max(max_drawdown_, dd);

    double lat_us = (fill.client_ack_time >= fill.client_submit_time)
                        ? static_cast<double>(fill.client_ack_time - fill.client_submit_time) / 1000.0
                        : 0.0;

    trade_records_.push_back(TradeRecord{
        .order_id   = fill.order_id,
        .timestamp  = fill.exchange_match_time,
        .side       = fill.side,
        .price      = fill.fill_price,
        .quantity   = fill.quantity,
        .pnl        = trade_pnl,
        .slippage   = fill.slippage_ticks,
        .latency_us = lat_us,
    });

    if (lat_us > 0.0) {
        latency_samples_us_.push_back(lat_us);
    }
}

void PerformanceAnalyzer::samplePnL(Timestamp current_time, Price current_mid_price) {
    double mid_d = toDouble(current_mid_price);
    double unrl_pnl = 0.0;
    if (position_ > 0) {
        unrl_pnl = static_cast<double>(position_) * (mid_d - avg_cost_);
    } else if (position_ < 0) {
        unrl_pnl = static_cast<double>(-position_) * (avg_cost_ - mid_d);
    }

    double tot_pnl = realized_pnl_ + unrl_pnl;
    peak_pnl_      = std::max(peak_pnl_, tot_pnl);
    double dd      = std::max(0.0, peak_pnl_ - tot_pnl);
    max_drawdown_  = std::max(max_drawdown_, dd);

    pnl_history_.push_back(PnLPoint{
        .timestamp      = current_time,
        .mid_price      = current_mid_price,
        .position       = position_,
        .realized_pnl   = realized_pnl_,
        .unrealized_pnl = unrl_pnl,
        .total_pnl      = tot_pnl,
        .drawdown       = dd,
    });
}

void PerformanceAnalyzer::recordLatency(Timestamp latency_ns) {
    latency_samples_us_.push_back(static_cast<double>(latency_ns) / 1000.0);
}

PerformanceMetrics PerformanceAnalyzer::computeMetrics(
    const ExecutionSimulator::SummaryStats& exec_stats) const
{
    PerformanceMetrics m{};
    m.realized_pnl   = realized_pnl_;
    m.max_drawdown   = max_drawdown_;
    m.max_long_pos   = max_long_;
    m.max_short_pos  = max_short_;
    m.total_trades   = trade_records_.size();

    if (!pnl_history_.empty()) {
        m.total_pnl      = pnl_history_.back().total_pnl;
        m.unrealized_pnl = pnl_history_.back().unrealized_pnl;
    } else {
        m.total_pnl      = realized_pnl_;
    }

    if (peak_pnl_ > 0.0) {
        m.max_drawdown_pct = (max_drawdown_ / peak_pnl_) * 100.0;
    }

    // Trade win-rate & profit factor
    size_t winning_trades = 0;
    double gross_profit   = 0.0;
    double gross_loss     = 0.0;
    Quantity total_vol    = 0;

    for (const auto& tr : trade_records_) {
        total_vol += tr.quantity;
        if (tr.pnl > 0.0) {
            ++winning_trades;
            gross_profit += tr.pnl;
        } else if (tr.pnl < 0.0) {
            gross_loss += std::abs(tr.pnl);
        }
    }
    m.total_volume = total_vol;

    if (!trade_records_.empty()) {
        m.win_rate = static_cast<double>(winning_trades) / static_cast<double>(trade_records_.size());
    }
    if (gross_loss > 0.0) {
        m.profit_factor = gross_profit / gross_loss;
    } else if (gross_profit > 0.0) {
        m.profit_factor = 999.0;
    }

    // Sharpe & Sortino based on PnL increments
    if (pnl_history_.size() >= 2) {
        std::vector<double> returns;
        returns.reserve(pnl_history_.size() - 1);
        for (size_t i = 1; i < pnl_history_.size(); ++i) {
            returns.push_back(pnl_history_[i].total_pnl - pnl_history_[i - 1].total_pnl);
        }

        double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
        double mean = sum / static_cast<double>(returns.size());

        double sq_sum = 0.0;
        double downside_sq_sum = 0.0;
        size_t downside_count = 0;

        for (double r : returns) {
            double diff = r - mean;
            sq_sum += diff * diff;
            if (r < 0.0) {
                downside_sq_sum += r * r;
                ++downside_count;
            }
        }

        double variance = sq_sum / static_cast<double>(returns.size());
        double stddev   = std::sqrt(variance);

        // Annualization factor (assuming ~252 trading days, ~23,400s per day)
        double annual_factor = std::sqrt(252.0 * 23400.0);

        if (stddev > 1e-9) {
            m.sharpe_ratio = (mean / stddev) * annual_factor;
        }

        if (downside_count > 0) {
            double downside_dev = std::sqrt(downside_sq_sum / static_cast<double>(downside_count));
            if (downside_dev > 1e-9) {
                m.sortino_ratio = (mean / downside_dev) * annual_factor;
            }
        }
    }

    // Execution metrics
    m.fill_rate          = exec_stats.fill_rate;
    m.avg_slippage_ticks = exec_stats.avg_slippage_ticks;
    m.avg_slippage_bps   = exec_stats.avg_slippage_bps;
    m.markout_curve      = exec_stats.avg_markouts;

    // Latency percentiles
    if (!latency_samples_us_.empty()) {
        auto sorted_lat = latency_samples_us_;
        std::sort(sorted_lat.begin(), sorted_lat.end());

        auto get_pct = [&](double pct) {
            size_t idx = static_cast<size_t>(pct * static_cast<double>(sorted_lat.size() - 1));
            return sorted_lat[idx];
        };

        m.latency_p50_us  = get_pct(0.50);
        m.latency_p90_us  = get_pct(0.90);
        m.latency_p99_us  = get_pct(0.99);
        m.latency_p999_us = get_pct(0.999);
        m.latency_max_us  = sorted_lat.back();

        double lat_sum = std::accumulate(sorted_lat.begin(), sorted_lat.end(), 0.0);
        m.latency_mean_us = lat_sum / static_cast<double>(sorted_lat.size());
    }

    return m;
}

bool PerformanceAnalyzer::exportPnLTimeSeries(const std::string& filepath) const {
    std::ofstream out(filepath);
    if (!out.is_open()) return false;

    out << "timestamp_ns,mid_price_ticks,position,realized_pnl,unrealized_pnl,total_pnl,drawdown\n";
    for (const auto& pt : pnl_history_) {
        out << pt.timestamp << ","
            << pt.mid_price << ","
            << pt.position << ","
            << std::fixed << std::setprecision(4)
            << pt.realized_pnl << ","
            << pt.unrealized_pnl << ","
            << pt.total_pnl << ","
            << pt.drawdown << "\n";
    }
    return true;
}

bool PerformanceAnalyzer::exportTrades(const std::string& filepath) const {
    std::ofstream out(filepath);
    if (!out.is_open()) return false;

    out << "order_id,timestamp_ns,side,price_ticks,quantity,pnl_dollars,slippage_ticks,latency_us\n";
    for (const auto& tr : trade_records_) {
        out << tr.order_id << ","
            << tr.timestamp << ","
            << toString(tr.side) << ","
            << tr.price << ","
            << tr.quantity << ","
            << std::fixed << std::setprecision(4)
            << tr.pnl << ","
            << tr.slippage << ","
            << tr.latency_us << "\n";
    }
    return true;
}

bool PerformanceAnalyzer::exportLatencySamples(const std::string& filepath) const {
    std::ofstream out(filepath);
    if (!out.is_open()) return false;

    out << "sample_index,latency_us\n";
    for (size_t i = 0; i < latency_samples_us_.size(); ++i) {
        out << i << "," << std::fixed << std::setprecision(3) << latency_samples_us_[i] << "\n";
    }
    return true;
}

bool PerformanceAnalyzer::exportSummaryMetrics(const std::string& filepath, const PerformanceMetrics& m) const {
    std::ofstream out(filepath);
    if (!out.is_open()) return false;

    out << "{\n"
        << "  \"total_pnl\": " << m.total_pnl << ",\n"
        << "  \"realized_pnl\": " << m.realized_pnl << ",\n"
        << "  \"unrealized_pnl\": " << m.unrealized_pnl << ",\n"
        << "  \"max_drawdown\": " << m.max_drawdown << ",\n"
        << "  \"max_drawdown_pct\": " << m.max_drawdown_pct << ",\n"
        << "  \"sharpe_ratio\": " << m.sharpe_ratio << ",\n"
        << "  \"sortino_ratio\": " << m.sortino_ratio << ",\n"
        << "  \"win_rate\": " << m.win_rate << ",\n"
        << "  \"profit_factor\": " << m.profit_factor << ",\n"
        << "  \"total_trades\": " << m.total_trades << ",\n"
        << "  \"total_volume\": " << m.total_volume << ",\n"
        << "  \"max_long_pos\": " << m.max_long_pos << ",\n"
        << "  \"max_short_pos\": " << m.max_short_pos << ",\n"
        << "  \"fill_rate\": " << m.fill_rate << ",\n"
        << "  \"avg_slippage_ticks\": " << m.avg_slippage_ticks << ",\n"
        << "  \"avg_slippage_bps\": " << m.avg_slippage_bps << ",\n"
        << "  \"latency_p50_us\": " << m.latency_p50_us << ",\n"
        << "  \"latency_p90_us\": " << m.latency_p90_us << ",\n"
        << "  \"latency_p99_us\": " << m.latency_p99_us << ",\n"
        << "  \"latency_p999_us\": " << m.latency_p999_us << ",\n"
        << "  \"latency_max_us\": " << m.latency_max_us << ",\n"
        << "  \"latency_mean_us\": " << m.latency_mean_us << ",\n"
        << "  \"markouts_ticks\": ["
        << m.markout_curve[0] << ", "
        << m.markout_curve[1] << ", "
        << m.markout_curve[2] << ", "
        << m.markout_curve[3] << ", "
        << m.markout_curve[4] << "]\n"
        << "}\n";
    return true;
}

void PerformanceAnalyzer::reset() {
    position_     = 0;
    avg_cost_     = 0.0;
    realized_pnl_ = 0.0;
    peak_pnl_     = 0.0;
    max_drawdown_ = 0.0;
    max_long_     = 0;
    max_short_    = 0;

    pnl_history_.clear();
    trade_records_.clear();
    latency_samples_us_.clear();
}

} // namespace njt
