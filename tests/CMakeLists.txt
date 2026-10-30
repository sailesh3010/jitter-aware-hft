add_executable(unit_tests
    test_order_book.cpp
    test_network.cpp
    test_feed_handler.cpp
    test_spsc_queue.cpp
    test_strategy.cpp
    test_execution.cpp
)

target_link_libraries(unit_tests PRIVATE
    njt_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(unit_tests)
