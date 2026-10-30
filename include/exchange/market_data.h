#pragma once
/// @file market_data.h
/// @brief Wire-level market-data structures published by the exchange.

#include "common/types.h"
#include <array>
#include <cstring>

namespace njt {

/// A single price-level entry in the book snapshot.
struct BookLevel {
    Price    price    = 0;
    Quantity quantity = 0;
};

/// Market-data update published by the exchange to all subscribers.
///
/// Contains a depth-of-book snapshot plus optional last-trade information.
/// Carried over the (simulated or real) network inside a NetworkMessage.
struct MarketUpdate {
    enum class Type : uint8_t {
        BookUpdate,   ///< BBO / depth changed
        Trade         ///< A trade occurred on the exchange
    };

    static constexpr size_t kMaxDepth = 5;

    SequenceNumber sequence            = 0;
    Timestamp      exchange_timestamp  = 0;
    InstrumentId   instrument          = kDefaultInstrument;
    Type           type                = Type::BookUpdate;

    // Book depth (populated for both BookUpdate and Trade events)
    std::array<BookLevel, kMaxDepth> bids{};
    std::array<BookLevel, kMaxDepth> asks{};
    uint8_t bid_levels = 0;
    uint8_t ask_levels = 0;

    // Last-trade fields (meaningful only when type == Trade)
    Price    last_trade_price    = 0;
    Quantity last_trade_quantity = 0;
    Side     aggressor_side      = Side::Buy;

    /// Best bid price (convenience).
    [[nodiscard]] Price bestBid() const noexcept {
        return bid_levels > 0 ? bids[0].price : 0;
    }
    /// Best ask price (convenience).
    [[nodiscard]] Price bestAsk() const noexcept {
        return ask_levels > 0 ? asks[0].price : 0;
    }
    /// Mid price in ticks.
    [[nodiscard]] Price midPrice() const noexcept {
        if (bid_levels > 0 && ask_levels > 0)
            return (bids[0].price + asks[0].price) / 2;
        return 0;
    }
};

/// Execution report sent by the exchange back to the order submitter.
struct ExecutionReport {
    OrderId     order_id           = 0;
    Side        side               = Side::Buy;
    OrderStatus status             = OrderStatus::New;
    Price       fill_price         = 0;
    Quantity    fill_quantity      = 0;
    Quantity    remaining_quantity = 0;
    Timestamp   exchange_timestamp = 0;
};

} // namespace njt
