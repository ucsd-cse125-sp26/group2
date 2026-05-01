/// @file HitboxHistorySystem.cpp
/// @brief Push the current frame's `HitboxInstance` into each entity's
///        `HitboxHistory` ring (Phase 6 scaffolding).

#include "server/systems/HitboxHistorySystem.hpp"

#include "ecs/components/Hitbox.hpp"
#include "ecs/components/HitboxHistory.hpp"

namespace systems
{

void pushHitboxHistory(Registry& registry, uint32_t serverTick)
{
    auto view = registry.view<HitboxInstance>();
    view.each([&](entt::entity entity, const HitboxInstance& hb) {
        // get_or_emplace: first-tick entities get a freshly zeroed ring.
        // The default-constructed `HitboxHistorySample` slots have tick=0
        // and an empty capsules vector — sample.tick==0 doubles as the
        // "unset" sentinel since serverTick == 0 is reserved (the very
        // first call passes a positive tick).
        auto& hist = registry.get_or_emplace<HitboxHistory>(entity);

        auto& slot = hist.ring[hist.head];
        slot.tick = serverTick;
        // Vector copy is intentional: the ring holds an independent
        // snapshot. `HitboxInstance::capsules` will be overwritten on
        // the next animation tick; we want the historical state frozen
        // here. Capsules per entity are ~12 × 24 B ≈ 290 B, so a copy
        // every tick is cheap.
        slot.capsules = hb.capsules;

        hist.head = (hist.head + 1) % HitboxHistory::k_capacity;
        if (hist.count < HitboxHistory::k_capacity)
            ++hist.count;
    });
}

} // namespace systems
