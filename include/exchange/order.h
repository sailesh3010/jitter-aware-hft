#pragma once
/// @file order.h
/// @brief Order and Trade value types.

#include "common/types.h"

namespace njt {

/// A single order submitted to the exchange.
struct Order {
    OrderId      id              = 0;
    InstrumentId instrument      = kDefaultInstrument;
    Side         side            = Side::Buy;
    OrderType    type            = OrderType::Limit;
    Price        price           = 0;          // in ticks (ignored for Market orders)
    Quantity     quantity        = 0;
    Quantity     filled_quantity = 0;
    OrderStatus  status          = OrderStatus::New;
    Timestamp    submit_time     = 0;          // client-side submission time
    Timestamp    accept_time     = 0;          // exchange-side acceptance time

    [[nodiscard]] Quantity remainingQuantity() const noexcept {
        return quantity - filled_quantity;
    }
    [[nodiscard]] bool isFullyFilled() const noexcept {
        return filled_quantity >= quantity;
    }
    [[nodiscard]] bool isActive() const noexcept {
        return status == OrderStatus::Accepted ||
               status == OrderStatus::PartiallyFilled ||
               status == OrderStatus::New;
    }
};

/// A single execution (match) between an aggressor and a passive order.
struct Trade {
    OrderId  aggressor_order_id = 0;
    OrderId  passive_order_id   = 0;
    Price    price              = 0;
    Quantity quantity           = 0;
    Side     aggressor_side     = Side::Buy;
    Timestamp timestamp         = 0;
};

} // namespace njt
