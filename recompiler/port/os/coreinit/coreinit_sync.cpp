// coreinit synchronization primitives
//
// Each guest sync object is backed by a host struct kept in a hash-map keyed
// by its guest address.  Using host condition variables means worker threads
// block without busy-spinning.
//
// OSEvent  — signaled/non-signaled flag with blocking wait
// OSSemaphore — counting semaphore with blocking wait
// OSCond   — host std::condition_variable (per guest address)
// OSMutex  — host std::mutex (per guest address, recursive-safe wrapper)
// OSFastMutex — mapped to the same OSMutex table for simplicity

#include "../os_common.h"
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <cstdio>
#include <array>
#include <memory>

// Objects have process lifetime and are never replaced or erased. Cache their
// stable addresses per host thread; each object's mutex/CV still enforces guest
// synchronization. Misses retain the registry lock for concurrent initialization.
template <class T>
static T* sync_object(uint32_t address, std::mutex& registry_lock, std::unordered_map<uint32_t, T*>& registry) {
    struct Entry {
        uint32_t address = 0;
        T* object = nullptr;
    };
    thread_local std::array<Entry, 64> recent{};
    auto& entry = recent[((address >> 2) ^ (address >> 10)) & (recent.size() - 1)];
    if (entry.object && entry.address == address) return entry.object;
    std::lock_guard lock(registry_lock);
    auto it = registry.find(address);
    T* object;
    if (it != registry.end())
        object = it->second;
    else {
        auto owned = std::make_unique<T>();
        registry.emplace(address, owned.get());
        object = owned.release();
    }
    entry = {address, object};
    return object;
}

// ---------------------------------------------------------------------------
// OSMutex — per-address recursive mutex
// (Wii U OSMutex is non-recursive but games sometimes lock the same mutex
//  from the same thread; use a recursive_mutex to avoid deadlocks.)
// ---------------------------------------------------------------------------
struct MutexImpl {
    std::recursive_mutex m;
};
static std::mutex s_mutex_map_lock;
static std::unordered_map<uint32_t, MutexImpl*> s_mutexes;

static MutexImpl* get_mutex(uint32_t ptr) {
    return sync_object(ptr, s_mutex_map_lock, s_mutexes);
}

static void OSInitMutex(CPUState* cpu) {
    get_mutex(ARG0);
}
static void OSLockMutex(CPUState* cpu) {
    get_mutex(ARG0)->m.lock();
}
static void OSUnlockMutex(CPUState* cpu) {
    get_mutex(ARG0)->m.unlock();
}
static void OSTryLockMutex(CPUState* cpu) {
    RET = get_mutex(ARG0)->m.try_lock() ? 1u : 0u;
}

// OSFastMutex — same backing as OSMutex
static void OSFastMutex_Init(CPUState* cpu) {
    get_mutex(ARG0);
    if (ARG0 && rbrew_memory_accessible(MEM, ARG0, 0x20u)) memset(MEM + ARG0, 0, 0x20);
}
static void OSFastMutex_Lock(CPUState* cpu) {
    get_mutex(ARG0)->m.lock();
}
static void OSFastMutex_Unlock(CPUState* cpu) {
    get_mutex(ARG0)->m.unlock();
}
static void OSFastMutex_TryLock(CPUState* cpu) {
    RET = get_mutex(ARG0)->m.try_lock() ? 1u : 0u;
}

// ---------------------------------------------------------------------------
// OSCond — per-address condition variable, paired with a recursive_mutex
// ---------------------------------------------------------------------------
struct CondImpl {
    std::mutex mtx; // separate lock for the CV
    std::condition_variable cv;
};
static std::mutex s_cond_map_lock;
static std::unordered_map<uint32_t, CondImpl*> s_condvars;

static CondImpl* get_cond(uint32_t ptr) {
    return sync_object(ptr, s_cond_map_lock, s_condvars);
}

static void OSInitCond(CPUState* cpu) {
    get_cond(ARG0);
}
// Taking the CV lock orders a signal after a waiter that already released the
// guest mutex has started waiting; without it that wakeup could be lost.
static void OSSignalCond(CPUState* cpu) {
    CondImpl* cd = get_cond(ARG0);
    { std::lock_guard<std::mutex> lk(cd->mtx); }
    cd->cv.notify_one();
}
static void OSBroadcastCond(CPUState* cpu) {
    CondImpl* cd = get_cond(ARG0);
    { std::lock_guard<std::mutex> lk(cd->mtx); }
    cd->cv.notify_all();
}

// OSWaitCond(cond, mutex)
// The game calls this while holding OSMutex (ARG1).  We must unlock it,
// wait, then relock — matching POSIX pthread_cond_wait semantics.
static void OSWaitCond(CPUState* cpu) {
    uint32_t cond_ptr = ARG0;
    uint32_t mutex_ptr = ARG1;

    CondImpl* cd = get_cond(cond_ptr);
    MutexImpl* md = get_mutex(mutex_ptr);

    // Unlock the game mutex, wait on our CV, then relock.
    std::unique_lock<std::mutex> lk(cd->mtx);
    md->m.unlock();
    cd->cv.wait(lk);
    lk.unlock(); // a signaler may hold the guest mutex while taking cd->mtx
    md->m.lock();
}

// ---------------------------------------------------------------------------
// OSSemaphore — counting semaphore with blocking wait
// ---------------------------------------------------------------------------
struct SemImpl {
    std::mutex mtx;
    std::condition_variable cv;
    int32_t count = 0;
};
static std::mutex s_sem_map_lock;
static std::unordered_map<uint32_t, SemImpl*> s_semaphores;

static SemImpl* get_sem(uint32_t ptr) {
    return sync_object(ptr, s_sem_map_lock, s_semaphores);
}

// OSInitSemaphore(sem, count)
static void OSInitSemaphore(CPUState* cpu) {
    SemImpl* s = get_sem(ARG0);
    std::lock_guard<std::mutex> lk(s->mtx);
    s->count = (int32_t)ARG1;
}

// OSWaitSemaphore — block until count > 0, then decrement
static void OSWaitSemaphore(CPUState* cpu) {
    SemImpl* s = get_sem(ARG0);
    std::unique_lock<std::mutex> lk(s->mtx);
    s->cv.wait(lk, [s] { return s->count > 0; });
    s->count--;
}

// OSSignalSemaphore — increment count + wake one waiter
static void OSSignalSemaphore(CPUState* cpu) {
    SemImpl* s = get_sem(ARG0);
    {
        std::lock_guard<std::mutex> lk(s->mtx);
        s->count++;
    }
    s->cv.notify_one();
}

// ---------------------------------------------------------------------------
// OSEvent — manual-reset event flag with blocking wait
// ---------------------------------------------------------------------------
struct EventImpl {
    std::mutex mtx;
    std::condition_variable cv;
    bool signaled = false;
    bool auto_reset = false; // mode 1 = autoreset
};
static std::mutex s_event_map_lock;
static std::unordered_map<uint32_t, EventImpl*> s_events;

static EventImpl* get_event(uint32_t ptr) {
    return sync_object(ptr, s_event_map_lock, s_events);
}

// OSInitEvent(event, initialState, mode)
// mode: 0 = manual-reset, 1 = auto-reset
static void OSInitEvent(CPUState* cpu) {
    EventImpl* e = get_event(ARG0);
    std::lock_guard<std::mutex> lk(e->mtx);
    e->signaled = (ARG1 != 0);
    e->auto_reset = (ARG2 != 0);
}

// OSSignalEvent — signal + wake all (or one for auto-reset)
static void OSSignalEvent(CPUState* cpu) {
    EventImpl* e = get_event(ARG0);
    {
        std::lock_guard<std::mutex> lk(e->mtx);
        e->signaled = true;
    }
    // Wake all waiters; auto-reset variant wakes one and clears in OSWaitEvent
    e->cv.notify_all();
}

// OSResetEvent — clear the signal
static void OSResetEvent(CPUState* cpu) {
    EventImpl* e = get_event(ARG0);
    std::lock_guard<std::mutex> lk(e->mtx);
    e->signaled = false;
}

// OSWaitEvent — block until signaled
static void OSWaitEvent(CPUState* cpu) {
    EventImpl* e = get_event(ARG0);
    std::unique_lock<std::mutex> lk(e->mtx);
    e->cv.wait(lk, [e] { return e->signaled; });
    if (e->auto_reset) e->signaled = false;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
#include "coreinit_addrs.h"
void coreinit_sync_register(CPUState* cpu) {
#define REG(name) rbrew_register_func(cpu, ADDR_##name, name)
    REG(OSInitMutex);
    REG(OSLockMutex);
    REG(OSUnlockMutex);
    REG(OSTryLockMutex);
    REG(OSInitSemaphore);
    REG(OSWaitSemaphore);
    REG(OSSignalSemaphore);
    REG(OSInitEvent);
    REG(OSWaitEvent);
    REG(OSSignalEvent);
    REG(OSResetEvent);
    REG(OSInitCond);
    REG(OSWaitCond);
    REG(OSSignalCond);
    rbrew_register_func(cpu, ADDR_OSFastMutex_Init, OSFastMutex_Init);
    rbrew_register_func(cpu, ADDR_OSFastMutex_Lock, OSFastMutex_Lock);
    rbrew_register_func(cpu, ADDR_OSFastMutex_Unlock, OSFastMutex_Unlock);
    rbrew_register_func(cpu, ADDR_OSFastMutex_TryLock, OSFastMutex_TryLock);
#undef REG
}
