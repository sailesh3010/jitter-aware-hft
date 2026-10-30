#pragma once
/// @file affinity.h
/// @brief Cross-platform OS thread affinity and CPU pinning utilities.
///
/// Essential for low-latency quant trading to:
///   1. Pin market data, strategy, and execution threads to dedicated CPU cores.
///   2. Prevent OS context switching overhead.
///   3. Maximize L1/L2 CPU cache residency.

#include <cstdint>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
#endif

namespace njt {

/// Pin the calling thread to a specific CPU core index.
inline bool pinCurrentThreadToCore(uint32_t core_id) noexcept {
#if defined(_WIN32) || defined(_WIN64)
    if (core_id >= sizeof(DWORD_PTR) * 8) return false;
    DWORD_PTR mask = static_cast<DWORD_PTR>(1ULL << core_id);
    HANDLE thread  = GetCurrentThread();
    DWORD_PTR res  = SetThreadAffinityMask(thread, mask);
    return res != 0;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<int>(core_id), &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    return rc == 0;
#else
    // Fallback: unsupported platform
    (void)core_id;
    return false;
#endif
}

/// Set the calling thread to high priority.
inline bool setThreadHighPriority() noexcept {
#if defined(_WIN32) || defined(_WIN64)
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0;
#elif defined(__linux__)
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
#else
    return false;
#endif
}

} // namespace njt
