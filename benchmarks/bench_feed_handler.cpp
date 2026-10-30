#include <benchmark/benchmark.h>
#include "market_data/feed_handler.h"

using namespace njt;

static void BM_FeedHandler_SequentialUpdate(benchmark::State& state) {
    FeedHandler fh;
    MarketUpdate u;
    u.bids[0] = {10000, 50};
    u.asks[0] = {10001, 50};
    u.bid_levels = 1;
    u.ask_levels = 1;

    SequenceNumber seq = 1;
    Timestamp t = 1000;

    for (auto _ : state) {
        u.sequence = seq++;
        u.exchange_timestamp = t += 1000;
        benchmark::DoNotOptimize(fh.processMessage(u, t + 100));
    }
}
BENCHMARK(BM_FeedHandler_SequentialUpdate);
