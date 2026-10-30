/// @file main.cpp
/// @brief Network Jitter-Aware Quantitative Trading Simulator CLI.

#include "common/timestamp.h"
#include "concurrency/affinity.h"
#include "concurrency/spsc_queue.h"
#include "concurrency/mutex_queue.h"
#include "network/socket_transport.h"
#include "trading_engine/trading_engine.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace njt {

void printBanner() {
    std::cout << R"(
================================================================================
   _  __     __                   __      ___ _   __             ____ 
  / |/ /__  / /__    _____  _____/ /__   / (_) /_/ /____ ____   / __/ 
 /    / -_)/ __/ |/|/ / _ \/ __/  '_/   / / / __/ __/ -_) __/  _\ \   
/_/|_/\__/ \__/|__,__/\___/_/ /_/\_\   /_/_/\__/\__/\__/_/    /___/   
   Network Jitter-Aware Quantitative Trading Simulator v1.0.0
   [HFT / Low-Latency C++20 / Quantitative Market Microstructure]
================================================================================
)" << std::endl;
}

SimulatorConfig getDefaultConfig() {
    SimulatorConfig cfg{};

    // Exchange: 10 seconds of simulated high-frequency market data
    cfg.exchange.initial_mid_price       = 10000;    // $100.00
    cfg.exchange.volatility              = 0.0003;   // per-step OU volatility
    cfg.exchange.mean_reversion_strength = 0.02;
    cfg.exchange.mean_reversion_target   = 10000;
    cfg.exchange.event_interval_ns       = 100'000;  // 100 μs
    cfg.exchange.duration_ns             = 5'000'000'000ULL; // 5 s default
    cfg.exchange.trade_probability       = 0.35;
    cfg.exchange.seed                    = 42;

    // Network downlink (Exchange -> Client market data)
    cfg.network.transport           = TransportMode::UdpLike;
    cfg.network.distribution        = LatencyDistribution::LogNormal;
    cfg.network.base_latency_ns     = 100'000;      // 100 μs base delay
    cfg.network.jitter_stddev_ns    =  50'000;      //  50 μs jitter stddev
    cfg.network.min_latency_ns      =  10'000;      //  10 μs floor
    cfg.network.max_latency_ns      = 10'000'000;   //  10 ms ceiling
    cfg.network.packet_loss_rate    = 0.001;        // 0.1% loss
    cfg.network.reorder_rate        = 0.001;        // 0.1% reorder
    cfg.network.duplication_rate    = 0.0001;
    cfg.network.seed                = 123;

    // Execution uplink (Client -> Exchange order routing)
    cfg.execution.uplink_config = cfg.network;
    cfg.execution.uplink_config.seed = 456;
    cfg.execution.downlink_config = cfg.network;
    cfg.execution.downlink_config.seed = 789;

    // Strategy
    cfg.strategy.threshold_buy    = 0.25;
    cfg.strategy.threshold_sell   = -0.25;
    cfg.strategy.decay_lambda     = 0.5;
    cfg.strategy.depth_levels     = 5;
    cfg.strategy.order_quantity   = 10;
    cfg.strategy.max_position     = 100;
    cfg.strategy.cooldown_ns      = 200'000; // 200 μs
    cfg.strategy.max_spread_ticks = 3;
    cfg.strategy.order_type       = OrderType::Limit;

    // Feed Handler
    cfg.feed_handler.max_gap_before_recovery = 10;
    cfg.feed_handler.stale_threshold_ns       = 2'000'000; // 2 ms

    return cfg;
}

SimulatorConfig loadConfigFromFile(const std::string& path) {
    auto cfg = getDefaultConfig();
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open config file: " << path << ". Using defaults.\n";
        return cfg;
    }

    try {
        json j;
        file >> j;

        if (j.contains("duration_s")) {
            cfg.exchange.duration_ns = static_cast<uint64_t>(j["duration_s"].get<double>() * 1e9);
        }
        if (j.contains("base_latency_us")) {
            cfg.network.base_latency_ns = static_cast<uint64_t>(j["base_latency_us"].get<double>() * 1000.0);
            cfg.execution.uplink_config.base_latency_ns = cfg.network.base_latency_ns;
            cfg.execution.downlink_config.base_latency_ns = cfg.network.base_latency_ns;
        }
        if (j.contains("jitter_stddev_us")) {
            cfg.network.jitter_stddev_ns = static_cast<uint64_t>(j["jitter_stddev_us"].get<double>() * 1000.0);
            cfg.execution.uplink_config.jitter_stddev_ns = cfg.network.jitter_stddev_ns;
            cfg.execution.downlink_config.jitter_stddev_ns = cfg.network.jitter_stddev_ns;
        }
        if (j.contains("packet_loss_rate")) {
            cfg.network.packet_loss_rate = j["packet_loss_rate"].get<double>();
            cfg.execution.uplink_config.packet_loss_rate = cfg.network.packet_loss_rate;
        }
        if (j.contains("reorder_rate")) {
            cfg.network.reorder_rate = j["reorder_rate"].get<double>();
        }
        if (j.contains("threshold_imbalance")) {
            double th = j["threshold_imbalance"].get<double>();
            cfg.strategy.threshold_buy = th;
            cfg.strategy.threshold_sell = -th;
        }
        if (j.contains("seed")) {
            uint64_t s = j["seed"].get<uint64_t>();
            cfg.exchange.seed = s;
            cfg.network.seed = s + 1;
            cfg.execution.uplink_config.seed = s + 2;
            cfg.execution.downlink_config.seed = s + 3;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing JSON config: " << e.what() << "\n";
    }

    return cfg;
}

void printDashboard(const PerformanceMetrics& m, const TradingEngine& engine, double runtime_ms) {
    std::cout << "\n"
              << "+-------------------------------------------------------------------------+\n"
              << "|                       SIMULATION RESULTS DASHBOARD                      |\n"
              << "+-------------------------------------------------------------------------+\n";

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "| Total PnL ($)            : " << std::setw(12) << m.total_pnl
              << " | Realized PnL ($)     : " << std::setw(12) << m.realized_pnl << " |\n";
    std::cout << "| Unrealized PnL ($)       : " << std::setw(12) << m.unrealized_pnl
              << " | Max Drawdown ($)     : " << std::setw(12) << m.max_drawdown << " |\n";
    std::cout << "| Max Drawdown (%)         : " << std::setw(11) << m.max_drawdown_pct << "%"
              << " | Sharpe Ratio         : " << std::setw(12) << m.sharpe_ratio << " |\n";
    std::cout << "| Sortino Ratio            : " << std::setw(12) << m.sortino_ratio
              << " | Win Rate (%)         : " << std::setw(11) << (m.win_rate * 100.0) << "% |\n";
    std::cout << "| Profit Factor            : " << std::setw(12) << m.profit_factor
              << " | Total Volume (lots)  : " << std::setw(12) << m.total_volume << " |\n";

    std::cout << "+-------------------------------------------------------------------------+\n"
              << "|                         EXECUTION & LATENCY METRICS                     |\n"
              << "+-------------------------------------------------------------------------+\n";
    std::cout << "| Total Orders Filled      : " << std::setw(12) << m.total_trades
              << " | Fill Rate (%)        : " << std::setw(11) << (m.fill_rate * 100.0) << "% |\n";
    std::cout << "| Avg Slippage (ticks)     : " << std::setw(12) << m.avg_slippage_ticks
              << " | Avg Slippage (bps)   : " << std::setw(12) << m.avg_slippage_bps << " |\n";
    std::cout << "| Latency p50 (μs)         : " << std::setw(12) << m.latency_p50_us
              << " | Latency p90 (μs)     : " << std::setw(12) << m.latency_p90_us << " |\n";
    std::cout << "| Latency p99 (μs)         : " << std::setw(12) << m.latency_p99_us
              << " | Latency Max (μs)     : " << std::setw(12) << m.latency_max_us << " |\n";

    std::cout << "+-------------------------------------------------------------------------+\n"
              << "|                    ADVERSE SELECTION MARKOUT CURVE                      |\n"
              << "+-------------------------------------------------------------------------+\n";
    std::cout << "| Horizon    :   +100 μs   |    +1 ms    |    +5 ms    |   +10 ms    |   +50 ms    |\n";
    std::cout << "| PnL (ticks): "
              << std::setw(10) << m.markout_curve[0] << " | "
              << std::setw(10) << m.markout_curve[1] << " | "
              << std::setw(10) << m.markout_curve[2] << " | "
              << std::setw(10) << m.markout_curve[3] << " | "
              << std::setw(10) << m.markout_curve[4] << " |\n";

    const auto& net_stats = engine.network().stats();
    const auto& fh_stats  = engine.feedHandler().stats();
    std::cout << "+-------------------------------------------------------------------------+\n"
              << "|                       NETWORK & FEED HANDLER STATS                      |\n"
              << "+-------------------------------------------------------------------------+\n";
    std::cout << "| Packets Sent             : " << std::setw(12) << net_stats.total_sent
              << " | Packets Delivered    : " << std::setw(12) << net_stats.total_delivered << " |\n";
    std::cout << "| Packets Dropped          : " << std::setw(12) << net_stats.total_dropped
              << " | Packets Reordered    : " << std::setw(12) << net_stats.total_reordered << " |\n";
    std::cout << "| Sequence Gaps Detected   : " << std::setw(12) << fh_stats.gaps_detected
              << " | Snapshots Recovered  : " << std::setw(12) << fh_stats.recoveries_triggered << " |\n";
    std::cout << "| Wall-clock Runtime (ms)  : " << std::setw(12) << runtime_ms
              << " | Simulator Throughput : " << std::setw(9) << (net_stats.total_sent / (runtime_ms / 1000.0)) << " msg/s |\n";
    std::cout << "+-------------------------------------------------------------------------+\n" << std::endl;
}

void runBacktestMode(const SimulatorConfig& config, const std::string& out_dir) {
    fs::create_directories(out_dir);

    std::cout << "[*] Running Backtest Simulation...\n";
    std::cout << "    Duration        : " << (config.exchange.duration_ns / 1e9) << " s\n";
    std::cout << "    Base Latency    : " << (config.network.base_latency_ns / 1000.0) << " μs\n";
    std::cout << "    Jitter StdDev   : " << (config.network.jitter_stddev_ns / 1000.0) << " μs\n";
    std::cout << "    Packet Loss     : " << (config.network.packet_loss_rate * 100.0) << " %\n";

    TradingEngine engine(config);

    auto t0 = std::chrono::high_resolution_clock::now();
    PerformanceMetrics m = engine.run();
    auto t1 = std::chrono::high_resolution_clock::now();
    double runtime_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    printDashboard(m, engine, runtime_ms);

    std::string pnl_file     = out_dir + "/pnl_series.csv";
    std::string trades_file  = out_dir + "/trades.csv";
    std::string latency_file = out_dir + "/latency.csv";
    std::string metrics_file = out_dir + "/summary_metrics.json";

    engine.analytics().exportPnLTimeSeries(pnl_file);
    engine.analytics().exportTrades(trades_file);
    engine.analytics().exportLatencySamples(latency_file);
    engine.analytics().exportSummaryMetrics(metrics_file, m);

    std::cout << "[+] Artifacts successfully exported to: " << out_dir << "\n"
              << "    • " << pnl_file << "\n"
              << "    • " << trades_file << "\n"
              << "    • " << latency_file << "\n"
              << "    • " << metrics_file << "\n" << std::endl;
}

void runSweepMode(const SimulatorConfig& base_config, const std::string& out_dir) {
    fs::create_directories(out_dir);

    // Sweep jitter standard deviations from 0 μs to 2000 μs
    const std::vector<uint64_t> jitter_levels_us = {0, 20, 50, 100, 250, 500, 1000, 2000};

    std::cout << "[*] Executing Network Jitter Parameter Sweep (" << jitter_levels_us.size() << " scenarios)...\n";
    std::cout << "+------------+-------------+--------------+--------------+-------------+------------------+\n";
    std::cout << "| Jitter(μs) | TotalPnL($) | Sharpe Ratio | Max Drawdown | Fill Rate % | Slippage (ticks) |\n";
    std::cout << "+------------+-------------+--------------+--------------+-------------+------------------+\n";

    std::string sweep_csv = out_dir + "/jitter_sweep_results.csv";
    std::ofstream out(sweep_csv);
    out << "jitter_us,total_pnl,sharpe_ratio,max_drawdown,fill_rate,avg_slippage_ticks,markout_100us,markout_1ms,markout_5ms,markout_10ms,markout_50ms\n";

    for (uint64_t j_us : jitter_levels_us) {
        SimulatorConfig cfg = base_config;
        cfg.network.jitter_stddev_ns              = j_us * 1000ULL;
        cfg.execution.uplink_config.jitter_stddev_ns   = j_us * 1000ULL;
        cfg.execution.downlink_config.jitter_stddev_ns = j_us * 1000ULL;

        TradingEngine engine(cfg);
        PerformanceMetrics m = engine.run();

        std::cout << "| " << std::setw(10) << j_us
                  << " | " << std::setw(11) << std::fixed << std::setprecision(2) << m.total_pnl
                  << " | " << std::setw(12) << m.sharpe_ratio
                  << " | " << std::setw(12) << m.max_drawdown
                  << " | " << std::setw(10) << (m.fill_rate * 100.0) << "%"
                  << " | " << std::setw(16) << m.avg_slippage_ticks
                  << " |\n";

        out << j_us << ","
            << m.total_pnl << ","
            << m.sharpe_ratio << ","
            << m.max_drawdown << ","
            << m.fill_rate << ","
            << m.avg_slippage_ticks << ","
            << m.markout_curve[0] << ","
            << m.markout_curve[1] << ","
            << m.markout_curve[2] << ","
            << m.markout_curve[3] << ","
            << m.markout_curve[4] << "\n";
    }

    std::cout << "+------------+-------------+--------------+--------------+-------------+------------------+\n";
    std::cout << "[+] Parameter sweep complete. Results written to: " << sweep_csv << "\n\n";
}

void runConcurrencyBenchmark(const std::string& out_dir) {
    fs::create_directories(out_dir);

    std::cout << "[*] Executing Lock-Free SPSC vs Mutex-Protected Ring Buffer Benchmark...\n";
    std::cout << "    Comparing L1/L2 cache-isolated SPSCQueue vs std::mutex + condition_variable.\n\n";

    constexpr size_t kItems = 2'000'000;
    constexpr size_t kCap   = 4096;

    // ── Test 1: Lock-Free SPSC Queue ──
    {
        SPSCQueue<uint64_t, kCap> spsc;
        std::atomic<bool> start_flag{false};

        auto t0 = std::chrono::high_resolution_clock::now();

        std::thread producer([&]() {
            pinCurrentThreadToCore(0);
            while (!start_flag.load(std::memory_order_acquire)) {}

            for (uint64_t i = 0; i < kItems; ++i) {
                while (!spsc.push(i)) {
                    std::this_thread::yield();
                }
            }
        });

        std::thread consumer([&]() {
            pinCurrentThreadToCore(1);
            while (!start_flag.load(std::memory_order_acquire)) {}

            size_t received = 0;
            while (received < kItems) {
                auto val = spsc.pop();
                if (val) {
                    ++received;
                } else {
                    std::this_thread::yield();
                }
            }
        });

        start_flag.store(true, std::memory_order_release);
        producer.join();
        consumer.join();

        auto t1 = std::chrono::high_resolution_clock::now();
        double spsc_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double spsc_mops = (static_cast<double>(kItems) / (spsc_ms / 1000.0)) / 1e6;

        std::cout << "  [Lock-Free SPSC Queue]\n";
        std::cout << "    Total Items  : " << kItems << "\n";
        std::cout << "    Elapsed Time : " << std::fixed << std::setprecision(2) << spsc_ms << " ms\n";
        std::cout << "    Throughput   : " << std::setprecision(3) << spsc_mops << " Million ops/sec\n";
        std::cout << "    Avg Latency  : " << (spsc_ms * 1e6 / static_cast<double>(kItems)) << " ns/op\n\n";

        // ── Test 2: Mutex Queue ──
        MutexQueue<uint64_t> mtx_q(kCap);
        start_flag.store(false, std::memory_order_release);

        auto t2 = std::chrono::high_resolution_clock::now();

        std::thread mtx_producer([&]() {
            pinCurrentThreadToCore(0);
            while (!start_flag.load(std::memory_order_acquire)) {}

            for (uint64_t i = 0; i < kItems; ++i) {
                while (!mtx_q.push(i)) {
                    std::this_thread::yield();
                }
            }
        });

        std::thread mtx_consumer([&]() {
            pinCurrentThreadToCore(1);
            while (!start_flag.load(std::memory_order_acquire)) {}

            size_t received = 0;
            while (received < kItems) {
                auto val = mtx_q.pop();
                if (val) {
                    ++received;
                } else {
                    std::this_thread::yield();
                }
            }
        });

        start_flag.store(true, std::memory_order_release);
        mtx_producer.join();
        mtx_consumer.join();

        auto t3 = std::chrono::high_resolution_clock::now();
        double mtx_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
        double mtx_mops = (static_cast<double>(kItems) / (mtx_ms / 1000.0)) / 1e6;

        std::cout << "  [Mutex-Protected Queue]\n";
        std::cout << "    Total Items  : " << kItems << "\n";
        std::cout << "    Elapsed Time : " << std::fixed << std::setprecision(2) << mtx_ms << " ms\n";
        std::cout << "    Throughput   : " << std::setprecision(3) << mtx_mops << " Million ops/sec\n";
        std::cout << "    Avg Latency  : " << (mtx_ms * 1e6 / static_cast<double>(kItems)) << " ns/op\n\n";

        double speedup = mtx_ms / spsc_ms;
        std::cout << "  ======================================================\n";
        std::cout << "  Speedup: Lock-Free SPSC is " << std::setprecision(2) << speedup
                  << "x FASTER than Mutex Queue!\n";
        std::cout << "  ======================================================\n\n";

        std::string bench_csv = out_dir + "/concurrency_benchmark.csv";
        std::ofstream out(bench_csv);
        out << "queue_type,items,time_ms,mops_per_sec,avg_latency_ns\n";
        out << "SPSCQueue," << kItems << "," << spsc_ms << "," << spsc_mops << "," << (spsc_ms * 1e6 / kItems) << "\n";
        out << "MutexQueue," << kItems << "," << mtx_ms << "," << mtx_mops << "," << (mtx_ms * 1e6 / kItems) << "\n";
    }
}

void runRealNetMode() {
    std::cout << "[*] Initializing Real Loopback UDP Network Latency Test (127.0.0.1:9876)...\n";

    constexpr uint16_t kPort = 9876;
    constexpr size_t kPackets = 1000;

    std::atomic<bool> receiver_ready{false};
    std::atomic<bool> done{false};
    std::vector<double> latencies_us;

    std::thread receiver([&]() {
        UdpSocket rx_sock;
        if (!rx_sock.bind(kPort)) {
            std::cerr << "Failed to bind receiver socket to port " << kPort << "\n";
            receiver_ready.store(true);
            return;
        }
        receiver_ready.store(true, std::memory_order_release);

        char buf[256];
        size_t count = 0;
        while (count < kPackets) {
            int bytes = rx_sock.receiveFrom(buf, sizeof(buf), 100'000); // 100 ms timeout
            if (bytes >= static_cast<int>(sizeof(Timestamp))) {
                Timestamp send_time = 0;
                std::memcpy(&send_time, buf, sizeof(send_time));
                Timestamp now_t = SimClock::now();
                if (now_t >= send_time) {
                    latencies_us.push_back(static_cast<double>(now_t - send_time) / 1000.0);
                }
                ++count;
            }
        }
        done.store(true, std::memory_order_release);
    });

    while (!receiver_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    std::thread sender([&]() {
        UdpSocket tx_sock;
        for (size_t i = 0; i < kPackets; ++i) {
            Timestamp send_time = SimClock::now();
            tx_sock.sendTo("127.0.0.1", kPort, &send_time, sizeof(send_time));
            // Microsecond pacing
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    sender.join();
    receiver.join();

    if (!latencies_us.empty()) {
        std::sort(latencies_us.begin(), latencies_us.end());
        double p50 = latencies_us[latencies_us.size() * 50 / 100];
        double p90 = latencies_us[latencies_us.size() * 90 / 100];
        double p99 = latencies_us[latencies_us.size() * 99 / 100];
        double max_lat = latencies_us.back();

        std::cout << "  [Real UDP Loopback Results (" << latencies_us.size() << " packets)]\n";
        std::cout << "    p50 Latency : " << std::fixed << std::setprecision(2) << p50 << " μs\n";
        std::cout << "    p90 Latency : " << p90 << " μs\n";
        std::cout << "    p99 Latency : " << p99 << " μs\n";
        std::cout << "    Max Latency : " << max_lat << " μs\n\n";
    }
}

} // namespace njt

int main(int argc, char* argv[]) {
    njt::printBanner();

    std::string mode      = "backtest";
    std::string config_file;
    std::string out_dir   = "results";
    double override_jitter_us = -1.0;
    double override_loss      = -1.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (arg == "--jitter-us" && i + 1 < argc) {
            override_jitter_us = std::stod(argv[++i]);
        } else if (arg == "--loss-rate" && i + 1 < argc) {
            override_loss = std::stod(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: simulator_main [OPTIONS]\n"
                      << "Options:\n"
                      << "  --mode <backtest|sweep|concurrency|realnet>  Run mode (default: backtest)\n"
                      << "  --config <path>                             JSON config path\n"
                      << "  --output-dir <path>                         Output directory for CSVs (default: results)\n"
                      << "  --jitter-us <value>                         Override jitter stddev in μs\n"
                      << "  --loss-rate <value>                         Override packet loss rate (e.g. 0.01)\n";
            return 0;
        }
    }

    njt::SimulatorConfig cfg = config_file.empty()
                                   ? njt::getDefaultConfig()
                                   : njt::loadConfigFromFile(config_file);

    if (override_jitter_us >= 0.0) {
        cfg.network.jitter_stddev_ns              = static_cast<uint64_t>(override_jitter_us * 1000.0);
        cfg.execution.uplink_config.jitter_stddev_ns   = cfg.network.jitter_stddev_ns;
        cfg.execution.downlink_config.jitter_stddev_ns = cfg.network.jitter_stddev_ns;
    }
    if (override_loss >= 0.0) {
        cfg.network.packet_loss_rate              = override_loss;
        cfg.execution.uplink_config.packet_loss_rate = override_loss;
    }

    if (mode == "backtest") {
        njt::runBacktestMode(cfg, out_dir);
    } else if (mode == "sweep") {
        njt::runSweepMode(cfg, out_dir);
    } else if (mode == "concurrency") {
        njt::runConcurrencyBenchmark(out_dir);
    } else if (mode == "realnet") {
        njt::runRealNetMode();
    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }

    return 0;
}
