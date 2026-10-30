/// @file execution_simulator.cpp
/// @brief Execution simulation implementation with uplink/downlink jitter and adverse selection.

#include "execution/execution_simulator.h"
#include <algorithm>
#include <numeric>

namespace njt {

ExecutionSimulator::ExecutionSimulator(const ExecutionConfig& config)
    : config_(config)
    , uplink_latency_(config.uplink_config)
    , downlink_latency_(config.downlink_config)
{}

void ExecutionSimulator::submitOrder(Order order, Timestamp client_time, Price expected_price) {
    ++total_orders_submitted_;

    // Uplink packet drop simulation
    if (uplink_latency_.shouldDrop()) {
        ++total_orders_dropped_;
        return;
    }

    Timestamp latency = uplink_latency_.sampleLatency();
    if (uplink_latency_.shouldReorder()) {
        latency += uplink_latency_.sampleLatency();
    }

    PendingOrder po{
        .order                  = std::move(order),
        .client_submit_time     = client_time,
        .exchange_delivery_time = client_time + latency,
        .expected_price         = expected_price
    };

    uplink_queue_.push(std::move(po));
}

void ExecutionSimulator::process(Timestamp current_time, ExchangeSimulator& exchange) {
    // Deliver orders whose uplink time has elapsed
    while (!uplink_queue_.empty() &&
           uplink_queue_.top().exchange_delivery_time <= current_time)
    {
        auto po = uplink_queue_.top();
        uplink_queue_.pop();

        // Submit to exchange at its arrival time
        po.order.submit_time = po.exchange_delivery_time;
        auto reports = exchange.submitOrder(std::move(po.order));

        for (auto& rep : reports) {
            rep.side = po.order.side;

            if (rep.status == OrderStatus::Filled || rep.status == OrderStatus::PartiallyFilled) {
                Price slip_ticks = 0;
                if (po.expected_price > 0) {
                    slip_ticks = (po.order.side == Side::Buy)
                                     ? (rep.fill_price - po.expected_price)
                                     : (po.expected_price - rep.fill_price);
                }

                double slip_bps = 0.0;
                if (po.expected_price > 0) {
                    slip_bps = (static_cast<double>(slip_ticks) /
                                static_cast<double>(po.expected_price)) * 10'000.0;
                }

                Fill fill{
                    .order_id            = rep.order_id,
                    .side                = po.order.side,
                    .order_type          = po.order.type,
                    .expected_price      = po.expected_price,
                    .fill_price          = rep.fill_price,
                    .quantity            = rep.fill_quantity,
                    .client_submit_time  = po.client_submit_time,
                    .exchange_match_time = rep.exchange_timestamp,
                    .client_ack_time     = 0,
                    .slippage_ticks      = slip_ticks,
                    .slippage_bps        = slip_bps,
                };
                fills_.push_back(fill);
            }

            // Downlink delivery for ack
            if (!downlink_latency_.shouldDrop()) {
                Timestamp down_lat = downlink_latency_.sampleLatency();
                downlink_queue_.push(PendingReport{
                    .report               = rep,
                    .client_delivery_time = rep.exchange_timestamp + down_lat
                });
            }
        }
    }

    // Update markouts with current mid price
    updateMarkouts(current_time, exchange.currentMidPrice());
}

std::vector<ExecutionReport> ExecutionSimulator::receiveReports(Timestamp client_time) {
    std::vector<ExecutionReport> reports;

    while (!downlink_queue_.empty() &&
           downlink_queue_.top().client_delivery_time <= client_time)
    {
        auto item = downlink_queue_.top();
        downlink_queue_.pop();

        // Stamp client ack time onto corresponding fill
        for (auto& fill : fills_) {
            if (fill.order_id == item.report.order_id && fill.client_ack_time == 0) {
                fill.client_ack_time = item.client_delivery_time;
                break;
            }
        }

        reports.push_back(std::move(item.report));
    }

    return reports;
}

void ExecutionSimulator::updateMarkouts(Timestamp current_time, Price current_mid_price) {
    for (auto& fill : fills_) {
        for (size_t h = 0; h < Fill::kNumMarkoutHorizons; ++h) {
            if (!fill.markout_ready[h]) {
                if (current_time >= fill.exchange_match_time + Fill::kMarkoutHorizonsNs[h]) {
                    fill.markout_mid_prices[h] = current_mid_price;
                    fill.markout_ready[h]      = true;
                }
            }
        }
    }
}

ExecutionSimulator::SummaryStats ExecutionSimulator::computeSummaryStats() const {
    SummaryStats stats{};
    stats.submitted_count = total_orders_submitted_;
    stats.filled_count    = fills_.size();
    stats.dropped_count   = total_orders_dropped_;

    if (stats.submitted_count > 0) {
        stats.fill_rate = static_cast<double>(stats.filled_count) /
                          static_cast<double>(stats.submitted_count);
    }

    if (!fills_.empty()) {
        double total_slip_ticks = 0.0;
        double total_slip_bps   = 0.0;
        for (const auto& f : fills_) {
            total_slip_ticks += static_cast<double>(f.slippage_ticks);
            total_slip_bps   += f.slippage_bps;
        }
        stats.avg_slippage_ticks = total_slip_ticks / static_cast<double>(fills_.size());
        stats.avg_slippage_bps   = total_slip_bps   / static_cast<double>(fills_.size());

        for (size_t h = 0; h < Fill::kNumMarkoutHorizons; ++h) {
            double horizon_sum = 0.0;
            size_t count       = 0;
            for (const auto& f : fills_) {
                if (f.markout_ready[h]) {
                    horizon_sum += f.markoutPnL(h);
                    ++count;
                }
            }
            stats.avg_markouts[h] = (count > 0) ? (horizon_sum / static_cast<double>(count)) : 0.0;
        }
    }

    return stats;
}

void ExecutionSimulator::reset() {
    fills_.clear();
    total_orders_submitted_ = 0;
    total_orders_dropped_   = 0;

    while (!uplink_queue_.empty())   uplink_queue_.pop();
    while (!downlink_queue_.empty()) downlink_queue_.pop();
}

} // namespace njt
