#pragma once
/// @file types.h
/// @brief Core type aliases and enumerations used throughout the simulator.
///
/// Prices are represented as integer "ticks" to avoid floating-point
/// comparison issues.  1 tick = kTickSize dollars (default 0.01).

#include <cstdint>
#include <string_view>

namespace njt {

// ── Fundamental Type Aliases ───────────────────────────────────────────────

/// Integer price in ticks.  E.g. 10000 ticks × $0.01/tick = $100.00
using Price    = int64_t;
using Quantity = int64_t;
using OrderId  = uint64_t;
using SequenceNumber = uint64_t;
/// Nanoseconds since simulation start (or epoch for wall-clock).
using Timestamp = uint64_t;
using InstrumentId = uint16_t;

// ── Constants ──────────────────────────────────────────────────────────────

constexpr double       kTickSize         = 0.01;
constexpr InstrumentId kDefaultInstrument = 1;

// ── Enumerations ───────────────────────────────────────────────────────────

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t { Market, Limit };

enum class OrderStatus : uint8_t {
    New, Accepted, PartiallyFilled, Filled, Cancelled, Rejected
};

enum class SignalType : uint8_t { Buy, Sell, Hold };

// ── Inline Conversion Helpers ──────────────────────────────────────────────

[[nodiscard]] constexpr double toDouble(Price p) noexcept {
    return static_cast<double>(p) * kTickSize;
}

[[nodiscard]] constexpr Price toTicks(double price) noexcept {
    return static_cast<Price>(price / kTickSize + 0.5);
}

[[nodiscard]] constexpr Side oppositeSide(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr std::string_view toString(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

[[nodiscard]] constexpr std::string_view toString(SignalType s) noexcept {
    switch (s) {
        case SignalType::Buy:  return "BUY";
        case SignalType::Sell: return "SELL";
        case SignalType::Hold: return "HOLD";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view toString(OrderStatus s) noexcept {
    switch (s) {
        case OrderStatus::New:             return "NEW";
        case OrderStatus::Accepted:        return "ACCEPTED";
        case OrderStatus::PartiallyFilled: return "PARTIAL_FILL";
        case OrderStatus::Filled:          return "FILLED";
        case OrderStatus::Cancelled:       return "CANCELLED";
        case OrderStatus::Rejected:        return "REJECTED";
    }
    return "UNKNOWN";
}

} // namespace njt
