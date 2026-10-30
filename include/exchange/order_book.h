#pragma once
/// @file order_book.h
/// @brief Price-time priority limit order book with FIFO matching.
///
/// Bids are stored in a std::map with std::greater (best = highest price first).
/// Asks are stored in a std::map with std::less  (best = lowest  price first).
/// Each price level contains a FIFO deque of resting orders.

#include "exchange/order.h"
#include "exchange/market_data.h"

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace njt {

/// Aggregate quantity and individual orders at one price level.
struct PriceLevel {
    Price    price          = 0;
    Quantity total_quantity = 0;
    std::deque<Order> orders;

    void addOrder(Order order) {
        total_quantity += order.remainingQuantity();
        orders.push_back(std::move(order));
    }

    bool removeOrder(OrderId id) {
        for (auto it = orders.begin(); it != orders.end(); ++it) {
            if (it->id == id) {
                total_quantity -= it->remainingQuantity();
                orders.erase(it);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool empty() const noexcept { return orders.empty(); }
};

/// Best-bid / best-ask convenience struct.
struct TopOfBook {
    std::optional<BookLevel> best_bid;
    std::optional<BookLevel> best_ask;

    [[nodiscard]] std::optional<Price> midPrice() const {
        if (best_bid && best_ask)
            return (best_bid->price + best_ask->price) / 2;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Price> spread() const {
        if (best_bid && best_ask)
            return best_ask->price - best_bid->price;
        return std::nullopt;
    }
};

/// Price-time-priority limit order book.
///
/// ## Matching algorithm
///  1. Incoming BUY matches against the ask side (lowest-price first).
///  2. Incoming SELL matches against the bid side (highest-price first).
///  3. At each price level orders are filled FIFO.
///  4. Remaining quantity of a limit order rests in the book.
class OrderBook {
public:
    /// Insert an order, run matching, return generated trades.
    std::vector<Trade> addOrder(Order order, Timestamp current_time);

    /// Cancel a resting order.  Returns true if found and cancelled.
    bool cancelOrder(OrderId id);

    // ── Queries ────────────────────────────────────────────────────────
    [[nodiscard]] TopOfBook              topOfBook()                     const;
    [[nodiscard]] std::vector<BookLevel> getBidDepth(size_t levels)      const;
    [[nodiscard]] std::vector<BookLevel> getAskDepth(size_t levels)      const;
    [[nodiscard]] Quantity               bestBidQuantity()               const;
    [[nodiscard]] Quantity               bestAskQuantity()               const;
    [[nodiscard]] std::optional<Price>   bestBid()                       const;
    [[nodiscard]] std::optional<Price>   bestAsk()                       const;
    [[nodiscard]] size_t                 bidLevelCount()                 const { return bids_.size(); }
    [[nodiscard]] size_t                 askLevelCount()                 const { return asks_.size(); }
    [[nodiscard]] size_t                 totalOrderCount()               const;

    void clear();

private:
    std::vector<Trade> matchOrder(Order& aggressor, Timestamp current_time);
    void               insertOrder(Order order);

    // Bids: highest price first
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    // Asks: lowest price first
    std::map<Price, PriceLevel>                      asks_;
    // O(1) lookup for cancellations
    std::unordered_map<OrderId, std::pair<Side, Price>> order_index_;
};

} // namespace njt
