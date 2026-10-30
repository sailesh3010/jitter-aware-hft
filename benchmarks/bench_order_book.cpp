#include <benchmark/benchmark.h>
#include "exchange/order_book.h"

using namespace njt;

static void BM_OrderBook_InsertLimitOrder(benchmark::State& state) {
    OrderBook book;
    OrderId id = 1;

    for (auto _ : state) {
        Order ord{
            .id = id++,
            .side = (id % 2 == 0) ? Side::Buy : Side::Sell,
            .type = OrderType::Limit,
            .price = (id % 2 == 0) ? (10000 - static_cast<Price>(id % 5)) : (10002 + static_cast<Price>(id % 5)),
            .quantity = 10,
        };
        benchmark::DoNotOptimize(book.addOrder(ord, 1000));
    }
}
BENCHMARK(BM_OrderBook_InsertLimitOrder);

static void BM_OrderBook_CancelOrder(benchmark::State& state) {
    OrderBook book;
    OrderId id = 1;

    for (auto _ : state) {
        state.PauseTiming();
        Order ord{.id = id, .side = Side::Buy, .price = 10000, .quantity = 10};
        book.addOrder(ord, 1000);
        state.ResumeTiming();

        benchmark::DoNotOptimize(book.cancelOrder(id));
        ++id;
    }
}
BENCHMARK(BM_OrderBook_CancelOrder);

static void BM_OrderBook_TopAndDepth(benchmark::State& state) {
    OrderBook book;
    for (int i = 0; i < 5; ++i) {
        book.addOrder(Order{.id = static_cast<OrderId>(i + 1), .side = Side::Buy, .price = 10000 - i, .quantity = 20}, 1000);
        book.addOrder(Order{.id = static_cast<OrderId>(i + 10), .side = Side::Sell, .price = 10002 + i, .quantity = 20}, 1000);
    }

    for (auto _ : state) {
        auto tob = book.topOfBook();
        auto depth = book.getBidDepth(5);
        benchmark::DoNotOptimize(tob);
        benchmark::DoNotOptimize(depth);
    }
}
BENCHMARK(BM_OrderBook_TopAndDepth);
