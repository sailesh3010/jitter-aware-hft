/// @file order_book.cpp
/// @brief Limit order book implementation with price-time priority matching.

#include "exchange/order_book.h"
#include <algorithm>

namespace njt {

// ────────────────────────────────────────────────────────────────────────────
// Public API
// ────────────────────────────────────────────────────────────────────────────

std::vector<Trade> OrderBook::addOrder(Order order, Timestamp current_time) {
    order.accept_time = current_time;
    order.status      = OrderStatus::Accepted;

    auto trades = matchOrder(order, current_time);

    // Rest any unfilled limit-order quantity in the book
    if (!order.isFullyFilled() && order.type == OrderType::Limit) {
        insertOrder(std::move(order));
    }

    return trades;
}

bool OrderBook::cancelOrder(OrderId id) {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) return false;

    auto [side, price] = it->second;

    if (side == Side::Buy) {
        auto level_it = bids_.find(price);
        if (level_it == bids_.end()) return false;
        bool removed = level_it->second.removeOrder(id);
        if (removed) {
            if (level_it->second.empty()) bids_.erase(level_it);
            order_index_.erase(it);
        }
        return removed;
    } else {
        auto level_it = asks_.find(price);
        if (level_it == asks_.end()) return false;
        bool removed = level_it->second.removeOrder(id);
        if (removed) {
            if (level_it->second.empty()) asks_.erase(level_it);
            order_index_.erase(it);
        }
        return removed;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Queries
// ────────────────────────────────────────────────────────────────────────────

TopOfBook OrderBook::topOfBook() const {
    TopOfBook tob;
    if (!bids_.empty()) {
        const auto& [price, level] = *bids_.begin();
        tob.best_bid = BookLevel{price, level.total_quantity};
    }
    if (!asks_.empty()) {
        const auto& [price, level] = *asks_.begin();
        tob.best_ask = BookLevel{price, level.total_quantity};
    }
    return tob;
}

std::vector<BookLevel> OrderBook::getBidDepth(size_t levels) const {
    std::vector<BookLevel> depth;
    depth.reserve(levels);
    for (const auto& [price, level] : bids_) {
        if (depth.size() >= levels) break;
        depth.push_back({price, level.total_quantity});
    }
    return depth;
}

std::vector<BookLevel> OrderBook::getAskDepth(size_t levels) const {
    std::vector<BookLevel> depth;
    depth.reserve(levels);
    for (const auto& [price, level] : asks_) {
        if (depth.size() >= levels) break;
        depth.push_back({price, level.total_quantity});
    }
    return depth;
}

Quantity OrderBook::bestBidQuantity() const {
    return bids_.empty() ? 0 : bids_.begin()->second.total_quantity;
}

Quantity OrderBook::bestAskQuantity() const {
    return asks_.empty() ? 0 : asks_.begin()->second.total_quantity;
}

std::optional<Price> OrderBook::bestBid() const {
    return bids_.empty() ? std::nullopt : std::optional{bids_.begin()->first};
}

std::optional<Price> OrderBook::bestAsk() const {
    return asks_.empty() ? std::nullopt : std::optional{asks_.begin()->first};
}

size_t OrderBook::totalOrderCount() const {
    size_t count = 0;
    for (const auto& [_, level] : bids_) count += level.orders.size();
    for (const auto& [_, level] : asks_) count += level.orders.size();
    return count;
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    order_index_.clear();
}

// ────────────────────────────────────────────────────────────────────────────
// Private helpers
// ────────────────────────────────────────────────────────────────────────────

std::vector<Trade> OrderBook::matchOrder(Order& aggressor, Timestamp current_time) {
    std::vector<Trade> trades;

    if (aggressor.side == Side::Buy) {
        // Aggressor is a BUY → match against asks (lowest price first)
        while (!asks_.empty() && aggressor.remainingQuantity() > 0) {
            auto& [level_price, level] = *asks_.begin();

            // Limit order price check
            if (aggressor.type == OrderType::Limit && level_price > aggressor.price)
                break;

            while (!level.orders.empty() && aggressor.remainingQuantity() > 0) {
                auto& passive  = level.orders.front();
                Quantity fill_qty = std::min(aggressor.remainingQuantity(),
                                             passive.remainingQuantity());

                trades.push_back(Trade{
                    .aggressor_order_id = aggressor.id,
                    .passive_order_id   = passive.id,
                    .price              = level_price,
                    .quantity           = fill_qty,
                    .aggressor_side     = aggressor.side,
                    .timestamp          = current_time,
                });

                aggressor.filled_quantity += fill_qty;
                passive.filled_quantity   += fill_qty;
                level.total_quantity      -= fill_qty;

                aggressor.status = aggressor.isFullyFilled()
                                       ? OrderStatus::Filled
                                       : OrderStatus::PartiallyFilled;

                if (passive.isFullyFilled()) {
                    order_index_.erase(passive.id);
                    level.orders.pop_front();
                } else {
                    passive.status = OrderStatus::PartiallyFilled;
                }
            }

            if (level.orders.empty()) asks_.erase(asks_.begin());
        }
    } else {
        // Aggressor is a SELL → match against bids (highest price first)
        while (!bids_.empty() && aggressor.remainingQuantity() > 0) {
            auto& [level_price, level] = *bids_.begin();

            if (aggressor.type == OrderType::Limit && level_price < aggressor.price)
                break;

            while (!level.orders.empty() && aggressor.remainingQuantity() > 0) {
                auto& passive  = level.orders.front();
                Quantity fill_qty = std::min(aggressor.remainingQuantity(),
                                             passive.remainingQuantity());

                trades.push_back(Trade{
                    .aggressor_order_id = aggressor.id,
                    .passive_order_id   = passive.id,
                    .price              = level_price,
                    .quantity           = fill_qty,
                    .aggressor_side     = aggressor.side,
                    .timestamp          = current_time,
                });

                aggressor.filled_quantity += fill_qty;
                passive.filled_quantity   += fill_qty;
                level.total_quantity      -= fill_qty;

                aggressor.status = aggressor.isFullyFilled()
                                       ? OrderStatus::Filled
                                       : OrderStatus::PartiallyFilled;

                if (passive.isFullyFilled()) {
                    order_index_.erase(passive.id);
                    level.orders.pop_front();
                } else {
                    passive.status = OrderStatus::PartiallyFilled;
                }
            }

            if (level.orders.empty()) bids_.erase(bids_.begin());
        }
    }

    return trades;
}

void OrderBook::insertOrder(Order order) {
    Price   price = order.price;
    OrderId id    = order.id;
    Side    side  = order.side;

    if (side == Side::Buy) {
        auto& level  = bids_[price];
        level.price  = price;
        level.addOrder(std::move(order));
    } else {
        auto& level  = asks_[price];
        level.price  = price;
        level.addOrder(std::move(order));
    }
    order_index_[id] = {side, price};
}

} // namespace njt
