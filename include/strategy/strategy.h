#pragma once
/// @file strategy.h
/// @brief Abstract base interface and data structures for quantitative strategies.

#include "common/types.h"
#include "exchange/market_data.h"
#include "exchange/order.h"
#include <optional>
#include <vector>

namespace njt {

/// Trading signal emitted by a strategy based on market data updates.
struct Signal {
    Timestamp  timestamp     = 0;
    InstrumentId instrument  = kDefaultInstrument;
    SignalType type          = SignalType::Hold;
    double     conviction    = 0.0;     // [-1.0, 1.0], magnitude indicates strength
    Price      target_price  = 0;       // suggested limit price (or 0 for market)
    Quantity   target_quantity = 0;     // suggested size
    double     feature_value = 0.0;     // e.g. raw imbalance ratio
};

/// Interface for trading strategies.
class IStrategy {
public:
    virtual ~IStrategy() = default;

    /// Process a new market update and optionally generate a signal.
    virtual std::optional<Signal> onMarketUpdate(const MarketUpdate& update, Timestamp current_time) = 0;

    /// Notification when an execution report arrives.
    virtual void onExecutionReport(const ExecutionReport& report) = 0;

    /// Current inventory / position.
    [[nodiscard]] virtual Quantity position() const noexcept = 0;

    /// Reset internal state.
    virtual void reset() = 0;
};

} // namespace njt
