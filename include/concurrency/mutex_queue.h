#pragma once
/// @file mutex_queue.h
/// @brief Thread-safe MPMC queue (mutex + condition variable).
///
/// Used as a baseline to compare against SPSCQueue in concurrency experiments.

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace njt {

template <typename T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t max_size = 65536) : max_size_(max_size) {}

    bool push(T item) {
        std::lock_guard lk(mutex_);
        if (queue_.size() >= max_size_) return false;
        queue_.push(std::move(item));
        cv_.notify_one();
        return true;
    }

    /// Non-blocking pop.
    std::optional<T> pop() {
        std::lock_guard lk(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    /// Blocking pop with timeout.
    std::optional<T> waitPop(std::chrono::microseconds timeout) {
        std::unique_lock lk(mutex_);
        if (!cv_.wait_for(lk, timeout, [this] { return !queue_.empty(); }))
            return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lk(mutex_);
        return queue_.empty();
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::queue<T>           queue_;
    std::size_t             max_size_;
};

} // namespace njt
