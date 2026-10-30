#include <gtest/gtest.h>
#include "market_data/feed_handler.h"

using namespace njt;

TEST(FeedHandlerTest, SequentialProcessing) {
    FeedHandler fh;

    MarketUpdate u1;
    u1.sequence = 1;
    u1.exchange_timestamp = 1000;
    u1.bids[0] = {10000, 50};
    u1.bid_levels = 1;

    EXPECT_TRUE(fh.processMessage(u1, 1050));
    EXPECT_EQ(fh.currentState().sequence, 1u);
    EXPECT_EQ(fh.currentState().bids[0].price, 10000);

    // Duplicate message
    EXPECT_FALSE(fh.processMessage(u1, 1100));
    EXPECT_EQ(fh.stats().duplicates_detected, 1u);
}

TEST(FeedHandlerTest, ReorderingAndBuffering) {
    FeedHandler fh;

    // Send sequence 2 before sequence 1
    MarketUpdate u2;
    u2.sequence = 2;
    u2.exchange_timestamp = 2000;

    EXPECT_FALSE(fh.processMessage(u2, 2050)); // buffered
    EXPECT_EQ(fh.currentState().sequence, 0u);
    EXPECT_EQ(fh.stats().gaps_detected, 1u);

    // Now send sequence 1: should process 1 and automatically drain 2!
    MarketUpdate u1;
    u1.sequence = 1;
    u1.exchange_timestamp = 1000;

    EXPECT_TRUE(fh.processMessage(u1, 2100));
    EXPECT_EQ(fh.currentState().sequence, 2u); // drained up to sequence 2
    EXPECT_EQ(fh.stats().reorders_handled, 1u);
}

TEST(FeedHandlerTest, GapRecoveryTrigger) {
    FeedHandler::Config cfg;
    cfg.max_gap_before_recovery = 3;
    FeedHandler fh(cfg);

    // Skip sequence 1..5, send sequence 6
    MarketUpdate u6;
    u6.sequence = 6;
    u6.exchange_timestamp = 6000;

    fh.processMessage(u6, 6100);
    EXPECT_TRUE(fh.needsRecovery());

    // Apply snapshot
    MarketUpdate snap;
    snap.sequence = 10;
    snap.exchange_timestamp = 10000;
    fh.applySnapshot(snap);

    EXPECT_FALSE(fh.needsRecovery());
    EXPECT_EQ(fh.currentState().sequence, 10u);
}
