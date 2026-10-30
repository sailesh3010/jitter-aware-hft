#include <gtest/gtest.h>
#include "execution/execution_simulator.h"
#include "exchange/exchange_simulator.h"

using namespace njt;

TEST(ExecutionTest, UplinkDeliveryAndFill) {
    ExecutionConfig exec_cfg;
    exec_cfg.uplink_config.base_latency_ns  = 50'000;
    exec_cfg.uplink_config.jitter_stddev_ns = 0; // constant
    exec_cfg.uplink_config.packet_loss_rate = 0.0;
    exec_cfg.downlink_config = exec_cfg.uplink_config;

    ExecutionSimulator exec(exec_cfg);

    ExchangeConfig ex_cfg;
    ex_cfg.initial_mid_price = 10000;
    ExchangeSimulator exchange(ex_cfg);

    Order order{
        .id = 1001,
        .side = Side::Buy,
        .type = OrderType::Market,
        .quantity = 10,
    };

    // Client submits at t = 100,000 ns
    exec.submitOrder(order, 100'000, 10001);

    // At t = 120,000 ns: not yet arrived at exchange (needs 50,000 ns uplink -> 150,000 ns)
    exec.process(120'000, exchange);
    EXPECT_EQ(exec.totalOrdersFilled(), 0u);

    // At t = 150,000 ns: arrived and matched!
    exec.process(150'000, exchange);
    EXPECT_EQ(exec.totalOrdersFilled(), 1u);

    // At t = 200,000 ns: downlink ack arrives back at client (150,000 + 50,000 = 200,000)
    auto reports = exec.receiveReports(200'000);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].order_id, 1001u);
    EXPECT_EQ(reports[0].status, OrderStatus::Filled);
}

TEST(ExecutionTest, MultiHorizonMarkouts) {
    ExecutionConfig exec_cfg;
    exec_cfg.uplink_config.base_latency_ns  = 0;
    exec_cfg.uplink_config.jitter_stddev_ns = 0;
    exec_cfg.uplink_config.packet_loss_rate = 0.0;
    exec_cfg.downlink_config = exec_cfg.uplink_config;

    ExecutionSimulator exec(exec_cfg);

    ExchangeConfig ex_cfg;
    ex_cfg.initial_mid_price = 10000;
    ExchangeSimulator exchange(ex_cfg);

    Order order{
        .id = 2001,
        .side = Side::Buy,
        .type = OrderType::Market,
        .quantity = 10,
    };

    exec.submitOrder(order, 10'000, 10001);
    exec.process(10'000, exchange);
    ASSERT_EQ(exec.totalOrdersFilled(), 1u);

    // Horizon 0 is +100 μs (+100,000 ns). At t = 50,000: not ready
    exec.updateMarkouts(50'000, 10005);
    EXPECT_FALSE(exec.fills()[0].markout_ready[0]);

    // At t = 110,000 ns: markout ready!
    exec.updateMarkouts(110'000, 10005);
    EXPECT_TRUE(exec.fills()[0].markout_ready[0]);
    EXPECT_EQ(exec.fills()[0].markout_mid_prices[0], 10005);
}
