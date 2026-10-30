# ── Core Static Library ──────────────────────────────────────────────────────
add_library(njt_core STATIC
    exchange/order_book.cpp
    exchange/exchange_simulator.cpp
    network/latency_model.cpp
    network/network_simulator.cpp
    network/socket_transport.cpp
    market_data/feed_handler.cpp
    strategy/order_book_imbalance_strategy.cpp
    execution/execution_simulator.cpp
    analytics/performance_analyzer.cpp
    trading_engine/trading_engine.cpp
)

target_include_directories(njt_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(njt_core PUBLIC
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(njt_core PUBLIC ws2_32)
else()
    target_link_libraries(njt_core PUBLIC pthread)
endif()

# ── Main Executable ──────────────────────────────────────────────────────────
add_executable(simulator_main main.cpp)
target_link_libraries(simulator_main PRIVATE njt_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(njt_core PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
    target_compile_options(simulator_main PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()
