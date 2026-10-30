#include <gtest/gtest.h>
#include "strategy/order_book_imbalance_strategy.h"

using namespace njt;

TEST(StrategyTest, ImbalanceCalculation) {
    ImbalanceStrategyConfig cfg;
    cfg.depth_levels = 2;
    cfg.decay_lambda = 0.0; // uniform weighting for easy checking
    OrderBookImbalanceStrategy strat(cfg);

    MarketUpdate u;
    // Bids: 60 @ 100, 40 @ 99 -> total bid = 100
    u.bids[0] = {10000, 60};
    u.bids[1] = {9999, 40};
    u.bid_levels = 2;

    // Asks: 20 @ 101, 30 @ 102 -> total ask = 50
    u.asks[0] = {10001, 20};
    u.asks[1] = {10002, 30};
    u.ask_levels = 2;

    // Imbalance = (100 - 50) / (100 + 50) = 50 / 150 = 0.3333...
    double imb = strat.calculateImbalance(u);
    EXPECT_NEAR(imb, 0.3333, 0.001);

    auto sig = strat.onMarketUpdate(u, 1000);
    ASSERT_TRUE(sig.has_value());
    EXPECT_EQ(sig->type, SignalType::Buy);
    EXPECT_EQ(sig->target_price, 10000); // joins best bid
}

TEST(StrategyTest, CooldownEnforcement) {
    ImbalanceStrategyConfig cfg;
    cfg.cooldown_ns = 1'000'000; // 1 ms cooldown
    cfg.threshold_buy = 0.2;
    OrderBookImbalanceStrategy strat(cfg);

    MarketUpdate u;
    u.bids[0] = {10000, 100};
    u.bid_levels = 1;
    u.asks[0] = {10001, 10};
    u.ask_levels = 1;

    // First signal at t = 1,000,000
    auto sig1 = strat.onMarketUpdate(u, 1'000'000);
    EXPECT_TRUE(sig1.has_value());

    // Immediate second update at t = 1,200,000 (only 200 μs later)
    auto sig2 = strat.onMarketUpdate(u, 1'200'000);
    EXPECT_FALSE(sig2.has_value()); // blocked by cooldown!

    // Third update after cooldown: t = 2,500,000 (1.5 ms later)
    auto sig3 = strat.onMarketUpdate(u, 2'500'000);
    EXPECT_TRUE(sig3.has_value());
}
