#pragma once
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <cstdint>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#include <immintrin.h>
#if defined(_MSC_VER)
#pragma comment(lib, "winmm.lib") // timeBeginPeriod
#endif

namespace rbrew_host_timing {
// Ensure 1ms global kernel scheduler granularity across the process lifetime
struct GlobalTimerPeriod {
    GlobalTimerPeriod() { timeBeginPeriod(1); }
    ~GlobalTimerPeriod() { timeEndPeriod(1); }
};
inline GlobalTimerPeriod s_global_timer_period;

struct Timer {
    // Windows 10 1803+: private auto-reset high-resolution timer.
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, 0x2, TIMER_MODIFY_STATE | SYNCHRONIZE);
    ~Timer() {
        if (handle) CloseHandle(handle);
    }
    Timer() = default;
    Timer(const Timer&) = delete;
};
} // namespace rbrew_host_timing
#endif

inline void rbrew_host_sleep_until(std::chrono::steady_clock::time_point deadline) {
    // Spin only for the final stretch to avoid scheduler overshoot. Earlier
    // fallbacks (no high-resolution timer, timer error, non-Windows) spun for
    // the whole wait and kept a core busy; they sleep for the bulk again.
    constexpr auto kSpin = std::chrono::microseconds(600);
#if defined(_WIN32)
    thread_local rbrew_host_timing::Timer timer;
    if (timer.handle) {
        for (auto now = std::chrono::steady_clock::now(); deadline - now > kSpin;
             now = std::chrono::steady_clock::now()) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now - kSpin).count();
            LARGE_INTEGER due{};
            due.QuadPart = -((ns + 99) / 100); // relative, 100 ns units
            if (!SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE) ||
                WaitForSingleObject(timer.handle, INFINITE) != WAIT_OBJECT_0)
                break;
        }
    }
#endif
    // Bulk sleep where the timer was unavailable or failed (~1 ms granularity).
    if (deadline - std::chrono::steady_clock::now() > kSpin + std::chrono::milliseconds(1))
        std::this_thread::sleep_until(deadline - kSpin - std::chrono::milliseconds(1));
    while (std::chrono::steady_clock::now() < deadline) {
#if defined(_WIN32)
        _mm_pause();
#else
        std::this_thread::yield();
#endif
    }
}

// Busy-wait cap for a host thread waiting on another host thread (render
// queue, recorder, shader translator). Spinning only pays while a spare
// logical CPU exists: the port keeps the game thread, render thread, Vulkan
// recorder, audio and translation helper runnable at once, so on a small CPU
// a millisecond spin takes the core the awaited thread or the game needs.
// Default: each wait's own budget on >= 8 logical CPUs, else 50 us before it
// sleeps. RBREW_HOST_SPIN=full keeps every budget, =<us> caps it.
inline std::chrono::microseconds rbrew_host_spin_cap() {
    static const std::chrono::microseconds cap = [] {
        constexpr auto kFull = std::chrono::microseconds::max();
        if (const char* v = std::getenv("RBREW_HOST_SPIN"); v && *v)
            return std::strcmp(v, "full") == 0 ? kFull : std::chrono::microseconds(std::strtoull(v, nullptr, 10));
        unsigned cpus = 0;
#if defined(_WIN32)
        DWORD_PTR process = 0, system = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system))
            for (; process; process &= process - 1) ++cpus;
#endif
        if (!cpus) cpus = std::thread::hardware_concurrency();
        return cpus >= 8 ? kFull : std::chrono::microseconds(50);
    }();
    return cap;
}
// The Vulkan recorder's ~0.1 ms pause loops stay as they are: capping them
// slowed a 2-CPU draw benchmark by 10% (sleep/wake per published chunk).
inline std::chrono::microseconds rbrew_host_spin_budget(std::chrono::microseconds full) {
    return std::min(full, rbrew_host_spin_cap());
}

inline void rbrew_host_sleep_for(uint64_t microseconds) {
    rbrew_host_sleep_until(std::chrono::steady_clock::now() + std::chrono::microseconds(microseconds));
}
