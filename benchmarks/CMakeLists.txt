add_executable(micro_benchmarks
    bench_order_book.cpp
    bench_spsc_vs_mutex.cpp
    bench_feed_handler.cpp
)

target_link_libraries(micro_benchmarks PRIVATE
    njt_core
    benchmark::benchmark
    benchmark::benchmark_main
)
