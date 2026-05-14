/// @file DebugCollisionDraw.cpp
/// @brief Implementation of the thread-local contact debug accumulator.

#include "ecs/physics/DebugCollisionDraw.hpp"

#include <atomic>
#include <mutex>
#include <vector>

namespace physics::debug
{

namespace
{

// ──────────────────────────────────────────────────────────────────────────
// Recording flag.  Read on every `pushContact` call; relaxed memory order
// since the buffer never feeds back into the simulation — an out-of-order
// observation just delays the first/last recorded contact by one tick.
// ──────────────────────────────────────────────────────────────────────────
std::atomic<bool> enabledFlag{false};

// ──────────────────────────────────────────────────────────────────────────
// Thread-local buffers.  Each writer thread accumulates into its own vector,
// avoiding cross-thread contention on the hot path.  `beginFrame()` drains
// every registered TLB into the front buffer and clears them.
//
// Registration is automatic: a thread_local Registrar's constructor adds
// the buffer to a mutex-protected list on first use, the destructor removes
// it on thread exit (e.g. when a TBB worker is reaped).
// ──────────────────────────────────────────────────────────────────────────
struct ThreadLocalBuffer
{
    std::vector<Contact> contacts;
};

std::mutex tlbMutex;
std::vector<ThreadLocalBuffer*> tlbs;

struct TlbRegistrar
{
    ThreadLocalBuffer buffer;

    TlbRegistrar()
    {
        std::lock_guard<std::mutex> lk(tlbMutex);
        tlbs.push_back(&buffer);
    }
    ~TlbRegistrar()
    {
        std::lock_guard<std::mutex> lk(tlbMutex);
        std::erase(tlbs, &buffer);
    }
};

// One registrar per thread.  First reference materialises the registration.
thread_local TlbRegistrar tlsRegistrar;

[[nodiscard]] ThreadLocalBuffer& localBuffer() noexcept
{
    return tlsRegistrar.buffer;
}

// ──────────────────────────────────────────────────────────────────────────
// Front buffer — coalesced view drained from all TLBs once per frame.
// Read by the debug-overlay code on the main thread.
// ──────────────────────────────────────────────────────────────────────────
std::vector<Contact> frontBuffer;

} // namespace

void pushContact(const Contact& c) noexcept
{
    if (!enabledFlag.load(std::memory_order_relaxed))
        return;
    localBuffer().contacts.push_back(c);
}

void pushSweepContact(glm::vec3 point, glm::vec3 normal, ContactSource source, uint32_t primitiveIndex) noexcept
{
    if (!enabledFlag.load(std::memory_order_relaxed))
        return;
    Contact c;
    c.point = point;
    c.normal = normal;
    c.depth = 0.0f;
    c.source = source;
    c.primitiveIndex = primitiveIndex;
    localBuffer().contacts.push_back(c);
}

void pushDepenContact(
    glm::vec3 point, glm::vec3 normal, float depth, ContactSource source, uint32_t primitiveIndex) noexcept
{
    if (!enabledFlag.load(std::memory_order_relaxed))
        return;
    Contact c;
    c.point = point;
    c.normal = normal;
    c.depth = depth;
    c.source = source;
    c.primitiveIndex = primitiveIndex;
    localBuffer().contacts.push_back(c);
}

void setEnabled(bool on) noexcept
{
    enabledFlag.store(on, std::memory_order_relaxed);
}

bool isEnabled() noexcept
{
    return enabledFlag.load(std::memory_order_relaxed);
}

void beginFrame() noexcept
{
    frontBuffer.clear();

    // Snapshot the TLB list under the mutex to serialise with thread
    // creation/destruction.  No other thread is writing into its TLB
    // at this point — `beginFrame()` is the explicit single-writer barrier.
    std::lock_guard<std::mutex> lk(tlbMutex);
    for (ThreadLocalBuffer* tlb : tlbs) {
        if (tlb->contacts.empty())
            continue;
        frontBuffer.insert(frontBuffer.end(), tlb->contacts.begin(), tlb->contacts.end());
        tlb->contacts.clear();
    }
}

std::span<const Contact> contacts() noexcept
{
    return {frontBuffer.data(), frontBuffer.size()};
}

} // namespace physics::debug
