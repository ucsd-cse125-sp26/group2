/// @file CollisionEvents.cpp
/// @brief Implementation of the per-tick collision event queue.

#include "ecs/physics/CollisionEvents.hpp"

#include <mutex>
#include <vector>

namespace physics::events
{

namespace
{

struct ThreadLocalBuffer
{
    std::vector<TriggerEvent> events;
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

thread_local TlbRegistrar tlsRegistrar;

[[nodiscard]] ThreadLocalBuffer& localBuffer() noexcept
{
    return tlsRegistrar.buffer;
}

std::vector<TriggerEvent> frontBuffer;

} // namespace

void beginTick() noexcept
{
    frontBuffer.clear();

    std::lock_guard<std::mutex> lk(tlbMutex);
    for (ThreadLocalBuffer* tlb : tlbs) {
        if (tlb->events.empty())
            continue;
        frontBuffer.insert(frontBuffer.end(), tlb->events.begin(), tlb->events.end());
        tlb->events.clear();
    }
}

void pushTriggerEvent(const TriggerEvent& e) noexcept
{
    localBuffer().events.push_back(e);
}

std::span<const TriggerEvent> triggerEvents() noexcept
{
    return {frontBuffer.data(), frontBuffer.size()};
}

} // namespace physics::events
