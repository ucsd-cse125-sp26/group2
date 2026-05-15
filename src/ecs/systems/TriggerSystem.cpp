/// @file TriggerSystem.cpp
/// @brief Implementation of the trigger-volume overlap system.

#include "ecs/systems/TriggerSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/TriggerVolume.hpp"
#include "ecs/physics/CollisionEvents.hpp"

#include <algorithm>
#include <cstdint>
#include <glm/vec3.hpp>
#include <vector>

namespace systems
{

namespace
{

/// @brief Persistent per-trigger state — sorted list of entity ids that
/// overlapped the trigger on the previous tick.  Maintained by `runTriggers`
/// and stored on the trigger entity so destruction cleans it up automatically.
struct TriggerOverlapSet
{
    std::vector<entt::entity> entities; ///< Sorted by underlying entity id.
};

/// @brief AABB-vs-AABB overlap test (used for both static & moving entities).
[[nodiscard]] inline bool overlapsAABB(
    const glm::vec3& aPos, const glm::vec3& aHE, const glm::vec3& bPos, const glm::vec3& bHE) noexcept
{
    if (std::abs(aPos.x - bPos.x) > aHE.x + bHE.x)
        return false;
    if (std::abs(aPos.y - bPos.y) > aHE.y + bHE.y)
        return false;
    if (std::abs(aPos.z - bPos.z) > aHE.z + bHE.z)
        return false;
    return true;
}

} // namespace

void runTriggers(Registry& registry, bool isPredictedClient)
{
    // First, gather all NON-trigger entities once.  Triggers do not generate
    // events against each other.  Storing in a flat vector lets us iterate
    // quickly per-trigger and produces a deterministic stable order.
    struct Candidate
    {
        entt::entity entity;
        glm::vec3 position;
        glm::vec3 halfExtents;
        uint32_t layerBits;
    };

    static thread_local std::vector<Candidate> candidates;
    candidates.clear();

    auto candView = registry.view<Position, CollisionShape>(entt::exclude<TriggerVolume>);
    for (auto e : candView) {
        const auto& pos = candView.get<Position>(e);
        const auto& shp = candView.get<CollisionShape>(e);
        uint32_t bits = 0xFFFFFFFFu;
        if (const auto* layer = registry.try_get<CollisionLayer>(e))
            bits = layer->bits;
        candidates.push_back({e, pos.value, shp.halfExtents, bits});
    }
    // Sort by entity id for deterministic iteration regardless of the
    // archetype-internal order.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return entt::to_integral(a.entity) < entt::to_integral(b.entity);
    });

    // Per-trigger pass.  Sort triggers by id too, so event order is fixed
    // even when entt rearranges its archetypes.
    static thread_local std::vector<entt::entity> triggers;
    triggers.clear();
    auto trigView = registry.view<TriggerVolume, Position, CollisionShape>();
    for (auto e : trigView)
        triggers.push_back(e);
    std::sort(triggers.begin(), triggers.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) < entt::to_integral(b);
    });

    static thread_local std::vector<entt::entity> currentOverlap;

    for (entt::entity tr : triggers) {
        const auto& tv = trigView.get<TriggerVolume>(tr);
        const auto& trPos = trigView.get<Position>(tr);
        const auto& trShp = trigView.get<CollisionShape>(tr);

        const bool suppress = isPredictedClient && !tv.fireOnPredictedClient;

        // Build the current overlap set (always — even if events are
        // suppressed, the persistent state must stay accurate for the next
        // tick's diff).
        currentOverlap.clear();
        for (const Candidate& c : candidates) {
            if ((c.layerBits & tv.layerMask) == 0u)
                continue;
            if (!overlapsAABB(trPos.value, trShp.halfExtents, c.position, c.halfExtents))
                continue;
            currentOverlap.push_back(c.entity);
        }
        // candidates is already sorted by id, so currentOverlap is too.

        // Diff against the persistent last-tick set.
        auto& state = registry.get_or_emplace<TriggerOverlapSet>(tr);

        // Merge-walk the two sorted vectors to emit Enter / Stay / Exit.
        size_t i = 0;
        size_t j = 0;
        while (i < state.entities.size() && j < currentOverlap.size()) {
            const auto a = entt::to_integral(state.entities[i]);
            const auto b = entt::to_integral(currentOverlap[j]);
            if (a == b) {
                if (!suppress)
                    physics::events::pushTriggerEvent(
                        {tr, currentOverlap[j], physics::events::TriggerEventType::Stay});
                ++i;
                ++j;
            } else if (a < b) {
                if (!suppress)
                    physics::events::pushTriggerEvent(
                        {tr, state.entities[i], physics::events::TriggerEventType::Exit});
                ++i;
            } else {
                if (!suppress)
                    physics::events::pushTriggerEvent(
                        {tr, currentOverlap[j], physics::events::TriggerEventType::Enter});
                ++j;
            }
        }
        for (; i < state.entities.size(); ++i) {
            if (!suppress)
                physics::events::pushTriggerEvent(
                    {tr, state.entities[i], physics::events::TriggerEventType::Exit});
        }
        for (; j < currentOverlap.size(); ++j) {
            if (!suppress)
                physics::events::pushTriggerEvent(
                    {tr, currentOverlap[j], physics::events::TriggerEventType::Enter});
        }

        // Swap into persistent state — moves the storage so we don't reallocate.
        state.entities = currentOverlap;
    }
}

} // namespace systems
