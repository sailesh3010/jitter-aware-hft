#include <gtest/gtest.h>
#include "exchange/order_book.h"

using namespace njt;

TEST(OrderBookTest, BasicInsertAndMatch) {
    OrderBook book;

    // Resting limit order on ask side: SELL 10 @ $100.05 (10005 ticks)
    Order ask{
        .id = 1,
        .side = Side::Sell,
        .type = OrderType::Limit,
        .price = 10005,
        .quantity = 10,
    };
    auto trades = book.addOrder(ask, 1000);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.bestAsk().value_or(0), 10005);
    EXPECT_EQ(book.bestAskQuantity(), 10);

    // Aggressive BUY 10 @ $100.05
    Order buy{
        .id = 2,
        .side = Side::Buy,
        .type = OrderType::Limit,
        .price = 10005,
        .quantity = 10,
    };
    trades = book.addOrder(buy, 1100);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].aggressor_order_id, 2u);
    EXPECT_EQ(trades[0].passive_order_id, 1u);
    EXPECT_EQ(trades[0].price, 10005);
    EXPECT_EQ(trades[0].quantity, 10);

    // Book should now be empty
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.totalOrderCount(), 0u);
}

TEST(OrderBookTest, PriceTimePriorityFIFO) {
    OrderBook book;

    // Two orders at same price: SELL 5 @ 10010, then SELL 15 @ 10010
    book.addOrder(Order{.id = 1, .side = Side::Sell, .price = 10010, .quantity = 5}, 1000);
    book.addOrder(Order{.id = 2, .side = Side::Sell, .price = 10010, .quantity = 15}, 1001);

    EXPECT_EQ(book.bestAskQuantity(), 20);

    // Aggressor buys 8 @ 10010: should fill order 1 completely (5) and order 2 partially (3)
    auto trades = book.addOrder(Order{.id = 3, .side = Side::Buy, .price = 10010, .quantity = 8}, 1100);
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].passive_order_id, 1u);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(trades[1].passive_order_id, 2u);
    EXPECT_EQ(trades[1].quantity, 3);

    // Remaining in book: order 2 with 12 quantity
    EXPECT_EQ(book.bestAskQuantity(), 12);
}

TEST(OrderBookTest, OrderCancellation) {
    OrderBook book;
    book.addOrder(Order{.id = 10, .side = Side::Buy, .price = 9990, .quantity = 50}, 1000);
    EXPECT_EQ(book.bestBidQuantity(), 50);

    bool cancelled = book.cancelOrder(10);
    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_EQ(book.bestBidQuantity(), 0);

    // Canceling nonexistent order returns false
    EXPECT_FALSE(book.cancelOrder(999));
}
