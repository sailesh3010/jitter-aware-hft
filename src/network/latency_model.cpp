/// @file latency_model.cpp
/// @brief Latency sampling and impairment decision logic.

#include "network/latency_model.h"
#include <algorithm>
#include <cmath>

namespace njt {

LatencyModel::LatencyModel(const NetworkConfig& config)
    : config_(config)
    , rng_(config.seed)
{
    // Normal distribution centred on base latency
    double mean  = static_cast<double>(config_.base_latency_ns);
    double sigma = static_cast<double>(config_.jitter_stddev_ns);
    normal_ = std::normal_distribution<double>(mean, sigma);

    // Log-normal: choose μ_ln, σ_ln so that the *median* of the log-normal
    // equals base_latency_ns and the spread matches jitter_stddev_ns.
    //   median = exp(μ_ln)                → μ_ln = ln(base)
    //   mode   ≈ exp(μ_ln − σ_ln²)
    // We derive σ_ln from the requested coefficient of variation.
    if (mean > 0 && sigma > 0) {
        double cv      = sigma / mean;
        double sigma_ln = std::sqrt(std::log(1.0 + cv * cv));
        double mu_ln    = std::log(mean) - 0.5 * sigma_ln * sigma_ln;
        lognormal_ = std::lognormal_distribution<double>(mu_ln, sigma_ln);
    } else {
        lognormal_ = std::lognormal_distribution<double>(std::log(std::max(mean, 1.0)), 0.1);
    }
}

Timestamp LatencyModel::sampleLatency() {
    if (config_.distribution == LatencyDistribution::Constant || config_.jitter_stddev_ns == 0) {
        return config_.base_latency_ns;
    }

    double raw = 0.0;

    switch (config_.distribution) {
        case LatencyDistribution::Constant:
            return config_.base_latency_ns;

        case LatencyDistribution::Uniform: {
            std::uniform_real_distribution<double> u(
                static_cast<double>(config_.min_latency_ns),
                static_cast<double>(config_.max_latency_ns));
            raw = u(rng_);
            break;
        }
        case LatencyDistribution::Normal:
            raw = normal_(rng_);
            break;

        case LatencyDistribution::LogNormal:
            raw = lognormal_(rng_);
            break;
    }

    // Truncate to [min, max]
    raw = std::clamp(raw,
                     static_cast<double>(config_.min_latency_ns),
                     static_cast<double>(config_.max_latency_ns));

    return static_cast<Timestamp>(raw);
}

bool LatencyModel::shouldDrop() {
    // Check burst loss first
    if (in_burst_) {
        if (burst_remaining_ > 0) {
            --burst_remaining_;
            if (burst_remaining_ == 0) in_burst_ = false;
            return true;
        }
        in_burst_ = false;
    }

    // Start new burst?
    if (uniform_(rng_) < config_.burst_loss_probability) {
        std::uniform_int_distribution<uint32_t> burst_len(1, config_.burst_loss_max_length);
        burst_remaining_ = burst_len(rng_);
        in_burst_        = true;
        --burst_remaining_;
        return true;
    }

    // Independent drop
    return uniform_(rng_) < config_.packet_loss_rate;
}

bool LatencyModel::shouldDuplicate() {
    return uniform_(rng_) < config_.duplication_rate;
}

bool LatencyModel::shouldReorder() {
    return uniform_(rng_) < config_.reorder_rate;
}

uint32_t LatencyModel::burstLossLength() {
    if (uniform_(rng_) < config_.burst_loss_probability) {
        std::uniform_int_distribution<uint32_t> d(1, config_.burst_loss_max_length);
        return d(rng_);
    }
    return 0;
}

void LatencyModel::reset(uint64_t seed) {
    rng_.seed(seed);
    in_burst_        = false;
    burst_remaining_ = 0;
}

} // namespace njt
