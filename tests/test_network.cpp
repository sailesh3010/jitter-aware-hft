#include <gtest/gtest.h>
#include "network/network_simulator.h"

using namespace njt;

TEST(NetworkSimulatorTest, LatencySamplingBounds) {
    NetworkConfig config;
    config.distribution    = LatencyDistribution::Normal;
    config.base_latency_ns = 100'000;
    config.jitter_stddev_ns= 20'000;
    config.min_latency_ns  = 50'000;
    config.max_latency_ns  = 200'000;
    config.packet_loss_rate= 0.0;
    config.seed            = 42;

    LatencyModel model(config);

    for (int i = 0; i < 1000; ++i) {
        Timestamp lat = model.sampleLatency();
        EXPECT_GE(lat, config.min_latency_ns);
        EXPECT_LE(lat, config.max_latency_ns);
    }
}

TEST(NetworkSimulatorTest, DeterministicDelivery) {
    NetworkConfig config;
    config.transport        = TransportMode::UdpLike;
    config.base_latency_ns  = 50'000;
    config.jitter_stddev_ns = 0; // constant latency
    config.packet_loss_rate = 0.0;
    config.reorder_rate     = 0.0;
    config.duplication_rate = 0.0;

    NetworkSimulator net(config);

    NetworkMessage msg;
    msg.sequence = 1;
    msg.market_update.exchange_timestamp = 100'000;

    net.send(msg);

    // At t = 120,000 ns, message should not be delivered yet (needs 50,000 ns delay -> 150,000 ns)
    auto d1 = net.receive(120'000);
    EXPECT_TRUE(d1.empty());

    // At t = 150,000 ns, message should arrive
    auto d2 = net.receive(150'000);
    ASSERT_EQ(d2.size(), 1u);
    EXPECT_EQ(d2[0].sequence, 1u);
}
