#pragma once
/// @file spsc_queue.h
/// @brief Lock-free single-producer single-consumer ring buffer.
///
/// ## Design
/// Classic Lamport-style ring buffer with:
///   • Power-of-two capacity for bit-mask indexing.
///   • Cached remote counters to reduce cross-core cache-line traffic.
///   • Separate cache-line alignment for producer and consumer state
///     to eliminate false sharing.
///
/// ## Memory ordering
///   • Producer: relaxed load of write_pos, acquire load of read_pos
///     (only when local cache is stale), release store of write_pos.
///   • Consumer: mirror of the above.
///
/// Usable capacity is Capacity − 1 (one slot sacrificed to distinguish
/// full from empty).

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace njt {

// Standard 64-byte L1 cache line size to prevent false sharing across CPU cores.
inline constexpr std::size_t kCacheLineSize = 64;

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(Capacity >= 2,                     "Capacity must be ≥ 2");
    static_assert((Capacity & (Capacity - 1)) == 0,  "Capacity must be a power of 2");

public:
    SPSCQueue() = default;

    // Non-copyable, non-movable (atomics are not movable)
    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    /// Push (producer side).  Returns false if full.
    bool push(const T& item) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        const std::size_t w    = write_pos_.load(std::memory_order_relaxed);
        const std::size_t next = (w + 1) & kMask;

        if (next == read_cache_) {
            read_cache_ = read_pos_.load(std::memory_order_acquire);
            if (next == read_cache_) return false;   // full
        }

        buffer_[w] = item;
        write_pos_.store(next, std::memory_order_release);
        return true;
    }

    bool push(T&& item) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const std::size_t w    = write_pos_.load(std::memory_order_relaxed);
        const std::size_t next = (w + 1) & kMask;

        if (next == read_cache_) {
            read_cache_ = read_pos_.load(std::memory_order_acquire);
            if (next == read_cache_) return false;
        }

        buffer_[w] = std::move(item);
        write_pos_.store(next, std::memory_order_release);
        return true;
    }

    /// Pop (consumer side).  Returns std::nullopt if empty.
    std::optional<T> pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        const std::size_t r = read_pos_.load(std::memory_order_relaxed);

        if (r == write_cache_) {
            write_cache_ = write_pos_.load(std::memory_order_acquire);
            if (r == write_cache_) return std::nullopt;  // empty
        }

        T item = std::move(buffer_[r]);
        read_pos_.store((r + 1) & kMask, std::memory_order_release);
        return item;
    }

    /// Approximate emptiness check (safe to call from either side).
    [[nodiscard]] bool empty() const noexcept {
        return read_pos_.load(std::memory_order_acquire) ==
               write_pos_.load(std::memory_order_acquire);
    }

    /// Approximate size.
    [[nodiscard]] std::size_t size() const noexcept {
        auto w = write_pos_.load(std::memory_order_acquire);
        auto r = read_pos_.load(std::memory_order_acquire);
        return (w - r + Capacity) & kMask;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // ── Producer state (cache-line isolated) ──
    alignas(kCacheLineSize) std::atomic<std::size_t> write_pos_{0};
    alignas(kCacheLineSize) std::size_t              read_cache_{0};

    // ── Consumer state (cache-line isolated) ──
    alignas(kCacheLineSize) std::atomic<std::size_t> read_pos_{0};
    alignas(kCacheLineSize) std::size_t              write_cache_{0};

    // ── Data buffer ──
    alignas(kCacheLineSize) std::array<T, Capacity>  buffer_{};
};

} // namespace njt
