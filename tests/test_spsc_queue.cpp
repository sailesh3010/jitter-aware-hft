#include <gtest/gtest.h>
#include "concurrency/spsc_queue.h"
#include <thread>
#include <vector>

using namespace njt;

TEST(SPSCQueueTest, SingleThreadedPushPop) {
    SPSCQueue<int, 4> q; // capacity = 3 usable

    EXPECT_TRUE(q.empty());
    EXPECT_TRUE(q.push(10));
    EXPECT_TRUE(q.push(20));
    EXPECT_TRUE(q.push(30));
    EXPECT_FALSE(q.push(40)); // full!

    EXPECT_EQ(q.pop(), 10);
    EXPECT_EQ(q.pop(), 20);
    EXPECT_TRUE(q.push(50));

    EXPECT_EQ(q.pop(), 30);
    EXPECT_EQ(q.pop(), 50);
    EXPECT_EQ(q.pop(), std::nullopt); // empty!
}

TEST(SPSCQueueTest, MultiThreadedConcurrentIntegrity) {
    constexpr size_t kItems = 200'000;
    SPSCQueue<uint64_t, 1024> q;

    std::thread producer([&]() {
        for (uint64_t i = 1; i <= kItems; ++i) {
            while (!q.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::vector<uint64_t> received;
    received.reserve(kItems);

    std::thread consumer([&]() {
        while (received.size() < kItems) {
            auto val = q.pop();
            if (val) {
                received.push_back(*val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), kItems);
    for (size_t i = 0; i < kItems; ++i) {
        ASSERT_EQ(received[i], i + 1);
    }
}
