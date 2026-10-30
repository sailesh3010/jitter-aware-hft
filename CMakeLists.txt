cmake_minimum_required(VERSION 3.20)
project(NetworkJitterTradingSimulator
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Network Jitter-Aware Quantitative Trading Simulator"
)

# ── C++20 Standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Build Type Defaults ─────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Options ─────────────────────────────────────────────────────────────────
option(NJT_BUILD_TESTS       "Build unit tests"                      ON)
option(NJT_BUILD_BENCHMARKS  "Build micro-benchmarks"                ON)
option(NJT_BUILD_NETWORKING  "Build real TCP/UDP networking module"   ON)

# ── Third-party Dependencies (FetchContent) ─────────────────────────────────
include(FetchContent)

# nlohmann/json – config file parsing
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(json)

# ── Include Paths ───────────────────────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)

# ── Core Library + Main Executable ──────────────────────────────────────────
add_subdirectory(src)

# ── Tests ───────────────────────────────────────────────────────────────────
if(NJT_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    add_subdirectory(tests)
endif()

# ── Benchmarks ──────────────────────────────────────────────────────────────
if(NJT_BUILD_BENCHMARKS)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.3
        GIT_SHALLOW    TRUE
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(benchmarks)
endif()
