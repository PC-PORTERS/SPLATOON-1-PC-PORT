#pragma once
#include <cstdint>

// One shared 60000/1001-Hz scan-out clock. Calls/cores do not consume
// individual vblanks: every waiter at the same time targets the same event.
constexpr uint64_t gx2_vblank_tick(uint64_t index, uint32_t timer_frequency) {
    const uint64_t cycle = uint64_t(timer_frequency) * 1001;
    return (index / 60000) * cycle + (index % 60000) * cycle / 60000;
}
constexpr uint64_t gx2_next_vblank_tick(uint64_t now, uint32_t timer_frequency) {
    const uint64_t cycle = uint64_t(timer_frequency) * 1001;
    uint64_t index = (now / cycle) * 60000 + (now % cycle) * 60000 / cycle + 1;
    uint64_t target = gx2_vblank_tick(index, timer_frequency);
    if (target <= now) target = gx2_vblank_tick(index + 1, timer_frequency);
    return target;
}

constexpr uint64_t gx2_current_vblank_tick(uint64_t now, uint32_t frequency) {
    const uint64_t cycle = uint64_t(frequency) * 1001;
    uint64_t index = (now / cycle) * 60000 + (now % cycle) * 60000 / cycle;
    if (gx2_vblank_tick(index + 1, frequency) <= now) ++index;
    return gx2_vblank_tick(index, frequency);
}

// Each calling thread observes the same free-running clock. Work that spans
// a vblank can consume that already elapsed event without another refresh
// wait. Never publish a future event, accumulate drift, or waive a second
// wait merely because it arrived within a fixed 4 ms window.
struct Gx2VblankWaiter {
    uint64_t observed = 0;
    bool initialized = false;
    uint64_t target(uint64_t now, uint32_t frequency) const {
        const uint64_t current = gx2_current_vblank_tick(now, frequency);
        return initialized && current > observed ? current : gx2_next_vblank_tick(now, frequency);
    }
    void complete(uint64_t now, uint32_t frequency) {
        observed = gx2_current_vblank_tick(now, frequency);
        initialized = true;
    }
};
