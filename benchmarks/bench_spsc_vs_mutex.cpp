#include <benchmark/benchmark.h>
#include "concurrency/spsc_queue.h"
#include "concurrency/mutex_queue.h"
#include <thread>
#include <atomic>

using namespace njt;

static void BM_SPSCQueue_Throughput(benchmark::State& state) {
    constexpr size_t kCap = 4096;
    SPSCQueue<uint64_t, kCap> queue;
    std::atomic<bool> done{false};

    std::thread consumer([&]() {
        uint64_t count = 0;
        while (!done.load(std::memory_order_relaxed)) {
            if (queue.pop()) ++count;
        }
        while (queue.pop()) ++count;
        benchmark::DoNotOptimize(count);
    });

    uint64_t val = 1;
    for (auto _ : state) {
        while (!queue.push(val++)) {
            // spin
        }
    }

    done.store(true, std::memory_order_relaxed);
    consumer.join();
}
BENCHMARK(BM_SPSCQueue_Throughput);

static void BM_MutexQueue_Throughput(benchmark::State& state) {
    constexpr size_t kCap = 4096;
    MutexQueue<uint64_t> queue(kCap);
    std::atomic<bool> done{false};

    std::thread consumer([&]() {
        uint64_t count = 0;
        while (!done.load(std::memory_order_relaxed)) {
            if (queue.pop()) ++count;
        }
        while (queue.pop()) ++count;
        benchmark::DoNotOptimize(count);
    });

    uint64_t val = 1;
    for (auto _ : state) {
        while (!queue.push(val++)) {
            // spin
        }
    }

    done.store(true, std::memory_order_relaxed);
    consumer.join();
}
BENCHMARK(BM_MutexQueue_Throughput);
